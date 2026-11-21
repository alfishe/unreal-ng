// The gs-debugger validation scenarios (design §8A.10, requirements §4.16 V-1..V-6): NedoOS software talking to the
// machine's NeoGS, run headlessly, its command log against a golden text. The machine is the shipped ZX-Evo (zxevo-ers
// fits a NeoGS); NedoOS boots from a host folder built per test (the zxevo testdata card plus the program and an
// autoexec.bat that starts it), as ZXEvoErs_Test.NedoOsBootsFromAHostFolder does.
//
// A whole OS boot and the program's run (hundreds to thousands of frames): the long tier (UNREAL_LONG_TESTS=1).
// The golden files are in testdata/software/nedoos/golden/; on a difference the actual log is written to
// scratch/gs-nedoos-scenarios/<name>.actual.txt and its path printed.

#include "pch.h"

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/io/keyboard/pckey.h>
#include <emulator/io/network/virtualnetwork.h>
#include <emulator/media/mediamanager.h>
#include <emulator/cpu/core.h>
#include <emulator/cpu/z80.h>
#include <emulator/memory/memory.h>
#include <emulator/ports/models/portdecoder_atm3.h>
#include <emulator/sound/chips/gs/firmware/gsfirmwareprofile.h>
#include <emulator/sound/chips/gs/generalsoundcard.h>
#include <emulator/sound/chips/gs/gscommandlog.h>
#include <emulator/sound/chips/neogs/soundchip_neogs.h>
#include <emulator/sound/soundmanager.h>
#include <emulator/state/devicestate.h>
#include <emulator/state/statenodejson.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <fstream>
#include <map>
#include <numeric>
#include <tuple>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/scriptedhostnet.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testtier.h"

