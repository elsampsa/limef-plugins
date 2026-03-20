#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$PWD/build/lib
export PYTHONPATH=$PYTHONPATH:$PWD/build/lib
echo "remember to add /path/to/opencv/install/lib directory to your LD_LIBRARY_PATH"
