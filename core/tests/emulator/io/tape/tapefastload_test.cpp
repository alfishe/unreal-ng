#include "tapefastload_test.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

#include "_helpers/testpathhelper.h"
#include "emulator/cpu/cputables.h"
#include "emulator/emulator.h"
#include "emulator/spectrumconstants.h"

/// region <SetUp / TearDown>

void TapeFastLoad_Test::SetUp()
{
    _emulator = new Emulator(LoggerLevel::LogError);
    if (!_emulator->Init())
    {
        throw std::runtime_error("Failed to initialize emulator for TapeFastLoad_Test");
    }

    _context = _emulator->GetContext();
    _z80 = _context->pCore->GetZ80();

    _tape = new TapeCUT(_context);
    _fastLoad = new TapeFastLoadCUT(_context, *_tape);

    // Hermetic arm state: bank 0 carries the LD-BYTES prologue regardless of
    // which ROM the default model loaded. The 'fasttape' feature defaults on.
    InjectLDBytesSignature();
}

void TapeFastLoad_Test::TearDown()
{
    if (_fastLoad != nullptr)
    {
        delete _fastLoad;
        _fastLoad = nullptr;
    }

    if (_tape != nullptr)
    {
        delete _tape;
        _tape = nullptr;
    }

    if (_emulator != nullptr)
    {
        _emulator->Stop();
        _emulator->Release();
        delete _emulator;
        _emulator = nullptr;
    }

    _context = nullptr;  // Owned by _emulator, don't delete
    _z80 = nullptr;
}

/// endregion </SetUp / TearDown>

/// region <Helpers>

void TapeFastLoad_Test::InjectLDBytesSignature()
{
    uint8_t* romBank = _context->pMemory->GetPhysicalAddressForZ80Page(0);
    ASSERT_NE(romBank, nullptr);

    // $0556: INC D / EX AF,AF' / DEC D / DI / LD A,$0F / OUT ($FE),A
    static constexpr uint8_t signature1[8] = { 0x14, 0x08, 0x15, 0xF3, 0x3E, 0x0F, 0xD3, 0xFE };
    // $0562: IN A,($FE) / RRA (offset +12 from LD_BYTES — see tapefastload.h)
    static constexpr uint8_t signature2[3] = { 0xDB, 0xFE, 0x1F };

    std::memcpy(romBank + ROMAddresses::LD_BYTES, signature1, sizeof signature1);
    std::memcpy(romBank + ROMAddresses::LD_BYTES + kLDBytesSignature2Offset, signature2, sizeof signature2);
}

void TapeFastLoad_Test::CorruptLDBytesSignature()
{
    uint8_t* romBank = _context->pMemory->GetPhysicalAddressForZ80Page(0);
    ASSERT_NE(romBank, nullptr);

    romBank[ROMAddresses::LD_BYTES] ^= 0xFF;
}

std::vector<uint8_t> TapeFastLoad_Test::MakeTAPBlock(uint8_t flag, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> block;
    block.reserve(payload.size() + 2);

    uint8_t checksum = flag;
    block.push_back(flag);
    for (const uint8_t byte : payload)
    {
        block.push_back(byte);
        checksum ^= byte;
    }
    block.push_back(checksum);

    return block;
}

std::vector<uint8_t> TapeFastLoad_Test::MakeHeaderPayload(const std::string& name, uint16_t dataLength)
{
    std::vector<uint8_t> payload;
    payload.reserve(17);

    payload.push_back(static_cast<uint8_t>(TAP_BLOCK_PROGRAM));
    for (size_t i = 0; i < 10; i++)
    {
        payload.push_back(i < name.size() ? static_cast<uint8_t>(name[i]) : static_cast<uint8_t>(' '));
    }
    payload.push_back(static_cast<uint8_t>(dataLength & 0xFF));
    payload.push_back(static_cast<uint8_t>(dataLength >> 8));
    payload.push_back(0x00);  // autostart line
    payload.push_back(0x00);
    payload.push_back(0x00);  // program length / start offset
    payload.push_back(0x00);

    return payload;
}

