idf_component_register(
    SRCS         "yaagc_socket.c"
    INCLUDE_DIRS "include"
    REQUIRES     agc_core channel_router lwip
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-implicit-function-declaration
)

# yaAGC.h gates socket plumbing behind __embedded__. We've replaced
# that plumbing here; expose the macro so includes resolve cleanly.
target_compile_definitions(${COMPONENT_LIB} PRIVATE __embedded__)
