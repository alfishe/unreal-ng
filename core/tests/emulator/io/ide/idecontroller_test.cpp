// The IDE board of a machine (implementation-plan.md P3): the units from the
// config, their media slots, media reaching the units, a disc swap, a folder
// as a hard disk, machines without IDE

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/cdtestdisc.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/scratchfolder.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/ide/ata/atapicdrom.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/media/mediacontrol.h"
#include "emulator/media/mediamanager.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/soundmanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "base/featuremanager.h"

using namespace ata;

namespace
{
    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    class IdeController_Test : public ::testing::Test
    {
    protected:
        Emulator* _emulator = nullptr;
        EmulatorContext* _context = nullptr;

        void Create(const char* model)
        {
            _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
            ASSERT_NE(_emulator, nullptr);
            _context = _emulator->GetContext();
        }
        void TearDown() override
        {
            if (_emulator)
                EmulatorTestHelper::CleanupEmulator(_emulator);
            MessageCenter::DisposeDefaultMessageCenter();
        }

        MediaManager& Manager() { return *_context->pMediaManager; }
        AtaChannel& Channel() { return _context->pIdeController->Channel(); }

        MediaResult Insert(const std::string& slot, const std::string& path)
        {
            MediaSource source;
            source.path = path;
            InsertOptions options;
            options.immediate = true;
            return Manager().Insert(slot, source, options);
        }
    };
}  // namespace

TEST(IdeScheme_Test, NamesRoundTrip)
{
    for (IDE_SCHEME scheme : {IDE_NONE, IDE_ATM, IDE_NEMO, IDE_NEMO_A8, IDE_NEMO_DIVIDE, IDE_SMUC, IDE_PROFI, IDE_DIVIDE, IDE_SPRINTER})
    {
        IDE_SCHEME parsed = IDE_NONE;
        ASSERT_TRUE(Config::ParseIdeScheme(Config::IdeSchemeName(scheme), parsed)) << Config::IdeSchemeName(scheme);
        EXPECT_EQ(parsed, scheme);
    }
    IDE_SCHEME parsed = IDE_NONE;
    EXPECT_TRUE(Config::ParseIdeScheme(" nemo-divide ", parsed));
    EXPECT_EQ(parsed, IDE_NEMO_DIVIDE);
    EXPECT_FALSE(Config::ParseIdeScheme("WILD", parsed));
}

/// Pentagon: the Nemo board with two hard-disk units (no CD drive unless the config says CDn=1)
TEST_F(IdeController_Test, PentagonHasTwoDiskUnits)
{
    Create("PENTAGON");
    ASSERT_NE(_context->pIdeController, nullptr);
    EXPECT_EQ(_context->pIdeController->Scheme(), IDE_NEMO);

    for (const char* id : {"ide0.master", "ide0.slave"})
    {
        const auto info = Manager().Info(id);
        ASSERT_TRUE(info.has_value()) << id;
        EXPECT_EQ(info->descriptor.kind, MediaKind::Block) << id;
        EXPECT_FALSE(info->descriptor.removable) << id;
        EXPECT_EQ(info->descriptor.defaultAccess, AccessMode::WriteThrough) << id;
    }
    std::string id;
    ASSERT_TRUE(MediaControl::ResolveSelector(Manager(), "hd", id).Ok());
    EXPECT_EQ(id, "ide0.master");
    ASSERT_TRUE(MediaControl::ResolveSelector(Manager(), "tag:ide+nemo+slave", id).Ok());
    EXPECT_EQ(id, "ide0.slave");
    EXPECT_FALSE(MediaControl::ResolveSelector(Manager(), "cd", id).Ok()) << "no CD drive";

    // No disks yet: the channel floats
    EXPECT_EQ(Channel().ReadRegister(StatusCommand), 0xFF);
}

/// The DD7 pull-down is the Sprinter board's: every other board keeps the floating #FF of an empty channel
TEST_F(IdeController_Test, OnlyTheSprinterPullsDd7Down)
{
    const std::pair<const char*, uint16_t> boards[] = {
        {"PENTAGON", AtaChannel::kEmptyBusFloating}, {"PROFI", AtaChannel::kEmptyBusFloating},
        {"SCORPION", AtaChannel::kEmptyBusFloating}, {"ATM3", AtaChannel::kEmptyBusFloating},
        {"ATM710", AtaChannel::kEmptyBusFloating},   {"SPRINTER", AtaChannel::kEmptyBusDd7PullDown}};
    for (const auto& [model, word] : boards)
    {
        Create(model);
        if (std::string(model) == "SCORPION")
        {
            _context->config.ide_scheme = IDE_SMUC;  // configs/scorpion ships Scheme=NONE
            _context->pCore->RefitIde();
        }
        IdeController& ide = *_context->pIdeController;
        ASSERT_TRUE(ide.Enabled()) << model;
        for (int index = 0; index < ide.ChannelCount(); index++)
        {
            EXPECT_EQ(ide.Channel(index).EmptyBus(), word) << model << " channel " << index;
            if (!ide.Channel(index).AnyPresent())  // a shipped CD unit is on the bus with no disc
                EXPECT_EQ(ide.Channel(index).ReadRegister(StatusCommand), word & 0xFF) << model << " channel " << index;
        }
        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }
}

