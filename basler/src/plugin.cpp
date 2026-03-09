/**
 * @file  plugin.cpp
 * @brief LimefBasler plugin — compilation unit
 *
 * Forces instantiation of the header-only classes into this .so and
 * exposes a version symbol for runtime sanity checks.
 */

#include "basler_camera_thread.h"

extern "C" const char* limef_basler_version() {
    return LIMEF_BASLER_VERSION;  // defined by CMakeLists.txt via -D
}
