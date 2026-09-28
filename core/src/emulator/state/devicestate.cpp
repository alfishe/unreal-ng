#include "stdafx.h"

#include "emulator/state/devicestate.h"

#include <cmath>
#include <cstdio>
#include <sstream>

#include "emulator/corestate.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/upd765.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/chips/soundchip_turbosoundfm.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/memory/memory.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/video/ulacontention.h"
#include "emulator/video/screen.h"

namespace
{

StateNode Unavailable(const char* description)
{
    StateNode n = StateNode::Object();
    n["available"] = false;
    n["description"] = description;
    return n;
}

/// region <AY>

/// One AY chip, fully decoded. Same keys as the WebAPI has always returned
/// for /state/audio/ay/{chip}, so existing clients keep working.
StateNode AyChipNode(SoundChip_AY8910* chip, int index)
{
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["chip_index"] = index;
    ret["chip_type"] = "AY-3-8912";

    const uint8_t* regs = chip->getRegisters();

    StateNode registers = StateNode::Object();
    for (int reg = 0; reg < 16; reg++)
        registers[SoundChip_AY8910::AYRegisterNames[reg]] = int(regs[reg]);
    ret["registers"] = registers;

    // Everything below decodes the register file - what the program wrote.
    // The generators receive a write on the tick of its T-state (timed SSG
    // writes), so right after an OUT their own copy can still lag behind
    StateNode channels = StateNode::Array();
    const char* channelNames[] = {"A", "B", "C"};
    for (int ch = 0; ch < 3; ch++)
    {
        StateNode channel = StateNode::Object();
        const uint8_t fine = regs[ch * 2];
        const uint8_t coarse = regs[ch * 2 + 1];
        const uint16_t period = uint16_t((coarse << 8) | fine);
        channel["name"] = channelNames[ch];
        channel["period"] = int(period);
        channel["fine"] = int(fine);
        channel["coarse"] = int(coarse);
        channel["frequency_hz"] = 1750000.0 / (16.0 * (period + 1));
        const uint8_t volumeReg = regs[8 + ch];
        channel["volume"] = int(volumeReg & 0x0F);
        channel["tone_enabled"] = (regs[7] & (0x01 << ch)) == 0;
        channel["noise_enabled"] = (regs[7] & (0x08 << ch)) == 0;
        channel["envelope_enabled"] = (volumeReg & 0x10) != 0;
        channels.push(channel);
    }
    ret["channels"] = channels;

    StateNode envelope = StateNode::Object();
    const uint8_t envShape = regs[13];
    const uint16_t envPeriod = uint16_t((regs[12] << 8) | regs[11]);
    envelope["shape"] = int(envShape);
    envelope["period"] = int(envPeriod);
    envelope["current_output"] = int(chip->getEnvelopeGenerator().out());
    envelope["frequency_hz"] = 1750000.0 / (256.0 * (envPeriod + 1));
    ret["envelope"] = envelope;

    StateNode noise = StateNode::Object();
    const uint8_t noisePeriod = regs[6] & 0x1F;
    noise["period"] = int(noisePeriod);
    noise["frequency_hz"] = 1750000.0 / (16.0 * (noisePeriod + 1));
    ret["noise"] = noise;

    StateNode mixer = StateNode::Object();
    const uint8_t mixerValue = regs[7];
    mixer["register_value"] = int(mixerValue);
    mixer["channel_a_tone"] = (mixerValue & 0x01) == 0;
    mixer["channel_b_tone"] = (mixerValue & 0x02) == 0;
    mixer["channel_c_tone"] = (mixerValue & 0x04) == 0;
    mixer["channel_a_noise"] = (mixerValue & 0x08) == 0;
    mixer["channel_b_noise"] = (mixerValue & 0x10) == 0;
    mixer["channel_c_noise"] = (mixerValue & 0x20) == 0;
    mixer["porta_input"] = (mixerValue & 0x40) != 0;
    mixer["portb_input"] = (mixerValue & 0x80) != 0;
    ret["mixer"] = mixer;

    StateNode ports = StateNode::Object();
    ports["porta_value"] = int(regs[14]);
    ports["porta_direction"] = (mixerValue & 0x40) ? "input" : "output";
    ports["portb_value"] = int(regs[15]);
    ports["portb_direction"] = (mixerValue & 0x80) ? "input" : "output";
    ret["io_ports"] = ports;

    ret["sound_played_since_reset"] = false;  // not tracked
    return ret;
}

/// endregion </AY>

/// region <FM>

const char* EgStateName(int state)
{
    switch (state)
    {
        case 0: return "depress";
        case 1: return "attack";
        case 2: return "decay";
        case 3: return "sustain";
        case 4: return "release";
        case 5: return "reverb";
        default: return "unknown";
    }
}

const char* Ch3ModeName(uint8_t reg27)
{
    switch ((reg27 >> 6) & 3)
    {
        case 0: return "normal";
        case 1: return "extended";
        case 2: return "extended_csm";
        default: return "illegal";
    }
}

/// OPN pitch: F = fnum * 2^(block-1) * fs / 2^20 (block 0 halves)
double OpnHz(uint32_t blockFreq, double sampleRateHz)
{
    const uint32_t fnum = blockFreq & 0x7FF;
    const int block = int((blockFreq >> 11) & 7);
    return double(fnum) * std::ldexp(1.0, block - 1) * sampleRateHz / 1048576.0;
}

StateNode FmChipNode(SoundChip_TurboSoundFM* device, int index)
{
    TsfmChip* c = device->chip(index);
    Ym2203Engine& fm = c->fm;
    auto& engine = fm.fmEngine();
    auto& regs = engine.regs();

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["chip_index"] = index;
    ret["chip_type"] = "YM2203";

    const uint32_t prescale = fm.fmClockPrescale();
    const double fs = double(CPU_CLOCK_RATE) / (12.0 * prescale);
    ret["prescaler"] = int(prescale);
    ret["fm_sample_rate_hz"] = fs;

    const uint8_t status = fm.read_status();
    StateNode st = StateNode::Object();
    st["value"] = int(status);
    st["busy"] = (status & 0x80) != 0;
    st["timer_a_flag"] = (status & 0x01) != 0;
    st["timer_b_flag"] = (status & 0x02) != 0;
    st["busy_remaining_tstates"] = int(c->intf._busy);
    ret["status"] = st;

    const uint8_t reg27 = fm.fmReg(0x27);
    StateNode mode = StateNode::Object();
    mode["register_27"] = int(reg27);
    mode["channel3_mode"] = Ch3ModeName(reg27);
    mode["csm"] = regs.csm() != 0;
    mode["extended"] = regs.multi_freq() != 0;
    ret["mode"] = mode;

    StateNode timers = StateNode::Object();
    {
        StateNode a = StateNode::Object();
        a["value"] = int(regs.timer_a_value());
        a["period_tstates"] = int((1024 - regs.timer_a_value()) * 12 * prescale);
        a["enabled"] = regs.enable_timer_a() != 0;
        a["load"] = regs.load_timer_a() != 0;
        a["running"] = c->intf._timer[0] > 0;
        a["remaining_tstates"] = int(c->intf._timer[0] > 0 ? c->intf._timer[0] : 0);
        timers["a"] = a;
        StateNode b = StateNode::Object();
        b["value"] = int(regs.timer_b_value());
        b["period_tstates"] = int(16 * (256 - regs.timer_b_value()) * 12 * prescale);
        b["enabled"] = regs.enable_timer_b() != 0;
        b["load"] = regs.load_timer_b() != 0;
        b["running"] = c->intf._timer[1] > 0;
        b["remaining_tstates"] = int(c->intf._timer[1] > 0 ? c->intf._timer[1] : 0);
        timers["b"] = b;
    }
    ret["timers"] = timers;

    static const char* slotNames[] = {"S1", "S3", "S2", "S4"};  // register slot order

    StateNode channels = StateNode::Array();
    int keyedChannels = 0;
    int soundingChannels = 0;
    for (int ch = 0; ch < 3; ch++)
    {
        StateNode channel = StateNode::Object();
        channel["index"] = ch;
        const uint32_t blockFreq = regs.ch_block_freq(ch);
        channel["block"] = int((blockFreq >> 11) & 7);
        channel["fnum"] = int(blockFreq & 0x7FF);
        channel["frequency_hz"] = OpnHz(blockFreq, fs);
        channel["feedback"] = int(regs.ch_feedback(ch));
        channel["algorithm"] = int(regs.ch_algorithm(ch));
        const uint8_t keyMask = c->fmKeyOn[ch];
        channel["key_on_mask"] = int(keyMask);
        channel["key_on"] = keyMask != 0;

        bool sounding = false;
        StateNode ops = StateNode::Array();
        auto* channelObj = engine.debug_channel(ch);
        for (int opnum = 0; opnum < 4; opnum++)
        {
            auto* op = channelObj ? channelObj->debug_operator(opnum) : nullptr;
            if (!op)
                continue;
            const uint32_t opoffs = op->opoffs();
            const int slot = int((opoffs - uint32_t(ch)) / 4);  // 0..3 in register order S1,S3,S2,S4
            StateNode o = StateNode::Object();
            o["slot"] = slotNames[slot & 3];
            o["register_offset"] = int(opoffs);
            o["detune"] = int(regs.op_detune(opoffs));
            o["multiple"] = int(regs.op_multiple(opoffs));
            o["total_level"] = int(regs.op_total_level(opoffs));
            o["total_level_db"] = -0.75 * double(regs.op_total_level(opoffs));
            o["key_scale"] = int(regs.op_ksr(opoffs));
            o["attack_rate"] = int(regs.op_attack_rate(opoffs));
            o["decay_rate"] = int(regs.op_decay_rate(opoffs));
            o["sustain_rate"] = int(regs.op_sustain_rate(opoffs));
            o["sustain_level"] = int(regs.op_sustain_level(opoffs));
            o["release_rate"] = int(regs.op_release_rate(opoffs));
            o["ssg_eg_enabled"] = regs.op_ssg_eg_enable(opoffs) != 0;
            o["ssg_eg_mode"] = int(regs.op_ssg_eg_mode(opoffs));
            // Pitch of this operator: channel 3 in extended mode uses the
            // per-slot registers (ymfm's mapping: opoffs 2 -> A9/AD, 10 ->
            // AA/AE, 6 -> A8/AC, S4 keeps A2/A6)
            uint32_t opBlockFreq = blockFreq;
            if (ch == 2 && regs.multi_freq())
            {
                if (opoffs == 2)
                    opBlockFreq = regs.multi_block_freq(1);
                else if (opoffs == 10)
                    opBlockFreq = regs.multi_block_freq(2);
                else if (opoffs == 6)
                    opBlockFreq = regs.multi_block_freq(0);
            }
            const double mul = regs.op_multiple(opoffs) == 0 ? 0.5 : double(regs.op_multiple(opoffs));
            o["block"] = int((opBlockFreq >> 11) & 7);
            o["fnum"] = int(opBlockFreq & 0x7FF);
            o["frequency_hz"] = OpnHz(opBlockFreq, fs) * mul;
            const int egState = int(op->debug_eg_state());
            const int atten = int(op->debug_eg_attenuation());
            o["envelope_state"] = EgStateName(egState);
            o["attenuation"] = atten;
            o["attenuation_db"] = -0.09375 * double(atten);
            // 0x28 mask bits are S1,S2,S3,S4; register slot order is S1,S3,S2,S4
            static const int maskBit[4] = {0, 2, 1, 3};
            o["key_on"] = ((keyMask >> maskBit[slot & 3]) & 1) != 0;
            const bool opSounding = atten < 0x3FF;
            o["sounding"] = opSounding;
            sounding = sounding || opSounding;
            ops.push(o);
        }
        channel["operators"] = ops;
        channel["sounding"] = sounding;
        if (keyMask)
            keyedChannels++;
        if (sounding)
            soundingChannels++;
        channels.push(channel);
    }
    ret["channels"] = channels;
    ret["keyed_channels"] = keyedChannels;
    ret["sounding_channels"] = soundingChannels;

    StateNode out = StateNode::Object();
    out["dac_last_word"] = int(std::lround(c->out.hold * 32768.0));
    out["dac_last_value"] = c->out.hold;
    out["fm_trim_db"] = device->fmTrimDb();
    ret["output"] = out;
    return ret;
}

/// endregion </FM>

/// region <FDC>

const char* WdCommandName(WD1793::WD_COMMANDS cmd)
{
    switch (cmd)
    {
        case WD1793::WD_CMD_RESTORE: return "restore";
        case WD1793::WD_CMD_SEEK: return "seek";
        case WD1793::WD_CMD_STEP: return "step";
        case WD1793::WD_CMD_STEP_IN: return "step_in";
        case WD1793::WD_CMD_STEP_OUT: return "step_out";
        case WD1793::WD_CMD_READ_SECTOR: return "read_sector";
        case WD1793::WD_CMD_WRITE_SECTOR: return "write_sector";
        case WD1793::WD_CMD_READ_ADDRESS: return "read_address";
        case WD1793::WD_CMD_READ_TRACK: return "read_track";
        case WD1793::WD_CMD_WRITE_TRACK: return "write_track";
        case WD1793::WD_CMD_FORCE_INTERRUPT: return "force_interrupt";
        default: return "invalid";
    }
}

/// The drives a controller reaches: inserted image, head position, motor, write protect, geometry
StateNode DrivesNode(EmulatorContext* context, int count)
{
    StateNode drives = StateNode::Array();
    for (int d = 0; d < count; d++)
    {
        StateNode drive = StateNode::Object();
        drive["index"] = d;
        drive["letter"] = std::string(1, char('A' + d));
        FDD* fdd = context->coreState.diskDrives[d];
        if (!fdd)
        {
            drive["present"] = false;
            drives.push(drive);
            continue;
        }
        drive["present"] = true;
        drive["inserted"] = fdd->isDiskInserted();
        drive["path"] = context->coreState.diskFilePaths[d];
        drive["track"] = int(fdd->getTrack());
        drive["side"] = fdd->getSide() ? 1 : 0;
        drive["motor_on"] = fdd->getMotor();
        drive["write_protected"] = fdd->isWriteProtect();
        DiskImage* image = fdd->getDiskImage();
        if (image && fdd->isDiskInserted())
        {
            StateNode geo = StateNode::Object();
            geo["cylinders"] = int(image->getCylinders());
            geo["sides"] = int(image->getSides());
            drive["image"] = geo;
        }
        drives.push(drive);
    }
    return drives;
}

const char* Upd765PhaseName(UPD765::UPDPHASE phase)
{
    switch (phase)
    {
        case UPD765::PHASE_COMMAND: return "command";
        case UPD765::PHASE_EXECUTION: return "execution";
        case UPD765::PHASE_RESULT: return "result";
    }
    return "?";
}

const char* Upd765StateName(UPD765::UPDSTATE state)
{
    switch (state)
    {
        case UPD765::S_IDLE: return "idle";
        case UPD765::S_SEARCH: return "search_sector";
        case UPD765::S_READ_BYTE: return "read_byte";
        case UPD765::S_WRITE_BYTE: return "write_byte";
        case UPD765::S_READ_ID: return "read_id";
        case UPD765::S_FORMAT_REQUEST: return "format_request";
        case UPD765::S_FORMAT_DUE: return "format_due";
        case UPD765::S_FORMAT_END: return "format_end";
        case UPD765::S_RESULT: return "result_pending";
        case UPD765::S_TRACK_SECTOR: return "read_track_sector";
        case UPD765::S_SCAN_BYTE: return "scan_byte";
    }
    return "?";
}

StateNode BytesArray(const uint8_t* bytes, size_t count)
{
    StateNode arr = StateNode::Array();
    for (size_t i = 0; i < count; i++)
        arr.push(int(bytes[i]));
    return arr;
}

/// NEC uPD765A (+3): phase, main status, the command in hand, status bytes, SPECIFY times, units, drives
StateNode Upd765Node(EmulatorContext* context, const UPD765& fdc)
{
    const UPD765::Snapshot s = fdc.getSnapshot();

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["controller"] = "uPD765A (+3)";
    ret["phase"] = Upd765PhaseName(s.phase);
    ret["execution_state"] = Upd765StateName(s.state);

    StateNode msr = StateNode::Object();
    msr["value"] = int(s.mainStatus);
    msr["rqm"] = (s.mainStatus & UPD765::MSR_RQM) != 0;
    msr["dio_to_cpu"] = (s.mainStatus & UPD765::MSR_DIO) != 0;
    msr["execution"] = (s.mainStatus & UPD765::MSR_EXM) != 0;
    msr["busy"] = (s.mainStatus & UPD765::MSR_CB) != 0;
    StateNode seeking = StateNode::Array();
    for (int u = 0; u < UPD765::UNITS; u++)
    {
        if (s.mainStatus & (UPD765::MSR_D0B << u))
            seeking.push(u);
    }
    msr["units_seeking"] = seeking;
    ret["main_status"] = msr;

    StateNode cmd = StateNode::Object();
    cmd["name"] = UPD765::commandName(s.command[0]);
    cmd["bytes"] = BytesArray(s.command, s.commandBytes);
    cmd["complete"] = s.commandBytes >= s.commandLength;
    if (s.commandLength >= 2)
    {
        cmd["unit"] = int(s.command[1] & 0x03);
        cmd["head"] = int((s.command[1] >> 2) & 0x01);
    }
    if (s.commandLength == 9)
    {
        // Data commands: MT MF SK and the sector address C H R N, last sector EOT
        cmd["multi_track"] = (s.command[0] & UPD765::CMD_FLAG_MT) != 0;
        cmd["mfm"] = (s.command[0] & UPD765::CMD_FLAG_MF) != 0;
        cmd["skip"] = (s.command[0] & UPD765::CMD_FLAG_SK) != 0;
        cmd["c"] = int(s.command[2]);
        cmd["h"] = int(s.command[3]);
        cmd["r"] = int(s.command[4]);
        cmd["n"] = int(s.command[5]);
        cmd["eot"] = int(s.command[6]);
    }
    ret["command"] = cmd;

    StateNode result = StateNode::Object();
    result["bytes"] = BytesArray(s.result, s.resultLength);
    result["read"] = int(s.resultPos);
    ret["result"] = result;

    StateNode st = StateNode::Object();
    st["st0"] = int(s.st0);
    st["st1"] = int(s.st1);
    st["st2"] = int(s.st2);
    static const char* const IC[] = { "normal", "abnormal", "invalid_command", "ready_changed" };
    st["interrupt_code"] = IC[(s.st0 >> 6) & 0x03];
    st["end_of_cylinder"] = (s.st1 & UPD765::ST1_EN) != 0;
    st["data_error"] = (s.st1 & UPD765::ST1_DE) != 0;
    st["overrun"] = (s.st1 & UPD765::ST1_OR) != 0;
    st["no_data"] = (s.st1 & UPD765::ST1_ND) != 0;
    st["not_writable"] = (s.st1 & UPD765::ST1_NW) != 0;
    st["missing_address_mark"] = (s.st1 & UPD765::ST1_MA) != 0;
    st["control_mark"] = (s.st2 & UPD765::ST2_CM) != 0;
    st["data_crc_error"] = (s.st2 & UPD765::ST2_DD) != 0;
    st["wrong_cylinder"] = (s.st2 & UPD765::ST2_WC) != 0;
    st["scan_hit"] = (s.st2 & UPD765::ST2_SH) != 0;
    st["scan_not_satisfied"] = (s.st2 & UPD765::ST2_SN) != 0;
    ret["status"] = st;

    StateNode specify = StateNode::Object();
    specify["step_rate_ms"] = int((16 - (s.stepRateTime & 0x0F)) * 2);
    specify["head_load_ms"] = int((s.headLoadTime == 0 ? 128 : s.headLoadTime) * 4);
    ret["specify"] = specify;
    ret["motor_on"] = s.motorOn;

    StateNode units = StateNode::Array();
    for (int u = 0; u < UPD765::UNITS; u++)
    {
        StateNode unit = StateNode::Object();
        unit["index"] = u;
        unit["drive"] = std::string(1, char('A' + (u & 1)));  // US1 is not connected on the +3
        unit["present_cylinder"] = int(s.units[u].pcn);
        unit["seeking"] = s.units[u].seeking;
        unit["interrupt_pending"] = s.units[u].interruptPending;
        units.push(unit);
    }
    ret["units"] = units;
    ret["drives"] = DrivesNode(context, UPD765::DRIVES);
    return ret;
}

/// endregion </FDC>

void TextLine(std::ostringstream& out, int indent, const std::string& key, const StateNode& v);

void TextNode(std::ostringstream& out, int indent, const StateNode& node)
{
    const std::string pad(size_t(indent) * 2, ' ');
    if (node.isObject())
    {
        for (const auto& m : node.members)
            TextLine(out, indent, m.first, m.second);
    }
    else if (node.isArray())
    {
        for (size_t i = 0; i < node.items.size(); i++)
        {
            out << pad << "[" << i << "]";
            const StateNode& item = node.items[i];
            if (item.isObject() || item.isArray())
            {
                out << "\n";
                TextNode(out, indent + 1, item);
            }
            else
            {
                out << " ";
                TextLine(out, 0, "", item);
            }
        }
    }
}

std::string Scalar(const StateNode& v)
{
    char buf[64];
    switch (v.kind)
    {
        case StateNode::Kind::Bool: return v.b ? "true" : "false";
        case StateNode::Kind::Int: snprintf(buf, sizeof(buf), "%lld", (long long)v.i); return buf;
        case StateNode::Kind::Double: snprintf(buf, sizeof(buf), "%.4g", v.d); return buf;
        case StateNode::Kind::String: return v.s;
        default: return "null";
    }
}

void TextLine(std::ostringstream& out, int indent, const std::string& key, const StateNode& v)
{
    const std::string pad(size_t(indent) * 2, ' ');
    if (v.isObject() || v.isArray())
    {
        if (!key.empty())
            out << pad << key << ":\n";
        TextNode(out, key.empty() ? indent : indent + 1, v);
    }
    else
    {
        if (!key.empty())
            out << pad << key << ": ";
        out << Scalar(v) << "\n";
    }
}

}  // namespace