TEST_F(IdeController_Test, DiskMediaReachTheUnit)
{
    Create("PENTAGON");
    ScratchFolder folder("ide-media");
    std::string disk(2048 * 512, '\0');
    std::memcpy(disk.data() + 7 * 512, "SECTOR7", 7);
    const std::string image = Utf8(folder.File("hdd.img", disk));
    std::string cd(40 * 2048, '\0');
    std::memcpy(cd.data() + 16 * 2048, "\x01" "CD001", 6);
    const std::string iso = Utf8(folder.File("disc.iso", cd));

    ASSERT_TRUE(Insert("ide0.master", image).Ok());
    EXPECT_EQ(Manager().Info("ide0.master")->access, AccessMode::WriteThrough);
    EXPECT_EQ(Insert("ide0.slave", iso).error, MediaError::KindMismatch) << "a CD image is no hard disk";

    // READ SECTORS through the channel, as a board adapter would
    Channel().WriteRegister(DeviceHead, 0xE0);  // master, LBA
    Channel().WriteRegister(SectorCount, 1);
    Channel().WriteRegister(SectorNumber, 7);
    Channel().WriteRegister(CylinderLow, 0);
    Channel().WriteRegister(CylinderHigh, 0);
    Channel().WriteRegister(StatusCommand, Command::ReadSectors);
    EXPECT_EQ(Channel().ReadData(), ('E' << 8) | 'S');
}

/// A unit configured as a CD drive (CD1=1): an Optical slot; the drive stays on
/// the bus, discs come and go and raise unit attention
TEST(IdeControllerCd_Test, ConfiguredCdDriveTakesDiscs)
{
    EmulatorContext context(LoggerLevel::LogError);
    context.config.mem_model = MM_PENTAGON;
    context.config.ide_scheme = IDE_NEMO;
    context.config.ide[1].cd = 1;
    MediaManager manager(&context);
    context.pMediaManager = &manager;
    {
        IdeController ide(&context);
        context.pIdeController = &ide;
        const auto slave = manager.Info("ide0.slave");
        ASSERT_TRUE(slave.has_value());
        EXPECT_EQ(slave->descriptor.kind, MediaKind::Optical);
        EXPECT_TRUE(slave->descriptor.removable);
        std::string id;
        ASSERT_TRUE(MediaControl::ResolveSelector(manager, "cd", id).Ok());
        EXPECT_EQ(id, "ide0.slave");

        ScratchFolder folder("ide-cd");
        std::string cd(40 * 2048, '\0');
        std::memcpy(cd.data() + 16 * 2048, "\x01" "CD001", 6);
        MediaSource source;
        source.path = Utf8(folder.File("disc.iso", cd));
        InsertOptions options;
        options.immediate = true;
        ASSERT_TRUE(manager.Insert("ide0.slave", source, options).Ok());
        EXPECT_EQ(manager.Info("ide0.slave")->access, AccessMode::ReadOnly);
        auto* cdrom = static_cast<AtapiCdrom*>(ide.Channel().Unit(1));
        EXPECT_TRUE(cdrom->HasDisc());
        EXPECT_EQ(cdrom->State().unitAttention, 1);

        ASSERT_TRUE(manager.Eject("ide0.slave").Ok());
        EXPECT_FALSE(cdrom->HasDisc());
        ide.Channel().WriteRegister(DeviceHead, 0xB0);
        EXPECT_NE(ide.Channel().ReadRegister(StatusCommand), 0xFF) << "the drive stays on the bus";
        context.pIdeController = nullptr;
    }
    context.pMediaManager = nullptr;
}

/// The board must fit the machine; one that does not is switched off
TEST(IdeScheme_Test, SchemesFitTheirMachines)
{
    EXPECT_TRUE(IdeController::SchemeFits(IDE_PROFI, MM_PROFI));
    EXPECT_FALSE(IdeController::SchemeFits(IDE_PROFI, MM_PENTAGON));
    EXPECT_TRUE(IdeController::SchemeFits(IDE_SMUC, MM_PROFSCORP));
    EXPECT_FALSE(IdeController::SchemeFits(IDE_SMUC, MM_ATM3));
    EXPECT_TRUE(IdeController::SchemeFits(IDE_ATM, MM_ATM710));
    EXPECT_TRUE(IdeController::SchemeFits(IDE_NEMO_DIVIDE, MM_TSL)) << "TSConf keeps the ZX-Evo NemoIDE (zports.v:766-783)";
    EXPECT_TRUE(IdeController::SchemeFits(IDE_NEMO_DIVIDE, MM_ATM3));
    EXPECT_FALSE(IdeController::SchemeFits(IDE_NEMO, MM_PROFI));
    EXPECT_TRUE(IdeController::SchemeFits(IDE_SPRINTER, MM_SPRINTER));
    EXPECT_FALSE(IdeController::SchemeFits(IDE_SPRINTER, MM_PENTAGON));
    EXPECT_FALSE(IdeController::SchemeFits(IDE_NEMO, MM_SPRINTER)) << "the Sprinter's PLD decodes every port";
    EXPECT_FALSE(IdeController::SchemeFits(IDE_DIVIDE, MM_SPRINTER));

    EmulatorContext context(LoggerLevel::LogError);
    context.config.mem_model = MM_PENTAGON;
    context.config.ide_scheme = IDE_PROFI;
    IdeController ide(&context);
    EXPECT_FALSE(ide.Enabled());
}