std::vector<std::vector<uint8_t>> TapeFastLoad_Test::MakeHeaderDataPair(std::vector<uint8_t>& headerPayloadOut,
                                                                        std::vector<uint8_t>& dataPayloadOut)
{
    headerPayloadOut = MakeHeaderPayload("fast", 8);
    dataPayloadOut = { 0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34, 0x56, 0x78 };

    return { MakeTAPBlock(0x00, headerPayloadOut),
             MakeTAPBlock(0xFF, dataPayloadOut) };
}

std::string TapeFastLoad_Test::WriteTAPFile(const std::string& name, const std::vector<std::vector<uint8_t>>& blocks)
{
    const std::string path = TestPathHelper::GetTestScratchPath("tapefastload/" + name);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (const std::vector<uint8_t>& block : blocks)
    {
        const uint16_t size = static_cast<uint16_t>(block.size());
        out.put(static_cast<char>(size & 0xFF));
        out.put(static_cast<char>((size >> 8) & 0xFF));
        out.write(reinterpret_cast<const char*>(block.data()), static_cast<std::streamsize>(block.size()));
    }

    return path;
}

void TapeFastLoad_Test::SetupLDBytesInvocation(uint8_t flag, uint16_t length, uint16_t dest, uint16_t returnAddress, bool carry)
{
    _z80->pc = ROMAddresses::LD_BYTES;
    _z80->a = flag;
    _z80->f = carry ? FLAG_CF : 0;
    _z80->de = length;
    _z80->ix = dest;

    // Stack the return address — after the CALL pushed it, SP points AT it
    // (the trap's RET path reads [sp] / [sp+1], then pops)
    _z80->sp = 0xFF00;
    _context->pMemory->DirectWriteToZ80Memory(0xFF00, static_cast<uint8_t>(returnAddress & 0xFF));
    _context->pMemory->DirectWriteToZ80Memory(0xFF01, static_cast<uint8_t>(returnAddress >> 8));
}

std::vector<uint8_t> TapeFastLoad_Test::ReadZ80Memory(uint16_t start, size_t length)
{
    std::vector<uint8_t> result(length);
    for (size_t i = 0; i < length; i++)
    {
        result[i] = _context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(start + i));
    }

    return result;
}

/// endregion </Helpers>

/// region <§12.1-1 Decline matrix>

namespace
{
    /// Assert a decline was fully inert: registers, T-state counter and the
    /// destination memory region are all untouched.
    #define ASSERT_INVARIANT(beforeA, beforeF, beforeDE, beforeIX, beforePC, beforeSP, beforeTT, \
                             cursorBefore, destRegionBefore, dest, regionSize)                       \
        EXPECT_EQ(_z80->a, beforeA);                                                                  \
        EXPECT_EQ(_z80->f, beforeF);                                                                  \
        EXPECT_EQ(_z80->de, beforeDE);                                                                \
        EXPECT_EQ(_z80->ix, beforeIX);                                                                \
        EXPECT_EQ(_z80->pc, beforePC);                                                                \
        EXPECT_EQ(_z80->sp, beforeSP);                                                                \
        EXPECT_EQ(_z80->tt, beforeTT);                                                                \
        EXPECT_EQ(_tape->GetConsumptionCursor(), cursorBefore);                                       \
        EXPECT_EQ(ReadZ80Memory(dest, regionSize), destRegionBefore);
}

