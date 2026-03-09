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
 * @file    tensorframe_opencv.h
 * @brief   Zero-copy helpers: TensorFrame::Plane ↔ OpenCV GpuMat / Mat
 *
 * All functions are zero-copy: they wrap the existing buffer pointer.
 * The caller is responsible for keeping the TensorFrame alive while the
 * returned Mat / GpuMat is in use.
 *
 * Plane layout assumed: CHW, C-contiguous, dtype UInt8 (CV_8U).
 *
 * Requirements:
 *   - OpenCV built with CUDA support (opencv_core, opencv_cudaarithm, ...)
 *   - LIMEF_CUDA defined for GPU helpers
 */

#include "limef/frame/tensorframe.h"

#include <opencv2/core.hpp>
#ifdef LIMEF_CUDA
#include <opencv2/core/cuda.hpp>
#endif

namespace Limef::opencv {

// ---------------------------------------------------------------------------
// CPU helpers
// ---------------------------------------------------------------------------

/**
 * Wrap a CPU plane as a cv::Mat (CV_8UC1).
 * CHW plane (ndim==3) → (C*H, W); 2D plane → (H, W).
 */
inline cv::Mat toMat(const Limef::frame::Plane& p)
{
    assert(p.data_ != nullptr && "toMat: plane is not a CPU plane");
    assert(p.ndim >= 2 && "toMat: plane must have at least 2 dims");

    int rows, cols;
    size_t step;
    if (p.ndim == 3) {
        rows = static_cast<int>(p.shape[0]) * static_cast<int>(p.shape[1]);
        cols = static_cast<int>(p.shape[2]);
        step = static_cast<size_t>(p.strides[1]);
    } else {
        rows = static_cast<int>(p.shape[0]);
        cols = static_cast<int>(p.shape[1]);
        step = static_cast<size_t>(p.strides[0]);
    }
    return cv::Mat(rows, cols, CV_8UC1, p.data_, step);
}

/**
 * Wrap channel c of a CHW CPU plane as a (H, W) cv::Mat CV_8UC1.
 */
inline cv::Mat channelMat(const Limef::frame::Plane& p, int c)
{
    assert(p.data_ != nullptr && "channelMat: plane is not a CPU plane");
    assert(p.ndim == 3 && "channelMat: plane must be CHW (ndim==3)");
    assert(c >= 0 && c < static_cast<int>(p.shape[0]) && "channelMat: channel out of range");

    const int H       = static_cast<int>(p.shape[1]);
    const int W       = static_cast<int>(p.shape[2]);
    const size_t step = static_cast<size_t>(p.strides[1]);
    uint8_t* ptr      = p.data_ + static_cast<size_t>(c) * static_cast<size_t>(p.strides[0]);
    return cv::Mat(H, W, CV_8UC1, ptr, step);
}

// ---------------------------------------------------------------------------
// GPU helpers
// ---------------------------------------------------------------------------

#ifdef LIMEF_CUDA

/**
 * Wrap a GPU plane as a cv::cuda::GpuMat (CV_8UC1).
 * CHW plane (ndim==3) → (C*H, W); 2D plane → (H, W).
 */
inline cv::cuda::GpuMat toGpuMat(const Limef::frame::Plane& p)
{
    assert(p.d_data_ != nullptr && "toGpuMat: plane is not a GPU plane");
    assert(p.ndim >= 2 && "toGpuMat: plane must have at least 2 dims");

    int rows, cols;
    size_t step;
    if (p.ndim == 3) {
        rows = static_cast<int>(p.shape[0]) * static_cast<int>(p.shape[1]);
        cols = static_cast<int>(p.shape[2]);
        step = static_cast<size_t>(p.strides[1]);
    } else {
        rows = static_cast<int>(p.shape[0]);
        cols = static_cast<int>(p.shape[1]);
        step = static_cast<size_t>(p.strides[0]);
    }
    return cv::cuda::GpuMat(rows, cols, CV_8UC1, p.d_data_, step);
}

/**
 * Wrap channel c of a CHW GPU plane as a (H, W) cv::cuda::GpuMat CV_8UC1.
 */
inline cv::cuda::GpuMat channelGpuMat(const Limef::frame::Plane& p, int c)
{
    assert(p.d_data_ != nullptr && "channelGpuMat: plane is not a GPU plane");
    assert(p.ndim == 3 && "channelGpuMat: plane must be CHW (ndim==3)");
    assert(c >= 0 && c < static_cast<int>(p.shape[0]) && "channelGpuMat: channel out of range");

    const int H       = static_cast<int>(p.shape[1]);
    const int W       = static_cast<int>(p.shape[2]);
    const size_t step = static_cast<size_t>(p.strides[1]);
    uint8_t* ptr      = p.d_data_ + static_cast<size_t>(c) * static_cast<size_t>(p.strides[0]);
    return cv::cuda::GpuMat(H, W, CV_8UC1, ptr, step);
}

#endif // LIMEF_CUDA

} // namespace Limef::opencv
