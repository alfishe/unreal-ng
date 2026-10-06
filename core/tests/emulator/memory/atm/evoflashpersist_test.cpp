// The ZX-Evo's saved flash (EvoFlash persistence, docs/inprogress/2026-09-27-tsconf/tdd-evo-flash.md section 6):
// what programs flash is saved to zxevo-flash-<machine>-<ROM image SHA-256>.rom in the settings folder and laid
// over the ROM image when the machine is created again; another ROM image never takes it; discard deletes it; the
// file is written after a quiet second and when the machine goes away, never while a TTD replay owns the machine.
// Each test creates real machines from the shipped configs (ROM boot not needed): about 20-60 ms each.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/atm/evoflash.h"
#include "emulator/memory/atm/evoflashrequest.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"

namespace
{
namespace fs = std::filesystem;

/// Persistence on, into a folder of this test's own (core-tests keep it off: main.cpp)
class PersistScope
{
public:
    explicit PersistScope(const std::string& name)
        : _previous(FileHelper::GetWritablePath())
        , _folder(TestPathHelper::GetUniqueTestScratchPath(name))
    {
        std::error_code ec;
        fs::remove_all(FileHelper::ToFsPath(_folder), ec);
        fs::create_directories(FileHelper::ToFsPath(_folder), ec);
        FileHelper::SetWritablePathOverride(_folder);
        EvoFlash::SetPersistenceAllowed(true);
    }
    ~PersistScope()
    {
        EvoFlash::SetPersistenceAllowed(false);
        FileHelper::SetWritablePathOverride(_previous);
        std::error_code ec;
        fs::remove_all(FileHelper::ToFsPath(_folder), ec);
    }
    const std::string& Folder() const { return _folder; }
    std::vector<std::string> Files() const
    {
        std::vector<std::string> names;
        std::error_code ec;
        for (fs::directory_iterator it(FileHelper::ToFsPath(_folder), ec), end; !ec && it != end; it.increment(ec))
            names.push_back(FileHelper::FromFsPath(it->path().filename()));
        return names;
    }

private:
    std::string _previous;
    std::string _folder;
};

struct Machine
{
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;
    Memory* memory = nullptr;
    EvoFlash* flash = nullptr;

    explicit Machine(const char* model)
    {
        emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        if (!emulator)
            return;
        context = emulator->GetContext();
        memory = context->pMemory;
        flash = context->pPortDecoder ? context->pPortDecoder->GetEvoFlash() : nullptr;
    }
    ~Machine()
    {
        if (emulator)
            EmulatorTestHelper::CleanupEmulator(emulator);
    }
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;

    uint8_t Rom(uint32_t page, uint32_t offset) const { return memory->ROMBase()[page * PAGE_SIZE + offset]; }
    void Out(uint16_t port, uint8_t value) { context->pPortDecoder->DecodePortOut(port, value, 0x0000); }
    void Poke(uint16_t addr, uint8_t value)
    {
        Z80* z80 = context->pCore->GetZ80();
        (memory->*(z80->MemIf->MemoryWrite))(addr, value);
    }

    /// TS-Conf: MEM_CONFIG #06, the program sequence through window 0, then 10 us pass and the chip completes
    void ProgramTsConf(uint8_t page, uint16_t offset, uint8_t value)
    {
        Out(0x21AF, 0x06);
        Out(0x10AF, 0x00);
        Poke(0x0555, 0xAA);
        Poke(0x02AA, 0x55);
        Poke(0x0555, 0xA0);
        Out(0x10AF, page);
        Poke(offset, value);
        context->emulatorState.t_states += 100;
        flash->Sync();
        Out(0x21AF, 0x04);
    }
};

/// A byte with a bit to clear in ROM page 8 and the value that clears its lowest set bit
uint16_t ByteToProgram(const Machine& m, uint8_t& original, uint8_t& value)
{
    uint16_t offset = 0x0100;
    while (m.Rom(8, offset) == 0x00)
        ++offset;
    original = m.Rom(8, offset);
    value = static_cast<uint8_t>(original & (original - 1));
    return offset;
}
}  // namespace

