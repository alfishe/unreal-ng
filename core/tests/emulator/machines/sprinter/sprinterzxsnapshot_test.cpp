// The Sprinter's snapshot commit (snapshot pipeline P4 = Sprinter phase Z5, tests T-ZX-11 and T-ZX-12 of
// tdd-zx-mode.md): a Spectrum snapshot goes into the running Spectrum mode through the PLD cell table, and is refused
// where the Sprinter would put it into its own system pages (the DSS prompt, a 48K mode, a missing page).
//
// The Spectrum mode is BIOS 3.06's own (ESC at the boot prompt: the 128 menu), reached the way sprinterzxtiming_test.cpp
// reaches it - no hard disk needed. Boot-bound (BIOS POST, the prompt, the menu): ~1 s host time with the turbo mode.

#include "stdafx.h"
#include "pch.h"

#include "sprinterzxsession.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

#include "emulator/memory/sprinter/sprintermemory.h"
#include "emulator/ports/models/sprinter/sprinterzxsnapshot.h"
#include "loaders/snapshot/loader_sna.h"
#include "loaders/snapshot/machinestatetransfer.h"
#include "loaders/snapshot/snapshotcapture.h"
#include "loaders/snapshot/snapshotpipeline.h"
#include "loaders/snapshot/snapshotpolicy.h"

namespace
{
std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

uint64_t Fnv(const uint8_t* data, size_t size)
{
    uint64_t h = 14695981038346656037ull;
    for (size_t i = 0; i < size; ++i)
    {
        h ^= data[i];
        h *= 1099511628211ull;
    }
    return h;
}

/// Every RAM page except `skip`, in one hash: what a snapshot must leave exactly as it was. The cell table decides where
/// the Spectrum banks live (BIOS 3.06's ZX mode: banks 0 / 2 / 5 in physical pages 0 / 2 / 5, bank 1 in #ED, 3 in #EF ...),
/// so "the system pages" is not a fixed range
uint64_t RamHashExcept(Memory& memory, const std::set<uint8_t>& skip)
{
    uint64_t h = 0;
    for (unsigned page = 0; page < 256; ++page)
    {
        if (!skip.count(static_cast<uint8_t>(page)))
            h = h * 31 + Fnv(memory.RAMPageAddress(static_cast<uint16_t>(page)), PAGE_SIZE);
    }
    return h;
}

/// The physical pages the cells #F0-#F7 name (Spectrum banks 0-7) and the window cells #E9 / #EA
std::set<uint8_t> SpectrumPages(const SprinterPldState& pld)
{
    std::set<uint8_t> pages;
    for (uint8_t cell = 0xF0; cell <= 0xF7; ++cell)
        pages.insert(pld.Cell(cell));
    pages.insert(pld.Cell(SprinterCode::Page1));
    pages.insert(pld.Cell(SprinterCode::Page2));
    return pages;
}

const char* kAction = "loaders/sna/action.sna";   // a 128K SNA: #7FFD = #19 (bank 1 on top, ROM 1, screen normal)
}  // namespace

