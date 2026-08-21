# Componente reutilizable tsnode — frontera y capas en ADR-0005/0006.
#
# Sin REQUIRES todavia: los stubs no tocan APIs de ESP-IDF. Cada dependencia
# se agrega junto con su primer uso real, nunca adelantada (la compilación
# standalone es el test canónico de la frontera, ADR-0005).
idf_component_register(
    SRCS "src/tsnode.c"
         "src/port/esp_idf/tsnode_port_esp_idf.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src" "src/port"
)
