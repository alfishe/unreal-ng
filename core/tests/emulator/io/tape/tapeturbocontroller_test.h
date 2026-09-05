#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/tape/tape.h"
#include "emulator/io/tape/tapeturbocontroller.h"
#include "pch.h"
#include "stdafx.h"

/// Unit tests for the turbo tape loading controller (design §9.1:
/// docs/inprogress/2026-09-04-turbo-tape-loading).
///
/// The controller is a pure observer of the tape state machine, so the E1-E6
/// matrix (design §6.1) is validated by driving TapePlaybackState transitions
/// directly on a fixture-owned TapeCUT (mount / start / watchdog-style pause /
/// end-of-tape stop) and asserting the real Core's turbo flag after every
/// controller tick. The 'turbotape' feature is forced ON in SetUp so results
/// never depend on a features.ini that happens to sit in the working
/// directory; the feature-gated rows toggle it explicitly.
class TapeTurboController_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    TapeCUT* _tape = nullptr;
    TapeTurboControllerCUT* _controller = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;

    /// region <Helpers>

    /// Build one TAP block body: flag + payload + XOR checksum.
    static std::vector<uint8_t> MakeTAPBlock(uint8_t flag, const std::vector<uint8_t>& payload);

    /// Write TAP block bodies (as produced by MakeTAPBlock) to a scratch file
    /// with the standard little-endian length prefixes. Returns full path.
    static std::string WriteTAPFile(const std::string& name, const std::vector<std::vector<uint8_t>>& blocks);

    /// Mount a single large data-block tape (~1200 frames of signal): one
    /// image block means a stopPlayback() during playback parks the cursor at
    /// end-of-tape, i.e. the natural Ended state. Leaves the tape Idle.
    void MountSingleBlockTape(const std::string& name);

    /// Start signal playback and bind the in-flight block (StartPlaybackAtCursor
    /// + one frame start) so watchdog pauses and terminal stops behave exactly
    /// like they do under MainLoop. Leaves the tape Playing.
    void StartPlayback();

    /// endregion </Helpers>
};
