# Minimal top-level CMake project that compiles yaYUL with the host
# toolchain. We can't point ExternalProject at the in-tree
# third_party/virtualagc/yaYUL/CMakeLists.txt directly because that file is a
# subdirectory CMakeLists (no project() / no cmake_minimum_required) and is
# only meant to be add_subdirectory()'d from the virtualagc root.
#
# This wrapper enumerates the same .c files yaYUL's own CMakeLists declares,
# referencing them through a YAYUL_SRC_DIR cache variable supplied at
# configure time by tools/build_yayul.cmake.

cmake_minimum_required(VERSION 3.16)
project(yayul_host C)

set(CMAKE_C_STANDARD 99)

if(NOT YAYUL_SRC_DIR)
    message(FATAL_ERROR "YAYUL_SRC_DIR must be set to the yaYUL source directory.")
endif()

# Mirrors third_party/virtualagc/yaYUL/CMakeLists.txt:1-6.
set(_yayul_srcs
    GetOctOrDec.c IncPc.c
    Parse2CADR.c Parse2DEC.c Parse2FCADR.c
    ParseBANK.c ParseBBCON.c ParseBLOCK.c
    ParseCADR.c ParseCHECKequals.c
    ParseEBANKEquals.c ParseECADR.c ParseEQUALS.c ParseERASE.c
    ParseEqMinus.c ParseEqualsECADR.c
    ParseGENADR.c ParseGeneral.c ParseINDEX.c ParseInterpretiveOperand.c
    ParseOCT.c ParseSBANKEquals.c ParseSETLOC.c ParseST.c
    ParseXCADR.c
    Pass.c PseudoToSegmented.c
    SymbolPass.c SymbolTable.c
    strcmpEBCDIC.c
    Utilities.c
    yaYUL.c yul2agc.c
)

list(TRANSFORM _yayul_srcs PREPEND "${YAYUL_SRC_DIR}/")

add_executable(yaYUL ${_yayul_srcs})
target_compile_definitions(yaYUL PRIVATE NVER="espAGC")
target_compile_options(yaYUL PRIVATE
    -Wno-implicit-function-declaration -Wno-format -Wno-unused-result
    -Wno-incompatible-pointer-types -Wno-int-conversion
    -Wno-incompatible-pointer-types-discards-qualifiers
    -Wno-discarded-qualifiers -Wno-deprecated-declarations
)
if(NOT MSVC)
    target_link_libraries(yaYUL PRIVATE m)
endif()
