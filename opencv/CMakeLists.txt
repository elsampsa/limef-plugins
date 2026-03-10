# CMakeLists.txt — Limef OpenCV CUDA plugin
#
# Produces: libLimefOpenCV.so
#
# This plugin is built standalone against an installed (or staged) Limef.
# It has no connection to the base Limef CMake build.
#
# Prerequisites:
#   - Limef installed or staged (headers + libLimef.so)
#   - OpenCV built with CUDA support (see ../../apps/ext/build_ext.bash)
#   - CUDA toolkit
#
# Build:
#   mkdir build && cd build
#   cmake .. -DLIMEF_PREFIX=/usr/local
#   make -j$(nproc)
#
# During development (Limef not yet installed, using build tree):
#   cmake .. -DLIMEF_PREFIX=$HOME/limef-stage
#   export LD_LIBRARY_PATH=/path/to/opencv/install/lib:$LD_LIBRARY_PATH

cmake_minimum_required(VERSION 3.18)
project(LimefOpenCV)

# ── Locate Limef ──────────────────────────────────────────────────────────────
# Declared first so the VERSION fallback path below can reference LIMEF_PREFIX.

set(LIMEF_PREFIX "$ENV{HOME}/limef-stage" CACHE PATH "Limef install prefix")

# ── Version ───────────────────────────────────────────────────────────────────

# Find the VERSION file: try in-tree first (plugins/opencv/../../VERSION),
# then fall back to the installed copy from the .deb (include/limef/VERSION).
# This makes out-of-tree builds (e.g. /tmp) work against an installed .deb.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" VERSION_STRING)
elseif(EXISTS "${LIMEF_PREFIX}/include/limef/VERSION")
    file(READ "${LIMEF_PREFIX}/include/limef/VERSION" VERSION_STRING)
else()
    set(VERSION_STRING "0.0.0")
    message(WARNING "Could not find Limef VERSION file — using ${VERSION_STRING}")
endif()
string(STRIP "${VERSION_STRING}" VERSION_STRING)
string(REPLACE "." ";" _ver_list ${VERSION_STRING})
list(GET _ver_list 0 MAJOR_VERSION)

# ── (continued) Locate Limef ──────────────────────────────────────────────────
set(LIMEF_INCLUDE_DIR "${LIMEF_PREFIX}/include")
set(LIMEF_SYSINCLUDE_DIR "${LIMEF_PREFIX}/include/limef")
set(LIMEF_FFMPEG_DIR  "${LIMEF_PREFIX}/include/limef/ffmpeg")

if(NOT EXISTS "${LIMEF_SYSINCLUDE_DIR}/thread/thread.h")
    message(FATAL_ERROR
        "Limef headers not found at ${LIMEF_SYSINCLUDE_DIR}\n"
        "Install Limef first or set -DLIMEF_PREFIX=<path>.")
endif()

# ── Locate OpenCV ─────────────────────────────────────────────────────────────

set(OPENCV_INSTALL "${CMAKE_CURRENT_SOURCE_DIR}/../../apps/ext/opencv/install"
    CACHE PATH "OpenCV install prefix (must be built with CUDA support)")

set(OPENCV_INCLUDE_DIR "${OPENCV_INSTALL}/include/opencv4")
set(OPENCV_LIB_DIR     "${OPENCV_INSTALL}/lib")

if(NOT EXISTS "${OPENCV_INCLUDE_DIR}/opencv2/core.hpp")
    message(FATAL_ERROR
        "OpenCV headers not found at ${OPENCV_INCLUDE_DIR}\n"
        "Build OpenCV with CUDA support first (see apps/ext/build_ext.bash),\n"
        "or set -DOPENCV_INSTALL=<path>.")
endif()

# ── CUDA ──────────────────────────────────────────────────────────────────────

find_package(CUDAToolkit REQUIRED)

# ── Plugin library ────────────────────────────────────────────────────────────

add_compile_options("-std=c++20")

add_library(LimefOpenCV SHARED src/plugin.cpp)

