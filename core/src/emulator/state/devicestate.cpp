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
#include "emulator/io/ide/ata/atapicdrom.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/io/rtc/ds12887.h"
#include "emulator/io/rtc/rtcaccess.h"
#include "common/stringhelper.h"
#include "common/serial/hostserialport.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/soundchip_moonsound.h"
#include "emulator/sound/chips/soundchip_turbosoundfm.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/ports/models/portdecoder_atm710.h"
#include "emulator/ports/portdecoder.h"
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

StateNode Gs(EmulatorContext* context, bool ramWindow)
{
    SoundManager* sm = context ? context->pSoundManager : nullptr;
    if (!sm)
        return Unavailable("Sound manager not available");
    GeneralSoundCard* gs = sm->getGeneralSound();
    if (!gs)
        return Unavailable("General Sound card not fitted (configure [SOUND] GSType=Z80, LW or NGS)");

    const uint8_t status = gs->getStatusRaw();
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["device"] = gs->deviceDescription();
    ret["implementation"] = gsImplementationLabel(gs->implementation());
    ret["enabled"] = true;
    ret["rom_loaded"] = gs->isROMLoaded();
    ret["firmware"] = gs->firmwareDescription();
    ret["ram_kb"] = uint64_t(gs->getRamSizeKB());
    ret["status"] = int(status);
    ret["command_pending"] = (status & 0x01) != 0;  // bit0: ZX command waiting
    ret["data_pending"] = (status & 0x80) != 0;     // bit7: GS data waiting
    // Single-latch mailbox on every personality (not a FIFO): the command
    // count mirrors command_pending; the data count mirrors data_pending on
    // the LLE card but is the lightweight card's param buffer depth (0..16)
    ret["command_queue_count"] = uint64_t(gs->getCommandQueueCount());
    ret["data_queue_count"] = uint64_t(gs->getDataQueueCount());
    ret["command_from_host"] = int(gs->getCommandFromHost());
    ret["data_from_host"] = int(gs->getDataFromHost());
    ret["data_to_host"] = int(gs->getDataToHost());
    ret["page"] = int(gs->getMPAG());  // MPAG banking latch (GS design §2.3)

    StateNode channels = StateNode::Array();
    for (int i = 0; i < gs->channelCount(); i++)
    {
        StateNode channel = StateNode::Object();
        channel["sample"] = int(gs->getChannelSample(i));
        channel["volume"] = int(gs->getChannelVolume(i));
        channels.push(channel);
    }
    ret["channels"] = channels;

    StateNode cpu = StateNode::Object();
    cpu["coprocessor"] = gs->hasCoprocessor();
    if (gs->hasCoprocessor())
    {
        cpu["pc"] = int(gs->getCPUReg(GSCpuRegister::PC));
        cpu["sp"] = int(gs->getCPUReg(GSCpuRegister::SP));
        cpu["af"] = int(gs->getCPUReg(GSCpuRegister::AF));
        cpu["halted"] = gs->isCPUHalted();
    }
    ret["cpu"] = cpu;

    NeoGSStateInfo ngs;
    if (gs->neogsState(ngs))
    {
        StateNode n = StateNode::Object();
        n["stereo_mode"] = neogsStereoModeName(sm->neoGSStereoMode());
        n["flash"] = ngs.flashTitle;
        n["flash_modified"] = ngs.flashModified;
        n["gscfg0"] = int(ngs.gscfg0);
        StateNode flags = StateNode::Array();
        flags.push((ngs.gscfg0 & 0x01) ? "ram_mode" : "rom_mode");
        if (ngs.gscfg0 & 0x02) flags.push("ramro");
        if (ngs.gscfg0 & 0x04) flags.push("8_channels");
        if (ngs.gscfg0 & 0x08) flags.push("expag");
        if (ngs.gscfg0 & 0x40) flags.push("pan4ch");
        if (ngs.gscfg0 & 0x80) flags.push("inv7b");
        n["gscfg0_flags"] = flags;
        n["clock_hz"] = ngs.clockHz;
        StateNode windows = StateNode::Array();
        for (int w = 0; w < 4; w++)
        {
            StateNode window = StateNode::Object();
            window["page"] = int(ngs.pages[w]);
            window["flash"] = ngs.windowFlash[w];
            windows.push(window);
        }
        n["windows"] = windows;
        n["led_on"] = ngs.ledOn;
        n["ready"] = ngs.readyForCommands;
        n["int_enable"] = int(ngs.intEnable);
        n["int_request"] = int(ngs.intRequest);
        n["tim_freq"] = int(ngs.timFreq);
        n["sctrl"] = int(ngs.sctrl);
        StateNode sd = StateNode::Object();
        sd["present"] = ngs.sdPresent;
        if (ngs.sdPresent)
        {
            sd["path"] = ngs.sdPath;
            sd["sdhc"] = ngs.sdSdhc;
            sd["size_bytes"] = ngs.sdSizeBytes;
            sd["blocks_read"] = ngs.sdBlocksRead;
            sd["blocks_written"] = ngs.sdBlocksWritten;
        }
        n["sd"] = sd;
        StateNode mp3 = StateNode::Object();
        mp3["fitted"] = ngs.mp3Fitted;
        if (ngs.mp3Fitted)
        {
            mp3["chip"] = ngs.mp3Chip;
            mp3["dreq"] = ngs.mp3Dreq;
            mp3["rate"] = ngs.mp3Rate;
            mp3["channels"] = ngs.mp3Channels;
            mp3["frames"] = ngs.mp3Frames;
            mp3["decode_time_s"] = ngs.mp3DecodeSeconds;
            mp3["input_fill"] = uint64_t(ngs.mp3InputFill);
        }
        n["mp3"] = mp3;
        StateNode dma = StateNode::Object();
        dma["select"] = int(ngs.dmaSelect);
        static const char* kModules[3] = {"zx", "sd", "mp3"};
        for (int m = 0; m < 3; m++)
        {
            StateNode module = StateNode::Object();
            module["running"] = ngs.dmaRunning[m];
            module["address"] = ngs.dmaAddress[m];
            dma[kModules[m]] = module;
        }
        // ZX-DMA: the host's view (neogs-zxdma-design.md §7)
        StateNode& zx = dma["zx"];
        zx["mode"] = ngs.zxMode;
        zx["overlay_installed"] = ngs.zxOverlayInstalled;
        zx["read_latch"] = int(ngs.zxReadLatch);
        zx["pending"] = ngs.zxPending;
        zx["pending_address"] = ngs.zxPendingAddress;
        zx["bytes_read"] = ngs.zxBytesRead;
        zx["bytes_written"] = ngs.zxBytesWritten;
        zx["bytes_dropped"] = ngs.zxBytesDropped;
        zx["wait_tstates"] = ngs.zxWaitTStates;
        zx["late_starts"] = ngs.zxLateStarts;
        zx["late_start_ticks"] = ngs.zxLateStartUnits;
        zx["watch_setting"] = ngs.zxWatchSetting;
        zx["watch_frames"] = ngs.zxWatchFrames;
        zx["watch_frames_left"] = int(ngs.zxWatchFramesLeft);
        n["dma"] = dma;
        ret["neogs"] = n;
    }

    // The card CPU's window #4000-#7FFF, where GS-compatible firmwares keep
    // their runtime variables (NUMPG #4080 .. MTSTAT #4151). Side-effect-free
    // peek: right on every card with a CPU
    uint8_t probe = 0;
    if (ramWindow && gs->peekCardMemory(0x4000, probe))
    {
        static const char kHexDigits[] = "0123456789abcdef";
        constexpr size_t kWindow = 0x4000;
        std::string windowHex(kWindow * 2, '0');
        for (size_t i = 0; i < kWindow; i++)
        {
            uint8_t byte = 0;
            gs->peekCardMemory(static_cast<uint16_t>(0x4000 + i), byte);
            windowHex[i * 2] = kHexDigits[byte >> 4];
            windowHex[i * 2 + 1] = kHexDigits[byte & 0x0F];
        }
        ret["fixed_window_base"] = 0x4000;
        ret["fixed_window_hex"] = windowHex;
    }
    return ret;
}

