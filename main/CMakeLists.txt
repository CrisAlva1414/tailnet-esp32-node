idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
)

# -Werror es intencional y no negociable — ver AGENTS.md §4.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=c11
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)
