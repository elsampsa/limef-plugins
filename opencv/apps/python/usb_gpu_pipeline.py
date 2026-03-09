"""
usb_gpu_pipeline.py — USB camera → GPU OpenCV Gaussian blur → RTSP

Pipeline (default):
    USBCameraThread → UploadGPUFrameFilter → EncodingFrameFilter(NVENC) → RTPMuxer → RTSPServer

Pipeline (--modify):
    USBCameraThread → UploadGPUFrameFilter → DecodedToTensorFrameFilter
        → GPUOpenCVThread (Gaussian blur)
        → TensorToDecodedFrameFilter → EncodingFrameFilter(NVENC) → RTPMuxer → RTSPServer

Usage:
    python usb_gpu_pipeline.py [--modify] [--device /dev/video0] [--port 8554]

Requires:
    LD_LIBRARY_PATH to include the OpenCV CUDA lib dir (when not installed system-wide)
"""

import argparse
import signal
import time

import limef
import limef_opencv


def main():
    parser = argparse.ArgumentParser(description="USB camera → GPU OpenCV → RTSP")
    parser.add_argument("--modify", action="store_true", help="Enable GPU Gaussian blur")
    parser.add_argument("--device", default="/dev/video0")
    parser.add_argument("--port",   type=int, default=8554)
    parser.add_argument("--width",  type=int, default=640)
    parser.add_argument("--height", type=int, default=480)
    parser.add_argument("--fps",    type=int, default=30)
    args = parser.parse_args()

    SLOT     = 1
    URL_TAIL = "/live/stream"

    # --- 1. USB Camera ---
    cam_ctx = limef.USBCameraContext(args.device, SLOT)
    cam_ctx.width  = args.width
    cam_ctx.height = args.height
    cam_ctx.fps    = args.fps
    cam_ctx.output_format = limef.AV_PIX_FMT_NV12
    camera = limef.USBCameraThread("usb-camera", cam_ctx)

    # --- 2. GPU Upload ---
    upload = limef.UploadGPUFrameFilter("gpu-upload", limef.HWACCEL_CUDA)

    # --- 3. NVENC Encoder ---
    enc = limef.FFmpegEncoderParams()
    enc.codec_id     = limef.AV_CODEC_ID_H264
    enc.hw_accel     = limef.HWACCEL_CUDA
    enc.bitrate      = 4 * 1024 * 1024
    enc.preset       = "p1"
    enc.tune         = "ull"
    enc.max_b_frames = 0
    enc.gop_size     = args.fps // 2
    encoder = limef.EncodingFrameFilter("nvenc-encoder", enc)

    # --- 4. RTP + RTSP ---
    rtp_muxer   = limef.RTPMuxerFrameFilter("rtp-muxer")
    rtsp_server = limef.RTSPServerThread("rtsp-server", args.port)

    if args.modify:
        # GPU OpenCV processing via limef_opencv plugin
        d2t    = limef.DecodedToTensorFrameFilter("d2t", limef.CHANNEL_ORDER_RGB)
        opencv = limef_opencv.GPUOpenCVThread("gpu-opencv")
        t2d    = limef.TensorToDecodedFrameFilter("t2d", limef.CHANNEL_ORDER_RGB)

        camera.cc(upload).cc(d2t).cc(opencv.getInput())
        opencv.cc(t2d).cc(encoder).cc(rtp_muxer).cc(rtsp_server.getInput())
    else:
        camera.cc(upload).cc(encoder).cc(rtp_muxer).cc(rtsp_server.getInput())

    # --- Start (downstream first) ---
    rtsp_server.start()
    time.sleep(0.1)
    rtsp_server.expose(SLOT, URL_TAIL)

    if args.modify:
        opencv.start()
        time.sleep(0.1)

    camera.start()

    print(f"Ready — connect with:")
    print(f"  ffplay rtsp://localhost:{args.port}{URL_TAIL}")

    running = True
    def _stop(sig, frame):
        nonlocal running
        running = False
    signal.signal(signal.SIGINT,  _stop)
    signal.signal(signal.SIGTERM, _stop)

    while running:
        time.sleep(0.1)

    # --- Stop (upstream first) ---
    camera.stop()
    if args.modify:
        opencv.stop()
    rtsp_server.stop()
    print("Done.")


if __name__ == "__main__":
    main()
