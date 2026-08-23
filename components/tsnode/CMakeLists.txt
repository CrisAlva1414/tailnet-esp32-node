# Componente reutilizable tsnode — frontera y capas en ADR-0005/0006.
# REQUIRES mínimos: solo lo que el port usa hoy (RNG hardware, esp_timer,
# networking/TLS para ts2021). Cada dependencia se agrega junto con su
# primer uso real, nunca adelantada.
idf_component_register(
    SRCS "src/tsnode.c"
         "src/port/esp_idf/tsnode_port_esp_idf.c"
         "src/port/esp_idf/tsnode_port_net_esp_idf.c"
         "src/port/x25519_wrapper.c"
         "src/crypto/blake2s.c"
         "src/proto/ts2021.c"
         "src/proto/tsnode_register.c"
         "src/proto/tsnode_map.c"
         "src/proto/tsnode_client.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src" "src/port" "src/crypto" "src/proto"
                      "/home/notebook01/esp/esp-idf/components/mbedtls/mbedtls/3rdparty/everest/include/everest"
    REQUIRES esp_hw_support esp_timer mbedtls esp_netif
)

# KRML_VERIFIED_UINT128: Hacl_Curve25519 necesita __int128 que no existe
# en Xtensa (ESP32). Definir esta macro usa la implementación fallback
# de uint128 del kremlin que no requiere __int128.
target_compile_options(${COMPONENT_LIB} PRIVATE -DKRML_VERIFIED_UINT128)
