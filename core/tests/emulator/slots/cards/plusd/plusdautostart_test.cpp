// The MGT +D autostart (docs/inprogress/2026-11-15-mgt-plus-d/autostart.md §3, TODO P4): the plan from the G+DOS
// directory, the routing of an MGT disk to the +D's drive (refused without the card, D-3), and the three plans that
// start something, end to end on disks the test writes: the emulator's system disk with an `AUTO` program (`run`) or
// with another program (`run+load`), and a disk without a DOS (`system-disk+load`). Each program loops on its own
// border colour, so the border says which program runs.

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <functional>
#include <iterator>
#include <regex>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/builtinactions.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskautostart.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/media/floppyformats.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "emulator/slots/cards/plusd/plusdautostart.h"
#include "emulator/slots/cards/plusd/plusdcard.h"
#include "emulator/slots/slotcontrol.h"
#include "unrealdisk/diskfs.h"

namespace
{
const char* kSystemDisk = "data/boot/plusd/gdos-2a-system.mgt";
constexpr size_t kDiskSize = 819200;

/// A G+DOS disk image in MGT order (the sides of a cylinder one after the other) with files written the way G+DOS
/// writes them: a directory entry, 510 data bytes a sector and the link to the next
class GdosDisk
{
public:
    GdosDisk() : bytes(kDiskSize, 0) {}
    explicit GdosDisk(std::vector<uint8_t> image) : bytes(std::move(image)) {}

    std::vector<uint8_t> bytes;

    uint8_t* Sector(uint8_t track, uint8_t sector)
    {
        const size_t cylinder = track & 0x7F, side = track >> 7;
        return bytes.data() + ((cylinder * 2 + side) * 10 + (sector - 1)) * 512;
    }

    uint8_t* Entry(uint32_t slot)
    {
        return Sector(static_cast<uint8_t>(slot / 20), static_cast<uint8_t>(slot % 20 / 2 + 1)) + (slot % 2) * 256;
    }

    /// Next free directory slot and the first track past every used file (files go there, side 0 from cylinder 40)
    uint32_t FreeSlot()
    {
        for (uint32_t slot = 0; slot < 80; slot++)
            if (Entry(slot)[0] == 0)
                return slot;
        return 80;
    }

    /// A file of `type` (1 BASIC, 4 CODE, 5 / 9 snapshots); the tape types get the 9-byte header in the entry and in
    /// front of the data
    void Add(const std::string& name, uint8_t type, const std::vector<uint8_t>& body, const uint8_t* header = nullptr)
    {
        std::vector<uint8_t> data;
        if (header)
            data.assign(header, header + 9);
        data.insert(data.end(), body.begin(), body.end());
        const uint16_t sectors = static_cast<uint16_t>((data.size() + 509) / 510);
        const uint32_t slot = FreeSlot();
        uint8_t* e = Entry(slot);
        e[0] = type;
        std::memset(e + 1, ' ', 10);
        std::memcpy(e + 1, name.data(), std::min<size_t>(name.size(), 10));
        e[11] = static_cast<uint8_t>(sectors >> 8);
        e[12] = static_cast<uint8_t>(sectors);
        uint8_t track = static_cast<uint8_t>(_nextCylinder), sector = 1;
        e[13] = track;
        e[14] = sector;
        if (header)
            std::memcpy(e + 211, header, 9);
        for (uint16_t k = 0; k < sectors; k++)
        {
            uint8_t* s = Sector(track, sector);
            const size_t from = static_cast<size_t>(k) * 510;
            std::memcpy(s, data.data() + from, std::min<size_t>(510, data.size() - from));
            const size_t bit = (static_cast<size_t>(track & 0x7F) - 4) * 10 + (sector - 1);
            e[15 + bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
            if (++sector > 10)
            {
                sector = 1;
                track++;
            }
            const bool last = k + 1 == sectors;
            s[510] = last ? 0 : track;
            s[511] = last ? 0 : sector;
        }
        _nextCylinder = track + 1;
    }

    /// `10 BORDER <colour>: GO TO 10`, autostarting at line 10
    void AddBorderLoop(const std::string& name, uint8_t colour)
    {
        const std::vector<uint8_t> line = {
            0xE7, static_cast<uint8_t>('0' + colour), 0x0E, 0x00, 0x00, colour, 0x00, 0x00,   // BORDER n
            ':', 0xEC, '1', '0', 0x0E, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x0D};                  // : GO TO 10
        std::vector<uint8_t> program = {0x00, 0x0A, static_cast<uint8_t>(line.size()), 0x00};
        program.insert(program.end(), line.begin(), line.end());
        const uint16_t length = static_cast<uint16_t>(program.size());
        const uint8_t header[9] = {0x00, static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8), 0xCB, 0x5C,
                                   static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8), 0x0A, 0x00};
        Add(name, 1, program, header);
    }