class SprinterZxSnapshot_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;

    void SetUp() override
    {
        const std::string bios = (TestPathHelper::FindProjectRoot() / "data" / "rom" / "sprinter" / "sp2k-3.06-hf2.rom").string();
        if (!FileHelper::FileExists(bios))
            GTEST_SKIP() << "data/rom/sprinter/sp2k-3.06-hf2.rom not found";
        _manager = EmulatorManager::GetInstance();
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
        _emulator = _manager->CreateEmulatorWithModelAndRAM("sprinter-zxsnap", "SPRINTER", 4096, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        _decoder->GetRtc().SetFixedTime(1767268830);
        CONFIG& config = _context->config;
        std::memset(config.sprinter_rom_path, 0, sizeof(config.sprinter_rom_path));
        std::strncpy(config.sprinter_rom_path, bios.c_str(), sizeof(config.sprinter_rom_path) - 1);
        ASSERT_TRUE(_context->pCore->GetROM()->LoadROM());
        config.sprinter.fast_start = 1;
        _emulator->Reset();
        _emulator->EnableTurboMode();
    }

    void TearDown() override
    {
        _emulator.reset();
        if (_manager)
        {
            for (const auto& id : _manager->GetEmulatorIds())
                _manager->RemoveEmulator(id);
        }
    }

    bool SpectrumHas(const std::string& text) { return ScreenOCR::containsText(_emulator->GetId(), text); }

    /// The BIOS prompt on screen: "<ESC> TO ZX-MODE"
    bool PromptShown()
    {
        const SprinterVideoRam& vram = _decoder->GetVideoRam();
        std::string text;
        for (uint8_t page = 0; page < 2; page++)
            for (uint8_t b = 0; b < 32; b++)
                for (uint8_t a = 0; a < 40; a++)
                    for (uint8_t half = 0; half < 2; half++)
                    {
                        const uint8_t c = vram.Read((1u + 2u * a + half + 0x80u * page) * 1024u + 0x301 + 4u * b);
                        text.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ');
                    }
        return text.find("<ESC> TO ZX-MODE") != std::string::npos;
    }

    void ToThePrompt()
    {
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return PromptShown(); }, 1200, 5);
        ASSERT_TRUE(PromptShown());
    }

    /// ESC at the prompt: BIOS 3.06's own ZX mode, the 128 menu on the Spectrum screen
    void ToTheZxMenu()
    {
        ToThePrompt();
        _emulator->GetDebugManager()->GetKeyboardManager()->TapKey("pc.esc", 3);
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 10);
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return SpectrumHas("TR-DOS"); }, 600, 10);
        ASSERT_TRUE(SpectrumHas("TR-DOS")) << ScreenOCR::ocrScreen(_emulator->GetId());
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 30);
    }
};

// T-ZX-12: at the BIOS prompt the Sprinter is not in a Spectrum mode (there is no Spectrum memory to load into). The
// snapshot is refused with the reason and what to do, and not one RAM byte changes (the legacy commit would have
// written the physical pages 0-7)
TEST_F(SprinterZxSnapshot_Test, RefusedAtThePromptAndTheSystemPagesAreUntouched)
{
    ToThePrompt();
    const uint64_t before = RamHashExcept(*_context->pMemory, {});

    EXPECT_FALSE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(kAction)));
    const snapshot::Report& report = _emulator->LastSnapshotReport();
    EXPECT_TRUE(report.refused);
    EXPECT_EQ(report.needs, "zx_mode");
    EXPECT_EQ(report.commit, "sprinter-zx");
    EXPECT_NE(report.reason.find("not in the Spectrum"), std::string::npos) << report.reason;
    EXPECT_NE(report.reason.find("ESC"), std::string::npos) << "it says how to get into a mode";
    EXPECT_EQ(RamHashExcept(*_context->pMemory, {}), before) << "not one RAM byte was written";

    // Every format is refused the same way
    for (const char* file : {"loaders/z80/dizzyx.z80", "loaders/szx/libspectrum/synth-128.szx"})
    {
        EXPECT_FALSE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(file))) << file;
        EXPECT_EQ(_emulator->LastSnapshotReport().needs, "zx_mode") << file;
    }

    // inspect says so before a load is tried
    StateNode inspected;
    std::string error;
    ASSERT_TRUE(_emulator->InspectSnapshot(TestPathHelper::GetTestDataPath(kAction), {}, inspected, error)) << error;
    EXPECT_FALSE(inspected.find("would_load")->b);
    EXPECT_EQ(inspected.find("would_commit")->s, "sprinter-zx");
    EXPECT_EQ(inspected.find("plan")->find("needs")->s, "zx_mode");
}

// The escape hatch: the caller can still ask for the legacy commit (an owner who knows what they do)
TEST_F(SprinterZxSnapshot_Test, TheCallerCanStillAskForTheLegacyCommit)
{
    ToThePrompt();
    snapshot::Options legacy;
    legacy.commit = "legacy";
    EXPECT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(kAction), {}, legacy));
    EXPECT_EQ(_emulator->LastSnapshotReport().commit, "legacy");
}

