// IDE under TTD (implementation-plan.md D4, IDE design §12.4): the board's
// state is a blob (AtaChannel, id 17) registered on machines with an IDE
// board; a guest write is a replay barrier once per frame and never ends the
// recording; a blob taken mid-transfer continues the transfer exactly

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/scratchfolder.h"
#include "base/featuremanager.h"
#include "debugger/ttd/ide/ttdatachannel.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/emulator.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/media/mediahistory.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"

using namespace ata;

namespace
{
    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    /// A Pentagon (Nemo board) with a hard disk whose sector n is filled with n
    class TTDAtaChannel_Test : public ::testing::Test
    {
    protected:
        Emulator* _emulator = nullptr;
        EmulatorContext* _context = nullptr;
        std::unique_ptr<ScratchFolder> _folder;

        void SetUp() override
        {
            _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
            ASSERT_NE(_emulator, nullptr);
            _context = _emulator->GetContext();
            _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
            _folder = std::make_unique<ScratchFolder>("ttd-ide");
            std::string disk;
            for (int n = 0; n < 64; n++)
            {
                std::string sector(512, static_cast<char>(n));
                sector[1] = static_cast<char>(0x80 | n);
                disk += sector;
            }
            MediaSource source;
            source.path = Utf8(_folder->File("hdd.img", disk));
            InsertOptions options;
            options.immediate = true;
            ASSERT_TRUE(_context->pMediaManager->Insert("ide0.master", source, options).Ok());
            _context->emulatorState.flags &= static_cast<uint8_t>(~(CF_DOSPORTS | CF_TRDOS));  // Nemo answers
        }
        void TearDown() override
        {
            _folder.reset();
            if (_emulator)
                EmulatorTestHelper::CleanupEmulator(_emulator);
            MessageCenter::DisposeDefaultMessageCenter();
        }

        // Z80 port I/O through the Pentagon's decoder (Nemo: A7..A5 register, #11 latch)
        void Out(uint16_t port, uint8_t value) { _context->pPortDecoder->DecodePortOut(port, value, 0); }
        uint8_t In(uint16_t port) { return _context->pPortDecoder->DecodePortIn(port, 0); }
        void Command(uint8_t command, uint8_t lba, uint8_t count)
        {
            Out(0xD0, 0xE0);  // master, LBA
            Out(0x50, count);
            Out(0x70, lba);
            Out(0x90, 0);
            Out(0xB0, 0);
            Out(0xF0, command);
        }
        uint16_t ReadWord()
        {
            const uint8_t low = In(0x10);
            return static_cast<uint16_t>(low | (In(0x11) << 8));
        }
        void WriteSector(uint8_t lba, uint8_t fill)
        {
            Command(Command::WriteSectors, lba, 1);
            for (int i = 0; i < 256; i++)
            {
                Out(0x11, fill);
                Out(0x10, fill);
            }
        }
    };
}  // namespace

TEST_F(TTDAtaChannel_Test, RegisteredOnMachinesWithABoard)
{
    ttd::TimeTravelController* ttd = _context->pTimeTravelController;
    ASSERT_TRUE(ttd->StartRecording());
    EXPECT_TRUE(ttd->GetPeripheralRegistry().IsRegistered(ttd::PeripheralId::AtaChannel));
    ttd->StopRecording();

    Emulator* spectrum = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(spectrum, nullptr);
    spectrum->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    ttd::TimeTravelController* other = spectrum->GetContext()->pTimeTravelController;
    ASSERT_TRUE(other->StartRecording());
    EXPECT_FALSE(other->GetPeripheralRegistry().IsRegistered(ttd::PeripheralId::AtaChannel)) << "no IDE board";
    other->StopRecording();
    EmulatorTestHelper::CleanupEmulator(spectrum);
}

/// The blob taken in the middle of a sector: restoring it continues with the
/// same words, latches included
TEST_F(TTDAtaChannel_Test, BlobContinuesATransfer)
{
    ttd::TTDAtaChannel blob(_context);
    Command(Command::ReadSectors, 3, 2);
    for (int i = 0; i < 100; i++)
        ReadWord();
    EXPECT_EQ(In(0x10), 3);  // word 100: its low byte read, the high byte waits in the latch

    std::vector<uint8_t> saved(blob.TTDStateSize());
    blob.TTDSaveState(saved.data());
    const uint64_t hash = blob.TTDHashState();

    std::vector<uint16_t> first;
    const uint8_t latched = In(0x11);
    for (int i = 101; i < 512; i++)
        first.push_back(ReadWord());

    blob.TTDLoadState(saved.data());
    EXPECT_EQ(blob.TTDHashState(), hash);
    EXPECT_EQ(In(0x11), latched) << "the adapter's latch came back";
    for (int i = 101; i < 512; i++)
        ASSERT_EQ(ReadWord(), first[i - 101]) << i;
    EXPECT_EQ(In(0xF0), Status::DRDY | Status::DSC);
}

