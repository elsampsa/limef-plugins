/*
 * Copyright (c) 2026 Sampsa Riikonen <sampsa.riikonen@iki.fi>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */
/*
 * usb_gpu_pipeline - USB camera to RTSP pipeline with GPU OpenCV processing
 *
 * Pipeline (default, no --modify):
 *   USBCameraThread → UploadGPUFrameFilter → EncodingFrameFilter(NVENC) → RTPMuxer → RTSPServer
 *
 * Pipeline (with --modify, GPU Gaussian blur via OpenCV):
 *   USBCameraThread → UploadGPUFrameFilter → DecodedToTensorFrameFilter
 *       → GPUOpenCVThread (Gaussian blur on TensorFrame)
 *       → TensorToDecodedFrameFilter → EncodingFrameFilter(NVENC) → RTPMuxer → RTSPServer
 *
 * Usage:
 *   ./usb_gpu_pipeline [--device /dev/video0] [--port 8554] [--width 640] [--height 480]
 *   ./usb_gpu_pipeline --modify    # enable GPU Gaussian blur
 *
 * Then connect with:
 *   ffplay rtsp://localhost:8554/live/stream
 *   ffplay -rtsp_transport tcp rtsp://localhost:8554/live/stream
 */

#include <iostream>
#include <string>
#include <atomic>
#include <csignal>
#include <thread>
#include <chrono>
#include <getopt.h>

// Limef base
#include "limef/thread/usbcamera.h"
#include "limef/framefilter/uploadgpu.h"
#include "limef/framefilter/decoded_to_tensor.h"
#include "limef/framefilter/tensor_to_decoded.h"
#include "limef/framefilter/encoding.h"
#include "limef/framefilter/rtp.h"
#include "limef/framefilter/dump.h"
#include "limef/rtsp/rtspserverthread.h"
#include "limef/encode/ffmpeg_encoder.h"

// LimefOpenCV plugin
#include "limef/opencv/gpu_opencv_thread.h"

static std::atomic<bool> g_running{true};

static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        std::cout << "\nShutting down..." << std::endl;
        g_running = false;
    }
}

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  --modify, -m          Enable GPU Gaussian blur via OpenCV\n"
              << "  --device, -d <path>   USB camera device (default: /dev/video0)\n"
              << "  --port,   -p <port>   RTSP port (default: 8554)\n"
              << "  --width,  -w <px>     Capture width (default: 640)\n"
              << "  --height, -H <px>     Capture height (default: 480)\n"
              << "  --fps,    -f <fps>    Capture FPS (default: 30)\n"
              << "  --help,   -h          Show this help\n"
              << "\n"
              << "Example:\n"
              << "  " << prog << " --modify --device /dev/video0 --port 8554\n"
              << "\n"
              << "Then connect with:\n"
              << "  ffplay rtsp://localhost:8554/live/stream\n"
              << std::endl;
}

int main(int argc, char** argv) {
    std::string device = "/dev/video0";
    int port = 8554;
    int width = 640;
    int height = 480;
    int fps = 30;
    bool modify = false;

    static struct option long_options[] = {
        {"modify", no_argument,       0, 'm'},
        {"device", required_argument, 0, 'd'},
        {"port",   required_argument, 0, 'p'},
        {"width",  required_argument, 0, 'w'},
        {"height", required_argument, 0, 'H'},
        {"fps",    required_argument, 0, 'f'},
        {"help",   no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "md:p:w:H:f:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'd': device = optarg; break;
            case 'p': port = std::stoi(optarg); break;
            case 'w': width = std::stoi(optarg); break;
            case 'H': height = std::stoi(optarg); break;
            case 'f': fps = std::stoi(optarg); break;
            case 'm': modify = true; break;
            case 'h':
            default:
                print_usage(argv[0]);
                return (opt == 'h') ? 0 : 1;
        }
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    using namespace Limef;
    using namespace Limef::thread;
    using namespace Limef::ff;

    const int SLOT = 1;
    const char* URL_TAIL = "/live/stream";

    std::cout << "==========================================\n";
    std::cout << "  USB Camera → GPU OpenCV → RTSP Pipeline\n";
    std::cout << "==========================================\n";
    std::cout << "Device:     " << device << "\n";
    std::cout << "Resolution: " << width << "x" << height << " @ " << fps << " fps\n";
    std::cout << "Port:       " << port << "\n";
    std::cout << "URL:        rtsp://localhost:" << port << URL_TAIL << "\n";
    std::cout << "Modify:     " << (modify ? "yes (Gaussian blur)" : "no") << "\n";
    std::cout << "==========================================\n";
    std::cout << "Press Ctrl+C to stop\n\n";

    // --- 1. USB Camera ---
    USBCameraContext cam_ctx(device, SLOT);
    cam_ctx.width = width;
    cam_ctx.height = height;
    cam_ctx.fps = fps;
    cam_ctx.output_format = AV_PIX_FMT_NV12;
    USBCameraThread camera("usb-camera", cam_ctx);

    // --- 2. GPU Upload ---
    UploadGPUParams upload_params(HWAccel::CUDA);
    UploadGPUFrameFilter upload("gpu-upload", upload_params);

    // --- 3. DecodedFrame → TensorFrame ---
    DecodedToTensorFrameFilter d2t("d2t", ChannelOrder::RGB);

    // --- 4. GPU OpenCV processing (from LimefOpenCV plugin) ---
    FrameFifoContext opencv_ctx(false, 5, 0, HWAccel::CUDA, "");
    Limef::opencv::GPUOpenCVThread opencv_thread("gpu-opencv", opencv_ctx);

    // --- 5. TensorFrame → DecodedFrame ---
    TensorToDecodedFrameFilter t2d("t2d", ChannelOrder::RGB);

    // --- 6. NVENC H.264 Encoding ---
    encode::FFmpegEncoderParams enc_params;
    enc_params.codec_id   = AV_CODEC_ID_H264;
    enc_params.hw_accel   = HWAccel::CUDA;
    enc_params.bitrate    = 4 * 1024 * 1024;
    enc_params.preset     = std::string("p1");
    enc_params.tune       = std::string("ull");
    enc_params.max_b_frames = 0;
    enc_params.gop_size   = fps / 2;
    EncodingFrameFilter encoder("nvenc-encoder", enc_params);

    // --- 7. RTP Muxer ---
    RTPMuxerFrameFilter rtp_muxer("rtp-muxer");

    // --- 8. RTSP Server ---
    FrameFifoContext rtsp_ctx(false, 5, 100);
    Limef::rtsp::RTSPServerThread rtsp_server("rtsp-server", rtsp_ctx, port);

    // --- Wire the pipeline ---
    if (modify) {
        camera.getOutput().cc(upload).cc(d2t).cc(opencv_thread.getInput());
        opencv_thread.getOutput().cc(t2d).cc(encoder).cc(rtp_muxer).cc(rtsp_server.getInput());
    } else {
        camera.getOutput().cc(upload).cc(encoder).cc(rtp_muxer).cc(rtsp_server.getInput());
    }

    // --- Start (downstream first) ---
    rtsp_server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    rtsp_server.expose(SLOT, URL_TAIL);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    if (modify) {
        opencv_thread.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    camera.start();

    std::cout << "\nReady! Connect with:\n";
    std::cout << "  ffplay rtsp://localhost:" << port << URL_TAIL << "\n";

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // --- Stop (upstream first) ---
    camera.stop();
    if (modify) opencv_thread.stop();
    rtsp_server.stop();

    std::cout << "Done." << std::endl;
    return 0;
}
