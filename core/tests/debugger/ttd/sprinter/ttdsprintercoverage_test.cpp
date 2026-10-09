// The whole Sprinter under TTD v2 (owner request 2026-10-08: full coverage). Each workload drives subsystems with real
// software from testdata, records them, and replays the recording from its first checkpoint and from the middle one:
// at every frame boundary the CPU, the chipset, every recorded device, every memory region and the picture equal the
// recording (TTDSprinterMachine_Test::ExpectExactReplay). The system is DSS 1.71.66 on BIOS 3.06 Hotfix 2, from a
// hard disk built here from testdata/machines/sprinter/dss/1.71.66 with the program's files in its root, so nothing
// outside the repository is needed.
//
// Runtime: boot-bound (BIOS POST, DSS from the disk, the program's start-up, all in the turbo mode and not recorded),
// then a few hundred recorded frames compared twice - seconds per workload.

#include <fstream>
#include <map>
#include <set>
#include <iterator>

#include "debugger/analyzers/rom-print/screenocr.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/io/ide/ata/atapicdrom.h"
#include "_helpers/cdtestdisc.h"
#include "_helpers/scratchfolder.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/ttd/ttdcontrol.h"
#include "ttdsprintermachine.h"

namespace
{
std::vector<uint8_t> ReadAll(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
}  // namespace

class TTDSprinterCoverage_Test : public TTDSprinterShipped_Test
{
protected:
    const char* BiosFile() const override { return "sp2k-3.06-hf2.rom"; }

    void TearDown() override
    {
        TTDSprinterShipped_Test::TearDown();
        if (!_image.empty())
            std::remove(_image.c_str());
    }

    /// DSS 1.71.66 on a hard disk of `megabytes` holding `files` (8.3 directory names) and a SYSTEM.BAT of `bat`;
    /// the machine boots it (turbo, not recorded) until the shell has started the batch
    void BootDss(const std::vector<DssHddFile>& files, const std::string& bat, uint32_t megabytes = 16)
    {
        const std::string dss = TestPathHelper::GetTestDataPath("machines/sprinter/dss/1.71.66/");
        const std::vector<uint8_t> loader = ReadAll(dss + "DSSloader.bin");
        const std::vector<uint8_t> kernel = ReadAll(dss + "system.dos");
        const std::vector<uint8_t> shell = ReadAll(dss + "system.exe");
        if (loader.empty() || kernel.empty() || shell.empty())
            GTEST_SKIP() << "testdata/machines/sprinter/dss/1.71.66 is missing";
        constexpr size_t kSectors13 = 1443;  // the loader's sectors 1-3 part (sprinter_boot_test, the CF test)
        ASSERT_EQ(loader.size(), kSectors13 + 276);
        const std::string batch = "ver\r\n" + bat;   // the version line tells the shell started the batch
        std::vector<DssHddFile> all = {{"SYSTEM  DOS", kernel},
                                       {"SYSTEM  EXE", shell},
                                       {"SYSTEM  BAT", std::vector<uint8_t>(batch.begin(), batch.end())}};
        all.insert(all.end(), files.begin(), files.end());
        const std::vector<uint8_t> disk = BuildDssHdd(std::vector<uint8_t>(loader.begin(), loader.begin() + kSectors13), all,
                                                      std::vector<uint8_t>(loader.begin() + kSectors13, loader.end()),
                                                      megabytes * 2048);
        _image = TestPathHelper::GetUniqueTestScratchPath("ttd-sprinter-coverage.img");
        ASSERT_TRUE(FileHelper::SaveBufferToFile(_image, const_cast<uint8_t*>(disk.data()), disk.size()));

        PowerOn(true);
        MediaSource source;
        source.path = _image;
        InsertOptions options;
        options.immediate = true;
        options.access = AccessMode::Session;
        ASSERT_TRUE(_context->pMediaManager->Insert("ide0.master", source, options).Ok());
        _emulator->EnableTurboMode();
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Shell version"); }, 3000, 1);
        _emulator->DisableTurboMode();
        ASSERT_TRUE(ScreenHas("Estex DSS version 1.71.66")) << ScreenText();
        RunToBoundary();
    }

    /// A file of testdata/machines/sprinter/<path> under the 8.3 directory name `name`
    static DssHddFile File(const char* name, const std::string& path)
    {
        return {name, ReadAll(TestPathHelper::GetTestDataPath("machines/sprinter/" + path))};
    }

    /// Record `frames` frames, then replay them from the first checkpoint and from the middle one
    void RecordAndReplay(int frames, const std::string& what)
    {
        StartRecording();
        Record(frames);
        _ttd->StopRecording();
        ASSERT_GE(_ttd->GetCheckpointCount(), static_cast<size_t>(frames));
        const size_t last = _ttd->GetCheckpointCount() - 1;
        ExpectExactReplay(0, last, what + ", from the start");
        ExpectExactReplay(last / 2, last - last / 2, what + ", from the middle");
    }