namespace
{

namespace fs = std::filesystem;
using Kind = GSCommandLogKind;

std::string Utf8(const fs::path& p)
{
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

std::string ReadText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

class GSNedoOsScenarios_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    GSCommandLog* _log = nullptr;
    SoundCardScope _sound{TestSound::GeneralSound};  // the runner leaves the card out unless asked; the shipped NeoGS
    std::string _comPort;                  ///< [NETWORK] ComPort= of the machine Boot creates (empty: none)
    std::function<void()> _onCreate;       ///< run once Boot created the machine, before it boots
    std::function<void()> _atMenu;         ///< run at the ERS menu, before the SD boot

    void SetUp() override
    {
        if (!TestTier::Long())
            GTEST_SKIP() << "a NedoOS boot and a program's run: UNREAL_LONG_TESTS=1";
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            _manager->RemoveEmulator(_emulator->GetId());
    }

    /// The zxevo NedoOS card plus `files` (name in bin/ -> source path) and an autoexec.bat running `command`
    fs::path Card(const std::map<std::string, fs::path>& files, const std::string& command)
    {
        const fs::path root = TestPathHelper::FindProjectRoot();
        const fs::path card = TestPathHelper::GetUniqueTestScratchPath("nedoos-card");
        std::error_code ec;
        fs::remove_all(card, ec);
        fs::create_directories(card);
        fs::copy(root / "testdata/machines/zxevo/nedoos/sdcard", card, fs::copy_options::recursive, ec);
        EXPECT_FALSE(ec) << ec.message();
        for (const auto& [name, source] : files)
        {
            fs::create_directories((card / "bin" / name).parent_path(), ec);
            fs::copy_file(source, card / "bin" / name, fs::copy_options::overwrite_existing, ec);
            EXPECT_FALSE(ec) << Utf8(source) << ": " << ec.message();
        }
        std::ofstream(card / "bin/autoexec.bat", std::ios::binary) << command << "\r\n";
        return card;
    }

    /// The shipped ZX-Evo with `kind` in its ZX-bus slot (its NeoGS with firmware v1.11, profile neogs, or the classic
    /// GS with the stock v1.05), the card inserted, the command log recording, booted to the ERS menu
    /// `gsRamKB`: the classic card's RAM (0: the machine's, 512 on the ZX-Evo)
    void Boot(const fs::path& card, GSTypeKind kind, unsigned gsRamKB = 0)
    {
        const std::string comPort = _comPort;
        _emulator = _manager->CreateEmulatorWithModelAndRAM(
            "zxevo-ers", "ATM3", 4096, LoggerLevel::LogError, nullptr, [gsRamKB, comPort](CONFIG& config) {
                if (gsRamKB)
                    config.sound.gsRamKB = gsRamKB;
                if (!comPort.empty())
                    std::snprintf(config.network.comPort, sizeof(config.network.comPort), "%s", comPort.c_str());
            });
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        if (_onCreate)
            ASSERT_NO_FATAL_FAILURE(_onCreate());
        if (auto* decoder = static_cast<PortDecoder_ATM3*>(_context->pPortDecoder))
            decoder->GetRtc().SetFixedTime(1767268830);  // deterministic RTC, as the ERS tests
        _emulator->EnableTurboMode();
        ASSERT_EQ(_context->pSoundManager->fittedGeneralSoundKind(), GSTypeKind::NGS) << "zxevo-ers fits a NeoGS";
        if (kind != GSTypeKind::NGS)
            ASSERT_TRUE(FitGeneralSoundCard(_context->pSoundManager, kind));
        _log = _context->pSoundManager->gsCommandLog();
        ASSERT_NE(_log, nullptr);
        std::string error;
        ASSERT_TRUE(_log->History().Configure({.mode = history::Mode::Capture}, error)) << error;

        MediaSource source;
        source.path = Utf8(card);
        InsertOptions options;
        options.freeBytes = 16 * 1024 * 1024;
        const MediaResult inserted = _context->pMediaManager->Insert("sd.zc", source, options);
        ASSERT_TRUE(inserted.Ok()) << inserted.message;

        // The ERS main menu, then "5. SDcard boot" -> SD_BOOT.$C -> the kernel -> autoexec.bat
        Memory* memory = _context->pMemory;
        Z80* z80 = _context->pCore->GetZ80();
        const auto atMenu = [&] { return z80->pc == 0x6117 && memory->IsBank0ROM() && memory->GetROMPage() == 28u; };
        EmulatorTestHelper::RunUntil(_emulator.get(), atMenu, 300);
        ASSERT_TRUE(atMenu()) << "the ERS main menu";
        if (_atMenu)
            ASSERT_NO_FATAL_FAILURE(_atMenu());
        _context->pKeyboard->PressKey(ZXKEY_5);
        _emulator->RunNFrames(4, true);
        _context->pKeyboard->ReleaseKey(ZXKEY_5);
    }

    std::vector<GSCommandLogEntry> Entries() const { return _log->History().Snapshot(); }

    /// The screen's text (NedoOS's text mode), for the failure messages
    std::string Screen() const
    {
        std::string text;
        const StateNode lines = DeviceState::VideoText(_context);
        if (const StateNode* list = lines.find("lines"))
            for (const StateNode& line : list->items)
                text += line.find("text")->s + "\n";
        return text;
    }

    /// The entries with their frames and card cycles (timing, for the failure messages)
    std::string Frames() const
    {
        std::string text = "\nframes:\n";
        for (const GSCommandLogEntry& e : Entries())
            text += std::to_string(e.frame) + " " + std::to_string(e.cardCycle) + " " + e.Text() + "\n";
        return text;
    }

    int Count(Kind kind, uint8_t command) const
    {
        int n = 0;
        for (const GSCommandLogEntry& e : Entries())
            n += e.kind == kind && e.command == command ? 1 : 0;
        return n;
    }

    bool Has(Kind kind, uint8_t command) const
    {
        for (const GSCommandLogEntry& e : Entries())
            if (e.kind == kind && e.command == command)
                return true;
        return false;
    }

    /// The log as text, timing-free: one line per entry. A run of equal lines is kept once with "(repeated)", a run of
    /// an equal block of 2 to 4 lines (a poll and its reply) once with "(the N lines above repeat)": polling loops whose
    /// length depends on the card's pace. With `stopCommand`, up to its `stopCount`-th command (a player's per-frame
    /// poll: the golden holds its first rounds, the test counts the rest)
    /// `skip`: commands whose own entries (the command, its taking, its replies) are left out: a streamer's polls,
    /// whose replies follow the card's pace (the test counts them)
    std::string Golden(int stopCommand = -1, int stopCount = 0, const std::vector<uint8_t>& skip = {}) const
    {
        std::vector<std::string> lines;
        int seen = 0;
        for (const GSCommandLogEntry& e : Entries())
        {
            if (e.kind == Kind::Command && e.command == stopCommand && ++seen > stopCount)
                break;
            const bool own = e.kind == Kind::Command || e.kind == Kind::CardTook || e.kind == Kind::Reply;
            if (own && std::find(skip.begin(), skip.end(), e.command) != skip.end())
                continue;
            lines.push_back(e.Text());
        }
        const auto same = [&](size_t a, size_t b, size_t k) {
            for (size_t j = 0; j < k; j++)
                if (lines[a + j] != lines[b + j])
                    return false;
            return true;
        };
        std::string text;
        for (size_t i = 0; i < lines.size();)
        {
            size_t block = 0;
            for (size_t k = 1; k <= 4 && !block; k++)
                if (i + 2 * k <= lines.size() && same(i, i + k, k))
                    block = k;
            if (!block)
            {
                text += lines[i++] + "\n";
                continue;
            }
            size_t next = i + block;
            while (next + block <= lines.size() && same(i, next, block))
                next += block;
            for (size_t j = 0; j < block; j++)
                text += lines[i + j] + (block == 1 ? " (repeated)" : "") + "\n";
            if (block > 1)
                text += "(the " + std::to_string(block) + " lines above repeat)\n";
            i = next;
        }
        return text;
    }

    /// The replies to `command`'s first reply byte, in order (#60: the song positions GP reads)
    std::vector<uint8_t> Replies(uint8_t command) const
    {
        std::vector<uint8_t> out;
        for (const GSCommandLogEntry& e : Entries())
            if (e.kind == Kind::Reply && e.command == command && e.index == 0)
                out.push_back(e.value);
        return out;
    }

    /// Whether `command` was written after the first `after`
    bool HasAfter(uint8_t after, uint8_t command) const
    {
        bool past = false;
        for (const GSCommandLogEntry& e : Entries())
        {
            if (e.kind != Kind::Command)
                continue;
            if (past && e.command == command)
                return true;
            past = past || e.command == after;
        }
        return false;
    }

    /// The log equals testdata/software/nedoos/golden/<name>.txt; else the actual one goes to scratch
    void ExpectGolden(const std::string& name, int stopCommand = -1, int stopCount = 0,
                      const std::vector<uint8_t>& skip = {})
    {
        const fs::path golden = TestPathHelper::FindProjectRoot() / "testdata/software/nedoos/golden" / (name + ".txt");
        const std::string actual = Golden(stopCommand, stopCount, skip);
        const std::string expected = ReadText(golden);
        if (actual != expected)
        {
            // Kept after the run (the per-process scratch is removed): the repository's git-ignored scratch/
            const fs::path dir = TestPathHelper::FindProjectRoot() / "scratch/gs-nedoos-scenarios";
            std::error_code ec;
            fs::create_directories(dir, ec);
            const fs::path out = dir / (name + ".actual.txt");
            std::ofstream(out, std::ios::binary) << actual;
            ADD_FAILURE() << "the command log differs from " << Utf8(golden) << "; actual: " << Utf8(out);
        }
    }

    /// The program's anomalies: the ones after the kernel's boot probe of the card (data #00, #23 read without a
    /// poll, data #00, #1E: the kernel's, in every golden, not the program's)
    std::map<GSCommandLogAnomaly, int> Anomalies() const
    {
        std::map<GSCommandLogAnomaly, int> out;
        bool probed = false;
        for (const GSCommandLogEntry& e : Entries())
        {
            if (!probed)
            {
                probed = e.kind == Kind::Command && e.command == 0x1E;
                continue;
            }
            if (e.kind == Kind::Anomaly)
                out[static_cast<GSCommandLogAnomaly>(e.value)]++;
        }
        return out;
    }
};

} // namespace

