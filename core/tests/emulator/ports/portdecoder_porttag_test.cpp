#include "stdafx.h"
#include "gtest/gtest.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/memory/rom.h"
#include "emulator/ports/models/portdecoder_pentagon128.h"
#include "emulator/ports/models/portdecoder_profi.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"
#include "emulator/ports/models/portdecoder_spectrum128.h"
#include "emulator/ports/models/portdecoder_spectrum3.h"
#include "emulator/ports/models/portdecoder_spectrum48.h"

/// @file portdecoder_porttag_test.cpp
/// @brief Tagged port registry unit tests (port-tags-paging design §8 row 1,
/// P1-2 Phase 1).
///
/// The §3.2 tag-assignment table of the design is normative here: every
/// static row carries at least one category bit, paging latches carry their
/// Memory/Rom/Screen tags plus the PagingLatch live binding, and the Sound
/// family member queries answer without per-model switches.

namespace
{
/// Row lookup by canonical port (nullptr when the row is absent)
const PortMapEntry* FindEntry(const std::vector<PortMapEntry>& entries, uint16_t port)
{
    for (const PortMapEntry& entry : entries)
    {
        if (entry.port == port)
            return &entry;
    }
    return nullptr;
}
}  // namespace

class PortDecoder_PortTag_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _mouse = new Mouse(_context);
        _context->pMouse = _mouse;  // fitted: Mouse::_present defaults to true
    }

    void TearDown() override
    {
        delete _mouse;
        delete _context;
    }

    EmulatorContext* _context = nullptr;
    Mouse* _mouse = nullptr;
};

/// region <Static row invariants>

TEST_F(PortDecoder_PortTag_Test, EveryStaticRowCarriesACategory)
{
    // All creatable master models: no static row may be tag-less. The only
    // legal tags == 0 row is the legacy registered-peripheral fallback, which
    // none of these fixtures produce (no RegisterPortHandler call).
    struct ModelCase
    {
        MEM_MODEL model;
        const char* name;
    };

    const ModelCase cases[] = {
        {MM_SPECTRUM48,  "48K"},
        {MM_SPECTRUM128, "128K"},
        {MM_PLUS3,       "+3"},
        {MM_PROFI,       "Profi"},
        {MM_SCORP,       "Scorpion"},
        {MM_PENTAGON,    "Pentagon"},
    };

    for (const ModelCase& item : cases)
    {
        _context->config.mem_model = item.model;

        std::unique_ptr<PortDecoder> decoder(PortDecoder::GetPortDecoderForModel(item.model, _context));
        ASSERT_NE(decoder.get(), nullptr) << item.name;

        for (const PortMapEntry& entry : decoder->getPortMapEntries())
        {
            EXPECT_NE(entry.tags & PORT_TAG_CATEGORY_MASK, 0u)
                << item.name << ": row #" << StringHelper::Format("0x%04X", entry.port)
                << " has no category tag";
        }
    }
}

/// endregion </Static row invariants>

/// region <Paging latch bindings (design §3.2 table)>

TEST_F(PortDecoder_PortTag_Test, Spectrum48_NoPagingLatches)
{
    _context->config.mem_model = MM_SPECTRUM48;
    PortDecoder_Spectrum48 decoder(_context);

    EXPECT_TRUE(decoder.GetPagingLatches().empty());
    EXPECT_FALSE(decoder.HasAnyTaggedPort(Tags(PortTag::Memory)));
}

TEST_F(PortDecoder_PortTag_Test, Spectrum128_Single7FFDLatch)
{
    _context->config.mem_model = MM_SPECTRUM128;
    PortDecoder_Spectrum128 decoder(_context);

    const std::vector<PortMapEntry> latches = decoder.GetPagingLatches();
    ASSERT_EQ(latches.size(), 1u);
    EXPECT_EQ(latches[0].port, 0x7FFD);
    EXPECT_EQ(latches[0].latch, PagingLatch::P7FFD);
    EXPECT_EQ(latches[0].tags, Tags(PortTag::Memory) | PortTag::Rom | PortTag::Screen);
}

