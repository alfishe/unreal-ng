#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "emulator/platform.h"

class Emulator;
class EmulatorContext;

/// Options of MachineStateTransfer (outside the class: a default argument of the class's own members)
struct MachineStateTransferOptions
{
    /// Keep the source's in-frame position when both models share the frame geometry (frame length and
    /// INT window). Otherwise the target starts at its frame start
    bool keepFramePositionWhenTimingMatches = true;
};

/// @brief Moves the running state of one machine into another, in memory, without serializing to a file
/// @details The in-memory counterpart of saving a snapshot on one instance and loading it on another.
///          It reads the source machine (CPU, RAM, paging, border, TR-DOS paging, every device the TTD
///          registry knows, and the device memory that TTD blobs do not carry: NeoGS RAM and flash,
///          MoonSound wave SRAM), decides whether the target can hold that state, and writes it.
///
///          Two ways, chosen automatically:
///          - **Clone** (same model, same RAM size): the full state, as a TTD checkpoint restore does -
///            chipset latches, the in-frame position, every device including the model's own paging state.
///          - **Cross-model** (another model): what the target can express - pages, CPU, the #7FFD /
///            #1FFD / #EFF7 latches replayed through the target's port decoder, TR-DOS paging, and the
///            devices that do not depend on the machine (sound cards, Covox, Kempston mouse). The target
///            starts its own frame with its own timing, unless both models share the frame geometry.
///
///          What it refuses (the report says why, per item): a state the target cannot express (pages the
///          target lacks, +2A/+3 all-RAM modes on a 128K, extended Pentagon / Scorpion paging on another
///          family, model-specific machines such as ATM / Profi / TSConf on another model), and devices whose
///          configuration differs (e.g. a 512 KB GS into a 128 KB GS).
///
///          Media (disk and tape images) are not moved; the report lists that.
class MachineStateTransfer
{
public:
    enum class ItemStatus : uint8_t
    {
        Copied,    ///< Transferred
        Dropped,   ///< The source has it, the target does not (or cannot take it); the rest proceeds
        Refused,   ///< The target cannot hold the state; the whole transfer is refused
        Note       ///< Information (e.g. the frame position restarted)
    };

    struct Item
    {
        std::string name;
        ItemStatus status = ItemStatus::Copied;
        std::string detail;
    };

    struct Report
    {
        bool ok = false;           ///< The transfer is possible (Check) / was applied (Transfer)
        bool clone = false;        ///< Same model and RAM size: full-fidelity path
        std::string reason;        ///< Why not, when !ok
        std::vector<Item> items;   ///< What was (or would be) copied, dropped, refused

        /// Human-readable multi-line summary
        std::string ToString() const;
        /// Items with the given status
        size_t Count(ItemStatus status) const;
    };

    using Options = MachineStateTransferOptions;

    /// @brief Decide whether the target can take the source's state; changes nothing
    static Report Check(EmulatorContext& source, EmulatorContext& target);

    /// @brief Write the source's state into the target (both must be paused, or not yet started)
    /// @details Context-level core of Transfer(): no pausing, no TTD handling. Checks first and changes
    ///          nothing when the check fails
    static Report Apply(EmulatorContext& source, EmulatorContext& target, const Options& options = {});

    /// @brief Transfer between two existing instances
    /// @details Pauses both (running instances resume afterwards), refuses while the target records a TTD
    ///          session, drops the target's TTD session, applies, and restarts the target's frame
    static Report Transfer(Emulator& source, Emulator& target, const Options& options = {});

    /// @brief Create a new instance of the given model and transfer the source's state into it
    /// @details The new instance gets the source's sound cards (TurboSound slot, General Sound / NeoGS,
    ///          MoonSound, Covox, SounDrive) and Beta 128 setting before its devices are built, so their state
    ///          can follow. The instance is initialized, not started. On failure nothing is left behind.
    /// @param ramSizeKB 0 = the model's default RAM size
    /// @return The new instance, or nullptr (report.reason says why)
    static std::shared_ptr<Emulator> TransferToNewInstance(Emulator& source, const std::string& modelName,
                                                           uint32_t ramSizeKB, Report& report,
                                                           const Options& options = {},
                                                           const std::string& symbolicId = "");

    /// @brief The source's configuration items that decide which devices exist, applied onto a target config
    /// @details Used by TransferToNewInstance; public so callers creating instances their own way can fit the
    ///          same cards
    static void FitSourceDevices(const CONFIG& source, CONFIG& target);
};
