idf_component_register(
    SRCS         "peripheral_stub.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../third_party/virtualagc/yaAGC"
    REQUIRES     agc_core log
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE __embedded__)