/// V-1 gstest (kapps/gstest) on the classic GS: the stock v1.05 vocabulary, the boot page count read by value polling
/// after #F4 (a poll, not a drain), the test mode's numbers #0B-#0E, the replies read without a poll (F4
/// reply_read_without_poll), the embedded module uploaded inside #30 ... #D2 and played with #31. Before gstest the
/// kernel probes the card (data #00, #23, #1E); its anomalies are the kernel's (Anomalies() starts after the probe)
TEST_F(GSNedoOsScenarios_Test, V1GsTestTalksTheStockVocabulary)
{
    const fs::path root = TestPathHelper::FindProjectRoot();
    const fs::path card =
        Card({{"gstest.com", root / "testdata/machines/p2666/software/nedoos/sdcard/bin/gstest.com"}}, "gstest");
    ASSERT_NO_FATAL_FAILURE(Boot(card, GSTypeKind::Z80));

    // The four test-mode beeps take about 5 s each; the module plays once #31 is in
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return Has(Kind::Command, 0x31); }, 3000, 25);
    ASSERT_TRUE(Has(Kind::Command, 0x31)) << Golden();
    _emulator->RunNFrames(10, true);
    ASSERT_NE(_log->Profile(), nullptr) << "the classic card's ROM has a profile";

    for (const uint8_t command : {0xF4, 0x20, 0xFA, 0x0B, 0x0C, 0x0D, 0x0E, 0x30, 0xD1, 0xD2, 0x31})
        EXPECT_TRUE(Has(Kind::Command, command)) << "#" << std::hex << int(command) << "\n" << Golden();
    for (const auto& [kind, count] : Anomalies())
        EXPECT_EQ(kind, GSCommandLogAnomaly::ReplyReadWithoutPoll)
            << GSCommandLogAnomalyName(kind) << " x" << count << "\n" << Golden();
    EXPECT_GT(Anomalies()[GSCommandLogAnomaly::ReplyReadWithoutPoll], 0) << "gstest reads #20's reply without a poll";
    ExpectGolden("v1-gstest");
}

/// V-2: GP plays a module on the stock firmware. GP's own code (gp/gp.plr's gscode) goes up with #14 / #13, asks for
/// the chip id (#02), and leaves with #00; the module then goes the stock way (#30, a #D1 ... #D2 stream, #31), and GP
/// polls #60 every frame for the song position. A position lower than the last ends the song (gp/mp3.asm playmod), and
/// GP unloads the module with #F3 (musicunload). A cancel during the load (Esc between two 32 KB chunks,
/// .cancelloading) sends #F4 into the open stream: the firmware restarts (LOAD_L, COMF4), the log flags
/// command_while_busy (F4).
///
/// The module is Karbofos's "After the war 2 GS" (a GS module: 61902 bytes, two chunks, 8 positions). The card has the
/// stock 128 KB: GP waits at most 50 frames for its #F3 after the card's hardware reset (gshardreset, gp/mp3.asm:651-670),
/// and the stock firmware first checks its ROM and all its RAM (INIT_L; the v1.05a patch keeps SKIP_MEM_TEST off). With
/// the ZX-Evo's 512 KB that takes 127 frames (29.7 M card cycles at 12 MHz): GP reports no device, as it would on such
/// a board
class GSNedoOsGp_Test : public GSNedoOsScenarios_Test
{
protected:
    fs::path _mod;

    /// GP with `media` as `name` (an 8.3 name: GP does not open a longer one from its command line) on `kind`
    void BootGp(const fs::path& media, const std::string& name, GSTypeKind kind, unsigned gsRamKB)
    {
        const fs::path root = TestPathHelper::FindProjectRoot();
        const fs::path bin = root / "testdata/machines/p2666/software/nedoos/sdcard/bin";
        _mod = media;
        const fs::path card = Card({{"gp.com", bin / "gp.com"}, {"gp/gp.plr", bin / "gp/gp.plr"},
                                    // Without gp/gp.ini GP opens its settings screen; the shipped defaults (ModDevice=0)
                                    {"gp/gp.ini", root / "testdata/software/nedoos/media/gp.ini"},
                                    {name, media}},
                                   "gp " + name);
        ASSERT_NO_FATAL_FAILURE(Boot(card, kind, gsRamKB));
    }

