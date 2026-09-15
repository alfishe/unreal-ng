#include "stdafx.h"
#include "pch.h"

#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/scorpion/ttdscorpionprofrom.h"
#include "emulator/ports/models/scorpionfixture.h"

#include <cstring>
#include <new>
#include <type_traits>
#include <sstream>
#include <unordered_map>
#include <vector>

/// @brief TTD serialization of the Scorpion ProfROM quadrant state.
///
/// profrom_bank is a read-strobe state machine: it depends on the whole read
/// history, so it cannot be rebuilt from the port latches on restore. That is
/// why it lives in a model-specific TTDSerializable rather than in the common
/// TTDChipsetState — the TTD framework itself knows nothing about Scorpion.
class TtdScorpionProfRom_Test : public ScorpionMachineFixture
{
protected:
    /// CPU-path read — drives the read strobe (unlike BankTag()/DirectRead)
    uint8_t FastRead(uint16_t addr)
    {
        return _memory->MemoryReadFast(addr, false);
    }

    void SetUpProf(uint16_t quadrants)
    {
        ASSERT_TRUE(RebuildWithModel(MM_PROFSCORP, RAM_256));
        ASSERT_TRUE(LoadSyntheticRom(quadrants));

        // The read strobe is only armed while the service ROM window is
        // visible at #0000 (#1FFD bit 1).
        WritePort(0x1FFD, 0x02);
        EXPECT_EQ(_context->emulatorState.profrom_bank, 0);
    }
};

/// TTD peripheral registry round-trip must carry the quadrant byte both ways
TEST_F(TtdScorpionProfRom_Test, RoundTripCarriesQuadrant)
{
    SetUpProf(4);

    FastRead(0x0104);              // S=1: Q0 -> Q3
    ASSERT_EQ(_context->emulatorState.profrom_bank, 3);

    ttd::TTDScorpionProfROM serializer(_context);
    uint8_t capturedState[sizeof(ttd::ScorpionProfROMState)] = {};
    serializer.TTDSaveState(capturedState);
    EXPECT_EQ(capturedState[0], 3);  // profrom_bank

    FastRead(0x0108);              // S=2: Q3 -> Q1
    ASSERT_EQ(_context->emulatorState.profrom_bank, 1);

    serializer.TTDLoadState(capturedState);
    EXPECT_EQ(_context->emulatorState.profrom_bank, 3);
}

/// The divergence hash must separate two states that differ only by quadrant.
/// This is the contract that keeps the common hash model-agnostic: the
/// Scorpion contribution reaches it only through the registry.
TEST_F(TtdScorpionProfRom_Test, StateHashDistinguishesQuadrants)
{
    SetUpProf(4);

    ttd::TTDPeripheralRegistry registry;
    ttd::TTDScorpionProfROM profromSerializer(_context);
    registry.Register(ttd::PeripheralId::ScorpionProfROM, &profromSerializer);

    Z80* z80 = _core->GetZ80();
    auto hashOfCurrentState = [&]() -> uint64_t
    {
        uint64_t ramDigest = ttd::HashBytes(_memory->RAMBase(), RAM_256 * 1024);
        return ttd::HashSnapshot(ttd::CaptureSnapshot(*static_cast<Z80State*>(z80),
                                                       _context->emulatorState,
                                                       ramDigest,
                                                       &registry));
    };

    uint64_t hashQ0 = hashOfCurrentState();

    _context->emulatorState.profrom_bank = 2;  // same port latches, other quadrant
    uint64_t hashQ2 = hashOfCurrentState();

    EXPECT_NE(hashQ0, hashQ2);
}

// ---------------------------------------------------------------------------
// Blob layout
// ---------------------------------------------------------------------------

