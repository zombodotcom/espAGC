get_filename_component(_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(YAAGC_DIR "${_PROJECT_ROOT}/third_party/virtualagc/yaAGC")

# Cherry-pick the engine sources we want from the upstream yaAGC tree.
# Excluded: SocketAPI.c (we replace), NullAPI.c, agc_engine_init.c (we replace
# with a memory-loading variant in agc_init.c), rfopen.c, agc_cli.c,
# agc_debugger.c, agc_simulator.c, agc_gdbmi.c, agc_help.c, agc_disassembler.c,
# agc_symtab.c, NormalizeSourceName.c, main.c, EmbeddedDemo.c, WinAGC.c.
set(_agc_engine_srcs
    "${YAAGC_DIR}/agc_engine.c"
    "${YAAGC_DIR}/Backtrace.c"
    "${YAAGC_DIR}/DecodeDigitalDownlink.c"
)
# agc_utilities.c is socket-packet plumbing (FormIoPacket / EstablishSocket /
# CallSocket / UnblockSocket) used only by SocketAPI.c, which we replace with
# io_callbacks.c. Excluding it removes the WSADATA / NO_ADDRESS dependency.

idf_component_register(
    SRCS
        ${_agc_engine_srcs}
        "agc_init.c"
        "io_callbacks.c"
        "posix_shim.c"
    INCLUDE_DIRS "include" "${YAAGC_DIR}"
    REQUIRES channel_router
)

# Engine + helpers are GPLv2 third-party C with old style; keep warnings tame.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-implicit-function-declaration
    -Wno-format
    -Wno-incompatible-pointer-types
    -Wno-int-conversion
)

# NOTE: agc_engine.c already does `#define AGC_ENGINE_C` internally at the
# top of the file before including agc_engine.h, so we must NOT also pass
# -DAGC_ENGINE_C — that triggers a redefinition error under -Werror.

# yaAGC.h's host-detection ladder includes an __embedded__ branch that skips
# all the socket/pthread plumbing; it's the closest fit for ESP-IDF. Apply it
# everywhere this component (and its header consumers) sees yaAGC.h.
target_compile_definitions(${COMPONENT_LIB} PUBLIC __embedded__)