/// A flashed TS-Conf stays flashed: the byte is saved when the machine goes away and is there in a new machine;
/// discard deletes the file and the next machine boots the shipped image
TEST(EvoFlashPersist_Test, AFlashedByteSurvivesTheMachineAndDiscardUndoesIt)
{
    PersistScope scope("evoflash-roundtrip");
    uint8_t original = 0;
    uint8_t value = 0;
    uint16_t offset = 0;
    std::string path;
    {
        Machine m("TSL");
        ASSERT_NE(m.flash, nullptr);
        offset = ByteToProgram(m, original, value);
        m.ProgramTsConf(8, offset, value);
        ASSERT_EQ(m.Rom(8, offset), value);
        EvoFlash::PersistStatus st;
        ASSERT_TRUE(EvoFlashGetStatus(m.context, st));
        EXPECT_EQ(st.machine, "tsconf");
        EXPECT_TRUE(st.unsaved);
        EXPECT_FALSE(st.fileExists) << "not written before the quiet time or the machine's end";
        path = st.path;
        EXPECT_EQ(FileHelper::FromFsPath(FileHelper::ToFsPath(path).filename()),
                  "zxevo-flash-tsconf-" + st.baseDigest + ".rom");
    }
    ASSERT_TRUE(FileHelper::FileExists(path)) << "saved when the machine went away";
    EXPECT_EQ(FileHelper::GetFileSize(path), 512u * 1024u) << "the whole flash";

    {
        Machine m("TSL");
        EXPECT_EQ(m.Rom(8, offset), value) << "the flashed byte is back";
        EvoFlash::PersistStatus st;
        ASSERT_TRUE(EvoFlashGetStatus(m.context, st));
        EXPECT_TRUE(st.loadedFromFile);
        EXPECT_EQ(st.path, path) << "the same ROM image, the same file";
        EXPECT_FALSE(st.unsaved);

        EXPECT_EQ(EvoFlashRequestDiscard(m.context), EvoFlashResult::Done);
        EXPECT_FALSE(FileHelper::FileExists(path));
        EXPECT_TRUE(m.emulator->RomReloadPending()) << "the shipped image returns at the next reset";
    }
    EXPECT_FALSE(FileHelper::FileExists(path)) << "a discarded flash is not saved again at the machine's end";

    Machine m("TSL");
    EXPECT_EQ(m.Rom(8, offset), original) << "the shipped image";
}

/// TS-Conf and ATM3 keep separate files; a file flashed over another ROM image is not used and is reported
TEST(EvoFlashPersist_Test, AnotherRomImageNeverTakesTheFile)
{
    PersistScope scope("evoflash-mismatch");
    std::string tsconfPath;
    uint8_t original = 0;
    uint8_t value = 0;
    uint16_t offset = 0;
    {
        Machine m("TSL");
        offset = ByteToProgram(m, original, value);
        m.ProgramTsConf(8, offset, value);
        EXPECT_EQ(EvoFlashRequestSave(m.context), EvoFlashResult::Done);
        EvoFlash::PersistStatus st;
        ASSERT_TRUE(EvoFlashGetStatus(m.context, st));
        EXPECT_TRUE(st.fileExists);
        tsconfPath = st.path;
    }

    // The same file name pattern, another base hash: as if zxevo.rom was replaced after flashing
    const std::string stale = FileHelper::PathCombine(scope.Folder(), "zxevo-flash-tsconf-" + std::string(64, 'a') + ".rom");
    std::vector<uint8_t> image(512 * 1024, 0x00);
    ASSERT_TRUE(FileHelper::SaveBufferToFile(stale, image.data(), image.size()));
    std::error_code ec;
    fs::remove(FileHelper::ToFsPath(tsconfPath), ec);

    {
        Machine m("TSL");
        EXPECT_EQ(m.Rom(8, offset), original) << "the old image's flash is not mixed into this one";
        EvoFlash::PersistStatus st;
        ASSERT_TRUE(EvoFlashGetStatus(m.context, st));
        EXPECT_FALSE(st.loadedFromFile);
        ASSERT_EQ(st.otherImageFiles.size(), 1u);
        EXPECT_EQ(st.otherImageFiles[0], stale);
    }

    // A truncated file for this image is ignored too
    {
        Machine probe("TSL");
        EvoFlash::PersistStatus st;
        ASSERT_TRUE(EvoFlashGetStatus(probe.context, st));
        tsconfPath = st.path;
    }
    ASSERT_TRUE(FileHelper::SaveBufferToFile(tsconfPath, image.data(), 1000));
    {
        Machine m("TSL");
        EXPECT_EQ(m.Rom(8, offset), original);
    }

    // The ATM3's file is its own
    Machine atm3("ATM3");
    ASSERT_NE(atm3.flash, nullptr);
    EvoFlash::PersistStatus st;
    ASSERT_TRUE(EvoFlashGetStatus(atm3.context, st));
    EXPECT_EQ(st.machine, "atm3");
    EXPECT_NE(FileHelper::FromFsPath(FileHelper::ToFsPath(st.path).filename()).find("zxevo-flash-atm3-"), std::string::npos);
    EXPECT_TRUE(st.otherImageFiles.empty()) << "TS-Conf's files are not the ATM3's";
    EXPECT_FALSE(st.loadedFromFile);
}