    void BootGp()
    {
        BootGp(TestPathHelper::FindProjectRoot() / "testdata/software/nedoos/media/afterwr2.mod", "afterwr2.mod",
               GSTypeKind::Z80, 128);
    }

    /// The `n`-th (1-based) `command`'s index in the log, or the log's size
    size_t IndexOf(uint8_t command, int n) const
    {
        const std::vector<GSCommandLogEntry> entries = Entries();
        for (size_t i = 0; i < entries.size(); i++)
            if (entries[i].kind == Kind::Command && entries[i].command == command && --n == 0)
                return i;
        return entries.size();
    }

    /// The #30 upload's block entries
    std::vector<uint32_t> ModuleBlocks() const
    {
        std::vector<uint32_t> out;
        for (const GSCommandLogEntry& e : Entries())
            if (e.kind == Kind::Block && e.command == 0x30)
                out.push_back(e.count);
        return out;
    }

    void ExpectNoAnomalies(int stopCommand, int stopCount)
    {
        for (const auto& [kind, count] : Anomalies())
            ADD_FAILURE() << "unexpected " << GSCommandLogAnomalyName(kind) << " x" << count << "\n"
                          << Golden(stopCommand, stopCount);
    }
};

TEST_F(GSNedoOsGp_Test, V2GpPlaysAModToItsEnd)
{
    ASSERT_NO_FATAL_FAILURE(BootGp());
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return Has(Kind::Command, 0x31); }, 3000, 25);
    ASSERT_TRUE(Has(Kind::Command, 0x31)) << Golden() << "\nscreen:\n" << Screen() << Frames();
    const int pollsBefore = Count(Kind::Command, 0x60);
    _emulator->RunNFrames(100, true);
    EXPECT_GE(Count(Kind::Command, 0x60) - pollsBefore, 50) << "GP polls the position about every frame (F1c sizing)";

    // The module is one block entry of its whole size (F1a), over GP's two chunks
    EXPECT_EQ(ModuleBlocks(), std::vector<uint32_t>{static_cast<uint32_t>(fs::file_size(_mod))}) << Golden(0x60, 3);

    // The card code: recorded, matched to its profile, started; #00 went back to the ROM's profile
    const std::vector<GSUploadRecord> uploads = _log->Uploads();
    ASSERT_FALSE(uploads.empty());
    ASSERT_NE(uploads.front().profile, nullptr) << uploads.front().sha256;
    EXPECT_STREQ(uploads.front().profile->id, "nedoos-gp-gscode");
    EXPECT_TRUE(uploads.front().started);
    ASSERT_NE(_log->Profile(), nullptr);
    EXPECT_EQ(std::string(_log->Profile()->id).rfind("gs10", 0), 0u) << _log->Profile()->id;

    // The end: the position falls back, and GP unloads with #F3
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return HasAfter(0x31, 0xF3); }, 6000, 50);
    ASSERT_TRUE(HasAfter(0x31, 0xF3)) << "\nscreen:\n" << Screen();
    const std::vector<uint8_t> positions = Replies(0x60);
    ASSERT_FALSE(positions.empty());
    EXPECT_LT(positions.back(), *std::max_element(positions.begin(), positions.end())) << "the position fell back";
    for (size_t i = 1; i + 1 < positions.size(); i++)
        EXPECT_GE(positions[i], positions[i - 1]) << "the positions rise until the last read, " << i;

    ExpectNoAnomalies(0xF3, 2);
    ExpectGolden("v2-gp-mod", 0xF3, 2);
}

TEST_F(GSNedoOsGp_Test, V2GpCancelsAModLoadWithF4)
{
    ASSERT_NO_FATAL_FAILURE(BootGp());
    // The module load starts (#30): the 61902 bytes take a few frames, so Esc goes down at once
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return Has(Kind::Command, 0x30); }, 3000, 1);
    ASSERT_TRUE(Has(Kind::Command, 0x30)) << Golden() << "\nscreen:\n" << Screen();

    // Esc held through the load: GP looks for NedoOS key_esc between its 32 KB chunks. NedoOS for the ZX-Evo
    // (PS2KBD=1) reads the keyboard from the AVR's PS/2 log, not the matrix, so the key goes in as a PC key
    _context->pKeyboard->ApplyPcKey(PcKey::Escape, true);
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return HasAfter(0xD1, 0xF4) || Has(Kind::Command, 0x31); },
                                 500, 1);
    _context->pKeyboard->ApplyPcKey(PcKey::Escape, false);
    _emulator->RunNFrames(10, true);

    ASSERT_TRUE(HasAfter(0xD1, 0xF4)) << Golden() << "\nscreen:\n" << Screen() << Frames();
    EXPECT_FALSE(Has(Kind::Command, 0x31)) << "the cancelled module is not played";
    const std::vector<uint32_t> blocks = ModuleBlocks();
    ASSERT_EQ(blocks.size(), 1u) << Golden();
    EXPECT_LT(blocks.front(), static_cast<uint32_t>(fs::file_size(_mod))) << "the upload stopped part way";
    EXPECT_EQ(blocks.front() % 0x8000, 0u) << "at a chunk's end";

    // GP sends #F4 into the open #D1 stream without #D2. LOAD_L takes it (COMF4) and restarts, and F4 flags it as
    // command_while_busy (requirements F4, rev. 4): that one entry and nothing else
    const std::map<GSCommandLogAnomaly, int> anomalies = Anomalies();
    EXPECT_EQ(anomalies, (std::map<GSCommandLogAnomaly, int>{{GSCommandLogAnomaly::CommandWhileBusy, 1}})) << Golden();
    for (const GSCommandLogEntry& e : Entries())
        if (e.kind == Kind::Anomaly && e.value == uint8_t(GSCommandLogAnomaly::CommandWhileBusy))
            EXPECT_EQ(e.command, 0xF4) << e.Text();
    ExpectGolden("v2-gp-cancel");
}

