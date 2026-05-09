# build_yayul.cmake
#
# Builds the host yaYUL assembler from third_party/virtualagc/yaYUL using the
# host compiler (NOT the cross-compiler ESP-IDF normally drives). Defines a
# CMake target `yayul_host` that other targets can depend on, and exposes the
# variable YAYUL_EXECUTABLE pointing at the built binary.
#
# Called once from the apollo_rom component CMakeLists.

include_guard(GLOBAL)

get_filename_component(_PROJ_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(YAYUL_HOST_BUILD_DIR "${CMAKE_BINARY_DIR}/host_tools/yaYUL-build")
set(YAYUL_HOST_SRC_DIR   "${_PROJ_ROOT}/third_party/virtualagc/yaYUL")

if(WIN32)
    set(YAYUL_EXECUTABLE "${YAYUL_HOST_BUILD_DIR}/yaYUL.exe" CACHE INTERNAL "")
else()
    set(YAYUL_EXECUTABLE "${YAYUL_HOST_BUILD_DIR}/yaYUL"     CACHE INTERNAL "")
endif()

# IDF runs each component's CMakeLists in script mode (cmake -P) during its
# requirements scan. ExternalProject is not scriptable, so bail out early in
# that phase — paths/vars above are still useful for the assemble_rom shim.
if(CMAKE_SCRIPT_MODE_FILE)
    return()
endif()

include(ExternalProject)

# Locate a host C compiler. The parent IDF build configures a RISC-V cross
# compiler; ExternalProject's child CMake invocation needs a separate one.
find_program(YAYUL_HOST_CC NAMES gcc cc clang
             HINTS "C:/Users/zombo/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
             "C:/msys64/mingw64/bin"
             "C:/MinGW/bin")
if(NOT YAYUL_HOST_CC)
    message(FATAL_ERROR "espAGC: no host C compiler found for building yaYUL. "
                        "Install MinGW-w64 or set YAYUL_HOST_CC.")
endif()
message(STATUS "espAGC: yaYUL host CC = ${YAYUL_HOST_CC}")

set(_yayul_wrapper_dir "${_PROJ_ROOT}/tools/yayul_host_project")

ExternalProject_Add(yayul_host
    SOURCE_DIR        "${_yayul_wrapper_dir}"
    BINARY_DIR        "${YAYUL_HOST_BUILD_DIR}"
    INSTALL_COMMAND   ""
    BUILD_BYPRODUCTS  "${YAYUL_EXECUTABLE}"
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_C_COMPILER=${YAYUL_HOST_CC}
        -DYAYUL_SRC_DIR=${YAYUL_HOST_SRC_DIR}
)
