/*
 * Minimal ILI9342 display driver for M5Stack Core2.
 * Raw SPI commands, no esp_lcd panel dependency.
 * 320x240, 16-bit color, DMA line buffer.
 */

#include "display.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"

static const char *TAG = "display";

/* M5Stack Core2 ILI9342C pinout (verified from working ruki-firmware) */
#define LCD_HOST       SPI3_HOST
#define LCD_PIXEL_CLK_HZ (40 * 1000 * 1000)
#define LCD_WIDTH      320
#define LCD_HEIGHT     240
#define LCD_LINES_BUF  8
#define LCD_BUF_SIZE   (LCD_WIDTH * LCD_LINES_BUF * 2)

#define LCD_CS         14
#define LCD_DC         27
#define LCD_MOSI       23
#define LCD_SCLK       18
#define LCD_MISO       19
#define LCD_BL         32

/* AXP192 PMIC (M5Stack Core2) - powers and resets the display */
#define AXP192_ADDR    0x34
#define I2C_PORT        I2C_NUM_0
#define I2C_SDA        21
#define I2C_SCL        22

static void axp192_write_reg(uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AXP192_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
}

static uint8_t axp192_read_reg(uint8_t reg)
{
    uint8_t val = 0;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AXP192_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AXP192_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &val, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return val;
}

static void axp192_init(void)
{
    ESP_LOGI(TAG, "init AXP192 PMIC (exact ruki-firmware sequence)");

    i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    i2c_param_config(I2C_PORT, &i2c_cfg);
    i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);

    uint8_t ver = axp192_read_reg(0x03);
    ESP_LOGI(TAG, "AXP192 ID: 0x%02x", ver);

    /* Exact AXP192 init from working ruki-firmware M5Core2 library */

    /* 1. VBUS limit off */
    axp192_write_reg(0x30, (axp192_read_reg(0x30) & 0x04) | 0x02);

    /* 2. GPIO1: open-drain output */
    axp192_write_reg(0x92, axp192_read_reg(0x92) & 0xF8);

    /* 3. GPIO2: open-drain output */
    axp192_write_reg(0x93, axp192_read_reg(0x93) & 0xF8);

    /* 4. RTC battery charging */
    axp192_write_reg(0x35, (axp192_read_reg(0x35) & 0x1C) | 0xA2);

    /* 5. DC-DC1 = 3.35V (ESP32) - reg 0x26 */
    axp192_write_reg(0x26, (axp192_read_reg(0x26) & 0x80) | 0x6A);

    /* 6. DC-DC3 = 2.8V (LCD backlight) - reg 0x27 */
    axp192_write_reg(0x27, (axp192_read_reg(0x27) & 0x80) | 0x54);

    /* 7. LDO2 = 3.3V (LCD logic + SD) - reg 0x28[7:4] */
    axp192_write_reg(0x28, (axp192_read_reg(0x28) & 0x0F) | (0x0F << 4));

    /* 8. LDO3 = 2.0V (vibrator) - reg 0x28[3:0] */
    axp192_write_reg(0x28, (axp192_read_reg(0x28) & 0xF0) | 0x02);

    /* 9. Enable LDO2 */
    axp192_write_reg(0x12, axp192_read_reg(0x12) | (1 << 2));

    /* 10. Enable DC-DC3 */
    axp192_write_reg(0x12, axp192_read_reg(0x12) | (1 << 1));

    /* 11. LED on */
    axp192_write_reg(0x94, axp192_read_reg(0x94) & 0xFD);

    /* 12. Charge current 100mA */
    axp192_write_reg(0x33, axp192_read_reg(0x33) & 0xF0);

    /* 13. GPIO4 config */
    axp192_write_reg(0x95, (axp192_read_reg(0x95) & 0x72) | 0x84);

    /* 14. Power control */
    axp192_write_reg(0x36, 0x4C);

    /* 15. Enable all ADCs */
    axp192_write_reg(0x82, 0xFF);

    /* 16. LCD reset via AXP192 GPIO4 (reg 0x96 bit 1) */
    axp192_write_reg(0x96, axp192_read_reg(0x96) & ~0x02);
    vTaskDelay(pdMS_TO_TICKS(100));
    axp192_write_reg(0x96, axp192_read_reg(0x96) | 0x02);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 17. Peripherals power (EXTEN 5V boost) */
    axp192_write_reg(0x10, axp192_read_reg(0x10) | 0x04);

    ESP_LOGI(TAG, "AXP192 init done, verifying...");
    ESP_LOGI(TAG, "  reg 0x12: 0x%02x", axp192_read_reg(0x12));
    ESP_LOGI(TAG, "  reg 0x26: 0x%02x", axp192_read_reg(0x26));
    ESP_LOGI(TAG, "  reg 0x27: 0x%02x", axp192_read_reg(0x27));
    ESP_LOGI(TAG, "  reg 0x28: 0x%02x", axp192_read_reg(0x28));
    ESP_LOGI(TAG, "  reg 0x96: 0x%02x", axp192_read_reg(0x96));
}

