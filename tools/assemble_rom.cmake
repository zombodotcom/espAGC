# assemble_rom.cmake
#
# Provides function: agc_assemble_rom(<MissionName> <MainAgcFile> <OutBinVar>)
#
# Runs the host yaYUL on a mission's MAIN.agc file and produces a packed binary
# ROM image. yaYUL natively writes a .bin alongside its input; we copy it to a
# stable path under ${CMAKE_BINARY_DIR}/roms.

include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/build_yayul.cmake")

set(ROM_OUT_DIR "${CMAKE_BINARY_DIR}/roms")

function(agc_assemble_rom MissionName MainAgcFile OutBinVar)
    set(OUT_BIN "${ROM_OUT_DIR}/${MissionName}.bin")
    set(${OutBinVar} "${OUT_BIN}" PARENT_SCOPE)

    # add_custom_command / add_custom_target are not scriptable; the IDF
    # requirements-scan still needs ${OutBinVar} to be set for EMBED_FILES,
    # which we did above. Skip the rest in script mode.
    if(CMAKE_SCRIPT_MODE_FILE)
        return()
    endif()

    file(MAKE_DIRECTORY "${ROM_OUT_DIR}")
    set(YAYUL_NATIVE_BIN "${MainAgcFile}.bin")

    # yaYUL writes <input>.bin in the same directory as <input>. We don't want
    # that to dirty the submodule, so we move it to ROM_OUT_DIR afterwards.
    get_filename_component(_agc_dir "${MainAgcFile}" DIRECTORY)
    add_custom_command(
        OUTPUT  "${OUT_BIN}"
        COMMAND "${YAYUL_EXECUTABLE}" "${MainAgcFile}"
        COMMAND "${CMAKE_COMMAND}" -E rename
                "${YAYUL_NATIVE_BIN}" "${OUT_BIN}"
        DEPENDS yayul_host "${MainAgcFile}"
        WORKING_DIRECTORY "${_agc_dir}"
        COMMENT "Assembling AGC ROM ${MissionName} via yaYUL"
        VERBATIM
    )

    add_custom_target(rom_${MissionName} ALL DEPENDS "${OUT_BIN}")
endfunction()