target_compile_definitions(LimefOpenCV PRIVATE
    LIMEF_CUDA
    LIMEF_OPENCV_VERSION="${VERSION_STRING}"
)

target_include_directories(LimefOpenCV PRIVATE
    src
    ${LIMEF_INCLUDE_DIR}        # for "limef/xxx.h" includes
    ${LIMEF_SYSINCLUDE_DIR}     # for <spdlog/...>, <uws/...> etc.
    ${LIMEF_FFMPEG_DIR}         # for <libavcodec/avcodec.h> etc.
    ${OPENCV_INCLUDE_DIR}
    ${CUDAToolkit_INCLUDE_DIRS}
)

# NOTE: --no-as-needed is required for libLimef because this plugin is header-only.
#
# Ubuntu's linker defaults to --as-needed, which only records a library in the
# NEEDED list of the output .so if at least one symbol from it is directly
# referenced in the object files being linked.  When a plugin is header-only
# (all C++ lives in .h files, plugin.cpp just #includes them), the compiler
# instantiates everything inline into the plugin's own object file.  The
# resulting .o has no *unresolved* references to libLimef — every call to a
# Limef function is a direct call into the plugin's own text segment.  The
# linker therefore sees libLimef as "unused" and omits it from NEEDED.
#
# At runtime this causes an ImportError for vtable symbols (e.g. SignalFrame's
# vtable) because those weak symbols live in libLimef.so and are not embedded
# inline.  Python's dlopen uses RTLD_LOCAL by default, so symbols from an
# already-loaded libLimef.so (imported via 'import limef') are not visible to
# the new .so being loaded, and the undefined vtable symbol causes a crash.
#
# --no-as-needed forces libLimef into NEEDED regardless of direct references,
# so the dynamic linker loads it alongside this plugin and all symbols resolve.
#
# If this plugin were NOT header-only (i.e. gpu_opencv_thread.cpp existed with
# the method bodies compiled separately), the .o files would contain unresolved
# references to libLimef symbols (constructors, virtual dispatch, etc.) and the
# linker would keep libLimef in NEEDED automatically.  --no-as-needed and the
# plain -lLimef flag would both be unnecessary — you could just write:
#
#   target_link_libraries(LimefOpenCV PRIVATE
#       "-L${LIMEF_PREFIX}/lib" Limef          # CMake turns this into -lLimef
#       "-L${OPENCV_LIB_DIR}" opencv_core ...
#   )
#
# and it would work.  The header-only design trades away that simplicity.
target_link_libraries(LimefOpenCV PRIVATE
    "-L${LIMEF_PREFIX}/lib" "-Wl,--no-as-needed" -lLimef "-Wl,--as-needed"
    "-L${OPENCV_LIB_DIR}"
    opencv_core
    opencv_cudaarithm
    opencv_cudafilters
    opencv_cudev
    opencv_imgproc
    CUDA::cudart
)