// T-ZX-11: in the Spectrum mode the banks go into the pages the cells name, #7FFD takes effect through the PLD, the
// system pages stay as they were, and the Spectrum screen shadow follows bank 5
TEST_F(SprinterZxSnapshot_Test, ASnaGoesIntoTheCellTable)
{
    ToTheZxMenu();
    Memory& memory = *_context->pMemory;
    const SprinterPldState& pld = _decoder->GetPldState();
    const std::set<uint8_t> spectrum = SpectrumPages(pld);
    ASSERT_EQ(spectrum.size(), 8u) << "the mode gives each of the 8 Spectrum banks its own page (window cells repeat banks 5 and 2)";
    const uint64_t restBefore = RamHashExcept(memory, spectrum);

    ASSERT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(kAction)));
    const snapshot::Report& report = _emulator->LastSnapshotReport();
    EXPECT_EQ(report.commit, "sprinter-zx");
    EXPECT_FALSE(report.refused);
    EXPECT_EQ(report.format, "sna");
    EXPECT_EQ(RamHashExcept(memory, spectrum), restBefore) << "no page outside the cell table's eight was written";

    // The oracle is the file itself: bank n is slice 0 (5), 1 (2), 2 (the paged bank, 1 here), then the others ascending
    const std::vector<uint8_t> f = ReadFile(TestPathHelper::GetTestDataPath(kAction));
    ASSERT_EQ(f.size(), 131103u);
    const uint8_t p7ffd = f[49181];
    ASSERT_EQ(p7ffd, 0x19);
    std::map<uint16_t, size_t> at = {{5, 27}, {2, 27 + 16384}, {static_cast<uint16_t>(p7ffd & 7), 27 + 2 * 16384}};
    size_t next = 49183;
    for (uint16_t bank = 0; bank < 8; ++bank)
        if (!at.count(bank))
        {
            at[bank] = next;
            next += 16384;
        }
    for (const auto& [bank, offset] : at)
    {
        const uint8_t cell = static_cast<uint8_t>(0xF0 + bank);
        const uint8_t page = pld.Cell(cell);
        EXPECT_EQ(Fnv(memory.RAMPageAddress(page), PAGE_SIZE), Fnv(&f[offset], 16384))
            << "bank " << bank << " in physical page #" << std::hex << int(page) << " (cell #" << int(cell) << ")";
    }

    // #7FFD took effect through the PLD: window 3 shows the paged bank's page
    EXPECT_EQ(pld.pn & 0x07, p7ffd & 0x07);
    EXPECT_EQ(memory.GetRAMPageForBank(3), pld.Cell(static_cast<uint8_t>(0xF0 + (p7ffd & 7))));
    EXPECT_EQ(_context->pCore->GetZ80()->pc, static_cast<uint16_t>(f[49179] | f[49180] << 8));

    // The Spectrum screen shadow in video RAM equals what a CPU writing bank 5 would have left
    const SprinterVideoRam& vram = _decoder->GetVideoRam();
    const std::vector<uint8_t> bank5(f.begin() + 27, f.begin() + 27 + 16384);
    size_t checked = 0;
    for (uint16_t offset = 0; offset < 0x1B00; ++offset)   // the bitmap and the attributes (#4000-#5AFF)
    {
        const uint16_t address = static_cast<uint16_t>(0x4000 + offset);
        ASSERT_EQ(vram.Read(SprinterMemory::ZxShadowAddress(address, pld.portY, pld.pg3)), bank5[offset])
            << "screen byte #" << std::hex << address;
        ++checked;
    }
    EXPECT_EQ(checked, 0x1B00u);

    // And the machine runs on from there
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 20);
    EXPECT_TRUE(_decoder->GetPldState().configState == SprinterConfigState::Configured);
    EXPECT_FALSE(_emulator->LastSnapshotReport().refused);
}

