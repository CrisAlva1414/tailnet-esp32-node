# Componente reutilizable tsnode — frontera y capas en ADR-0005/0006.
# REQUIRES mínimos: solo lo que el port usa hoy (RNG hardware, esp_timer).
# Cada dependencia se agrega junto con su primer uso real, nunca adelantada.
idf_component_register(
    SRCS "src/tsnode.c"
         "src/port/esp_idf/tsnode_port_esp_idf.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src" "src/port"
    REQUIRES esp_hw_support esp_timer
)
