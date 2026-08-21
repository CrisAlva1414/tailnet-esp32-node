idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
)

# -Werror es intencional y no negociable — ver AGENTS.md §4.
#
# Sin -Wpedantic: los headers de ESP-IDF/newlib disparan pedwarns del
# preprocesador (#include_next) que ni el dialecto gnu ni los pragmas
# scoped suprimen. Detalle y compensación en docs/format/c-style.md.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu11
    -Wall
    -Wextra
    -Werror
)