/// A folder as the hard disk: a FAT16 volume, guest writes kept for the session
TEST_F(IdeController_Test, FolderBecomesAHardDisk)
{
    Create("PENTAGON");
    ScratchFolder folder("ide-folder");
    folder.File("HELLO.TXT", "hello");
    ASSERT_TRUE(Insert("ide0.master", Utf8(folder.Path())).Ok());
    const auto info = Manager().Info("ide0.master");
    EXPECT_EQ(info->access, AccessMode::Session) << "a folder is never written";
    EXPECT_EQ(info->format, "folder-fat16");

    // WRITE SECTORS lands in the session, the folder is untouched
    Channel().WriteRegister(DeviceHead, 0xE0);
    Channel().WriteRegister(SectorCount, 1);
    Channel().WriteRegister(SectorNumber, 100);
    Channel().WriteRegister(CylinderLow, 0);
    Channel().WriteRegister(CylinderHigh, 0);
    Channel().WriteRegister(StatusCommand, Command::WriteSectors);
    for (int i = 0; i < 256; i++)
        Channel().WriteData(0x4242);
    EXPECT_EQ(Channel().ReadRegister(StatusCommand), Status::DRDY | Status::DSC);
    Manager().ApplyPending();
    EXPECT_TRUE(Manager().Info("ide0.master")->dirty);
}

TEST_F(IdeController_Test, MachinesWithoutIdeHaveNoUnits)
{
    Create("48K");
    ASSERT_NE(_context->pIdeController, nullptr);
    EXPECT_FALSE(_context->pIdeController->Enabled());
    EXPECT_FALSE(Manager().HasSlot("ide0.master"));
    EXPECT_FALSE(Manager().HasSlot("ide0.slave"));
}

namespace
{
    struct MachineBoard
    {
        const char* model;
        IDE_SCHEME scheme;
        uint16_t statusPort;  ///< the board's status register port
    };

    /// Put the machine's decoder into the state its board answers in
    void OpenGate(EmulatorContext& context, IDE_SCHEME scheme)
    {
        EmulatorState& state = context.emulatorState;
        switch (scheme)
        {
            case IDE_PROFI:
                state.pDFFD |= 0x20;  // CP/M
                state.p7FFD |= 0x10;  // ROM14: EXT mode
                break;
            case IDE_ATM:
                state.flags |= CF_DOSPORTS | CF_TRDOS;
                break;
            case IDE_NEMO:
                state.flags &= static_cast<uint8_t>(~(CF_DOSPORTS | CF_TRDOS));
                break;
            default:
                break;
        }
    }
}  // namespace

/// Every shipped machine with an IDE board: a disk inserted through the media
/// manager answers through the machine's own port decoder, and the board takes
/// none of the ports the machine's port map documents
TEST(IdeMachines_Test, BoardsAnswerThroughTheMachineDecoders)
{
    for (const MachineBoard& m : {MachineBoard{"PENTAGON", IDE_NEMO, 0x00F0}, MachineBoard{"ATM3", IDE_NEMO_DIVIDE, 0x00F0},
                                  MachineBoard{"PROFI", IDE_PROFI, 0x07CB}, MachineBoard{"ATM710", IDE_ATM, 0xFEEF}})
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(m.model, LoggerLevel::LogError);
        ASSERT_NE(emulator, nullptr) << m.model;
        EmulatorContext& context = *emulator->GetContext();
        ASSERT_EQ(context.pIdeController->Scheme(), m.scheme) << m.model;

        ScratchFolder folder("ide-machine");
        MediaSource source;
        source.path = Utf8(folder.File("hdd.img", std::string(1024 * 512, '\0')));
        InsertOptions options;
        options.immediate = true;
        ASSERT_TRUE(context.pMediaManager->Insert("ide0.master", source, options).Ok()) << m.model;

        OpenGate(context, m.scheme);
        EXPECT_EQ(context.pPortDecoder->DecodePortIn(m.statusPort, 0), Status::DRDY | Status::DSC) << m.model;

        // The machine's own ports stay the machine's, whatever the gate
        IdeAdapter& adapter = context.pPortDecoder->GetIdeAdapter();
        for (const PortMapEntry& row : context.pPortDecoder->getPortMapEntries())
        {
            for (bool dos : {false, true})
            {
                IdeAdapter::Gate gate;
                gate.dosPorts = dos;
                gate.profiExt = true;
                uint8_t value = 0;
                // ATM Turbo 2+: reading the #7FFD class is the board's IDE / DAC status by design
                // (the write stays paging); nothing else may be taken
                const bool atmStatusRead = m.scheme == IDE_ATM && (row.port & 0x8202) == 0x0200;
                if (!atmStatusRead)
                    EXPECT_FALSE(adapter.In(row.port, gate, value))
                        << m.model << ": the IDE board takes the read of #" << std::hex << row.port << " (" << row.device << ")";
                EXPECT_FALSE(adapter.Out(row.port, gate, 0))
                    << m.model << ": the IDE board takes the write of #" << std::hex << row.port << " (" << row.device << ")";
            }
        }
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
    MessageCenter::DisposeDefaultMessageCenter();
}