    std::string _image;
};

/// deMarche's dontBlink (testdata/machines/sprinter/demo/DNTBLINK; the owner's local test data, skipped where it is
/// absent): a progress bar while it loads from the hard disk, then the demo - the accelerator's graphics, the music
/// streamed from the disk into the Covox-Blaster in the CTC handler (IM 2). Recorded once it plays
TEST_F(TTDSprinterCoverage_Test, DontBlink_TheAcceleratorAndTheCovoxBlasterFedFromTheDisk)
{
    const std::string d = "demo/DNTBLINK/";
    if (!FileHelper::FileExists(TestPathHelper::GetTestDataPath("machines/sprinter/" + d + "dntblink.mus")))
        GTEST_SKIP() << "testdata/machines/sprinter/demo/DNTBLINK is not here";
    ASSERT_NO_FATAL_FAILURE(BootDss({File("DNTBLINKEXE", d + "dntblink.exe"), File("DNTBLINKBIN", d + "dntblink.bin"),
                                     File("DNTBLINKFRM", d + "dntblink.frm"), File("DNTBLINKMUS", d + "dntblink.mus")},
                                    "dntblink\r\n", 64));
    Skip(1200);   // the progress bar, then the demo
    const CovoxBlasterState before = _decoder->GetCovoxBlaster().State();
    StartRecording();
    Record(300);
    _ttd->StopRecording();
    std::set<uint64_t> pictures;
    for (const auto& [frame, hash] : _screens)
        pictures.insert(hash);
    EXPECT_GT(pictures.size(), 50u) << "the demo moves\n" << ScreenText();
    const CovoxBlasterState& after = _decoder->GetCovoxBlaster().State();
    EXPECT_GT(after.ringWrites - before.ringWrites, 0u) << "the music streams into the ring";
    EXPECT_GT(after.ticks - before.ticks, 0u) << "and plays";
    const size_t last = _ttd->GetCheckpointCount() - 1;
    ExpectExactReplay(0, last, "dontBlink, from the start");
    ExpectExactReplay(last / 2, last - last / 2, "dontBlink, from the middle");
}

/// ACCTEST.EXE (testdata/machines/sprinter/software): copies its 64 x 64 picture into the video RAM with the accelerator's
/// block reads and writes. Recorded from its start: the program loading from the hard disk, the accelerator at work,
/// the picture appearing
TEST_F(TTDSprinterCoverage_Test, AccTest_TheAcceleratorCopiesThePicture)
{
    ASSERT_NO_FATAL_FAILURE(BootDss({File("ACCTEST EXE", "software/acctest.exe")}, "acctest\r\n"));
    const uint32_t before = _decoder->GetAccelerator()->State().operations;
    StartRecording();
    Record(120);
    _ttd->StopRecording();
    EXPECT_GE(_decoder->GetAccelerator()->State().operations - before, 128u) << "64 block reads and 64 block writes";
    const size_t last = _ttd->GetCheckpointCount() - 1;
    ExpectExactReplay(0, last, "ACCTEST, from the start");
    ExpectExactReplay(last / 2, last - last / 2, "ACCTEST, from the middle");
}

/// The Spectrum mode (BIOS 3.04 with DSS 1.62's SPECTRUM.EXE: the Pentagon 128 launcher from the floppy), recorded from
/// its menu with a TRD in drive A: TR-DOS starts, LIST reads the catalog and LOAD "smReadMe" CODE the file - the PLD's
/// ZX mode, the ZX keyboard, the WD1793 at 720 KB
class TTDSprinterZxMode_Test : public TTDSprinterShipped_Test
{
protected:
    /// Keys the way the host keyboard sends them (the window's KeyboardEvent, as MessageCenter delivers it:
    /// journaled input, replayed)
    void HostKey(ZXKeysEnum key, bool pressed)
    {
        KeyboardEvent event(key, pressed ? KEY_PRESSED : KEY_RELEASED, "");
        Message message(0, &event, /*cleanupPayload=*/false);
        if (pressed)
            _context->pKeyboard->OnKeyPressed(0, &message);
        else
            _context->pKeyboard->OnKeyReleased(0, &message);
    }
    void Press(ZXKeysEnum key) { HostKey(key, true); }
    void Release(ZXKeysEnum key) { HostKey(key, false); }
    void Tap(ZXKeysEnum key)
    {
        Press(key);
        Record(4);
        Release(key);
        Record(4);
    }
    void Chord(ZXKeysEnum shift, ZXKeysEnum key)
    {
        Press(shift);
        Record(2);
        Tap(key);
        Release(shift);
        Record(2);
    }
    bool SpectrumHas(const std::string& text) { return ScreenOCR::containsText(_emulator->GetId(), text); }
    /// Record until `text` is on the Spectrum screen (at most `frames`)
    void RecordUntil(const std::string& text, int frames)
    {
        for (int i = 0; i < frames && !SpectrumHas(text) && !HasFatalFailure(); i++)
            Record(1);
    }
};

TEST_F(TTDSprinterZxMode_Test, TrDosListsAndLoadsFromATrd)
{
    const std::string image = TestPathHelper::GetTestDataPath("machines/sprinter/dss_1_62_92.img");
    const std::string trd = TestPathHelper::GetTestDataPath("loaders/trd/zx-format8.trd");
    std::vector<uint8_t> disk = ReadAll(image);
    if (disk.size() != 1474560u || !FileHelper::FileExists(trd))
        GTEST_SKIP() << "the DSS floppy or the TRD fixture is missing";
    // SYSTEM.BAT (the third root entry, cluster 49 = LBA 80) starts the Pentagon 128 launcher
    const std::string bat = "a:\\zx\\spectrum.exe a:\\zx\\pent128.zx\r\n";
    uint8_t* entry = disk.data() + 19 * 512 + 2 * 32;
    ASSERT_EQ(std::string(reinterpret_cast<const char*>(entry), 11), "SYSTEM  BAT");
    entry[28] = static_cast<uint8_t>(bat.size());
    entry[29] = entry[30] = entry[31] = 0;
    std::memcpy(disk.data() + 80 * 512, bat.data(), bat.size());
    std::vector<std::string> copies;
    std::string error;
    for (uint8_t drive : {1, 0})
    {
        copies.push_back(TestPathHelper::GetUniqueTestScratchPath(drive ? "ttd-spectrum-b.img" : "ttd-spectrum-a.img"));
        ASSERT_TRUE(FileHelper::SaveBufferToFile(copies.back(), disk.data(), disk.size()));
        ASSERT_TRUE(_emulator->LoadDisk(copies.back(), drive, &error)) << error;
    }

    PowerOn(true);
    ASSERT_NO_FATAL_FAILURE(SkipIdeProbe());
    _emulator->EnableTurboMode();
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return SpectrumHas("128 BASIC"); }, 3000, 10);
    _emulator->DisableTurboMode();
    ASSERT_TRUE(SpectrumHas("TR-DOS")) << ScreenOCR::ocrScreen(_emulator->GetId());
    RunToBoundary();

    ASSERT_TRUE(_emulator->LoadDisk(trd, 0, &error)) << error;   // an explicit recording refuses a media change (D43)
    StartRecording();
    Tap(ZXKEY_ENTER);  // TR-DOS
    RecordUntil("TR-DOS", 300);
    Record(50);
    Tap(ZXKEY_K);  // LIST
    Tap(ZXKEY_ENTER);
    RecordUntil("scroll?", 400);
    ASSERT_TRUE(SpectrumHas("Title: AMD4ever")) << ScreenOCR::ocrScreen(_emulator->GetId());
    Tap(ZXKEY_Y);
    RecordUntil("mem_t.ba", 200);
    Record(50);
    // LOAD "smReadMe" CODE
    Tap(ZXKEY_J);
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);
    for (ZXKeysEnum key : {ZXKEY_S, ZXKEY_M})
        Tap(key);
    Chord(ZXKEY_CAPS_SHIFT, ZXKEY_R);
    for (ZXKeysEnum key : {ZXKEY_E, ZXKEY_A, ZXKEY_D})
        Tap(key);
    Chord(ZXKEY_CAPS_SHIFT, ZXKEY_M);
    Tap(ZXKEY_E);
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);
    Press(ZXKEY_CAPS_SHIFT);  // E mode, then I = CODE
    Press(ZXKEY_SYM_SHIFT);
    Record(4);
    Release(ZXKEY_SYM_SHIFT);
    Release(ZXKEY_CAPS_SHIFT);
    Record(4);
    Tap(ZXKEY_I);
    Tap(ZXKEY_ENTER);
    RecordUntil("O.K.", 400);
    ASSERT_TRUE(SpectrumHas("O.K.")) << ScreenOCR::ocrScreen(_emulator->GetId());
    Record(10);
    _ttd->StopRecording();

    const size_t last = _ttd->GetCheckpointCount() - 1;
    ExpectExactReplay(0, last, "TR-DOS in the Spectrum mode, from the start");
    ExpectExactReplay(last / 2, last - last / 2, "TR-DOS in the Spectrum mode, from the middle");
    for (const std::string& copy : copies)
        std::remove(copy.c_str());
}

