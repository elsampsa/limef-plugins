"""
basler_pipeline.py — Basler camera grab demo

Pipeline:
    BaslerCameraThread → DumpFrameFilter

Grabs frames from the camera and prints frame info to the terminal.
Use --frames N to stop after N frames, or Ctrl-C to stop at any time.

Usage:
    python basler_pipeline.py [options]

Options:
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
import time

import limef
import limef_basler


def main():
    parser = argparse.ArgumentParser(
        description="Basler camera → DumpFrameFilter demo")
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
    parser.add_argument("--frames", type=int, default=30,
                        help="Frames to grab then exit (0 = run until Ctrl-C)")
    args = parser.parse_args()

    # ── Build pipeline ─────────────────────────────────────────────────────────

    ctx               = limef_basler.BaslerCameraContext()
    ctx.serial        = args.serial
    ctx.width         = args.width
    ctx.height        = args.height
    ctx.fps           = args.fps
    ctx.mode          = limef_basler.Mode.Mono if args.mono else limef_basler.Mode.Color
    ctx.output_format = limef.AV_PIX_FMT_GRAY8 if args.mono else limef.AV_PIX_FMT_NV12

    cam  = limef_basler.BaslerCameraThread("basler-cam", ctx)
    dump = limef.DumpFrameFilter("dump")
    cam.cc(dump)

    # ── Run ────────────────────────────────────────────────────────────────────

    mode_str = "Mono" if args.mono else "Color"
    print(f"Starting Basler camera ({mode_str} mode)"
          + (f", serial={args.serial}" if args.serial else "")
          + (f", {args.width}x{args.height}" if args.width and args.height else "")
          + f", {args.fps} fps"
          + (f", grabbing {args.frames} frames" if args.frames > 0 else "")
    )
    print("Press Ctrl-C to stop early.\n")

    running = True
    def _stop(sig, frame):
        nonlocal running
        running = False
    signal.signal(signal.SIGINT,  _stop)
    signal.signal(signal.SIGTERM, _stop)

    cam.start()

    if args.frames > 0:
        deadline = time.monotonic() + args.frames / args.fps + 2.0
        while running and time.monotonic() < deadline:
            time.sleep(0.1)
    else:
        while running:
            time.sleep(0.1)

    cam.stop()
    print("Done.")


if __name__ == "__main__":
    main()
