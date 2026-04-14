#!/bin/bash
# Run from plugins/opencv/testing/:
#   ./run.bash
#
# Prerequisites:
#   source ~/limef/go_debug.bash   (stages base library + OpenCV LD_LIBRARY_PATH)
#   cd plugins/opencv && ./gobuild.bash  (builds + stages the plugin)

# OpenCV libs: use dev tree if present, fall back to system install
OPENCV_LIB="${OPENCV_LIB:-$HOME/limef/limef/apps/ext/opencv/install/lib}"
if [ ! -d "$OPENCV_LIB" ]; then
    echo "WARNING: WILL USE SYSTEM_WIDE INSTALLED OPENCV - this probably wont work"
    OPENCV_LIB="/usr/local/lib"
fi

python3 run-tests.py \
    --yaml=tests.yaml \
    --groups=static,cuda \
    --fixture-dir=/tmp \
    --dump-dir=$PWD/dump \
    --bin-dir=$PWD/../build_debug/bin \
    --lib-dir=$HOME/limef-stage/lib \
    --env LD_LIBRARY_PATH=$HOME/limef-stage/lib:$OPENCV_LIB \
    --jobs=3 \
    --full-output=./dump.out \
    --dump \
    --override