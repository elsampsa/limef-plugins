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
 * @file  plugin.cpp
 * @brief LimefOpenCV plugin — compilation unit
 *
 * Forces instantiation of the header-only classes into this .so and
 * exposes a version symbol for runtime sanity checks.
 */

#include "gpu_opencv_thread.h"
#include "tensorframe_opencv.h"

extern "C" const char* limef_opencv_version() {
    return LIMEF_OPENCV_VERSION;  // defined by CMakeLists.txt via -D
}