StateNode Covox(EmulatorContext* context)
{
    SoundManager* sm = context ? context->pSoundManager : nullptr;
    if (!sm)
        return Unavailable("Sound manager not available");
    if (IModelAudioSource* dac = sm->getModelAudioSource())
    {
        // The machine's own DAC holds the COVOX slot (the Sprinter's Covox / Covox-Blaster)
        StateNode ret = StateNode::Object();
        ret["available"] = true;
        ret["device"] = dac->AudioSourceName();
        ret["fitment"] = "machine";
        for (const auto& [name, value] : dac->AudioStateFields())
            ret[name] = value;
        return ret;
    }
    ::Covox* covox = sm->getCovox();
    if (!covox)
        return Unavailable("Covox not fitted (configure [SOUND] CovoxFB=1 for #FB or SD=1 for the SoundDrive)");

    const bool quad = covox->fitment() == ::Covox::Fitment::Quad;
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["device"] = quad ? "SoundDrive (4 x 8-bit DAC)" : "Covox (8-bit DAC on #FB)";
    ret["fitment"] = quad ? "quad" : "mono";

    // Ports: this model's decode, straight from its port map (the same rows
    // /ports and `ports` list), and the ones it shares with Beta-128
    StateNode ports = StateNode::Array();
    StateNode shared = StateNode::Array();
    if (PortDecoder* decoder = context->pPortDecoder)
    {
        const std::vector<PortMapEntry> map = decoder->getPortMapEntries();
        std::vector<uint8_t> betaLowBytes;
        for (const PortMapEntry& row : map)
            if (row.device && std::string(row.device).rfind("Beta128", 0) == 0)
                betaLowBytes.push_back(static_cast<uint8_t>(row.port & 0x00FF));
        for (const PortMapEntry& row : map)
        {
            const bool dac = (row.tags & Tags(PortTag::SoundCovox)) == Tags(PortTag::SoundCovox) ||
                             (row.tags & Tags(PortTag::SoundSoundDrive)) == Tags(PortTag::SoundSoundDrive);
            if (!dac)
                continue;
            StateNode p = StateNode::Object();
            p["port"] = int(row.port);
            p["mask"] = int(row.mask);
            p["match"] = int(row.match);
            p["decode"] = row.device ? row.device : "";
            if (row.gate)
                p["gate"] = row.gate;
            ports.push(p);
        }
        // A Beta-128 register whose low byte a DAC row also decodes (e.g.
        // SoundDrive mode 1 on #1F/#5F) is shared: which device answers
        // depends on whether TR-DOS is paged in
        for (uint8_t low : betaLowBytes)
            for (const PortMapEntry& row : map)
            {
                const bool dac = (row.tags & Tags(PortTag::SoundCovox)) == Tags(PortTag::SoundCovox) ||
                                 (row.tags & Tags(PortTag::SoundSoundDrive)) == Tags(PortTag::SoundSoundDrive);
                if (dac && (low & (row.mask & 0x00FF)) == (row.match & 0x00FF))
                {
                    shared.push(int(low));
                    break;
                }
            }
    }
    ret["ports"] = ports;
    ret["shared_with_beta128"] = shared;
    if (!shared.items.empty())
        ret["shared_port_rule"] = "Beta-128 owns them while TR-DOS is paged in; the DAC otherwise";

    uint8_t latches[4] = {};
    covox->getDacLatches(latches);
    static const char* kNames[4] = {"left_a", "left_b", "right_a", "right_b"};
    StateNode channels = StateNode::Array();
    for (int i = 0; i < 4; i++)
    {
        StateNode channel = StateNode::Object();
        channel["name"] = kNames[i];
        channel["latch"] = int(latches[i]);
        channel["muted"] = covox->isChannelMuted(static_cast<::Covox::Channel>(i));
        channels.push(channel);
    }
    ret["channels"] = channels;
    ret["last_left_amplitude"] = int(covox->lastLeftAmplitude());
    ret["last_right_amplitude"] = int(covox->lastRightAmplitude());
    ret["sound_last_frame"] = covox->hadSoundLastFrame();
    ret["dc_removal"] = covox->isDCRemovalEnabled();
    ret["synthesis_suppressed"] = covox->isSynthesisSuppressed();
    return ret;
}

namespace
{
SoundChip_Moonsound* MoonSoundOf(EmulatorContext* context, StateNode& unavailable)
{
    SoundManager* sm = context ? context->pSoundManager : nullptr;
    if (!sm)
    {
        unavailable = Unavailable("Sound manager not available");
        return nullptr;
    }
    SoundChip_Moonsound* ms = sm->getMoonSound();
    if (!ms)
        unavailable = Unavailable("MoonSound not fitted (configure [SOUND] MoonSound=1)");
    return ms;
}

std::string HexBytes(const uint8_t* data, size_t size)
{
    static const char kHex[] = "0123456789abcdef";
    std::string out(size * 2, '0');
    for (size_t i = 0; i < size; i++)
    {
        out[i * 2] = kHex[data[i] >> 4];
        out[i * 2 + 1] = kHex[data[i] & 0x0F];
    }
    return out;
}

/// One block mix latch (#F8 FM / #F9 PCM): 3-bit attenuation per side, 3 dB
/// steps, level 7 = muted
StateNode MixNode(uint8_t latch)
{
    StateNode n = StateNode::Object();
    n["raw"] = int(latch);
    for (const auto& [side, shift] : {std::pair<const char*, int>{"left", 0}, {"right", 3}})
    {
        const int level = (latch >> shift) & 0x07;
        StateNode s = StateNode::Object();
        s["level"] = level;
        s["muted"] = level == 7;
        if (level != 7)
            s["attenuation_db"] = -3 * level;
        n[side] = s;
    }
    return n;
}

const char* PcmPhaseName(opl4::Opl4::PcmEnvelopePhase phase)
{
    switch (phase)
    {
        case opl4::Opl4::PcmEnvelopePhase::Attack: return "attack";
        case opl4::Opl4::PcmEnvelopePhase::Decay: return "decay";
        case opl4::Opl4::PcmEnvelopePhase::Sustain: return "sustain";
        case opl4::Opl4::PcmEnvelopePhase::Release: return "release";
        case opl4::Opl4::PcmEnvelopePhase::Off: return "off";
    }
    return "off";
}

const char* FmPhaseName(opl4::Opl4::FmEnvelopePhase phase)
{
    switch (phase)
    {
        case opl4::Opl4::FmEnvelopePhase::Attack: return "attack";
        case opl4::Opl4::FmEnvelopePhase::Decay: return "decay";
        case opl4::Opl4::FmEnvelopePhase::Sustain: return "sustain";
        case opl4::Opl4::FmEnvelopePhase::Release: return "release";
        case opl4::Opl4::FmEnvelopePhase::Off: return "off";
    }
    return "off";
}

/// One OPL3 operator, decoded (register fields plus the live envelope)
StateNode FmOperatorNode(const opl4::Opl4::FmView& fm, int slot)
{
    static const double kMultFactor[16] = {0.5, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15};
    static const double kKslDbPerOctave[4] = {0.0, 3.0, 1.5, 6.0};  // register values 0..3
    static const char* kWaveforms[8] = {"sine",     "half_sine",       "abs_sine",   "pulse_sine",
                                        "sine_even", "abs_sine_even", "square",     "derived_square"};
    const opl4::Opl4::FmOperatorView& op = fm.operators[size_t(slot)];
    StateNode n = StateNode::Object();
    n["slot"] = slot;
    n["register_offset"] = slot % 22;  // operator registers 0x20/0x40/0x60/0x80/0xE0 + offset
    n["mult"] = int(op.mult);
    n["multiplier"] = kMultFactor[op.mult & 0x0F];
    n["ksr"] = op.ksr;
    n["tremolo"] = op.am;
    n["vibrato"] = op.vib;
    n["sustaining"] = op.egt;
    n["ksl"] = int(op.kslRegister);
    n["ksl_db_per_octave"] = kKslDbPerOctave[op.kslRegister & 0x03];
    n["tl"] = int(op.tl);
    n["total_level_db"] = -0.75 * op.tl + 0.0;  // + 0.0: no "-0.0" for level 0
    n["ar"] = int(op.ar);
    n["dr"] = int(op.dr);
    n["sl"] = int(op.sl);
    n["sustain_level_db"] = op.sl == 15 ? -93 : -3 * int(op.sl);
    n["rr"] = int(op.rr);
    n["waveform"] = int(op.ws);
    n["waveform_name"] = kWaveforms[op.ws & 0x07];
    n["key_on"] = op.keyOn;
    StateNode env = StateNode::Object();
    env["phase"] = FmPhaseName(op.phase);
    env["attenuation"] = int(op.attenuation);
    env["attenuation_max"] = opl4::Opl4::kFmMaxAttenuation;
    n["envelope"] = env;
    n["sounding"] = op.phase != opl4::Opl4::FmEnvelopePhase::Off && op.attenuation < opl4::Opl4::kFmMaxAttenuation;
    return n;
}

bool FmKeyOn(const opl4::Opl4::FmView& fm, int ch)
{
    const int base = (ch / 9) * 256;
    return (fm.regs[size_t(base + 0xB0 + ch % 9)] & 0x20) != 0;
}
}  // namespace