/// V-3: GP plays an MP3 on the NeoGS. GP uploads its streamer (gp/gp.plr's gscode, #14 to #4000) and starts it (#13):
/// the card then talks the streamer's own set (profile nedoos-gp-gscode: #00 back to the ROM, #01 free ring space, #02
/// chip id, #06 stream info). GP asks for the chip id and leaves with #00, then uploads the streamer again for the file
/// and streams it: raw data bytes while #01 says the ring has room, #06 for the stream info, every frame. The data is
/// one host data stream block (hostDataStream, F1a), the polls go on inside it. At the end of the file GP sends 150
/// padding frames, then #00 (gscodereset). Each streamer start sets the card to 10, then 12 MHz, and its #00 to 20
/// (N2, S9); the streamer's IM 2 handler runs on the card's timer. The MP3 is 8 s, 22 kHz mono VBR with an ID3 tag;
/// the chip id is #00, the VS1001-class MA8201 the board carries
TEST_F(GSNedoOsGp_Test, V3GpPlaysAnMp3OnNeoGs)
{
    const fs::path root = TestPathHelper::FindProjectRoot();
    ASSERT_NO_FATAL_FAILURE(
        BootGp(root / "testdata/sound/neogs/mp3/eyeache1-22k-mono-vbr-id3.mp3", "eyeache1.mp3", GSTypeKind::NGS, 0));
    auto* card = dynamic_cast<SoundChip_NeoGS*>(_context->pSoundManager->getGeneralSound());
    ASSERT_NE(card, nullptr);
    const uint64_t switchesAtBoot = card->counters().clockSwitches;

    // The file's streamer start (the second #13), then its #00 ends the play
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return Count(Kind::Command, 0x13) >= 2; }, 3000, 1);
    ASSERT_GE(Count(Kind::Command, 0x13), 2) << Golden() << "\nscreen:\n" << Screen();
    const uint64_t timerAtStart = card->counters().interrupts[0];
    const uint64_t startFrame = _context->emulatorState.frame_counter;
    const size_t streamerAt = IndexOf(0x13, 2);
    _emulator->RunNFrames(100, true);
    EXPECT_EQ(card->cardClockHz(), 12000000u) << "the streamer runs at 12 MHz";
    const auto ended = [&] { return IndexOf(0x00, Count(Kind::Command, 0x00)) > IndexOf(0x13, 2); };
    EmulatorTestHelper::RunUntil(_emulator.get(), ended, 3000, 1);
    ASSERT_TRUE(ended()) << Golden(-1, 0, {0x01, 0x06}) << "\nscreen:\n" << Screen();
    const uint64_t frames = _context->emulatorState.frame_counter - startFrame;
    _emulator->RunNFrames(5, true);

    // Both streamer uploads are GP's gscode
    const std::vector<GSUploadRecord> uploads = _log->Uploads();
    ASSERT_EQ(uploads.size(), 2u);
    for (const GSUploadRecord& u : uploads)
    {
        ASSERT_NE(u.profile, nullptr);
        EXPECT_STREQ(u.profile->id, "nedoos-gp-gscode");
        EXPECT_TRUE(u.started);
    }

    // One stream block: the file and the padding frames (GP's 417-byte frame, 150 of them)
    std::vector<GSCommandLogEntry> streams;
    for (const GSCommandLogEntry& e : Entries())
        if (e.kind == Kind::Block && e.hostData)
            streams.push_back(e);
    ASSERT_EQ(streams.size(), 1u) << Golden(-1, 0, {0x01, 0x06});
    const auto fileSize = static_cast<uint32_t>(fs::file_size(_mod));
    EXPECT_GE(streams[0].count, fileSize + 150u * 417u) << "the file and the padding";
    EXPECT_FALSE(streams[0].flags & GSCommandLogFlags::kTruncated);
    const std::vector<GSCommandLogEntry> entries = Entries();
    for (size_t i = streamerAt; i < entries.size(); i++)
        EXPECT_NE(entries[i].kind, Kind::Data) << "no data entries from the stream: " << entries[i].Text();

    // Played in real time (8 s at 50 frames/s, then the padding), polled every frame
    EXPECT_GT(frames, 400u);
    EXPECT_GE(static_cast<uint64_t>(Count(Kind::Command, 0x01)), frames) << "#01 every frame";
    EXPECT_GE(static_cast<uint64_t>(Count(Kind::Command, 0x06)), frames / 2);

    // Each streamer start: 10, then 12 MHz; each #00: 20. Two starts (the chip id, the file); the timer interrupts
    // drive the streamer
    EXPECT_GE(card->counters().clockSwitches - switchesAtBoot, 6u);
    EXPECT_EQ(card->cardClockHz(), 20000000u);
    EXPECT_GT(card->counters().interrupts[0] - timerAtStart, frames);

    for (const auto& [kind, count] : Anomalies())
        ADD_FAILURE() << "unexpected " << GSCommandLogAnomalyName(kind) << " x" << count << "\n"
                      << Golden(-1, 0, {0x01, 0x06});
    ExpectGolden("v3-gp-mp3", -1, 0, {0x01, 0x06});
}

