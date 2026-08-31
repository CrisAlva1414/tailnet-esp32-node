/*
 * Smoke test display for M5Stack Core2 (ILI9342 320x240).
 *
 * Minimal text display showing:
 *   - WiFi SSID and IP
 *   - Tailscale node IP (100.x.x.x)
 *   - Client state (connecting/syncing/online/error)
 *   - Peer count
 *
 * Uses esp_lcd directly (no LVGL) for minimal footprint.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "tsnode_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize the display (SPI + ILI9342). Call once at boot.
 * Returns TSNODE_OK on success.
 */
tsnode_err_t display_init(void);

/*
 * Clear screen and show boot splash.
 */
void display_splash(void);

/*
 * Update WiFi status line.
 * ssid: connected SSID (or NULL if disconnected)
 * ip: IP address string (or NULL if no IP)
 */
void display_wifi(const char *ssid, const char *ip);

/*
 * Update Tailscale status line.
 * state: human-readable state string
 * tailscale_ip: assigned 100.x.x.x (or NULL)
 * peer_count: number of known peers
 */
void display_tailscale(const char *state, const char *tailscale_ip,
                       int peer_count);

/*
 * Show a message on the last line (scrolling).
 * Useful for transient status updates.
 */
void display_status(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_H */