// Z80 and SZX go the same road, and a 48K snapshot leaves the paging the way a 128K runs a 48K program
TEST_F(SprinterZxSnapshot_Test, Z80AndSzxAndA48kSnapshot)
{
    ToTheZxMenu();
    Memory& memory = *_context->pMemory;
    const SprinterPldState& pld = _decoder->GetPldState();
    const uint64_t restBefore = RamHashExcept(memory, SpectrumPages(pld));

    ASSERT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/z80/dizzyx.z80")));
    EXPECT_EQ(_emulator->LastSnapshotReport().commit, "sprinter-zx");
    EXPECT_EQ(_emulator->LastSnapshotReport().format, "z80");
    EXPECT_EQ(pld.pn & 0x37, 0x10) << "dizzyx.z80: #7FFD #10 (bank 0, ROM 1, unlocked)";

    ASSERT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/szx/libspectrum/synth-128.szx")));
    EXPECT_EQ(_emulator->LastSnapshotReport().commit, "sprinter-zx");
    EXPECT_EQ(_emulator->LastSnapshotReport().format, "szx");

    ASSERT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/z80full.sna")));
    EXPECT_EQ(_emulator->LastSnapshotReport().commit, "sprinter-zx");
    EXPECT_EQ(pld.pn & 0x37, 0x30) << "a 48K snapshot: 48K ROM, bank 0 on top, paging locked";

    EXPECT_EQ(RamHashExcept(memory, SpectrumPages(pld)), restBefore) << "none of them wrote outside the cell table's pages";
}

// A mode without #7FFD paging (a 48K mode) cannot hold a 128K snapshot: refused with the way out, a 48K one is fine.
// The mode is made by setting the CNF bit the 48K launcher modes set (the PLD's clean rule), not by booting one
TEST_F(SprinterZxSnapshot_Test, A128kSnapshotIsRefusedWhereTheModeHasNo7ffdPaging)
{
    ToTheZxMenu();
    SprinterPldState& pld = _decoder->GetPldState();
    pld.cnf = static_cast<uint8_t>(pld.cnf | 0x20);   // CNF bit 5: #7FFD clean (off)
    const std::set<uint8_t> spectrum = SpectrumPages(pld);
    const uint64_t restBefore = RamHashExcept(*_context->pMemory, spectrum);

    EXPECT_FALSE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(kAction)));
    EXPECT_EQ(_emulator->LastSnapshotReport().needs, "mode:128k");
    EXPECT_NE(_emulator->LastSnapshotReport().reason.find("p128.zx"), std::string::npos) << "it names a mode that would work";

    EXPECT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/z80full.sna")));
    EXPECT_EQ(_emulator->LastSnapshotReport().commit, "sprinter-zx");
    EXPECT_EQ(RamHashExcept(*_context->pMemory, spectrum), restBefore);
}

// A bank whose cell points at the port table or the launcher's marker page is not RAM for programs: refused
TEST_F(SprinterZxSnapshot_Test, ABankWithoutARamPageIsRefused)
{
    ToTheZxMenu();
    SprinterPldState& pld = _decoder->GetPldState();
    pld.Cell(0xF3) = SprinterMemory::kPortTablePage;
    EXPECT_FALSE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(kAction)));
    EXPECT_EQ(_emulator->LastSnapshotReport().needs, "mode:page_table");
    EXPECT_NE(_emulator->LastSnapshotReport().reason.find("bank 3"), std::string::npos);
}

// The name works on its own, and on a machine that is not a Sprinter it says so
TEST(SprinterZxSnapshotName_Test, TheCommitNameIsKnownAndRefusesOtherMachines)
{
    EXPECT_NE(snapshot::SnapshotPolicies::Find("sprinter-zx"), nullptr);
    const std::vector<std::string> names = snapshot::SnapshotPolicies::Names();
    EXPECT_NE(std::find(names.begin(), names.end(), "sprinter-zx"), names.end());

    Emulator* pentagon = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(pentagon, nullptr);
    snapshot::Options options;
    options.commit = "sprinter-zx";
    EXPECT_FALSE(pentagon->LoadSnapshot(TestPathHelper::GetTestDataPath(kAction), {}, options));
    EXPECT_TRUE(pentagon->LastSnapshotReport().refused);
    EXPECT_EQ(pentagon->LastSnapshotReport().needs, "model:SPRINTER");
    EmulatorTestHelper::CleanupEmulator(pentagon);
}

// ---------------------------------------------------------------------------------------------------------------------
// The save side (snapshot pipeline P6): the Sprinter shows a snapshot the Spectrum machine its mode made it, banks read
// through the PLD cells in Spectrum order
// ---------------------------------------------------------------------------------------------------------------------