/// V-4: ngsplay plays an S3M through the NeoTracker BIOS on the NeoGS. It uploads its loader (ngsldr.bin to #5100)
/// and jumps there, uploads the BIOS through it to #0000 (over the ROM's code, not the loader's) and jumps, syncs with
/// the barrier #00, sets page 2 (#E1) and uploads the BIOS's second part (neopg2.bin to #D000). The module goes in by
/// the BIOS's request-driven loader: #E8 with the 256-byte header, then #E9 (what block next) and #EA (its pages)
/// until #E9 says done, #E2 for the samples, #E3 starts it; ngsplay then polls #E6 (profile nedoos-neotracker: only
/// #E4 / #E6 while the player runs). The handshakes differ by command: #14 and #E3 confirmed by the data flag (WD),
/// the others by the command flag (WC). The module is "A New Frontend" (kj_jose, 18336 bytes)
TEST_F(GSNedoOsGp_Test, V4NgsplayLoadsAnS3mThroughTheNeoTrackerBios)
{
    const fs::path root = TestPathHelper::FindProjectRoot();
    _mod = root / "testdata/sound/wildsound/src/kj_jose_-_a_new_frontend.s3m";
    const fs::path card = Card({{"ngsplay.com", root / "testdata/software/nedoos/bin/ngsplay.com"},
                                {"frontend.s3m", _mod}},
                               "ngsplay frontend.s3m");
    ASSERT_NO_FATAL_FAILURE(Boot(card, GSTypeKind::NGS));

    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return Has(Kind::Command, 0xE3); }, 3000, 1);
    ASSERT_TRUE(Has(Kind::Command, 0xE3)) << Golden() << "\nscreen:\n" << Screen();
    _emulator->RunNFrames(200, true);

    // [S] stops it: in PlayCykl the BIOS takes only #E4 / #E6. The release ngsplay's own status poll (#E6 every 32nd
    // pass of its loop) never ran here in 3,000 frames while its loop took keys, so the stop is what this checks
    _context->pKeyboard->ApplyPcKey(PcKey::S, true);
    _emulator->RunNFrames(10, true);
    _context->pKeyboard->ApplyPcKey(PcKey::S, false);
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return Has(Kind::CardTook, 0xE4); }, 100, 1);
    EXPECT_TRUE(Has(Kind::CardTook, 0xE4)) << "screen:\n" << Screen();

    // Three uploads, each its profile: the loader, the BIOS over #0000, its second part in page 2
    const std::vector<GSUploadRecord> uploads = _log->Uploads();
    ASSERT_EQ(uploads.size(), 3u) << Golden();
    const std::vector<std::tuple<uint16_t, uint32_t, const char*>> expected = {
        {0x5100, 143, "nedoos-ngsldr"}, {0x0000, 12303, "nedoos-neotracker"}, {0xD000, 7272, "nedoos-neopg2"}};
    for (size_t i = 0; i < uploads.size(); i++)
    {
        const auto& [address, length, id] = expected[i];
        EXPECT_EQ(uploads[i].address, address) << i;
        EXPECT_EQ(uploads[i].length, length) << i;
        ASSERT_NE(uploads[i].profile, nullptr) << i;
        EXPECT_STREQ(uploads[i].profile->id, id) << i;
    }
    EXPECT_TRUE(uploads[0].started && uploads[1].started) << "the loader and the BIOS are jumped into";
    EXPECT_EQ(Count(Kind::Command, 0x13), 2);
    EXPECT_GE(Count(Kind::Command, 0x00), 2) << "the barrier after the BIOS start and before the load";

    // The request-driven loader: the header as one block, then #E9 / #EA until #E9's type says done
    std::vector<uint32_t> header, pages;
    for (const GSCommandLogEntry& e : Entries())
        if (e.kind == Kind::Block && (e.command == 0xE8 || e.command == 0xEA))
        {
            (e.command == 0xE8 ? header : pages).push_back(e.count);
            EXPECT_FALSE(e.flags & GSCommandLogFlags::kTruncated) << e.Text();
        }
    EXPECT_EQ(header, std::vector<uint32_t>{256});
    ASSERT_FALSE(pages.empty());
    EXPECT_EQ(static_cast<int>(pages.size()), Count(Kind::Command, 0xE9) - 1) << "an #EA per #E9 but the last";
    const uint32_t loaded = std::accumulate(pages.begin(), pages.end(), 0u);
    EXPECT_GT(loaded + 256, static_cast<uint32_t>(fs::file_size(_mod)) / 2) << "the module's patterns and samples";

    // WD-confirmed #E3 and #14, WC-confirmed rest: no flags
    for (const auto& [kind, count] : Anomalies())
        ADD_FAILURE() << "unexpected " << GSCommandLogAnomalyName(kind) << " x" << count;
    ExpectGolden("v4-ngsplay-s3m");
}


