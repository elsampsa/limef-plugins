#!/usr/bin/env python3
"""
basler_rtsp.py — Basler camera RTSP server with Python frame processing

Pipeline:
    BaslerCameraThread(BGR24) → PythonInterface
    Python consumer: pull → Gaussian blur → push
    SwScaleFrameFilter(YUV420P) → EncodingFrameFilter(VP8)
        → RTSPMuxerFrameFilter → RTSPServerThread

Usage:
    python3 basler_rtsp.py [options]

Then connect with:
    ffplay rtsp://localhost:8554/live/stream
    ffplay -rtsp_transport tcp rtsp://localhost:8554/live/stream

Press Ctrl+C to stop.

Test without hardware:
    PYLON_CAMEMU=1 python3 basler_rtsp.py
"""

import os
import sys
import time
import argparse
import threading

import limef
import limef_basler
import numpy as np

try:
    import cv2
    _CV2 = True
except ImportError:
    _CV2 = False


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
        iface  = d.GetDeviceClass()
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
    p = argparse.ArgumentParser(
        description="Basler camera RTSP server with Python frame processing",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--list",    action="store_true",
                   help="List connected cameras and exit (requires pypylon)")
    p.add_argument("--serial",  default="",
                   help="Camera serial number (default: first available)")
    p.add_argument("--width",   type=int, default=0,
                   help="Capture width  (0 = camera default)")
    p.add_argument("--height",  type=int, default=0,
                   help="Capture height (0 = camera default)")
    p.add_argument("--fps",     type=float, default=30.0,
                   help="Frame rate")
    p.add_argument("--mono",    action="store_true",
                   help="Mono8 mode (default: Color BGR24)")
    p.add_argument("--port",    type=int, default=8554,
                   help="RTSP server port")
    p.add_argument("--url-tail", default="/live/stream", metavar="PATH",
                   help="RTSP URL path component")
    p.add_argument("--bitrate", type=int, default=2_000_000,
                   help="VP8 encoder target bitrate (bits/sec)")
    p.add_argument("--secs",    type=float, default=0.0, metavar="SECS",
                   help="Stop after N seconds (0 = run until Ctrl-C)")
    p.add_argument("--feature-file", default="", metavar="PATH",
                   help="Pylon .pfs feature file to load (focus, aperture, exposure, …)")
    p.add_argument("--no-auto-exposure", action="store_true",
                   help="Disable ExposureAuto (use camera/feature-file setting)")
    args = p.parse_args()

    if args.list:
        list_cameras()
        sys.exit(0)

    if not _CV2:
        print("WARNING: opencv-python not found — blur step skipped (pip install opencv-python)")

    SLOT       = 1
    TIMEOUT_MS = 200
    url_tail   = args.url_tail

    # Basler outputs BGR24 directly so PythonInterface receives it without an
    # extra SwScaleFrameFilter upstream.  Mono mode uses GRAY8 instead.
    pix_fmt = limef.AV_PIX_FMT_GRAY8 if args.mono else limef.AV_PIX_FMT_BGR24

    print("=================================")
    print("  Basler RTSP Server")
    print("=================================")
    if args.serial:
        print(f"Camera:     serial={args.serial}")
    else:
        print("Camera:     first available")
    if args.width and args.height:
        print(f"Resolution: {args.width}x{args.height}")
    print(f"FPS:        {args.fps}")
    print(f"Mode:       {'Mono' if args.mono else 'Color (BGR24)'}")
    print(f"Port:       {args.port}")
    print(f"URL:        rtsp://localhost:{args.port}{url_tail}")
    print(f"Bitrate:    {args.bitrate // 1000} kbps")
    print("=================================")
    print("Press Ctrl+C to stop\n")

    # ── Camera source ──────────────────────────────────────────────────────────

    ctx               = limef_basler.BaslerCameraContext()
    ctx.serial        = args.serial
    ctx.width         = args.width
    ctx.height        = args.height
    ctx.fps           = args.fps
    ctx.mode          = limef_basler.Mode.Mono if args.mono else limef_basler.Mode.Color
    ctx.output_format = pix_fmt
    ctx.feature_file  = args.feature_file
    ctx.exposure_auto = not args.no_auto_exposure

    cam = limef_basler.BaslerCameraThread("basler-cam", ctx)

    # ── Python interface ───────────────────────────────────────────────────────
    # stack_size=10, leaky=True: drop frames if the Python consumer falls behind
    # rather than stalling the camera grab loop.

    pyf = limef.PythonInterface(stack_size=10, leaky=True, fifo_size=0)
    cam.cc(pyf.getInput())

    # ── Downstream chain ───────────────────────────────────────────────────────
    # BGR24 (from Python push) → YUV420P → VP8 encode → RTP mux → RTSP server

    scale_yuv = limef.SwScaleFrameFilter("scale_yuv", limef.AV_PIX_FMT_YUV420P)

    enc_params          = limef.FFmpegEncoderParams()
    enc_params.codec_id = limef.AV_CODEC_ID_VP8
    enc_params.bitrate  = args.bitrate

    encode    = limef.EncodingFrameFilter("encode", enc_params)
    rtp_muxer = limef.RTSPMuxerFrameFilter("rtp_muxer")
    rtsp      = limef.RTSPServerThread("rtsp_server", port=args.port,
                                       stack_size=30, fifo_size=100)

    pyf.getOutput().cc(scale_yuv).cc(encode).cc(rtp_muxer).cc(rtsp.getInput())

    # ── RTSP callbacks ─────────────────────────────────────────────────────────

    rtsp.onStreamRequired(
        lambda slot: print(f"[event] Client subscribed to slot {slot}"))
    rtsp.onStreamNotRequired(
        lambda slot: print(f"[event] No more clients on slot {slot}"))

    # ── Python consumer thread ─────────────────────────────────────────────────

    stop_event  = threading.Event()
    client      = pyf.client()
    video_count = [0]
    t_start     = [0.0]

    def consumer():
        while not stop_event.is_set():
            frame = client.pull(timeout_ms=TIMEOUT_MS)
            if frame is None:
                continue

            if isinstance(frame, limef.StreamFrame):
                print(f"  StreamFrame  slot={frame.slot}  streams={len(frame.streams)}")
                for i, s in enumerate(frame.streams):
                    if s.codec_type == limef.AVMEDIA_TYPE_VIDEO:
                        print(f"    [{i}] video  {s.width}x{s.height}"
                              f"  fps={s.r_frame_rate[0]}/{s.r_frame_rate[1]}")
                client.push(frame)
                continue

            if not isinstance(frame, limef.DecodedFrame) or not frame.is_video:
                continue

            video_count[0] += 1
            w, h = frame.width, frame.height

            if args.mono:
                plane = np.array(frame.planes[0])
                img   = plane[:h, :w]
                blurred = cv2.GaussianBlur(img, (21, 21), 0) if _CV2 else img
                out = limef.DecodedFrame()
                out.reserve_video(w, h, frame.format)
                out.timestamp = frame.timestamp
                out.pts       = frame.pts
                out.slot      = frame.slot
                out.planes    = [np.ascontiguousarray(blurred)]
            else:
                plane = np.array(frame.planes[0])
                img   = plane[:h, :w * 3].reshape(h, w, 3)
                blurred = cv2.GaussianBlur(img, (21, 21), 0) if _CV2 else img
                out = limef.DecodedFrame()
                out.reserve_video(w, h, frame.format)
                out.timestamp = frame.timestamp
                out.pts       = frame.pts
                out.slot      = frame.slot
                out.planes    = [np.ascontiguousarray(blurred.reshape(h, w * 3))]

            client.push(out)

            if video_count[0] % 100 == 1:
                elapsed = time.monotonic() - t_start[0]
                print(f"  video #{video_count[0]:5d}  {w}x{h}"
                      f"  ts={frame.timestamp / 1e6:7.3f} s"
                      f"  elapsed={elapsed:.1f} s")

    consumer_thread = threading.Thread(
        target=consumer, daemon=True, name="limef-consumer")

    # ── Live parameter console ─────────────────────────────────────────────────

    def console():
        print("Live control — type KEY VALUE to adjust camera parameters.")
        print("  Examples: ExposureTime 20000 | ExposureAuto Continuous | Gain 5.0")
        print("  Type 'quit' to stop.\n")
        for line in sys.stdin:
            line = line.strip()
            if line.lower() in ("quit", "exit", "q"):
                stop_event.set()
                break
            parts = line.split(None, 1)
            if len(parts) == 2:
                cam.setParam(parts[0], parts[1])
            elif line:
                print("  Usage: KEY VALUE")

    # ── Start everything ───────────────────────────────────────────────────────

    if sys.stdin.isatty():
        threading.Thread(target=console, daemon=True, name="limef-console").start()

    print("Starting RTSP server ...")
    rtsp.start()
    time.sleep(0.1)

    rtsp.expose(SLOT, url_tail)
    time.sleep(0.05)

    print("Starting Basler camera ...")
    cam.start()
    t_start[0] = time.monotonic()
    consumer_thread.start()

    time.sleep(1.0)
    print(f"\nReady!  Connect with:")
    print(f"  ffplay rtsp://localhost:{args.port}{url_tail}")
    print(f"  ffplay -rtsp_transport tcp rtsp://localhost:{args.port}{url_tail}\n")

    # ── Main loop ──────────────────────────────────────────────────────────────

    try:
        if args.secs > 0:
            time.sleep(args.secs)
        else:
            while not stop_event.is_set():
                time.sleep(0.5)
    except KeyboardInterrupt:
        print("\nShutting down...")

    # ── Cleanup ────────────────────────────────────────────────────────────────

    # Stop camera first so the consumer thread stops receiving frames and exits.
    print("Stopping camera ...")
    cam.stop()

    stop_event.set()
    consumer_thread.join(timeout=2.0)
    elapsed = time.monotonic() - t_start[0]

    print("Stopping RTSP server ...")
    rtsp.stop()

    print(f"\nDone.  {elapsed:.1f} s, {video_count[0]} video frames processed.")
    os._exit(0)  # skip Python GC to avoid C++ extension destructor ordering issues


if __name__ == "__main__":
    main()
