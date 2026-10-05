// DeviceState::MultiSound and DeviceState::Midi (emulator/state/devicestate.h): the ZX-MultiSound's report for every
// automation surface (ZX-MultiSound tdd-integration.md §5, MS-6), built beside the card so shared code names no
// MultiSound type. The YM2203 pair, its SSG halves and the General Sound use the reports the TurboSound FM and the GS
// slot use (Ym2203ChipReport, AyChipReport, GeneralSoundReport): one shape for one chip wherever it sits.

#include "emulator/state/devicestate.h"

#include <cstdio>
#include <string>

#include "emulator/emulatorcontext.h"
#include "emulator/slots/card.h"
#include "emulator/slots/cards/multisound/multisoundslotcard.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/slots/slotplanner.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/tsfm/ym2203pair.h"
#include "sam2695/sam2695.h"
#include "sam2695/soundbank.h"

namespace
{

StateNode Unavailable(const char* description)
{
    StateNode n = StateNode::Object();
    n["available"] = false;
    n["description"] = description;
    return n;
}

/// The fitted ZX-MultiSound (the first; the plan fits one: a second would share every function, D1)
MultiSoundSlotCard* FittedCard(EmulatorContext* context)
{
    SlotManager* manager = context != nullptr ? context->pSlotManager : nullptr;
    if (manager == nullptr)
    {
        return nullptr;
    }
    for (const std::unique_ptr<ICard>& card : manager->Cards())
    {
        if (auto* multisound = dynamic_cast<MultiSoundSlotCard*>(card.get()))
        {
            return multisound;
        }
    }
    return nullptr;
}

constexpr const char* kNotFitted =
    "no ZX-MultiSound card fitted (fit one: slots plug zxbus.next multisound, or [SLOTS] zxbus.N = multisound)";

std::string Hex(const uint8_t* bytes, size_t count)
{
    static const char kDigits[] = "0123456789abcdef";
    std::string text;
    text.reserve(count * 2);
    for (size_t i = 0; i < count; i++)
    {
        text += kDigits[bytes[i] >> 4];
        text += kDigits[bytes[i] & 0x0F];
    }
    return text;
}

/// "C4" for MIDI note 60
std::string NoteName(int note)
{
    static const char* const kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return std::string(kNames[note % 12]) + std::to_string(note / 12 - 1);
}

StateNode SaaNode(const Saa1099Report& saa)
{
    StateNode node = StateNode::Object();
    node["registers_hex"] = Hex(saa.registers, sizeof(saa.registers));
    node["address_latch"] = int(saa.addressLatch);
    node["sound_enabled"] = saa.soundEnabled;
    node["sync"] = saa.sync;
    node["clock_enabled"] = saa.clockEnabled;
    node["chip_clock_hz"] = uint64_t(saa.chipClockHz);
    node["chip_clocks"] = saa.chipClocks;
    node["output_left"] = int(saa.outputLeft);
    node["output_right"] = int(saa.outputRight);
    StateNode voices = StateNode::Array();
    for (int v = 0; v < 6; v++)
    {
        const Saa1099Report::Voice& voice = saa.voices[v];
        const Saa1099Report::Tone& tone = saa.tones[v];
        StateNode n = StateNode::Object();
        n["voice"] = v;
        n["tone_enabled"] = voice.toneEnabled;
        n["noise_enabled"] = voice.noiseEnabled;
        n["envelope_shaped"] = voice.envelopeShaped;
        n["amplitude_left"] = int(voice.amplitudeLeft);
        n["amplitude_right"] = int(voice.amplitudeRight);
        n["tone_register"] = int(tone.toneRegister);
        n["octave"] = int(tone.octaveRegister);
        n["frequency_hz"] = tone.frequencyHz;
        n["level_left"] = int(voice.levelLeft);
        n["level_right"] = int(voice.levelRight);
        voices.push(std::move(n));
    }
    node["voices"] = std::move(voices);
    StateNode noise = StateNode::Array();
    for (int g = 0; g < 2; g++)
    {
        StateNode n = StateNode::Object();
        n["source"] = int(saa.noise[g].source);
        n["lfsr"] = uint64_t(saa.noise[g].lfsr);
        noise.push(std::move(n));
    }
    node["noise"] = std::move(noise);
    StateNode envelopes = StateNode::Array();
    for (int e = 0; e < 2; e++)
    {
        const Saa1099Report::Envelope& env = saa.envelopes[e];
        StateNode n = StateNode::Object();
        n["control"] = int(env.controlRegister);
        n["enabled"] = env.enabled;
        n["shape"] = int(env.shape);
        n["resolution_3bit"] = env.resolution3Bit;
        n["invert_right"] = env.invertRight;
        n["external_clock"] = env.externalClock;
        n["phase"] = int(env.phase);
        n["position"] = int(env.position);
        n["ended"] = env.ended;
        n["level_left"] = int(env.levelLeft);
        n["level_right"] = int(env.levelRight);
        envelopes.push(std::move(n));
    }
    node["envelopes"] = std::move(envelopes);
    return node;
}

StateNode MidiLineNode(const MidiLineReport& line)
{
    StateNode node = StateNode::Object();
    node["source"] = "YM2203 U4 IOA2 (chip 0, I/O port A bit 2)";
    node["port"] = line.port == AyIoPort::PortA ? "A" : "B";
    node["bit"] = line.bit;
    node["connected"] = line.connected;
    node["level"] = line.level ? 1 : 0;
    node["edges"] = line.edges;
    node["last_change_time"] = line.lastChangeTime;
    return node;
}

/// The synthesizer: parts, voices, effects, counters (Midi() and the summary of MultiSound())
StateNode SynthNode(const MultiSoundCard& card)
{
    sam2695::SynthReport synth;
    card.DescribeSynth(synth);
    const sam2695::ISoundBank* bank = card.Synth().Bank();

    StateNode node = StateNode::Object();
    StateNode parts = StateNode::Array();
    for (int ch = 0; ch < 16; ch++)
    {
        const sam2695::SynthReport::ChannelView& cv = synth.channels[ch];
        StateNode part = StateNode::Object();
        part["channel"] = ch + 1;
        part["program"] = int(cv.program) + 1;
        part["bank_msb"] = int(cv.bankMsb);
        part["rhythm"] = cv.rhythm;
        std::string preset;
        if (bank != nullptr && cv.preset >= 0 && size_t(cv.preset) < bank->Model().presets.size())
        {
            preset = bank->Model().presets[size_t(cv.preset)].name;
        }
        part["preset"] = preset;
        part["volume"] = int(cv.volume);
        part["pan"] = int(cv.pan);
        part["expression"] = int(cv.expression);
        part["pitch_bend"] = int(cv.pitchBend);
        part["active_voices"] = int(cv.activeVoices);
        StateNode keys = StateNode::Array();
        StateNode names = StateNode::Array();
        for (int key = 0; key < 128; key++)
        {
            if (cv.keys[size_t(key >> 6)] & (uint64_t{1} << (key & 63)))
            {
                keys.push(key);
                names.push(NoteName(key));
            }
        }
        part["keys"] = std::move(keys);
        part["notes"] = std::move(names);
        part["muted"] = cv.muted;
        parts.push(std::move(part));
    }
    node["parts"] = std::move(parts);
    node["polyphony_limit"] = uint64_t(synth.polyphonyLimit);
    node["active_voices"] = uint64_t(synth.activeVoices);
    node["fading_voices"] = uint64_t(synth.fadingVoices);
    node["master_volume"] = int(synth.masterVolume);
    StateNode effects = StateNode::Object();
    effects["word"] = int(synth.effectsWord);
    effects["reverb_program"] = int(synth.reverbProgram);
    effects["reverb_character"] = int(synth.reverbCharacter);
    effects["reverb_decay_s"] = synth.reverbDecaySeconds;
    effects["chorus_program"] = int(synth.chorusProgram);
    node["effects"] = std::move(effects);
    StateNode uart = StateNode::Object();
    uart["bytes_received"] = synth.bytesReceived;
    uart["framing_errors"] = synth.framingErrors;
    uart["dropped_busy"] = synth.bytesDroppedBusy;
    uart["dropped_queue_full"] = synth.bytesDroppedQueueFull;
    uart["sysex_received"] = synth.sysExReceived;
    uart["sysex_overflows"] = synth.sysExOverflows;
    uart["voices_stolen"] = synth.voicesStolen;
    uart["notes_dropped"] = synth.notesDropped;
    uart["stream_overruns"] = synth.streamOverruns;
    node["counters"] = std::move(uart);
    return node;
}

StateNode BankNode(const MultiSoundCardReport& report)
{
    StateNode node = StateNode::Object();
    node["status"] = report.midi.bankStatus;
    node["name"] = report.midi.bankName;
    node["source"] = report.midi.bankSource;
    if (!report.midi.bankError.empty())
    {
        node["error"] = report.midi.bankError;
    }
    return node;
}

} // namespace

