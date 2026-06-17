#!/usr/bin/env python3
"""
basler_dump.py — Basler camera diagnostic: grab a few frames and save as PNG

Pipeline:
    BaslerCameraThread → WritePNGFrameFilter → frames/

Runs at 1 fps by default and writes frame000001.png, frame000002.png, …
into the frames/ directory (relative to cwd, created if needed).

Usage:
    python3 basler_dump.py [options]

Options:
    --list      List connected cameras and exit (requires pypylon)
    --serial    Camera serial number (default: first available)
    --width     Capture width  (0 = camera default)
    --height    Capture height (0 = camera default)
    --fps       Frame rate (default: 1)
    --frames    Number of frames to capture then exit (default: 5)
    --mono      Mono8 mode (default: Color)
    --outdir    Output directory (default: frames/)

Test without hardware:
    PYLON_CAMEMU=1 python3 basler_dump.py
"""

import sys
import time
import os
import argparse

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
        iface    = d.GetDeviceClass()
        model    = d.GetModelName()
        serial   = d.GetSerialNumber()
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
    p = argparse.ArgumentParser(description="Basler camera → PNG dump (diagnostic)")
    p.add_argument("--list",   action="store_true",
                   help="List connected cameras and exit (requires pypylon)")
    p.add_argument("--serial", default="",
                   help="Camera serial number (default: first available)")
    p.add_argument("--width",  type=int, default=0,
                   help="Capture width  (0 = camera default)")
    p.add_argument("--height", type=int, default=0,
                   help="Capture height (0 = camera default)")
    p.add_argument("--fps",    type=float, default=1.0,
                   help="Frame rate (default: 1)")
    p.add_argument("--frames", type=int, default=5,
                   help="Number of frames to capture (default: 5)")
    p.add_argument("--mono",   action="store_true",
                   help="Mono8 mode (default: Color)")
    p.add_argument("--outdir", default="frames",
                   help="Output directory for PNG files (default: frames/)")
    p.add_argument("--feature-file", default="", metavar="PATH",
                   help="Pylon .pfs feature file to load (overrides camera defaults)")
    p.add_argument("--no-auto-exposure", action="store_true",
                   help="Disable ExposureAuto (use camera/feature-file setting)")
    args = p.parse_args()

    if args.list:
        list_cameras()
        sys.exit(0)

    outdir = os.path.abspath(args.outdir)
    os.makedirs(outdir, exist_ok=True)

    print(f"Basler camera dump")
    print(f"  Serial:  {args.serial or '(first available)'}")
    print(f"  Mode:    {'Mono' if args.mono else 'Color'}")
    if args.width and args.height:
        print(f"  Size:    {args.width}x{args.height}")
    print(f"  FPS:     {args.fps}")
    print(f"  Frames:  {args.frames}")
    print(f"  Output:  {outdir}/")
    print()

    # ── Pipeline ───────────────────────────────────────────────────────────────

    ctx               = limef_basler.BaslerCameraContext()
    ctx.serial        = args.serial
    ctx.width         = args.width
    ctx.height        = args.height
    ctx.fps           = args.fps
    ctx.mode          = limef_basler.Mode.Mono if args.mono else limef_basler.Mode.Color
    ctx.output_format = limef.AV_PIX_FMT_GRAY8 if args.mono else limef.AV_PIX_FMT_NV12

    ctx.feature_file   = args.feature_file
    ctx.exposure_auto  = not args.no_auto_exposure

    cam    = limef_basler.BaslerCameraThread("basler-cam", ctx)
    writer = limef.WritePNGFrameFilter("writer", outdir)
    cam.cc(writer)

    # ── Run ────────────────────────────────────────────────────────────────────

    cam.start()

    # Wait long enough for all frames: frames/fps + 3 s startup margin
    wait = args.frames / args.fps + 3.0
    print(f"Grabbing {args.frames} frame(s) at {args.fps} fps "
          f"(waiting up to {wait:.0f} s) — press Ctrl-C to stop early.")
    try:
        time.sleep(wait)
    except KeyboardInterrupt:
        print("\nInterrupted.")

    cam.stop()

    # Report what was written
    pngs = sorted(f for f in os.listdir(outdir) if f.endswith(".png"))
    if pngs:
        print(f"\nWrote {len(pngs)} file(s) to {outdir}/")
        for name in pngs:
            path = os.path.join(outdir, name)
            size = os.path.getsize(path)
            print(f"  {name}  ({size // 1024} kB)")
    else:
        print(f"\nNo PNG files written to {outdir}/ — check camera connection.")


if __name__ == "__main__":
    main()