TEST_F(PortDecoder_PortTag_Test, Plus3_7FFDAnd1FFDLatches)
{
    _context->config.mem_model = MM_PLUS3;
    PortDecoder_Spectrum3 decoder(_context);

    const std::vector<PortMapEntry> latches = decoder.GetPagingLatches();
    ASSERT_EQ(latches.size(), 2u);

    const PortMapEntry* paging = FindEntry(latches, 0x7FFD);
    ASSERT_NE(paging, nullptr);
    EXPECT_EQ(paging->latch, PagingLatch::P7FFD);
    EXPECT_EQ(paging->tags, Tags(PortTag::Memory) | PortTag::Rom | PortTag::Screen);

    const PortMapEntry* special = FindEntry(latches, 0x1FFD);
    ASSERT_NE(special, nullptr);
    EXPECT_EQ(special->latch, PagingLatch::P1FFD);
    EXPECT_EQ(special->tags, Tags(PortTag::Memory) | PortTag::Rom);
}

TEST_F(PortDecoder_PortTag_Test, Profi_DFFDIsMemoryAndScreen)
{
    _context->config.mem_model = MM_PROFI;
    PortDecoder_Profi decoder(_context);

    const std::vector<PortMapEntry> latches = decoder.GetPagingLatches();
    ASSERT_EQ(latches.size(), 2u);

    const PortMapEntry* extended = FindEntry(latches, 0xDFFD);
    ASSERT_NE(extended, nullptr);
    EXPECT_EQ(extended->latch, PagingLatch::PDFFD);
    EXPECT_EQ(extended->tags, Tags(PortTag::Memory) | PortTag::Screen);

    // The video-steering bit is visible as a Screen-tagged RAM-paging port
    // (query set carries BOTH bits - `Tags(Screen) | Tags(Memory)`, not `&`:
    // the AND of two distinct tags is the empty set)
    EXPECT_TRUE(decoder.HasAnyTaggedPort(Tags(PortTag::Screen) | Tags(PortTag::Memory)));
}

TEST_F(PortDecoder_PortTag_Test, Scorpion_1FFDWindowLatchCarriesScreen)
{
    _context->config.mem_model = MM_SCORP;
    PortDecoder_Scorpion256 decoder(_context);

    const std::vector<PortMapEntry> latches = decoder.GetPagingLatches();
    ASSERT_EQ(latches.size(), 2u);

    const PortMapEntry* window = FindEntry(latches, 0x1FFD);
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(window->latch, PagingLatch::P1FFD);
    EXPECT_EQ(window->tags, Tags(PortTag::Memory) | PortTag::Rom | PortTag::Screen);

    // Joystick row is tagged but is not a latch: it never enters GetPagingLatches.
    // (entries kept in a named vector - FindEntry must not point into a
    // destroyed temporary)
    const std::vector<PortMapEntry> scorpionEntries = decoder.getPortMapEntries();
    const PortMapEntry* joystick = FindEntry(scorpionEntries, 0xFF1F);
    ASSERT_NE(joystick, nullptr);
    EXPECT_EQ(joystick->tags, Tags(PortTag::Joystick));
    EXPECT_EQ(joystick->latch, PagingLatch::None);
}

TEST_F(PortDecoder_PortTag_Test, Pentagon_7FFDOnly)
{
    _context->config.mem_model = MM_PENTAGON;
    PortDecoder_Pentagon128 decoder(_context);

    const std::vector<PortMapEntry> latches = decoder.GetPagingLatches();
    ASSERT_EQ(latches.size(), 1u);
    EXPECT_EQ(latches[0].port, 0x7FFD);
    EXPECT_EQ(latches[0].latch, PagingLatch::P7FFD);
}

/// endregion </Paging latch bindings>

/// region <Sound family queries>