/// device=cdrom / device=disk on insert: the unit's drive changes (only while
/// it is empty); the other unit keeps its disk; the aliases follow
TEST(IdeUnitKind_Test, InsertCanSwapTheDrive)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext& context = *emulator->GetContext();
    MediaManager& manager = *context.pMediaManager;
    ScratchFolder folder("ide-unit-kind");
    const std::string image = Utf8(folder.File("hdd.img", std::string(256 * 512, '\0')));
    const std::string second = Utf8(folder.File("hdd2.img", std::string(256 * 512, '\0')));
    std::string cd(40 * 2048, '\0');
    std::memcpy(cd.data() + 16 * 2048, "\x01" "CD001", 6);
    const std::string iso = Utf8(folder.File("disc.iso", cd));

    MediaControl control(&context);
    auto run = [&control](const std::string& verb, const std::string& slot, const std::string& path,
                          std::map<std::string, std::string> options = {}) {
        MediaRequest request;
        request.verb = verb;
        request.selector = slot;
        request.path = path;
        request.options = std::move(options);
        return control.Execute(request);
    };

    ASSERT_TRUE(run("insert", "ide0.master", image).result.Ok());
    EXPECT_EQ(run("insert", "ide0.slave", iso).result.error, MediaError::KindMismatch);
    const MediaReply cdInsert = run("insert", "ide0.slave", iso, {{"device", "cdrom"}});
    ASSERT_TRUE(cdInsert.result.Ok()) << cdInsert.result.message;
    EXPECT_EQ(manager.Info("ide0.slave")->descriptor.kind, MediaKind::Optical);
    EXPECT_EQ(context.config.ide[1].cd, 1) << "the machine's config follows";
    std::string id;
    ASSERT_TRUE(MediaControl::ResolveSelector(manager, "cd", id).Ok());
    EXPECT_EQ(id, "ide0.slave");
    EXPECT_TRUE(manager.Info("ide0.master")->present) << "the master kept its disk";

    // A drive with a disc in it stays
    EXPECT_EQ(run("insert", "ide0.slave", second, {{"device", "disk"}}).result.error, MediaError::BadRequest);
    ASSERT_TRUE(run("eject", "ide0.slave", "").result.Ok());
    ASSERT_TRUE(run("insert", "ide0.slave", second, {{"device", "disk"}}).result.Ok());
    EXPECT_EQ(manager.Info("ide0.slave")->descriptor.kind, MediaKind::Block);
    EXPECT_FALSE(MediaControl::ResolveSelector(manager, "cd", id).Ok());

    EXPECT_EQ(run("insert", "fdd.a", image, {{"device", "cdrom"}}).result.error, MediaError::BadRequest)
        << "only IDE units change drives";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// The slot's write-protect switch changes only what WRITE answers: a read in
/// flight goes on. A swap waits only for a block half way through the data
/// register, not for a transfer the guest left between blocks
TEST_F(IdeController_Test, ProtectSwitchAndBusyLeaveTransfersAlone)
{
    Create("PENTAGON");
    ScratchFolder folder("ide-protect");
    std::string disk(256 * 512, '\0');
    for (size_t i = 0; i < disk.size(); i++)
        disk[i] = static_cast<char>(i / 512);
    ASSERT_TRUE(Insert("ide0.master", Utf8(folder.File("hdd.img", disk))).Ok());
    IdeUnitSlot* slot = _context->pIdeController->Slot(0);

    Channel().WriteRegister(DeviceHead, 0xE0);
    Channel().WriteRegister(SectorCount, 3);
    Channel().WriteRegister(SectorNumber, 4);
    Channel().WriteRegister(CylinderLow, 0);
    Channel().WriteRegister(CylinderHigh, 0);
    Channel().WriteRegister(StatusCommand, Command::ReadSectors);
    for (int i = 0; i < 10; i++)
        Channel().ReadData();
    EXPECT_TRUE(slot->IsBusy()) << "half a sector moved";

    ASSERT_TRUE(Manager().SetWriteProtect("ide0.master", true).Ok());
    EXPECT_EQ(Channel().Unit(0)->State().bufferPos, 20) << "the transfer went on untouched";
    for (int i = 10; i < 256; i++)
        Channel().ReadData();
    EXPECT_FALSE(slot->IsBusy()) << "between blocks: a swap may go ahead";
    EXPECT_EQ(Channel().ReadData(), 0x0505) << "sector 5 follows";

    Channel().WriteRegister(SectorCount, 1);
    Channel().WriteRegister(StatusCommand, Command::WriteSectors);
    EXPECT_EQ(Channel().ReadRegister(ErrorFeatures), Error::ABRT) << "the switch is on";
    ASSERT_TRUE(Manager().SetWriteProtect("ide0.master", false).Ok());
    Channel().WriteRegister(StatusCommand, Command::WriteSectors);
    EXPECT_TRUE(Channel().ReadRegister(StatusCommand) & Status::DRQ);
}