/// TTD blobs are copied and hashed byte-wise, so implicit padding would leak
/// uninitialized bytes into the stream and the divergence hash.
TEST(TtdScorpionProfRomLayout_Test, BlobIsPaddingFree)
{
    using ttd::ScorpionProfROMState;

    EXPECT_EQ(sizeof(ScorpionProfROMState), 8u);
    EXPECT_TRUE(std::is_trivially_copyable_v<ScorpionProfROMState>);
    EXPECT_TRUE(std::is_standard_layout_v<ScorpionProfROMState>);

    // Every byte must belong to a named member: copy into a poisoned buffer
    // and prove assignment wrote all of it.
    alignas(ScorpionProfROMState) uint8_t raw[sizeof(ScorpionProfROMState)];
    std::memset(raw, 0xAA, sizeof(raw));
    auto* dst = new (raw) ScorpionProfROMState;

    ScorpionProfROMState src{};
    src.plane_id = 0x11;
    src.rom_page = 0x22;
    src.p7EFD = 0x33;
    src.p1FFD = 0x44;
    src.scorpionDosTrigger = 0x55;

    *dst = src;
    EXPECT_EQ(std::memcmp(dst, &src, sizeof(ScorpionProfROMState)), 0)
        << "ScorpionProfROMState has padding that assignment does not copy";
}

// ---------------------------------------------------------------------------
// Port state carried by the blob
// ---------------------------------------------------------------------------

/// p7EFD (plane window) and p1FFD (service / RAM0) are Scorpion-specific inputs
/// to the #0000 paging chain. They are deliberately absent from the
/// model-agnostic TTDChipsetState, so if this serializer did not carry them a
/// restore would silently page the wrong ROM.
TEST_F(TtdScorpionProfRom_Test, RoundTripCarriesPlaneSelectAndServiceLatches)
{
    SetUpProf(16);   // >256K image, so the #7EFD window bits are live

    WritePort(0x7EFD, 0x10);       // select a non-zero plane window
    FastRead(0x0104);              // advance the GAL state within it

    const uint8_t planeBefore = _context->emulatorState.profrom_bank;
    const uint8_t p7EFDBefore = _context->emulatorState.p7EFD;
    const uint8_t p1FFDBefore = _context->emulatorState.p1FFD;
    ASSERT_NE(planeBefore, 0);
    ASSERT_NE(p7EFDBefore, 0);

    ttd::TTDScorpionProfROM serializer(_context);
    uint8_t blob[sizeof(ttd::ScorpionProfROMState)] = {};
    serializer.TTDSaveState(blob);

    // Scribble over every field the blob owns.
    _context->emulatorState.profrom_bank = 0;
    _context->emulatorState.p7EFD = 0;
    _context->emulatorState.p1FFD = 0;
    _context->emulatorState.scorpionDosTrigger = 1;

    serializer.TTDLoadState(blob);

    EXPECT_EQ(_context->emulatorState.profrom_bank, planeBefore);
    EXPECT_EQ(_context->emulatorState.p7EFD, p7EFDBefore);
    EXPECT_EQ(_context->emulatorState.p1FFD, p1FFDBefore);
    EXPECT_EQ(_context->emulatorState.scorpionDosTrigger, 0);
}

/// rom_page is an observation of the live paging chain, not stored state. It
/// must track what is actually mapped at #0000.
TEST_F(TtdScorpionProfRom_Test, RomPageReflectsLivePaging)
{
    SetUpProf(4);

    // SetUpProf leaves the Shadow Monitor (page 2 of the plane) at #0000.
    ttd::TTDScorpionProfROM serializer(_context);
    EXPECT_EQ(serializer.CurrentRomPage(), 2);

    ttd::ScorpionProfROMState blob{};
    serializer.TTDSaveState(reinterpret_cast<uint8_t*>(&blob));
    EXPECT_EQ(blob.rom_page, 2);
    EXPECT_EQ(blob.plane_id, _context->emulatorState.profrom_bank);
}

/// #1FFD bit 0 latches RAM at #0000 — there is no ROM page there at all, and
/// the blob must say so rather than reporting a stale page.
TEST_F(TtdScorpionProfRom_Test, RomPageReportsSentinelWhenRamIsLatchedAtZero)
{
    SetUpProf(4);

    WritePort(0x1FFD, 0x01);       // RAM bank 0 at #0000

    ttd::TTDScorpionProfROM serializer(_context);
    EXPECT_EQ(serializer.CurrentRomPage(), ttd::TTDScorpionProfROM::kNoRomPage);
}

