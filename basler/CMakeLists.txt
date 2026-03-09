# CMakeLists.txt — Limef Basler plugin
#
# Produces: libLimefBasler.so
#           limef_basler.cpython-*.so  (if pybind11 is available)
#
# Prerequisites:
#   - Limef installed or staged (headers + libLimef.so)
#   - Basler Pylon SDK installed (default: /opt/pylon)
#     See README.md for install instructions.
#
# Build:
#   mkdir build && cd build
#   cmake .. -DLIMEF_PREFIX=$HOME/limef-stage -DPYLON_ROOT=/opt/pylon
#   make -j$(nproc)

cmake_minimum_required(VERSION 3.18)
project(LimefBasler)

# ── Version ───────────────────────────────────────────────────────────────────

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" VERSION_STRING)
string(STRIP "${VERSION_STRING}" VERSION_STRING)
string(REPLACE "." ";" _ver_list ${VERSION_STRING})
list(GET _ver_list 0 MAJOR_VERSION)

# ── Locate Limef ──────────────────────────────────────────────────────────────

set(LIMEF_PREFIX "$ENV{HOME}/limef-stage" CACHE PATH "Limef install prefix")
set(LIMEF_INCLUDE_DIR    "${LIMEF_PREFIX}/include")
set(LIMEF_SYSINCLUDE_DIR "${LIMEF_PREFIX}/include/limef")
set(LIMEF_FFMPEG_DIR     "${LIMEF_PREFIX}/include/limef/ffmpeg")

if(NOT EXISTS "${LIMEF_SYSINCLUDE_DIR}/thread/thread.h")
    message(FATAL_ERROR
        "Limef headers not found at ${LIMEF_SYSINCLUDE_DIR}\n"
        "Install Limef first or set -DLIMEF_PREFIX=<path>.")
endif()

# ── Locate Pylon SDK ──────────────────────────────────────────────────────────

set(PYLON_ROOT "/opt/pylon" CACHE PATH "Basler Pylon SDK root directory")

# Pylon 7+ ships a CMake config at ${PYLON_ROOT}/lib/cmake/pylon/
find_package(pylon QUIET HINTS "${PYLON_ROOT}/lib/cmake/pylon")

if(pylon_FOUND)
    message(STATUS "Pylon SDK found via CMake config: ${pylon_VERSION}")
else()
    # Fallback: probe headers and construct targets manually.
    if(NOT EXISTS "${PYLON_ROOT}/include/pylon/PylonIncludes.h")
        message(FATAL_ERROR
            "Pylon SDK not found at ${PYLON_ROOT}\n"
            "Install the Pylon SDK (see README.md) or set -DPYLON_ROOT=<path>.")
    endif()
    message(STATUS "Pylon SDK found at ${PYLON_ROOT} (manual mode)")

    # Collect all pylon shared libs — names vary between Pylon 7 and 8
    file(GLOB _pylon_libs "${PYLON_ROOT}/lib/libpylon*.so"
                          "${PYLON_ROOT}/lib/libGCBase*.so"
                          "${PYLON_ROOT}/lib/libGenApi*.so"
                          "${PYLON_ROOT}/lib/liblog4cpp*.so"
                          "${PYLON_ROOT}/lib/libLog*.so")

    add_library(pylon::pylon INTERFACE IMPORTED)
    target_include_directories(pylon::pylon INTERFACE "${PYLON_ROOT}/include")
    target_link_directories(pylon::pylon INTERFACE "${PYLON_ROOT}/lib")
    target_link_libraries(pylon::pylon INTERFACE ${_pylon_libs})
endif()

# ── Plugin library ────────────────────────────────────────────────────────────

add_compile_options("-std=c++20")

add_library(LimefBasler SHARED src/plugin.cpp)

target_compile_definitions(LimefBasler PRIVATE
    LIMEF_BASLER_VERSION="${VERSION_STRING}"
)

target_include_directories(LimefBasler PRIVATE
    src
    ${LIMEF_INCLUDE_DIR}        # for "limef/xxx.h" includes
    ${LIMEF_SYSINCLUDE_DIR}     # for <spdlog/...> etc.
    ${LIMEF_FFMPEG_DIR}         # for <libavcodec/avcodec.h> etc.
)

# See opencv/CMakeLists.txt for a detailed explanation of --no-as-needed.
# Same header-only problem applies here.
target_link_libraries(LimefBasler PRIVATE
    "-L${LIMEF_PREFIX}/lib" "-Wl,--no-as-needed" -lLimef "-Wl,--as-needed"
    pylon::pylon
)

set_target_properties(LimefBasler PROPERTIES
    VERSION   "${VERSION_STRING}"
    SOVERSION "${MAJOR_VERSION}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# ── pybind11 Python module (import limef_basler) ──────────────────────────────

execute_process(
    COMMAND python3 -m pybind11 --cmakedir
    OUTPUT_VARIABLE PYBIND11_CMAKE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(PYBIND11_CMAKE_DIR)
    set(pybind11_DIR ${PYBIND11_CMAKE_DIR})
    find_package(pybind11 REQUIRED)

    execute_process(
        COMMAND python3-config --includes
        OUTPUT_VARIABLE PYTHON_INCLUDE_DIRS_TMP OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REPLACE "-I" " " PYTHON_INCLUDE_DIRS_TMP ${PYTHON_INCLUDE_DIRS_TMP})
    string(REPLACE " " ";" PYTHON_INCLUDE_DIRS ${PYTHON_INCLUDE_DIRS_TMP})

    pybind11_add_module(limef_basler_py python/limef_basler.cpp)
    set_target_properties(limef_basler_py PROPERTIES OUTPUT_NAME "limef_basler")

    target_compile_definitions(limef_basler_py PRIVATE
        LIMEF_BASLER_VERSION="${VERSION_STRING}"
    )
    target_include_directories(limef_basler_py PRIVATE
        src
        ${LIMEF_INCLUDE_DIR}
        ${LIMEF_SYSINCLUDE_DIR}
        ${LIMEF_FFMPEG_DIR}
        ${PYTHON_INCLUDE_DIRS}
    )
    target_link_libraries(limef_basler_py PRIVATE
        "-L${LIMEF_PREFIX}/lib"
        "-Wl,--no-as-needed" LimefBasler -lLimef "-Wl,--as-needed"
        pylon::pylon
    )
    set_target_properties(limef_basler_py PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )
    add_dependencies(limef_basler_py LimefBasler)

    install(TARGETS limef_basler_py
            LIBRARY DESTINATION lib/python3/dist-packages)
    message(STATUS "pybind11 found — building limef_basler Python module")
else()
    message(STATUS "pybind11 not found (run: pip install pybind11) — skipping Python module")
endif()

# ── Install ───────────────────────────────────────────────────────────────────

install(TARGETS LimefBasler LIBRARY DESTINATION lib)

install(DIRECTORY src/
        DESTINATION include/limef/basler
        FILES_MATCHING PATTERN "*.h")
