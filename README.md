# Limef Plugins

Each subdirectory is a self-contained plugin that extends Limef with new threads and
framefilters.  Plugins are built and distributed independently of the base library.

## Available plugins

All plugins require you have installed limef base library with the deb package, or otherwise in a development/staging environment.

### 1. OpenCV

[opencv/](opencv/)

This plugin implements a thread that uses cuda OpenCV to manipulate frames in the framefilter chain.  Both cpp and python apps using the plugin are provided.

Requires cuda OpenCV installed in your system.  Scripts to download and install opencv-contrib and create a deb package are provided in the [limef apps example](https://github.com/elsampsa/limef-apps).

#### Build plugin library

Build the plugin library with:
```bash
cd plugins/opencv
mkdir build && cd build
# adapt as necessary:
# cmake .. -DLIMEF_PREFIX=$HOME/limef-stage  -DOPENCV_INSTALL=/path/to/opencv/install
cmake .. # .. or if you have installed everything (base lib and opencv) using the deb packages
make -j$(nproc)
# produces: build/lib/libLimefOpenCV.so  build/lib/limef_opencv.cpython-*.so
make package # produce distributable deb package
```
Once you have the deb produced, you can install it for the next step

#### Build apps using the plugin

The example apps live under `apps/cpp/` and have their own standalone CMake build.
They link against both `libLimef.so` (base) and `libLimefOpenCV.so` (this plugin).

Again, depending if you have installed everything, limef base library, opencv and this opencv plugin in dev/stage
environment or everything with the deb packages (the latter is the easiest way), the cmake commands will be different:

```bash
cd plugins/opencv/apps/cpp
mkdir build && cd build
# adapt as necessary:
cmake .. \
    -DLIMEF_PREFIX=$HOME/limef-stage \
    -DLIMEF_OPENCV_PREFIX=/path/to/opencv/build \ # wherever libLimefOpenCV.so lives
    -DOPENCV_INSTALL=/path/to/opencv/install \ # wherever opencv headers are
# or if everythings installed with deb packages, just:
# cmake ..
make -j$(nproc)
# produces: build/bin/usb_gpu_pipeline
```

#### Run the apps

The python examples under `apps/python` work out of the box if you installed with debian, otherwise you need to
set `LD_LIBRARY_PATH` and `PYTHONPATH` before they work.

*python apps*
```bash
# No separate build step — limef_opencv.cpython-*.so was produced in step 1.
source gobuild.bash # edit & adapt # not needed if everything was done with deb packages
python3 apps/python/usb_gpu_pipeline.py --modify
```

*cpp apps*
```bash
source gobuild.bash # edit & adapt # at the main directory # not needed if everything was done with deb packages
bin/usb_gpu_pipeline --modify # at the build directory
```

### 2. Basler

[basler/](basler/)

*WORK IN PROGRESS*

## Details

- Plugins include Limef headers and link `libLimef.so` — they are Limef citizens.
- The base `limef/CMakeLists.txt` has **no knowledge** of plugins.
- Each plugin reads `../../VERSION` for its own version number.
- ABI breaks in Limef require a SONAME bump; plugins will then refuse to load with
  a clear linker error rather than a silent memory fault.

## Copyright

(c) 2026 Sampsa Riikonen

## License

MIT
