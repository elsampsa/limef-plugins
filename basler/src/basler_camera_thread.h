#pragma once
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
/**
 * @file    basler_camera_thread.h
 * @brief   Limef producer threads for Basler cameras via the Pylon SDK
 *
 * ## Classes
 *
 *   BaslerCamera              — RAII helper: open/configure/grab/close one camera
 *   BaslerCameraThread        — single-band producer (colour or mono, set by Mode)
 *   BaslerMultispectralThread — multi-band producer, emits TensorFrame(N,H,W)
 *
 * ## Output — BaslerCameraThread
 *
 *   1. StreamFrame  (once at startup)
 *   2. DecodedFrame (continuous, converted to ctx.output_format via SwScale)
 *
 *   Mode::Color — requests YUV422_YUYV_Packed, converts to output_format (default NV12)
 *   Mode::Mono  — requests Mono8,              converts to output_format (default GRAY8)
 *
 * ## Output — BaslerMultispectralThread
 *
 *   One TensorFrame per spectral cube, shape (N, H, W), DType::UInt8, CPU.
 *   N = band_filter_values.size().  Timestamp reflects the start of the first grab.
 *
 * ## Testing without hardware
 *
 *   export PYLON_CAMEMU=1   # Pylon presents one virtual colour camera
 */

#include "limef/thread/producer.h"
#include "limef/frame/decodedframe.h"
#include "limef/frame/streamframe.h"
#include "limef/frame/tensorframe.h"
#include "limef/framefilter/swscale.h"

#include <pylon/PylonIncludes.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace Limef::basler {

// ─────────────────────────────────────────────────────────────────────────────
// RAII camera helper
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief RAII wrapper around a single Pylon CInstantCamera.
 *
 * Handles open / configure / StartGrabbing / StopGrabbing / close.
 * Both BaslerCameraThread and BaslerMultispectralThread hold one as a member.
 * PylonInitialize / PylonTerminate are the caller's responsibility.
 */
struct BaslerCamera {

    /**
     * @brief Open and configure the camera.
     *
     * @param serial        Serial number string; "" = first available camera.
     * @param width         Requested width  (0 = keep camera default).
     * @param height        Requested height (0 = keep camera default).
     * @param fps           Requested acquisition frame rate.
     * @param pylon_format  GenICam PixelFormat value, e.g. "Mono8".
     * @param log           Logger to use for messages.
     * @return true on success; actual width / height readable afterwards.
     */
    bool open(const std::string& serial,
              int width, int height, double fps,
              const std::string& pylon_format,
              std::shared_ptr<spdlog::logger> log)
    {
        logger_ = log;
        Pylon::CTlFactory& tl = Pylon::CTlFactory::GetInstance();
        try {
            if (serial.empty()) {
                camera_.Attach(tl.CreateFirstDevice());
            } else {
                Pylon::CDeviceInfo info;
                info.SetSerialNumber(serial.c_str());
                camera_.Attach(tl.CreateDevice(info));
            }
            camera_.Open();
        } catch (const Pylon::GenericException& e) {
            logger_->error("BaslerCamera: cannot open camera: {}", e.what());
            return false;
        }

        setEnum("PixelFormat", pylon_format);

        if (width  > 0) setInt("Width",  width);
        if (height > 0) setInt("Height", height);

        // Enable frame-rate control (may not be available on all models)
        try {
            Pylon::CBooleanParameter(camera_.GetNodeMap(),
                                     "AcquisitionFrameRateEnable").SetValue(true);
            Pylon::CFloatParameter(camera_.GetNodeMap(),
                                   "AcquisitionFrameRate").SetValue(fps);
        } catch (const Pylon::GenericException& e) {
            logger_->warn("BaslerCamera: cannot set frame rate: {}", e.what());
        }

        // Read back actual dimensions
        try {
            actual_width_  = static_cast<int>(
                Pylon::CIntegerParameter(camera_.GetNodeMap(), "Width").GetValue());
            actual_height_ = static_cast<int>(
                Pylon::CIntegerParameter(camera_.GetNodeMap(), "Height").GetValue());
        } catch (const Pylon::GenericException& e) {
            logger_->error("BaslerCamera: cannot read dimensions: {}", e.what());
            camera_.Close();
            return false;
        }

        logger_->info("BaslerCamera: opened {}x{} @ {:.1f} fps format={}",
                      actual_width_, actual_height_, fps, pylon_format);
        camera_.StartGrabbing();
        return true;
    }

    void close() {
        if (camera_.IsGrabbing()) camera_.StopGrabbing();
        if (camera_.IsOpen())     camera_.Close();
    }

    bool isOpen() const { return camera_.IsOpen(); }