/// Restoring must not write rom_page back: it is derived from the latches, and
/// forcing it would mask a paging bug instead of surfacing it as a divergence.
TEST_F(TtdScorpionProfRom_Test, LoadStateLeavesRomPageToThePagingChain)
{
    SetUpProf(4);

    ttd::TTDScorpionProfROM serializer(_context);
    ttd::ScorpionProfROMState blob{};
    serializer.TTDSaveState(reinterpret_cast<uint8_t*>(&blob));

    // A blob claiming an impossible page must not corrupt anything: the
    // restore path ignores the field entirely.
    blob.rom_page = 0xEE;
    serializer.TTDLoadState(reinterpret_cast<const uint8_t*>(&blob));

    EXPECT_EQ(serializer.CurrentRomPage(), 2);   // still whatever paging says
}

// ---------------------------------------------------------------------------
// Divergence hash sensitivity
// ---------------------------------------------------------------------------

/// Every byte the blob carries must move the hash, or a divergence in that
/// field would go unnoticed by the oracle.
TEST_F(TtdScorpionProfRom_Test, HashRespondsToEveryCarriedField)
{
    SetUpProf(16);

    ttd::TTDScorpionProfROM serializer(_context);
    EmulatorState& state = _context->emulatorState;

    const uint64_t base = serializer.TTDHashState();

    state.profrom_bank ^= 0x01;
    EXPECT_NE(serializer.TTDHashState(), base) << "plane_id not hashed";
    state.profrom_bank ^= 0x01;

    state.p7EFD ^= 0x10;
    EXPECT_NE(serializer.TTDHashState(), base) << "p7EFD not hashed";
    state.p7EFD ^= 0x10;

    state.p1FFD ^= 0x02;
    EXPECT_NE(serializer.TTDHashState(), base) << "p1FFD not hashed";
    state.p1FFD ^= 0x02;

    state.scorpionDosTrigger ^= 0x01;
    EXPECT_NE(serializer.TTDHashState(), base) << "scorpionDosTrigger not hashed";
    state.scorpionDosTrigger ^= 0x01;

    EXPECT_EQ(serializer.TTDHashState(), base) << "hash is not a pure function of state";
}

// ---------------------------------------------------------------------------
// Registry integration
// ---------------------------------------------------------------------------

/// The framework only ever talks to the registry, so the full capture/restore
/// path must work through CaptureAll/RestoreAll rather than the device API.
TEST_F(TtdScorpionProfRom_Test, RegistryCaptureAllRestoresPlaneAndLatches)
{
    SetUpProf(16);

    ttd::TTDPeripheralRegistry registry;
    ttd::TTDScorpionProfROM serializer(_context);
    registry.Register(ttd::PeripheralId::ScorpionProfROM, &serializer);

    WritePort(0x7EFD, 0x10);
    FastRead(0x0104);
    const uint8_t planeBefore = _context->emulatorState.profrom_bank;
    const uint8_t p7EFDBefore = _context->emulatorState.p7EFD;

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);
    ASSERT_EQ(blobs.count(static_cast<uint8_t>(ttd::PeripheralId::ScorpionProfROM)), 1u);

    _context->emulatorState.profrom_bank = 0;
    _context->emulatorState.p7EFD = 0;

    registry.RestoreAll(blobs);

    EXPECT_EQ(_context->emulatorState.profrom_bank, planeBefore);
    EXPECT_EQ(_context->emulatorState.p7EFD, p7EFDBefore);
}

/// A blob for a device this build has no serializer for must be ignored, not
/// crash or corrupt neighbouring state — that is what lets a session recorded
/// on a richer build still load.
TEST_F(TtdScorpionProfRom_Test, RegistryIgnoresBlobsForUnregisteredDevices)
{
    SetUpProf(4);

    ttd::TTDPeripheralRegistry registry;   // deliberately empty

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    blobs[static_cast<uint8_t>(ttd::PeripheralId::ScorpionProfROM)] =
        std::vector<uint8_t>(64, 0xCD);

    const uint8_t planeBefore = _context->emulatorState.profrom_bank;
    EXPECT_NO_FATAL_FAILURE(registry.RestoreAll(blobs));
    EXPECT_EQ(_context->emulatorState.profrom_bank, planeBefore);
}

