idf_component_register(
    SRCS         "channel_router.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../third_party/virtualagc/yaAGC"
    REQUIRES     freertos
    PRIV_REQUIRES peripheral_stub
)

# Match agc_core's __embedded__ flag so the engine header parses the same way
# wherever it's included.
target_compile_definitions(${COMPONENT_LIB} PRIVATE __embedded__)