    /** @brief Retrieve one grab result (blocks up to timeout_ms). */
    bool retrieve(Pylon::CGrabResultPtr& result, int timeout_ms = 5000) {
        try {
            return camera_.RetrieveResult(
                timeout_ms, result, Pylon::TimeoutHandling_Return);
        } catch (const Pylon::GenericException& e) {
            if (logger_) logger_->error("BaslerCamera: RetrieveResult: {}", e.what());
            return false;
        }
    }

    /** @brief Set an integer GenICam node (silently warns on failure). */
    void setInt(const std::string& name, int64_t value) {
        try {
            Pylon::CIntegerParameter(camera_.GetNodeMap(), name.c_str())
                .SetValue(value);
        } catch (const Pylon::GenericException& e) {
            if (logger_) logger_->warn("BaslerCamera: cannot set {} = {}: {}",
                                        name, value, e.what());
        }
    }

    /** @brief Set an enum GenICam node (silently warns on failure). */
    void setEnum(const std::string& name, const std::string& value) {
        try {
            Pylon::CEnumParameter(camera_.GetNodeMap(), name.c_str())
                .SetValue(value.c_str());
        } catch (const Pylon::GenericException& e) {
            if (logger_) logger_->warn("BaslerCamera: cannot set {} = {}: {}",
                                        name, value, e.what());
        }
    }

    int actualWidth()  const { return actual_width_;  }
    int actualHeight() const { return actual_height_; }

private:
    Pylon::CInstantCamera camera_;
    int actual_width_{0};
    int actual_height_{0};
    std::shared_ptr<spdlog::logger> logger_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Context — single-band
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Configuration for BaslerCameraThread.
 */
struct BaslerCameraContext {
    /** @brief Selects the Pylon pixel format and FFmpeg input interpretation. */
    enum class Mode {
        Color,  ///< YUV422_YUYV_Packed → SwScale → output_format (default NV12)
        Mono,   ///< Mono8             → SwScale → output_format (default GRAY8)
    };

    std::string serial{""};       ///< Camera serial; "" = first available
    Slot        slot{1};
    int         width{0};         ///< 0 = camera default
    int         height{0};        ///< 0 = camera default
    double      fps{30.0};
    Mode        mode{Mode::Color};
    AVPixelFormat output_format{AV_PIX_FMT_NV12}; ///< Format after SwScale
};

// ─────────────────────────────────────────────────────────────────────────────
// BaslerCameraThread — single-band
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Basler camera producer thread — colour or mono, single frame per grab.
 *
 * Mirrors the USBCameraThread pattern:
 *   preRun()  — PylonInitialize, open camera, send StreamFrame
 *   task()    — RetrieveResult, wrap buffer, SwScale → output_format, send DecodedFrame
 *   postRun() — close camera, PylonTerminate
 *
 * @code{.cpp}
 * BaslerCameraContext ctx;
 * ctx.mode          = BaslerCameraContext::Mode::Color;
 * ctx.output_format = AV_PIX_FMT_NV12;
 *
 * BaslerCameraThread cam("basler-cam", ctx);
 * DumpFrameFilter    dump("dump");
 * cam.getOutput().cc(dump);
 * cam.start();
 * @endcode
 */
class BaslerCameraThread : public Limef::thread::ProducerThread {
    THREAD_CLASS(BaslerCameraThread);

public:
    explicit BaslerCameraThread(std::string name, const BaslerCameraContext& ctx)
        : ProducerThread(name, ctx.slot, Limef::FrameFifoContext(),
                         static_cast<int>(ctx.fps))
        , ctx_(ctx)
        , swscale_("basler_swscale", ctx.output_format)
    {}

    ~BaslerCameraThread() override {
        camera_.close();
        if (pylon_initialized_) {
            Pylon::PylonTerminate();
        }
    }

protected:
    void preRun() override {
        ProducerThread::preRun();

        Pylon::PylonInitialize();
        pylon_initialized_ = true;

        if (!camera_.open(ctx_.serial, ctx_.width, ctx_.height, ctx_.fps,
                          pylonPixelFormat(), logger)) {
            logger->error("BaslerCameraThread: failed to open camera");
            return;
        }

        swscale_.cc(output_ff);
        sendStreamInfo();
    }

    void postRun() override {
        camera_.close();
        if (pylon_initialized_) {
            Pylon::PylonTerminate();
            pylon_initialized_ = false;
        }
        ProducerThread::postRun();
    }

