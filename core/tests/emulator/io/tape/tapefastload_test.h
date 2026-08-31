#pragma once

#include <vector>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/tape/tape.h"
#include "emulator/io/tape/tapefastload.h"
#include "pch.h"
#include "stdafx.h"

/// Unit tests for the fast tape loading trap (design §12.1:
/// docs/inprogress/2026-08-30-fast-tape-loading).
///
/// TAP images are synthesized in-test (header+data pair builder with correct
/// XOR checksums) and written under scratch/. The LD-BYTES signature is
/// injected directly into the mapped bank 0, so the arm state never depends
/// on which ROM the default model happened to load.
class TapeFastLoad_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    TapeCUT* _tape = nullptr;
    TapeFastLoadCUT* _fastLoad = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;

    /// region <Helpers>

    /// Copy the known LD-BYTES prologue bytes into the bank currently mapped
    /// at Z80 $0000-$3FFF (raw physical write — bypasses ROM protection).
    void InjectLDBytesSignature();

    /// Flip the first signature byte so CheckROMSignature() must fail.
    void CorruptLDBytesSignature();

    /// Build one TAP block body: flag + payload + XOR checksum.
    static std::vector<uint8_t> MakeTAPBlock(uint8_t flag, const std::vector<uint8_t>& payload);

    /// Build a 17-byte ROM header payload (Program type + zero-padded name).
    static std::vector<uint8_t> MakeHeaderPayload(const std::string& name, uint16_t dataLength);

    /// Standard two-block image used by most tests: 17-byte header + 8-byte
    /// data. Payloads are returned through the out-params for later comparison.
    static std::vector<std::vector<uint8_t>> MakeHeaderDataPair(std::vector<uint8_t>& headerPayloadOut,
                                                                std::vector<uint8_t>& dataPayloadOut);

    /// Write TAP block bodies (as produced by MakeTAPBlock) to a scratch file
    /// with the standard little-endian length prefixes. Returns full path.
    static std::string WriteTAPFile(const std::string& name, const std::vector<std::vector<uint8_t>>& blocks);

    /// Set up CPU state for a LD-BYTES invocation and stack the return address.
    void SetupLDBytesInvocation(uint8_t flag, uint16_t length, uint16_t dest, uint16_t returnAddress, bool carry = true);

    /// Read a range of Z80 memory without triggering debug logic.
    std::vector<uint8_t> ReadZ80Memory(uint16_t start, size_t length);

    /// endregion </Helpers>
};