static spi_device_handle_t s_spi;
static uint16_t *s_linebuf;

/* Send command byte to ILI9342 */
static void lcd_cmd(uint8_t cmd)
{
    gpio_set_level(LCD_DC, 0);  /* Command mode */
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(s_spi, &t);
    gpio_set_level(LCD_DC, 1);  /* Back to data mode */
}

/* Send data bytes */
static void lcd_data(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(s_spi, &t);
}

/* Send single data byte */
static void lcd_data1(uint8_t val)
{
    lcd_data(&val, 1);
}

/* Set address window */
static void lcd_set_window(int x0, int y0, int x1, int y1)
{
    lcd_cmd(0x2A);  /* Column Address Set */
    uint8_t cols[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0,
                         (uint8_t)(x1 >> 8), (uint8_t)x1 };
    lcd_data(cols, 4);

    lcd_cmd(0x2B);  /* Page Address Set */
    uint8_t rows[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0,
                         (uint8_t)(y1 >> 8), (uint8_t)y1 };
    lcd_data(rows, 4);

    lcd_cmd(0x2C);  /* Memory Write */
}

/* ILI9342C init sequence (from working ruki-firmware M5Core2 library) */
static void lcd_init_sequence(void)
{
    /* Reset already done by AXP192 GPIO4 */
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_cmd(0xC8);          /* Undocumented gamma positive */
    uint8_t g1[] = {0xFF, 0x93, 0x42};
    lcd_data(g1, 3);

    lcd_cmd(0xC0);          /* Power Control 1 */
    uint8_t pc1[] = {0x12, 0x12};
    lcd_data(pc1, 2);

    lcd_cmd(0xC1);          /* Power Control 2 */
    lcd_data1(0x03);

    lcd_cmd(0xB0);          /* RGB interface control */
    lcd_data1(0xE0);

    lcd_cmd(0xF6);          /* Gate control */
    uint8_t gc[] = {0x00, 0x01, 0x01};
    lcd_data(gc, 3);

    lcd_cmd(0x36);          /* Memory Access Control */
    lcd_data1(0x08);        /* BGR=1, landscape rotation 1 */

    lcd_cmd(0x3A);          /* Pixel Format */
    lcd_data1(0x55);        /* 16-bit RGB565 */

    lcd_cmd(0xB6);          /* Display Function Control */
    uint8_t df[] = {0x08, 0x82, 0x27};
    lcd_data(df, 3);

    lcd_cmd(0xE0);          /* Positive Gamma Correction */
    uint8_t gp[] = {0x00,0x0C,0x11,0x04,0x11,0x08,0x37,
                    0x89,0x4C,0x06,0x0C,0x0A,0x2E,0x34,0x0F};
    lcd_data(gp, 15);

    lcd_cmd(0xE1);          /* Negative Gamma Correction */
    uint8_t gn[] = {0x00,0x0B,0x11,0x05,0x13,0x09,0x33,
                    0x67,0x48,0x07,0x0E,0x0B,0x2E,0x33,0x0F};
    lcd_data(gn, 15);

    lcd_cmd(0x11);          /* Sleep Out */
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_cmd(0x29);          /* Display ON */
    vTaskDelay(pdMS_TO_TICKS(50));

    lcd_cmd(0x21);          /* Display Inversion ON */
}