    std::chrono::microseconds task() override {
        if (!camera_.isOpen()) {
            return std::chrono::microseconds(100'000);
        }

        Pylon::CGrabResultPtr result;
        if (!camera_.retrieve(result)) {
            return std::chrono::microseconds(1'000);
        }

        if (!result->GrabSucceeded()) {
            logger->warn("BaslerCameraThread: grab failed: {}",
                         result->GetErrorDescription().c_str());
            return std::chrono::microseconds(1'000);
        }

        if (fillInputFrame(result)) {
            swscale_.go(&input_frame_);
        }
        return std::chrono::microseconds(0);
    }

private:
    /** @brief Pylon GenICam PixelFormat string for the configured mode. */
    std::string pylonPixelFormat() const {
        return (ctx_.mode == BaslerCameraContext::Mode::Color)
            ? "YUV422_YUYV_Packed"
            : "Mono8";
    }

    /** @brief FFmpeg pixel format matching the raw bytes Pylon delivers. */
    AVPixelFormat ffmpegInputFormat() const {
        return (ctx_.mode == BaslerCameraContext::Mode::Color)
            ? AV_PIX_FMT_YUYV422
            : AV_PIX_FMT_GRAY8;
    }

    void sendStreamInfo() {
        AVCodecParameters* par = avcodec_parameters_alloc();
        if (!par) return;

        par->codec_type = AVMEDIA_TYPE_VIDEO;
        par->codec_id   = AV_CODEC_ID_RAWVIDEO;
        par->width      = camera_.actualWidth();
        par->height     = camera_.actualHeight();
        par->format     = ctx_.output_format;

        stream_frame_.reset();
        stream_frame_.addStreamFromCodecParams(
            par, {1, 1'000'000}, {static_cast<int>(ctx_.fps), 1}, 0);
        stream_frame_.setSlot(ctx_.slot);
        avcodec_parameters_free(&par);

        logger->info("BaslerCameraThread: {}", stream_frame_.dump());
        output_ff.go(&stream_frame_);
    }

    bool fillInputFrame(const Pylon::CGrabResultPtr& result) {
        const AVPixelFormat fmt = ffmpegInputFormat();
        const int w = camera_.actualWidth();
        const int h = camera_.actualHeight();

        if (!input_frame_.reserveVideo(w, h, fmt)) {
            logger->error("BaslerCameraThread: cannot reserve input frame");
            return false;
        }

        // Bytes per pixel for packed formats (YUYV=2, GRAY8=1)
        const int bpp = av_get_bits_per_pixel(av_pix_fmt_desc_get(fmt)) / 8;
        const int src_stride = w * bpp;
        const auto* src = static_cast<const uint8_t*>(result->GetBuffer());
        AVFrame* av = const_cast<AVFrame*>(input_frame_.getFrame());

        for (int y = 0; y < h; ++y) {
            std::memcpy(av->data[0] + y * av->linesize[0],
                        src + y * src_stride,
                        src_stride);
        }

        // Absolute timestamp (wall clock, microseconds since epoch)
        auto abs_ts = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch());
        input_frame_.setAbsoluteTimestamp(abs_ts);
        input_frame_.setMediaType(Limef::frame::DecodedFrame::MediaType::Video);
        input_frame_.setSlot(ctx_.slot);
        input_frame_.setTimeBase({1, 1'000'000});

        // PTS: microseconds elapsed since stream start
        auto now = std::chrono::steady_clock::now();
        if (!stream_start_set_) { stream_start_ = now; stream_start_set_ = true; }
        av->pts = std::chrono::duration_cast<std::chrono::microseconds>(
            now - stream_start_).count();

        return true;
    }

private:
    BaslerCameraContext ctx_;
    BaslerCamera        camera_;
    bool                pylon_initialized_{false};

    Limef::frame::DecodedFrame    input_frame_;
    Limef::frame::StreamFrame     stream_frame_;
    Limef::ff::SwScaleFrameFilter swscale_;

    std::chrono::steady_clock::time_point stream_start_;
    bool                                  stream_start_set_{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// Context — multispectral
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Configuration for BaslerMultispectralThread.
 */
struct BaslerMultispectralContext {
    std::string      serial{""};
    Slot             slot{1};
    int              width{0};
    int              height{0};
    double           fps{1.0};              ///< Cube rate (frames per second of complete cubes)
    std::vector<int> band_filter_values{};  ///< GenICam filter node values, one per band
    int              filter_settle_ms{50};  ///< Wait after each filter change before grabbing
    std::string      filter_node{"FilterWheelPosition"}; ///< GenICam integer node for the filter
};

// ─────────────────────────────────────────────────────────────────────────────
// BaslerMultispectralThread
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Basler camera thread for multispectral (spectral cube) acquisition.
 *
 * Cycles through `ctx.band_filter_values`, setting the filter wheel position
 * before each grab.  All N Mono8 frames are packed into a single CPU
 * TensorFrame of shape (N, H, W) which is emitted once per complete cube.
 *
 * No SwScale — Mono8 bytes are copied directly into the tensor plane.
 *
 * Temporal note: the cube spans N × (exposure + filter_settle_ms).  The scene
 * must be quasi-static for valid band registration.  The cube timestamp
 * reflects the wall-clock time at the start of the first grab.
 *
 * @code{.cpp}
 * BaslerMultispectralContext ctx;
 * ctx.band_filter_values = {0, 1, 2, 3, 4};  // 5 spectral bands
 * ctx.filter_settle_ms   = 60;
 *
 * BaslerMultispectralThread cam("ms-cam", ctx);
 * DumpFrameFilter           dump("dump");
 * cam.getOutput().cc(dump);
 * cam.start();
 * @endcode
 */
class BaslerMultispectralThread : public Limef::thread::ProducerThread {
    THREAD_CLASS(BaslerMultispectralThread);

public:
    explicit BaslerMultispectralThread(std::string name,
                                       const BaslerMultispectralContext& ctx)
        : ProducerThread(name, ctx.slot, Limef::FrameFifoContext(),
                         static_cast<int>(ctx.fps))
        , ctx_(ctx)
    {}

    ~BaslerMultispectralThread() override {
        camera_.close();
        if (pylon_initialized_) {
            Pylon::PylonTerminate();
        }
    }

protected:
    void preRun() override {
        ProducerThread::preRun();

        if (ctx_.band_filter_values.empty()) {
            logger->error("BaslerMultispectralThread: band_filter_values is empty");
            return;
        }

        Pylon::PylonInitialize();
        pylon_initialized_ = true;

        if (!camera_.open(ctx_.serial, ctx_.width, ctx_.height, ctx_.fps,
                          "Mono8", logger)) {
            logger->error("BaslerMultispectralThread: failed to open camera");
            return;
        }

        // Pre-allocate output tensor: one plane of shape (N, H, W)
        const int N = static_cast<int>(ctx_.band_filter_values.size());
        const int H = camera_.actualHeight();
        const int W = camera_.actualWidth();
        const int64_t shape[3] = {N, H, W};
        output_tensor_.reserveCPUPlane(0, 3, shape, Limef::frame::DType::UInt8);
        output_tensor_.setNumPlanes(1);

        logger->info("BaslerMultispectralThread: {}x{} x {} bands, settle={}ms",
                     W, H, N, ctx_.filter_settle_ms);
    }

    void postRun() override {
        camera_.close();
        if (pylon_initialized_) {
            Pylon::PylonTerminate();
            pylon_initialized_ = false;
        }
        ProducerThread::postRun();
    }

    std::chrono::microseconds task() override {
        if (!camera_.isOpen()) {
            return std::chrono::microseconds(100'000);
        }

        const int N = static_cast<int>(ctx_.band_filter_values.size());
        const int H = camera_.actualHeight();
        const int W = camera_.actualWidth();
        uint8_t* const tensor_buf = output_tensor_.planes[0].data_;

        std::chrono::steady_clock::time_point cube_start;

        for (int n = 0; n < N; ++n) {
            // Set filter position
            camera_.setInt(ctx_.filter_node, ctx_.band_filter_values[n]);

            // Wait for filter to settle
            std::this_thread::sleep_for(
                std::chrono::milliseconds(ctx_.filter_settle_ms));

            if (n == 0) {
                cube_start = std::chrono::steady_clock::now();
            }

            // Grab one Mono8 frame
            Pylon::CGrabResultPtr result;
            if (!camera_.retrieve(result) || !result->GrabSucceeded()) {
                logger->warn("BaslerMultispectralThread: grab failed at band {}", n);
                return std::chrono::microseconds(1'000);
            }

            // Copy Mono8 plane into tensor band n (stride = W bytes)
            const auto* src = static_cast<const uint8_t*>(result->GetBuffer());
            std::memcpy(tensor_buf + n * H * W, src, static_cast<size_t>(H * W));
        }

        // Timestamp: wall clock at start of first grab
        auto abs_ts = std::chrono::duration_cast<std::chrono::microseconds>(
            cube_start.time_since_epoch());   // steady_clock epoch, good enough for relative use
        // Use system_clock for absolute wall time
        auto wall_ts = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch());
        output_tensor_.setAbsoluteTimestamp(wall_ts);
        output_tensor_.setSlot(ctx_.slot);

        output_ff.go(&output_tensor_);
        return std::chrono::microseconds(0);
    }

private:
    BaslerMultispectralContext ctx_;
    BaslerCamera               camera_;
    bool                       pylon_initialized_{false};

    Limef::frame::TensorFrame  output_tensor_;
};

} // namespace Limef::basler