// Row 1: Fc == 0 -> VERIFY entry. Must not store the payload over memory —
// that is the memory-corruption failure mode of §13 (VERIFY mistaken for LOAD).
TEST_F(TapeFastLoad_Test, DeclineRow1VerifyEntry)
{
    std::vector<uint8_t> headerPayload;
    std::vector<uint8_t> dataPayload;
    _context->coreState.tapeFilePath = WriteTAPFile("row1-verify.tap", MakeHeaderDataPair(headerPayload, dataPayload));

    // Pre-fill the would-be destination with a pattern so any store is visible
    const uint16_t dest = 0x8000;
    for (size_t i = 0; i < headerPayload.size(); i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(dest + i), static_cast<uint8_t>(0xA5 ^ i));

    SetupLDBytesInvocation(0x00, 17, dest, 0x1234, /*carry=*/false);

    const auto beforeA = _z80->a, beforeF = _z80->f;
    const auto beforeDE = _z80->de, beforeIX = _z80->ix;
    const auto beforePC = _z80->pc, beforeSP = _z80->sp;
    const auto beforeTT = _z80->tt;
    const auto regionBefore = ReadZ80Memory(dest, headerPayload.size());

    EXPECT_FALSE(_fastLoad->HandleLDBytesTrap(*_z80));

    ASSERT_INVARIANT(beforeA, beforeF, beforeDE, beforeIX, beforePC, beforeSP, beforeTT, 0, regionBefore, dest, headerPayload.size());
}

// Row 2: DE == 0 — degenerate zero-length call
TEST_F(TapeFastLoad_Test, DeclineRow2ZeroLength)
{
    std::vector<uint8_t> headerPayload;
    std::vector<uint8_t> dataPayload;
    _context->coreState.tapeFilePath = WriteTAPFile("row2-zerolen.tap", MakeHeaderDataPair(headerPayload, dataPayload));

    SetupLDBytesInvocation(0x00, 0, 0x8000, 0x1234);

    const auto beforeA = _z80->a, beforeF = _z80->f;
    const auto beforeDE = _z80->de, beforeIX = _z80->ix;
    const auto beforePC = _z80->pc, beforeSP = _z80->sp;
    const auto beforeTT = _z80->tt;
    const auto regionBefore = ReadZ80Memory(0x8000, 17);

    EXPECT_FALSE(_fastLoad->HandleLDBytesTrap(*_z80));

    ASSERT_INVARIANT(beforeA, beforeF, beforeDE, beforeIX, beforePC, beforeSP, beforeTT, 0, regionBefore, 0x8000, 17);
}

// Row 3: no tape image loaded and none can be loaded
TEST_F(TapeFastLoad_Test, DeclineRow3NoImage)
{
    _context->coreState.tapeFilePath.clear();

    SetupLDBytesInvocation(0x00, 17, 0x8000, 0x1234);

    const auto beforeA = _z80->a, beforeF = _z80->f;
    const auto beforeDE = _z80->de, beforeIX = _z80->ix;
    const auto beforePC = _z80->pc, beforeSP = _z80->sp;
    const auto beforeTT = _z80->tt;
    const auto regionBefore = ReadZ80Memory(0x8000, 17);

    EXPECT_FALSE(_fastLoad->HandleLDBytesTrap(*_z80));

    ASSERT_INVARIANT(beforeA, beforeF, beforeDE, beforeIX, beforePC, beforeSP, beforeTT, 0, regionBefore, 0x8000, 17);
}

// Row 4: consumption cursor at end of tape
TEST_F(TapeFastLoad_Test, DeclineRow4EndOfTape)
{
    std::vector<uint8_t> headerPayload;
    std::vector<uint8_t> dataPayload;
    headerPayload = MakeHeaderPayload("solo", 8);
    _context->coreState.tapeFilePath = WriteTAPFile("row4-eot.tap", { MakeTAPBlock(0x00, headerPayload) });

    SetupLDBytesInvocation(0x00, 17, 0x8000, 0x1234);
    EXPECT_TRUE(_fastLoad->HandleLDBytesTrap(*_z80));  // Consumes the only block
    EXPECT_EQ(_tape->GetConsumptionCursor(), 1u);

    SetupLDBytesInvocation(0x00, 17, 0x8000, 0x1234);
    const auto beforeA = _z80->a, beforeF = _z80->f;
    const auto beforeDE = _z80->de, beforeIX = _z80->ix;
    const auto beforePC = _z80->pc, beforeSP = _z80->sp;
    const auto beforeTT = _z80->tt;
    const auto regionBefore = ReadZ80Memory(0x8000, 17);

    EXPECT_FALSE(_fastLoad->HandleLDBytesTrap(*_z80));  // Tape ran out

    ASSERT_INVARIANT(beforeA, beforeF, beforeDE, beforeIX, beforePC, beforeSP, beforeTT, 1, regionBefore, 0x8000, 17);
}