TEST_F(PortDecoder_PortTag_Test, SoundAyFamilyIsTheAyRowsOnEveryModel)
{
    const MEM_MODEL models[] = {MM_SPECTRUM48, MM_SPECTRUM128, MM_PLUS3,
                                MM_PROFI, MM_SCORP, MM_PENTAGON};

    for (MEM_MODEL model : models)
    {
        _context->config.mem_model = model;
        std::unique_ptr<PortDecoder> decoder(PortDecoder::GetPortDecoderForModel(model, _context));

        const std::vector<PortMapEntry> ay = decoder->GetSoundEntries(PortTag::SoundAy);
        ASSERT_EQ(ay.size(), 2u) << "model " << static_cast<int>(model);
        EXPECT_EQ(ay[0].port, 0xFFFD);
        EXPECT_EQ(ay[1].port, 0xBFFD);
        for (const PortMapEntry& entry : ay)
        {
            EXPECT_TRUE(entry.tags & PortTag::Sound) << "member tag must embed the Sound bit";
        }

        // The family answer needs no per-model switch
        EXPECT_TRUE(decoder->HasAnyTaggedPort(Tags(PortTag::Sound)));
    }
}

TEST_F(PortDecoder_PortTag_Test, Pentagon_CovoxRowCarriesBothMembers)
{
    _context->config.mem_model = MM_PENTAGON;
    _context->config.sound.sd = 1;  // SoundDrive fitted: quad rows carry both members
    PortDecoder_Pentagon128 decoder(_context);

    const std::vector<PortMapEntry> pentagonEntries = decoder.getPortMapEntries();
    const PortMapEntry* covox = FindEntry(pentagonEntries, 0x00FB);
    ASSERT_NE(covox, nullptr);
    EXPECT_EQ(covox->tags, Tags(PortTag::SoundCovox) | PortTag::SoundSoundDrive);

    // Two rows now: SoundDrive mode 2 (#F1/#F3/#F9/#FB) and mode 1
    // (#0F/#1F/#4F/#5F, TR-DOS-gated) - both wired by SoundManager when SD=1
    const PortMapEntry* mode1 = FindEntry(pentagonEntries, 0x001F);
    ASSERT_NE(mode1, nullptr);
    EXPECT_EQ(mode1->tags, Tags(PortTag::SoundCovox) | PortTag::SoundSoundDrive);
    EXPECT_NE(mode1->gate, nullptr) << "mode 1 row must document the TR-DOS precedence";

    EXPECT_EQ(decoder.GetSoundEntries(PortTag::SoundCovox).size(), 2u);
    EXPECT_EQ(decoder.GetSoundEntries(PortTag::SoundSoundDrive).size(), 2u);

    // 128K has no Covox: the member query is a fitment answer (real 128K
    // configs ship SD=0, so the DAC row is absent there)
    _context->config.mem_model = MM_SPECTRUM128;
    _context->config.sound.sd = 0;
    PortDecoder_Spectrum128 decoder128(_context);
    EXPECT_FALSE(decoder128.HasAnyTaggedPort(Tags(PortTag::SoundCovox)));
}

TEST_F(PortDecoder_PortTag_Test, BareSoundIsNotAMemberQuery)
{
    _context->config.mem_model = MM_SPECTRUM128;
    PortDecoder_Spectrum128 decoder(_context);

    EXPECT_TRUE(decoder.GetSoundEntries(PortTag::Sound).empty());
    EXPECT_TRUE(decoder.GetSoundEntries(PortTag::None).empty());
}

/// endregion </Sound family queries>

/// region <ReadPagingLatch live bindings>