/* Font 5x7 ASCII */
static const uint8_t FONT_5X7[][5] = {
    [0]  = {0x00,0x00,0x00,0x00,0x00}, [1]  = {0x00,0x00,0x5F,0x00,0x00},
    [2]  = {0x00,0x07,0x00,0x07,0x00}, [3]  = {0x14,0x7F,0x14,0x7F,0x14},
    [4]  = {0x24,0x2A,0x7F,0x2A,0x12}, [5]  = {0x23,0x13,0x08,0x64,0x62},
    [6]  = {0x36,0x49,0x55,0x22,0x50}, [7]  = {0x00,0x05,0x03,0x00,0x00},
    [8]  = {0x00,0x1C,0x22,0x41,0x00}, [9]  = {0x00,0x41,0x22,0x1C,0x00},
    [10] = {0x14,0x08,0x3E,0x08,0x14}, [11] = {0x08,0x08,0x3E,0x08,0x08},
    [12] = {0x00,0x50,0x30,0x00,0x00}, [13] = {0x08,0x08,0x08,0x08,0x08},
    [14] = {0x00,0x60,0x60,0x00,0x00}, [15] = {0x20,0x10,0x08,0x04,0x02},
    [16] = {0x3E,0x51,0x49,0x45,0x3E}, [17] = {0x00,0x42,0x7F,0x40,0x00},
    [18] = {0x42,0x61,0x51,0x49,0x46}, [19] = {0x21,0x41,0x45,0x4B,0x31},
    [20] = {0x18,0x14,0x12,0x7F,0x10}, [21] = {0x27,0x45,0x45,0x45,0x39},
    [22] = {0x3C,0x4A,0x49,0x49,0x30}, [23] = {0x01,0x71,0x09,0x05,0x03},
    [24] = {0x36,0x49,0x49,0x49,0x36}, [25] = {0x06,0x49,0x49,0x29,0x1E},
    [26] = {0x00,0x36,0x36,0x00,0x00}, [27] = {0x00,0x56,0x36,0x00,0x00},
    [28] = {0x08,0x14,0x22,0x41,0x00}, [29] = {0x14,0x14,0x14,0x14,0x14},
    [30] = {0x00,0x41,0x22,0x14,0x08}, [31] = {0x02,0x01,0x51,0x09,0x06},
    [32] = {0x32,0x49,0x79,0x41,0x3E}, [33] = {0x7E,0x11,0x11,0x11,0x7E},
    [34] = {0x7F,0x49,0x49,0x49,0x36}, [35] = {0x3E,0x41,0x41,0x41,0x22},
    [36] = {0x7F,0x41,0x41,0x22,0x1C}, [37] = {0x7F,0x49,0x49,0x49,0x41},
    [38] = {0x7F,0x09,0x09,0x09,0x01}, [39] = {0x3E,0x41,0x49,0x49,0x7A},
    [40] = {0x7F,0x08,0x08,0x08,0x7F}, [41] = {0x00,0x41,0x7F,0x41,0x00},
    [42] = {0x20,0x40,0x41,0x3F,0x01}, [43] = {0x7F,0x08,0x14,0x22,0x41},
    [44] = {0x7F,0x40,0x40,0x40,0x40}, [45] = {0x7F,0x02,0x0C,0x02,0x7F},
    [46] = {0x7F,0x04,0x08,0x10,0x7F}, [47] = {0x3E,0x41,0x41,0x41,0x3E},
    [48] = {0x7F,0x09,0x09,0x09,0x06}, [49] = {0x3E,0x41,0x51,0x21,0x5E},
    [50] = {0x7F,0x09,0x19,0x29,0x46}, [51] = {0x46,0x49,0x49,0x49,0x31},
    [52] = {0x01,0x01,0x7F,0x01,0x01}, [53] = {0x3F,0x40,0x40,0x40,0x3F},
    [54] = {0x1F,0x20,0x40,0x20,0x1F}, [55] = {0x3F,0x40,0x38,0x40,0x3F},
    [56] = {0x63,0x14,0x08,0x14,0x63}, [57] = {0x07,0x08,0x70,0x08,0x07},
    [58] = {0x61,0x51,0x49,0x45,0x43}, [59] = {0x00,0x7F,0x41,0x41,0x00},
    [60] = {0x02,0x04,0x08,0x10,0x20}, [61] = {0x00,0x41,0x41,0x7F,0x00},
    [62] = {0x04,0x02,0x01,0x02,0x04}, [63] = {0x40,0x40,0x40,0x40,0x40},
    [64] = {0x00,0x01,0x02,0x04,0x00}, [65] = {0x20,0x54,0x54,0x54,0x78},
    [66] = {0x7F,0x48,0x44,0x44,0x38}, [67] = {0x38,0x44,0x44,0x44,0x20},
    [68] = {0x38,0x44,0x44,0x48,0x7F}, [69] = {0x38,0x54,0x54,0x54,0x18},
    [70] = {0x08,0x7E,0x09,0x01,0x02}, [71] = {0x0C,0x52,0x52,0x52,0x3E},
    [72] = {0x7F,0x08,0x04,0x04,0x78}, [73] = {0x00,0x44,0x7D,0x40,0x00},
    [74] = {0x20,0x40,0x44,0x3D,0x00}, [75] = {0x7F,0x10,0x28,0x44,0x00},
    [76] = {0x00,0x41,0x7F,0x40,0x00}, [77] = {0x7C,0x04,0x18,0x04,0x78},
    [78] = {0x7C,0x08,0x04,0x04,0x78}, [79] = {0x38,0x44,0x44,0x44,0x38},
    [80] = {0x7C,0x14,0x14,0x14,0x08}, [81] = {0x08,0x14,0x14,0x18,0x7C},
    [82] = {0x7C,0x08,0x04,0x04,0x08}, [83] = {0x48,0x54,0x54,0x54,0x20},
    [84] = {0x04,0x3F,0x44,0x40,0x20}, [85] = {0x3C,0x40,0x40,0x20,0x7C},
    [86] = {0x1C,0x20,0x40,0x20,0x1C}, [87] = {0x3C,0x40,0x30,0x40,0x3C},
    [88] = {0x44,0x28,0x10,0x28,0x44}, [89] = {0x0C,0x50,0x50,0x50,0x3C},
    [90] = {0x44,0x64,0x54,0x4C,0x44},
};