// Row 5: signal playback currently active — a second consumer would double-consume
TEST_F(TapeFastLoad_Test, DeclineRow5PlaybackActive)
{
    std::vector<uint8_t> headerPayload;
    std::vector<uint8_t> dataPayload;
    _context->coreState.tapeFilePath = WriteTAPFile("row5-playing.tap", MakeHeaderDataPair(headerPayload, dataPayload));

    EXPECT_TRUE(_tape->EnsureImageLoaded());
    _tape->StartPlaybackAtCursor();
    ASSERT_TRUE(_tape->IsPlaying());

    SetupLDBytesInvocation(0x00, 17, 0x8000, 0x1234);
    const auto beforeA = _z80->a, beforeF = _z80->f;
    const auto beforeDE = _z80->de, beforeIX = _z80->ix;
    const auto beforePC = _z80->pc, beforeSP = _z80->sp;
    const auto beforeTT = _z80->tt;
    const auto regionBefore = ReadZ80Memory(0x8000, 17);

    EXPECT_FALSE(_fastLoad->HandleLDBytesTrap(*_z80));

    ASSERT_INVARIANT(beforeA, beforeF, beforeDE, beforeIX, beforePC, beforeSP, beforeTT, 0, regionBefore, 0x8000, 17);
    EXPECT_TRUE(_tape->IsPlaying());  // Playback itself unaffected
}

// Row 6: flag byte does not equal the expected flag in A
TEST_F(TapeFastLoad_Test, DeclineRow6FlagMismatch)
{
    std::vector<uint8_t> headerPayload;
    std::vector<uint8_t> dataPayload;
    _context->coreState.tapeFilePath = WriteTAPFile("row6-flag.tap", MakeHeaderDataPair(headerPayload, dataPayload));

    // Cursor block is the header (flag $00) but the caller expects data ($FF)
    SetupLDBytesInvocation(0xFF, 17, 0x8000, 0x1234);

    const auto beforeA = _z80->a, beforeF = _z80->f;
    const auto beforeDE = _z80->de, beforeIX = _z80->ix;
    const auto beforePC = _z80->pc, beforeSP = _z80->sp;
    const auto beforeTT = _z80->tt;
    const auto regionBefore = ReadZ80Memory(0x8000, 17);

    EXPECT_FALSE(_fastLoad->HandleLDBytesTrap(*_z80));

    ASSERT_INVARIANT(beforeA, beforeF, beforeDE, beforeIX, beforePC, beforeSP, beforeTT, 0, regionBefore, 0x8000, 17);
}

// Row 7: XOR checksum broken — a decline reproduces the authentic loading error
TEST_F(TapeFastLoad_Test, DeclineRow7BadChecksum)
{
    std::vector<uint8_t> headerPayload = TapeFastLoad_Test::MakeHeaderPayload("badcs", 8);
    std::vector<uint8_t> block = TapeFastLoad_Test::MakeTAPBlock(0x00, headerPayload);
    block.back() ^= 0x55;  // Corrupt the checksum byte

    _context->coreState.tapeFilePath = WriteTAPFile("row7-checksum.tap", { block });

    SetupLDBytesInvocation(0x00, 17, 0x8000, 0x1234);

    const auto beforeA = _z80->a, beforeF = _z80->f;
    const auto beforeDE = _z80->de, beforeIX = _z80->ix;
    const auto beforePC = _z80->pc, beforeSP = _z80->sp;
    const auto beforeTT = _z80->tt;
    const auto regionBefore = ReadZ80Memory(0x8000, 17);

    EXPECT_FALSE(_fastLoad->HandleLDBytesTrap(*_z80));

    ASSERT_INVARIANT(beforeA, beforeF, beforeDE, beforeIX, beforePC, beforeSP, beforeTT, 0, regionBefore, 0x8000, 17);
}