TEST_F(PortDecoder_PortTag_Test, ReadPagingLatchMapsEnumToStateField)
{
    // The enum -> field mapping is pinned by writing the EmulatorState field
    // and asserting the read-back; the decoder-writes-the-field half is
    // already pinned by the portdecoder_models_test decode sweeps.
    EmulatorState& state = _context->emulatorState;

    state.p7FFD = 0x11;
    state.p1FFD = 0x02;
    state.pDFFD = 0x81;
    state.p7EFD = 0x05;
    state.pEFF7 = 0x03;
    state.pFF77 = 0x10;
    state.aFE = 0x40;
    state.aFB = 0x20;
    state.pFDFD = 0x07;
    state.pFFF7[0] = 0x1234;

    EXPECT_EQ(PortDecoder::ReadPagingLatch(PagingLatch::P7FFD, state), 0x11u);
    EXPECT_EQ(PortDecoder::ReadPagingLatch(PagingLatch::P1FFD, state), 0x02u);
    EXPECT_EQ(PortDecoder::ReadPagingLatch(PagingLatch::PDFFD, state), 0x81u);
    EXPECT_EQ(PortDecoder::ReadPagingLatch(PagingLatch::PFDFD, state), 0x07u);
    EXPECT_EQ(PortDecoder::ReadPagingLatch(PagingLatch::P7EFD, state), 0x05u);
    EXPECT_EQ(PortDecoder::ReadPagingLatch(PagingLatch::PEFF7, state), 0x03u);
    EXPECT_EQ(PortDecoder::ReadPagingLatch(PagingLatch::PFF77, state), 0x10u);
    EXPECT_EQ(PortDecoder::ReadPagingLatch(PagingLatch::AFE, state), 0x40u);
    EXPECT_EQ(PortDecoder::ReadPagingLatch(PagingLatch::AFB, state), 0x20u);
    EXPECT_EQ(PortDecoder::ReadPagingLatch(PagingLatch::PFFF7Window0, state), 0x1234u);
    EXPECT_EQ(PortDecoder::ReadPagingLatch(PagingLatch::None, state), 0u);
}

TEST_F(PortDecoder_PortTag_Test, EveryMemoryRowHasALiveBinding)
{
    const MEM_MODEL models[] = {MM_SPECTRUM128, MM_PLUS3, MM_PROFI, MM_SCORP, MM_PENTAGON};

    for (MEM_MODEL model : models)
    {
        _context->config.mem_model = model;
        std::unique_ptr<PortDecoder> decoder(PortDecoder::GetPortDecoderForModel(model, _context));

        for (const PortMapEntry& entry : decoder->getPortMapEntries())
        {
            if (entry.tags & PortTag::Memory)
            {
                EXPECT_NE(entry.latch, PagingLatch::None)
                    << "Memory-tagged row #" << StringHelper::Format("0x%04X", entry.port)
                    << " has no live binding (model " << static_cast<int>(model) << ")";
            }
        }
    }
}

/// endregion </ReadPagingLatch live bindings>

/// region <Query invariants>

TEST_F(PortDecoder_PortTag_Test, GetPagingLatchesMatchesLatchedRowsFilteredByCategory)
{
    // Guards the two query paths from drifting: the latch filter must equal
    // the manual "row has a latch AND carries the requested categories" set
    _context->config.mem_model = MM_SCORP;
    PortDecoder_Scorpion256 decoder(_context);

    const std::vector<PortMapEntry> latches = decoder.GetPagingLatches(Tags(PortTag::Memory));
    const std::vector<PortMapEntry> all = decoder.getPortMapEntries();

    size_t expected = 0;
    for (const PortMapEntry& entry : all)
    {
        if (entry.latch != PagingLatch::None &&
            (entry.tags & Tags(PortTag::Memory)) == Tags(PortTag::Memory))
        {
            expected++;
        }
    }
    EXPECT_EQ(latches.size(), expected);

    // Rom-scoped query narrows to the same rows (both latches steer ROM too)
    EXPECT_EQ(decoder.GetPagingLatches(Tags(PortTag::Rom)).size(), 2u);
    // Screen-scoped on Scorpion: 7FFD + 1FFD (Shadow Monitor) - the joystick
    // row carries no Screen bit and no latch
    EXPECT_EQ(decoder.GetPagingLatches(Tags(PortTag::Screen)).size(), 2u);
}

/// endregion </Query invariants>

/// region <Serialization single source (every automation surface)>

static bool ContainsName(const std::vector<std::string>& names, const std::string& name)
{
    for (const std::string& candidate : names)
    {
        if (candidate == name)
            return true;
    }
    return false;
}