    void AddCode(const std::string& name)
    {
        const std::vector<uint8_t> body(100, 0xC9);
        const uint8_t header[9] = {0x03, 100, 0, 0x00, 0x80, 0xFF, 0xFF, 0x00, 0x00};
        Add(name, 4, body, header);
    }

    std::string Save(const std::string& file) const
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(file);
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return path;
    }

    std::unique_ptr<DiskImage> Image() const
    {
        std::unique_ptr<DiskImage> disk;
        std::string format;
        EXPECT_TRUE(unrealdisk::FloppyFormats::LoadData({}, bytes.data(), bytes.size(), "work.mgt", disk, format).Ok());
        return disk;
    }

private:
    int _nextCylinder = 40;
};

std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

GdosDisk SystemDisk()
{
    return GdosDisk(ReadFile((TestPathHelper::FindProjectRoot() / kSystemDisk).string()));
}

class PlusDAutostart_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Create(const std::string& model, bool withCard, const char* card = "plusd")
    {
        std::function<void(CONFIG&)> slotSet;
        std::string error;
        if (withCard)
            ASSERT_TRUE(SlotControl::CreateOverride({{"edge.1", card}}, slotSet, error)) << error;
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero, slotSet);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }

    PlusDAutostart::Status Status() const
    {
        return _context->pDiskAutostart->PlusD().GetStatus();
    }

    /// Frames until the steps end and the border shows `colour` (the program runs)
    bool RunsWithBorder(uint8_t colour, int maxFrames)
    {
        for (int frame = 0; frame < maxFrames; frame += 5)
        {
            _emulator->RunNFrames(5, true);
            const PlusDAutostart::Status status = Status();
            if (status.finished && !status.ok)
                return false;
            if (status.finished && _context->emulatorState.border_attr == colour)
                return true;
        }
        return false;
    }

    std::string Describe() const
    {
        const PlusDAutostart::Status status = Status();
        std::string text = "plan " + status.plan.kind + ", stage " + status.stage + ", message '" + status.message +
                           "', border " + std::to_string(_context->emulatorState.border_attr) + ", steps:";
        for (const PlusDAutostart::Step& step : status.steps)
            text += " " + step.name + "@" + std::to_string(step.frame);
        return text;
    }
};
}  // namespace

