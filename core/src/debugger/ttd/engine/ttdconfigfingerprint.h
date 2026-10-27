#pragma once

/// @file ttdconfigfingerprint.h
/// @brief The machine settings a session was recorded with (Phase 3, Step 4; FR-14).
///
/// A fingerprint is a list of named values: the model and its memory, the
/// frame and interrupt timing, the audio and screen rendering settings, the
/// board options that change timing (Sprinter turbo, Profi switches, ...) and
/// a signature of the machine's ROM set (every model's, the Sprinter BIOS
/// among them), and the machine's slot set: `slots.<slot>` per fitted card
/// (a hash of the card, its options and adapter) and `slots.builtin.<id>`
/// per switchable built-in (ZX-bus slots SL-5, affectsRestore). The devices
/// themselves and their firmware are not here: the engine's device table
/// already compares them on every restore (Phase 2).
///
/// Worked example: a session recorded with the Reference decimator is
/// opened on a machine set to HighFidelity. Compare gives one difference,
/// {"sound.decimator_high_fidelity", "0", "1", affectsRestore = false}:
/// restoring a checkpoint is still exact, a replay from it is not bit-exact
/// (the sound chips filter differently), and the session says which setting.
/// Design: docs/inprogress/2026-09-25-ttd-v2-migration/phase-3-replay-inputs-tdd.md §4.5.

#include <cstdint>
#include <string>
#include <vector>

namespace ttd
{

struct TTDConfigField
{
    std::string name;            ///< "timing.frame", "rom.signature", ...
    uint64_t value = 0;
    /// A difference also makes a checkpoint restore inexact (the model, the
    /// RAM size); otherwise only a replay from it
    bool affectsRestore = false;

    bool operator==(const TTDConfigField& o) const
    {
        return name == o.name && value == o.value && affectsRestore == o.affectsRestore;
    }
};

struct TTDConfigFingerprint
{
    std::vector<TTDConfigField> fields;   ///< in a fixed order (the capture's)

    void Add(std::string name, uint64_t value, bool affectsRestore = false)
    {
        fields.push_back({std::move(name), value, affectsRestore});
    }
    const TTDConfigField* Find(const std::string& name) const;
    bool Empty() const { return fields.empty(); }
    bool operator==(const TTDConfigFingerprint& o) const { return fields == o.fields; }
    bool operator!=(const TTDConfigFingerprint& o) const { return !(*this == o); }
};

/// One setting that differs between the recording and the live machine
struct TTDFingerprintDiff
{
    std::string field;
    std::string recorded;   ///< "-" when the recording has no such field
    std::string live;       ///< "-" when the live machine has none
    bool affectsRestore = false;
};

/// The settings that differ, in the recorded fingerprint's order, then the
/// live machine's extra fields
std::vector<TTDFingerprintDiff> Compare(const TTDConfigFingerprint& recorded, const TTDConfigFingerprint& live);

}  // namespace ttd