/// Sprinter (tdd-storage §1): two channels, four units, the slots ide0.master ... ide1.slave; every unit an empty
/// hard disk by default (ide0.slave has no device: Q5); the channel the adapter selects is the one that answers
TEST_F(IdeController_Test, SprinterHasTwoChannels)
{
    Create("SPRINTER");
    IdeController& ide = *_context->pIdeController;
    ASSERT_EQ(ide.Scheme(), IDE_SPRINTER) << "configs/sprinter: [HDD] Scheme=SPRINTER";
    EXPECT_EQ(ide.ChannelCount(), 2);

    const char* const ids[4] = {"ide0.master", "ide0.slave", "ide1.master", "ide1.slave"};
    const char* const labels[4] = {"IDE primary master (hard disk)", "IDE primary slave (hard disk)",
                                   "IDE secondary master (hard disk)", "IDE secondary slave (hard disk)"};
    for (int unit = 0; unit < 4; unit++)
    {
        const auto info = Manager().Info(ids[unit]);
        ASSERT_TRUE(info.has_value()) << ids[unit];
        EXPECT_EQ(info->descriptor.kind, MediaKind::Block) << ids[unit];
        EXPECT_EQ(info->descriptor.label, labels[unit]);
        EXPECT_FALSE(info->present) << ids[unit];
        EXPECT_EQ(IdeController::UnitForSlot(ids[unit]), unit);
        EXPECT_EQ(ide.Slot(unit)->Descriptor().id, ids[unit]);
    }
    std::string id;
    ASSERT_TRUE(MediaControl::ResolveSelector(Manager(), "hd", id).Ok());
    EXPECT_EQ(id, "ide0.master");
    ASSERT_TRUE(MediaControl::ResolveSelector(Manager(), "tag:ide+sprinter+secondary+slave", id).Ok());
    EXPECT_EQ(id, "ide1.slave");

    // A disk on the secondary master answers only once #2A selects that channel
    ScratchFolder folder("ide-sprinter");
    std::string disk(512 * 512, '\0');
    std::memcpy(disk.data() + 3 * 512, "\x34\x12", 2);
    ASSERT_TRUE(Insert("ide1.master", Utf8(folder.File("ide1.img", disk))).Ok());
    IdeAdapter& adapter = _context->pPortDecoder->GetIdeAdapter();
    EXPECT_EQ(adapter.SprinterIn(0x27, 0x4053), 0x7F) << "primary: no drive, DD7 pulled down (BSY = 0)";
    EXPECT_EQ(adapter.SprinterIn(0x28, 0x4054), 0x7F) << "alternate status as well";
    adapter.SprinterOut(0x2A, 0x01BC, 0x01);
    EXPECT_EQ(adapter.SprinterIn(0x27, 0x4053), Status::DRDY | Status::DSC);
    for (const auto& [code, value] : std::vector<std::pair<uint8_t, uint8_t>>{
             {0x26, 0xE0}, {0x22, 1}, {0x23, 3}, {0x24, 0}, {0x25, 0}, {0x27, Command::ReadSectors}})
        adapter.SprinterOut(code, static_cast<uint16_t>(0x0150 | (code & 7)), value);
    EXPECT_EQ(adapter.SprinterIn(0x20, 0x0050), 0x34);
    EXPECT_EQ(adapter.SprinterIn(0x20, 0x0150), 0x12);

    // The secondary slave can become a CD drive (the media verb's device=cdrom)
    std::string error;
    ASSERT_TRUE(ide.SetUnitKind(3, true, &error)) << error;
    EXPECT_EQ(Manager().Info("ide1.slave")->descriptor.kind, MediaKind::Optical);
    EXPECT_EQ(Manager().Info("ide1.slave")->descriptor.label, "IDE secondary slave (CD-ROM)");
    ASSERT_TRUE(MediaControl::ResolveSelector(Manager(), "cd", id).Ok());
    EXPECT_EQ(id, "ide1.slave");
    EXPECT_EQ(_context->config.ide[3].cd, 1);

    // The empty CD unit becomes a CompactFlash card on an IDE adapter (device=cf): a disk unit again, tagged "cf"
    ASSERT_TRUE(ide.SetUnitKind(3, IdeController::UnitKind::CompactFlash, &error)) << error;
    EXPECT_EQ(ide.KindOf(3), IdeController::UnitKind::CompactFlash);
    EXPECT_EQ(Manager().Info("ide1.slave")->descriptor.kind, MediaKind::Block);
    EXPECT_EQ(Manager().Info("ide1.slave")->descriptor.label, "IDE secondary slave (CompactFlash)");
    const std::vector<std::string> tags = Manager().Info("ide1.slave")->descriptor.tags;
    EXPECT_NE(std::find(tags.begin(), tags.end(), "cf"), tags.end());
    EXPECT_EQ(_context->config.ide[3].cf, 1);
    EXPECT_EQ(_context->config.ide[3].cd, 0);
    ASSERT_TRUE(ide.SetUnitKind(3, IdeController::UnitKind::Disk, &error)) << error;
    EXPECT_EQ(_context->config.ide[3].cf, 0) << "back to a hard disk";
    EXPECT_EQ(Manager().Info("ide1.slave")->descriptor.label, "IDE secondary slave (hard disk)");
}