/// The plan by the directory (~5 ms): AUTO with +SYS runs; the rules for the start file; a 128K snapshot on a 48K;
/// CODE only; a non-G+DOS disk
TEST(PlusDAutostart_Plan, RulesByDirectory)
{
    const std::string system = (TestPathHelper::FindProjectRoot() / kSystemDisk).string();
    auto plan = [&](const GdosDisk& disk, bool is48K = true) {
        std::unique_ptr<DiskImage> image = disk.Image();
        return PlusDAutostart::MakePlan(*image, is48K, system);
    };

    GdosDisk withAuto = SystemDisk();
    withAuto.AddBorderLoop("AUTOrun", 3);
    PlusDAutostart::Plan p = plan(withAuto);
    EXPECT_EQ(p.kind, "run");
    EXPECT_EQ(p.dos, "disk");
    EXPECT_EQ(p.startFile, "AUTOrun");

    GdosDisk sysOnly = SystemDisk();
    sysOnly.AddBorderLoop("game", 4);
    p = plan(sysOnly);
    EXPECT_EQ(p.kind, "run+load");
    EXPECT_EQ(p.command, "LOAD d1\"CONFIG\"") << "the first BASIC (CONFIG comes before game)";
    EXPECT_EQ(p.dosBody.size(), 6656u);

    GdosDisk loader;
    loader.AddBorderLoop("intro", 1);
    loader.AddBorderLoop("Loader", 2);
    p = plan(loader);
    EXPECT_EQ(p.kind, "system-disk+load");
    EXPECT_EQ(p.dos, "system-disk");
    EXPECT_EQ(p.command, "LOAD d1\"Loader\"") << "a loader's name wins over the first BASIC";

    GdosDisk autoNoDos;
    autoNoDos.AddBorderLoop("intro", 1);
    autoNoDos.AddBorderLoop("auto", 2);
    EXPECT_EQ(plan(autoNoDos).command, "LOAD d1\"auto\"") << "AUTO without +SYS is loaded explicitly";

    GdosDisk snapshots;
    snapshots.Add("Part A", 5, std::vector<uint8_t>(49152, 0));
    snapshots.Add("Part B", 5, std::vector<uint8_t>(49152, 0));
    EXPECT_EQ(plan(snapshots).command, "LOAD d1\"Part A\"S") << "the first snapshot";

    GdosDisk snap128;
    snap128.Add("Big", 9, std::vector<uint8_t>(1000, 0));
    EXPECT_EQ(plan(snap128).kind, "mount") << "a 128K snapshot cannot start on a 48K";
    EXPECT_NE(plan(snap128).message.find("128K-PLUSD"), std::string::npos);
    EXPECT_EQ(plan(snap128, false).command, "LOAD d1\"Big\"K");

    GdosDisk code;
    code.AddCode("screen");
    EXPECT_EQ(plan(code).kind, "mount");
    EXPECT_NE(plan(code).message.find("no program to start"), std::string::npos);

    GdosDisk foreign;
    foreign.Add("other", 19, std::vector<uint8_t>(100, 0));
    EXPECT_EQ(plan(foreign).kind, "mount");
    EXPECT_NE(plan(foreign).message.find("not a G+DOS disk"), std::string::npos);

    std::unique_ptr<DiskImage> image = loader.Image();
    EXPECT_EQ(PlusDAutostart::MakePlan(*image, true, "").kind, "mount") << "no system disk, nothing to boot";
}

/// The DISCiPLE's plan (~5 ms): GDOS 3 boots `SYS*`, not G+DOS's `+SYS*`; a 128K snapshot names 128K-DISCIPLE
TEST(PlusDAutostart_Plan, DiscipleLooksForSys)
{
    EXPECT_TRUE(PlusDAutostart::IsSystemFile("SYSTEM 3d", 4, true));
    EXPECT_TRUE(PlusDAutostart::IsSystemFile("sys", 4, true));
    EXPECT_FALSE(PlusDAutostart::IsSystemFile("+SYS 2a", 4, true)) << "the DISCiPLE ROM does not load +SYS";
    EXPECT_TRUE(PlusDAutostart::IsSystemFile("+SYS 2a", 4, false));
    EXPECT_FALSE(PlusDAutostart::IsSystemFile("SYSTEM 3d", 4, false));
    EXPECT_FALSE(PlusDAutostart::IsSystemFile("SYSTEM 3d", 1, true)) << "CODE only";

    GdosDisk disk;
    const uint8_t header[9] = {0x03, 0x70, 0x17, 0x00, 0x20, 0xFF, 0xFF, 0x00, 0x00};  // CODE 6000 bytes at #2000
    disk.Add("SYSTEM 3d", 4, std::vector<uint8_t>(6000, 0x11), header);
    disk.AddBorderLoop("game", 4);
    std::unique_ptr<DiskImage> image = disk.Image();
    const PlusDAutostart::Plan p = PlusDAutostart::MakePlan(*image, true, "", true);
    EXPECT_EQ(p.kind, "run+load") << p.message;
    EXPECT_EQ(p.dos, "disk");
    EXPECT_EQ(p.command, "LOAD d1\"game\"");
    EXPECT_EQ(p.dosBody.size(), 6000u);

    GdosDisk snap128;
    snap128.Add("Big", 9, std::vector<uint8_t>(1000, 0));
    image = snap128.Image();
    EXPECT_NE(PlusDAutostart::MakePlan(*image, true, "", true).message.find("128K-DISCIPLE"), std::string::npos);
}