/// A guest write marks the recording (a seek never crosses it silently) once
/// per frame and keeps it running
TEST_F(TTDAtaChannel_Test, WritesAreBarriersOncePerFrame)
{
    ttd::TimeTravelController* ttd = _context->pTimeTravelController;
    MediaManager& manager = *_context->pMediaManager;
    ASSERT_TRUE(ttd->StartRecording());
    const size_t before = ttd->GetExternalEvents().Size();

    Command(Command::ReadSectors, 1, 1);  // reads are no barrier
    for (int i = 0; i < 256; i++)
        ReadWord();
    EXPECT_EQ(ttd->GetExternalEvents().Size(), before);

    WriteSector(10, 0x11);
    WriteSector(11, 0x22);
    EXPECT_EQ(ttd->GetExternalEvents().Size(), before + 1) << "one barrier per frame, not per sector";

    manager.ApplyPending();  // the frame ends
    WriteSector(12, 0x33);
    EXPECT_EQ(ttd->GetExternalEvents().Size(), before + 2);
    EXPECT_TRUE(ttd->IsRecording()) << "a guest write never ends the recording";

    // The media set is fixed while recording
    EXPECT_EQ(manager.Eject("ide0.master", {.disposition = Disposition::Discard}).error, MediaError::Recording);
    ttd->StopRecording();
}

