// The ZX-MultiSound under time travel (docs/inprogress/2026-10-03-zx-multisound/tdd-integration.md §4, MS-5): the
// card's four devices (the card with its CPLD, YM2203 pair, MIDI line and DACs; the SAA1099; the SAM2695; the board's
// General Sound) recorded and restored, named by the card's slot; the session guard for a card / no-card mismatch and
// another MIDI bank; recording refused when a slot-built card's devices are missing; two cards with one module
// refused up front by the slot planner.
//
// The machines are a shipped config with its [SLOTS] section replaced (the card is fitted in tests only: no shipped
// config carries it, slots Q8) and a test MIDI bank.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "3rdparty/sam2695/tests/sf2builder.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/engine/ttdconfigfingerprint.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdconfigcapture.h"
#include "debugger/ttd/ttdmachineperipherals.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/slots/card.h"
#include "emulator/slots/cards/multisound/multisoundslotcard.h"
#include "emulator/slots/slotconfig.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/soundmanager.h"
#include "sam2695/sam2695.h"

namespace fs = std::filesystem;
using ttd::PeripheralId;

namespace
{

/// A test bank: one preset (bank 0, program 0) of a looped sine; `variant` changes the preset name, so the bank
/// file and its SHA-256 differ
std::string WriteBank(const std::string& name, int variant)
{
    using namespace sam2695test;
    Sf2Builder builder;
    const int sine = builder.AddSample(SineSample("sine"));
    builder.presets.push_back(
        SimplePreset(variant == 0 ? "Sine" : "Sine B", 0, 0, sine, { { G(sam2695::Gen::SampleModes), 1 } }));
    const std::vector<uint8_t> bytes = builder.Build();
    const std::string path = TestPathHelper::GetUniqueTestScratchPath(name);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

/// A shipped config's machine with its [SLOTS] replaced and the given extra sections, every sound card as
/// configured, time travel available
class StagedMachine
{
public:
    StagedMachine(const std::string& folder, const std::string& slotsSection, const std::string& extra = {})
    {
        const fs::path source = TestPathHelper::FindProjectRoot() / "data" / "configs" / folder / "unreal.ini";
        std::ifstream in(source, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t shipped = text.find("\n[SLOTS]");
        if (shipped != std::string::npos)
        {
            const size_t next = text.find("\n[", shipped + 1);
            text.erase(shipped, next == std::string::npos ? std::string::npos : next - shipped);
        }
        text += "\n[SLOTS]\n" + slotsSection + "\n" + extra + "\n";
        _path = TestPathHelper::GetUniqueTestScratchPath("ttdms-" + folder + ".ini");
        std::ofstream out(_path, std::ios::binary);
        out << text;
        out.close();
        _emulator = std::make_unique<Emulator>(LoggerLevel::LogError);
        _emulator->SetCustomConfigPath(_path.string());
        _ok = _emulator->Init();
        if (_ok)
        {
            _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
            _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
            _emulator->GetContext()->pMemory->UpdateFeatureCache();
        }
    }
    ~StagedMachine()
    {
        _emulator->Release();
        std::error_code ignored;
        fs::remove(_path, ignored);
    }
    bool Ok() const { return _ok; }
    Emulator& Machine() { return *_emulator; }
    EmulatorContext* Context() const { return _emulator->GetContext(); }
    ttd::TimeTravelManager* Ttd() const { return _emulator->GetContext()->pTimeTravelManager; }
    MultiSoundSlotCard* Card(const std::string& slot = "zxbus.1") const
    {
        SlotManager* slots = Context()->pSlotManager;
        return slots != nullptr ? dynamic_cast<MultiSoundSlotCard*>(slots->FindCard(slot)) : nullptr;
    }

private:
    SoundCardScope _everySound;
    fs::path _path;
    std::unique_ptr<Emulator> _emulator;
    bool _ok = false;
};

/// Z80 code built in the test
class Program
{
public:
    std::vector<uint8_t> code;

    void Bytes(std::initializer_list<uint8_t> bytes) { code.insert(code.end(), bytes); }
    /// LD BC,port; LD A,value; OUT (C),A
    void Out(uint16_t port, uint8_t value)
    {
        Bytes({ 0x01, static_cast<uint8_t>(port), static_cast<uint8_t>(port >> 8), 0x3E, value, 0xED, 0x79 });
    }
    void Ym(uint8_t reg, uint8_t value)
    {
        Out(0xFFFD, reg);
        Out(0xBFFD, value);
    }
    void Saa(uint8_t reg, uint8_t value)
    {
        Out(0x01FF, reg);
        Out(0x00FF, value);
    }
    uint16_t Here() const { return static_cast<uint16_t>(kOrigin + code.size()); }

    /// Bit-bangs `bits` R14 values from the table on U4's IOA2 (the MIDI line), 112 T-states a bit (31250 baud)
    void MidiBits(uint16_t table, uint8_t bits)
    {
        Out(0xFFFD, 0x0E);
        Bytes({ 0x01, 0xFD, 0xBF });                                                   // LD BC,#BFFD
        Bytes({ 0x21, static_cast<uint8_t>(table), static_cast<uint8_t>(table >> 8) }); // LD HL,table
        Bytes({ 0x16, bits });                                                          // LD D,count
        const uint16_t loop = Here();
        Bytes({ 0x7E, 0xED, 0x79, 0x23 });   // LD A,(HL); OUT (C),A; INC HL                       7 + 12 + 6
        for (int i = 0; i < (112 - 49) / 7; i++)
            Bytes({ 0x1E, 0x00 });           // LD E,0                                              7 each
        const uint16_t next = static_cast<uint16_t>(Here() + 3);
        Bytes({ 0xC3, static_cast<uint8_t>(next), static_cast<uint8_t>(next >> 8) });   // JP next   10
        Bytes({ 0x15 });                                                                 // DEC D     4
        Bytes({ 0xC2, static_cast<uint8_t>(loop), static_cast<uint8_t>(loop >> 8) });   // JP NZ     10
    }

    static constexpr uint16_t kOrigin = 0x8000;
};

constexpr uint16_t kMidiTable = 0x9000;
constexpr uint8_t kLineHigh = 0xFF;   // R14 with IOA2 = 1 (the MIDI line idle)
constexpr uint8_t kLineLow = 0xFB;    // IOA2 = 0

/// R14 values, one per MIDI bit (start 0, eight data bits LSB first, stop 1), after two idle bits
std::vector<uint8_t> MidiLineBits(std::initializer_list<uint8_t> bytes)
{
    std::vector<uint8_t> line = { kLineHigh, kLineHigh };
    for (uint8_t byte : bytes)
    {
        line.push_back(kLineLow);
        for (int bit = 0; bit < 8; bit++)
            line.push_back(((byte >> bit) & 1) ? kLineHigh : kLineLow);
        line.push_back(kLineHigh);
    }
    return line;
}

/// The MIDI line idle and U4's IOA an output (the reset latch is 0, which would send a start bit)
void MidiLineReady(Program& p)
{
    p.Ym(0x0E, kLineHigh);
    p.Ym(0x07, 0x7E);   // tone A on, IOA an output
}

/// Every source: control byte (U4, FM on, SAA clock on), an SSG tone and an FM note on U4, an SAA tone, a General
/// Sound command, a MIDI Note On; then forever a SounDrive square wave (the CPU keeps writing ports)
std::vector<uint8_t> TuneProgram(uint8_t midiBits)
{
    Program p;
    p.Bytes({ 0xF3 });   // DI
    p.Out(0xFFFD, 0xF2);
    MidiLineReady(p);
    p.Ym(0x00, 0x00);
    p.Ym(0x01, 0x01);
    p.Ym(0x08, 0x0F);
    for (uint8_t op : { 0x00, 0x04, 0x08, 0x0C })
    {
        p.Ym(static_cast<uint8_t>(0x30 + op), 0x01);
        p.Ym(static_cast<uint8_t>(0x40 + op), 0x00);
        p.Ym(static_cast<uint8_t>(0x50 + op), 0x1F);
        p.Ym(static_cast<uint8_t>(0x80 + op), 0x0F);
    }
    p.Ym(0xB0, 0x07);
    p.Ym(0xA4, 0x22);
    p.Ym(0xA0, 0x6A);
    p.Ym(0x28, 0xF0);
    p.Saa(0x00, 0xFF);
    p.Saa(0x08, 0x80);
    p.Saa(0x10, 0x04);
    p.Saa(0x14, 0x01);
    p.Saa(0x1C, 0x01);
    p.Out(0x00BB, 0xF3);
    p.MidiBits(kMidiTable, midiBits);
    // SounDrive channel 0: #F0 / #10 every 1000 T-states or so, forever
    p.Bytes({ 0x01, 0x0F, 0x00 });                       // LD BC,#000F
    const uint16_t loop = p.Here();
    p.Bytes({ 0x3E, 0xF0, 0xED, 0x79 });                 // LD A,#F0; OUT (C),A
    p.Bytes({ 0x1E, 0x40 });                             // LD E,#40
    const uint16_t d1 = p.Here();
    p.Bytes({ 0x1D, 0x20, static_cast<uint8_t>(d1 - (p.Here() + 2)) });   // DEC E; JR NZ,d1
    p.Bytes({ 0x3E, 0x10, 0xED, 0x79 });                 // LD A,#10; OUT (C),A
    p.Bytes({ 0x1E, 0x40 });
    const uint16_t d2 = p.Here();
    p.Bytes({ 0x1D, 0x20, static_cast<uint8_t>(d2 - (p.Here() + 2)) });
    p.Bytes({ 0x18, static_cast<uint8_t>(loop - (p.Here() + 2)) });       // JR loop
    return p.code;
}

/// MIDI only: the line ready, then the bits, DI; HALT
std::vector<uint8_t> MidiProgram(uint8_t midiBits)
{
    Program p;
    p.Bytes({ 0xF3 });
    MidiLineReady(p);
    p.MidiBits(kMidiTable, midiBits);
    p.Bytes({ 0xF3, 0x76 });
    return p.code;
}

void Load(StagedMachine& m, const std::vector<uint8_t>& code, const std::vector<uint8_t>& table)
{
    Z80* z80 = m.Context()->pCore->GetZ80();
    for (size_t i = 0; i < code.size(); i++)
        z80->DirectWrite(static_cast<uint16_t>(Program::kOrigin + i), code[i]);
    for (size_t i = 0; i < table.size(); i++)
        z80->DirectWrite(static_cast<uint16_t>(kMidiTable + i), table[i]);
    z80->halted = 0;
    z80->pc = Program::kOrigin;
}

/// `DI; HALT` at #7F00: the frames run with the CPU parked
void Park(StagedMachine& m)
{
    Z80* z80 = m.Context()->pCore->GetZ80();
    z80->DirectWrite(0x7F00, 0xF3);
    z80->DirectWrite(0x7F01, 0x76);
    z80->pc = 0x7F00;
}

uint64_t Fnv1a(const uint8_t* data, size_t size, uint64_t hash = 0xcbf29ce484222325ULL)
{
    for (size_t i = 0; i < size; i++)
    {
        hash ^= data[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

/// The card's state over its four devices
uint64_t CardStateHash(MultiSoundSlotCard& card)
{
    std::vector<CardTtdDevice> devices;
    card.CollectTtdDevices(devices);
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const CardTtdDevice& device : devices)
    {
        std::vector<uint8_t> blob(device.device->TTDStateSize());
        device.device->TTDSaveState(blob.data());
        hash = Fnv1a(blob.data(), blob.size(), hash);
    }
    return hash;
}

/// Each device loads its own state: the chips stay where they are, the render layers (filters, output buffers,
/// resamplers) start over exactly as after a time-travel restore
void RestartCardRender(MultiSoundSlotCard& card)
{
    std::vector<CardTtdDevice> devices;
    card.CollectTtdDevices(devices);
    for (const CardTtdDevice& device : devices)
    {
        std::vector<uint8_t> blob(device.device->TTDStateSize());
        device.device->TTDSaveState(blob.data());
        device.device->TTDLoadState(blob.data());
    }
}

const MultiSoundRow kRows[] = { MultiSoundRow::Ssg1, MultiSoundRow::Ssg2, MultiSoundRow::Fm1, MultiSoundRow::Fm2,
                                MultiSoundRow::Saa,  MultiSoundRow::Pcm,  MultiSoundRow::Midi };
constexpr size_t kRowCount = std::size(kRows);

/// The seven rows of the last frame, one digest each
std::array<uint64_t, kRowCount> RowDigests(const MultiSoundSlotCard& card)
{
    std::array<uint64_t, kRowCount> digests{};
    for (size_t i = 0; i < std::size(kRows); i++)
    {
        const int16_t* row = card.Card().Row(kRows[i]);
        digests[i] = Fnv1a(reinterpret_cast<const uint8_t*>(row), card.Card().RowFrames() * 2 * sizeof(int16_t));
    }
    return digests;
}

MultiSoundCardReport Report(const MultiSoundSlotCard& card)
{
    MultiSoundCardReport report;
    card.Card().Describe(report);
    return report;
}

/// A config's plan (no emulator)
SlotManager::Result PlanOf(MEM_MODEL model, std::initializer_list<std::pair<std::string, std::string>> lines)
{
    auto config = std::make_unique<CONFIG>();
    config->mem_model = model;
    config->sound.turboSoundKind = TurboSoundKind::None;
    config->sound.gsTypeKind = GSTypeKind::NONE;
    config->sound.gsRamKB = 128;
    config->ngs.ramKB = 2048;
    ParseSlotsSection(std::vector<std::pair<std::string, std::string>>(lines), config->slotConfig);
    SlotManager::Project(config->slotConfig, *config);
    return SlotManager::Plan(*config);
}

}  // namespace

/// The card's four devices are registered under their ids, named by the slot, and the engine's device table builds
/// from them; the bank is a fingerprint field. One Pentagon (~20 ms)
TEST(TtdMultiSound_Test, DevicesRegisteredBySlot)
{
    const std::string bank = WriteBank("ttdms-bank.sf2", 0);
    StagedMachine m("pentagon128k", "zxbus.1 = multisound", "[MIDI]\nBank = " + bank);
    ASSERT_TRUE(m.Ok());
    ASSERT_NE(m.Card(), nullptr);

    ttd::TTDPeripheralRegistry registry;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> owned;
    std::string error;
    ASSERT_TRUE(ttd::RegisterMachinePeripherals(m.Context(), registry, owned, &error)) << error;
    ASSERT_TRUE(registry.CheckDeviceTable(error)) << error;
    const std::vector<ttd::TTDDeviceEntry> entries = registry.DeviceEntries();
    struct Expected
    {
        PeripheralId id;
        const char* instance;
    };
    for (const Expected& e : { Expected{ PeripheralId::MultiSound, "zxbus.1.multisound" },
                               Expected{ PeripheralId::Saa1099, "zxbus.1.multisound.saa1099" },
                               Expected{ PeripheralId::Sam2695, "zxbus.1.multisound.sam2695" },
                               Expected{ PeripheralId::MultiSoundGs, "zxbus.1.multisound.gs" } })
    {
        EXPECT_TRUE(registry.IsRegistered(e.id)) << e.instance;
        const auto entry = std::find_if(entries.begin(), entries.end(),
                                        [&](const ttd::TTDDeviceEntry& x) { return x.descriptor.legacyId == e.id; });
        ASSERT_NE(entry, entries.end()) << e.instance;
        EXPECT_EQ(entry->descriptor.instance, e.instance);
    }
    // The board's GS RAM is an engine region of its own, next to where a GS card's would be
    bool gsRegion = false;
    for (ttd::ITTDRegionSource* source : registry.RegionSources())
    {
        std::vector<ttd::TTDDeviceRegion> regions;
        source->TTDRegions(regions);
        for (const ttd::TTDDeviceRegion& region : regions)
            gsRegion = gsRegion || (region.desc.id == ttd::TTDRegionId::MultiSoundGsRam && region.desc.bytes == 1024u * 1024u);
    }
    EXPECT_TRUE(gsRegion);

    const ttd::TTDConfigFingerprint fp = ttd::CaptureConfigFingerprint(*m.Context(), 0);
    ASSERT_NE(fp.Find("slots.zxbus.1.bank"), nullptr);
    EXPECT_NE(fp.Find("slots.zxbus.1.bank")->value, 0u);
    EXPECT_TRUE(fp.Find("slots.zxbus.1.bank")->affectsRestore);

    std::remove(bank.c_str());
}

/// A tune on all five sources (the CPU still writing the SounDrive) recorded, then replayed from the session start
/// and from a checkpoint in the middle: the card's state and every frame's five rows equal the original run's.
/// The render layers (filters, resamplers) are not state, so a restore starts them over; the original run starts
/// them over at the same two points, which makes its audio comparable bit for bit. ~100 ms: three runs of eight
/// frames with the card's GS firmware, synthesizer and 1 MB GS RAM in every checkpoint
TEST(TtdMultiSound_Test, RoundTripMidTune)
{
    const std::string bank = WriteBank("ttdms-bank.sf2", 0);
    StagedMachine m("pentagon128k", "zxbus.1 = multisound", "[MIDI]\nBank = " + bank);
    ASSERT_TRUE(m.Ok());
    MultiSoundSlotCard* card = m.Card();
    ASSERT_NE(card, nullptr);
    ASSERT_TRUE(card->Card().MidiBankLoaded());

    // The synthesizer's boot window, then the tune for two frames: mid tune when the recording starts
    Park(m);
    m.Machine().RunNFrames(4);
    const std::vector<uint8_t> midi = MidiLineBits({ 0x90, 0x3C, 0x64 });
    Load(m, TuneProgram(static_cast<uint8_t>(midi.size())), midi);
    m.Machine().RunNFrames(2);
    {
        const MultiSoundCardReport report = Report(*card);
        ASSERT_EQ(report.midi.bytesReceived, 3u);
        ASSERT_GE(report.midi.activeVoices, 1u);
        ASSERT_EQ(report.gs.commandFromHost, 0xF3);
    }

    constexpr int kFrames = 8;
    constexpr int kMiddle = 3;
    std::vector<std::array<uint64_t, kRowCount>> original;
    ASSERT_TRUE(m.Ttd()->StartRecording());
    const uint64_t start = m.Ttd()->GetCheckpoint(0)->time.frame;
    RestartCardRender(*card);
    for (int f = 0; f < kFrames; f++)
    {
        if (f == kMiddle)
            RestartCardRender(*card);
        m.Machine().RunNFrames(1);
        original.push_back(RowDigests(*card));
    }
    const uint64_t endHash = CardStateHash(*card);
    m.Ttd()->StopRecording();
    for (size_t i = 0; i < std::size(kRows); i++)
    {
        bool sounding = false;
        for (const auto& digests : original)
            sounding = sounding || digests[i] != original.front()[i];
        EXPECT_TRUE(sounding) << "row " << i << " never changed: not part of the tune";
    }

    for (const int from : { 0, kMiddle })
    {
        SCOPED_TRACE(from == 0 ? "seek to the session start" : "seek to a checkpoint mid tune");
        ASSERT_TRUE(m.Ttd()->SeekTo({ start + static_cast<uint64_t>(from), 0 }));
        for (int f = from; f < kFrames; f++)
        {
            if (f == kMiddle && from != kMiddle)
                RestartCardRender(*card);
            m.Machine().RunNFrames(1);
            const std::array<uint64_t, kRowCount> replayed = RowDigests(*card);
            for (size_t i = 0; i < std::size(kRows); i++)
                EXPECT_EQ(replayed[i], original[static_cast<size_t>(f)][i]) << "frame " << f << " row " << i;
        }
        EXPECT_EQ(CardStateHash(*card), endHash);
    }
    std::remove(bank.c_str());
}

/// A checkpoint falls between two bits of a MIDI byte: restored there, the rest of the byte still arrives, without a
/// framing error, and the synthesizer ends where it did. ~40 ms: one machine, three frames recorded and replayed
TEST(TtdMultiSound_Test, MidiByteAcrossCheckpoint)
{
    const std::string bank = WriteBank("ttdms-bank.sf2", 0);
    StagedMachine m("pentagon128k", "zxbus.1 = multisound", "[MIDI]\nBank = " + bank);
    ASSERT_TRUE(m.Ok());
    MultiSoundSlotCard* card = m.Card();
    ASSERT_NE(card, nullptr);
    Park(m);
    m.Machine().RunNFrames(4);

    // Start the program so the frame ends in the middle of the second byte: the setup (6 OUTs and the loop's
    // set-up, about 220 T-states), two idle bits, the first byte, five bits of the second
    const std::vector<uint8_t> midi = MidiLineBits({ 0x90, 0x3C, 0x64 });
    const uint32_t startAt = m.Context()->config.frame - (220 + (2 + 10 + 5) * 112);
    const EmulatorState* state = &m.Context()->emulatorState;
    const uint64_t parkedFrame = state->frame_counter;
    m.Machine().RunUntilCondition([state, parkedFrame, startAt](const Z80State& z80) {
        return state->frame_counter > parkedFrame || z80.t >= startAt;
    });
    ASSERT_EQ(state->frame_counter, parkedFrame);
    Load(m, MidiProgram(static_cast<uint8_t>(midi.size())), midi);

    ASSERT_TRUE(m.Ttd()->StartRecording());
    const uint64_t start = m.Ttd()->GetCheckpoint(0)->time.frame;
    // To the frame boundary inside the second byte
    const uint64_t baselineFrame = state->frame_counter;
    m.Machine().RunUntilCondition([state, baselineFrame](const Z80State&) { return state->frame_counter > baselineFrame; });
    const uint64_t boundary = m.Context()->emulatorState.frame_counter;
    {
        const MultiSoundCardReport report = Report(*card);
        ASSERT_EQ(report.midi.bytesReceived, 1u) << "the boundary must fall inside the second byte";
        ASSERT_GT(report.midiLine.edges, 2u) << "the second byte has started";
    }
    m.Machine().RunNFrames(2);
    const MultiSoundCardReport recorded = Report(*card);
    ASSERT_EQ(recorded.midi.bytesReceived, 3u);
    ASSERT_EQ(recorded.midi.framingErrors, 0u);
    const uint64_t endHash = CardStateHash(*card);
    m.Ttd()->StopRecording();

    ASSERT_GT(boundary, 0u);
    ASSERT_TRUE(m.Ttd()->SeekTo({ start + 1, 0 }));   // the checkpoint at that boundary
    EXPECT_EQ(Report(*card).midi.bytesReceived, 1u) << "restored between two bits of the second byte";
    m.Machine().RunNFrames(2);
    const MultiSoundCardReport replayed = Report(*card);
    EXPECT_EQ(replayed.midi.bytesReceived, 3u);
    EXPECT_EQ(replayed.midi.framingErrors, 0u);
    EXPECT_EQ(replayed.midi.activeVoices, recorded.midi.activeVoices);
    EXPECT_EQ(CardStateHash(*card), endHash);
    std::remove(bank.c_str());
}

/// A session recorded with one MIDI bank is refused on a machine with another (the synthesizer's blob names its bank;
/// the v2 fingerprint has the bank too); the same bank loads. Three machines (~60 ms)
TEST(TtdMultiSound_Test, SessionRefusesOtherBank)
{
    const std::string bankA = WriteBank("ttdms-bank-a.sf2", 0);
    const std::string bankB = WriteBank("ttdms-bank-b.sf2", 1);
    std::stringstream session;
    ttd::TTDConfigFingerprint recordedFp;
    {
        StagedMachine recorder("pentagon128k", "zxbus.1 = multisound", "[MIDI]\nBank = " + bankA);
        ASSERT_TRUE(recorder.Ok());
        ASSERT_TRUE(recorder.Ttd()->StartRecording());
        recorder.Machine().RunNFrames(2);
        recorder.Ttd()->StopRecording();
        std::string err;
        ASSERT_TRUE(recorder.Ttd()->SerializeSession(session, err)) << err;
        recordedFp = ttd::CaptureConfigFingerprint(*recorder.Context(), 0);
    }
    {
        StagedMachine other("pentagon128k", "zxbus.1 = multisound", "[MIDI]\nBank = " + bankB);
        ASSERT_TRUE(other.Ok());
        session.seekg(0);
        std::string err;
        EXPECT_FALSE(other.Ttd()->DeserializeSession(session, err));
        EXPECT_NE(err.find("zxbus.1: MIDI bank differs from the recording"), std::string::npos) << err;

        const std::vector<ttd::TTDFingerprintDiff> diffs =
            ttd::Compare(recordedFp, ttd::CaptureConfigFingerprint(*other.Context(), 0));
        EXPECT_TRUE(std::any_of(diffs.begin(), diffs.end(), [](const ttd::TTDFingerprintDiff& d) {
            return d.field == "slots.zxbus.1.bank" && d.affectsRestore;
        }));
    }
    {
        StagedMachine same("pentagon128k", "zxbus.1 = multisound", "[MIDI]\nBank = " + bankA);
        ASSERT_TRUE(same.Ok());
        session.clear();
        session.seekg(0);
        std::string err;
        EXPECT_TRUE(same.Ttd()->DeserializeSession(session, err)) << err;
    }
    std::remove(bankA.c_str());
    std::remove(bankB.c_str());
}

/// The session guard: a session recorded with the card does not load on a machine without it, nor the other way
/// round. Four machines (~60 ms)
TEST(TtdMultiSound_Test, SessionGuardRefusesCardMismatch)
{
    const std::string bank = WriteBank("ttdms-bank.sf2", 0);
    auto record = [](StagedMachine& m, std::stringstream& out) {
        ASSERT_TRUE(m.Ttd()->StartRecording());
        m.Machine().RunNFrames(2);
        m.Ttd()->StopRecording();
        std::string err;
        ASSERT_TRUE(m.Ttd()->SerializeSession(out, err)) << err;
    };
    std::stringstream withCard;
    std::stringstream withoutCard;
    {
        StagedMachine m("pentagon128k", "zxbus.1 = multisound", "[MIDI]\nBank = " + bank);
        ASSERT_TRUE(m.Ok());
        record(m, withCard);
    }
    {
        StagedMachine m("pentagon128k", "ay-socket = ay");
        ASSERT_TRUE(m.Ok());
        record(m, withoutCard);
    }
    {
        StagedMachine m("pentagon128k", "ay-socket = ay");
        ASSERT_TRUE(m.Ok());
        std::string err;
        EXPECT_FALSE(m.Ttd()->DeserializeSession(withCard, err));
        EXPECT_NE(err.find("multisound card: recorded multisound, this machine none"), std::string::npos) << err;
    }
    {
        StagedMachine m("pentagon128k", "zxbus.1 = multisound", "[MIDI]\nBank = " + bank);
        ASSERT_TRUE(m.Ok());
        std::string err;
        EXPECT_FALSE(m.Ttd()->DeserializeSession(withoutCard, err));
        EXPECT_NE(err.find("zxbus.1: recorded none, this machine multisound"), std::string::npos) << err;
    }
    std::remove(bank.c_str());
}

/// Every slot-built card records its state: a plan fitting the card with its devices missing (or only some) is
/// refused naming the slot. Pure
TEST(TtdMultiSound_Test, RecordingRefusedWhenACardDeviceIsMissing)
{
    const SlotManager::Result plan = PlanOf(MM_PENTAGON, { { "zxbus.1", "multisound" } });
    ASSERT_NE(plan.FindSlot("zxbus.1"), nullptr);
    auto devices = [](std::initializer_list<PeripheralId> ids) {
        SlotManager::TtdDeviceSet set;
        for (PeripheralId id : ids)
            set.ids.push_back(static_cast<uint8_t>(id));
        return set;
    };
    std::string why;
    EXPECT_TRUE(SlotManager::TtdDevicesMatchPlan(
        plan,
        devices({ PeripheralId::MultiSound, PeripheralId::Saa1099, PeripheralId::Sam2695, PeripheralId::MultiSoundGs }),
        why))
        << why;

    why.clear();
    EXPECT_FALSE(SlotManager::TtdDevicesMatchPlan(plan, devices({}), why));
    EXPECT_NE(why.find("zxbus.1: multisound did not register its device 58"), std::string::npos) << why;
    EXPECT_NE(why.find("zxbus.1: the plan fits multisound, the device is none"), std::string::npos) << why;

    why.clear();
    EXPECT_FALSE(SlotManager::TtdDevicesMatchPlan(
        plan, devices({ PeripheralId::MultiSound, PeripheralId::Saa1099, PeripheralId::Sam2695 }), why));
    EXPECT_NE(why.find("zxbus.1: multisound did not register its device 60"), std::string::npos) << why;
}

/// Two instances of one module cannot both be recorded (a v1 checkpoint keeps one blob per id), so the planner
/// refuses them up front: a second MultiSound's SAA, a GS card next to the card's GS. With the card's GS switched off
/// by its DIP a GS card may stay: the card's GS records under its own id, the GS card under its own. Pure plans, then
/// one Pentagon with both GS (~30 ms)
TEST(TtdMultiSound_Test, TwoInstancesOfOneModuleRefusedByThePlanner)
{
    SlotManager::Result plan = PlanOf(MM_PENTAGON, { { "zxbus.1", "multisound" }, { "zxbus.1.dip", "saa" },
                                                     { "zxbus.2", "multisound" }, { "zxbus.2.dip", "saa" } });
    ASSERT_EQ(plan.conflicts.size(), 1u) << plan.Refusal();
    EXPECT_NE(plan.Refusal().find("`saa`"), std::string::npos) << plan.Refusal();

    plan = PlanOf(MM_PENTAGON, { { "zxbus.1", "multisound" }, { "zxbus.2", "gs" } });
    ASSERT_EQ(plan.conflicts.size(), 1u) << plan.Refusal();
    EXPECT_NE(plan.Refusal().find("`gs`"), std::string::npos) << plan.Refusal();

    plan = PlanOf(MM_PENTAGON, { { "zxbus.1", "multisound" }, { "zxbus.1.dip", "ym,saa,sd" }, { "zxbus.2", "gs" } });
    EXPECT_TRUE(plan.conflicts.empty()) << plan.Refusal();

    StagedMachine m("pentagon128k", "zxbus.1 = multisound\nzxbus.1.dip = ym,saa,sd\nzxbus.2 = gs");
    ASSERT_TRUE(m.Ok()) << m.Machine().GetInitError();
    ASSERT_NE(m.Card(), nullptr);
    ASSERT_NE(m.Context()->pSoundManager->getGeneralSound(), nullptr);
    ttd::TTDPeripheralRegistry registry;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> owned;
    std::string error;
    ASSERT_TRUE(ttd::RegisterMachinePeripherals(m.Context(), registry, owned, &error)) << error;
    EXPECT_TRUE(registry.IsRegistered(PeripheralId::GeneralSound));
    EXPECT_TRUE(registry.IsRegistered(PeripheralId::MultiSoundGs));
    EXPECT_TRUE(registry.CheckDeviceTable(error)) << error;
}

/// Two MultiSound cards with disjoint DIP switches share no function and no port, so the planner fits both: each
/// card hands out all four of its devices, under the same four ids. A checkpoint of either backend keeps one state per
/// id (v1: one blob per id; the engine: its per-checkpoint device states, the device state regions and the binding of
/// a loaded session are keyed by the v1 id although its device table is keyed by {type, instance}), so the second
/// card's devices are refused at registration and named with the first's: recording is refused instead of losing one
/// card's state (slots TODO, item "two instances of one module"). One Pentagon with both cards (~30 ms)
TEST(TtdMultiSound_Test, TwoCardsWithDisjointDipsRefuseRecordingNamingBoth)
{
    const SlotManager::Result plan = PlanOf(MM_PENTAGON, { { "zxbus.1", "multisound" }, { "zxbus.1.dip", "ym" },
                                                           { "zxbus.2", "multisound" }, { "zxbus.2.dip", "saa" } });
    EXPECT_TRUE(plan.conflicts.empty()) << plan.Refusal();

    StagedMachine m("pentagon128k", "zxbus.1 = multisound\nzxbus.1.dip = ym\nzxbus.2 = multisound\nzxbus.2.dip = saa");
    ASSERT_TRUE(m.Ok()) << m.Machine().GetInitError();
    ASSERT_NE(m.Card("zxbus.1"), nullptr);
    ASSERT_NE(m.Card("zxbus.2"), nullptr);
    ttd::TTDPeripheralRegistry registry;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> owned;
    std::string error;
    EXPECT_FALSE(ttd::RegisterMachinePeripherals(m.Context(), registry, owned, &error));
    EXPECT_NE(error.find("two devices under id 58: zxbus.1.multisound and zxbus.2.multisound"), std::string::npos)
        << error;
    EXPECT_FALSE(m.Ttd()->StartRecording()) << "both cards' state cannot be kept";
}