/// The NeoGS (the shipped card) and the classic General Sound behind the ZX-bus adapter in ISA slot 1: a host program reaches it through memory
/// window 3 (#1FFD bit 4, page #D4, the #9FBD latch 0) and sends commands to its mailbox in a loop - #C0BB the command,
/// then the command flag polled until the card's firmware takes it (or a timeout), the data byte read from #C0B3 -
/// counting the commands taken at #9000. Both CPUs, the mailbox, the card's RAM and flash regions, under one recording
class TTDSprinterGs_Test : public TTDSprinterShipped_Test, public ::testing::WithParamInterface<GSTypeKind>
{
protected:
    void ConfigureMachine(CONFIG& config) override { config.sound.gsTypeKind = GetParam(); }   // behind the adapter
};

TEST_P(TTDSprinterGs_Test, TheMailboxBetweenBothCpus)
{
    GeneralSoundCard* gs = _context->pSoundManager->getGeneralSound();
    ASSERT_NE(gs, nullptr) << "the card behind the adapter in ISA slot 1";
    PowerOn(true);
    Skip(150);   // the BIOS up; the card's firmware running
    RunToBoundary();

    const uint8_t code[] = {
        0xF3,                    // DI
        0x01, 0xFD, 0x1F,        // LD BC,#1FFD
        0x3E, 0x10, 0xED, 0x79,  // LD A,#10 : OUT (C),A    window 3 shows ISA
        0x3E, 0xD4, 0xD3, 0xE2,  // LD A,#D4 : OUT (#E2),A  slot 1, I/O space
        0x01, 0xBD, 0x9F,        // LD BC,#9FBD
        0xAF, 0xED, 0x79,        // XOR A : OUT (C),A        A19-A14 = 0, AEN = 0
        0x21, 0x00, 0x90,        // LD HL,#9000             the count
        0x36, 0x00,              // LD (HL),0
        0x1E, 0x20,              // LD E,#20                 the command
        // loop:
        0x7B, 0x32, 0xBB, 0xC0,  // LD A,E : LD (#C0BB),A    the command to the mailbox
        0x06, 0x00,              // LD B,0                    timeout
        // wait:
        0x3A, 0xBB, 0xC0,        // LD A,(#C0BB)             status
        0x0F,                    // RRCA                      bit 0: command not taken yet
        0x30, 0x04,              // JR NC,taken
        0x10, 0xF8,              // DJNZ wait
        0x18, 0x01,              // JR next (timed out)
        // taken:
        0x34,                    // INC (HL)
        // next:
        0x3A, 0xB3, 0xC0,        // LD A,(#C0B3)              the data byte
        0x32, 0x01, 0x90,        // LD (#9001),A
        0x1C,                    // INC E
        0x7B, 0xE6, 0x27,        // LD A,E : AND #27          commands #20-#27 round
        0x5F,                    // LD E,A
        0xB7, 0x20, 0x02,        // OR A : JR NZ,+2
        0x1E, 0x20,              // LD E,#20
        0x18, 0xDD};             // JR loop
    for (size_t i = 0; i < sizeof code; i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
    _z80->pc = 0x8000;
    _z80->iff1 = _z80->iff2 = 0;
    _z80->halted = 0;   // the BIOS may sit in HALT

    StartRecording();
    Record(60);
    _ttd->StopRecording();
    EXPECT_GT(_context->pMemory->DirectReadFromZ80Memory(0x9000), 0) << "the card's firmware took commands";
    const size_t last = _ttd->GetCheckpointCount() - 1;
    ExpectExactReplay(0, last, "the GS mailbox, from the start");
    ExpectExactReplay(last / 2, last - last / 2, "the GS mailbox, from the middle");
}

INSTANTIATE_TEST_SUITE_P(Cards, TTDSprinterGs_Test, ::testing::Values(GSTypeKind::NGS, GSTypeKind::Z80),
                         [](const auto& info) { return info.param == GSTypeKind::NGS ? "NeoGS" : "GeneralSound"; });

/// The ATAPI CD drive on the primary slave (a music disc built here: three audio tracks of tones, a data track) plays
/// audio: a host program sends PLAY AUDIO MSF 00:02:00 - 80:00:74 the way the Sprinter's CDPLAYER.FLX does
/// (select the slave, the PACKET command, the 12-byte packet through the data port as six words) and waits; the drive's
/// audio head, its sectors read from the image (the media read journal) and the mix run under the recording
TEST_F(TTDSprinterShipped_Test, CdDrive_PlaysAudioThroughAnAtapiPacket)
{
    ScratchFolder folder("ttd-sprinter-cd");
    const std::string cue = cdtest::WriteMusicDisc(folder.Path(), 3, 4);
    std::string error;
    ASSERT_TRUE(_context->pIdeController->SetUnitKind(1, IdeController::UnitKind::Cdrom, &error)) << error;
    MediaSource source;
    source.path = cue;
    InsertOptions options;
    options.immediate = true;
    ASSERT_TRUE(_context->pMediaManager->Insert("ide0.slave", source, options).Ok());
    auto* cd = dynamic_cast<AtapiCdrom*>(_context->pIdeController->Channel(0).Unit(1));
    ASSERT_NE(cd, nullptr);

    PowerOn(true);
    ASSERT_NO_FATAL_FAILURE(SkipIdeProbe());
    Skip(20);

    // OUT (C),A with BC = port: the Sprinter's IDE registers are whole 16-bit addresses (hardware-reference §9.1)
    std::vector<uint8_t> code = {0xF3};   // DI
    auto out = [&code](uint16_t port, uint8_t value) {
        code.insert(code.end(), {0x01, static_cast<uint8_t>(port), static_cast<uint8_t>(port >> 8), 0x3E, value, 0xED, 0x79});
    };
    auto waitStatus = [&code](uint8_t mask, uint8_t want) {
        // wait: LD BC,#4053 : IN A,(C) : AND mask : CP want : JR NZ,wait
        const size_t at = code.size();
        code.insert(code.end(), {0x01, 0x53, 0x40, 0xED, 0x78, 0xE6, mask, 0xFE, want, 0x20});
        code.push_back(static_cast<uint8_t>(at - (code.size() + 1)));
    };
    out(0x21BC, 0x21);         // the primary channel (OUT (#BC),A puts A on A15-A8: port #21BC)
    out(0x4152, 0xB0);         // device/head: the slave
    // Twice: the first command after the disc went in answers UNIT ATTENTION (a player repeats it)
    for (int attempt = 0; attempt < 2; attempt++)
    {
        waitStatus(0x80, 0x00);    // not busy
        out(0x0151, 0x00);         // features: PIO
        out(0x0154, 0x00);         // byte count 0
        out(0x0155, 0x00);
        out(0x4153, 0xA0);         // PACKET
        waitStatus(0x88, 0x08);    // DRQ, not busy
        const uint8_t packet[12] = {0x47, 0x00, 0x00, 0x00, 0x02, 0x00, 0x50, 0x00, 0x4A, 0x00, 0x00, 0x00};
        for (int i = 0; i < 12; i += 2)
        {
            out(0x0050, packet[i]);       // the low byte into the latch
            out(0x0150, packet[i + 1]);   // the word goes out
        }
        waitStatus(0x80, 0x00);    // the command done
    }
    code.insert(code.end(), {0x18, 0xFE});   // JR $
    for (size_t i = 0; i < code.size(); i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
    _z80->pc = 0x8000;
    _z80->iff1 = _z80->iff2 = 0;
    _z80->halted = 0;   // the BIOS may sit in HALT

    StartRecording();
    Record(80);
    _ttd->StopRecording();
    EXPECT_EQ(cd->Audio().PeekStatus(), CdAudioStatus::Playing)
        << "the packet started the play; PC " << std::hex << _z80->pc << " status " << int(cd->State().status) << " error "
        << int(cd->State().error) << " command " << int(cd->State().command) << " phase " << int(cd->State().phase);
    EXPECT_GT(cd->Audio().HeadLba(), 100u) << "the head moved on from 00:02:00 (LBA 0), about 75 sectors a second";
    const size_t last = _ttd->GetCheckpointCount() - 1;
    ExpectExactReplay(0, last, "CD audio, from the start");
    ExpectExactReplay(last / 2, last - last / 2, "CD audio, from the middle");
}

/// The AY (1.75 MHz, ABC stereo) under a recording: a host program walks all its registers through #FFFD / #BFFD in a
/// loop - tone periods, the noise, the mixer, the volumes, the envelope period and shape (a new shape restarts the
/// envelope) - so the chip's generators and envelope state change inside every frame
TEST_F(TTDSprinterShipped_Test, Ay_EveryRegisterChangingInsideFrames)
{
    PowerOn(true);
    Skip(150);
    RunToBoundary();
    SoundChip_AY8910* ay = _context->pSoundManager->getAYChip(0);
    ASSERT_NE(ay, nullptr) << "the Sprinter's AY";
    const uint8_t code[] = {
        0xF3,                    // DI
        0x16, 0x00,              // LD D,0           the value counter
        // loop:
        0x1E, 0x00,              // LD E,0           register
        // reg:
        0x01, 0xFD, 0xFF,        // LD BC,#FFFD
        0xED, 0x59,              // OUT (C),E        select
        0x7A, 0x83,              // LD A,D : ADD A,E  a value per register
        0x06, 0xBF,              // LD B,#BF
        0xED, 0x79,              // OUT (C),A        write
        0x1C,                    // INC E
        0x7B, 0xFE, 0x0E,        // LD A,E : CP 14
        0x20, 0xEF,              // JR NZ,reg
        0x14,                    // INC D
        0x06, 0x00,              // LD B,0           a pause: 256 x DJNZ
        0x10, 0xFE,              // DJNZ $
        0x18, 0xE6};             // JR loop
    for (size_t i = 0; i < sizeof code; i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
    _z80->pc = 0x8000;
    _z80->iff1 = _z80->iff2 = 0;
    _z80->halted = 0;   // the BIOS may sit in HALT

    StartRecording();
    Record(40);
    _ttd->StopRecording();
    std::string regs;
    for (uint8_t r = 0; r < 14; r++)
        regs += std::to_string(ay->readRegisterOnBus(r)) + " ";
    EXPECT_NE(ay->readRegisterOnBus(0) | ay->readRegisterOnBus(7) | ay->readRegisterOnBus(13), 0)
        << "the registers were written: " << regs << " PC " << std::hex << _z80->pc << " chips "
        << _context->pSoundManager->getAYChipCount();
    const size_t last = _ttd->GetCheckpointCount() - 1;
    ExpectExactReplay(0, last, "the AY, from the start");
    ExpectExactReplay(last / 2, last - last / 2, "the AY, from the middle");
}

/// The Covox-Blaster (the Sprinter's 16-bit stereo DAC with its 256-entry ring) under a recording: a host program turns
/// it on (#0046 = #88: CBL on, mono, 8-bit, no INT, 7.8 kHz), then fills the ring again and again with OTIR from a table
/// it changes each round (with the INT off the write address is ~A15..A8: OTIR with B = 0 fills 0 .. 255) - the play
/// position, the ring and the DAC level change inside every frame
TEST_F(TTDSprinterShipped_Test, CovoxBlaster_TheRingPlaysWhileTheHostRefillsIt)
{
    PowerOn(true);
    Skip(150);
    RunToBoundary();
    const uint8_t code[] = {
        0xF3,                    // DI
        0x01, 0x46, 0x00,        // LD BC,#0046
        0x3E, 0x88, 0xED, 0x79,  // LD A,#88 : OUT (C),A     CBL on, 7.8 kHz
        // loop:
        0x21, 0x00, 0x90,        // LD HL,#9000              the table
        0x01, 0xFB, 0x00,        // LD BC,#00FB              B = 0: 256 bytes, port #FB
        0xED, 0xB3,              // OTIR                     into the ring
        0x21, 0x00, 0x90,        // LD HL,#9000
        0x06, 0x00,              // LD B,0
        // bump:
        0x34,                    // INC (HL)                 a new table
        0x23,                    // INC HL
        0x10, 0xFC,              // DJNZ bump
        0x18, 0xED};             // JR loop
    for (size_t i = 0; i < sizeof code; i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
    for (int i = 0; i < 256; i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x9000 + i), static_cast<uint8_t>(i * 3));
    _z80->pc = 0x8000;
    _z80->iff1 = _z80->iff2 = 0;
    _z80->halted = 0;   // the BIOS may sit in HALT

    const CovoxBlasterState before = _decoder->GetCovoxBlaster().State();
    StartRecording();
    Record(40);
    _ttd->StopRecording();
    const CovoxBlasterState& after = _decoder->GetCovoxBlaster().State();
    EXPECT_EQ(after.control, 0x88);
    EXPECT_GT(after.ringWrites - before.ringWrites, 40u * 256u) << "the ring refilled";
    EXPECT_GT(after.ticks - before.ticks, 40u * 100u) << "and played: 7.8 kHz is about 160 ticks a frame";
    const size_t last = _ttd->GetCheckpointCount() - 1;
    ExpectExactReplay(0, last, "the Covox-Blaster, from the start");
    ExpectExactReplay(last / 2, last - last / 2, "the Covox-Blaster, from the middle");
}

/// "Who wrote this byte" in the Sprinter's own memories (2026-10-08): find-last with space vram names a byte of the
/// 256 KB video RAM by its offset, space cache one of the 64 KB fast RAM. A host program maps graphics page #50 into
/// window 1 (#A2), sets PORT_Y #12 (#89), turns the fast RAM on in window 0 (IN #FB) and writes both in a loop: the
/// video RAM through the graphics window (#4005 is offset #12 * 1024 + 5) and the fast RAM at #0123, then the
/// accelerator fills 4 bytes through the graphics window at #4100 (video RAM #4900) and of RAM at #9100 (its stores are
/// the LD (HL),A's; it does not reach the fast RAM). With the
/// write journal (the answer from the journal) and without it (from replay), the search finds the writing instructions
class TTDSprinterSpaces_Test : public TTDSprinterMachine_Test, public ::testing::WithParamInterface<bool>
{
protected:
    /// The program above, recorded for 6 frames (the journal on with the parameter); `atBoundary` looks at every
    /// recorded boundary
    /// The program, loaded at #8000 on a booted machine, the CPU about to run it (nothing recorded)
    void LoadTheProgram()
    {
        PowerOn(true);
        Skip(150);
        RunToBoundary();
        const uint8_t code[] = {
            0xF3,                    // DI
            0x3E, 0x50, 0xD3, 0xA2,  // LD A,#50 : OUT (#A2),A    window 1: graphics page #50
            0x3E, 0x12, 0xD3, 0x89,  // LD A,#12 : OUT (#89),A    PORT_Y
            0xDB, 0xFB,              // IN A,(#FB)                 window 0: fast RAM
            // loop (#800B):
            0x14,                    // INC D
            0x7A,                    // LD A,D
            0x32, 0x05, 0x40,        // #800D LD (#4005),A         video RAM #4805
            0x32, 0x23, 0x01,        // #8010 LD (#0123),A         fast RAM
            0x3A, 0x06, 0x48,        // #8013 LD A,(#4806)         a read of video RAM #4806 (written by nobody)
            0x21, 0x00, 0x41,        // LD HL,#4100
            0x52, 0x1E, 0x04, 0x49,  // LD D,D : LD E,4 : LD C,C   the accelerator: length 4, fill
            0x77, 0x40,              // #801D LD (HL),A : LD B,B   video RAM #4900-#4903
            0x21, 0x00, 0x91,        // LD HL,#9100
            0x52, 0x1E, 0x04, 0x49,  // LD D,D : LD E,4 : LD C,C
            0x77, 0x40,              // #8026 LD (HL),A : LD B,B   RAM #9100-#9103
            0x06, 0x00, 0x10, 0xFE,  // LD B,0 : DJNZ $
            0x18, 0xDD};             // JR loop
        for (size_t i = 0; i < sizeof code; i++)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
        _z80->pc = 0x8000;
        _z80->iff1 = _z80->iff2 = 0;
        _z80->halted = 0;   // the BIOS may sit in HALT

    }

    void RecordTheProgram(const std::function<void()>& atBoundary = {})
    {
        ASSERT_NO_FATAL_FAILURE(LoadTheProgram());
        ASSERT_TRUE(_ttd->SetEnableWriteJournal(GetParam()));
        StartRecording();
        Record(6, {}, atBoundary);
        _ttd->StopRecording();
    }
};

TEST_P(TTDSprinterSpaces_Test, FindLastNamesTheWriterOfAVideoAndAFastRamByte)
{
    ASSERT_NO_FATAL_FAILURE(RecordTheProgram());
    const SprinterPldState& pld = _decoder->GetPldState();
    ASSERT_EQ(pld.portY, 0x12);
    ASSERT_EQ(pld.Cell(SprinterCode::Page1), 0x50);
    ASSERT_TRUE(pld.cacheOn);
    const uint32_t cacheOffset = (pld.romRg & 0x03) * 0x4000u + 0x123;
    const uint8_t last = static_cast<uint8_t>(_z80->d);
    const uint8_t video = _decoder->GetVideoRam().Read(0x4805);
    EXPECT_TRUE(video == last || video == static_cast<uint8_t>(last - 1)) << "the program writes the video RAM";

    auto findLast = [&](const char* space, uint32_t offset) {
        return ttd::TTDControl(_context).Execute(
            {"find-last", {{"addr", std::to_string(offset)}, {"space", space}, {"access", "write"}}});
    };
    const ttd::TTDReply v = findLast("vram", 0x4805);
    ASSERT_TRUE(v.Ok()) << v.message;
    ASSERT_TRUE(v.body.find("found")->b) << "the video RAM write";
    EXPECT_EQ(v.body.find("pc")->i, 0x800D);
    EXPECT_EQ(v.body.find("space")->s, "vram");
    EXPECT_EQ(v.body.find("offset")->i, 0x4805);
    EXPECT_EQ(v.body.find("phys_page")->kind, StateNode::Kind::Null) << "not a RAM page";

    const ttd::TTDReply c = findLast("cache", cacheOffset);
    ASSERT_TRUE(c.Ok()) << c.message;
    ASSERT_TRUE(c.body.find("found")->b) << "the fast RAM write";
    EXPECT_EQ(c.body.find("pc")->i, 0x8010);
    EXPECT_EQ(c.body.find("space")->s, "cache");
    EXPECT_EQ(c.body.find("offset")->i, cacheOffset);

    // The accelerator's stores past the first: the video RAM by its space, RAM by the Z80 address
    ASSERT_EQ(_decoder->GetVideoRam().Read(0x4903), _decoder->GetVideoRam().Read(0x4900)) << "the accelerator filled";
    const ttd::TTDReply fv = findLast("vram", 0x4903);
    ASSERT_TRUE(fv.Ok()) << fv.message;
    ASSERT_TRUE(fv.body.find("found")->b) << "the accelerator's video RAM store";
    EXPECT_EQ(fv.body.find("pc")->i, 0x801D);
    const ttd::TTDReply fr = findLast("ram", 0x9103);
    ASSERT_TRUE(fr.Ok()) << fr.message;
    ASSERT_TRUE(fr.body.find("found")->b) << "the accelerator's RAM store";
    EXPECT_EQ(fr.body.find("pc")->i, 0x8026);

    // A read through the graphics window, by the video RAM's address (from replay: the journal holds writes)
    const ttd::TTDReply rd = ttd::TTDControl(_context).Execute(
        {"find-last", {{"addr", std::to_string(0x4806)}, {"space", "vram"}, {"access", "read"}}});
    ASSERT_TRUE(rd.Ok()) << rd.message;
    ASSERT_TRUE(rd.body.find("found")->b) << "the video RAM read";
    EXPECT_EQ(rd.body.find("pc")->i, 0x8013);
    EXPECT_EQ(rd.body.find("access")->s, "read");
    EXPECT_EQ(fr.body.find("addr")->i, 0x9103);

    // A neighbour nobody wrote, in each space; the fast RAM's in-page address #0123 in the video RAM (another space)
    for (const auto& [space, offset] :
         {std::pair{"vram", 0x4806u}, std::pair{"cache", cacheOffset + 1}, std::pair{"vram", 0x0123u}})
    {
        const ttd::TTDReply none = findLast(space, offset);
        ASSERT_TRUE(none.Ok()) << none.message;
        EXPECT_FALSE(none.body.find("found")->b) << space;
    }
}

// The contents of the video and fast RAM at a past checkpoint (memory-at) and what changed between two (memory-diff),
// from the engine's store without moving the machine; the coverage index answers for a video RAM byte (space)
TEST_P(TTDSprinterSpaces_Test, MemoryAtMemoryDiffAndCoverageReadTheSpaces)
{
    std::map<uint64_t, uint8_t> seen;   // video RAM #4805 at every recorded boundary
    ASSERT_NO_FATAL_FAILURE(RecordTheProgram([&] { seen[Frame()] = _decoder->GetVideoRam().Read(0x4805); }));
    ASSERT_GE(seen.size(), 4u);
    const uint64_t now = Frame();
    auto run = [&](const char* verb, std::map<std::string, std::string> options) {
        return ttd::TTDControl(_context).Execute({verb, std::move(options)});
    };

    // Each boundary's byte, from the store; the machine stays where it is
    for (const auto& [frame, value] : seen)
    {
        const ttd::TTDReply at = run("memory-at", {{"space", "vram"}, {"offset", "0x4805"}, {"length", "2"},
                                                   {"frame", std::to_string(frame)}});
        ASSERT_TRUE(at.Ok()) << at.message;
        EXPECT_EQ(at.body.find("space")->s, "sprinter.vram");
        EXPECT_TRUE(at.body.find("exact")->b);
        char expected[3];
        std::snprintf(expected, sizeof expected, "%02X", value);
        EXPECT_EQ(at.body.find("hex")->s.substr(0, 2), expected) << "frame " << frame;
    }
    EXPECT_EQ(Frame(), now) << "memory-at does not seek";

    // Between the first and the last boundary: the CPU's byte and nothing else in the video RAM (the accelerator fills
    // #4900-#4903 with the byte the program read from #4806, which nobody writes)
    const uint64_t first = seen.begin()->first;
    const uint64_t last = seen.rbegin()->first;
    const ttd::TTDReply diff = run("memory-diff", {{"space", "sprinter.vram"}, {"from_frame", std::to_string(first)},
                                                   {"to_frame", std::to_string(last)}});
    ASSERT_TRUE(diff.Ok()) << diff.message;
    std::vector<std::pair<int64_t, int64_t>> ranges;
    for (const StateNode& r : diff.body.find("ranges")->items)
        ranges.emplace_back(r.find("offset")->i, r.find("length")->i);
    EXPECT_EQ(ranges, (std::vector<std::pair<int64_t, int64_t>>{{0x4805, 1}}));
    EXPECT_EQ(diff.body.find("changed_bytes")->i, 1);
    const ttd::TTDReply cache = run("memory-diff", {{"space", "cache"}, {"from_frame", std::to_string(first)},
                                                    {"to_frame", std::to_string(last)}});
    ASSERT_TRUE(cache.Ok()) << cache.message;
    const uint32_t cacheOffset = (_decoder->GetPldState().romRg & 0x03) * 0x4000u + 0x123;
    ASSERT_EQ(cache.body.find("ranges")->items.size(), 1u);
    EXPECT_EQ(cache.body.find("ranges")->items[0].find("offset")->i, cacheOffset);
    EXPECT_EQ(cache.body.find("space")->s, "sprinter.fastram");

    // The coverage index by the video RAM's address: #4805 written in every frame, #4806 only read
    const ttd::TTDReply written = run("coverage-scan", {{"kind", "written"}, {"space", "vram"}, {"addr_from", "0x4805"},
                                                        {"addr_to", "0x4805"}});
    ASSERT_TRUE(written.Ok()) << written.message;
    EXPECT_GE(written.body.find("matching_frames")->i, 4);
    EXPECT_EQ(written.body.find("space")->s, "vram");
    EXPECT_EQ(written.body.find("offset_from")->s, "0x04805");
    const ttd::TTDReply read = run("coverage-probe", {{"frame", std::to_string(last - 1)}, {"kind", "read"}, {"space", "vram"},
                                                      {"addr_from", "0x4806"}, {"addr_to", "0x4806"}});
    ASSERT_TRUE(read.Ok()) << read.message;
    EXPECT_TRUE(read.body.find("touched")->b) << "read through the window";
    const ttd::TTDReply notWritten = run("coverage-probe", {{"frame", std::to_string(last - 1)}, {"kind", "written"},
                                                            {"space", "vram"}, {"addr_from", "0x4806"}, {"addr_to", "0x4806"}});
    ASSERT_TRUE(notWritten.Ok()) << notWritten.message;
    EXPECT_FALSE(notWritten.body.find("touched")->b);

    // Refusals say why
    EXPECT_EQ(run("memory-at", {{"space", "nope"}, {"frame", "0"}}).error, ttd::TTDControlError::BadRequest);
    const uint64_t start = _ttd->GetCheckpoint(0)->time.frame;
    EXPECT_EQ(run("memory-at", {{"space", "vram"}, {"frame", std::to_string(start - 1)}}).error, ttd::TTDControlError::BadRequest)
        << "before the first checkpoint";
    EXPECT_EQ(run("coverage-scan", {{"space", "neogs.ram"}, {"addr_from", "0"}}).error, ttd::TTDControlError::BadRequest)
        << "no accesses recorded there";
}

// A watchpoint on a video RAM page (vram1: offsets #4000-#7FFF of the video RAM): the write through the graphics
// window at #4005 (video RAM #4805) stops the run after that instruction; the read of #4806 stops a read watchpoint;
// nothing else in the page is touched, so a watchpoint on #4807 never fires
TEST_P(TTDSprinterSpaces_Test, AVideoRamWatchpointStopsTheWriterAndTheReader)
{
    ASSERT_NO_FATAL_FAILURE(LoadTheProgram());
    _emulator->GetFeatureManager()->setFeature(Features::kBreakpoints, true);
    _context->pMemory->UpdateFeatureCache();
    BreakpointManager& brk = *_emulator->GetBreakpointManager();
    auto watch = [&](uint8_t access, uint16_t offset) {
        BreakpointSpec spec;
        spec.type = BRK_MEMORY;
        spec.access = access;
        spec.address = offset;
        std::string error;
        EXPECT_TRUE(BreakpointManager::ParsePageInto("vram1", spec, error)) << error;
        const uint16_t id = brk.AddBreakpoint(spec, error);
        EXPECT_NE(id, BRK_INVALID) << error;
        return id;
    };
    const uint16_t never = watch(BRK_MEM_WRITE | BRK_MEM_READ, 0x0807);
    const uint16_t written = watch(BRK_MEM_WRITE, 0x0805);
    _emulator->RunNCPUCycles(200, false);
    Emulator::BreakpointStop stop = _emulator->LastDirectStop();
    ASSERT_TRUE(stop.hit) << "the write through the window";
    EXPECT_EQ(stop.breakpointId, written);
    EXPECT_EQ(stop.kind, BreakpointHitKind::MemoryWrite);
    EXPECT_EQ(stop.address, 0x4005) << "the CPU address of the access";
    EXPECT_EQ(_z80->pc, 0x8010) << "after LD (#4005),A";
    EXPECT_NE(brk.GetBreakpointListAsString().find("in vram1"), std::string::npos) << brk.GetBreakpointListAsString();

    brk.RemoveBreakpointByID(written);
    const uint16_t read = watch(BRK_MEM_READ, 0x0806);
    _emulator->RunNCPUCycles(200, false);
    stop = _emulator->LastDirectStop();
    ASSERT_TRUE(stop.hit) << "the read through the window";
    EXPECT_EQ(stop.breakpointId, read);
    EXPECT_EQ(stop.kind, BreakpointHitKind::MemoryRead);
    EXPECT_EQ(_z80->pc, 0x8016) << "after LD A,(#4806)";
    EXPECT_EQ(brk.GetBreakpointById(never)->hitCount, 0u);

    // A CPU address breakpoint of the same number is another breakpoint: #0807 in the Z80 view is not the video RAM
    BreakpointSpec cpu;
    cpu.type = BRK_MEMORY;
    cpu.access = BRK_MEM_WRITE | BRK_MEM_READ;
    cpu.address = 0x0807;
    std::string error;
    const uint16_t cpuId = brk.AddBreakpoint(cpu, error);
    EXPECT_NE(cpuId, BRK_INVALID) << error;
    EXPECT_NE(cpuId, never) << "not merged with the video RAM watchpoint";
    // Refusals: execution, a range across pages, a page past the video RAM
    BreakpointSpec bad;
    bad.type = BRK_MEMORY;
    bad.access = BRK_MEM_EXECUTE;
    ASSERT_TRUE(BreakpointManager::ParsePageInto("vram2", bad, error));
    EXPECT_EQ(brk.AddBreakpoint(bad, error), BRK_INVALID);
    EXPECT_FALSE(BreakpointManager::ParsePageInto("vram16", bad, error));
}

// memory-at inside a frame (tinframe): the frame's checkpoint and its writes up to that point - the video RAM byte
// the program rewrites every loop and the RAM the accelerator fills read as a seek to the same point shows them.
// Without the journal the frame's writes are rebuilt by replay, and the machine stays where it was
TEST_P(TTDSprinterSpaces_Test, MemoryAtInsideAFrameMatchesASeekThere)
{
    ASSERT_NO_FATAL_FAILURE(RecordTheProgram());
    const uint64_t frame = _ttd->GetCheckpoint(3)->time.frame;
    const uint64_t here = Frame();
    std::vector<std::pair<uint32_t, std::string>> asked;
    for (uint32_t tin : {1000u, 25000u, 60000u, 90000u})
    {
        const ttd::TTDReply at = ttd::TTDControl(_context).Execute(
            {"memory-at", {{"space", "vram"}, {"offset", "0x4805"}, {"length", "1"}, {"frame", std::to_string(frame)},
                           {"tinframe", std::to_string(tin)}}});
        ASSERT_TRUE(at.Ok()) << at.message;
        EXPECT_EQ(at.body.find("tinframe")->i, tin);
        const ttd::TTDReply ram = ttd::TTDControl(_context).Execute(
            {"memory-at", {{"space", "ram"}, {"offset", std::to_string(_context->pMemory->GetRAMPageForBank2() * 0x4000u + 0x1100)},
                           {"length", "1"}, {"frame", std::to_string(frame)}, {"tinframe", std::to_string(tin)}}});
        ASSERT_TRUE(ram.Ok()) << ram.message;
        asked.emplace_back(tin, at.body.find("hex")->s + ram.body.find("hex")->s);
    }
    EXPECT_EQ(Frame(), here) << "memory-at moves nothing";
    std::set<std::string> distinct;
    for (const auto& [tin, hex] : asked)
    {
        distinct.insert(hex);
        ASSERT_TRUE(_ttd->SeekTo({frame, tin}));
        char seen[8];
        std::snprintf(seen, sizeof seen, "%02X%02X", _decoder->GetVideoRam().Read(0x4805),
                      _context->pMemory->DirectReadFromZ80Memory(0x9100));
        EXPECT_EQ(hex, seen) << "frame " << frame << " t=" << tin;
    }
    EXPECT_GE(distinct.size(), 3u) << "the byte changes inside the frame";

    // Other memories are read at frame starts only; tinframe there is refused, not ignored
    EXPECT_EQ(ttd::TTDControl(_context).Execute({"memory-at", {{"space", "rtc.cmos"}, {"frame", std::to_string(frame)},
                                                               {"tinframe", "100"}}}).error,
              ttd::TTDControlError::BadRequest);
}

INSTANTIATE_TEST_SUITE_P(Journal, TTDSprinterSpaces_Test, ::testing::Bool(),
                         [](const auto& info) { return info.param ? "FromTheJournal" : "FromReplay"; });