/// `system-disk+load` on the DISCiPLE (~1 s): a disk without `SYS*` boots GDOS 3d from
/// data/boot/disciple/gdos-3d-system.mgt in `fdd.disciple1`, then the work's disk comes back and its loader runs
TEST_F(PlusDAutostart_Test, DiscipleBootsGdos3dThenTheWork)
{
    Create("48K", true, "disciple");
    _emulator->EnableTurboMode();
    GdosDisk disk;
    disk.AddBorderLoop("Loader", 5);
    const std::string path = disk.Save("disciple-nodos.mgt");
    const Emulator::DiskAutostartResult result = _emulator->AutostartDisk(path);
    ASSERT_TRUE(result.started) << result.message;
    EXPECT_EQ(result.plan, "system-disk+load");
    PlusDCard* card = PlusDCard::Find(_context);
    ASSERT_NE(card, nullptr);
    ASSERT_TRUE(card->IsDisciple());
    EXPECT_EQ(card->Drive(0)->getSlotId(), "fdd.disciple1");
    EXPECT_TRUE(RunsWithBorder(5, 2000)) << Describe();
    EXPECT_EQ(Status().plan.dosBody.size(), 0x1A00u) << "SYSTEM 3d";
    EXPECT_FALSE(card->Drive(0)->isWriteProtect()) << "the work's disk is back";
}

/// An MGT disk goes to the +D's drive (~30 ms): drive A / B are `fdd.plusd1` / `2`, C is refused; Beta drive A stays
/// empty
TEST_F(PlusDAutostart_Test, MgtGoesToThePlusDDrive)
{
    Create("48K", true);
    PlusDCard* card = PlusDCard::Find(_context);
    ASSERT_NE(card, nullptr);
    const std::string path = SystemDisk().Save("route.mgt");
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(path, 0, &error)) << error;
    EXPECT_NE(card->Drive(0)->getDiskImage(), nullptr);
    ASSERT_TRUE(_emulator->LoadDisk(SystemDisk().Save("route-b.mgt"), 1, &error)) << error;
    EXPECT_NE(card->Drive(1)->getDiskImage(), nullptr);
    EXPECT_FALSE(_emulator->LoadDisk(path, 2, &error));
    EXPECT_NE(error.find("drives 1 and 2"), std::string::npos) << error;
}

/// No +D (~30 ms): an MGT disk is refused, naming the presets (D-3), by LoadDisk and by the autostart
TEST_F(PlusDAutostart_Test, MgtRefusedWithoutTheCard)
{
    Create("PENTAGON", false);
    const std::string path = SystemDisk().Save("nocard.mgt");
    std::string error;
    EXPECT_FALSE(_emulator->LoadDisk(path, 0, &error));
    EXPECT_NE(error.find("48K-PLUSD"), std::string::npos) << error;
    const Emulator::DiskAutostartResult result = _emulator->AutostartDisk(path);
    EXPECT_FALSE(result.mounted);
    EXPECT_FALSE(result.started);
    EXPECT_NE(result.message.find("no MGT interface"), std::string::npos) << result.message;
    EXPECT_NE(result.message.find("48K-DISCIPLE"), std::string::npos) << result.message;
}