// At the BIOS prompt there is no Spectrum memory: every format is refused with the way out, nothing is written
TEST_F(SprinterZxSnapshot_Test, SaveIsRefusedWhereThereIsNoZxMode)
{
    ToThePrompt();
    const snapshot::SaveFormats formats = _emulator->SnapshotSaveFormats();
    EXPECT_FALSE(formats.viewAvailable);
    for (const snapshot::FormatStatus& status : formats.formats)
    {
        EXPECT_FALSE(status.available) << snapshot::ToText(status.format);
        EXPECT_EQ(status.needs, "zx_mode");
        EXPECT_NE(status.reason.find("ESC"), std::string::npos) << "it says how to get into a mode: " << status.reason;
    }
    for (const char* extension : {"sna", "z80", "szx"})
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(std::string("sprinter-save.") + extension);
        EXPECT_FALSE(_emulator->SaveSnapshot(path)) << extension;
        EXPECT_FALSE(std::ifstream(path).good()) << "a refusal writes nothing";
        EXPECT_EQ(_emulator->LastSaveResult().needs, "zx_mode");
    }
}

// In a ZX mode the saved file holds the Spectrum banks in the Spectrum's order: a .sna written right after loading a .sna
// has the same bank sections, #7FFD and PC; the other formats restore on a plain 128K machine, and back on the Sprinter
TEST_F(SprinterZxSnapshot_Test, ASavedSnapshotRestoresOnAnotherMachineAndBack)
{
    ToTheZxMenu();
    Memory& memory = *_context->pMemory;
    const SprinterPldState& pld = _decoder->GetPldState();
    ASSERT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(kAction)));

    const snapshot::SaveFormats formats = _emulator->SnapshotSaveFormats();
    ASSERT_TRUE(formats.viewAvailable) << formats.view;
    EXPECT_NE(formats.view.find("PLD cell table"), std::string::npos) << formats.view;
    for (const snapshot::FormatStatus& status : formats.formats)
        EXPECT_TRUE(status.available) << snapshot::ToText(status.format) << ": " << status.reason;

    // The same file, banks in the order the format keeps them (5, 2, the paged one, then ascending)
    const std::string sna = TestPathHelper::GetUniqueTestScratchPath("sprinter-save.sna");
    ASSERT_TRUE(_emulator->SaveSnapshot(sna)) << _emulator->LastSaveResult().text;
    const std::vector<uint8_t> saved = ReadFile(sna);
    const std::vector<uint8_t> source = ReadFile(TestPathHelper::GetTestDataPath(kAction));
    ASSERT_EQ(saved.size(), source.size());
    EXPECT_TRUE(std::equal(saved.begin() + 27, saved.begin() + 49179, source.begin() + 27)) << "banks 5, 2 and the paged one";
    EXPECT_EQ(saved[49181], source[49181]) << "#7FFD";
    EXPECT_EQ(saved[49179], source[49179]) << "PC low";
    EXPECT_EQ(saved[49180], source[49180]) << "PC high";
    EXPECT_TRUE(std::equal(saved.begin() + 49183, saved.end(), source.begin() + 49183)) << "the other five banks, ascending";

    // The other formats: onto a plain machine of the kind the file names, and back
    const bool pentagon = formats.machine == "Pentagon 128";
    for (const char* extension : {"z80", "szx"})
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(std::string("sprinter-save.") + extension);
        ASSERT_TRUE(_emulator->SaveSnapshot(path)) << extension << ": " << _emulator->LastSaveResult().text;

        Emulator* plain = EmulatorTestHelper::CreateStandardEmulator(pentagon ? "PENTAGON" : "128k", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(plain, nullptr);
        ASSERT_TRUE(plain->LoadSnapshot(path)) << extension;
        for (uint16_t bank = 0; bank < 8; bank++)
        {
            const uint8_t page = pld.Cell(static_cast<uint8_t>(0xF0 + bank));
            EXPECT_EQ(0, std::memcmp(memory.RAMPageAddress(page), plain->GetContext()->pMemory->RAMPageAddress(bank), PAGE_SIZE))
                << extension << " bank " << bank << " (physical page #" << std::hex << int(page) << ")";
        }
        EXPECT_EQ(plain->GetContext()->emulatorState.p7FFD, pld.pn & 0x3F) << extension;
        EmulatorTestHelper::CleanupEmulator(plain);

        // Back: scribble over the banks, load the file, the banks are as saved
        std::vector<std::vector<uint8_t>> before;
        for (uint16_t bank = 0; bank < 8; bank++)
        {
            uint8_t* bytes = memory.RAMPageAddress(pld.Cell(static_cast<uint8_t>(0xF0 + bank)));
            before.emplace_back(bytes, bytes + PAGE_SIZE);
            std::memset(bytes, 0xA5, PAGE_SIZE);
        }
        ASSERT_TRUE(_emulator->LoadSnapshot(path)) << extension << " back onto the Sprinter";
        for (uint16_t bank = 0; bank < 8; bank++)
            EXPECT_EQ(0, std::memcmp(memory.RAMPageAddress(pld.Cell(static_cast<uint8_t>(0xF0 + bank))), before[bank].data(), PAGE_SIZE))
                << extension << " bank " << bank;
        std::remove(path.c_str());
    }
    std::remove(sna.c_str());
}