// ---------------------------------------------------------------------------
// TimeTravelManager wiring
// ---------------------------------------------------------------------------

/// The framework must discover model serializers on its own. Nothing outside
/// TimeTravelManager registers them, so if this regresses a Scorpion recording
/// silently loses its ProfROM state with no error anywhere.
TEST_F(TtdScorpionProfRom_Test, RecordingRegistersTheScorpionSerializer)
{
    SetUpProf(4);

    ttd::TimeTravelManager ttd(_context);
    EXPECT_FALSE(ttd.GetPeripheralRegistry().IsRegistered(ttd::PeripheralId::ScorpionProfROM))
        << "serializers must be session-scoped, not constructed eagerly";

    ASSERT_TRUE(ttd.StartRecording());
    EXPECT_TRUE(ttd.GetPeripheralRegistry().IsRegistered(ttd::PeripheralId::ScorpionProfROM));

    ttd::TTDSerializable* device =
        ttd.GetPeripheralRegistry().GetDevice(ttd::PeripheralId::ScorpionProfROM);
    ASSERT_NE(device, nullptr);
    EXPECT_EQ(device->TTDDeviceName(), "ScorpionProfROM");
    EXPECT_EQ(device->TTDStateSize(), sizeof(ttd::ScorpionProfROMState));

    ttd.StopRecording();
}

/// Restarting a session must not stack duplicate serializers.
TEST_F(TtdScorpionProfRom_Test, RestartingRecordingDoesNotDuplicateSerializers)
{
    SetUpProf(4);

    ttd::TimeTravelManager ttd(_context);
    ASSERT_TRUE(ttd.StartRecording());
    const size_t afterFirst = ttd.GetPeripheralRegistry().Count();
    ttd.StopRecording();

    ASSERT_TRUE(ttd.StartRecording());
    EXPECT_EQ(ttd.GetPeripheralRegistry().Count(), afterFirst);
    ttd.StopRecording();
}

/// The capture/restore self-test hashes live state before capture and after
/// restore. With the registry wired in, ProfROM state is part of that hash — so
/// this fails if the plane, page latches or trigger are lost on the round trip.
TEST_F(TtdScorpionProfRom_Test, CaptureRestoreSelfTestCoversProfRomState)
{
    SetUpProf(16);

    ttd::TimeTravelManager ttd(_context);
    ASSERT_TRUE(ttd.StartRecording());

    // Drive the state off its power-on values first, so a serializer that
    // silently captured nothing would still be caught.
    WritePort(0x7EFD, 0x10);
    FastRead(0x0104);
    ASSERT_NE(_context->emulatorState.profrom_bank, 0);

    const auto result = ttd.CaptureRestoreSelfTest();
    EXPECT_TRUE(result.pre_post_match)
        << "capture/restore diverged: " << result.notes;

    ttd.StopRecording();
}

/// A seek must put the plane and the paging that follows from it back exactly.
/// This is the end-to-end contract: record, walk the GAL somewhere else, seek
/// back, and the ROM page at #0000 must be the recorded one again.
TEST_F(TtdScorpionProfRom_Test, SeekRestoresPlaneAndResultingRomPage)
{
    SetUpProf(16);

    ttd::TimeTravelManager ttd(_context);
    ASSERT_TRUE(ttd.StartRecording());

    WritePort(0x7EFD, 0x10);
    FastRead(0x0104);

    ttd::TTDScorpionProfROM probe(_context);
    const uint8_t planeAtCapture = _context->emulatorState.profrom_bank;
    const uint8_t pageAtCapture  = probe.CurrentRomPage();

    ttd.OnFrameBoundary();
    ASSERT_GE(ttd.GetCheckpointCount(), 1u);
    const uint64_t frame = _context->emulatorState.frame_counter;

    ttd.StopRecording();

    // Walk the plane somewhere else entirely.
    FastRead(0x0108);
    FastRead(0x0104);
    WritePort(0x7EFD, 0x00);
    ASSERT_NE(_context->emulatorState.profrom_bank, planeAtCapture);

    ttd::TTDTimePoint target;
    target.frame = frame;
    target.tInFrame = 0;
    ASSERT_TRUE(ttd.SeekTo(target));

    EXPECT_EQ(_context->emulatorState.profrom_bank, planeAtCapture);
    EXPECT_EQ(probe.CurrentRomPage(), pageAtCapture)
        << "plane restored but the paging chain was not rebuilt from it";
}

