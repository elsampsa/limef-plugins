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