TEST_F(PortDecoder_PortTag_Test, TagNamesCarryEverySoundMember)
{
    // Pentagon #FB answers both covox and SoundDrive: the wire names must
    // report BOTH members. Guards the else-if regression that silently
    // dropped the second member on WebAPI (SD=1 fits the quad SoundDrive,
    // whose #FB also answers plain Covox software)
    _context->config.mem_model = MM_PENTAGON;
    _context->config.sound.sd = 1;
    PortDecoder_Pentagon128 decoder(_context);

    const std::vector<PortMapEntry> entries = decoder.getPortMapEntries();
    const PortMapEntry* covox = FindEntry(entries, 0x00FB);
    ASSERT_NE(covox, nullptr);

    const std::vector<std::string> names = PortTagSetToStrings(covox->tags);
    EXPECT_TRUE(ContainsName(names, "sound_covox"));
    EXPECT_TRUE(ContainsName(names, "sound_sounddrive"));
}

TEST_F(PortDecoder_PortTag_Test, TagNamesFollowTaxonomyOrder)
{
    // 7FFD on Scorpion carries memory+rom+screen; names come out in the
    // taxonomy order (categories first), exactly as /ports documents them
    _context->config.mem_model = MM_SCORP;
    PortDecoder_Scorpion256 decoder(_context);

    const std::vector<PortMapEntry> entries = decoder.getPortMapEntries();
    const PortMapEntry* paging = FindEntry(entries, 0x7FFD);
    ASSERT_NE(paging, nullptr);

    const std::vector<std::string> names = PortTagSetToStrings(paging->tags);
    const std::vector<std::string> expected = {"memory", "rom", "screen"};
    EXPECT_EQ(names, expected);
}

TEST_F(PortDecoder_PortTag_Test, TagNamesEdgeCases)
{
    // Untagged row (legacy registered peripheral): no names at all
    EXPECT_TRUE(PortTagSetToStrings(0).empty());
    // Bare Sound bit without a member degrades to the category name
    const std::vector<std::string> bare = PortTagSetToStrings(Tags(PortTag::Sound));
    const std::vector<std::string> expectedBare = {"sound"};
    EXPECT_EQ(bare, expectedBare);
}

TEST_F(PortDecoder_PortTag_Test, LatchWireNames)
{
    EXPECT_STREQ(PagingLatchToString(PagingLatch::P7FFD), "p7FFD");
    EXPECT_STREQ(PagingLatchToString(PagingLatch::P1FFD), "p1FFD");
    EXPECT_STREQ(PagingLatchToString(PagingLatch::PDFFD), "pDFFD");
    EXPECT_STREQ(PagingLatchToString(PagingLatch::PFFF7Window2), "pFFF7_w2");
    EXPECT_EQ(PagingLatchToString(PagingLatch::None), nullptr);
}

