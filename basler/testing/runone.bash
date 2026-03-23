#!/bin/bash
# Run from plugins/basler/testing/:
#   ./runone.bash basler_test:1
#   ./runone.bash basler_test:2,basler_test:3
#
# Prerequisites:
#   source ~/limef/go_debug.bash   (stages base library)
#   cd plugins/basler && ./gobuild.bash  (builds + stages the plugin)
#   cd test && mkdir build_debug && cd build_debug && ../run_cmake.bash && make -j$(nproc)

python3 run-tests.py \
    --yaml=tests.yaml \
    --groups=static,emulator \
    --fixture-dir=/tmp \
    --dump-dir=$PWD/dump \
    --bin-dir=$PWD/../build_debug/bin \
    --lib-dir=$HOME/limef-stage/lib \
    --jobs=1 \
    --full-output=./dump.out \
    --dump \
    --override \
    --tests $@