/// A replay runs the guest's writes again: they are the recording's, not new ones. The medium's version (frames that
/// wrote it, what the TTD media check compares) does not move, so a seek after the last write lands exactly however
/// many earlier seeks replayed writes
TEST_F(TTDAtaChannel_Test, ReplayedWritesDoNotMoveTheMediumsVersion)
{
    // Three sectors (LBA 3, 2, 1) written about three frames apart through the Nemo ports (B = 0), then a loop
    const std::vector<uint8_t> program = {
        0xF3,              // 8000 di
        0x31, 0x00, 0xC0,  // 8001 ld sp,#C000
        0x1E, 0x03,        // 8004 ld e,3
        0x06, 0x00,        // 8006 ld b,0
        0x0E, 0xD0,        // 8008 next: ld c,#D0
        0x3E, 0xE0,        //      ld a,#E0       master, LBA
        0xED, 0x79,        //      out (c),a
        0x0E, 0x50,        //      ld c,#50
        0x3E, 0x01,        //      ld a,1         one sector
        0xED, 0x79,        //      out (c),a
        0x0E, 0x70,        //      ld c,#70
        0x7B,              //      ld a,e         LBA
        0xED, 0x79,        //      out (c),a
        0x0E, 0x90,        //      ld c,#90
        0xAF,              //      xor a
        0xED, 0x79,        //      out (c),a
        0x0E, 0xB0,        //      ld c,#B0
        0xED, 0x79,        //      out (c),a
        0x0E, 0xF0,        //      ld c,#F0
        0x3E, 0x30,        //      ld a,#30       WRITE SECTORS
        0xED, 0x79,        //      out (c),a
        0x16, 0x00,        //      ld d,0         256 words
        0x0E, 0x11,        // 802A word: ld c,#11
        0x7B,              //      ld a,e
        0xED, 0x79,        //      out (c),a      the high byte into the latch
        0x0E, 0x10,        //      ld c,#10
        0xED, 0x79,        //      out (c),a      the low byte: the word
        0x15,              //      dec d
        0x20, 0xF4,        //      jr nz,word
        0x21, 0x00, 0x20,  //      ld hl,#2000    about three frames
        0x2B,              // 8039 delay: dec hl
        0x7C,              //      ld a,h
        0xB5,              //      or l
        0x20, 0xFB,        //      jr nz,delay
        0x1D,              //      dec e
        0x20, 0xC7,        //      jr nz,next
        0x18, 0xFE,        // 8041 jr $
    };
    for (size_t i = 0; i < program.size(); i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    _context->pCore->GetZ80()->pc = 0x8000;
    auto version = [this]() {
        std::vector<MediaVersionInfo> versions;
        _context->pMediaManager->CurrentVersions(versions);
        for (const MediaVersionInfo& v : versions)
            if (v.slot == "ide0.master")
                return v.version;
        return ~uint64_t(0);
    };

    ttd::TimeTravelController* ttd = _context->pTimeTravelController;
    const uint64_t before = version();
    ASSERT_TRUE(ttd->StartRecording());
    _emulator->RunNFrames(14, /*skipBreakpoints=*/true);
    ttd->StopRecording();
    const uint64_t recorded = version();
    ASSERT_EQ(recorded - before, 3u) << "three frames wrote the disk";

    // Into the end of every frame of the writes: each replay runs the frame's write again
    const uint64_t first = ttd->GetCheckpoint(0)->time.frame;
    const uint32_t late = ttd->FrameSpan() - 1000;
    for (uint64_t f = first; f < first + 9; f++)
        ASSERT_TRUE(ttd->SeekTo({f, late})) << "frame " << f;
    EXPECT_EQ(version(), recorded) << "replayed writes are the recording's own";

    ASSERT_TRUE(ttd->SeekTo({first + 12, late}));
    EXPECT_EQ(ttd->LastEngineCheck().status, ttd::TTDRestoreStatus::Exact)
        << "after the last write the medium is the recorded one: " << ttd->LastEngineCheck().message;
}

/// One-channel boards keep the v1 blob size; the Sprinter's two channels append the second one, and a blob taken
/// mid-transfer on the secondary channel (selected by the adapter) continues there
TEST(TTDAtaChannelSprinter_Test, SecondChannelIsInTheBlob)
{
    Emulator* pentagon = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(pentagon, nullptr);
    const size_t oneChannel = ttd::TTDAtaChannel(pentagon->GetContext()).TTDStateSize();
    EXPECT_EQ(oneChannel, 4 + sizeof(IdeAdapterState) + 2 * sizeof(AtaDeviceState)) << "unchanged for one channel";
    EmulatorTestHelper::CleanupEmulator(pentagon);

    Emulator* sprinter = EmulatorTestHelper::CreateStandardEmulator("SPRINTER", LoggerLevel::LogError);
    ASSERT_NE(sprinter, nullptr);
    EmulatorContext* context = sprinter->GetContext();
    ttd::TTDAtaChannel blob(context);
    EXPECT_EQ(blob.TTDStateSize(), oneChannel + 4 + 2 * sizeof(AtaDeviceState));

    ScratchFolder folder("ttd-ide-sprinter");
    std::string disk;
    for (int n = 0; n < 16; n++)
    {
        std::string sector(512, static_cast<char>(n));
        sector[1] = static_cast<char>(0x80 | n);
        disk += sector;
    }
    MediaSource source;
    source.path = Utf8(folder.File("ide1.img", disk));
    InsertOptions options;
    options.immediate = true;
    ASSERT_TRUE(context->pMediaManager->Insert("ide1.master", source, options).Ok());

    IdeAdapter& adapter = context->pPortDecoder->GetIdeAdapter();
    adapter.SprinterOut(0x2A, 0x01BC, 0x01);  // secondary
    for (const auto& [code, value] : std::vector<std::pair<uint8_t, uint8_t>>{
             {0x26, 0xE0}, {0x22, 1}, {0x23, 5}, {0x24, 0}, {0x25, 0}, {0x27, Command::ReadSectors}})
        adapter.SprinterOut(code, static_cast<uint16_t>(0x0150 | (code & 7)), value);
    for (int i = 0; i < 10; i++)
        adapter.SprinterIn(0x20, 0x0050);

    std::vector<uint8_t> saved(blob.TTDStateSize());
    blob.TTDSaveState(saved.data());
    const uint8_t next = adapter.SprinterIn(0x20, 0x0050);

    adapter.SprinterOut(0x2B, 0x21BC, 0x21);  // primary: another channel, another latch
    adapter.SprinterIn(0x20, 0x0050);
    context->pIdeController->Channel(1).HardReset();

    blob.TTDLoadState(saved.data());
    EXPECT_EQ(adapter.State().channel, 1) << "the channel select came back";
    EXPECT_EQ(adapter.SprinterIn(0x20, 0x0050), next) << "the secondary unit continues its sector";
    EmulatorTestHelper::CleanupEmulator(sprinter);
}
