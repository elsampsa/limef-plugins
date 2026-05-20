/**
 * @file    opencv_test.cpp
 * @brief   Unit tests for the LimefOpenCV plugin.
 *
 * Test 1   : static (CPU-only, no GPU needed)
 *              toMat / channelMat wrappers — verify shape and data pointer
 * Tests 2-3: CUDA GPU required
 *              toGpuMat / channelGpuMat — verify shape
 *              GPUOpenCVThread — feed GPU TensorFrames, verify output
 *
 * Build:  cd build_debug && cmake .. && make -j$(nproc)
 * Run:    cd testing && ./runone.bash opencv_test:1
 */

#include "limef/opencv/tensorframe_opencv.h"
#include "limef/opencv/gpu_opencv_thread.h"

#include "limef/frame/tensorframe.h"
#include "limef/framefilter/simple.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

// ─────────────────────────────────────────────────────────────────────────────
// Counting framefilter
// ─────────────────────────────────────────────────────────────────────────────

class CountFrameFilter : public Limef::ff::SimpleFrameFilter {
    FRAMEFILTER_CLASS(CountFrameFilter);
public:
    explicit CountFrameFilter(std::string name) : SimpleFrameFilter(std::move(name)) {}
    std::atomic<int> tensor_count{0};
    void go(const Limef::frame::Frame* frame) override {
        if (frame->getFrameClass() == Limef::frame::FrameClass::Tensor) ++tensor_count;
        pass(frame);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 — toMat / channelMat: CPU wrappers (no GPU)
// ─────────────────────────────────────────────────────────────────────────────

int test_1()
{
    const int C = 3, H = 64, W = 80;
    const int64_t shape[3] = {C, H, W};

    Limef::frame::TensorFrame tf;
    tf.setNumPlanes(1);
    tf.reserveCPUPlane(0, 3, shape, Limef::frame::DType::UInt8);

    const auto& p = tf.planes[0];
    bool ok = true;

    // toMat: CHW → (C*H, W)
    cv::Mat m = Limef::opencv::toMat(p);
    if (m.rows != C * H || m.cols != W || m.data != p.data_) {
        printf("toMat: FAIL (rows=%d cols=%d ptr_match=%d)\n",
               m.rows, m.cols, (m.data == p.data_));
        ok = false;
    } else {
        printf("toMat: ok\n");
    }

    // channelMat: channel 1 → (H, W)
    cv::Mat cm = Limef::opencv::channelMat(p, 1);
    uint8_t* expected_ptr = p.data_ + static_cast<size_t>(p.strides[0]);
    if (cm.rows != H || cm.cols != W || cm.data != expected_ptr) {
        printf("channelMat: FAIL (rows=%d cols=%d ptr_match=%d)\n",
               cm.rows, cm.cols, (cm.data == expected_ptr));
        ok = false;
    } else {
        printf("channelMat: ok\n");
    }

    return ok ? 0 : 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2 — toGpuMat / channelGpuMat: GPU wrappers
// ─────────────────────────────────────────────────────────────────────────────

int test_2()
{
    const int C = 3, H = 64, W = 80;
    const int64_t shape[3] = {C, H, W};

    Limef::frame::TensorFrame tf;
    tf.setNumPlanes(1);
    tf.reserveGPUPlane(0, 3, shape, Limef::frame::DType::UInt8, 0);

    const auto& p = tf.planes[0];
    bool ok = true;

    // toGpuMat: CHW → (C*H, W)
    cv::cuda::GpuMat gm = Limef::opencv::toGpuMat(p);
    if (gm.rows != C * H || gm.cols != W || gm.data != p.d_data_) {
        printf("toGpuMat: FAIL (rows=%d cols=%d ptr_match=%d)\n",
               gm.rows, gm.cols, (gm.data == p.d_data_));
        ok = false;
    } else {
        printf("toGpuMat: ok\n");
    }

    // channelGpuMat: channel 2 → (H, W)
    cv::cuda::GpuMat cgm = Limef::opencv::channelGpuMat(p, 2);
    uint8_t* expected_ptr = p.d_data_ + static_cast<size_t>(2) * static_cast<size_t>(p.strides[0]);
    if (cgm.rows != H || cgm.cols != W || cgm.data != expected_ptr) {
        printf("channelGpuMat: FAIL (rows=%d cols=%d ptr_match=%d)\n",
               cgm.rows, cgm.cols, (cgm.data == expected_ptr));
        ok = false;
    } else {
        printf("channelGpuMat: ok\n");
    }

    return ok ? 0 : 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3 — GPUOpenCVThread: push GPU TensorFrames, verify output
// ─────────────────────────────────────────────────────────────────────────────

int test_3()
{
    const int WANT_FRAMES = 5;
    const int TIMEOUT_MS  = 10000;
    const int C = 3, H = 64, W = 80;
    const int64_t shape[3] = {C, H, W};

    CountFrameFilter counter("counter");

    // target=CUDA_FFMPEG: TensorFrameFifo does H2D upload, so we push CPU frames
    Limef::FrameFifoContext ctx(false, 10, 0, Limef::frame::BufferLocation::CUDA_FFMPEG);
    Limef::opencv::GPUOpenCVThread proc("gpu-opencv", ctx);
    proc.getOutput().cc(counter);
    proc.start();  // blocks until preRun() completes — fifo is ready on return

    // CPU TensorFrame — the TensorFrameFifo uploads it to GPU automatically
    Limef::frame::TensorFrame input;
    input.setNumPlanes(1);
    input.reserveCPUPlane(0, 3, shape, Limef::frame::DType::UInt8);
    input.setSlot(1);

    // Push WANT_FRAMES frames via the thread's input framefilter
    for (int i = 0; i < WANT_FRAMES; ++i) {
        proc.getInput().go(&input);
    }

    // Wait for all frames to be processed
    int waited = 0;
    while (counter.tensor_count < WANT_FRAMES && waited < TIMEOUT_MS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waited += 100;
    }

    proc.requestStop();
    proc.waitStop();

    printf("tensor frames out: %d\n", counter.tensor_count.load());

    return (counter.tensor_count >= WANT_FRAMES) ? 0 : 1;
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

    if (argc > 2) {
        switch (atoi(argv[2])) {
        case 0: spdlog::set_level(spdlog::level::off);   break;
        case 1: spdlog::set_level(spdlog::level::warn);  break;
        case 2: spdlog::set_level(spdlog::level::info);  break;
        case 3: spdlog::set_level(spdlog::level::debug); break;
        case 4: spdlog::set_level(spdlog::level::trace); break;
        default: break;
        }
    }

    switch (atoi(argv[1])) {
    case 1: return test_1();
    case 2: return test_2();
    case 3: return test_3();
    default:
        printf("No such test %s\n", argv[1]);
        return 1;
    }
}
