# CMakeLists.txt — LimefOpenCV plugin example apps (C++)
#
# Build:
#   mkdir build && cd build
#   cmake .. -DLIMEF_PREFIX=/usr/local -DLIMEF_OPENCV_PREFIX=/usr/local
#   make -j$(nproc)
#
# During development (using build trees instead of installed libs):
#   cmake .. \
#     -DLIMEF_PREFIX=$HOME/limef-stage \
#     -DLIMEF_OPENCV_PREFIX=/path/to/plugins/opencv/build
#   export LD_LIBRARY_PATH=/path/to/opencv/install/lib:$LD_LIBRARY_PATH

cmake_minimum_required(VERSION 3.18)
project(LimefOpenCVApps)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options("-std=c++20")

# ── Locate Limef ──────────────────────────────────────────────────────────────

set(LIMEF_PREFIX "$ENV{HOME}/limef-stage" CACHE PATH "Limef install prefix")
set(LIMEF_INCLUDE_DIR    "${LIMEF_PREFIX}/include")
set(LIMEF_SYSINCLUDE_DIR "${LIMEF_PREFIX}/include/limef")
set(LIMEF_FFMPEG_DIR     "${LIMEF_PREFIX}/include/limef/ffmpeg")

# ── Locate LimefOpenCV plugin ─────────────────────────────────────────────────

set(LIMEF_OPENCV_PREFIX "${LIMEF_PREFIX}" CACHE PATH
    "LimefOpenCV install prefix (may be same as LIMEF_PREFIX)")

# ── Locate OpenCV ─────────────────────────────────────────────────────────────

set(OPENCV_INSTALL "${CMAKE_CURRENT_SOURCE_DIR}/../../../../apps/ext/opencv/install"
    CACHE PATH "OpenCV install prefix")
set(OPENCV_INCLUDE_DIR "${OPENCV_INSTALL}/include/opencv4")
set(OPENCV_LIB_DIR     "${OPENCV_INSTALL}/lib")

# ── CUDA + other deps ─────────────────────────────────────────────────────────

find_package(CUDAToolkit REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_search_module(X11     REQUIRED x11)
pkg_search_module(ALSA    REQUIRED alsa)
pkg_search_module(OPENSSL REQUIRED openssl)
find_package(ZLIB REQUIRED)
exec_program(pkg-config ARGS --libs x11 glew OUTPUT_VARIABLE GLEW_LIBRARIES)

# ── Common settings ───────────────────────────────────────────────────────────

set(COMMON_INCLUDES
    ${LIMEF_INCLUDE_DIR}        # for "limef/xxx.h" and "limef/opencv/xxx.h"
    ${LIMEF_SYSINCLUDE_DIR}     # for <spdlog/...>, <flatbuffers/...>, <uws/...>
    ${LIMEF_FFMPEG_DIR}         # for <libavcodec/avcodec.h> etc.
    ${OPENCV_INCLUDE_DIR}
    ${CUDAToolkit_INCLUDE_DIRS}
)

set(COMMON_LIBS
    ${X11_LIBRARIES}
    ${GLEW_LIBRARIES}
    ${ALSA_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${ZLIB_LIBRARIES}
    pthread
    "-L${LIMEF_PREFIX}/lib"         Limef.so
    "-L${LIMEF_OPENCV_PREFIX}/lib"  LimefOpenCV.so
    "-L${OPENCV_LIB_DIR}"
    opencv_core opencv_cudaarithm opencv_cudafilters opencv_cudev opencv_imgproc
    CUDA::cudart
)

# ── Apps ──────────────────────────────────────────────────────────────────────

function(add_opencv_app name)
    add_executable(${name} ${name}.cpp)
    target_compile_definitions(${name} PRIVATE LIMEF_CUDA)
    target_include_directories(${name} PRIVATE ${COMMON_INCLUDES})
    target_link_libraries(${name} PRIVATE ${COMMON_LIBS})
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endfunction()

add_opencv_app(usb_gpu_pipeline)