#define FONT_W 6
#define FONT_H 8
#define COLOR_BG   0x0000
#define COLOR_FG   0xFFFF
#define COLOR_LABEL 0x07FF
#define COLOR_OK   0x07E0
#define COLOR_ERR  0xF800
#define COLOR_WARN 0xFFE0

static void line_fill(uint16_t color)
{
    for (int i = 0; i < LCD_WIDTH * LCD_LINES_BUF; i++) {
        s_linebuf[i] = color;
    }
}

static void line_draw_char(int x, int y_in_batch, char c, uint16_t fg, uint16_t bg)
{
    int idx = -1;
    if (c >= ' ' && c <= 'z') idx = c - ' ';
    if (idx < 0 || idx >= (int)(sizeof(FONT_5X7) / sizeof(FONT_5X7[0]))) return;

    for (int col = 0; col < 5; col++) {
        uint8_t bits = FONT_5X7[idx][col];
        for (int row = 0; row < 7; row++) {
            uint16_t color = (bits & (1 << row)) ? fg : bg;
            int px = x + col;
            int py = y_in_batch + row;
            if (px < LCD_WIDTH && py >= 0 && py < LCD_LINES_BUF) {
                s_linebuf[py * LCD_WIDTH + px] = color;
            }
        }
    }
}

static void render_text_line(int screen_y, const char *text, uint16_t fg, uint16_t bg)
{
    for (int batch_y = screen_y; batch_y < screen_y + FONT_H; batch_y += LCD_LINES_BUF) {
        int batch_start = batch_y & ~(LCD_LINES_BUF - 1);
        if (batch_start < 0) batch_start = 0;

        line_fill(bg);
        int y_in_batch = screen_y - batch_start;
        if (y_in_batch >= 0 && y_in_batch < LCD_LINES_BUF) {
            int x = 10;
            const char *p = text;
            while (*p && x < LCD_WIDTH) {
                line_draw_char(x, y_in_batch, *p, fg, bg);
                x += FONT_W;
                p++;
            }
        }
        lcd_set_window(0, batch_start, LCD_WIDTH - 1, batch_start + LCD_LINES_BUF - 1);
        lcd_data((uint8_t *)s_linebuf, LCD_WIDTH * LCD_LINES_BUF * 2);
    }
}

