# Limef Plugins

Each subdirectory is a self-contained plugin that extends Limef with new threads and
framefilters.  Plugins are built and distributed independently of the base library.

## Available plugins

| Directory | Library | Python module | Description |
|---|---|---|---|
| [opencv/](opencv/) | `libLimefOpenCV.so` | `limef_opencv` | GPU image processing via OpenCV CUDA |

## Quick start

### 1. Build the plugin library

```bash
cd plugins/opencv
mkdir build && cd build
cmake .. -DLIMEF_PREFIX=$HOME/limef-stage   # adapt as necessary
make -j$(nproc)
# produces: build/lib/libLimefOpenCV.so  build/lib/limef_opencv.cpython-*.so
```

### 2. Build the C++ example apps

The example apps live under `apps/cpp/` and have their own standalone CMake build.
They link against both `libLimef.so` (base) and `libLimefOpenCV.so` (plugin).

```bash
cd plugins/opencv/apps/cpp
mkdir build && cd build
cmake .. \
    -DLIMEF_PREFIX=$HOME/limef-stage \
    -DLIMEF_OPENCV_PREFIX=$HOME/limef/plugins/opencv/build   # or wherever libLimefOpenCV.so lives
make -j$(nproc)
# produces: build/bin/usb_gpu_pipeline
```

### 3. Run

```bash
export LD_LIBRARY_PATH=/path/to/apps/ext/opencv/install/lib:$LD_LIBRARY_PATH
./build/bin/usb_gpu_pipeline --modify
```

### Python

```bash
# No separate build step — limef_opencv.cpython-*.so was produced in step 1.
export PYTHONPATH=/path/to/plugins/opencv/build/lib:$PYTHONPATH
python3 apps/python/usb_gpu_pipeline.py --modify
```

## Rules

- Plugins include Limef headers and link `libLimef.so` — they are Limef citizens.
- The base `limef/CMakeLists.txt` has **no knowledge** of plugins.
- Each plugin reads `../../VERSION` for its own version number.
- ABI breaks in Limef require a SONAME bump; plugins will then refuse to load with
  a clear linker error rather than a silent memory fault.
