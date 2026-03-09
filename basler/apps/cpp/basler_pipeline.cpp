/*
 * basler_pipeline — Basler camera grab demo
 *
 * Default pipeline (--frames N):
 *   BaslerCameraThread → DumpFrameFilter
 *
 * Grabs N frames from the camera, prints frame info, then exits.
 * Use PYLON_CAMEMU=1 to run without physical hardware.
 *
 * Usage:
 *   export PYLON_CAMEMU=1
 *   ./basler_pipeline [options]
 *
 * Options:
 *   --serial, -s <serial>   Camera serial number (default: first available)
 *   --width,  -w <px>       Capture width  (default: camera default)
 *   --height, -H <px>       Capture height (default: camera default)
 *   --fps,    -f <fps>      Frame rate (default: 30)
 *   --mono,   -m            Use Mono8 mode (default: Color / YUV422)
 *   --frames, -n <count>    Number of frames to grab (default: 30)
 *   --help,   -h
 */

#include <iostream>
#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>
#include <getopt.h>

#include "limef/framefilter/dump.h"
#include "limef/basler/basler_camera_thread.h"

static std::atomic<bool> g_running{true};

static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::cout << "\nShutting down..." << std::endl;
        g_running = false;
    }
}

static void print_usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [options]\n"
        << "\n"
        << "Options:\n"
        << "  --serial, -s <serial>   Camera serial (default: first available)\n"
        << "  --width,  -w <px>       Capture width  (default: camera default)\n"
        << "  --height, -H <px>       Capture height (default: camera default)\n"
        << "  --fps,    -f <fps>      Frame rate (default: 30)\n"
        << "  --mono,   -m            Use Mono8 mode (default: Color)\n"
        << "  --frames, -n <count>    Frames to grab then exit (default: 30, 0=run until Ctrl-C)\n"
        << "  --help,   -h            Show this help\n"
        << "\n"
        << "Test without hardware:\n"
        << "  PYLON_CAMEMU=1 " << prog << "\n"
        << std::endl;
}

int main(int argc, char** argv) {
    std::string serial;
    int  width  = 0;
    int  height = 0;
    double fps  = 30.0;
    bool mono   = false;
    int  frames = 30;

    static struct option long_options[] = {
        {"serial",  required_argument, nullptr, 's'},
        {"width",   required_argument, nullptr, 'w'},
        {"height",  required_argument, nullptr, 'H'},
        {"fps",     required_argument, nullptr, 'f'},
        {"mono",    no_argument,       nullptr, 'm'},
        {"frames",  required_argument, nullptr, 'n'},
        {"help",    no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "s:w:H:f:mn:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 's': serial  = optarg;            break;
            case 'w': width   = std::stoi(optarg); break;
            case 'H': height  = std::stoi(optarg); break;
            case 'f': fps     = std::stod(optarg); break;
            case 'm': mono    = true;              break;
            case 'n': frames  = std::stoi(optarg); break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ── Build context ─────────────────────────────────────────────────────────

    Limef::basler::BaslerCameraContext ctx;
    ctx.serial        = serial;
    ctx.width         = width;
    ctx.height        = height;
    ctx.fps           = fps;
    ctx.mode          = mono ? Limef::basler::BaslerCameraContext::Mode::Mono
                             : Limef::basler::BaslerCameraContext::Mode::Color;
    ctx.output_format = mono ? AV_PIX_FMT_GRAY8 : AV_PIX_FMT_NV12;

    // ── Build pipeline ────────────────────────────────────────────────────────

    Limef::ff::DumpFrameFilter dump("dump");
    Limef::basler::BaslerCameraThread cam("basler-cam", ctx);

    cam.getOutput().cc(dump);

    // ── Run ───────────────────────────────────────────────────────────────────

    std::cout << "Starting Basler camera ("
              << (mono ? "Mono" : "Color") << " mode)";
    if (!serial.empty()) std::cout << ", serial=" << serial;
    if (width)  std::cout << ", w=" << width;
    if (height) std::cout << ", h=" << height;
    std::cout << ", fps=" << fps;
    if (frames > 0) std::cout << ", grabbing " << frames << " frames";
    std::cout << "\nPress Ctrl-C to stop early.\n" << std::endl;

    cam.start();

    if (frames > 0) {
        // Wait for N frames then stop (simple wall-clock estimate)
        double wait_s = static_cast<double>(frames) / fps + 2.0;
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(static_cast<int>(wait_s * 1000));
        while (g_running && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } else {
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    cam.stop();
    std::cout << "Done." << std::endl;
    return 0;
}