// Row 8: payload length does not equal DE
TEST_F(TapeFastLoad_Test, DeclineRow8LengthMismatch)
{
    std::vector<uint8_t> headerPayload;
    std::vector<uint8_t> dataPayload;
    _context->coreState.tapeFilePath = WriteTAPFile("row8-length.tap", MakeHeaderDataPair(headerPayload, dataPayload));

    // Header block has 17 payload bytes; the caller claims 19
    SetupLDBytesInvocation(0x00, 19, 0x8000, 0x1234);

    const auto beforeA = _z80->a, beforeF = _z80->f;
    const auto beforeDE = _z80->de, beforeIX = _z80->ix;
    const auto beforePC = _z80->pc, beforeSP = _z80->sp;
    const auto beforeTT = _z80->tt;
    const auto regionBefore = ReadZ80Memory(0x8000, 19);

    EXPECT_FALSE(_fastLoad->HandleLDBytesTrap(*_z80));

    ASSERT_INVARIANT(beforeA, beforeF, beforeDE, beforeIX, beforePC, beforeSP, beforeTT, 0, regionBefore, 0x8000, 19);
}

/// endregion </§12.1-1 Decline matrix>

/// region <§12.1-2..5 Trap consume>

// §12.1-2: header consume — A=$00, DE=17, IX=$8000
TEST_F(TapeFastLoad_Test, HeaderConsume)
{
    std::vector<uint8_t> headerPayload;
    std::vector<uint8_t> dataPayload;
    _context->coreState.tapeFilePath = WriteTAPFile("consume-header.tap", MakeHeaderDataPair(headerPayload, dataPayload));

    // Pre-fill the destination so "untouched" bytes are distinguishable
    for (size_t i = 0; i <= headerPayload.size(); i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), 0xA5);

    SetupLDBytesInvocation(0x00, 17, 0x8000, 0x1234);
    const uint32_t ttBefore = _z80->tt;

    EXPECT_TRUE(_fastLoad->HandleLDBytesTrap(*_z80));

    // Payload landed at $8000, flag and checksum excluded
    EXPECT_EQ(ReadZ80Memory(0x8000, headerPayload.size()), headerPayload);
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x8011), 0xA5);  // Byte past payload untouched

    // Register postconditions (design §4.3 / §7)
    EXPECT_EQ(_z80->ix, 0x8011);
    EXPECT_EQ(_z80->de, 0u);
    EXPECT_EQ(_z80->a, 0x00);         // Checksum accumulator: 0 on valid block
    EXPECT_EQ(_z80->f, 0xBF);         // Exact CP $01 result word; carry = success
    EXPECT_EQ(_z80->pc, 0x1234);      // RET to the stacked address
    EXPECT_EQ(_z80->sp, 0xFF02);      // Stack popped

    // Cursor advanced past the header; time was charged
    EXPECT_EQ(_tape->GetConsumptionCursor(), 1u);
    EXPECT_GT(_z80->tt, ttBefore);
}

// §12.1-3: data consume — A=$FF, DE=N
TEST_F(TapeFastLoad_Test, DataConsume)
{
    std::vector<uint8_t> headerPayload;
    std::vector<uint8_t> dataPayload;
    _context->coreState.tapeFilePath = WriteTAPFile("consume-data.tap", MakeHeaderDataPair(headerPayload, dataPayload));

    // Fast-load the header first so the cursor points at the data block
    SetupLDBytesInvocation(0x00, 17, 0x8000, 0x1234);
    ASSERT_TRUE(_fastLoad->HandleLDBytesTrap(*_z80));

    SetupLDBytesInvocation(0xFF, 8, 0xC000, 0x0ABC);
    EXPECT_TRUE(_fastLoad->HandleLDBytesTrap(*_z80));

    EXPECT_EQ(ReadZ80Memory(0xC000, dataPayload.size()), dataPayload);
    EXPECT_EQ(_z80->ix, 0xC008);
    EXPECT_EQ(_z80->de, 0u);
    EXPECT_EQ(_z80->a, 0x00);
    EXPECT_EQ(_z80->f, 0xBF);
    EXPECT_EQ(_z80->pc, 0x0ABC);
    EXPECT_EQ(_tape->GetConsumptionCursor(), 2u);
}