TEST_F(PortDecoder_PortTag_Test, DecodeLatchDictionary)
{
    // 0x23 = 0b0010_0011: RAM bank 3, screen 0, ROM 0, locked
    std::vector<DecodedLatchField> fields = DecodePagingLatch(PagingLatch::P7FFD, 0x23, MM_SPECTRUM128);
    ASSERT_EQ(fields.size(), 4u);
    EXPECT_EQ(fields[0].key, "ram_bank");
    EXPECT_FALSE(fields[0].isBool);
    EXPECT_EQ(fields[0].intValue, 3);
    EXPECT_EQ(fields[1].key, "shadow_screen");
    EXPECT_TRUE(fields[1].isBool);
    EXPECT_FALSE(fields[1].boolValue);
    EXPECT_EQ(fields[2].key, "rom_select");
    EXPECT_EQ(fields[2].intValue, 0);
    EXPECT_EQ(fields[3].key, "locked");
    EXPECT_TRUE(fields[3].isBool);
    EXPECT_TRUE(fields[3].boolValue);

    // Pentagon 512K: bits [6:7] fold into the 5-bit bank index
    // (PortDecoder_Pentagon512::switchRAMPage)
    fields = DecodePagingLatch(PagingLatch::P7FFD, 0x40, MM_PENTAGON, 512);
    ASSERT_EQ(fields.size(), 4u);
    EXPECT_EQ(fields[0].key, "ram_bank");
    EXPECT_EQ(fields[0].intValue, 8);

    // Pentagon with any other RAM size keeps the standard 3-bit bank: the
    // 128K decoder is the factory default (golden ModelsRegression bank maps)
    fields = DecodePagingLatch(PagingLatch::P7FFD, 0x40, MM_PENTAGON, 1024);
    ASSERT_EQ(fields.size(), 4u);
    EXPECT_EQ(fields[0].intValue, 0);

    // Scorpion: #7FFD D6/D7 are unused (extensions live in #1FFD) - 3-bit bank
    fields = DecodePagingLatch(PagingLatch::P7FFD, 0x40, MM_SCORP);
    ASSERT_EQ(fields.size(), 4u);
    EXPECT_EQ(fields[0].intValue, 0);

    // Same latch value on +3: special paging 4 + disk motor on
    fields = DecodePagingLatch(PagingLatch::P1FFD, 0x0C, MM_PLUS3);
    ASSERT_EQ(fields.size(), 2u);
    EXPECT_EQ(fields[0].key, "special_paging");
    EXPECT_EQ(fields[0].intValue, 4);
    EXPECT_EQ(fields[1].key, "disk_motor");
    EXPECT_TRUE(fields[1].boolValue);

    // On Scorpion the same port is the shadow-monitor window latch
    fields = DecodePagingLatch(PagingLatch::P1FFD, 0x02, MM_SCORP);
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_EQ(fields[0].key, "shadow_monitor_paged");
    EXPECT_TRUE(fields[0].boolValue);

    // On plain 128K the #1FFD latch does not exist - no dictionary fields
    EXPECT_TRUE(DecodePagingLatch(PagingLatch::P1FFD, 0xFF, MM_SPECTRUM128).empty());

    // Profi DFFD: extended bank 3 + 512x240 video bit
    fields = DecodePagingLatch(PagingLatch::PDFFD, 0x83, MM_PROFI);
    ASSERT_EQ(fields.size(), 2u);
    EXPECT_EQ(fields[0].key, "extended_ram_bank");
    EXPECT_EQ(fields[0].intValue, 3);
    EXPECT_EQ(fields[1].key, "video_512x240");
    EXPECT_TRUE(fields[1].boolValue);
}

/// endregion </Serialization single source>

/// region <ROM page role layout table (§5.2 single source)>

TEST_F(PortDecoder_PortTag_Test, RomPageRolePerModelLayout)
{
    ROM rom(_context);

    _context->config.mem_model = MM_SPECTRUM48;
    EXPECT_EQ(rom.GetROMPageRole(0), "48K BASIC ROM");

    _context->config.mem_model = MM_SPECTRUM128;
    EXPECT_EQ(rom.GetROMPageRole(0), "128K Editor/Menu ROM");
    EXPECT_EQ(rom.GetROMPageRole(1), "48K BASIC ROM");

    _context->config.mem_model = MM_PENTAGON;
    EXPECT_EQ(rom.GetROMPageRole(0), "Service ROM");
    EXPECT_EQ(rom.GetROMPageRole(1), "TR-DOS ROM");
    EXPECT_EQ(rom.GetROMPageRole(2), "128K Editor/Menu ROM");
    EXPECT_EQ(rom.GetROMPageRole(3), "48K BASIC ROM");

    _context->config.mem_model = MM_PLUS3;
    EXPECT_EQ(rom.GetROMPageRole(2), "+3DOS ROM");
    EXPECT_EQ(rom.GetROMPageRole(3), "48K BASIC ROM (copy)");

    _context->config.mem_model = MM_SCORP;
    EXPECT_EQ(rom.GetROMPageRole(3), "48K BASIC ROM");

    // Beyond the curated 4-page layouts and on uncurated models: generic name
    _context->config.mem_model = MM_PENTAGON;
    EXPECT_EQ(rom.GetROMPageRole(7), "ROM Page 7");
    _context->config.mem_model = MM_ATM710;
    EXPECT_EQ(rom.GetROMPageRole(0), "ROM Page 0");
}

/// endregion </ROM page role layout table>
