/**
 * @file tapeuisnapshot.h
 * @brief TapeUiSnapshot — the single state carrier from core Tape to the Qt
 *        Tape Manager window (design §9.2).
 *
 * Dependency rule: pure data only. Produced by EmulatorBinding's frame-end
 * hook on the emulator thread (plain POD reads of Tape fields), delivered to
 * the UI thread exclusively through the queued
 * EmulatorBinding::tapeStateChanged(const TapeUiSnapshot&) signal. The window
 * never touches Tape* directly — snapshots in, commands out.
 *
 * Per-tick fields ride every snapshot; generation-scoped fields are valid
 * only while `catalogChanged` is true (the catalog copy is shipped once per
 * image (re)load, never per tick — design §9.3 rule 3).
 */

#pragma once

#include <QMetaType>
#include <QString>

#include <optional>
#include <string>
#include <vector>

#include "emulator/io/tape/tapecatalog.h"  // TapeFastLoadPlan (leaf, no cycle)
#include "emulator/io/tape/tape.h"         // TapePlaybackState, TapePosition

struct TapeUiSnapshot
{
    // ---- per-tick live fields (every snapshot) ----
    QString emulatorId;                       // bound emulator instance id ("" when unbound)
    QString emulatorLabel;                    // human-friendly instance label — symbolic id, else "#" + id tail (r8)
    QString filePath;                         // coreState.tapeFilePath ("" = no tape inserted)
    TapePlaybackState state = TapePlaybackState::Idle;
    std::optional<TapePosition> position;     // nullopt: no image loaded
    size_t cursor = 0;                        // next block to deliver (signal or trap)
    bool fastTapeEnabled = false;             // kFastTape feature toggle (advisory badge)
    bool turboTapeEnabled = false;            // kTurboTape feature toggle (FAST column warp marker)

    // ---- generation-scoped fields (valid when catalogChanged) ----
    uint64_t catalogGeneration = 0;           // bump on image (re)load / image drop
    bool catalogChanged = false;              // true => the fields below are fresh
    bool catalogValid = false;                // EnsureImageLoaded() succeeded for filePath
    QString formatId;                         // "tap"/"tzx"/... as selected by content probe
    std::vector<TapeBlockDescriptor> catalog; // per-block descriptors (copy rides only on change)
    TapeFastLoadPlan plan;                    // whole-image fast-load pre-analysis (advisory)
};

Q_DECLARE_METATYPE(TapeUiSnapshot)
