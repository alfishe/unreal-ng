#pragma once

/// @file ttdv1feeder.h
/// @brief Feeds a recorded v1 session to TimeTravelEngine, frame by frame (verification).
///
/// A v1 `.ttd` file holds a snapshot of every frame boundary. The feeder
/// walks the checkpoints of a session loaded into a TimeTravelManager,
/// decodes each one's RAM and hands the engine the pieces whose content
/// changed since the previous checkpoint, with the checkpoint's CPU, chipset
/// and device state. v1's key-frame re-stores of unchanged pieces are not
/// fed: the engine sees what really changed.
///
/// Verification code, not part of the engine: the engine never depends on
/// v1. Design: docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md §4.2.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ttd
{
class TimeTravelManager;
class TimeTravelEngine;
}  // namespace ttd

namespace ttd::bench
{

struct FeedStats
{
    size_t checkpoints = 0;
    size_t changedPieces = 0;   ///< pieces handed to the engine, the first frame's included
    size_t changedRamPieces = 0;   ///< of which machine RAM (region 0)
    size_t events = 0;          ///< input, network and marker events fed
    size_t eventsRefused = 0;   ///< events before the first checkpoint
};

/// Machine RAM of v1 checkpoint @p index, decoded: @p ram gets pieces × 4 KB,
/// @p present one flag per piece (0 = the session had not touched it)
bool DecodeV1Ram(const TimeTravelManager& v1, size_t index, std::vector<uint8_t>& ram,
                 std::vector<uint8_t>& present, std::string& error);

/// Split a v1 General Sound blob (registers + RAM) as the engine stores it:
/// @p fixedState gets the registers, @p ram the card RAM. False when the
/// checkpoint has no General Sound blob
bool SplitV1GeneralSound(const std::vector<uint8_t>& blob, std::vector<uint8_t>& fixedState, std::vector<uint8_t>& ram);

/// Start a session on @p engine with machine RAM as region 0 (and the General
/// Sound RAM as region 1 when the session has that card) and feed it every
/// checkpoint of the session loaded into @p v1. @p blockPieces sets the
/// reference-table block size (0 = the engine's default for the region size)
bool FeedV1Session(const TimeTravelManager& v1, TimeTravelEngine& engine, std::string& error,
                   FeedStats* stats = nullptr, uint32_t blockPieces = 0);

}  // namespace ttd::bench
