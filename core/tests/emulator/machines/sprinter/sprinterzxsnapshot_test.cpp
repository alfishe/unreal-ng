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