set_target_properties(LimefOpenCV PROPERTIES
    VERSION   "${VERSION_STRING}"
    SOVERSION "${MAJOR_VERSION}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# ── pybind11 Python module (import limef_opencv) ──────────────────────────────

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

    pybind11_add_module(limef_opencv_py python/limef_opencv.cpp)
    set_target_properties(limef_opencv_py PROPERTIES OUTPUT_NAME "limef_opencv")

    target_compile_definitions(limef_opencv_py PRIVATE
        LIMEF_CUDA
        LIMEF_OPENCV_VERSION="${VERSION_STRING}"
    )
    target_include_directories(limef_opencv_py PRIVATE
        src
        ${LIMEF_INCLUDE_DIR}        # for "limef/xxx.h" includes
        ${LIMEF_SYSINCLUDE_DIR}     # for <spdlog/...>, <uws/...> etc.
        ${LIMEF_FFMPEG_DIR}         # for <libavcodec/avcodec.h> etc.
        ${OPENCV_INCLUDE_DIR}
        ${CUDAToolkit_INCLUDE_DIRS}
        ${PYTHON_INCLUDE_DIRS}
    )
    # NOTE: same header-only problem applies to the pybind11 module.
    #
    # limef_opencv.cpp includes gpu_opencv_thread.h, which pulls in all the
    # inline OpenCV CUDA code (processFrame, ensureOutput, etc.).  The compiler
    # compiles that code directly into limef_opencv.cpython.so's object file.
    # With --as-needed the linker sees no unresolved references to
    # libLimefOpenCV.so or libLimef.so and strips both from NEEDED.
    #
    # Additionally, because processFrame() uses cv::cuda::createGaussianFilter
    # and other OpenCV functions, the pybind11 module itself needs to link
    # against the OpenCV CUDA libs directly — those calls are baked into this
    # .so, not delegated to libLimefOpenCV.so at runtime.
    #
    # If the plugin were NOT header-only (method bodies in .cpp files compiled
    # into libLimefOpenCV.so), then:
    #   - limef_opencv.cpp would only see declarations, not definitions
    #   - The .o for limef_opencv.cpython.so would have unresolved references
    #     to GPUOpenCVThread symbols -> linker keeps libLimefOpenCV.so in NEEDED
    #   - OpenCV symbols would live in libLimefOpenCV.so, not here, so the
    #     pybind11 module would NOT need to link OpenCV directly
    #   - This whole block would simplify to just:
    #
    #       target_link_libraries(limef_opencv_py PRIVATE LimefOpenCV)
    #
    #   and the transitive NEEDED chain (limef_opencv -> libLimefOpenCV ->
    #   libLimef -> OpenCV) would handle everything automatically.
    target_link_libraries(limef_opencv_py PRIVATE
        "-L${LIMEF_PREFIX}/lib"
        "-Wl,--no-as-needed" LimefOpenCV -lLimef "-Wl,--as-needed"
        "-L${OPENCV_LIB_DIR}"
        opencv_core
        opencv_cudaarithm
        opencv_cudafilters
        opencv_cudev
        CUDA::cudart
    )
    set_target_properties(limef_opencv_py PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )
    add_dependencies(limef_opencv_py LimefOpenCV)

    set(PYTHON_DIR "lib/python3/dist-packages")
    install(TARGETS limef_opencv_py LIBRARY DESTINATION ${PYTHON_DIR})
    message(STATUS "pybind11 found — building limef_opencv Python module")
else()
    message(STATUS "pybind11 not found (run: pip install pybind11) — skipping Python module")
endif()

# ── Install ───────────────────────────────────────────────────────────────────

install(TARGETS LimefOpenCV LIBRARY DESTINATION lib)

install(DIRECTORY src/
        DESTINATION include/limef/opencv
        FILES_MATCHING PATTERN "*.h")

# ── Bundle OpenCV .so files into the package (self-contained .deb) ────────────
#
# Uncomment when building a distributable .deb so the target system does not
# need OpenCV installed.  Also add /etc/ld.so.conf.d/limef-opencv.conf to the
# .deb postinst (one line: /usr/local/lib/limef/opencv) and run ldconfig.
#
# file(GLOB OPENCV_SHARED_LIBS "${OPENCV_LIB_DIR}/libopencv_*.so*")
# install(FILES ${OPENCV_SHARED_LIBS}
#         DESTINATION lib/limef/opencv)

# ── CPack / .deb packaging ────────────────────────────────────────────────────

exec_program(dpkg ARGS --print-architecture OUTPUT_VARIABLE MY_ARCH)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "LimefOpenCV")
set(CPACK_PACKAGE_VERSION         "${VERSION_STRING}")
set(CPACK_PACKAGE_VERSION_MAJOR   "${MAJOR_VERSION}")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${MY_ARCH}")
set(CPACK_PACKAGE_CONTACT         "sampsa.riikonen@iki.fi")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Limef OpenCV CUDA plugin")
# The plugin requires the matching Limef base package at runtime.
set(CPACK_DEBIAN_PACKAGE_DEPENDS  "limef (= ${VERSION_STRING})")

include(CPack)
