#pragma once

/// @file loaderszx.h
/// @brief The SZX snapshot loader and writer (design §6-§9), the same public
/// shape as LoaderSNA / LoaderZ80: load() and save() on a path, plus
/// Commit() / Capture() on a parsed stage for callers that hold one.
///
/// Scope: every standard block for hardware we emulate - CRTR, Z80R (with
/// the exact frame position, MEMPTR, Q, HALT, the interrupt shadow), SPCR,
/// RAMP, AY, B128 (the WD1793 registers, #FF and TR-DOS paging), BDSK, +3,
/// DSK, TAPE, GS + GSRP (classic GS), COVX, AMXM (Kempston), KEYB. Blocks for
/// hardware we do not emulate (Issue 2 keyboard, joysticks, SpecDrum, IF1,
/// Multiface, Timex, ...) are listed in the report as ignored.
///
/// Media: a linked image is looked up next to the snapshot first, then at
/// the stored path, and always inserted with Session access (the guest's
/// writes never reach the linked file); an embedded image is staged in a
/// temporary file that goes with the medium. Saving links file-backed media
/// (relative to the snapshot's folder when inside it). Standard SZX only: the snapshot's machine must be the running one
/// (no model switch), and a model without an SZX machine id cannot be saved.
///
/// Frame position: SZX counts dwCyclesStart from the INT; our Z80::t counts
/// from the frame start and the INT is first seen at intstart + 1. Worked
/// example (Pentagon, frame 71680, intstart 71635): dwCyclesStart 100 loads
/// as t = (71636 + 100) mod 71680 = 56.

#include <string>

#include "loaders/snapshot/szx/szxformat.h"
#include "loaders/snapshot/snapshotimage.h"
#include "loaders/snapshot/snapshotpipeline.h"
#include "loaders/snapshot/snapshotreport.h"

class EmulatorContext;

class LoaderSZX
{
public:
    LoaderSZX(EmulatorContext* context, const std::string& path);

    bool load();
    bool save();

    const szx::Report& GetReport() const { return _report; }
    const std::string& GetError() const { return _error; }

    /// What the caller asked for (call before load(); the default lets the plan decide)
    void SetOptions(const snapshot::Options& options) { _options = options; }
    /// The pipeline's view of the last load: the image of the file and the report (the plan's verdicts + SZX's blocks)
    const snapshot::Image& GetSnapshotImage() const { return _image; }
    const snapshot::Report& GetSnapshotReport() const { return _snapshotReport; }
    /// A parsed stage as a SnapshotImage; nothing touches the machine
    static snapshot::Image BuildImage(const szx::Stage& stage, const std::string& path);
    /// Read a file and build its image, without a machine or a commit (inspect)
    static bool ReadImage(const std::string& path, snapshot::Image& image, std::string& error);
    /// SZX's per-block outcomes and warnings onto the pipeline's report (the two Outcome enums differ)
    static void AppendReport(const szx::Report& from, snapshot::Report& to);

    /// Can a running machine (model, RAM in KB) load a snapshot of `machine`? The loader's own rule: the same model, a Scorpion on a
    /// Scorpion with ProfROM, a smaller Pentagon on a bigger one
    static bool Suits(MEM_MODEL runningModel, uint32_t runningRamKb, const szx::Machine& machine);

    /// The machine an SZX file was saved on, from its header alone (the GUI
    /// creates that machine before loading). False: not an SZX file, or a
    /// machine id we do not emulate (`error` says which)
    static bool ProbeMachine(const std::string& path, szx::Machine& machine, std::string& error);

    /// Apply a parsed stage to the running machine
    static bool Commit(EmulatorContext* context, const szx::Stage& stage, szx::Report& report, std::string& error);
    /// The same, with the machine state (RAM, paging, CPU, AY, border) read from `image` - the stage's BuildImage, as the plan
    /// left it - and the rest (the model check, media, devices, the version rules) from the stage, whose payloads the neutral
    /// image only describes
    static bool CommitImage(EmulatorContext* context, const snapshot::Image& image, const szx::Stage& stage, szx::Report& report,
                            std::string& error);
    /// The running machine as a stage
    static bool Capture(EmulatorContext* context, szx::Stage& stage, std::string& error);

    /// Our frame position for a count from the INT, and back
    static uint32_t FramePositionFromIntCount(EmulatorContext* context, uint32_t cyclesFromInt);
    static uint32_t IntCountFromFramePosition(EmulatorContext* context, uint32_t framePosition);

private:
    static void ApplyPaging(EmulatorContext* context, const snapshot::Image& image, const szx::Stage& stage, szx::Report& report);
    static void ApplyCpu(EmulatorContext* context, const snapshot::Image& image, const szx::Stage& stage, szx::Report& report);
    /// BDSK, +3 / DSK, TAPE: linked or embedded media into the media manager
    static void ApplyMedia(EmulatorContext* context, const szx::Stage& stage, szx::Report& report);
    /// GS, COVX, AMXM, KEYB, JOY, DRUM
    static void ApplyDevices(EmulatorContext* context, const szx::Stage& stage, szx::Report& report);
    static void CaptureMedia(EmulatorContext* context, szx::Stage& stage);
    static void CaptureDevices(EmulatorContext* context, szx::Stage& stage);

    EmulatorContext* _context;
    std::string _path;
    szx::Report _report;
    std::string _error;
    snapshot::Options _options;
    snapshot::Image _image;
    snapshot::Report _snapshotReport;
    snapshot::Decision _decision;
};
