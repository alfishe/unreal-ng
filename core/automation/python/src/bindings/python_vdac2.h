#pragma once

/// @file python_vdac2.h
/// @brief pybind11 bindings for the TS-Conf VDAC2 card (FT812): the bus
/// capture to an .evr replay stream. Same surface as the Lua bindings,
/// through Vdac2Control like every interface. Failures raise RuntimeError
/// with the reason.
/// Design: docs/inprogress/2026-10-01-tsconf-vdac2/ (vdac2-test-corpus.md §4)

#include <pybind11/pybind11.h>

#include <stdexcept>
#include <string>

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/platforms/tsconf/vdac2control.h>

namespace PythonBindings
{

/// Register vdac2_* methods on the Emulator Python class
template <typename EmulatorClass>
inline void registerVdac2Bindings(EmulatorClass& emulatorClass)
{
    namespace py = pybind11;
    emulatorClass
        .def("vdac2_capture_start",
             [](Emulator& self, const std::string& path) {
                 std::string error;
                 if (!Vdac2Control::StartCapture(self.GetContext(), path, &error))
                     throw std::runtime_error("vdac2_capture_start: " + error);
             },
             "Write the VDAC2 card's FT812 bus to an .evr replay stream (chip selects, bytes and answers, FT812 "
             "clocks, per-frame picture hashes); on a running chip the stream begins with the chip's whole state",
             py::arg("path"))
        .def("vdac2_capture_stop",
             [](Emulator& self) {
                 std::string error;
                 if (!Vdac2Control::StopCapture(self.GetContext(), &error))
                     throw std::runtime_error("vdac2_capture_stop: " + error);
             },
             "Finish the running VDAC2 capture")
        .def("vdac2_capture_status",
             [](Emulator& self) {
                 Vdac2Control::CaptureStatus status;
                 std::string error;
                 if (!Vdac2Control::GetCaptureStatus(self.GetContext(), status, &error))
                     throw std::runtime_error("vdac2_capture_status: " + error);
                 py::dict d;
                 d["capturing"] = status.capturing;
                 d["path"] = status.path;
                 d["bytes"] = status.bytesWritten;
                 d["selects"] = status.selects;
                 d["exchanges"] = status.exchanges;
                 d["frames"] = status.frames;
                 d["start_clock"] = status.startClock;
                 d["last_clock"] = status.lastClock;
                 return d;
             },
             "The running or the last VDAC2 capture: capturing, path, bytes, selects, exchanges, frames, "
             "start_clock, last_clock");
}

}  // namespace PythonBindings