namespace DeviceState
{

StateNode MultiSound(EmulatorContext* context)
{
    MultiSoundSlotCard* slotCard = FittedCard(context);
    if (slotCard == nullptr)
    {
        return Unavailable(kNotFitted);
    }
    MultiSoundCard& card = slotCard->Card();
    MultiSoundCardReport report;
    card.Describe(report);

    StateNode node = StateNode::Object();
    node["available"] = true;
    node["card"] = MultiSoundCard::kDisplayName;
    node["slot"] = slotCard->SlotId();

    // The options: the DIP functions and the firmware build
    StateNode options = StateNode::Object();
    options["text"] = slots::FormatCardOptions(slotCard->Def(), slotCard->Options());
    StateNode dip = StateNode::Object();
    dip["ym"] = report.options.ym;
    dip["saa"] = report.options.saa;
    dip["gs"] = report.options.gs;
    dip["sd"] = report.options.sd;
    options["dip"] = std::move(dip);
    options["gs_ram"] = report.options.gsRam == MultiSoundGsRam::TwoMb ? "2m" : "1m";
    options["ctrl_mask"] = report.options.ctrlMask == MultiSoundCtrlMask::Classic ? "classic" : "pro";
    if (report.options.ctrlMask == MultiSoundCtrlMask::Classic)
    {
        options["ctrl_mask_note"] = "unofficial issue #11 firmware patch: d[7:3] = 11111 for the YM latches while the "
                                    "SAA DIP is off";
    }
    node["options"] = std::move(options);

    // Fit and the built-in devices the slot shadows or took out
    const SlotManager::Result& slots = context->pSlotManager->Current();
    if (const SlotManager::Slot* slot = slots.FindSlot(slotCard->SlotId()))
    {
        node["fit"] = slot->fit == slots::Fit::Unrealistic ? "unrealistic" : slot->fit == slots::Fit::Adapter ? "adapter" : "real";
        node["adapter"] = slot->entry.adapter;
        node["source"] = slot->source;
    }
    StateNode shadowed = StateNode::Array();
    for (const SlotManager::BuiltIn& builtIn : slots.builtIns)
    {
        if (builtIn.state.find(slotCard->SlotId()) != std::string::npos || builtIn.removed)
        {
            StateNode b = StateNode::Object();
            b["id"] = builtIn.id;
            b["name"] = builtIn.name;
            b["state"] = builtIn.state;
            shadowed.push(std::move(b));
        }
    }
    node["shadowed_devices"] = std::move(shadowed);

    // The CPLD
    StateNode logic = StateNode::Object();
    logic["ym_chip"] = int(report.latches.ymChip);
    logic["ym_read_status"] = report.latches.ymReadStatus;
    logic["fm_muted"] = report.latches.fmMuted;
    logic["saa_clock"] = report.latches.saaClock;
    logic["rom_lock"] = report.latches.romLock;
    logic["gs_data"] = int(report.latches.gsData);
    logic["gs_command"] = int(report.latches.gsCommand);
    logic["gs_page"] = int(report.latches.gsPage);
    logic["gs_output"] = int(report.latches.gsOutput);
    logic["gs_data_flag"] = report.latches.dataFlag;
    logic["gs_command_flag"] = report.latches.commandFlag;
    // The four DAC registers as the CPLD latched them (the output stage below plays them in time order)
    StateNode latchedDacs = StateNode::Array();
    for (int ch = 0; ch < 4; ch++)
    {
        StateNode d = StateNode::Object();
        d["sample"] = int(card.Logic().Dac(ch).sample);
        d["volume"] = int(card.Logic().Dac(ch).volume);
        latchedDacs.push(std::move(d));
    }
    logic["dac"] = std::move(latchedDacs);
    node["logic"] = std::move(logic);

    // The YM2203 pair: the TSFM report's chips
    Ym2203Pair& pair = card.Ym();
    const double master = double(pair.config().masterClockHz);
    StateNode ym = StateNode::Object();
    ym["master_clock_hz"] = uint64_t(pair.config().masterClockHz);
    ym["selected_chip"] = int(report.latches.ymChip);
    ym["ratio_phase"] = report.ymRatioPhase;
    ym["fm_trim_db"] = card.FmTrimDb();   // the TSFM board's fm_trim_db: [SOUND] TSFM_FmTrimDb, the same calibration
    StateNode chips = StateNode::Array();
    for (int c = 0; c < 2; c++)
    {
        Ym2203Chip* chip = pair.chip(c);
        StateNode n = StateNode::Object();
        n["index"] = c;
        n["part"] = c == 0 ? "U4 (MIDI pin)" : "U10";
        n["address_latch"] = int(report.ym[c].address);
        n["status"] = int(report.ym[c].status);
        n["ssg"] = AyChipReport(chip->ssg, c, master / 2.0);
        n["fm"] = Ym2203ChipReport(*chip, c, master, card.FmTrimDb());
        chips.push(std::move(n));
    }
    ym["chips"] = std::move(chips);
    node["ym"] = std::move(ym);

    node["saa"] = SaaNode(report.saa);

    // The General Sound: the GS slot's report, plus the card's own firmware state
    StateNode gs = GeneralSoundReport(card.Gs(), nullptr);
    gs["firmware_ready"] = report.gs.firmwareReady;
    gs["cpu_steps"] = report.gs.cpuSteps;
    gs["dac_fetches"] = report.gs.dacFetches;
    node["gs"] = std::move(gs);

    StateNode dacs = StateNode::Array();
    static const char* const kSides[] = {"left", "left", "right", "right"};
    for (int ch = 0; ch < 4; ch++)
    {
        StateNode d = StateNode::Object();
        d["channel"] = ch;
        d["side"] = kSides[ch];
        d["sample"] = int(report.dac[size_t(ch)].sample);
        d["volume"] = int(report.dac[size_t(ch)].volume);
        dacs.push(std::move(d));
    }
    node["dac"] = std::move(dacs);
    node["dac_pending_events"] = uint64_t(report.dacPendingEvents);
    node["dac_late_events"] = report.dacLateEvents;

    StateNode midi = StateNode::Object();
    midi["line"] = MidiLineNode(report.midiLine);
    midi["bank"] = BankNode(report);
    midi["bytes_received"] = report.midi.bytesReceived;
    midi["framing_errors"] = report.midi.framingErrors;
    midi["active_voices"] = uint64_t(report.midi.activeVoices);
    node["midi"] = std::move(midi);
    node["time"] = report.time;
    return node;
}

StateNode Midi(EmulatorContext* context)
{
    MultiSoundSlotCard* slotCard = FittedCard(context);
    if (slotCard == nullptr)
    {
        return Unavailable("no MIDI synthesizer fitted (the ZX-MultiSound carries one: slots plug zxbus.next multisound)");
    }
    const MultiSoundCard& card = slotCard->Card();
    MultiSoundCardReport report;
    card.Describe(report);

    StateNode node = StateNode::Object();
    node["available"] = true;
    node["slot"] = slotCard->SlotId();
    node["synthesizer"] = "SAM2695 (General MIDI)";
    node["line"] = MidiLineNode(report.midiLine);
    node["bank"] = BankNode(report);
    StateNode synth = SynthNode(card);
    for (auto& [key, value] : synth.members)
    {
        node[key] = std::move(value);
    }
    return node;
}

} // namespace DeviceState