tsnode_err_t display_init(void)
{
    ESP_LOGI(TAG, "init ILI9342C (M5Stack Core2)");

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = LCD_MISO,
        .sclk_io_num = LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_LINES_BUF * 2,
    };
    esp_err_t err = spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI init: %s", esp_err_to_name(err));
        return TSNODE_ERR_NETWORK;
    }

    spi_device_interface_config_t dev_cfg = {
        .mode = 0,
        .clock_speed_hz = LCD_PIXEL_CLK_HZ,
        .spics_io_num = LCD_CS,
        .queue_size = 10,
    };
    err = spi_bus_add_device(LCD_HOST, &dev_cfg, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI add device: %s", esp_err_to_name(err));
        return TSNODE_ERR_NETWORK;
    }

    /* Init AXP192 PMIC first (powers, resets the display) */
    axp192_init();

    /* GPIO init - DC pin */
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << LCD_DC),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_cfg);

    /* Backlight PWM via LEDC (like M5Stack Arduino library) */
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 44100,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t ch_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .gpio_num = LCD_BL,
        .duty = 200,
        .hpoint = 0,
    };
    ledc_channel_config(&ch_cfg);

    /* Init sequence */
    lcd_init_sequence();

    /* Allocate line buffer */
    s_linebuf = (uint16_t *)heap_caps_malloc(LCD_BUF_SIZE, MALLOC_CAP_DMA);
    if (s_linebuf == NULL) {
        ESP_LOGE(TAG, "linebuf alloc failed");
        return TSNODE_ERR_NO_MEMORY;
    }

    /* Fill screen red to verify display works */
    for (int y = 0; y < LCD_HEIGHT; y += LCD_LINES_BUF) {
        for (int i = 0; i < LCD_WIDTH * LCD_LINES_BUF; i++) {
            s_linebuf[i] = 0xF800; /* Red */
        }
        lcd_set_window(0, y, LCD_WIDTH - 1, y + LCD_LINES_BUF - 1);
        lcd_data((uint8_t *)s_linebuf, LCD_WIDTH * LCD_LINES_BUF * 2);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* Now clear to black */
    line_fill(COLOR_BG);
    for (int y = 0; y < LCD_HEIGHT; y += LCD_LINES_BUF) {
        lcd_set_window(0, y, LCD_WIDTH - 1, y + LCD_LINES_BUF - 1);
        lcd_data((uint8_t *)s_linebuf, LCD_WIDTH * LCD_LINES_BUF * 2);
    }

    ESP_LOGI(TAG, "display ready");
    return TSNODE_OK;
}

void display_splash(void)
{
    if (s_linebuf == NULL) return;
    line_fill(COLOR_BG);
    for (int y = 0; y < LCD_HEIGHT; y += LCD_LINES_BUF) {
        lcd_set_window(0, y, LCD_WIDTH - 1, y + LCD_LINES_BUF - 1);
        lcd_data((uint8_t *)s_linebuf, LCD_WIDTH * LCD_LINES_BUF * 2);
    }

    render_text_line(60, "TAILNET-ESP32-NODE", COLOR_LABEL, COLOR_BG);
    render_text_line(80, "WireGuard Data Plane", COLOR_FG, COLOR_BG);
    render_text_line(110, "Boot...", COLOR_FG, COLOR_BG);
}

void display_wifi(const char *ssid, const char *ip)
{
    if (s_linebuf == NULL) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "WiFi: %s", ssid ? ssid : "???");
    render_text_line(10, buf, ssid ? COLOR_OK : COLOR_ERR, COLOR_BG);
    if (ip) {
        snprintf(buf, sizeof(buf), "IP: %s", ip);
        render_text_line(26, buf, COLOR_FG, COLOR_BG);
    }
}

void display_tailscale(const char *state, const char *tailscale_ip, int peer_count)
{
    if (s_linebuf == NULL) return;
    uint16_t sc = COLOR_WARN;
    if (state && strstr(state, "online")) sc = COLOR_OK;
    else if (state && strstr(state, "error")) sc = COLOR_ERR;

    char buf[64];
    snprintf(buf, sizeof(buf), "TS: %s", state ? state : "???");
    render_text_line(50, buf, sc, COLOR_BG);

    snprintf(buf, sizeof(buf), "Tail: %s", tailscale_ip ? tailscale_ip : "(wait)");
    render_text_line(66, buf, COLOR_FG, COLOR_BG);

    snprintf(buf, sizeof(buf), "Peers: %d", peer_count);
    render_text_line(82, buf, COLOR_FG, COLOR_BG);
}

void display_status(const char *msg)
{
    if (s_linebuf == NULL || !msg) return;
    render_text_line(140, msg, COLOR_FG, COLOR_BG);
}