StateNode MoonSound(EmulatorContext* context)
{
    StateNode unavailable;
    SoundChip_Moonsound* ms = MoonSoundOf(context, unavailable);
    if (!ms)
        return unavailable;
    const opl4::Opl4& chip = ms->chip();
    opl4::Opl4::FmView fm;
    chip.PeekFm(fm);
    opl4::Opl4::PcmView pcm;
    chip.PeekPcm(pcm);

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["device"] = "ZXM-MoonSound (YMF278B OPL4: 18-channel FM + 24-slot wavetable)";
    ret["new_mode"] = fm.newMode;
    ret["new2_mode"] = fm.new2;
    ret["status"] = int(fm.status);

    StateNode latches = StateNode::Object();
    latches["fm_address_bank0"] = int(ms->fmAddressLatch(0));
    latches["fm_address_bank1"] = int(ms->fmAddressLatch(1));
    latches["fm_selected_bank"] = int(ms->fmSelectedBank());
    latches["wave_address"] = int(ms->waveAddressLatch());
    ret["latches"] = latches;

    StateNode mix = StateNode::Object();
    mix["fm"] = MixNode(chip.MixFmLatch());
    mix["pcm"] = MixNode(chip.MixPcmLatch());
    ret["mix"] = mix;

    const opl4::WaveMemory& memory = ms->waveMemory();
    StateNode wave = StateNode::Object();
    wave["rom_bytes"] = uint64_t(memory.RomEnd());
    wave["rom_loaded_bytes"] = uint64_t(ms->waveRomLoadedBytes());
    wave["ram_bytes"] = uint64_t(memory.RamEnd() - memory.RomEnd());
    wave["ram_dirty_pages"] = uint64_t(memory.DirtyPageCount());
    ret["wave_memory"] = wave;

    StateNode fmKeyed = StateNode::Array();
    for (int ch = 0; ch < 18; ch++)
        if (FmKeyOn(fm, ch))
            fmKeyed.push(ch);
    ret["fm_keyed_channels"] = fmKeyed;
    StateNode pcmKeyed = StateNode::Array();
    for (size_t i = 0; i < pcm.slots.size(); i++)
        if (pcm.slots[i].keyOn)
            pcmKeyed.push(int(i));
    ret["pcm_keyed_slots"] = pcmKeyed;
    return ret;
}

StateNode MoonSoundFm(EmulatorContext* context)
{
    StateNode unavailable;
    SoundChip_Moonsound* ms = MoonSoundOf(context, unavailable);
    if (!ms)
        return unavailable;
    const opl4::Opl4& chip = ms->chip();
    opl4::Opl4::FmView fm;
    chip.PeekFm(fm);

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["new_mode"] = fm.newMode;
    ret["status"] = int(fm.status);
    StateNode timers = StateNode::Array();
    for (int n = 0; n < 2; n++)
    {
        StateNode t = StateNode::Object();
        t["timer"] = n + 1;
        t["step_us"] = n == 0 ? 80 : 320;
        t["count"] = int(fm.timers[n].count);
        t["load"] = int(fm.timers[n].load);
        t["enabled"] = fm.timers[n].enabled;
        t["masked"] = fm.timers[n].masked;
        t["flag"] = (fm.status & (n == 0 ? 0x40 : 0x20)) != 0;
        timers.push(t);
    }
    ret["timers"] = timers;
    ret["four_op_connections"] = int(fm.regs[0x104]);  // bank 1 reg 0x04: 4-op pairs
    ret["rhythm"] = fm.rhythm;
    ret["tremolo_depth_db"] = (fm.regs[0xBD] & 0x80) ? 4.8 : 1.0;   // DAM
    ret["vibrato_depth_cents"] = (fm.regs[0xBD] & 0x40) ? 14 : 7;   // DVB
    ret["note_select"] = (fm.regs[0x08] & 0x40) != 0;               // NTS

    const double fmRate = double(opl4::kMasterClockHz) / double(opl4::kFmDivider);
    StateNode channels = StateNode::Array();
    for (int ch = 0; ch < 18; ch++)
    {
        const int base = (ch / 9) * 256;
        const int c = ch % 9;
        const uint8_t a0 = fm.regs[size_t(base + 0xA0 + c)];
        const uint8_t b0 = fm.regs[size_t(base + 0xB0 + c)];
        const uint8_t c0 = fm.regs[size_t(base + 0xC0 + c)];
        const int fnum = a0 | ((b0 & 0x03) << 8);
        const int block = (b0 >> 2) & 0x07;
        StateNode n = StateNode::Object();
        n["channel"] = ch;
        n["bank"] = ch / 9;
        n["fnum"] = fnum;
        n["block"] = block;
        n["frequency_hz"] = std::round(fnum * fmRate / double(1 << (20 - block)) * 100.0) / 100.0;
        n["key_on"] = (b0 & 0x20) != 0;
        n["feedback"] = (c0 >> 1) & 0x07;
        n["connection"] = c0 & 0x01;
        n["output_left"] = (fm.route[size_t(ch)] & 0x10) != 0;
        n["output_right"] = (fm.route[size_t(ch)] & 0x20) != 0;

        // 4-op pairs: channels 0-2 with 3-5 in each bank; the first carries the
        // algorithm (its CNT and the partner's), the second is its lower half
        const opl4::Opl4::FmChannelView& view = fm.channels[size_t(ch)];
        const bool pairFirst = view.fourOp && c < 3;
        n["four_op"] = view.fourOp;
        if (view.fourOp)
            n["four_op_role"] = pairFirst ? "first" : "second";
        if (pairFirst)
        {
            static const char* kAlgorithms[4] = {"fm_fm", "am_fm", "fm_am", "am_am"};
            const int partnerConn = fm.channels[size_t(ch + 3)].connection & 1;
            n["algorithm"] = kAlgorithms[(view.connection & 1) | (partnerConn << 1)];
        }
        else if (!view.fourOp)
        {
            n["algorithm"] = (view.connection & 1) ? "additive" : "fm";
        }
        if (fm.rhythm && ch >= 6 && ch <= 8)
        {
            static const char* kParts[3] = {"bass_drum", "hi_hat_snare", "tom_cymbal"};
            n["rhythm_part"] = kParts[ch - 6];
        }
        StateNode operators = StateNode::Array();
        operators.push(FmOperatorNode(fm, view.op1));
        operators.push(FmOperatorNode(fm, view.op2));
        n["operators"] = operators;
        n["sounding"] = operators.items[0].find("sounding")->b || operators.items[1].find("sounding")->b;
        n["peak"] = double(chip.ChannelPeak(opl4::ChannelId{opl4::ChannelGroup::Fm, uint8_t(ch)}));
        channels.push(n);
    }
    ret["channels"] = channels;
    ret["registers_bank0_hex"] = HexBytes(fm.regs.data(), 256);
    ret["registers_bank1_hex"] = HexBytes(fm.regs.data() + 256, 256);
    return ret;
}