// §12.1-4: 64K wrap — IX=$FF00, DE=$200; only the RAM part is observable
TEST_F(TapeFastLoad_Test, Wrap64K)
{
    std::vector<uint8_t> payload(0x200);
    for (size_t i = 0; i < payload.size(); i++)
        payload[i] = static_cast<uint8_t>(i & 0xFF);

    _context->coreState.tapeFilePath = WriteTAPFile("wrap64k.tap", { MakeTAPBlock(0xFF, payload) });

    SetupLDBytesInvocation(0xFF, 0x200, 0xFF00, 0x1234);
    EXPECT_TRUE(_fastLoad->HandleLDBytesTrap(*_z80));

    // $FF00-$FFFF is RAM and must hold payload[0..255]; the wrapped part
    // ($0000-) lands in ROM where writes are dropped, like on real hardware
    std::vector<uint8_t> expected(payload.begin(), payload.begin() + 0x100);
    EXPECT_EQ(ReadZ80Memory(0xFF00, 0x100), expected);

    // IX wrapped naturally: $FF00 + $200 = $0100
    EXPECT_EQ(_z80->ix, 0x0100);
    EXPECT_EQ(_z80->de, 0u);
    EXPECT_EQ(_z80->f, 0xBF);
    EXPECT_EQ(_z80->pc, 0x1234);
}

// §12.1-5: IFF1 net unchanged. The trap skips both the DI at $0559 and the
// EI inside SA/LD-RET ($053F, byte-verified: ... RRA / EI / JR C ...), so
// ROM callers observe interrupts enabled either way (design §7.1 as amended).
TEST_F(TapeFastLoad_Test, IFF1Preserved)
{
    std::vector<uint8_t> headerPayload;
    std::vector<uint8_t> dataPayload;
    _context->coreState.tapeFilePath = WriteTAPFile("iff1.tap", MakeHeaderDataPair(headerPayload, dataPayload));

    // Case 1: interrupts enabled before -> still enabled
    SetupLDBytesInvocation(0x00, 17, 0x8000, 0x1234);
    _z80->iff1 = 1;
    ASSERT_TRUE(_fastLoad->HandleLDBytesTrap(*_z80));
    EXPECT_EQ(_z80->iff1, 1u);

    // Case 2: interrupts disabled before -> still disabled (both DI and EI skipped)
    SetupLDBytesInvocation(0xFF, 8, 0x9000, 0x1234);
    _z80->iff1 = 0;
    ASSERT_TRUE(_fastLoad->HandleLDBytesTrap(*_z80));
    EXPECT_EQ(_z80->iff1, 0u);
}

/// endregion </§12.1-2..5 Trap consume>

/// region <§12.1-6..8 Arm / fallback / partial block>

// §12.1-6: arm state — signature corruption, TR-DOS session, config toggle
TEST_F(TapeFastLoad_Test, ArmState)
{
    std::vector<uint8_t> headerPayload;
    std::vector<uint8_t> dataPayload;
    _context->coreState.tapeFilePath = WriteTAPFile("arm.tap", MakeHeaderDataPair(headerPayload, dataPayload));

    // Baseline: armed
    EXPECT_TRUE(_fastLoad->IsArmed());

    // Signature corrupted -> not armed (custom ROM / non-loader code at $0556)
    CorruptLDBytesSignature();
    EXPECT_FALSE(_fastLoad->IsArmed());
    SetupLDBytesInvocation(0x00, 17, 0x8000, 0x1234);
    EXPECT_FALSE(_fastLoad->HandleLDBytesTrap(*_z80));

    InjectLDBytesSignature();
    ASSERT_TRUE(_fastLoad->IsArmed());

    // Active TR-DOS session -> not armed
    _context->emulatorState.flags |= CF_TRDOS;
    EXPECT_FALSE(_fastLoad->IsArmed());
    _context->emulatorState.flags &= ~CF_TRDOS;
    ASSERT_TRUE(_fastLoad->IsArmed());

    // Feature gate (live-read; the runtime 'fasttape' feature is the sole
    // control plane — CLI / WebAPI / Qt switch it at runtime)
    FeatureManager* featureManager = _context->pFeatureManager;
    ASSERT_NE(featureManager, nullptr);
    ASSERT_TRUE(featureManager->setFeature(Features::kFastTape, false));
    EXPECT_FALSE(_fastLoad->IsArmed());
    SetupLDBytesInvocation(0x00, 17, 0x8000, 0x1234);
    EXPECT_FALSE(_fastLoad->HandleLDBytesTrap(*_z80));  // inert decline
    // The alias resolves to the same feature
    ASSERT_TRUE(featureManager->setFeature(Features::kFastTapeAlias, true));
    EXPECT_TRUE(_fastLoad->IsArmed());
}

