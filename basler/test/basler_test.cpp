/**
 * @file    basler_test.cpp
 * @brief   Unit tests for the Basler camera plugin.
 *
 * Tests 1    : static (no hardware / no Pylon emulator needed)
 * Tests 2-4  : require PYLON_CAMEMU=1 (set by the test runner via tests.yaml env)
 *
 * Build:  cd build_debug && ../run_cmake.bash && make -j$(nproc)
 * Run:    cd testing && ./runone.bash basler_test:1
 */

// Pylon must come before any X11 / limef headers (see basler_camera_thread.h)
#include "limef/basler/basler_camera_thread.h"

#include "limef/framefilter/simple.h"
#include "limef/framefilter/dump.h"
#include "limef/frame/frame.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

// ─────────────────────────────────────────────────────────────────────────────
// Counting framefilter — tallies incoming frame types
// ─────────────────────────────────────────────────────────────────────────────

class CountFrameFilter : public Limef::ff::SimpleFrameFilter {
    FRAMEFILTER_CLASS(CountFrameFilter);
public:
    explicit CountFrameFilter(std::string name) : SimpleFrameFilter(std::move(name)) {}

    std::atomic<int> decoded_count{0};
    std::atomic<int> tensor_count{0};
    std::atomic<int> stream_count{0};

    void go(const Limef::frame::Frame* frame) override {
        switch (frame->getFrameClass()) {
            case Limef::frame::FrameClass::Decoded: ++decoded_count; break;
            case Limef::frame::FrameClass::Tensor:  ++tensor_count;  break;
            case Limef::frame::FrameClass::Stream:  ++stream_count;  break;
            default: break;
        }
        pass(frame);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 — pylonFormatToFFmpeg static mapping (no Pylon / no hardware)
// ─────────────────────────────────────────────────────────────────────────────

int test_1()
{
    using Limef::basler::BaslerCamera;

    bool ok = true;

    auto check = [&](const std::string& fmt, AVPixelFormat expected, const char* label) {
        AVPixelFormat got = BaslerCamera::pylonFormatToFFmpeg(fmt);
        if (got == expected) {
            printf("%s: ok\n", label);
        } else {
            printf("%s: FAIL (got %d, expected %d)\n", label, (int)got, (int)expected);
            ok = false;
        }
    };

    check("YUV422_YUYV_Packed", AV_PIX_FMT_YUYV422, "YUYV422");
    check("Mono8",              AV_PIX_FMT_GRAY8,    "GRAY8");
    check("BGR8",               AV_PIX_FMT_BGR24,    "BGR24");
    check("RGB8",               AV_PIX_FMT_RGB24,    "RGB24");
    check("Unknown",            AV_PIX_FMT_NONE,     "AV_PIX_FMT_NONE");

    return ok ? 0 : 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2 — BaslerCameraThread, emulator, Color mode → DecodedFrame
// ─────────────────────────────────────────────────────────────────────────────

int test_2()
{
    const int WANT_FRAMES = 5;
    const int TIMEOUT_MS  = 10000;

    CountFrameFilter counter("counter");

    Limef::basler::BaslerCameraContext ctx;
    ctx.mode          = Limef::basler::BaslerCameraContext::Mode::Color;
    ctx.output_format = AV_PIX_FMT_NV12;

    Limef::basler::BaslerCameraThread cam("basler-cam", ctx);
    cam.getOutput().cc(counter);
    cam.start();

    int waited = 0;
    while (counter.decoded_count < WANT_FRAMES && waited < TIMEOUT_MS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waited += 100;
    }

    cam.requestStop();
    cam.waitStop();

    printf("stream frames: %d\n",  counter.stream_count.load());
    printf("decoded frames: %d\n", counter.decoded_count.load());

    return (counter.decoded_count >= WANT_FRAMES) ? 0 : 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3 — BaslerCameraThread, emulator, Mono mode → DecodedFrame
// ─────────────────────────────────────────────────────────────────────────────

int test_3()
{
    const int WANT_FRAMES = 5;
    const int TIMEOUT_MS  = 10000;

    CountFrameFilter counter("counter");

    Limef::basler::BaslerCameraContext ctx;
    ctx.mode          = Limef::basler::BaslerCameraContext::Mode::Mono;
    ctx.output_format = AV_PIX_FMT_GRAY8;

    Limef::basler::BaslerCameraThread cam("basler-cam-mono", ctx);
    cam.getOutput().cc(counter);
    cam.start();

    int waited = 0;
    while (counter.decoded_count < WANT_FRAMES && waited < TIMEOUT_MS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waited += 100;
    }

    cam.requestStop();
    cam.waitStop();

    printf("stream frames: %d\n",  counter.stream_count.load());
    printf("decoded frames: %d\n", counter.decoded_count.load());

    return (counter.decoded_count >= WANT_FRAMES) ? 0 : 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4 — BaslerMultispectralThread, emulator, 3 bands → TensorFrame
//
// Note: the Pylon emulator has no filter wheel, so setInt("FilterWheelPosition")
// will warn but not fail.  The thread still grabs N Mono8 frames per cube.
// ─────────────────────────────────────────────────────────────────────────────

int test_4()
{
    const int WANT_CUBES = 2;
    const int TIMEOUT_MS = 15000;

    CountFrameFilter counter("counter");

    Limef::basler::BaslerMultispectralContext ctx;
    ctx.band_filter_values = {0, 1, 2};   // 3 bands
    ctx.filter_settle_ms   = 10;           // short settle for emulator
    ctx.fps                = 5.0;

    Limef::basler::BaslerMultispectralThread cam("basler-ms", ctx);
    cam.getOutput().cc(counter);
    cam.start();

    int waited = 0;
    while (counter.tensor_count < WANT_CUBES && waited < TIMEOUT_MS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waited += 100;
    }

    cam.requestStop();
    cam.waitStop();

    printf("tensor frames: %d\n", counter.tensor_count.load());

    return (counter.tensor_count >= WANT_CUBES) ? 0 : 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: %s <test_number> [verbosity]\n", argv[0]);
        return 1;
    }

    // Verbosity (mirrors base library test convention)
    if (argc > 2) {
        switch (atoi(argv[2])) {
        case 0: spdlog::set_level(spdlog::level::off);      break;
        case 1: spdlog::set_level(spdlog::level::warn);     break;
        case 2: spdlog::set_level(spdlog::level::info);     break;
        case 3: spdlog::set_level(spdlog::level::debug);    break;
        case 4: spdlog::set_level(spdlog::level::trace);    break;
        default: break;
        }
    }

    switch (atoi(argv[1])) {
    case 1: return test_1();
    case 2: return test_2();
    case 3: return test_3();
    case 4: return test_4();
    default:
        printf("No such test %s\n", argv[1]);
        return 1;
    }
}
