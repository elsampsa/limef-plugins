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

## Build the plugin

```bash
mkdir -p build && cd build
# if your limef base library is installed with deb package
cmake ..
# otherwise adapt this:
# cmake .. -DLIMEF_PREFIX=$HOME/limef-stage -DPYLON_ROOT=/opt/pylon
make -j$(nproc)
```

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