// §12.1-7: fallback positioning — after a trap consume and a data decline,
// signal playback must start exactly at the declined block
TEST_F(TapeFastLoad_Test, FallbackPositioning)
{
    std::vector<uint8_t> headerPayload;
    std::vector<uint8_t> dataPayload;
    _context->coreState.tapeFilePath = WriteTAPFile("fallback.tap", MakeHeaderDataPair(headerPayload, dataPayload));

    // Header fast-loads
    SetupLDBytesInvocation(0x00, 17, 0x8000, 0x1234);
    ASSERT_TRUE(_fastLoad->HandleLDBytesTrap(*_z80));
    ASSERT_EQ(_tape->GetConsumptionCursor(), 1u);

    // Data block declines: caller expects a different length (custom loader shape)
    SetupLDBytesInvocation(0xFF, 4, 0xA000, 0x1234);
    ASSERT_FALSE(_fastLoad->HandleLDBytesTrap(*_z80));

    // Fallback: playback engages at the cursor, not at tape start
    _tape->StartPlaybackAtCursor();
    ASSERT_TRUE(_tape->IsPlaying());

    // Materialize the in-flight block: bitstream generation must target the
    // data block (block index 1, flag $FF), not the already-consumed header
    _tape->handleFrameStart();
    ASSERT_NE(_tape->_currentTapeBlock, nullptr);
    EXPECT_EQ(_tape->_currentTapeBlock->blockIndex, 1u);
    EXPECT_EQ(_tape->_currentTapeBlock->data[0], 0xFF);
    EXPECT_FALSE(_tape->_currentTapeBlock->edgePulseTimings.empty());
}

// §12.1-8: a partially played block counts as consumed on stop
TEST_F(TapeFastLoad_Test, PartialBlockConsumedOnStop)
{
    std::vector<uint8_t> headerPayload;
    std::vector<uint8_t> dataPayload;
    _context->coreState.tapeFilePath = WriteTAPFile("partial.tap", MakeHeaderDataPair(headerPayload, dataPayload));

    ASSERT_TRUE(_tape->EnsureImageLoaded());
    _tape->StartPlaybackAtCursor();
    ASSERT_TRUE(_tape->IsPlaying());

    // In-flight block 0 materialized (mid-block), then the watchdog stops playback
    _tape->handleFrameStart();
    ASSERT_EQ(_tape->_currentTapeBlock, &_tape->GetBlocks()[0]);
    _tape->stopPlayback();

    EXPECT_FALSE(_tape->IsPlaying());
    EXPECT_EQ(_tape->GetConsumptionCursor(), 1u);  // Partial block 0 counts as consumed

    // The next trap invocation consumes the FOLLOWING block, never a re-run of 0
    SetupLDBytesInvocation(0xFF, 8, 0xC000, 0x1234);
    EXPECT_TRUE(_fastLoad->HandleLDBytesTrap(*_z80));
    EXPECT_EQ(_tape->GetConsumptionCursor(), 2u);
    EXPECT_EQ(ReadZ80Memory(0xC000, dataPayload.size()), dataPayload);

    // End of tape: starting playback is a no-op (nothing left to play)
    _tape->StartPlaybackAtCursor();
    EXPECT_FALSE(_tape->IsPlaying());
}

/// endregion </§12.1-6..8 Arm / fallback / partial block>