/// V-5: the kernel's NeoGS SD driver. Only the Pentagon 2.666 SD build (`osp26sd.$C`, NGSSD, atm=3) uploads it: the
/// kernel asks #23, uploads the 768-byte installer to #5B00 (#14), jumps there (#13); the installer patches the
/// firmware's COMTAB[#1E] in RAM and replies #77; the kernel then drives sector I/O through #1E. The board's NeoGS
/// (none by default: its ZX-BUS is empty) carries a second SD card, which NedoOS mounts as a drive
/// V-6: Moon Rabbit (mrfue, the ZX-Evo UART build with GS support) streams a MOD from a gopher server straight into
/// the stock firmware (drivers/general-sound.asm): #F3, #30, #D1, then every byte of the network's packets with WD,
/// #D2, data 1, #31, then #60 from the play loop (player/mod-processor.asm: HALT, the key read, the clock, #60 to see the
/// song wrap; about every other frame). The ESP on the AVR's COM port has the AT firmware (ComPort=AT); behind the
/// virtual network a scripted gopher server answers the selector with the module, so the run is the same every time.
/// The ESP passes the module at 115200 baud: the #D1 stream stays open for hundreds of frames (F1a: still one block).
/// The home page (bin/browser/index.gph) is a menu with the module's row first: Enter opens it (a .mod row is
/// Gopher.loadMod, gopher/render/row.asm)
class GSNedoOsMoonRabbit_Test : public GSNedoOsGp_Test
{
protected:
    static constexpr uint32_t kServer = NetIp(192, 0, 2, 10);

    void BootMoonRabbit(GSTypeKind kind, unsigned gsRamKB)
    {
        const fs::path root = TestPathHelper::FindProjectRoot();
        const fs::path bin = root / "testdata/machines/p2666/software/nedoos/sdcard/bin";
        _mod = root / "testdata/software/nedoos/media/afterwr2.mod";
        const fs::path page = fs::path(TestPathHelper::GetUniqueTestScratchPath("moon-rabbit-home")) / "index.gph";
        fs::create_directories(page.parent_path());
        std::ofstream(page, std::ios::binary) << "9afterwr2.mod\t/afterwr2.mod\t192.0.2.10\t70\r\n"
                                                 "iThe module streams into the GS\t-\t-\t-\r\n";
        const fs::path card = Card({{"mrfue.com", bin / "mrfue.com"},
                                    {"browser/auth.pwd", bin / "browser/auth.pwd"},
                                    {"browser/index.gph", page}},
                                   "mrfue");
        _comPort = "AT";
        _onCreate = [this] {
            ASSERT_NE(_context->pVirtualNetwork, nullptr) << "ComPort=AT brings the virtual network";
            auto host = std::make_unique<ScriptedHostNet>();
            std::ifstream in(_mod, std::ios::binary);
            host->AddGopher({kServer, 70}, {{"/afterwr2.mod", std::string{std::istreambuf_iterator<char>(in),
                                                                          std::istreambuf_iterator<char>()}}});
            _context->pVirtualNetwork->ReplaceHost(std::move(host));
        };
        // The ESP's power-up lines ("ready", WIFI CONNECTED, WIFI GOT IP) wait for RTS: the AVR holds it off from its
        // start, and Moon Rabbit's init raises it after its FIFO reset, so the 38 bytes would flood the 16-byte FIFO
        // while it sends ATE0 and its OK would be lost (it does not resync). A session before Moon Rabbit takes them,
        // as a terminal does: RTS up until the ESP is done, then the FIFO reset
        _atMenu = [this] {
            const auto pending = [&] {
                const StateNode net = DeviceState::Network(_context);
                const StateNode* com = net.find("com_port");
                const StateNode* n = com ? com->find("peer_pending") : nullptr;
                return n ? n->i : -1;
            };
            EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return pending() >= 38; }, 300, 5);
            ASSERT_GE(pending(), 38) << "the ESP's power-up lines";
            Z80* z80 = _context->pCore->GetZ80();
            for (int i = 0; i < 100 && pending() != 0; ++i)
            {
                z80->out(0xFCEF, 0x02);   // MCR: RTS
                z80->in(0xF8EF);          // RBR: one byte out of the FIFO
                _emulator->RunNFrames(1, true);
            }
            ASSERT_EQ(pending(), 0);
            z80->out(0xFCEF, 0x00);
            z80->out(0xFAEF, 0x87);       // FCR: FIFO on, both reset
        };
        ASSERT_NO_FATAL_FAILURE(Boot(card, kind, gsRamKB));
    }

    /// The frames between the first #D1 and the first #D2
    uint32_t StreamFrames() const
    {
        uint32_t open = 0;
        for (const GSCommandLogEntry& e : Entries())
        {
            if (e.kind != Kind::Command)
                continue;
            if (e.command == 0xD1 && !open)
                open = e.frame ? e.frame : 1;
            if (e.command == 0xD2 && open)
                return e.frame - open;
        }
        return 0;
    }
};