StateNode MoonSoundPcm(EmulatorContext* context)
{
    StateNode unavailable;
    SoundChip_Moonsound* ms = MoonSoundOf(context, unavailable);
    if (!ms)
        return unavailable;
    const opl4::Opl4& chip = ms->chip();
    opl4::Opl4::PcmView pcm;
    chip.PeekPcm(pcm);

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["wave_enabled"] = chip.New2Mode();  // NEW2 gates wave register writes
    ret["memory_address"] = uint64_t(pcm.memAddress);
    const uint32_t romEnd = ms->waveMemory().RomEnd();
    auto panDb = [](uint16_t att) -> StateNode { return att >= 1020 ? StateNode() : StateNode(-3.0 * att / 32.0 + 0.0); };
    StateNode slots = StateNode::Array();
    for (size_t i = 0; i < pcm.slots.size(); i++)
    {
        const opl4::Opl4::PcmSlotView& s = pcm.slots[i];
        StateNode n = StateNode::Object();
        n["slot"] = int(i);
        n["wave"] = int(s.wave);
        n["octave"] = int(s.octave);
        n["fnum"] = int(s.fnum);
        n["playback_rate_hz"] =
            std::round(44100.0 * std::ldexp(1.0, s.octave) * (1024.0 + s.fnum) / 1024.0 * 100.0) / 100.0;
        n["key_on"] = s.keyOn;
        n["total_level"] = int(s.totalLevel);
        const uint8_t tlReg = pcm.regs[0x50 + i];
        n["total_level_db"] = -0.375 * (tlReg >> 1) + 0.0;  // 7-bit TL, 0.375 dB/step
        n["level_direct"] = (tlReg & 0x01) != 0;
        n["pan"] = int(s.pan);
        StateNode pan = StateNode::Object();
        pan["left_db"] = panDb(s.panLeft);    // null: that side is off
        pan["right_db"] = panDb(s.panRight);
        n["pan_attenuation"] = pan;
        n["damp"] = s.damp;
        n["sample_bits"] = s.bits == 0 ? 8 : (s.bits == 1 ? 12 : 16);
        n["start"] = uint64_t(s.start);
        n["loop"] = int(s.loop);
        n["end"] = int(0x10000 - s.endComplement);
        n["position"] = int(s.position);
        // Byte address of the current sample (12-bit samples pack two in three bytes)
        const uint32_t offset = s.bits == 0 ? s.position : (s.bits == 1 ? (s.position * 3u) / 2u : s.position * 2u);
        n["sample_address"] = uint64_t((s.start + offset) & 0x3FFFFF);
        n["memory"] = s.start < romEnd ? "rom" : "ram";
        StateNode env = StateNode::Object();
        env["phase"] = PcmPhaseName(s.phase);
        env["attenuation"] = int(s.attenuation);
        env["attenuation_max"] = opl4::Opl4::kPcmMaxAttenuation;
        env["ar"] = int(s.ar);
        env["d1r"] = int(s.d1r);
        env["d2r"] = int(s.d2r);
        env["rr"] = int(s.rr);
        env["rc"] = int(s.rc);
        // D1L: register group 7 (0xB0 + slot) bits 7:4, 3 dB/step, 15 = -93 dB
        const int dl = pcm.regs[0xB0 + i] >> 4;
        env["decay_level"] = dl;
        env["decay_level_db"] = dl == 15 ? -93 : -3 * dl;
        n["envelope"] = env;
        n["lfo"] = int(s.lfo);
        n["lfo_hz"] = opl4::Opl4::kPcmLfoHz[s.lfo & 0x07];
        n["lfo_active"] = s.lfoActive;
        n["vibrato"] = int(s.vib);
        n["am"] = int(s.am);
        n["sounding"] = s.phase != opl4::Opl4::PcmEnvelopePhase::Off &&
                        s.attenuation < opl4::Opl4::kPcmMaxAttenuation;
        n["peak"] = double(chip.ChannelPeak(opl4::ChannelId{opl4::ChannelGroup::Pcm, uint8_t(i)}));
        slots.push(n);
    }
    ret["slots"] = slots;
    ret["registers_hex"] = HexBytes(pcm.regs.data(), pcm.regs.size());
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
    ret["clock_policy"] = WD1793::ClockPolicyName(fdc->GetClockPolicy());
    ret["clock_mhz"] = static_cast<int>(fdc->GetClock());
    ret["data_rate_kbps"] = fdc->GetDataRate() == FdcDataRate::Rate500Kbps ? 500 : 250;
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
    if (s.model == MM_ATM450)
    {
        // ATM 4.50 has no #FF77: the mode is the low address byte of the last #FE write
        StateNode afe = StateNode::Object();
        afe["value"] = int(s.aFE);
        afe["value_hex"] = Hex8(s.aFE);
        afe["video_mode_bits"] = int((s.aFE >> 5) & 0x03);
        afe["rom_at_0000"] = (s.aFE & 0x80) != 0;
        ret["afe"] = afe;
    }
    if (s.model == MM_ATM710 || s.model == MM_ATM3)
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
/// One decoded attribute cell: ink/paper/bright/flash from a classic ZX
/// attribute byte (bits 0-2 ink, 3-5 paper, 6 bright, 7 flash).
StateNode AttributeCellNode(uint8_t attr)
{
    StateNode cell = StateNode::Object();
    cell["ink"] = int(attr & 0x07);
    cell["paper"] = int((attr >> 3) & 0x07);
    cell["bright"] = (attr & 0x40) != 0;
    cell["flash"] = (attr & 0x80) != 0;
    return cell;
}

/// One screen's 32x24 decoded attribute cells, read straight off the RAM
/// page (not the Z80 bank mapping) at offset 0x1800.
StateNode ScreenAttributesNode(Memory* memory, int screenIndex, uint16_t page)
{
    StateNode n = StateNode::Object();
    n["screen"] = screenIndex;
    n["ram_page"] = int(page);

    const uint8_t* base = memory ? memory->RAMPageAddress(page) : nullptr;
    StateNode cells = StateNode::Array();
    for (int row = 0; row < 24; row++)
    {
        for (int col = 0; col < 32; col++)
        {
            const uint8_t attr = base ? base[0x1800 + row * 32 + col] : 0;
            cells.push(AttributeCellNode(attr));
        }
    }
    n["cells"] = cells;
    return n;
}
}  // namespace