namespace DeviceState
{

StateNode Ay(EmulatorContext* context)
{
    SoundManager* sm = context ? context->pSoundManager : nullptr;
    if (!sm)
        return Unavailable("Sound manager not available");

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    const int ayCount = sm->getAYChipCount();
    ret["available_chips"] = ayCount;
    ret["turbo_sound"] = sm->hasTurboSound();
    ITurboSoundDevice* ts = sm->getTurboSound();
    const bool fm = ts && ts->hasFm();
    ret["slot_device"] = !ts ? "None" : (fm ? "TSFM" : "TurboSound");
    if (ayCount == 0)
        ret["description"] = "No AY chips available";
    else if (ayCount == 1)
        ret["description"] = "Standard AY-3-8912";
    else if (ayCount == 2)
        ret["description"] = fm ? "TurboSound FM (2 x YM2203, SSG halves)" : "TurboSound (dual AY-3-8912)";
    else
        ret["description"] = "ZX Next (triple AY-3-8912)";

    StateNode chips = StateNode::Array();
    for (int i = 0; i < ayCount; i++)
    {
        StateNode info = StateNode::Object();
        info["index"] = i;
        info["type"] = fm ? "YM2203 SSG" : "AY-3-8912";
        SoundChip_AY8910* chip = sm->getAYChip(i);
        if (chip)
        {
            // Tone or noise enabled on any channel, decoded from the mixer register
            const uint8_t mixerReg = chip->getRegisters()[7];
            info["active_channels"] = (mixerReg & 0x3F) != 0x3F;
            info["envelope_active"] = chip->getEnvelopeGenerator().out() > 0;
        }
        info["sound_played_since_reset"] = false;
        chips.push(info);
    }
    ret["chips"] = chips;
    return ret;
}

StateNode AyChip(EmulatorContext* context, int chip)
{
    SoundManager* sm = context ? context->pSoundManager : nullptr;
    if (!sm)
        return Unavailable("Sound manager not available");
    SoundChip_AY8910* ay = sm->getAYChip(chip);
    if (!ay)
        return Unavailable("AY chip not available");
    return AyChipNode(ay, chip);
}

StateNode Fm(EmulatorContext* context)
{
    SoundManager* sm = context ? context->pSoundManager : nullptr;
    if (!sm)
        return Unavailable("Sound manager not available");
    auto* device = dynamic_cast<SoundChip_TurboSoundFM*>(sm->getTurboSound());
    if (!device)
        return Unavailable("TurboSound slot device is not TSFM (no FM half); set [SOUND] TurboSound=FM");

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["description"] = "TurboSound FM (2 x YM2203)";
    StateNode board = StateNode::Object();
    board["selected_chip"] = int(device->board().chip);
    board["status_read_mode"] = device->board().statusRead;
    board["fm_enabled"] = device->board().fmEnabled;
    board["fm_trim_db"] = device->fmTrimDb();
    ret["board"] = board;

    StateNode chips = StateNode::Array();
    for (int i = 0; i < 2; i++)
    {
        StateNode full = FmChipNode(device, i);
        StateNode info = StateNode::Object();
        info["index"] = i;
        info["type"] = "YM2203";
        info["prescaler"] = *full.find("prescaler");
        info["channel3_mode"] = *full.find("mode")->find("channel3_mode");
        info["keyed_channels"] = *full.find("keyed_channels");
        info["sounding_channels"] = *full.find("sounding_channels");
        info["status"] = *full.find("status")->find("value");
        chips.push(info);
    }
    ret["chips"] = chips;
    return ret;
}

StateNode FmChip(EmulatorContext* context, int chip)
{
    SoundManager* sm = context ? context->pSoundManager : nullptr;
    if (!sm)
        return Unavailable("Sound manager not available");
    auto* device = dynamic_cast<SoundChip_TurboSoundFM*>(sm->getTurboSound());
    if (!device)
        return Unavailable("TurboSound slot device is not TSFM (no FM half); set [SOUND] TurboSound=FM");
    if (chip < 0 || chip > 1)
        return Unavailable("FM chip index must be 0 or 1");
    return FmChipNode(device, chip);
}

StateNode Fdc(EmulatorContext* context)
{
    // The +3 has its own controller; the WD1793 object exists on every model but is not wired there
    if (context && context->pUPD765)
        return Upd765Node(context, *context->pUPD765);

    const WD1793* fdc = context ? context->pBetaDisk : nullptr;  // const: picks the register getters, not the computing overloads
    if (!fdc)
        return Unavailable("Beta Disk interface (WD1793) not present on this machine");

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["controller"] = "WD1793 (Beta Disk)";

    StateNode regs = StateNode::Object();
    regs["command"] = int(fdc->getCommandRegister());
    regs["track"] = int(fdc->getTrackRegister());
    regs["sector"] = int(fdc->getSectorRegister());
    regs["data"] = int(fdc->getDataRegister());
    regs["status"] = int(fdc->getStatusRegister());
    ret["registers"] = regs;

    const WD1793::WD_COMMANDS lastCmd = fdc->getLastDecodedCommand();
    ret["last_command"] = WdCommandName(lastCmd);
    const bool type1 = lastCmd <= WD1793::WD_CMD_STEP_OUT;
    const uint8_t status = fdc->getStatusRegister();
    StateNode sb = StateNode::Object();
    sb["busy"] = (status & 0x01) != 0;
    if (type1)
    {
        sb["index"] = (status & 0x02) != 0;
        sb["track0"] = (status & 0x04) != 0;
    }
    else
    {
        sb["drq"] = (status & 0x02) != 0;
        sb["lost_data"] = (status & 0x04) != 0;
    }
    sb["crc_error"] = (status & 0x08) != 0;
    sb["record_not_found"] = (status & 0x10) != 0;
    sb[type1 ? "head_loaded" : "record_type"] = (status & 0x20) != 0;
    sb["write_protected"] = (status & 0x40) != 0;
    sb["not_ready"] = (status & 0x80) != 0;
    ret["status_bits"] = sb;

    ret["fsm_state"] = WD1793::WDSTATEToString(fdc->getFSMState());
    const uint8_t beta = fdc->getBeta128Status();
    StateNode sig = StateNode::Object();
    sig["intrq"] = (beta & WD1793::INTRQ) != 0;
    sig["drq"] = (beta & WD1793::DRQ) != 0;
    ret["signals"] = sig;
    ret["beta128_register"] = int(fdc->getBeta128Register());
    ret["density"] = fdc->isDoubleDensityMode() ? "MFM" : "FM";
    ret["selected_drive"] = int(fdc->getSelectedDriveIndex());
    ret["side"] = fdc->getSideUp() ? 1 : 0;

    ret["drives"] = DrivesNode(context, 4);
    return ret;
}

namespace
{
std::string Hex8(uint8_t v)
{
    char buf[8];
    snprintf(buf, sizeof buf, "0x%02X", v);
    return buf;
}

StateNode PagesArray(const std::vector<uint16_t>& pages)
{
    StateNode arr = StateNode::Array();
    for (uint16_t p : pages)
        arr.push(int(p));
    return arr;
}

/// Z80 windows the RAM page is mapped into right now, e.g. "0x4000-0x7FFF"
std::string Z80Access(Memory* memory, uint16_t page)
{
    std::string out;
    if (memory)
    {
        for (uint8_t bank = 0; bank < 4; bank++)
        {
            if (memory->GetRAMPageForBank(bank) != page)
                continue;
            char buf[24];
            snprintf(buf, sizeof buf, "%s0x%04X-0x%04X", out.empty() ? "" : ", ", bank * 0x4000, bank * 0x4000 + 0x3FFF);
            out += buf;
        }
    }
    return out.empty() ? "not mapped" : out;
}
}  // namespace

StateNode Screen(EmulatorContext* context, bool verbose)
{
    if (!context || !context->pScreen)
        return Unavailable("Screen not available");

    const ScreenState s = context->pScreen->DescribeScreenState();
    // In effect: the model's rule and the 'contention' switch (DeviceState::Contention has the details)
    const bool contention = s.contention && (!context->pCore || context->pCore->IsContentionSwitchOn());
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["model"] = Config::GetModelFullName(s.model);
    ret["video_mode"] = s.videoMode;
    ret["resolution"] = std::to_string(s.width) + "x" + std::to_string(s.height);
    ret["border_color"] = int(s.borderColor);
    ret["shadow_screen_capable"] = s.shadowScreenCapable;
    ret["active_screen"] = int(s.activeScreen);
    ret["active_ram_page"] = int(s.activeRamPage);
    ret["active_ram_pages"] = PagesArray(s.activeRamPages);
    ret["contention"] = contention;
    ret["flash_inverted"] = s.flashInverted;
    if (!verbose)
        return ret;

    Memory* memory = context->pMemory;
    auto screenNode = [&](const char* name, uint16_t page, bool displayed) {
        StateNode n = StateNode::Object();
        n["name"] = name;
        n["ram_page"] = int(page);
        n["pixel_data"] = "0x0000-0x17FF (6144 bytes)";
        n["attributes"] = "0x1800-0x1AFF (768 bytes)";
        n["z80_access"] = Z80Access(memory, page);
        n["ula_display"] = displayed;
        n["contention"] = contention ? "active" : "none";
        return n;
    };

    if (!s.shadowScreenCapable)
    {
        ret["screen"] = screenNode("Single screen", 5, true);
        return ret;
    }

    ret["screen_0"] = screenNode("Screen 0 (normal)", 5, s.activeScreen == 0);
    ret["screen_1"] = screenNode("Screen 1 (shadow)", 7, s.activeScreen == 1);

    StateNode port = StateNode::Object();
    port["value"] = int(s.p7FFD);
    port["value_hex"] = Hex8(s.p7FFD);
    std::string bin;
    for (int bit = 7; bit >= 0; bit--)
        bin += ((s.p7FFD >> bit) & 1) ? '1' : '0';
    port["value_bin"] = bin;
    port["ram_bank"] = int(s.p7FFD & 0x07);
    port["shadow_screen"] = (s.p7FFD & 0x08) != 0;
    port["rom_select"] = (s.p7FFD & 0x10) ? "48K BASIC" : "128K Editor";
    port["paging_locked"] = (s.p7FFD & 0x20) != 0;
    ret["port_0x7FFD"] = port;
    return ret;
}

StateNode ScreenMode(EmulatorContext* context)
{
    if (!context || !context->pScreen)
        return Unavailable("Screen not available");

    const ScreenState s = context->pScreen->DescribeScreenState();
    const VideoModeInfo& f = s.format;
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["model"] = Config::GetModelFullName(s.model);
    ret["video_mode"] = s.videoMode;
    ret["resolution"] = std::to_string(s.width) + "x" + std::to_string(s.height);
    ret["color_depth"] = f.colorDepth;
    ret["colors"] = int(f.colors);
    if (f.bpp)
        ret["bpp"] = int(f.bpp);
    if (f.attributeSize)
        ret["attribute_size"] = f.attributeSize;
    if (f.textColumns)
    {
        ret["text_columns"] = int(f.textColumns);
        ret["text_rows"] = int(f.textRows);
    }
    if (f.totalBytes)
    {
        StateNode mem = StateNode::Object();
        mem["pixel_data_bytes"] = int(f.pixelDataBytes);
        if (f.attributeBytes)
            mem["attribute_bytes"] = int(f.attributeBytes);
        if (f.planes)
            mem["planes"] = int(f.planes);
        mem["total_bytes"] = int(f.totalBytes);
        ret["memory_layout"] = mem;
    }
    ret["active_screen"] = int(s.activeScreen);
    ret["active_ram_page"] = int(s.activeRamPage);
    ret["active_ram_pages"] = PagesArray(s.activeRamPages);

    if ((s.model == MM_PENTAGON || s.model == MM_ATM3) && s.pEFF7 != 0)
    {
        StateNode eff7 = StateNode::Object();
        eff7["value"] = int(s.pEFF7);
        eff7["value_hex"] = Hex8(s.pEFF7);
        eff7["16col_enabled"] = (s.pEFF7 & EFF7_4BPP) != 0;
        eff7["512_enabled"] = (s.pEFF7 & EFF7_512) != 0;
        eff7["hwmc_enabled"] = (s.pEFF7 & EFF7_HWMC) != 0;
        eff7["384_enabled"] = (s.pEFF7 & EFF7_384) != 0;
        ret["eff7"] = eff7;
    }
    if (s.model == MM_PROFI)
    {
        StateNode dffd = StateNode::Object();
        dffd["value"] = int(s.pDFFD);
        dffd["value_hex"] = Hex8(s.pDFFD);
        dffd["video_512x240"] = (s.pDFFD & 0x80) != 0;
        dffd["scr"] = (s.pDFFD & 0x40) != 0;
        ret["dffd"] = dffd;
    }
    if (s.model == MM_ATM710 || s.model == MM_ATM3 || s.model == MM_ATM450)
    {
        StateNode ff77 = StateNode::Object();
        ff77["value"] = int(s.pFF77);
        ff77["value_hex"] = Hex8(s.pFF77);
        ff77["video_mode_bits"] = int(s.pFF77 & 0x07);
        ret["ff77"] = ff77;
    }
    return ret;
}

StateNode ScreenFlash(EmulatorContext* context)
{
    if (!context)
        return Unavailable("Emulator context not available");

    const uint64_t frame = context->emulatorState.frame_counter;
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["flash_phase"] = (frame & 0x10) ? "inverted" : "normal";
    ret["frames_until_toggle"] = int(16 - (frame % 16));
    ret["flash_cycle_position"] = int(frame % 32);
    ret["flash_cycle_total"] = 32;
    ret["toggle_interval_frames"] = 16;
    ret["toggle_interval_seconds"] = 16.0 * context->config.frame_duration_us / 1e6;
    return ret;
}

namespace
{
StateNode CountersNode(const ContentionCounters& c)
{
    static const char* const kinds[CONTENTION_KINDS] = { "fetch", "read", "write", "io" };
    StateNode n = StateNode::Object();
    uint64_t accesses = 0;
    uint64_t waitT = 0;
    for (int k = 0; k < CONTENTION_KINDS; k++)
    {
        StateNode kind = StateNode::Object();
        kind["accesses"] = c.accesses[k];
        kind["wait_t"] = c.waitT[k];
        n[kinds[k]] = kind;
        accesses += c.accesses[k];
        waitT += c.waitT[k];
    }
    n["accesses"] = accesses;
    n["wait_t"] = waitT;
    return n;
}
}  // namespace

StateNode Contention(EmulatorContext* context)
{
    UlaContention* ula = context ? context->pUlaContention : nullptr;
    Core* core = context ? context->pCore : nullptr;
    if (!ula || !core || !core->GetZ80())
        return Unavailable("Contention component not available");

    const ContentionRule rule = ula->GetRule();
    const bool effective = core->IsContentionEffective();
    Z80* z80 = core->GetZ80();

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["rule"] = ContentionRuleName(rule);
    ret["applicable"] = rule != ContentionRule::None;
    ret["switch"] = core->IsContentionSwitchOn() ? "on" : "off";
    ret["effective"] = effective;
    ret["memory_interface"] = core->GetMemoryInterfaceName();
    ret["io_rule"] = z80->ioContention ? ContentionRuleName(rule) : "none";

    // The slots the CPU would wait on (none while contention is not in effect)
    Memory* memory = context->pMemory;
    StateNode slots = StateNode::Array();
    for (uint8_t slot = 0; slot < 4; slot++)
    {
        StateNode n = StateNode::Object();
        char range[24];
        snprintf(range, sizeof range, "0x%04X-0x%04X", slot * 0x4000, slot * 0x4000 + 0x3FFF);
        n["slot"] = int(slot);
        n["range"] = range;
        n["mapping"] = memory ? memory->GetCurrentBankName(slot) : std::string("unknown");
        n["contended"] = core->IsSlotContended(slot);
        slots.push(n);
    }
    ret["slots"] = slots;

    if (rule == ContentionRule::GateArray)
        ret["floating_bus_latch"] = Hex8(ula->GetLatchedByte());

    // Counted only by the debug interfaces
    if (z80->isDebugMode)
    {
        StateNode stats = StateNode::Object();
        stats["current_frame"] = CountersNode(ula->GetStatisticsCurrentFrame());
        stats["last_frame"] = CountersNode(ula->GetStatisticsLastFrame());
        stats["total"] = CountersNode(ula->GetStatisticsTotal());
        ret["statistics"] = stats;
    }
    else
    {
        ret["statistics"] = "debug mode off (counted only while the debugger is on)";
    }

    return ret;
}

std::string ToText(const StateNode& node, int indent)
{
    std::ostringstream out;
    if (node.isObject() || node.isArray())
        TextNode(out, indent, node);
    else
        TextLine(out, indent, "", node);
    return out.str();
}

}  // namespace DeviceState