/// Debounce: the file is written once the chip has been quiet for kSaveQuietFrames frames after a change
TEST(EvoFlashPersist_Test, SavedAfterAQuietSecond)
{
    PersistScope scope("evoflash-debounce");
    Machine m("TSL");
    uint8_t original = 0;
    uint8_t value = 0;
    const uint16_t offset = ByteToProgram(m, original, value);
    m.ProgramTsConf(8, offset, value);

    EvoFlash::PersistStatus st;
    ASSERT_TRUE(EvoFlashGetStatus(m.context, st));
    for (uint32_t f = 0; f < EvoFlash::kSaveQuietFrames; ++f)
    {
        m.context->pPortDecoder->OnFrameEnd();
        ASSERT_FALSE(FileHelper::FileExists(st.path)) << "frame " << f;
    }
    m.context->pPortDecoder->OnFrameEnd();
    EXPECT_TRUE(FileHelper::FileExists(st.path)) << "after the quiet frames";
    ASSERT_TRUE(EvoFlashGetStatus(m.context, st));
    EXPECT_FALSE(st.unsaved);
}

/// Inside a TTD replay the session is the source of truth: nothing is written - not by the debounce, not at the
/// machine's end, not on request
TEST(EvoFlashPersist_Test, NothingIsWrittenWhileAReplayOwnsTheMachine)
{
    PersistScope scope("evoflash-replay");
    std::string path;
    {
        Machine m("TSL");
        uint8_t original = 0;
        uint8_t value = 0;
        const uint16_t offset = ByteToProgram(m, original, value);
        m.ProgramTsConf(8, offset, value);
        EvoFlash::PersistStatus st;
        ASSERT_TRUE(EvoFlashGetStatus(m.context, st));
        path = st.path;

        m.context->ttdReplayActive = true;
        for (uint32_t f = 0; f < 2 * EvoFlash::kSaveQuietFrames; ++f)
            m.context->pPortDecoder->OnFrameEnd();
        EXPECT_FALSE(FileHelper::FileExists(path)) << "the debounce";
        EXPECT_EQ(EvoFlashRequestSave(m.context), EvoFlashResult::ReplayOwnsInput);
        EXPECT_EQ(EvoFlashRequestDiscard(m.context), EvoFlashResult::ReplayOwnsInput);
        m.flash->SaveIfUnsaved();
        EXPECT_FALSE(FileHelper::FileExists(path)) << "the machine's end";
    }
    EXPECT_FALSE(FileHelper::FileExists(path));
    EXPECT_TRUE(scope.Files().empty());
}

/// With persistence off (core-tests' default) nothing is read or written; other machines have no flash
TEST(EvoFlashPersist_Test, OffMeansNoFileAndOtherMachinesHaveNone)
{
    {
        Machine m("TSL");
        ASSERT_NE(m.flash, nullptr);
        EvoFlash::PersistStatus st;
        ASSERT_TRUE(EvoFlashGetStatus(m.context, st));
        EXPECT_TRUE(st.path.empty());
        EXPECT_EQ(EvoFlashRequestSave(m.context), EvoFlashResult::Failed);
    }
    Machine pentagon("PENTAGON");
    EvoFlash::PersistStatus st;
    EXPECT_FALSE(EvoFlashGetStatus(pentagon.context, st));
    EXPECT_EQ(EvoFlashRequestSave(pentagon.context), EvoFlashResult::NoFlash);
}