TEST_F(GSNedoOsMoonRabbit_Test, V6MoonRabbitStreamsAModFromTheNetwork)
{
    ASSERT_NO_FATAL_FAILURE(BootMoonRabbit(GSTypeKind::Z80, 128));
    // Moon Rabbit shows its home page (the module's row, then the info line)
    const auto home = [&] { return Screen().find("The module streams") != std::string::npos; };
    EmulatorTestHelper::RunUntil(_emulator.get(), home, 3000, 25);
    ASSERT_TRUE(home()) << "screen:\n" << Screen() << "\nnetwork:\n" << StateNodeToJsonText(DeviceState::Network(_context));
    _emulator->RunNFrames(25, true);
    _context->pKeyboard->ApplyPcKey(PcKey::Enter, true);
    _emulator->RunNFrames(4, true);
    _context->pKeyboard->ApplyPcKey(PcKey::Enter, false);

    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return Has(Kind::Command, 0x31); }, 3000, 25);
    ASSERT_TRUE(Has(Kind::Command, 0x31)) << Golden() << "\nscreen:\n" << Screen() << Frames();
    const int pollsBefore = Count(Kind::Command, 0x60);
    _emulator->RunNFrames(100, true);
    EXPECT_GE(Count(Kind::Command, 0x60) - pollsBefore, 40) << "the play loop polls the position every other frame";

    // The module is one block of its whole size over the network's packets and the UART's pace (F1a, F1c)
    EXPECT_EQ(ModuleBlocks(), std::vector<uint32_t>{static_cast<uint32_t>(fs::file_size(_mod))}) << Golden(0x60, 3);
    EXPECT_GE(StreamFrames(), 100u) << "the #D1 stream stays open while the ESP passes the module";
    ExpectNoAnomalies(0x60, 3);
    ExpectGolden("v6-moon-rabbit-mod", 0x60, 3);
}

class GSNedoOsP2666_Test : public GSNedoOsScenarios_Test
{
protected:
    GeneralSoundFitScope _fit{GSTypeKind::NGS};  // the P2666's ZX-BUS is empty: the card fitted at creation

    /// @p kernel replaces the card's SD_BOOT.$C (a path under the project root) when not empty
    void BootP2666(const fs::path& ngsSd, const std::string& command, const fs::path& kernel = {})
    {
        const fs::path root = TestPathHelper::FindProjectRoot();
        const fs::path card = TestPathHelper::GetUniqueTestScratchPath("nedoos-p2666-card");
        std::error_code ec;
        fs::remove_all(card, ec);
        fs::create_directories(card);
        fs::copy(root / "testdata/machines/p2666/software/nedoos/sdcard", card, fs::copy_options::recursive, ec);
        ASSERT_FALSE(ec) << ec.message();
        std::ofstream(card / "bin/autoexec.bat", std::ios::binary) << command << "\r\n";
        if (!kernel.empty())
        {
            fs::copy_file(root / kernel, card / "SD_BOOT.$C", fs::copy_options::overwrite_existing, ec);
            ASSERT_FALSE(ec) << ec.message();
        }

        _emulator = _manager->CreateEmulatorWithModel("p2666-ngs", "P2666", LoggerLevel::LogError, nullptr,
                                                      Config::RamPowerOnOverride(RamPowerOn::Zero));
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _emulator->EnableTurboMode();
        ASSERT_TRUE(FitNeoGSWithSd(_context, Utf8(ngsSd))) << "a NeoGS with an SD card on the P2666's ZX-BUS";
        _log = _context->pSoundManager->gsCommandLog();
        ASSERT_NE(_log, nullptr);
        std::string error;
        ASSERT_TRUE(_log->History().Configure({.mode = history::Mode::Capture}, error)) << error;

        MediaSource source;
        source.path = Utf8(card);
        InsertOptions options;
        options.freeBytes = 16 * 1024 * 1024;
        const MediaResult inserted = _context->pMediaManager->Insert("sd.zc", source, options);
        ASSERT_TRUE(inserted.Ok()) << inserted.message;

        _emulator->RunNFrames(50, true);  // the Gluk menu, then "SD boot" (key 5)
        _context->pKeyboard->PressKey(ZXKEY_5);
        _emulator->RunNFrames(5, true);
        _context->pKeyboard->ReleaseKey(ZXKEY_5);
    }
};


// The release kernel hangs here, on the real board as well: its NGS SD driver sends with LD BC,GSDAT : OUTI, B
// counts down on A15..A8 and #E0B3..#EFB3 also write the board's #EFF7 (RAM 0 at #0000 after the installer). The
// test boots the same kernel rebuilt with the fix (testdata/machines/p2666/software/README.md, "The NGS SD driver fix")
TEST_F(GSNedoOsP2666_Test, V5KernelDrivesTheNeoGsSdCard)
{
    const fs::path ngsSd = TestPathHelper::GetUniqueTestScratchPath("nedoos-ngs-sd");
    std::error_code ec;
    fs::remove_all(ngsSd, ec);
    fs::create_directories(ngsSd);
    std::ofstream(ngsSd / "ngssd.txt", std::ios::binary) << "UNREALNGSSDFILE\r\n";
    ASSERT_NO_FATAL_FAILURE(BootP2666(ngsSd, "type n:/ngssd.txt\r\necho UNREALNGSDBOOT",
                                      "testdata/machines/p2666/software/nedoos/kernel-ngsfix/osp26sd-ngsfix.$C"));

    // `type` prints the file the kernel read from the NeoGS card (the autoexec's echo spells another word)
    const auto printed = [&] { return Screen().find("UNREALNGSSDFILE") != std::string::npos; };
    EmulatorTestHelper::RunUntil(_emulator.get(), printed, 1500, 20);
    ASSERT_TRUE(printed()) << Golden() << "\nscreen:\n" << Screen();
    ASSERT_GT(Count(Kind::Command, 0x1E), 3) << Golden();
    for (const auto& [kind, count] : Anomalies())
        ADD_FAILURE() << "unexpected " << GSCommandLogAnomalyName(kind) << " x" << count;
    ExpectGolden("v5-kernel-ngssd");
}
