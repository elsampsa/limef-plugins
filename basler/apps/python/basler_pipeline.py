"""
basler_pipeline.py — Basler camera grab demo

Pipeline:
    BaslerCameraThread → DumpFrameFilter

Grabs frames from the camera and prints frame info to the terminal.
Use --frames N to stop after N frames, or Ctrl-C to stop at any time.

Usage:
    python basler_pipeline.py [options]

Options:
    --list     List connected cameras and exit (requires pypylon)
    --serial   Camera serial number (default: first available)
    --width    Capture width  (default: camera default)
    --height   Capture height (default: camera default)
    --fps      Frame rate (default: 30)
    --mono     Use Mono8 mode (default: Color / YUV422)
    --frames   Number of frames to grab then exit (default: 30, 0 = run until Ctrl-C)

Test without hardware:
    PYLON_CAMEMU=1 python basler_pipeline.py
"""

import argparse
import signal
import sys
import time

import limef
import limef_basler


def list_cameras():
    try:
        from pypylon import pylon
    except ImportError:
        print("pypylon is not installed — cannot enumerate cameras.")
        print("Install it with:  pip install pypylon")
        sys.exit(1)

    tl = pylon.TlFactory.GetInstance()
    devices = tl.EnumerateDevices()
    if not devices:
        print("No Basler cameras found.")
        sys.exit(0)

    print(f"{len(devices)} camera(s) found:\n")
    for i, d in enumerate(devices):
        iface = d.GetDeviceClass()          # "BaslerUsb" / "BaslerGigE" / ...
        model  = d.GetModelName()
        serial = d.GetSerialNumber()
        friendly = d.GetFriendlyName()
        line = f"  [{i}]  {model:<28}  serial={serial:<14}  {iface}"
        try:
            ip = d.GetIpAddress()
            if ip and ip != "N/A":
                line += f"  ip={ip}"
        except Exception:
            pass
        print(line)
        if friendly and friendly != model:
            print(f"        friendly name: {friendly}")
    print()
    print("Pass --serial <serial> to select a specific camera.")


def main():
    parser = argparse.ArgumentParser(
        description="Basler camera → DumpFrameFilter demo")
    parser.add_argument("--list",   action="store_true",
                        help="List connected cameras and exit (requires pypylon)")
    parser.add_argument("--serial", default="",
                        help="Camera serial number (default: first available)")
    parser.add_argument("--width",  type=int, default=0,
                        help="Capture width  (default: camera default)")
    parser.add_argument("--height", type=int, default=0,
                        help="Capture height (default: camera default)")
    parser.add_argument("--fps",    type=float, default=30.0,
                        help="Frame rate (default: 30)")
    parser.add_argument("--mono",   action="store_true",
                        help="Use Mono8 mode (default: Color)")
    parser.add_argument("--secs", type=float, default=0,
                        help="Run this many seconds (0 = run until Ctrl-C)")
    parser.add_argument("--feature-file", default="", metavar="PATH",
                        help="Pylon .pfs feature file to load (focus, aperture, exposure, …)")
    parser.add_argument("--no-auto-exposure", action="store_true",
                        help="Disable ExposureAuto (use camera/feature-file setting)")
    args = parser.parse_args()

    if args.list:
        list_cameras()
        sys.exit(0)

    # ── Build pipeline ─────────────────────────────────────────────────────────

    ctx               = limef_basler.BaslerCameraContext()
    ctx.serial        = args.serial
    ctx.width         = args.width
    ctx.height        = args.height
    ctx.fps           = args.fps
    ctx.mode          = limef_basler.Mode.Mono if args.mono else limef_basler.Mode.Color
    ctx.output_format = limef.AV_PIX_FMT_GRAY8 if args.mono else limef.AV_PIX_FMT_NV12
    ctx.feature_file  = args.feature_file
    ctx.exposure_auto = not args.no_auto_exposure

    cam  = limef_basler.BaslerCameraThread("basler-cam", ctx)
    dump = limef.DumpFrameFilter("dump")
    cam.cc(dump)

    # ── Run ────────────────────────────────────────────────────────────────────

    mode_str = "Mono" if args.mono else "Color"
    print(f"Starting Basler camera ({mode_str} mode)"
          + (f", serial={args.serial}" if args.serial else "")
          + (f", {args.width}x{args.height}" if args.width and args.height else "")
          + f", {args.fps} fps"
          + (f", grabbing {args.secs} secs" if args.secs > 0 else "")
    )
    print("Press Ctrl-C to stop early.\n")

    running = True
    def _stop(sig, frame):
        nonlocal running
        running = False
    signal.signal(signal.SIGINT,  _stop)
    signal.signal(signal.SIGTERM, _stop)

    cam.start()

    if args.secs > 0:
        deadline = time.monotonic() + args.secs + 0.1
        while running and time.monotonic() < deadline:
            time.sleep(0.1)
    else:
        while running:
            time.sleep(0.1)

    cam.stop()
    print("Done.")


if __name__ == "__main__":
    main()
