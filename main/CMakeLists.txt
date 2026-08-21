idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
)

# -Werror es intencional y no negociable — ver AGENTS.md §4.
#
# gnu11 y no c11 estricto: los headers de ESP-IDF/newlib usan #include_next
# y macros variádicas GNU (ESP_LOGx), incompatibles con -Wpedantic bajo ISO
# estricto. Ver docs/format/c-style.md.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu11
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)