/// `run` (~1 s): the system disk with an AUTO program: reset, RUN; the ROM loads +SYS, then AUTO, which runs
TEST_F(PlusDAutostart_Test, RunsAutoWithTheDisksDos)
{
    Create("48K", true);
    _emulator->EnableTurboMode();
    GdosDisk disk = SystemDisk();
    disk.AddBorderLoop("AUTO", 3);
    const Emulator::DiskAutostartResult result = _emulator->AutostartDisk(disk.Save("auto.mgt"));
    ASSERT_TRUE(result.mounted) << result.message;
    ASSERT_TRUE(result.started) << result.message;
    EXPECT_EQ(result.plan, "run");
    EXPECT_EQ(result.dos, "disk");
    EXPECT_TRUE(RunsWithBorder(3, 1500)) << Describe();
}

/// A program written by unrealdisk's G+DOS writer (DiskFs, as `zxdisk put`), not by hand (~1 s): the system disk with
/// `AUTO` put on it, saved as .mgt; the +D ROM's RUN loads +SYS, then G+DOS loads the written AUTO, which runs
TEST_F(PlusDAutostart_Test, RunsAProgramWrittenByTheDiskTool)
{
    Create("48K", true);
    _emulator->EnableTurboMode();
    GdosDisk border;
    border.AddBorderLoop("x", 5);
    std::unique_ptr<DiskImage> loopImage = border.Image();
    std::vector<uint8_t> program;
    std::string error;
    ASSERT_TRUE(unrealdisk::DiskFs(*loopImage).Read("", "x", program, error)) << error;

    std::unique_ptr<DiskImage> image = SystemDisk().Image();
    unrealdisk::DiskFs fs(*image);
    unrealdisk::DiskFsWriteOptions options;
    options.basicType = 0;
    options.start = 10;
    ASSERT_TRUE(fs.Write("", "AUTO", program, options, error)) << error;
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("written.mgt");
    const unrealdisk::FloppySaveResult saved = unrealdisk::FloppyFormats::Save({}, *image, path, false);
    ASSERT_TRUE(saved.saved) << saved.reason;

    const Emulator::DiskAutostartResult result = _emulator->AutostartDisk(path);
    ASSERT_TRUE(result.started) << result.message;
    EXPECT_EQ(result.plan, "run");
    EXPECT_TRUE(RunsWithBorder(5, 1500)) << Describe();
}

/// On a 128K the reset goes to its menu and the steps choose 128 BASIC (FLAGS bit 4: a 128K game checks it);
/// `basic` "48" keeps 48 BASIC, a user's choice for 48K works; "128" on a 48K is refused
TEST_F(PlusDAutostart_Test, ChoosesTheBasicOnThe128K)
{
    for (const char* basic : {"", "48"})
    {
        Create("128K", true);
        _emulator->EnableTurboMode();
        GdosDisk disk = SystemDisk();
        disk.AddBorderLoop("AUTO", 3);
        const Emulator::DiskAutostartResult result = _emulator->AutostartDisk(disk.Save("auto128.mgt"), 0, basic);
        ASSERT_TRUE(result.started) << result.message;
        EXPECT_EQ(result.basic, *basic ? "48" : "128");
        EXPECT_TRUE(RunsWithBorder(3, 1500)) << "basic '" << basic << "': " << Describe();
        const uint8_t flags = _context->pMemory->DirectReadFromZ80Memory(0x5C3B);
        EXPECT_EQ((flags & 0x10) != 0, *basic == '\0') << "basic '" << basic << "' FLAGS " << int(flags);
        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }
    Create("48K", true);
    GdosDisk disk = SystemDisk();
    disk.AddBorderLoop("AUTO", 3);
    const Emulator::DiskAutostartResult refused = _emulator->AutostartDisk(disk.Save("auto48.mgt"), 0, "128");
    EXPECT_FALSE(refused.mounted) << "refused before anything is mounted";
    EXPECT_FALSE(refused.started);
    EXPECT_NE(refused.message.find("128K-PLUSD"), std::string::npos) << refused.message;
}