StateNode ScreenAttributes(EmulatorContext* context, int screen)
{
    if (!context || !context->pScreen)
        return Unavailable("Screen not available");

    const ScreenState s = context->pScreen->DescribeScreenState();
    Memory* memory = context->pMemory;

    if (screen == 1 && !s.shadowScreenCapable)
        return Unavailable("Shadow screen not available on this model");

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["model"] = Config::GetModelFullName(s.model);
    ret["cols"] = 32;
    ret["rows"] = 24;

    StateNode screens = StateNode::Array();
    if (screen == 0)
        screens.push(ScreenAttributesNode(memory, 0, 5));
    else if (screen == 1)
        screens.push(ScreenAttributesNode(memory, 1, 7));
    else if (s.shadowScreenCapable)
    {
        screens.push(ScreenAttributesNode(memory, 0, 5));
        screens.push(ScreenAttributesNode(memory, 1, 7));
    }
    else
        screens.push(ScreenAttributesNode(memory, 0, 5));
    ret["screens"] = screens;

    return ret;
}

namespace
{
StateNode CountersNode(const ContentionCounters& c)
{
    static const char* const kinds[CONTENTION_KINDS] = { "fetch", "read", "write", "io", "idle" };
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
    // The Scorpion's waits outside the contention rule: Even M1 at 3.5 MHz, and the Turbo+ logic firmware
    ret["even_m1"] = context->config.even_M1 != 0;
    if (context->config.mem_model == MM_SCORP || context->config.mem_model == MM_PROFSCORP)
        ret["scorpion_turbo_logic"] =
            context->config.scorpionTurboLogic == ScorpionTurboLogic::SC153 ? "SC15.3" : "SC15.1";
    // The ATM Turbo 2+ v7.10's RAM waits at 7 MHz (Atm710TurboOverlay): active, off (3.5 MHz) or contention_off
    if (context->config.mem_model == MM_ATM710)
    {
        const auto* atm = dynamic_cast<const PortDecoder_ATM710*>(context->pPortDecoder);
        const bool turbo = atm && atm->AreTurboRamWaitsInstalled() && context->emulatorState.hw_turbo_ratio_applied == 2;
        ret["atm710_turbo_waits"] = !turbo ? "off" : (core->IsContentionSwitchOn() ? "active" : "contention_off");
    }

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

/// region <IDE>

namespace
{
    const char* AtaCommandName(uint8_t command)
    {
        using namespace ata;
        if (command >= Command::RecalibrateFirst && command <= Command::RecalibrateLast)
            return "RECALIBRATE";
        if (command >= Command::SeekFirst && command <= Command::SeekLast)
            return "SEEK";
        switch (command)
        {
            case 0x00: return "none";
            case Command::DeviceReset: return "DEVICE RESET";
            case Command::ReadSectors:
            case Command::ReadSectorsNoRetry: return "READ SECTORS";
            case Command::ReadSectorsExt: return "READ SECTORS EXT";
            case Command::ReadMultipleExt: return "READ MULTIPLE EXT";
            case Command::WriteSectors:
            case Command::WriteSectorsNoRetry: return "WRITE SECTORS";
            case Command::WriteSectorsExt: return "WRITE SECTORS EXT";
            case Command::WriteMultipleExt: return "WRITE MULTIPLE EXT";
            case Command::ReadVerify:
            case Command::ReadVerifyNoRetry: return "READ VERIFY";
            case Command::ReadVerifyExt: return "READ VERIFY EXT";
            case Command::FormatTrack: return "FORMAT TRACK";
            case Command::ExecuteDiagnostic: return "EXECUTE DEVICE DIAGNOSTIC";
            case Command::InitializeDeviceParameters: return "INITIALIZE DEVICE PARAMETERS";
            case Command::Packet: return "PACKET";
            case Command::IdentifyPacket: return "IDENTIFY PACKET DEVICE";
            case Command::ReadMultiple: return "READ MULTIPLE";
            case Command::WriteMultiple: return "WRITE MULTIPLE";
            case Command::SetMultipleMode: return "SET MULTIPLE MODE";
            case Command::CheckPowerMode: return "CHECK POWER MODE";
            case Command::FlushCache:
            case Command::FlushCacheExt: return "FLUSH CACHE";
            case Command::Identify: return "IDENTIFY DEVICE";
            case Command::SetFeatures: return "SET FEATURES";
            default: return "other";
        }
    }

    const char* AtaPhaseName(uint8_t phase)
    {
        switch (static_cast<AtaPhase>(phase))
        {
            case AtaPhase::Idle: return "idle";
            case AtaPhase::DataIn: return "data in";
            case AtaPhase::DataOut: return "data out";
            case AtaPhase::PacketCommand: return "packet";
        }
        return "?";
    }

    StateNode Bits(uint8_t value, const std::vector<std::pair<uint8_t, const char*>>& names)
    {
        StateNode bits = StateNode::Array();
        for (const auto& [mask, name] : names)
            if (value & mask)
                bits.push(name);
        return bits;
    }

    const char* IdeGateText(IDE_SCHEME scheme)
    {
        switch (scheme)
        {
            case IDE_NEMO:
            case IDE_NEMO_A8:
            case IDE_DIVIDE: return "TR-DOS ports off";
            case IDE_NEMO_DIVIDE: return "always";
            case IDE_ATM:
            case IDE_SMUC: return "TR-DOS ports on";
            case IDE_PROFI: return "Profi EXT mode (#DFFD.5 and #7FFD.4)";
            case IDE_SPRINTER: return "the PLD port table (codes #20-#2B)";
            default: return "";
        }
    }
}  // namespace

StateNode Ide(EmulatorContext* context)
{
    IdeController* ide = context ? context->pIdeController : nullptr;
    if (!ide || !ide->Enabled())
        return Unavailable("No IDE board on this machine (configure [HDD] Scheme)");

    using namespace ata;
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["scheme"] = Config::IdeSchemeName(ide->Scheme());
    ret["gate"] = IdeGateText(ide->Scheme());
    // The channel the adapter talks to (the Sprinter selects one of two; the other boards have one)
    const uint8_t selectedChannel = context->pPortDecoder ? context->pPortDecoder->GetIdeAdapter().State().channel : 0;
    const int channelCount = ide->ChannelCount();
    AtaChannel& channel = ide->Channel(selectedChannel);
    ret["channels"] = channelCount;
    if (channelCount == 2)
        ret["selected_channel"] = selectedChannel ? "secondary" : "primary";
    ret["selected"] = channel.Selected() ? "slave" : "master";
    ret["intrq"] = channel.Intrq();

    if (context->pPortDecoder)
    {
        const IdeAdapterState& latches = context->pPortDecoder->GetIdeAdapter().State();
        StateNode adapter = StateNode::Object();
        adapter["read_latch"] = int(latches.readLatch);
        adapter["write_latch"] = int(latches.writeLatch);
        adapter["read_pair"] = latches.readPair != 0;
        adapter["write_pair"] = latches.writePair != 0;
        adapter["write_high_armed"] = latches.writeHigh != 0;
        if (ide->Scheme() == IDE_SPRINTER)
        {
            // One PLD latch (HDDR) for both directions: read_latch is that register
            adapter["data_latch"] = int(latches.readLatch);
            adapter["channel"] = int(latches.channel);
        }
        ret["adapter"] = adapter;
    }

    StateNode units = StateNode::Array();
    for (int index = 0; index < channelCount * AtaChannel::kUnits; index++)
    {
        const int channelIndex = index / AtaChannel::kUnits;
        const int unit = index % AtaChannel::kUnits;
        AtaDevice* device = ide->Channel(channelIndex).Unit(unit);
        if (!device)
            continue;
        const AtaDeviceState& s = device->State();
        const bool cd = device->Kind() == AtaDeviceKind::Cdrom;
        StateNode u = StateNode::Object();
        u["position"] = unit ? "slave" : "master";
        u["slot"] = IdeUnitSlot::IdFor(channelIndex, unit);
        if (channelCount == 2)
        {
            u["channel"] = channelIndex ? "secondary" : "primary";
            u["selected"] = ide->Channel(channelIndex).Selected() == unit;
        }
        u["kind"] = cd ? "cdrom" : "disk";
        u["present"] = device->IsPresent();

        if (IBlockDevice* medium = device->Medium())
        {
            StateNode m = StateNode::Object();
            m["description"] = medium->Describe();
            m["sectors"] = static_cast<uint64_t>(medium->SectorCount());
            if (cd)
                m["blocks"] = static_cast<uint64_t>(medium->SectorCount() / AtapiCdrom::kSectorsPerBlock);
            m["writable"] = medium->IsWritable();
            u["medium"] = m;
        }
        else
            u["medium"] = StateNode();
        if (!cd)
        {
            StateNode chs = StateNode::Object();
            chs["cylinders"] = static_cast<unsigned>(s.cylinders);
            chs["heads"] = int(s.heads);
            chs["sectors"] = int(s.sectors);
            u["translation"] = chs;
            u["write_protect"] = device->Config().writeProtect;
            u["multiple"] = int(s.multiple);
        }

        StateNode task = StateNode::Object();
        task["features"] = int(s.features);
        task["sector_count"] = int(s.sectorCount);
        task["lba_low"] = int(s.lbaLow);
        task["lba_mid"] = int(s.lbaMid);
        task["lba_high"] = int(s.lbaHigh);
        task["device"] = int(s.device);
        task["status"] = int(s.status);
        task["status_bits"] = Bits(s.status, {{Status::BSY, "BSY"}, {Status::DRDY, "DRDY"}, {Status::DF, "DF"},
                                              {Status::DSC, "DSC"}, {Status::DRQ, "DRQ"}, {Status::CORR, "CORR"},
                                              {Status::IDX, "IDX"}, {Status::ERR, "ERR"}});
        task["error"] = int(s.error);
        task["error_bits"] = Bits(s.error, {{Error::ICRC, "ICRC"}, {Error::UNC, "UNC"}, {Error::MC, "MC"},
                                            {Error::IDNF, "IDNF"}, {Error::MCR, "MCR"}, {Error::ABRT, "ABRT"},
                                            {Error::TK0NF, "TK0NF"}, {Error::AMNF, "AMNF"}});
        task["control"] = int(s.control);
        task["control_bits"] = Bits(s.control, {{DeviceControl::HOB, "HOB"}, {DeviceControl::SRST, "SRST"},
                                                {DeviceControl::nIEN, "nIEN"}});
        u["task_file"] = task;

        StateNode command = StateNode::Object();
        command["code"] = int(s.command);
        command["name"] = AtaCommandName(s.command);
        command["phase"] = AtaPhaseName(s.phase);
        command["lba"] = static_cast<uint64_t>(s.lba);
        command["left"] = static_cast<unsigned>(s.sectorsLeft);
        command["buffer_position"] = int(s.bufferPos);
        command["buffer_length"] = int(s.bufferLen);
        command["intrq_pending"] = s.intrq != 0;
        u["command"] = command;

        if (cd)
        {
            StateNode atapi = StateNode::Object();
            atapi["disc"] = device->Medium() != nullptr;
            atapi["unit_attention"] = s.unitAttention != 0;
            atapi["byte_count_limit"] = int(s.byteLimit);
            atapi["transfer_left"] = static_cast<unsigned>(s.transferLeft);
            atapi["sense_key"] = int(s.senseKey);
            atapi["asc"] = int(s.asc);
            atapi["ascq"] = int(s.ascq);
            char cdb[40];
            std::snprintf(cdb, sizeof(cdb), "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", s.cdb[0], s.cdb[1],
                          s.cdb[2], s.cdb[3], s.cdb[4], s.cdb[5], s.cdb[6], s.cdb[7], s.cdb[8], s.cdb[9], s.cdb[10], s.cdb[11]);
            atapi["last_packet"] = cdb;
            u["atapi"] = atapi;
        }
        units.push(u);
    }
    ret["units"] = units;
    return ret;
}

/// endregion </IDE>


namespace
{
    const char* TimeModeName(Ds12887::TimeMode mode)
    {
        switch (mode)
        {
            case Ds12887::TimeMode::Emulated:
                return "emulated";
            case Ds12887::TimeMode::Fixed:
                return "fixed";
            case Ds12887::TimeMode::Host:
            default:
                return "host";
        }
    }

    std::string Hex2(unsigned value)
    {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02X", value & 0xFF);
        return buf;
    }
}  // namespace

namespace
{
const char* NetProtoName(NetProto p)
{
    switch (p)
    {
        case NetProto::Tcp: return "tcp";
        case NetProto::Udp: return "udp";
        case NetProto::Icmp: return "icmp";
        case NetProto::Serial: return "serial";
    }
    return "?";
}

const char* NetStatusName(NetEventStatus s)
{
    return NetStatusText(s);
}

const char* W5300StateName(uint8_t ssr)
{
    switch (ssr)
    {
        case 0x00: return "CLOSED";
        case 0x01: return "ARP";
        case 0x13: return "INIT";
        case 0x14: return "LISTEN";
        case 0x15: return "SYNSENT";
        case 0x16: return "SYNRECV";
        case 0x17: return "ESTABLISHED";
        case 0x18: return "FIN_WAIT";
        case 0x1B: return "TIME_WAIT";
        case 0x1C: return "CLOSE_WAIT";
        case 0x1D: return "LAST_ACK";
        case 0x22: return "UDP";
        case 0x32: return "IPRAW";
        case 0x42: return "MACRAW";
    }
    return "?";
}

std::string Endpoint(const NetEndpoint& e)
{
    return NetIpToString(e.addr) + ":" + std::to_string(e.port);
}

std::string Ip4(const std::array<uint8_t, 256>& r, size_t at)
{
    return NetIpToString(NetIp(r[at], r[at + 1], r[at + 2], r[at + 3]));
}
}  // namespace

StateNode Network(EmulatorContext* context)
{
    NetworkManager* manager = (context && context->pCore) ? context->pCore->GetNetworkManager() : nullptr;
    if (!manager)
        return Unavailable("no network support in this machine");
    const NetworkManager::Status st = manager->GetStatus();

    // Always available: the settings and what the machine offers are worth
    // seeing with nothing fitted (the Qt Network window builds on this)
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["frame"] = st.frame;

    // The settings in force, as network_configure / POST /network/config take them
    StateNode& set = ret["settings"];
    set["card"] = st.settings.card;
    set["com_port"] = st.settings.comPort;
    set["zx_wifi"] = st.settings.zxWifi;
    set["esp_chip"] = st.settings.espChip;
    set["com_modem_lines"] = st.settings.comModemLines;
    set["avr_firmware"] = st.settings.avrFirmware;
    set["atm2ioesp"] = st.settings.atm2IoEsp;
    set["atm2ioesp_address"] = StringHelper::Format("0x%02X", st.settings.atm2IoEspAddress);
    if (!st.settings.kbcFirmware.empty())
        set["kbc_firmware"] = st.settings.kbcFirmware;
    set["host_access"] = st.settings.hostAccess;
    set["dns_mode"] = st.settings.dnsMode;
    set["hosts"] = st.settings.hosts;
    set["forwards"] = st.settings.forwards;
    set["connect_timeout_ms"] = st.settings.connectTimeoutMs;

    // Devices a SERIAL: peer can open (read now: a USB adapter comes and goes)
    StateNode& devices = ret["host_serial_devices"];
    devices = StateNode::Array();
    for (const std::string& device : HostSerialPort::ListDevices())
        devices.push(StateNode(device));

    // What the machine offers, what is plugged, what could not be fitted and why
    StateNode& machine = ret["machine"];
    machine["zx_bus"] = st.zxBus;
    machine["serial_port"] = st.serialPort;
    machine["internal_io"] = st.internalIo;
    ret["cards"] = st.cards;
    if (!st.notes.empty())
    {
        StateNode& notes = ret["not_fitted"];
        notes = StateNode::Array();
        for (const std::string& n : st.notes)
            notes.push(StateNode(n));
    }

    // A serial port's peer (either port)
    auto peerFields = [](StateNode& node, const NetworkManager::Status::Com& c) {
        node["peer"] = c.peer.empty() ? std::string("none") : c.peer;
        if (!c.target.empty())
            node["target"] = c.target;
        node["connected"] = c.connected;
        if (!c.phase.empty())
            node["phase"] = c.phase;
        if (!c.error.empty())
            node["error"] = c.error;
        node["modem_lines"] = c.modemLines;
        if (!c.exchanges.empty() || c.requests)
        {
            node["requests"] = c.requests;
            StateNode& log = node["recent_exchanges"];
            log = StateNode::Array();
            for (const auto& [request, reply] : c.exchanges)
            {
                StateNode e = StateNode::Object();
                e["request"] = request;
                e["reply"] = reply;
                log.push(std::move(e));
            }
        }
        node["baud"] = c.baud;
        node["frame_bits"] = c.frameBits;
        node["peer_pending"] = uint64_t(c.pending);
        if (c.peerBaud)
            node["peer_baud"] = c.peerBaud;   // an ESP module's own rate: a mismatch with "baud" garbles both sides
    };

    // The machine's own serial port when it is no 16550 (ATM Turbo 2+
    // keyboard controller): the MCU's UART line and its peer
    StateNode& machineSerial = ret["machine_serial"];
    machineSerial["fitted"] = st.machineSerial.fitted;
    if (st.machineSerial.fitted)
    {
        const NetworkManager::Status::Com& m = st.machineSerial;
        machineSerial["flavor"] = m.flavor;
        machineSerial["kbc_firmware"] = m.firmware;
        peerFields(machineSerial, m);
        machineSerial["rts"] = m.rts;
        machineSerial["dtr"] = m.dtr;
        machineSerial["bytes_in"] = m.bytesIn;
        machineSerial["bytes_out"] = m.bytesOut;
        machineSerial["lost"] = m.lost;
    }

    // A 16550's registers as the Z80 sees them (the #xxEF port, the ATM2IOESP card)
    auto uartFields = [](StateNode& node, const NetworkManager::Status::Com& c) {
        const Uart16550::View& u = c.uart;
        node["divisor"] = int(u.divisor);
        node["lcr"] = StringHelper::Format("#%02X", u.lcr);
        node["mcr"] = StringHelper::Format("#%02X", u.mcr);
        node["lsr"] = StringHelper::Format("#%02X", u.lsr);
        node["msr"] = StringHelper::Format("#%02X", u.msr);
        node["ier"] = StringHelper::Format("#%02X", u.ier);
        node["iir"] = StringHelper::Format("#%02X", u.iir);
        node["scr"] = StringHelper::Format("#%02X", u.scr);
        node["rts"] = (u.mcr & Uart16550::kMcrRts) != 0;
        node["cts"] = (u.msr & Uart16550::kMsrCts) != 0;
        node["rx_fifo"] = int(u.rxCount);
        node["tx_fifo"] = int(u.txCount);
        node["bytes_in"] = u.bytesIn;
        node["bytes_out"] = u.bytesOut;
        node["overruns"] = u.overruns;
    };

    // The COM port (TDD §7): the UART as the Z80 sees it and the peer
    StateNode& com = ret["com_port"];
    com["fitted"] = st.com.fitted;
    if (st.com.fitted)
    {
        com["flavor"] = st.com.flavor;
        if (!st.com.firmware.empty())
            com["avr_firmware"] = st.com.firmware;
        peerFields(com, st.com);
        uartFields(com, st.com);
    }

    // The ATM2IOESP card on the ATM Turbo 2+ INTERNAL I/O connector: bus address (#FB), the 16550 and the peer
    StateNode& ioEsp = ret["atm2ioesp"];
    ioEsp["fitted"] = st.atm2IoEsp.fitted;
    if (st.atm2IoEsp.fitted)
    {
        ioEsp["address"] = StringHelper::Format("0x%02X", st.atm2IoEspAddress);
        peerFields(ioEsp, st.atm2IoEsp);
        uartFields(ioEsp, st.atm2IoEsp);
    }

    StateNode& card = ret["card"];
    card["kind"] = st.fitted ? st.card : std::string("none");
    if (st.fitted)
    {
        card["port_83AB"] = StringHelper::Format("#%02X", st.control);
        card["port_82AB"] = StringHelper::Format("#%02X", st.mode);
        card["port_81AB"] = StringHelper::Format("#%02X", st.addressHigh);
        card["w5300_running"] = st.chipRunning;
        card["w5300_int"] = st.chipInt;
        card["int_to_z80"] = st.intToZ80;
        card["w5300_in_io_space"] = (st.mode & 0x10) != 0 && (st.mode & 0x04) == 0;
        card["mac"] = StringHelper::Format("%02X:%02X:%02X:%02X:%02X:%02X", st.common[8], st.common[9], st.common[10],
                                            st.common[11], st.common[12], st.common[13]);
        card["ip"] = Ip4(st.common, 0x18);
        card["gateway"] = Ip4(st.common, 0x10);
        card["mask"] = Ip4(st.common, 0x14);
        StateNode& chipSockets = card["sockets"];
        chipSockets = StateNode::Array();
        for (size_t n = 0; n < st.chipSockets.size(); ++n)
        {
            const W5300::SocketView& v = st.chipSockets[n];
            StateNode s = StateNode::Object();
            s["n"] = int(n);
            s["mode"] = v.mode == 1 ? "TCP" : v.mode == 2 ? "UDP" : v.mode == 3 ? "IPRAW" : v.mode == 0 ? "CLOSED" : "OTHER";
            s["state"] = W5300StateName(v.state);
            s["ssr"] = StringHelper::Format("#%02X", v.state);
            s["ir"] = StringHelper::Format("#%02X", v.ir);
            s["source_port"] = int(v.sourcePort);
            s["destination"] = Endpoint(v.destination);
            s["tx_free"] = v.txFree;
            s["rx_received"] = v.rxReceived;
            s["tcp_backlog"] = uint64_t(v.tcpBacklog);
            s["network_socket"] = int(v.networkSocket);
            chipSockets.push(std::move(s));
        }
    }

    StateNode& net = ret["virtual_network"];
    net["host_access"] = st.hostAccess;
    net["network"] = NetIpToString(st.config.network) + "/" + NetIpToString(st.config.mask);
    net["gateway"] = NetIpToString(st.config.gateway);
    net["dns"] = NetIpToString(st.config.dnsServer);
    net["dns_mode"] = st.config.dnsMode == VirtualNetworkConfig::DnsMode::Host ? "host" : "pass";
    StateNode& hosts = net["hosts"];
    hosts = StateNode::Object();
    for (const auto& [name, addr] : st.config.hosts)
        hosts[name] = NetIpToString(addr);
    StateNode& leases = net["dhcp_leases"];
    leases = StateNode::Array();
    for (const auto& [mac, addr] : st.leases)
    {
        StateNode l = StateNode::Object();
        l["mac"] = StringHelper::Format("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        l["ip"] = NetIpToString(addr);
        leases.push(std::move(l));
    }
    StateNode& sockets = net["sockets"];
    sockets = StateNode::Array();
    for (const VirtualNetwork::SocketInfo& i : st.sockets)
    {
        StateNode s = StateNode::Object();
        s["id"] = int(i.id);
        s["proto"] = NetProtoName(i.proto);
        s["connected"] = i.connected;
        s["remote"] = Endpoint(i.remote);
        if (i.listenPort)
            s["listen_port"] = int(i.listenPort);
        s["bytes_in"] = i.bytesIn;
        s["bytes_out"] = i.bytesOut;
        sockets.push(std::move(s));
    }
    StateNode& servers = net["guest_servers"];
    servers = StateNode::Array();
    for (const VirtualNetwork::ListenerInfo& l : st.listeners)
    {
        StateNode s = StateNode::Object();
        s["guest_port"] = int(l.guestPort);
        s["host_port"] = int(l.hostPort);
        s["waiting_sockets"] = uint64_t(l.waitingSockets);
        s["pending_clients"] = uint64_t(l.pendingClients);
        servers.push(std::move(s));
    }
    StateNode& counters = net["counters"];
    counters["dhcp_replies"] = st.counters.dhcpReplies;
    counters["dns_local_answers"] = st.counters.dnsLocalAnswers;
    counters["dns_host_queries"] = st.counters.dnsHostQueries;
    counters["echo_replies"] = st.counters.echoReplies;
    counters["host_events"] = st.counters.hostEvents;
    counters["link_resets"] = st.counters.linkResets;
    StateNode& activity = net["recent_activity"];
    activity = StateNode::Array();
    for (const VirtualNetwork::Activity& a : st.activity)
    {
        StateNode e = StateNode::Object();
        e["frame"] = a.frame;
        e["socket"] = int(a.socket);
        e["proto"] = NetProtoName(a.proto);
        e["action"] = a.action;
        e["remote"] = Endpoint(a.remote);
        if (a.status != NetEventStatus::Ok)
            e["status"] = NetStatusName(a.status);
        if (a.bytes)
            e["bytes"] = a.bytes;
        activity.push(std::move(e));
    }
    return ret;
}

StateNode Rtc(EmulatorContext* context)
{
    std::string reason;
    Ds12887* chip = RtcAccess::Find(context, &reason);
    if (!chip)
        return Unavailable(reason.c_str());

    const PortDecoder::RtcBinding binding = context->pPortDecoder->GetRtcBinding();
    auto peek = [chip](uint8_t index) { return chip->PeekRegister(index); };
    const uint8_t a = peek(Ds12887::kRegA);
    const uint8_t b = peek(Ds12887::kRegB);
    const uint8_t c = peek(Ds12887::kRegC);
    const uint8_t d = peek(Ds12887::kRegD);
    const bool binary = (b & Ds12887::kBBinary) != 0;
    const bool hour24 = (b & Ds12887::kB24Hour) != 0;
    auto decode = [binary](uint8_t v) { return binary ? int(v) : int((v >> 4) * 10 + (v & 0x0F)); };

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["chip"] = chip->ChipName();
    ret["ports"] = binding.ports;
    ret["cells"] = int(chip->GetCellCount());
    ret["nvram_file"] = binding.nvramFile.empty() ? std::string("(none: kept for the session only)") : binding.nvramFile;
    ret["address_latch"] = int(chip->GetAddress());
    ret["time_mode"] = TimeModeName(chip->GetTimeMode());
    if (chip->RegistersNote()[0] != '\0')
        ret["note"] = chip->RegistersNote();

    // The time as the guest reads it now, decoded per register B
    const uint8_t hoursRaw = peek(Ds12887::kHours);
    int hours = decode(static_cast<uint8_t>(hoursRaw & (hour24 ? 0xFF : 0x7F)));
    if (!hour24)
        hours = (hours % 12) + ((hoursRaw & 0x80) ? 12 : 0);
    const int year = decode(peek(Ds12887::kYear));
    const int month = decode(peek(Ds12887::kMonth));
    const int day = decode(peek(Ds12887::kDay));
    const int minutes = decode(peek(Ds12887::kMinutes));
    const int seconds = decode(peek(Ds12887::kSeconds));
    StateNode time = StateNode::Object();
    time["year"] = year;
    time["month"] = month;
    time["day"] = day;
    time["hours"] = hours;
    time["minutes"] = minutes;
    time["seconds"] = seconds;
    time["day_of_week"] = int(peek(Ds12887::kDayOfWeek));
    char text[32];
    std::snprintf(text, sizeof(text), "%02d-%02d-%02d %02d:%02d:%02d", year, month, day, hours, minutes, seconds);
    time["text"] = text;
    ret["time"] = time;

    StateNode regA = StateNode::Object();
    regA["value"] = int(a);
    regA["uip"] = (a & 0x80) != 0;
    regA["divider"] = int((a >> 4) & 0x07);
    regA["rate"] = int(a & 0x0F);
    ret["register_a"] = regA;

    StateNode regB = StateNode::Object();
    regB["value"] = int(b);
    regB["set"] = (b & 0x80) != 0;
    regB["periodic_irq"] = (b & 0x40) != 0;
    regB["alarm_irq"] = (b & 0x20) != 0;
    regB["update_irq"] = (b & 0x10) != 0;
    regB["square_wave"] = (b & 0x08) != 0;
    regB["binary"] = binary;
    regB["hour_24"] = hour24;
    regB["daylight_saving"] = (b & 0x01) != 0;
    ret["register_b"] = regB;

    StateNode regC = StateNode::Object();
    regC["value"] = int(c);
    regC["irq"] = (c & 0x80) != 0;
    regC["periodic"] = (c & 0x40) != 0;
    regC["alarm"] = (c & 0x20) != 0;
    regC["update_ended"] = (c & 0x10) != 0;
    ret["register_c"] = regC;

    StateNode regD = StateNode::Object();
    regD["value"] = int(d);
    regD["battery_ok"] = (d & 0x80) != 0;
    ret["register_d"] = regD;

    StateNode alarm = StateNode::Object();
    alarm["seconds"] = int(peek(Ds12887::kSecondsAlarm));
    alarm["minutes"] = int(peek(Ds12887::kMinutesAlarm));
    alarm["hours"] = int(peek(Ds12887::kHoursAlarm));
    ret["alarm"] = alarm;

    // Every cell as the guest reads it (peeked, no side effects), 16 per line
    StateNode dump = StateNode::Array();
    for (size_t row = 0; row < chip->GetCellCount(); row += 16)
    {
        std::string line = Hex2(unsigned(row)) + ":";
        for (size_t col = 0; col < 16 && row + col < chip->GetCellCount(); ++col)
            line += " " + Hex2(peek(static_cast<uint8_t>(row + col)));
        dump.push(line);
    }
    ret["dump"] = dump;
    return ret;
}

}  // namespace DeviceState