// The mode's name decides what the file says the machine is
TEST(SprinterZxCaptureIdentity_Test, TheLauncherModeNamesTheMachine)
{
    using Capture = SprinterZxCapture;
    EXPECT_EQ(Capture::IdentityOf("Sprinter ZX", true).machineHint, "128k");
    EXPECT_EQ(Capture::IdentityOf("Default (Sprinter ZX)", true).machineHint, "128k");
    EXPECT_EQ(Capture::IdentityOf("Original ZX Spectrum", true).machineHint, "128k") << "ORIGIN.ZX keeps #7FFD: a 128K";
    EXPECT_EQ(Capture::IdentityOf("", true).machineHint, "128k") << "no name: a 128K";

    const Capture::Identity pentagon = Capture::IdentityOf("Pentagon 128", true);
    EXPECT_EQ(pentagon.machineHint, "pentagon128");
    EXPECT_EQ(pentagon.model, MM_PENTAGON);
    EXPECT_EQ(pentagon.timingHint, "pentagon");

    const Capture::Identity scorpion = Capture::IdentityOf("Scorpion 256", true);
    EXPECT_EQ(scorpion.machineHint, "scorpion256");
    EXPECT_EQ(scorpion.bankCount, 16);
    EXPECT_TRUE(scorpion.scorpion);

    const Capture::Identity none = Capture::IdentityOf("Pentagon 128", false);
    EXPECT_TRUE(none.layout48) << "a mode without #7FFD paging is a 48K";
    EXPECT_EQ(none.machineHint, "48k");
    EXPECT_EQ(none.bankCount, 3);
}

