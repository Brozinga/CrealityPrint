# Include UOS detection module
include(${CMAKE_SOURCE_DIR}/../cmake/modules/CheckUos.cmake)

# Default: no patch
set(_patch_command "")

if (FLATPAK)
    set(_patch_command ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/GNU.cmake ./cmake/compilers/GNU.cmake)
elseif (IS_UOS)
    # Apply UOS specific patch and LoongArch fix
    set(_patch_command git init && ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/uos-patch.patch && ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-TBB-LoongArch.patch)
    message(STATUS "Applying UOS specific TBB patch")
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "loongarch")
    # Non-UOS LoongArch: apply UOS patch and LoongArch atomic fix as well
    set(_patch_command git init && ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/uos-patch.patch && ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-TBB-LoongArch.patch)
    message(STATUS "Applying LoongArch TBB patches (uos-patch + atomic fix)")
elseif (UNIX AND NOT APPLE)
    # Same LTO-disabling GNU.cmake used for FLATPAK: upstream oneTBB
    # unconditionally builds with -flto in Release, which bakes a
    # GCC-version-locked bytecode format into libtbb.a. That breaks any
    # build setup that links the final binary with a different GCC major
    # version than the one that compiled deps (e.g. distro package churn
    # pinning an older GCC for old autotools deps while linking with the
    # distro default). Reuse the existing patch generally on Linux.
    set(_patch_command ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/GNU.cmake ./cmake/compilers/GNU.cmake)
endif()

orcaslicer_add_cmake_project(
    TBB
    URL "https://github.com/oneapi-src/oneTBB/archive/refs/tags/v2021.5.0.zip"
    URL_HASH SHA256=83ea786c964a384dd72534f9854b419716f412f9d43c0be88d41874763e7bb47
    PATCH_COMMAND ${_patch_command}
    CMAKE_ARGS          
        -DTBB_BUILD_SHARED=OFF
        -DTBB_BUILD_TESTS=OFF
        -DTBB_TEST=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_DEBUG_POSTFIX=_debug
)

if (MSVC)
    add_debug_dep(dep_TBB)
endif ()