/// The planes pass `basic` through: the action media.open (Stream Deck, handheld, every plane's action call) answers
/// with the plan and the BASIC, and refuses 128 BASIC on a 48K by the core's message (~20 ms, the steps not run)
TEST_F(PlusDAutostart_Test, MediaOpenPassesTheBasic)
{
    using namespace actions;
    Create("48K", true);
    GdosDisk disk = SystemDisk();
    disk.AddBorderLoop("AUTO", 3);
    const std::string path = disk.Save("action.mgt");
    auto open = [&](const char* basic) {
        ActionValue value;
        value.pressed = true;
        value.params["path"] = path;
        value.params["basic"] = basic;
        ActionOrigin origin;
        origin.origin = execution::Origin::HostDevice;
        origin.identity = "test";
        origin.client = "test";
        return ActionRegistry::Instance().Run("media.open", std::move(value), _emulator->GetId(), origin);
    };
    const ActionResult opened = open("48");
    ASSERT_TRUE(opened.ok) << opened.message;
    ASSERT_NE(opened.body.find("plan"), nullptr);
    EXPECT_EQ(opened.body.find("plan")->s, "run");
    EXPECT_EQ(opened.body.find("basic")->s, "48");

    const ActionResult refused = open("128");
    EXPECT_FALSE(refused.ok);
    EXPECT_NE(refused.message.find("128K-PLUSD"), std::string::npos) << refused.message;
    const ActionResult unknown = open("64");
    EXPECT_FALSE(unknown.ok);
    EXPECT_NE(unknown.message.find("48 or 128"), std::string::npos) << unknown.message;
}

/// `run+load` (~1 s): the system disk with another program: RUN, the DOS loads, then LOAD d1"..." of the first BASIC
TEST_F(PlusDAutostart_Test, LoadsTheFirstProgramAfterTheDisksDos)
{
    Create("48K", true);
    _emulator->EnableTurboMode();
    GdosDisk disk = SystemDisk();
    // CONFIG is the first BASIC on the system disk: drop it so the work's program is the one to load
    disk.Entry(1)[0] = 0;
    disk.AddBorderLoop("game", 4);
    const Emulator::DiskAutostartResult result = _emulator->AutostartDisk(disk.Save("sysonly.mgt"));
    ASSERT_TRUE(result.started) << result.message;
    EXPECT_EQ(result.plan, "run+load");
    EXPECT_EQ(result.command, "LOAD d1\"game\"");
    EXPECT_TRUE(RunsWithBorder(4, 1500)) << Describe();
    const PlusDAutostart::Status status = Status();
    ASSERT_EQ(status.steps.size(), 4u) << Describe();
    EXPECT_EQ(status.steps[2].name, "dos");
}

/// `system-disk+load` (~1.5 s): a disk without a DOS: the system disk boots G+DOS, the work's disk goes back into
/// drive 1 (writable again), its loader runs
TEST_F(PlusDAutostart_Test, BootsTheSystemDiskThenTheWork)
{
    Create("48K", true);
    _emulator->EnableTurboMode();
    GdosDisk disk;
    disk.AddBorderLoop("Loader", 2);
    const std::string path = disk.Save("nodos.mgt");
    const Emulator::DiskAutostartResult result = _emulator->AutostartDisk(path);
    ASSERT_TRUE(result.started) << result.message;
    EXPECT_EQ(result.plan, "system-disk+load");
    EXPECT_EQ(result.dos, "system-disk");
    PlusDCard* card = PlusDCard::Find(_context);
    ASSERT_NE(card, nullptr);
    EXPECT_TRUE(card->Drive(0)->isWriteProtect()) << "the system disk is in drive 1, read only";
    EXPECT_TRUE(RunsWithBorder(2, 2000)) << Describe();
    const PlusDAutostart::Status status = Status();
    ASSERT_EQ(status.steps.size(), 5u) << Describe();
    EXPECT_EQ(status.steps[3].name, "swap");
    EXPECT_FALSE(card->Drive(0)->isWriteProtect()) << "the work's disk is back";
}