/// Single-channel boards keep one channel and no ide1 slots
TEST_F(IdeController_Test, OneChannelBoardsHaveNoSecondChannel)
{
    Create("PENTAGON");
    EXPECT_EQ(_context->pIdeController->ChannelCount(), 1);
    EXPECT_FALSE(Manager().Info("ide1.master").has_value());
    EXPECT_EQ(_context->pIdeController->Slot(2), nullptr);
    EXPECT_EQ(&_context->pIdeController->Channel(1), &_context->pIdeController->Channel(0)) << "out of range: channel 0";
    std::string error;
    EXPECT_FALSE(_context->pIdeController->SetUnitKind(2, true, &error));
}

/// region <CD audio on every board (PLAN #83)>

namespace
{
    struct CdBoard
    {
        const char* model;
        IDE_SCHEME scheme;  ///< IDE_NONE: as the machine's config ships it
        int unit;           ///< the unit made a CD drive
    };

    std::ostream& operator<<(std::ostream& out, const CdBoard& board)
    {
        return out << board.model << " unit " << board.unit;
    }

    class IdeControllerCd_Test : public ::testing::TestWithParam<CdBoard>
    {
    };
}  // namespace

/// Every board that hosts an ATAPI unit plays CD audio: the unit made a CD drive, a disc with
/// audio tracks, PLAY AUDIO MSF through the channel as a driver sends it, the line output on the
/// drive's mixer row, the head on the board's frame clock, the CdDrive blob under TTD. The guest
/// side of each board's ports is covered by IdeAdapter_Test and, with real players, by
/// ZXEvoErs_Test.NedoOsCdplayPlaysAudioTracks (Nemo / DivIDE ports) and
/// ATM710NedoOsCdplay_Test (ATM ports)
TEST_P(IdeControllerCd_Test, PlaysAudioOnTheBoard)
{
    const CdBoard board = GetParam();
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(board.model, LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    struct Cleanup
    {
        Emulator* emulator;
        ~Cleanup()
        {
            EmulatorTestHelper::CleanupEmulator(emulator);
            MessageCenter::DisposeDefaultMessageCenter();
        }
    } cleanup{emulator};
    if (board.scheme != IDE_NONE)
    {
        context->config.ide_scheme = board.scheme;
        context->pCore->RefitIde();
    }
    IdeController& ide = *context->pIdeController;
    ASSERT_TRUE(ide.Enabled()) << board;
    std::string error;
    ASSERT_TRUE(ide.SetUnitKind(board.unit, true, &error)) << error;
    context->pSoundManager->setCoreRatePin(44100);

    ScratchFolder folder("ide-cd-board");
    MediaSource source;
    source.path = cdtest::WriteMusicDisc(folder.Path(), 1, 2, 16, cdtest::MusicLayout::Mixed);
    InsertOptions options;
    options.immediate = true;
    const std::string slot = IdeUnitSlot::IdFor(board.unit / AtaChannel::kUnits, board.unit % AtaChannel::kUnits);
    const MediaResult inserted = context->pMediaManager->Insert(slot, source, options);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    emulator->RunNFrames(1);

    // The driver's side: select the unit, clear the unit attention, PLAY AUDIO MSF 00:04:16 - 00:06:16 (track 2)
    AtaChannel& channel = ide.Channel(board.unit / AtaChannel::kUnits);
    auto packet = [&channel, &board](std::vector<uint8_t> cdb) {
        cdb.resize(12, 0);
        channel.WriteRegister(DeviceHead, (board.unit % 2) ? 0xB0 : 0xA0);
        channel.WriteRegister(ErrorFeatures, 0);
        channel.WriteRegister(CylinderLow, 0xFE);
        channel.WriteRegister(CylinderHigh, 0xFF);
        channel.WriteRegister(StatusCommand, Command::Packet);
        for (size_t i = 0; i < 12; i += 2)
            channel.WriteData(static_cast<uint16_t>(cdb[i] | (cdb[i + 1] << 8)));
    };
    packet({0x00});
    packet({0x03, 0, 0, 0, 18});
    for (int i = 0; i < 9; i++)
        channel.ReadData();
    packet({0x47, 0, 0, 0, 4, 16, 0, 6, 16});
    ASSERT_EQ(channel.ReadRegister(StatusCommand) & Status::ERR, 0) << board;

    const AudioSourceType row = CdAudioSourceFor(board.unit);
    CdAudioPlayer* player = ide.CdAudio(board.unit);
    ASSERT_NE(player, nullptr);
    emulator->RunNFrames(10);
    EXPECT_EQ(player->PeekStatus(), CdAudioStatus::Playing) << board;
    const int16_t* out = context->pSoundManager->deviceBuffer(row);
    ASSERT_NE(out, nullptr) << board << ": the drive's mixer row";
    EXPECT_TRUE(player->HadSoundLastFrame()) << board;
    ASSERT_NE(context->pSoundManager->device(row), nullptr);
    EXPECT_EQ(context->pSoundManager->device(row)->name, "CD " + slot);
    // Ten frames of the board's frame length: the head moved exactly that much emulated time
    EXPECT_EQ(player->PeekHeadSample() / 588, 166u + 10ull * context->config.frame * 44100 / 3500000 / 588) << board;

    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());
    EXPECT_TRUE(context->pTimeTravelManager->GetPeripheralRegistry().IsRegistered(ttd::PeripheralId::CdDrive)) << board;
    context->pTimeTravelManager->StopRecording();

    // The guest ejects (START STOP UNIT LoEj): the play stops and, at the frame boundary, the slot is
    // empty as after a user's eject - on every board, through the shared media manager
    packet({0x1B, 0, 0, 0, 0x02});
    ASSERT_EQ(channel.ReadRegister(StatusCommand) & Status::ERR, 0) << board;
    EXPECT_EQ(player->PeekStatus(), CdAudioStatus::Idle) << board;
    emulator->RunNFrames(1);
    EXPECT_FALSE(context->pMediaManager->Info(slot)->present) << board << ": the guest's eject emptied the slot";
}