// ---------------------------------------------------------------------------
// Session file format
// ---------------------------------------------------------------------------

/// Model blobs must survive the .ttd round trip, or a saved Scorpion session
/// reloads without its ProfROM state.
TEST_F(TtdScorpionProfRom_Test, SessionRoundTripPreservesModelBlobs)
{
    SetUpProf(16);

    ttd::TimeTravelManager ttd(_context);
    ASSERT_TRUE(ttd.StartRecording());
    WritePort(0x7EFD, 0x10);
    FastRead(0x0104);
    const uint8_t planeAtCapture = _context->emulatorState.profrom_bank;
    ttd.OnFrameBoundary();
    ASSERT_GE(ttd.GetCheckpointCount(), 1u);
    ttd.StopRecording();

    std::ostringstream out(std::ios::binary);
    std::string err;
    ASSERT_TRUE(ttd.SerializeSession(out, err)) << err;

    // Load into a second manager on the same machine.
    ttd::TimeTravelManager reloaded(_context);
    std::istringstream in(out.str(), std::ios::binary);
    ASSERT_TRUE(reloaded.DeserializeSession(in, err)) << err;
    ASSERT_EQ(reloaded.GetCheckpointCount(), ttd.GetCheckpointCount());

    // Move the live plane away, then seek the reloaded session back.
    FastRead(0x0108);
    WritePort(0x7EFD, 0x00);
    ASSERT_NE(_context->emulatorState.profrom_bank, planeAtCapture);

    const ttd::TTDCheckpoint* first = reloaded.GetCheckpoint(0);
    ASSERT_NE(first, nullptr);

    ttd::TTDTimePoint target;
    target.frame = first->time.frame;
    target.tInFrame = 0;
    ASSERT_TRUE(reloaded.SeekTo(target));

    EXPECT_EQ(_context->emulatorState.profrom_bank, planeAtCapture)
        << "model blobs did not survive serialization";
}

/// A session recorded against a different ROM set must be refused: checkpoints
/// store ROM page numbers, not ROM bytes, so a plane id only means something
/// relative to the image it was recorded against.
TEST_F(TtdScorpionProfRom_Test, SessionFromDifferentRomSetIsRejected)
{
    SetUpProf(16);

    ttd::TimeTravelManager ttd(_context);
    ASSERT_TRUE(ttd.StartRecording());
    ttd.OnFrameBoundary();
    ttd.StopRecording();

    std::ostringstream out(std::ios::binary);
    std::string err;
    ASSERT_TRUE(ttd.SerializeSession(out, err)) << err;

    // Reload the machine with a different ProfROM image (different quadrant
    // count changes the bundle contents, hence the signature).
    ASSERT_TRUE(LoadSyntheticRom(4));

    ttd::TimeTravelManager reloaded(_context);
    std::istringstream in(out.str(), std::ios::binary);
    EXPECT_FALSE(reloaded.DeserializeSession(in, err))
        << "a session recorded against another ROM set must not load";
    EXPECT_NE(err.find("ROM set mismatch"), std::string::npos) << "actual error: " << err;
}

/// The same ROM set must still load — the check must not be so strict that it
/// rejects legitimate sessions.
TEST_F(TtdScorpionProfRom_Test, SessionFromSameRomSetLoads)
{
    SetUpProf(16);

    ttd::TimeTravelManager ttd(_context);
    ASSERT_TRUE(ttd.StartRecording());
    ttd.OnFrameBoundary();
    ttd.StopRecording();

    std::ostringstream out(std::ios::binary);
    std::string err;
    ASSERT_TRUE(ttd.SerializeSession(out, err)) << err;

    ttd::TimeTravelManager reloaded(_context);
    std::istringstream in(out.str(), std::ios::binary);
    EXPECT_TRUE(reloaded.DeserializeSession(in, err)) << err;
}
