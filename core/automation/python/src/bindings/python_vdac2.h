#pragma once

/// @file python_vdac2.h
/// @brief pybind11 bindings for the TS-Conf VDAC2 card (FT812): the bus
/// capture to an .evr replay stream, the line budget metrics. Same surface as the Lua bindings,
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
             "start_clock, last_clock")
        .def("vdac2_metrics",
             [](Emulator& self, bool lines, bool inFlight) {
                 Vdac2Control::FrameMetrics m;
                 std::string error;
                 if (!Vdac2Control::GetFrameMetrics(self.GetContext(), m, lines, inFlight, &error))
                     throw std::runtime_error("vdac2_metrics: " + error);
                 py::dict d;
                 d["valid"] = m.valid;
                 d["frame"] = m.frame;
                 d["lines"] = m.lines;
                 d["hard_budget"] = m.hardBudget;
                 d["soft_budget"] = m.softBudget;
                 d["worst_line"] = m.worstLine;
                 d["worst_clocks"] = m.worstClocks;
                 d["total_clocks"] = m.totalClocks;
                 d["lines_over_soft"] = m.linesOverSoft;
                 d["lines_over_hard"] = m.linesOverHard;
                 d["margin"] = m.margin;
                 d["measure_always"] = m.measureAlways;
                 if (lines)
                 {
                     py::list costs;
                     for (uint16_t clocks : m.lineClocks)
                         costs.append(clocks);
                     d["line_clocks"] = costs;
                 }
                 if (inFlight)
                 {
                     py::dict flight;
                     flight["known"] = m.inFlightKnown;
                     flight["lines_passed"] = m.inFlightLinesPassed;
                     if (lines && m.inFlightKnown)
                     {
                         py::list costs;
                         for (int32_t clocks : m.inFlightLineClocks)
                             costs.append(clocks);
                         flight["line_clocks"] = costs;
                     }
                     d["in_flight"] = flight;
                 }
                 return d;
             },
             "FT812 line budget of the last finished frame: valid, frame, lines, hard_budget, soft_budget, "
             "worst_line, worst_clocks, total_clocks, lines_over_soft, lines_over_hard, margin, measure_always; "
             "lines=True adds line_clocks, in_flight=True the frame in flight (paused machine: known, "
             "lines_passed, line_clocks with -1 for lines passed without drawing)",
             py::arg("lines") = false, py::arg("in_flight") = false)
        .def("vdac2_metrics_set",
             [](Emulator& self, py::object margin, py::object measureAlways) {
                 std::string error;
                 if (!margin.is_none() &&
                     !Vdac2Control::SetLineBudgetMargin(self.GetContext(), margin.cast<uint32_t>(), &error))
                     throw std::runtime_error("vdac2_metrics_set: " + error);
                 if (!measureAlways.is_none() &&
                     !Vdac2Control::SetMeasureAlways(self.GetContext(), measureAlways.cast<bool>(), &error))
                     throw std::runtime_error("vdac2_metrics_set: " + error);
                 if (margin.is_none() && measureAlways.is_none() && !Vdac2Control::HasCard(self.GetContext(), &error))
                     throw std::runtime_error("vdac2_metrics_set: " + error);
             },
             "Set the soft budget margin (percent 0..50) and/or measure_always (draw and measure every frame); "
             "None keeps a value",
             py::arg("margin") = py::none(), py::arg("measure_always") = py::none());
}

}  // namespace PythonBindings