INSTANTIATE_TEST_SUITE_P(Boards, IdeControllerCd_Test,
                         ::testing::Values(CdBoard{"PENTAGON", IDE_NONE, 1},           // Nemo
                                           CdBoard{"PENTAGON", IDE_NEMO_A8, 0},        // Nemo A8
                                           CdBoard{"PENTAGON", IDE_DIVIDE, 1},         // DivIDE ports
                                           CdBoard{"ATM3", IDE_NONE, 1},               // ZX-Evo, Nemo / DivIDE, CD1=1 shipped
                                           CdBoard{"ATM710", IDE_NONE, 1},             // ATM
                                           CdBoard{"ATM450", IDE_NONE, 1},             // ATM
                                           CdBoard{"PROFI", IDE_NONE, 1},              // Profi
                                           CdBoard{"SCORPION", IDE_SMUC, 1},           // SMUC
                                           CdBoard{"TSL", IDE_NONE, 1},                // TS-Conf, Nemo / DivIDE
                                           CdBoard{"SPRINTER", IDE_NONE, 3}),          // Sprinter, secondary slave
                         [](const ::testing::TestParamInfo<CdBoard>& info) {
                             std::string name = std::string(info.param.model) + "_" + Config::IdeSchemeName(info.param.scheme) +
                                                "_unit" + std::to_string(info.param.unit);
                             for (char& c : name)
                                 if (!std::isalnum(static_cast<unsigned char>(c)))
                                     c = '_';
                             return name;
                         });

/// endregion </CD audio on every board>

/// region <The guest ejects the disc>

namespace
{
    /// A ZX-Evo (ATM3: its CD drive on the IDE slave) with the Enhanced CD test disc
    class IdeControllerCdEject_Test : public ::testing::Test
    {
    protected:
        Emulator* _emulator = nullptr;
        EmulatorContext* _context = nullptr;
        std::unique_ptr<ScratchFolder> _folder;
        MediaSource _source;

        void SetUp() override
        {
            _emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
            ASSERT_NE(_emulator, nullptr);
            _context = _emulator->GetContext();
            _folder = std::make_unique<ScratchFolder>("ide-cd-eject");
            _source.path = cdtest::WriteMusicDisc(_folder->Path(), 2, 4, 300);
            ASSERT_TRUE(Insert());
            Packet({0x00});  // the unit attention of the insert
        }
        void TearDown() override
        {
            _folder.reset();
            if (_emulator)
                EmulatorTestHelper::CleanupEmulator(_emulator);
            MessageCenter::DisposeDefaultMessageCenter();
        }

        bool Insert()
        {
            InsertOptions now;
            now.immediate = true;
            const bool ok = _context->pMediaManager->Insert("ide0.slave", _source, now).Ok();
            _emulator->RunNFrames(1);
            return ok;
        }
        AtaChannel& Channel() { return _context->pIdeController->Channel(); }
        AtapiCdrom& Cd() { return *static_cast<AtapiCdrom*>(Channel().Unit(1)); }
        bool Present() { return _context->pMediaManager->Info("ide0.slave")->present; }

        /// A packet to the slave as a driver sends it; true when it completed without CHECK CONDITION
        bool Packet(std::vector<uint8_t> cdb)
        {
            cdb.resize(12, 0);
            Channel().WriteRegister(DeviceHead, 0xB0);
            Channel().WriteRegister(ErrorFeatures, 0);
            Channel().WriteRegister(CylinderLow, 0xFE);
            Channel().WriteRegister(CylinderHigh, 0xFF);
            Channel().WriteRegister(StatusCommand, Command::Packet);
            for (size_t i = 0; i < 12; i += 2)
                Channel().WriteData(static_cast<uint16_t>(cdb[i] | (cdb[i + 1] << 8)));
            return (Channel().ReadRegister(StatusCommand) & Status::ERR) == 0;
        }
        /// REQUEST SENSE: key, ASC, ASCQ
        std::vector<uint8_t> Sense()
        {
            Packet({0x03, 0, 0, 0, 18});
            std::vector<uint8_t> data;
            for (int i = 0; i < 9; i++)
            {
                const uint16_t w = Channel().ReadData();
                data.push_back(static_cast<uint8_t>(w));
                data.push_back(static_cast<uint8_t>(w >> 8));
            }
            return {data[2], data[12], data[13]};
        }
    };
}  // namespace

