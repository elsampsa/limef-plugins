// plugins/opencv/python/limef_opencv.cpp
//
// pybind11 Python module: 'import limef_opencv'
//
// Exposes:
//   GPUOpenCVThread  - applies GPU Gaussian blur to TensorFrames via OpenCV CUDA
//
// Requires 'import limef' to be loaded first so that FrameFilter, HWAccel, etc.
// are registered in pybind11's global type registry.
//
// Typical usage::
//
//   import limef
//   import limef_opencv
//
//   cam     = limef.USBCameraThread('cam', ctx)
//   upload  = limef.UploadGPUFrameFilter('upload', limef.HWACCEL_CUDA)
//   d2t     = limef.DecodedToTensorFrameFilter('d2t', limef.CHANNEL_ORDER_RGB)
//   opencv  = limef_opencv.GPUOpenCVThread('opencv')
//   t2d     = limef.TensorToDecodedFrameFilter('t2d', limef.CHANNEL_ORDER_RGB)
//
//   cam.cc(upload).cc(d2t).cc(opencv.getInput())
//   opencv.cc(t2d).cc(...)
//
//   opencv.start()
//   cam.start()
//   ...
//   cam.stop()
//   opencv.stop()

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "gpu_opencv_thread.h"   // from plugin src/

namespace py = pybind11;

PYBIND11_MODULE(limef_opencv, m) {
    m.doc() =
        "Limef OpenCV CUDA plugin.\n\n"
        "Provides GPU-accelerated image processing threads and framefilters\n"
        "built on OpenCV CUDA, integrated into the Limef framefilter pipeline.\n\n"
        "Import limef before this module so that shared types (FrameFilter etc.)\n"
        "are registered in pybind11's type registry.";

    m.attr("__version__") = LIMEF_OPENCV_VERSION;

    // ── GPUOpenCVThread ───────────────────────────────────────────────────────

    py::class_<Limef::opencv::GPUOpenCVThread,
               std::shared_ptr<Limef::opencv::GPUOpenCVThread>>(m, "GPUOpenCVThread",
        "Thread that applies GPU Gaussian blur to TensorFrames via OpenCV CUDA.\n\n"
        "Receives TensorFrames with shape (3, H, W) UInt8 on GPU, applies per-channel\n"
        "Gaussian blur, and emits TensorFrames downstream.\n\n"
        "Non-TensorFrame frames (SignalFrame, StreamFrame, etc.) pass through unchanged.\n\n"
        "Typical pipeline::\n\n"
        "    cam    = limef.USBCameraThread('cam', ctx)\n"
        "    upload = limef.UploadGPUFrameFilter('upload', limef.HWACCEL_CUDA)\n"
        "    d2t    = limef.DecodedToTensorFrameFilter('d2t', limef.CHANNEL_ORDER_RGB)\n"
        "    opencv = limef_opencv.GPUOpenCVThread('opencv')\n"
        "    t2d    = limef.TensorToDecodedFrameFilter('t2d', limef.CHANNEL_ORDER_RGB)\n\n"
        "    cam.cc(upload).cc(d2t).cc(opencv.getInput())\n"
        "    opencv.cc(t2d).cc(...)\n\n"
        "    opencv.start()\n"
        "    cam.start()\n"
        "    ...\n"
        "    cam.stop()\n"
        "    opencv.stop()")
        .def(py::init([](const std::string& name, int fifo_size) {
                 Limef::FrameFifoContext ctx(
                     false,                   // leaky = false
                     fifo_size,               // stack size
                     0,                       // timeout_ms: 0 = wait forever
                     Limef::HWAccel::CUDA,    // GPU target
                     ""
                 );
                 return std::make_shared<Limef::opencv::GPUOpenCVThread>(name, ctx);
             }),
             py::arg("name")      = "gpu-opencv",
             py::arg("fifo_size") = 5,
             "Args:\n"
             "    name:      label for logging (default: 'gpu-opencv')\n"
             "    fifo_size: internal TensorFrame fifo depth (default: 5)")
        .def("getInput",
             [](Limef::opencv::GPUOpenCVThread& self) -> Limef::ff::FrameFilter& {
                 return self.getInput();
             },
             py::return_value_policy::reference_internal,
             "Return the input framefilter.\n\n"
             "Connect the upstream chain to this::\n\n"
             "    d2t.cc(opencv.getInput())\n"
             "    # or equivalently:\n"
             "    cam.cc(upload).cc(d2t).cc(opencv.getInput())")
        .def("cc",
             [](Limef::opencv::GPUOpenCVThread& self, py::object next_obj) -> py::object {
                 self.getOutput().cc(py::cast<Limef::ff::FrameFilter&>(next_obj));
                 return next_obj;
             },
             py::arg("next"),
             "Connect thread output to a filter; return the filter (enables chaining).\n\n"
             "Must be called before start()::\n\n"
             "    opencv.cc(t2d).cc(encoder)")
        .def("start",       &Limef::opencv::GPUOpenCVThread::start,
             "Start the processing thread. Call after the filter chain is fully connected.")
        .def("stop",
             [](Limef::opencv::GPUOpenCVThread& self) {
                 py::gil_scoped_release release;
                 self.stop();
             },
             "Request stop and block until the thread finishes.")
        .def("requestStop", &Limef::opencv::GPUOpenCVThread::requestStop,
             "Request stop (non-blocking). Pair with waitStop().")
        .def("waitStop",
             [](Limef::opencv::GPUOpenCVThread& self) {
                 py::gil_scoped_release release;
                 self.waitStop();
             },
             "Block until the thread finishes. Call after requestStop().")
        .def("isStarted",   &Limef::opencv::GPUOpenCVThread::isStarted,
             "Return True if the thread is running.");
}
