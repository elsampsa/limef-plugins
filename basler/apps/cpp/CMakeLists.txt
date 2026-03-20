# CMakeLists.txt — LimefBasler plugin example apps (C++)
#
# Build:
#   mkdir build && cd build
#   cmake .. -DLIMEF_PREFIX=$HOME/limef-stage -DPYLON_ROOT=/opt/pylon
#   make -j$(nproc)
#
# Run (emulated camera, no hardware needed):
#   PYLON_CAMEMU=1 LD_LIBRARY_PATH=$HOME/limef-stage/lib:/opt/pylon/lib \
#       ./bin/basler_pipeline --frames 30

cmake_minimum_required(VERSION 3.18)
project(LimefBaslerApps)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options("-std=c++20")

# ── Locate Limef ──────────────────────────────────────────────────────────────

if(EXISTS "$ENV{HOME}/limef-stage/include/limef/thread/thread.h")
    set(_limef_prefix_default "$ENV{HOME}/limef-stage")
elseif(EXISTS "/usr/include/limef/thread/thread.h")
    set(_limef_prefix_default "/usr")
else()
    set(_limef_prefix_default "$ENV{HOME}/limef-stage")
endif()
set(LIMEF_PREFIX "${_limef_prefix_default}" CACHE PATH "Limef install prefix")
message(STATUS "LIMEF_PREFIX: ${LIMEF_PREFIX}")
set(LIMEF_INCLUDE_DIR    "${LIMEF_PREFIX}/include")
set(LIMEF_SYSINCLUDE_DIR "${LIMEF_PREFIX}/include/limef")
set(LIMEF_FFMPEG_DIR     "${LIMEF_PREFIX}/include/limef/ffmpeg")

# ── Locate LimefBasler plugin ─────────────────────────────────────────────────

set(LIMEF_BASLER_PREFIX "${LIMEF_PREFIX}" CACHE PATH
    "LimefBasler install prefix (may be same as LIMEF_PREFIX or plugin build dir)")

# ── Locate Pylon SDK ──────────────────────────────────────────────────────────

set(PYLON_ROOT "/opt/pylon" CACHE PATH "Basler Pylon SDK root directory")

find_package(pylon QUIET HINTS "${PYLON_ROOT}/lib/cmake/pylon")
if(NOT pylon_FOUND)
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

# ── Common settings ───────────────────────────────────────────────────────────

set(COMMON_INCLUDES
    ${LIMEF_INCLUDE_DIR}            # for "limef/xxx.h" and "limef/basler/xxx.h"
    ${LIMEF_SYSINCLUDE_DIR}         # for <spdlog/...>, <flatbuffers/...>
    ${LIMEF_FFMPEG_DIR}             # for <libavcodec/avcodec.h> etc.
)

set(COMMON_LIBS
    pthread
    "-L${LIMEF_PREFIX}/lib"         Limef.so
    "-L${LIMEF_BASLER_PREFIX}/lib"  LimefBasler.so
    pylon::pylon
)

# ── Apps ──────────────────────────────────────────────────────────────────────

function(add_basler_app name)
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name} PRIVATE ${COMMON_INCLUDES})
    target_link_libraries(${name} PRIVATE ${COMMON_LIBS})
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endfunction()

add_basler_app(basler_pipeline)
