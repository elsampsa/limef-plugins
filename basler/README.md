# Limef Basler Plugin

Provides `BaslerCameraThread` — a Limef producer thread backed by the Basler Pylon SDK.

## Install the Pylon SDK

Download from the Basler website (free account required):

> https://www.baslerweb.com/en/downloads/software-downloads/

Choose **pylon Camera Software Suite — Linux x86 64-bit** and install the `.deb`:

```bash
sudo apt install ./pylon_*.deb
# installs headers, libs and tools to /opt/pylon
```

A quote from the Basler docs:
> During the installation, an environment variable required for pylon GenTL
producers and a permission file for Basler USB cameras are installed
automatically. For this to take effect, you need to log out and log in again to
your Linux system as well as unplug and replug all USB cameras.

## Install pypylon python bindings

These are used for listing cameras, adjusting their parameters, etc.
```bash
pip install pypylon --break-system-packages
```

## Build the plugin

```bash
mkdir -p build && cd build
# if your limef base library is installed with deb package
cmake ..
# otherwise adapt this:
# cmake .. -DLIMEF_PREFIX=$HOME/limef-stage -DPYLON_ROOT=/opt/pylon
make -j$(nproc)
```

## Camera configuration (focus, aperture, exposure)

Before running any test scripts, configure the camera once and save the settings
to a `.pfs` feature file.  The thread loads it automatically on startup.

### Option A — Pylon Viewer (recommended, GUI)

```bash
/opt/pylon/bin/pylonviewer
```

Connect to the camera, adjust focus distance (0.5 m – ∞), aperture (f/2.5 – f/16),
exposure, gain, etc. while watching the live image.  When happy:

**Features → Save Features…** → save as e.g. `apps/python/camera.pfs`

### Option B — basler_configure.py (CLI / scriptable)

```bash
cd apps/python
python3 basler_configure.py          # interactive shell + saves camera.pfs
# or non-interactively:
python3 basler_configure.py --set "ExposureAuto=Continuous" --save camera.pfs
# or inspect a single parameter:
python3 basler_configure.py --get ExposureTime
# or print everything:
python3 basler_configure.py --dump
```

### Using the saved config in test scripts

Pass `--feature-file camera.pfs` to any Python app:

```bash
python3 basler_dump.py --feature-file camera.pfs          # grab frames → PNG
python3 basler_pipeline.py --feature-file camera.pfs      # basic pipeline
python3 basler_rtsp.py --feature-file camera.pfs          # RTSP server
```

Or in Python code:

```python
ctx = limef_basler.BaslerCameraContext()
ctx.feature_file = "camera.pfs"
```

> **Note:** only one process can hold the camera open at a time.  Close Pylon
> Viewer (or `basler_configure.py`) before starting any limef script, and vice versa.

## Run

```bash
source /opt/pylon/bin/pylon-setup-env.sh   # adds /opt/pylon/lib to LD_LIBRARY_PATH
./build/bin/basler_pipeline --port 8554
ffplay rtsp://localhost:8554/live/stream
```

## Testing (no hardware needed)

Pylon ships a software emulator.  Set `PYLON_CAMEMU=1` to get a virtual camera:

```bash
cd testing
PYLON_CAMEMU=1 ./runone.bash basler_test:1
```