// A 512 KB mode (CNF bit 7: P512.ZX, PENT512.ZX) is a Pentagon 512: banks 0-31, the banks 16-31 behind the cells #D0-#DF. Only an
// .szx holds 32 banks; the file restores on a Pentagon 512 and keeps every bank
TEST_F(SprinterZxSnapshot_Test, A512kModeIsAPentagon512SavedAsSzx)
{
    ToTheZxMenu();
    Memory& memory = *_context->pMemory;
    SprinterPldState& pld = _decoder->GetPldState();
    pld.cnf = static_cast<uint8_t>(pld.cnf | 0x80);   // the 512 KB paging
    // Give banks 16-31 their own pages (the launcher of a real mode allocates them), a pattern in every bank
    for (uint16_t bank = 0; bank < 32; bank++)
    {
        const uint8_t cell = bank < 8 ? 0xF0 + bank : (bank < 16 ? 0xF8 + (bank - 8) : (bank < 24 ? 0xD0 + (bank - 16) : 0xD8 + (bank - 24)));
        if (bank >= 16)
            pld.Cell(static_cast<uint8_t>(cell)) = static_cast<uint8_t>(0xE0 + (bank - 16));
        uint8_t* bytes = memory.RAMPageAddress(pld.Cell(static_cast<uint8_t>(cell)));
        for (uint32_t i = 0; i < PAGE_SIZE; i++)
            bytes[i] = static_cast<uint8_t>((i * 5 + bank * 29) & 0xFF);
    }
    pld.pn = 0xD5;   // bank 5 | bit 6 -> +8 | bit 7 -> +16 = bank 29, screen normal

    const snapshot::SaveFormats formats = _emulator->SnapshotSaveFormats();
    ASSERT_TRUE(formats.viewAvailable) << formats.view;
    EXPECT_EQ(formats.machine, "Pentagon 512");
    EXPECT_FALSE(formats.For(snapshot::SaveFormat::Sna).available);
    EXPECT_FALSE(formats.For(snapshot::SaveFormat::Z80).available);
    EXPECT_EQ(formats.For(snapshot::SaveFormat::Sna).needs, "format:szx");
    ASSERT_TRUE(formats.For(snapshot::SaveFormat::Szx).available);

    const std::string path = TestPathHelper::GetUniqueTestScratchPath("sprinter-p512.szx");
    ASSERT_TRUE(_emulator->SaveSnapshot(path)) << _emulator->LastSaveResult().text;
    Emulator* pentagon = EmulatorManager::GetInstance()->CreateEmulatorWithModelAndRAM("p512-target", "PENTAGON", 512, LoggerLevel::LogError).get();
    ASSERT_NE(pentagon, nullptr);
    ASSERT_TRUE(pentagon->LoadSnapshot(path)) << pentagon->LastSnapshotReport().ToText();
    for (uint16_t bank = 0; bank < 32; bank++)
    {
        const uint8_t cell = bank < 8 ? 0xF0 + bank : (bank < 16 ? 0xF8 + (bank - 8) : (bank < 24 ? 0xD0 + (bank - 16) : 0xD8 + (bank - 24)));
        EXPECT_EQ(0, std::memcmp(memory.RAMPageAddress(pld.Cell(static_cast<uint8_t>(cell))),
                                 pentagon->GetContext()->pMemory->RAMPageAddress(bank), PAGE_SIZE))
            << "bank " << bank << " (cell #" << std::hex << int(cell) << ")";
    }
    EXPECT_EQ(pentagon->GetContext()->emulatorState.p7FFD, 0xD5);
    EXPECT_EQ(pentagon->GetContext()->pMemory->GetRAMPageForBank3(), 29) << "bank 29 at #C000";
    std::remove(path.c_str());
}

TEST(SprinterZxCaptureIdentity_Test, A512kModeIsAPentagon512)
{
    using Capture = SprinterZxCapture;
    const Capture::Identity identity = Capture::IdentityOf("Pentagon 512", true, true);
    EXPECT_EQ(identity.machineHint, "pentagon512");
    EXPECT_EQ(identity.model, MM_PENTAGON);
    EXPECT_EQ(identity.ramKb, 512u);
    EXPECT_EQ(identity.bankCount, 32);
    EXPECT_EQ(Capture::IdentityOf("Pentagon 128", true, false).machineHint, "pentagon128") << "no CNF bit 7: a 128";
}

// ---------------------------------------------------------------------------------------------------------------------
// The state transfer (MachineStateTransfer, an instance's own mechanism, not the snapshot pipeline): a 128K state moves into a
// Sprinter that runs a Spectrum mode, and a Sprinter's Spectrum mode moves out to a 128K. Banks go behind the PLD cells; the machine
// is not reset (a reset would leave the mode)
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
void FillBank(uint8_t* bytes, uint8_t seed)
{
    for (uint32_t i = 0; i < PAGE_SIZE; i++)
        bytes[i] = static_cast<uint8_t>(seed * 29 + i * 7 + (i >> 8));
}
}  // namespace

