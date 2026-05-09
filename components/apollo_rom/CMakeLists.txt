get_filename_component(_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${_PROJECT_ROOT}/tools/assemble_rom.cmake")

# We assemble from the virtualagc submodule's mission tree, not Apollo-11.
# Modern yaYUL targets virtualagc's canonical file layout; chrislgarry/Apollo-11
# is a re-presentation that doesn't match yaYUL's $INCLUDE expectations
# (e.g. its TAGS_FOR_RELATIVE_SETLOC.agc isn't in virtualagc's split). We keep
# the Apollo-11 submodule around for source-display features but route ROM
# generation through virtualagc.
set(VAGC_SRC "${_PROJECT_ROOT}/third_party/virtualagc")

agc_assemble_rom(Luminary099 "${VAGC_SRC}/Luminary099/MAIN.agc" LUM_BIN)
agc_assemble_rom(Comanche055 "${VAGC_SRC}/Comanche055/MAIN.agc" COM_BIN)

idf_component_register(
    SRCS        "apollo_rom.c"
    INCLUDE_DIRS "include"
    EMBED_FILES "${LUM_BIN}" "${COM_BIN}"
)

# Make the IDF component target wait for the host yaYUL pass.
add_dependencies(${COMPONENT_LIB} rom_Luminary099 rom_Comanche055)
