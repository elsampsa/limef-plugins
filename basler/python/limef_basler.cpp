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
// plugins/basler/python/limef_basler.cpp
//
// pybind11 Python module: 'import limef_basler'
//
// Exposes:
//   BaslerCameraContext       - configuration for BaslerCameraThread
//   BaslerCameraThread        - colour or mono Basler camera producer
//   BaslerMultispectralContext - configuration for BaslerMultispectralThread
//   BaslerMultispectralThread - spectral-cube Basler camera producer
//
// Requires 'import limef' to be loaded first so that FrameFilter etc. are
// registered in pybind11's global type registry.
//
// Typical usage::
//
//   import limef
//   import limef_basler
//
//   ctx = limef_basler.BaslerCameraContext()
//   ctx.mode          = limef_basler.Mode.Color
//   ctx.output_format = limef.AV_PIX_FMT_NV12
//   ctx.fps           = 30.0
//
//   cam  = limef_basler.BaslerCameraThread('basler-cam', ctx)
//   dump = limef.DumpFrameFilter('dump')
//   cam.cc(dump)
//   cam.start()
//   ...
//   cam.stop()

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "basler_camera_thread.h"

namespace py = pybind11;
using namespace Limef::basler;

PYBIND11_MODULE(limef_basler, m) {
    m.doc() =
        "Limef Basler camera plugin.\n\n"
        "Provides producer threads for Basler cameras via the Pylon SDK.\n\n"
        "Import limef before this module so that shared types (FrameFilter etc.)\n"
        "are registered in pybind11's type registry.\n\n"
        "Test without hardware by setting PYLON_CAMEMU=1 in the environment.";

    m.attr("__version__") = LIMEF_BASLER_VERSION;

    // ── BaslerCameraContext::Mode enum ────────────────────────────────────────

    py::enum_<BaslerCameraContext::Mode>(m, "Mode",
        "Pixel format mode for BaslerCameraThread.\n\n"
        "  Color — requests YUV422_YUYV_Packed; converts to output_format via SwScale\n"
        "  Mono  — requests Mono8;              converts to output_format via SwScale")
        .value("Color", BaslerCameraContext::Mode::Color)
        .value("Mono",  BaslerCameraContext::Mode::Mono)
        .export_values();

    // ── BaslerCameraContext ───────────────────────────────────────────────────

    py::class_<BaslerCameraContext>(m, "BaslerCameraContext",
        "Configuration for BaslerCameraThread.\n\n"
        "Attributes:\n"
        "    serial        Camera serial number; empty string = first available camera.\n"
        "    slot          Stream slot identifier (default: 1).\n"
        "    width         Requested capture width;  0 = camera default.\n"
        "    height        Requested capture height; 0 = camera default.\n"
        "    fps           Requested frame rate (default: 30.0).\n"
        "    mode          Mode.Color or Mode.Mono (default: Mode.Color).\n"
        "    output_format AVPixelFormat after SwScale conversion (default: AV_PIX_FMT_NV12).")
        .def(py::init<>())
        .def_readwrite("serial",        &BaslerCameraContext::serial)
        .def_readwrite("slot",          &BaslerCameraContext::slot)
        .def_readwrite("width",         &BaslerCameraContext::width)
        .def_readwrite("height",        &BaslerCameraContext::height)
        .def_readwrite("fps",           &BaslerCameraContext::fps)
        .def_readwrite("mode",          &BaslerCameraContext::mode)
        .def_readwrite("output_format", &BaslerCameraContext::output_format);

    // ── BaslerCameraThread ────────────────────────────────────────────────────

    py::class_<BaslerCameraThread,
               std::shared_ptr<BaslerCameraThread>>(m, "BaslerCameraThread",
        "Basler camera producer thread — colour or mono, one DecodedFrame per grab.\n\n"
        "Sends a StreamFrame at startup then a continuous stream of DecodedFrames\n"
        "converted to ctx.output_format via SwScale.\n\n"
        "Typical pipeline::\n\n"
        "    ctx             = limef_basler.BaslerCameraContext()\n"
        "    ctx.mode        = limef_basler.Mode.Color\n"
        "    cam             = limef_basler.BaslerCameraThread('basler', ctx)\n"
        "    dump            = limef.DumpFrameFilter('dump')\n"
        "    cam.cc(dump)\n"
        "    cam.start()\n\n"
        "Test without hardware: export PYLON_CAMEMU=1")
        .def(py::init([](const std::string& name, const BaslerCameraContext& ctx) {
                 return std::make_shared<BaslerCameraThread>(name, ctx);
             }),
             py::arg("name"), py::arg("ctx"),
             "Args:\n"
             "    name: label for logging\n"
             "    ctx:  BaslerCameraContext")
        .def("cc",
             [](BaslerCameraThread& self, py::object next) -> py::object {
                 self.getOutput().cc(py::cast<Limef::ff::FrameFilter&>(next));
                 return next;
             },
             py::arg("next"),
             "Connect output to a FrameFilter; returns the filter (enables chaining).\n"
             "Must be called before start().")
        .def("getOutput",
             [](BaslerCameraThread& self) -> Limef::ff::FrameFilter& {
                 return self.getOutput();
             },
             py::return_value_policy::reference_internal,
             "Return the output FrameFilter for manual chaining.")
        .def("start",       &BaslerCameraThread::start,
             "Start the camera thread. Call after the filter chain is fully connected.")
        .def("stop",
             [](BaslerCameraThread& self) {
                 py::gil_scoped_release release;
                 self.stop();
             },
             "Request stop and block until the thread finishes.")
        .def("requestStop", &BaslerCameraThread::requestStop,
             "Request stop (non-blocking). Pair with waitStop().")
        .def("waitStop",
             [](BaslerCameraThread& self) {
                 py::gil_scoped_release release;
                 self.waitStop();
             },
             "Block until the thread finishes. Call after requestStop().")
        .def("isStarted",   &BaslerCameraThread::isStarted,
             "Return True if the thread is running.");

    // ── BaslerMultispectralContext ─────────────────────────────────────────────

    py::class_<BaslerMultispectralContext>(m, "BaslerMultispectralContext",
        "Configuration for BaslerMultispectralThread.\n\n"
        "Attributes:\n"
        "    serial             Camera serial; empty = first available.\n"
        "    slot               Stream slot identifier (default: 1).\n"
        "    width              Requested width;  0 = camera default.\n"
        "    height             Requested height; 0 = camera default.\n"
        "    fps                Cube rate — complete spectral cubes per second (default: 1.0).\n"
        "    band_filter_values List of integer filter wheel positions, one per band.\n"
        "    filter_settle_ms   Milliseconds to wait after each filter change (default: 50).\n"
        "    filter_node        GenICam node name for the filter wheel (default: FilterWheelPosition).")
        .def(py::init<>())
        .def_readwrite("serial",             &BaslerMultispectralContext::serial)
        .def_readwrite("slot",               &BaslerMultispectralContext::slot)
        .def_readwrite("width",              &BaslerMultispectralContext::width)
        .def_readwrite("height",             &BaslerMultispectralContext::height)
        .def_readwrite("fps",                &BaslerMultispectralContext::fps)
        .def_readwrite("band_filter_values", &BaslerMultispectralContext::band_filter_values)
        .def_readwrite("filter_settle_ms",   &BaslerMultispectralContext::filter_settle_ms)
        .def_readwrite("filter_node",        &BaslerMultispectralContext::filter_node);

    // ── BaslerMultispectralThread ──────────────────────────────────────────────

    py::class_<BaslerMultispectralThread,
               std::shared_ptr<BaslerMultispectralThread>>(m, "BaslerMultispectralThread",
        "Basler camera thread for spectral cube acquisition.\n\n"
        "Cycles through band_filter_values, grabbing one Mono8 frame per band.\n"
        "Emits a TensorFrame of shape (N, H, W) once per complete cube.\n\n"
        "Typical pipeline::\n\n"
        "    ctx                    = limef_basler.BaslerMultispectralContext()\n"
        "    ctx.band_filter_values = [0, 1, 2, 3, 4]\n"
        "    ctx.filter_settle_ms   = 60\n"
        "    cam                    = limef_basler.BaslerMultispectralThread('ms', ctx)\n"
        "    dump                   = limef.DumpFrameFilter('dump')\n"
        "    cam.cc(dump)\n"
        "    cam.start()")
        .def(py::init([](const std::string& name,
                         const BaslerMultispectralContext& ctx) {
                 return std::make_shared<BaslerMultispectralThread>(name, ctx);
             }),
             py::arg("name"), py::arg("ctx"),
             "Args:\n"
             "    name: label for logging\n"
             "    ctx:  BaslerMultispectralContext")
        .def("cc",
             [](BaslerMultispectralThread& self, py::object next) -> py::object {
                 self.getOutput().cc(py::cast<Limef::ff::FrameFilter&>(next));
                 return next;
             },
             py::arg("next"),
             "Connect output to a FrameFilter; returns the filter (enables chaining).")
        .def("getOutput",
             [](BaslerMultispectralThread& self) -> Limef::ff::FrameFilter& {
                 return self.getOutput();
             },
             py::return_value_policy::reference_internal)
        .def("start",       &BaslerMultispectralThread::start)
        .def("stop",
             [](BaslerMultispectralThread& self) {
                 py::gil_scoped_release release;
                 self.stop();
             })
        .def("requestStop", &BaslerMultispectralThread::requestStop)
        .def("waitStop",
             [](BaslerMultispectralThread& self) {
                 py::gil_scoped_release release;
                 self.waitStop();
             })
        .def("isStarted",   &BaslerMultispectralThread::isStarted);
}