TEST_F(SprinterZxSnapshot_Test, A128kStateMovesIntoASprinterZxMode)
{
    ToTheZxMenu();
    const SprinterPldState& pld = _decoder->GetPldState();
    ASSERT_FALSE((pld.cnf & 0x20) != 0) << "the BIOS 128 mode has #7FFD paging";

    auto source = EmulatorManager::GetInstance()->CreateEmulatorWithModel("transfer-128k", "128k", LoggerLevel::LogError);
    ASSERT_NE(source, nullptr);
    EmulatorContext* from = source->GetContext();
    for (uint16_t bank = 0; bank < 8; bank++)
        FillBank(from->pMemory->RAMPageAddress(bank), static_cast<uint8_t>(bank + 1));
    from->pPortDecoder->UnlockPaging();
    from->pPortDecoder->DecodePortOut(0x7FFD, 0x16, 0x8000);   // bank 6 on top, 48 BASIC
    from->pCore->GetZ80()->pc = 0x8123;
    from->pCore->GetZ80()->sp = 0xBEEF;

    const auto report = MachineStateTransfer::Transfer(*source, *_emulator);
    ASSERT_TRUE(report.ok) << report.ToString();
    for (uint16_t bank = 0; bank < 8; bank++)
        EXPECT_EQ(0, std::memcmp(from->pMemory->RAMPageAddress(bank), _context->pMemory->RAMPageAddress(pld.Cell(static_cast<uint8_t>(0xF0 + bank))), PAGE_SIZE))
            << "bank " << bank << " behind cell #F" << bank;
    EXPECT_EQ(pld.pn & 0x37, 0x16 & 0x37) << "#7FFD reached the PLD latch";
    EXPECT_EQ(_context->pCore->GetZ80()->pc, 0x8123);
    EXPECT_TRUE(_decoder->GetPldState().configState == SprinterConfigState::Configured) << "the machine stayed in its ZX mode";
    EXPECT_EQ(_context->pMemory->GetRAMPageForBank3(), pld.Cell(static_cast<uint8_t>(0xF0 + 6))) << "bank 6 at #C000";
    EmulatorManager::GetInstance()->RemoveEmulator(source->GetId());
}

TEST_F(SprinterZxSnapshot_Test, ASprinterThatRunsNoZxModeIsNoTarget)
{
    ToThePrompt();
    auto source = EmulatorManager::GetInstance()->CreateEmulatorWithModel("transfer-128k", "128k", LoggerLevel::LogError);
    ASSERT_NE(source, nullptr);
    const uint64_t before = RamHashExcept(*_context->pMemory, {});
    const auto report = MachineStateTransfer::Transfer(*source, *_emulator);
    EXPECT_FALSE(report.ok);
    EXPECT_NE(report.reason.find("not in a Spectrum (ZX) mode"), std::string::npos) << report.reason;
    EXPECT_EQ(RamHashExcept(*_context->pMemory, {}), before) << "nothing was written";
    EmulatorManager::GetInstance()->RemoveEmulator(source->GetId());
}

TEST_F(SprinterZxSnapshot_Test, ASprinterZxModeMovesOutToA128k)
{
    ToTheZxMenu();
    const SprinterPldState& pld = _decoder->GetPldState();
    for (uint16_t bank = 0; bank < 8; bank++)
        FillBank(_context->pMemory->RAMPageAddress(pld.Cell(static_cast<uint8_t>(0xF0 + bank))), static_cast<uint8_t>(bank + 40));
    _context->pCore->GetZ80()->pc = 0x8222;

    auto target = EmulatorManager::GetInstance()->CreateEmulatorWithModel("transfer-target", "128k", LoggerLevel::LogError);
    ASSERT_NE(target, nullptr);
    const auto report = MachineStateTransfer::Transfer(*_emulator, *target);
    ASSERT_TRUE(report.ok) << report.ToString();
    for (uint16_t bank = 0; bank < 8; bank++)
        EXPECT_EQ(0, std::memcmp(target->GetContext()->pMemory->RAMPageAddress(bank),
                                 _context->pMemory->RAMPageAddress(pld.Cell(static_cast<uint8_t>(0xF0 + bank))), PAGE_SIZE))
            << "bank " << bank;
    EXPECT_EQ(target->GetContext()->emulatorState.p7FFD & 0x37, pld.pn & 0x37);
    EXPECT_EQ(target->GetContext()->pCore->GetZ80()->pc, 0x8222);
    EmulatorManager::GetInstance()->RemoveEmulator(target->GetId());
}

// A Sprinter in the DSS / BIOS runs its own software: its state moves nowhere but into another Sprinter
TEST_F(SprinterZxSnapshot_Test, ASprinterAtThePromptMovesNowhere)
{
    ToThePrompt();
    auto target = EmulatorManager::GetInstance()->CreateEmulatorWithModel("transfer-target", "128k", LoggerLevel::LogError);
    ASSERT_NE(target, nullptr);
    const auto report = MachineStateTransfer::Check(*_context, *target->GetContext());
    EXPECT_FALSE(report.ok);
    EXPECT_NE(report.reason.find("not in a Spectrum (ZX) mode"), std::string::npos) << report.reason;
    EmulatorManager::GetInstance()->RemoveEmulator(target->GetId());
}