TEST_F(IdeControllerCdEject_Test, GuestEjectEmptiesTheSlotLikeAUserEject)
{
    // The Sprinter CDPLAYER.FLX sequence: Play (PLAY AUDIO MSF 00:02:00 - 80:00:74), Eject (1B ... 02)
    const uint64_t revision = _context->pMediaManager->Revision();
    ASSERT_TRUE(Packet({0x47, 0x00, 0x00, 0x00, 0x02, 0x00, 0x50, 0x00, 0x4A, 0x00, 0x00, 0x00}));
    _emulator->RunNFrames(2);
    ASSERT_EQ(Cd().Audio().PeekStatus(), CdAudioStatus::Playing);
    ASSERT_TRUE(Packet({0x1B, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));
    EXPECT_EQ(Cd().Audio().PeekStatus(), CdAudioStatus::Idle);
    EXPECT_TRUE(Present()) << "applied at the frame boundary, not inside the command";
    _emulator->RunNFrames(1);
    EXPECT_FALSE(Present()) << "the slot is empty";
    EXPECT_GT(_context->pMediaManager->Revision(), revision) << "every surface sees the change";
    EXPECT_FALSE(Cd().HasDisc());
    EXPECT_TRUE(Cd().TrayOpen()) << "the tray stays open";
    MediaRequest info{"info", "ide0.slave", "", {}};
    const MediaReply reply = MediaControl(_context).Execute(info);
    ASSERT_TRUE(reply.result.Ok());
    EXPECT_EQ(reply.body.find("info")->find("state")->s, "empty");

    // No unit attention for an eject the guest asked for: TEST UNIT READY says NOT READY, tray open
    EXPECT_FALSE(Packet({0x00}));
    EXPECT_EQ(Sense(), (std::vector<uint8_t>{0x02, 0x3A, 0x02}));

    // Load with no disc: the tray closes, still no medium (MEDIUM NOT PRESENT - TRAY CLOSED)
    EXPECT_TRUE(Packet({0x1B, 0, 0, 0, 0x03}));
    EXPECT_FALSE(Cd().TrayOpen());
    EXPECT_FALSE(Packet({0x00}));
    EXPECT_EQ(Sense(), (std::vector<uint8_t>{0x02, 0x3A, 0x01}));

    // Eject again (the tray opens, no disc to take out) and insert from outside: the tray closes with it
    EXPECT_TRUE(Packet({0x1B, 0, 0, 0, 0x02}));
    EXPECT_TRUE(Cd().TrayOpen());
    ASSERT_TRUE(Insert());
    EXPECT_TRUE(Present());
    EXPECT_FALSE(Cd().TrayOpen());
    EXPECT_FALSE(Packet({0x00}));
    EXPECT_EQ(Sense(), (std::vector<uint8_t>{0x06, 0x28, 0x00})) << "a new disc: UNIT ATTENTION";
    EXPECT_TRUE(Packet({0x00}));
}

TEST_F(IdeControllerCdEject_Test, PreventAllowKeepsTheDisc)
{
    ASSERT_TRUE(Packet({0x1E, 0, 0, 0, 0x01}));  // PREVENT MEDIUM REMOVAL
    EXPECT_FALSE(Packet({0x1B, 0, 0, 0, 0x02}));
    EXPECT_EQ(Sense(), (std::vector<uint8_t>{0x05, 0x53, 0x02}));
    _emulator->RunNFrames(2);
    EXPECT_TRUE(Present()) << "a refused eject leaves the slot alone";
    EXPECT_FALSE(Cd().TrayOpen());
    ASSERT_TRUE(Packet({0x1E, 0, 0, 0, 0x00}));
    ASSERT_TRUE(Packet({0x1B, 0, 0, 0, 0x02}));
    _emulator->RunNFrames(1);
    EXPECT_FALSE(Present());
}

TEST_F(IdeControllerCdEject_Test, UserEjectStillReportsAndOpensTheTray)
{
    ASSERT_TRUE(_context->pMediaManager->Eject("ide0.slave").Ok());
    _emulator->RunNFrames(1);
    EXPECT_TRUE(Cd().TrayOpen());
    EXPECT_FALSE(Packet({0x00}));
    EXPECT_EQ(Sense()[0], 0x06) << "taken out from outside: reported as a change, as before";
    EXPECT_FALSE(Packet({0x00}));
    EXPECT_EQ(Sense(), (std::vector<uint8_t>{0x02, 0x3A, 0x02}));
}

/// endregion </The guest ejects the disc>