/// A disk with nothing to start (~30 ms): mounted in drive 1, no reset, the answer says why
TEST_F(PlusDAutostart_Test, CodeOnlyIsMounted)
{
    Create("48K", true);
    GdosDisk disk;
    disk.AddCode("picture");
    const Emulator::DiskAutostartResult result = _emulator->AutostartDisk(disk.Save("code.mgt"));
    EXPECT_TRUE(result.mounted);
    EXPECT_FALSE(result.started);
    EXPECT_EQ(result.plan, "mount");
    EXPECT_NE(result.message.find("no program to start"), std::string::npos) << result.message;
}

/// One image, for a closer look: UNREAL_PLUSD_ONE = the .mgt, UNREAL_PLUSD_MODEL = 48K / 128K (default 48K),
/// UNREAL_PLUSD_BASIC = 48 / 128 (default the machine's own).
/// Prints the steps, the BASIC position (PPC / SUBPPC / ERR_NR / FLAGS) and the screen text every 250 frames
TEST(PlusDAutostart_Image, One)
{
    const char* path = std::getenv("UNREAL_PLUSD_ONE");
    if (path == nullptr || *path == '\0')
        GTEST_SKIP() << "UNREAL_PLUSD_ONE (an .mgt) not set";
    const char* modelEnv = std::getenv("UNREAL_PLUSD_MODEL");
    const std::string model = modelEnv != nullptr && *modelEnv != '\0' ? modelEnv : "48K";
    std::function<void(CONFIG&)> slotSet;
    std::string error;
    ASSERT_TRUE(SlotControl::CreateOverride({{"edge.1", "plusd"}}, slotSet, error)) << error;
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero, slotSet);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    emulator->EnableTurboMode();
    emulator->RunNFrames(100, true);
    const char* basicEnv = std::getenv("UNREAL_PLUSD_BASIC");
    const Emulator::DiskAutostartResult result = emulator->AutostartDisk(path, 0, basicEnv != nullptr ? basicEnv : "");
    std::cout << "plan " << result.plan << " command '" << result.command << "' message '" << result.message << "'\n";
    PlusDCard* card = PlusDCard::Find(context);
    auto peek = [&](uint16_t a) { return int(context->pMemory->DirectReadFromZ80Memory(a)); };
    for (int frame = 0; frame <= 3000; frame += 250)
    {
        const PlusDAutostart::Status status = context->pDiskAutostart->PlusD().GetStatus();
        std::string steps;
        for (const PlusDAutostart::Step& step : status.steps)
            steps += " " + step.name + "@" + std::to_string(step.frame);
        std::cout << "---- frame " << frame << " stage " << status.stage << " ok " << status.ok << " '" << status.message
                  << "' steps" << steps << " pc " << context->pCore->GetZ80()->pc << " paged "
                  << (card && card->IsPaged()) << " fdc " << (card && card->Fdc() ? int(card->Fdc()->PeekRegister(0)) : -1) << " PPC " << (peek(0x5C45) | peek(0x5C46) << 8) << ":" << peek(0x5C47)
                  << " ERR_NR " << peek(0x5C3A) << " FLAGS " << peek(0x5C3B) << " [65535] " << peek(0xFFFF) << "\n"
                  << ScreenOCR::ocrScreen(emulator->GetUUID()) << "\n";
        emulator->RunNFrames(250, true);
    }
    EmulatorTestHelper::CleanupEmulator(emulator);
}
