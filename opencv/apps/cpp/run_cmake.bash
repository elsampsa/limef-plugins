#!/bin/bash
#
# Run this from the build directory:
#   mkdir build && cd build
#   ../run_cmake.bash
#   make -j$(nproc)
#
# Prerequisites:
#   1. Build libLimef.so:         cd limef && make -j$(nproc)
#   2. Stage limef:               cd limef && ./staging.bash
#   3. Build the plugin:          cd plugins/opencv && mkdir build && cd build && cmake .. && make
#   4. Stage the plugin:          cd plugins/opencv && ./staging.bash
#   5. Build these apps:          (you are here)
#
# LIMEF_PREFIX — where limef + plugin headers and libs live (default: ~/limef-stage)
# OPENCV_INSTALL — OpenCV CUDA install prefix (default: apps/ext/opencv/install)
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIMEF_PREFIX="${LIMEF_PREFIX:-$HOME/limef-stage}"
OPENCV_INSTALL="${OPENCV_INSTALL:-$SCRIPT_DIR/../../../../apps/ext/opencv/install}"

BUILD_TYPE="Debug"
# BUILD_TYPE="Release"

echo
echo "LimefOpenCV Apps - CMake Configuration"
echo "======================================="
echo "LIMEF_PREFIX:   $LIMEF_PREFIX"
echo "OPENCV_INSTALL: $OPENCV_INSTALL"
echo "Source dir:     $SCRIPT_DIR"
echo

cmake \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DLIMEF_PREFIX=$LIMEF_PREFIX \
    -DOPENCV_INSTALL=$OPENCV_INSTALL \
    $SCRIPT_DIR

echo
echo "Run 'make -j\$(nproc)' to build"
echo "Binaries will be in bin/"
echo "Run with:"
echo "  LD_LIBRARY_PATH=$LIMEF_PREFIX/lib:$OPENCV_INSTALL/lib ./bin/<app>"
echo
