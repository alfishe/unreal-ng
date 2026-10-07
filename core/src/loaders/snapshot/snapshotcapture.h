#pragma once

/// @file snapshotcapture.h
/// @brief The save side of the snapshot pipeline (P6, PLAN #84): the machine's "128K view" captured into a
/// snapshot::Image, the question "which formats can this machine be saved in right now, and why not", and the one
/// entry point every surface saves through.
///
/// A snapshot holds what a Spectrum 128K program sees: RAM as logical banks 0-7 (a 48K machine: 5, 2, 0), #7FFD, the
/// CPU, the AY. Most machines ARE that (48K, 128K, +2, +2A, +3, Pentagon, Scorpion) and are captured as they are. Machines
/// with their own memory layout (the Sprinter, TS-Conf, the ATM family) give a view only while they run a Spectrum 128K
/// layout: the Sprinter in a ZX mode reads its banks through the PLD cell table, TS-Conf and the ATMs are checked against
/// their live window map. Otherwise there is no view, and the save is refused with the reason (nothing is written).
///
/// No file is converted into another here: a snapshot goes machine -> file and file -> machine only.
/// Design: docs/inprogress/2026-10-02-snapshot-pipeline/proposal.md, P6.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "emulator/platform.h"
#include "emulator/state/statenode.h"
#include "loaders/snapshot/snapshotimage.h"

class EmulatorContext;

namespace snapshot
{
enum class SaveFormat : uint8_t
{
    Sna,
    Z80,
    Szx,
};

/// "sna", "z80", "szx"
const char* ToText(SaveFormat format);
/// The extension without the dot, any case; nullopt for another
std::optional<SaveFormat> SaveFormatFromExtension(const std::string& extension);

/// What a machine shows a snapshot, taken at one instant. The pointers point into live memory: copy before running
struct MachineView
{
    bool available = true;
    std::string reason;   ///< when not available: why, in words a user can act on
    std::string needs;    ///< when not available, machine-readable: "mode:128k", "zx_mode", "model:..."

    /// What the snapshot says the machine was: the model the formats name it by (a Sprinter in its Pentagon 128 mode
    /// is a Pentagon 128), with the RAM size that goes with it
    MEM_MODEL model = MM_SPECTRUM128;
    uint32_t ramKb = 128;
    std::string machineHint;          ///< Image::machineHint
    std::string timingHint;           ///< Image::timingHint
    bool layout48 = false;            ///< banks 5, 2, 0 only (a 48K machine, a 48K mode)

    std::map<uint16_t, const uint8_t*> banks;   ///< logical bank -> 16384 bytes
    uint8_t p7FFD = 0;                          ///< unused when layout48
    std::optional<uint8_t> p1FFD;
    std::optional<uint8_t> pEFF7;

    std::string note;                 ///< how the view was taken ("Sprinter ZX mode 'Pentagon 128': banks through cells #F0-#F7")
};

/// A machine that is not a plain Spectrum 128K family member gives its view through one of these (PortDecoder owns it)
class ISnapshotCapturePolicy
{
public:
    virtual ~ISnapshotCapturePolicy() = default;
    virtual std::string Name() const = 0;
    /// Look at the machine, write nothing
    virtual MachineView Examine(EmulatorContext& context) const = 0;
};

/// For a machine with its own memory manager (TS-Conf, the ATM family): the 128K view exists while the LIVE window map is a
/// Spectrum 128K (window 0 ROM, windows 1 and 2 RAM pages 5 and 2, window 3 the page #7FFD names), and RAM page n is bank n. Its
/// decoder installs this; in any other layout (the machine's own mode) there is no view and the reason says what differs
class WindowMapCapture : public ISnapshotCapturePolicy
{
public:
    static WindowMapCapture& Instance();
    std::string Name() const override { return "window-map"; }
    MachineView Examine(EmulatorContext& context) const override;
};

/// Is the snapshot a 48K program? A 48K layout, or a 128K-family machine whose paging is locked with bank 0 on top and the
/// normal screen (#7FFD bit 5 set, bits 3 and 0-2 clear, no #1FFD special paging): the other banks cannot be reached and
/// nothing of the running program is lost in the 48K layout of a .sna / .z80. (A .szx keeps the whole machine regardless.)
bool SavesAs48K(bool layout48, size_t bankCount, std::optional<uint8_t> p7ffd, std::optional<uint8_t> p1ffd);
bool SavesAs48K(const Image& image);

/// The machine's view: its capture policy's, else the default one for the model (see the file comment)
MachineView ExamineView(EmulatorContext& context);

struct FormatStatus
{
    SaveFormat format = SaveFormat::Sna;
    bool available = false;
    std::string reason;   ///< when not available
    std::string needs;    ///< when not available: "format:szx" (what would work), "zx_mode", "model:...", "stack"
    std::string note;     ///< when available: what the format cannot hold of this machine's state
};

struct SaveFormats
{
    bool viewAvailable = false;
    std::string machine;   ///< what the snapshot will say: "Pentagon 128", "ZX Spectrum 128K", ...
    std::string view;      ///< how the state is taken (MachineView::note), or why not
    std::vector<FormatStatus> formats;   ///< sna, z80, szx in this order

    const FormatStatus& For(SaveFormat format) const;
    StateNode ToStateNode() const;
};

/// Which formats this machine can be saved in right now. Reads the CPU (a 48K SNA needs a stack inside RAM)
SaveFormats QuerySaveFormats(EmulatorContext& context);

/// The view and the CPU, AY, border, frame position as one image; false when there is no view (`reason` says why)
bool CaptureImage(EmulatorContext& context, const MachineView& view, Image& out);

struct SaveResult
{
    bool ok = false;
    std::string format;     ///< "sna", "z80", "szx"
    std::string path;
    std::string machine;    ///< what the file says it was made on
    std::string reason;     ///< when not ok
    std::string needs;      ///< when not ok, machine-readable (see FormatStatus::needs)
    std::vector<std::string> warnings;
    std::string text;       ///< "saved ..." or the refusal, ready to show

    /// {ok, format, path, machine, reason, needs, warnings, message}: the one form every surface returns
    StateNode ToStateNode() const;
};

/// Capture and write one file. A refusal writes nothing and leaves a partial file nowhere. The caller has stopped the
/// machine (Emulator::SaveSnapshot waits for the pause)
SaveResult SaveSnapshotFile(EmulatorContext& context, SaveFormat format, const std::string& path);
}  // namespace snapshot
