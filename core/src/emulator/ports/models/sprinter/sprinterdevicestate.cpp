// DeviceState::Sprinter and its port-table / paging views - the Sprinter reports every
// automation interface renders (declared in emulator/state/devicestate.h; built here, beside
// the Sprinter code, so the shared state code names no Sprinter type). Design:
// docs/inprogress/2026-09-28-sprinter/tdd-integration.md §3, as built:
// docs/inprogress/2026-09-28-sprinter/automation-outcome.md

#include "emulator/io/ide/ideadapter.h"
#include "stdafx.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>

#include "emulator/state/devicestate.h"

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/tape/tape.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/rom.h"
#include "emulator/memory/sprinter/sprintermemory.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/emulator.h"
#include "emulator/ports/models/sprinter/sprinterbios.h"
#include "emulator/ports/models/sprinter/sprinterpldconfig.h"
#include "emulator/ports/models/sprinter/sprinterporttable.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/sound/sprinter/covoxblaster.h"
#include "emulator/video/sprinter/screensprinter.h"
#include "emulator/video/sprinter/sprinterintsource.h"
#include "emulator/video/sprinter/sprintervideoram.h"

namespace
{
StateNode Unavailable(const char* description)
{
    StateNode n = StateNode::Object();
    n["available"] = false;
    n["description"] = description;
    return n;
}

PortDecoder_Sprinter* SprinterDecoder(EmulatorContext* context)
{
    return context ? dynamic_cast<PortDecoder_Sprinter*>(context->pPortDecoder) : nullptr;
}

std::string Hex8(unsigned value) { return StringHelper::Format("0x%02X", value & 0xFFu); }
std::string Hex16(unsigned value) { return StringHelper::Format("0x%04X", value & 0xFFFFu); }
std::string Hex32(uint32_t value) { return StringHelper::Format("0x%08X", value); }

/// The mode table's square grid: the picture is 40 x 32 squares from (0, 0), the table 56 x 40
constexpr uint8_t kPictureColumns = 40;
constexpr uint8_t kPictureRows = 32;
constexpr uint8_t kSquareRowsAll = 40;

/// Space-separated hex bytes ("00 05 02 ...")
std::string HexRow(const uint8_t* data, size_t count)
{
    std::string out;
    for (size_t i = 0; i < count; i++)
    {
        if (i)
            out += ' ';
        out += StringHelper::Format("%02X", data[i]);
    }
    return out;
}

const char* ConfigStateName(uint8_t state)
{
    switch (state)
    {
        case SprinterConfigState::Unconfigured: return "unconfigured";
        case SprinterConfigState::Loading: return "loading";
        case SprinterConfigState::Configured: return "configured";
        default: return "unknown";
    }
}

/// The decoder's code names (GetPortTraceCodeTable: the port trace and these views share them)
std::map<uint16_t, std::string> CodeNames(const PortDecoder_Sprinter& decoder)
{
    std::map<uint16_t, std::string> names;
    for (const PortTraceCodeName& entry : decoder.GetPortTraceCodeTable())
        names.emplace(entry.code, entry.name);
    return names;
}

std::string CodeName(const std::map<uint16_t, std::string>& names, uint16_t code)
{
    auto it = names.find(code);
    return it != names.end() ? it->second : StringHelper::Format("Code%02X", code & 0xFFu);
}

/// One window of the CPU's address space as the PLD maps it
StateNode Window(EmulatorContext* context, PortDecoder_Sprinter& decoder, uint8_t window)
{
    static const char* const kRanges[4] = {"0x0000-0x3FFF", "0x4000-0x7FFF", "0x8000-0xBFFF", "0xC000-0xFFFF"};
    Memory& memory = *context->pMemory;
    const SprinterPldState& pld = decoder.GetPldState();
    auto* sprinterMemory = dynamic_cast<SprinterMemory*>(context->pMemory);

    StateNode w = StateNode::Object();
    w["window"] = int(window);
    w["address_range"] = kRanges[window];

    const MemoryBankModeEnum mode = memory.GetMemoryBankMode(window);
    const bool configured = pld.configState == SprinterConfigState::Configured;
    const SprinterMemory::ReadRedirect redirect =
        sprinterMemory ? sprinterMemory->GetReadRedirect(window) : SprinterMemory::ReadRedirect::None;
    const SprinterMemory::BankAction action =
        sprinterMemory ? sprinterMemory->GetBankAction(window) : SprinterMemory::BankAction::Plain;

    std::string kind;
    int page = -1;
    bool writable = false;
    std::string note;
    if (mode == BANK_ROM)
    {
        page = memory.GetROMPageForBank(window);
        kind = configured ? "ROM" : "loader ROM";
        if (!configured)
            note = redirect == SprinterMemory::ReadRedirect::LoadingCs ? "the PLD loads: writes are configuration bits; reads above the Z84C15 CS0 come from fast RAM"
                                                                       : "the PLD loads: writes are configuration bits";
        else
            note = "system ROM: ROM_RG (code #8F / port #5C) and SYS_PG";
    }
    else if (mode == BANK_CACHE)
    {
        page = static_cast<int>(memory.GetPageForBank(window)) - static_cast<int>(MAX_RAM_PAGES);
        kind = "fast RAM";
        writable = true;
        note = "IN #FB on, IN #7B off; page = ROM_RG bits 1-0";
    }
    else
    {
        page = memory.GetRAMPageForBank(window);
        writable = true;
        if (redirect == SprinterMemory::ReadRedirect::Graphics)
        {
            kind = "graphics";
            writable = action == SprinterMemory::BankAction::Graphics;
            note = "pages #50-#5F: reads and writes go to video address PORT_Y x 1024 + A9-A0";
        }
        else if (redirect == SprinterMemory::ReadRedirect::Isa)
        {
            kind = "ISA";
            writable = false;
            note = "ISA view (#1FFD bit 4, pages #D0-#D6): no card, reads #FF, writes ignored";
        }
        else if (window == 0 && (pld.sc & 0x01) && pld.ramSys)
        {
            kind = "RAM";
            note = "system RAM in window 0 (#1FFD bit 0 with RAM_SYS), page from the cells #E0-#EF";
        }
        else if (window == 0)
        {
            kind = "vROM";
            writable = false;
            note = "Spectrum ROM image in RAM (read-only), from the cells #E0-#EF";
        }
        else if (window == 3 && pld.starting && page == SprinterMemory::kPortTablePage)
        {
            kind = "port table";
            note = "the PLD just reset: page #40 until the first port read";
        }
        else if (action == SprinterMemory::BankAction::ResetPage)
        {
            kind = "RAM (reset page)";
            note = "page #A0 with #1FFD = #10: a write resets the CPU";
        }
        else if (action == SprinterMemory::BankAction::CblPage)
        {
            kind = "RAM (Covox-Blaster page)";
            note = "page #FD: accelerator copies into it also feed the Covox-Blaster ring while its INT is on";
        }
        else
            kind = "RAM";
    }

    w["kind"] = kind;
    w["page"] = page;
    w["page_hex"] = page >= 0 ? Hex8(static_cast<unsigned>(page)) : std::string("-");
    w["writable"] = writable;
    // The cell that names the page (window 3: the Spectrum page's cell #D0-#FF)
    if (configured)
    {
        if (window == 1)
            w["cell"] = "#E9";
        else if (window == 2)
            w["cell"] = "#EA";
        else if (window == 3)
            w["cell"] = StringHelper::Format("#%02X", 0xC0 + (pld.pg3 & 0x3F));
        else if (window == 0 && mode == BANK_RAM)
            w["cell"] = "#E0-#EF";
    }
    if (!note.empty())
        w["note"] = note;
    return w;
}

StateNode VideoSummary(PortDecoder_Sprinter& decoder, EmulatorContext* context)
{
    const SprinterPldState& pld = decoder.GetPldState();
    const SprinterVideoRam& vram = decoder.GetVideoRam();
    const uint8_t modePage = pld.rgMod & 0x01;
    auto* screen = dynamic_cast<ScreenSprinter*>(context->pScreen);
    const uint16_t lines = screen ? screen->FrameLines() : (pld.frameLines ? 312 : 320);

    // The 56 x 40 squares of the mode page; the picture is the 40 x 32 squares from (0, 0)
    // (SprinterSquare: the classifier ScreenSprinter::DescribeScreenState shares)
    constexpr int kKinds = static_cast<int>(SprinterSquare::Kind::Count);
    int all[kKinds] = {};
    int picture[kKinds] = {};
    int lowres = 0;
    int intArmed = 0;
    for (uint8_t b = 0; b < kSquareRowsAll; b++)
    {
        for (uint8_t a = 0; a < SprinterIntSource::kSquareColumns; a++)
        {
            const SprinterSquare square = SprinterSquare::Decode(vram.Data() + SprinterVideoRam::ModeAddress(a, b, modePage));
            all[static_cast<int>(square.kind)]++;
            if (a < kPictureColumns && b < kPictureRows)
                picture[static_cast<int>(square.kind)]++;
            lowres += square.LowRes() ? 1 : 0;
            intArmed += square.IntArmed() ? 1 : 0;
        }
    }

    StateNode v = StateNode::Object();
    v["mode_page"] = int(modePage);
    int best = 0;
    for (int k = 1; k < kKinds; k++)
        if (picture[k] > picture[best])
            best = k;
    v["picture_mode"] = SprinterSquare::Name(static_cast<SprinterSquare::Kind>(best));
    v["picture_mode_squares"] = StringHelper::Format("%d of 1280", picture[best]);
    StateNode squares = StateNode::Object();
    for (int k = 0; k < kKinds; k++)
        squares[SprinterSquare::Key(static_cast<SprinterSquare::Kind>(k))] = all[k];
    squares["low_res"] = lowres;
    squares["int_armed"] = intArmed;
    v["squares"] = squares;
    v["port_y"] = int(pld.portY);
    v["border"] = int(pld.Cell(SprinterCode::Border) & 0x07);
    StateNode hold = StateNode::Object();
    hold["value"] = Hex8(pld.hold);
    hold["x_pixels"] = (7 - static_cast<int>(pld.hold & 0x0F)) * 2;
    hold["y_lines"] = 7 - static_cast<int>(pld.hold >> 4);
    v["hold"] = hold;

    // The frame INT positions the mode table places (base T-states, 3.5 MHz)
    const std::vector<uint32_t> positions = SprinterIntSource::ComputePositions(vram, modePage, lines);
    StateNode ints = StateNode::Array();
    for (size_t i = 0; i < positions.size() && i < 8; i++)
        ints.push(positions[i]);
    v["int_count"] = static_cast<uint64_t>(positions.size());
    v["int_positions"] = ints;
    v["keyboard_int_latched"] = decoder.GetIntSource().KeyboardIntLatched();
    return v;
}

StateNode Z84Summary(PortDecoder_Sprinter& decoder, EmulatorContext* context)
{
    Z84Lib::Z84C15& chip = decoder.GetZ84();
    StateNode z = StateNode::Object();
    Z80* z80 = context->pCore ? context->pCore->GetZ80() : nullptr;
    z["engine"] = (z80 && z80->GetEngine()) ? "z84c15 library (Z84C15Engine)" : "native Z80 interpreter";

    const Z84Lib::Z84SystemRegs& sys = chip.system;
    StateNode s = StateNode::Object();
    s["wcr"] = Hex8(sys.wcr);
    s["mwbr"] = Hex8(sys.mwbr);
    s["csbr"] = Hex8(sys.csbr);
    s["mcr"] = Hex8(sys.mcr);
    s["cs0_end"] = Hex32(sys.Cs0End());
    s["scrp"] = Hex8(sys.scrp);
    s["irq_priority"] = Hex8(sys.irqPriority);
    z["system"] = s;

    StateNode wd = StateNode::Object();
    wd["wdtmr"] = Hex8(sys.wdtmr);
    wd["wdtcr"] = Hex8(sys.wdtcr);
    wd["enabled"] = (sys.wdtmr & 0x80) != 0;
    wd["running"] = chip.WatchdogRunning();
    wd["period_clocks_log2"] = 16 + 2 * ((sys.wdtmr >> 5) & 0x03);
    wd["output"] = "not connected (/WDTOUT, research-cpu-z84c15.md Q3)";
    z["watchdog"] = wd;

    StateNode ctc = StateNode::Object();
    ctc["vector"] = Hex8(chip.ctc.Vector());
    StateNode channels = StateNode::Array();
    for (uint8_t c = 0; c < 4; c++)
    {
        const Z84Lib::Z84Ctc::ChannelState& ch = chip.ctc.GetChannel(c);
        StateNode n = StateNode::Object();
        n["channel"] = int(c);
        n["control"] = Hex8(ch.control);
        n["mode"] = (ch.control & 0x40) ? "counter" : "timer";
        n["interrupt"] = (ch.control & 0x80) != 0;
        n["time_constant"] = ch.timeConstant ? int(ch.timeConstant) : 256;
        n["running"] = ch.running != 0;
        n["ip"] = ch.ip != 0;
        n["ius"] = ch.ius != 0;
        channels.push(n);
    }
    ctc["channels"] = channels;
    z["ctc"] = ctc;

    StateNode sio = StateNode::Array();
    static const char* const kSioUse[2] = {"AT keyboard (set 2 scan codes)", "serial mouse (Microsoft, 1200 baud)"};
    for (uint8_t c = 0; c < 2; c++)
    {
        const Z84Lib::Z84Sio::Channel& ch = chip.sio.GetChannel(c);
        StateNode n = StateNode::Object();
        n["channel"] = c == 0 ? "A" : "B";
        n["use"] = kSioUse[c];
        n["wr1"] = Hex8(ch.wr[1]);
        n["wr3"] = Hex8(ch.wr[3]);
        n["wr4"] = Hex8(ch.wr[4]);
        n["wr5"] = Hex8(ch.wr[5]);
        n["rx_enabled"] = (ch.wr[3] & 0x01) != 0;
        n["fifo_count"] = int(ch.fifoCount);
        n["fifo"] = HexRow(ch.fifo, ch.fifoCount);
        n["overrun"] = ch.overrun != 0;
        sio.push(n);
    }
    z["sio"] = sio;

    SprinterInput& input = decoder.GetInput();
    StateNode kbd = StateNode::Object();
    kbd["int_enabled"] = input.KeyboardIntEnabled();
    kbd["bytes_on_the_way"] = input.KeyboardStream().Busy();
    kbd["overruns"] = input.KeyboardOverruns();
    kbd["sio_a_fifo"] = HexRow(chip.sio.GetChannel(0).fifo, chip.sio.GetChannel(0).fifoCount);
    z["keyboard"] = kbd;

    // The board mouse: its counters (both views read them) and the serial packet on SIO B
    const SprinterInput::BoardMouse board = input.GetBoardMouse();
    const MsSerialMouse::State& serial = input.SerialMouse().GetState();
    StateNode mouse = StateNode::Object();
    mouse["x"] = int(board.x);
    mouse["y"] = int(board.y);
    mouse["buttons"] = Hex8(board.buttons);
    mouse["packet_in_flight"] = serial.sent < 3;
    mouse["sio_b_fifo"] = HexRow(chip.sio.GetChannel(1).fifo, chip.sio.GetChannel(1).fifoCount);
    z["mouse"] = mouse;

    StateNode pio = StateNode::Array();
    static const char* const kPioModes[4] = {"output", "input", "bidirectional", "bit control"};
    for (uint8_t p = 0; p < 2; p++)
    {
        const Z84Lib::Z84Pio::Port& port = chip.pio.GetPort(p);
        StateNode n = StateNode::Object();
        n["port"] = p == 0 ? "A" : "B";
        n["mode"] = kPioModes[port.mode & 3];
        n["direction"] = Hex8(port.direction);
        n["output"] = Hex8(port.output);
        n["inputs"] = Hex8(port.inputs);
        n["ip"] = port.ip != 0;
        pio.push(n);
    }
    z["pio"] = pio;
    z["any_under_service"] = chip.AnyUnderService();

    // The wait generator as WCR / MWBR program it (z84waits.cpp; PS0182 p. 318-320)
    {
        static const uint8_t kPairWaits[4] = {0, 2, 4, 6};
        static const uint8_t kRetiWaits[4] = {0, 0, 2, 4};
        StateNode w = StateNode::Object();
        w["m1_extra"] = (sys.wcr >> 4) & 1;
        w["memory"] = (sys.wcr >> 2) & 3;
        w["memory_range"] = StringHelper::Format("#%X000-#%XFFF (MWBR)", sys.mwbr & 0x0F, sys.mwbr >> 4);
        w["io"] = kPairWaits[sys.wcr & 3];
        w["inta_daisy_chain"] = kPairWaits[sys.wcr >> 6];
        w["inta_vector"] = (sys.wcr >> 5) & 1;
        w["reti_extension"] = kRetiWaits[sys.wcr >> 6];
        w["note"] = "Z84C15 clocks, inside each cycle; the board's 21 MHz RAM waits are clock.waits";
        z["wait_generator"] = w;
    }

    // The interrupt daisy chain in priority order (the chip's priority register, Z84C15::Order)
    {
        static const uint8_t kPriority[6][3] = {{0, 1, 2}, {1, 0, 2}, {0, 2, 1}, {2, 1, 0}, {2, 0, 1}, {1, 2, 0}};
        static const char* const kDevice[3] = {"CTC", "SIO", "PIO"};
        uint8_t priority = static_cast<uint8_t>(sys.irqPriority & 0x07);
        if (priority > 5)
            priority &= 0x03;
        StateNode chain = StateNode::Array();
        std::string order;
        for (uint8_t device : kPriority[priority])
        {
            order += order.empty() ? kDevice[device] : std::string(" > ") + kDevice[device];
            const uint8_t sources = device == 0 ? 4 : 2;
            for (uint8_t i = 0; i < sources; i++)
            {
                StateNode n = StateNode::Object();
                bool ip = false;
                bool ius = false;
                std::string name;
                if (device == 0)
                {
                    ip = chip.ctc.GetChannel(i).ip != 0;
                    ius = chip.ctc.GetChannel(i).ius != 0;
                    name = StringHelper::Format("CTC %u", static_cast<unsigned>(i));
                }
                else if (device == 1)
                {
                    ip = chip.sio.RxIp(i);
                    ius = chip.sio.GetChannel(i).rxIus != 0;
                    name = i == 0 ? "SIO A receive" : "SIO B receive";
                }
                else
                {
                    ip = chip.pio.GetPort(i).ip != 0;
                    ius = chip.pio.GetPort(i).ius != 0;
                    name = i == 0 ? "PIO A" : "PIO B";
                }
                n["source"] = name;
                n["ip"] = ip;
                n["ius"] = ius;
                chain.push(n);
            }
        }
        StateNode d = StateNode::Object();
        d["priority_register"] = Hex8(sys.irqPriority);
        d["order"] = order;
        d["sources"] = chain;
        z["daisy_chain"] = d;
    }
    z["watchdog"]["deadline_clock"] = static_cast<uint64_t>(chip.WatchdogDeadline());
    return z;
}

/// CRC-32 (IEEE, zlib's) of a small buffer: a fingerprint of the accelerator's line buffer
uint32_t Crc32(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/// The block accelerator (s5-accelerator-outcome.md "For the automation branch"): the configured
/// module's (PortDecoder_Sprinter::GetAccelerator, null while the PLD loads)
StateNode AcceleratorSummary(PortDecoder_Sprinter& decoder, EmulatorContext* context)
{
    StateNode a = StateNode::Object();
    const SprinterAccelerator* accelerator = decoder.GetAccelerator();
    if (!accelerator)
    {
        a["available"] = false;
        a["description"] = "no accelerator while the PLD loads (the configured module brings it)";
        return a;
    }
    const SprinterAccelState& st = accelerator->State();
    a["available"] = true;
    a["enabled"] = accelerator->IsEnabled();
    a["mode"] = int(st.mode);
    a["mode_name"] = SprinterAccelerator::ModeName(st.mode);
    a["armed"] = (st.dir & SprinterAccelerator::kDirOn) != 0;
    a["dir"] = Hex8(st.dir);
    a["length"] = static_cast<uint64_t>(SprinterAccelerator::Accesses(st.length));
    a["length_register"] = Hex8(st.length);
    a["function"] = SprinterAccelerator::FunctionName(st.fn);
    a["blocked"] = st.blocked != 0;
    a["int_suspend"] = context->config.sprinter.accel_int_suspend != 0;
    a["alt"] = st.alt != 0;
    a["xcnt"] = int(st.xcnt);
    a["aagr"] = int(st.aagr);
    a["operations"] = static_cast<uint64_t>(st.operations);
    a["last_extra_clocks"] = static_cast<uint64_t>(st.lastExtraClocks);
    a["buffer_crc32"] = Hex32(Crc32(st.buffer, sizeof st.buffer));
    a["buffer_head"] = HexRow(st.buffer, 16);
    a["control"] = "LD B,B off, LD C,C fill, LD D,D length, LD E,E vertical fill, LD H,H double, LD L,L copy, "
                   "LD A,A vertical copy, HALT off; ALL_MODE bit 0 enables (sprinter-accelerator.md)";
    return a;
}

/// The 21 MHz wait rule (SprinterWaits, MAME do_mem_wait): which windows wait now
StateNode WaitsSummary(PortDecoder_Sprinter& decoder, EmulatorContext* context)
{
    StateNode w = StateNode::Object();
    const SprinterWaits* waits = decoder.GetWaits();
    const bool turbo = context->emulatorState.hw_turbo_ratio > 1;
    w["active"] = turbo && waits != nullptr;
    w["rule"] = "an access started at CPU clock t waits ((6 - t mod 6) mod 6) + 6 - taken clocks: taken 3 for "
                "memory, 4 for a port (MAME do_mem_wait); main RAM and ports only, ROM and fast RAM never";
    w["memory_taken"] = static_cast<uint64_t>(SprinterWaits::kMemoryTaken);
    w["port_taken"] = static_cast<uint64_t>(SprinterWaits::kPortTaken);
    StateNode windows = StateNode::Array();
    for (uint8_t slot = 0; slot < 4; slot++)
        windows.push(turbo && waits && waits->SlotWaits(slot));
    w["windows_waiting"] = windows;
    w["at_3_5_mhz"] = "none (the PLD's /MR_WAIT with an accelerator mode armed is not emulated: S5 open point 3)";
    return w;
}

/// The ZX mode's "original waits" (SprinterOrigWaits, PLD WAIT_ORIG; tdd-zx-mode.md §3.3): ALL_MODE bit 2 = 0 at 3.5 MHz
StateNode OrigWaitsSummary(PortDecoder_Sprinter& decoder)
{
    StateNode w = StateNode::Object();
    const SprinterPldState& pld = decoder.GetPldState();
    const bool active = decoder.OrigWaitsActive();
    w["active"] = active;
    w["all_mode_bit2"] = (pld.allMode & 0x04) != 0;
    w["rule"] = "ALL_MODE bit 2 = 0 at 3.5 MHz: a memory access to #4000-#7FFF, or to #C000-#FFFF while #7FFD bit 2 is set, "
                "waits while CT5 = 0 - 2 T when its T2 falls on the first low T of the 4-T CT5 period, 1 T on the second, "
                "none on the high half (PLD WAIT_ORIG; ORIGIN.ZX)";
    w["period_t"] = static_cast<uint64_t>(SprinterOrigWaits::kPeriod);
    w["phase_t"] = static_cast<uint64_t>(SprinterOrigWaits::kPhase);
    w["phase_note"] = "placeholder until measured on a real board (testdata/machines/sprinter/zx-timing, tdd-zx-mode Q1)";
    StateNode windows = StateNode::Array();
    for (uint8_t window = 0; window < 4; window++)
        windows.push(active && SprinterOrigWaits::WindowWaits(window, pld.pn));
    w["windows_waiting"] = windows;
    return w;
}

StateNode Bios(EmulatorContext* context)
{
    StateNode b = StateNode::Object();
    ROM* rom = context->pCore ? context->pCore->GetROM() : nullptr;
    const std::string selected = context->config.sprinter_rom_path;
    b["rom_file"] = selected;
    uint32_t loadedCrc = 0;
    if (rom && context->pMemory)
    {
        static const struct
        {
            uint8_t page;
            const char* key;
        } kPages[] = {{8, "page_8"}, {0, "page_0"}, {12, "page_12"}};
        StateNode pages = StateNode::Object();
        for (const auto& p : kPages)
        {
            const uint8_t* data = context->pMemory->ROMPageHostAddress(p.page);
            if (data)
                pages[p.key] = rom->GetROMTitle(rom->CalculateSignature(data, 0x4000));
        }
        b["identified"] = pages;
        // The flash as loaded (16 pages of 16 KB): which shipped image it is
        loadedCrc = SprinterBios::Crc32(context->pMemory->ROMBase(), 16 * 0x4000);
        b["loaded_crc32"] = StringHelper::Format("%08x", loadedCrc);
    }

    const std::string selectedName = std::filesystem::path(selected).filename().string();
    std::string loadedName;
    StateNode images = StateNode::Array();
    for (const SprinterBios::Image& known : SprinterBios::Known())
    {
        const std::string file = std::string("rom/sprinter/") + known.file;
        std::string path, error;
        StateNode n = StateNode::Object();
        n["file"] = file;
        n["alias"] = known.alias;
        n["version"] = known.version;
        n["crc32"] = StringHelper::Format("%08x", known.crc32);
        n["present"] = SprinterBios::Resolve(known.file, path, error);
        n["loaded"] = loadedCrc == known.crc32;
        n["selected"] = selectedName == known.file;
        n["active"] = loadedCrc == known.crc32;  // kept for older clients: the image that runs
        if (loadedCrc == known.crc32)
            loadedName = known.file;
        images.push(n);
    }
    b["images"] = images;
    b["loaded"] = loadedName.empty() ? std::string("not a shipped image (") + selectedName + ")" : loadedName;
    b["reload_pending"] = context->pEmulator && context->pEmulator->RomReloadPending();
    StateNode options = StateNode::Object();
    options["fast_start"] = context->config.sprinter.fast_start != 0;
    options["accel_int_suspend"] = context->config.sprinter.accel_int_suspend != 0;
    b["options"] = options;
    b["select"] = "POST /api/v1/emulator/{id}/sprinter/bios {bios: 3.04 | 3.06 | 3.07 | <file>, fast_start, "
                  "accel_int_suspend, reset} (CLI state sprinter bios <name>, Lua / Python sprinter_bios_select); at "
                  "create: {\"model\": \"SPRINTER\", \"sprinter\": {\"bios\": \"3.06\"}}; or [ROM] SPRINTER= in "
                  "configs/sprinter/unreal.ini (docs/inprogress/2026-09-28-sprinter/bios-versions.md)";
    return b;
}

/// Greedy cover of a set of 9-bit address points with cubes (value, mask of fixed bits) -
/// tools/machines/sprinter/dcp-table/dcp-table.py cubes(), restricted to the address bits
std::vector<std::pair<uint16_t, uint16_t>> Cubes(const std::set<uint16_t>& points)
{
    constexpr int kBits = 9;
    constexpr uint16_t kFull = (1u << kBits) - 1u;
    std::set<uint16_t> remaining = points;
    std::vector<std::pair<uint16_t, uint16_t>> out;

    auto forEach = [](uint16_t value, uint16_t mask, const auto& fn) {
        // Every point of the cube: the free bits run through all combinations
        uint16_t free = static_cast<uint16_t>(~mask & kFull);
        uint16_t sub = 0;
        while (true)
        {
            if (!fn(static_cast<uint16_t>((value & mask) | sub)))
                return false;
            if (sub == free)
                return true;
            sub = static_cast<uint16_t>((sub - free) & free);
        }
    };

    while (!remaining.empty())
    {
        const uint16_t seed = *remaining.begin();
        uint16_t mask = kFull;
        for (int bit = 0; bit < kBits; bit++)
        {
            const uint16_t trial = static_cast<uint16_t>(mask & ~(1u << bit));
            if (forEach(seed, trial, [&](uint16_t p) { return points.count(p) != 0; }))
                mask = trial;
        }
        forEach(seed, mask, [&](uint16_t p) {
            remaining.erase(p);
            return true;
        });
        out.emplace_back(static_cast<uint16_t>(seed & mask), mask);
    }
    return out;
}

/// "xxxx xxxx 000x x111": the address bits a cube fixes, A15 first (dcp-table.py describe())
std::string AddressPattern(uint16_t value, uint16_t mask)
{
    // index bit -> address bit
    static const int kAddressBit[9] = {0, 1, 2, 7, 13, 5, 6, 14, 15};
    char addr[16];
    std::fill(addr, addr + 16, 'x');
    for (int ib = 0; ib < 9; ib++)
    {
        if ((mask >> ib) & 1)
            addr[15 - kAddressBit[ib]] = ((value >> ib) & 1) ? '1' : '0';
    }
    std::string out;
    for (int i = 0; i < 16; i++)
    {
        if (i && i % 4 == 0)
            out += ' ';
        out += addr[i];
    }
    return out;
}

struct QueryState
{
    uint8_t map;
    bool dosOn;
    bool pn5;
};

QueryState Resolve(const SprinterPldState& pld, const DeviceState::SprinterPortQuery& query)
{
    QueryState s;
    s.map = query.map >= 0 ? static_cast<uint8_t>(query.map & 3) : static_cast<uint8_t>((pld.cnf >> 3) & 3);
    s.dosOn = query.dos >= 0 ? query.dos != 0 : pld.dos == 0;
    s.pn5 = query.pn5 >= 0 ? query.pn5 != 0 : (pld.pn & 0x20) != 0;
    return s;
}

void PutQuery(StateNode& n, const QueryState& s, const DeviceState::SprinterPortQuery& query)
{
    n["map"] = int(s.map);
    n["dos"] = s.dosOn;
    n["pn5"] = s.pn5;
    n["from_machine"] = query.map < 0 && query.dos < 0 && query.pn5 < 0;
}

/// The Z84C15's on-chip ports: the chip answers them itself, the port table never sees them
StateNode Z84Ports(const std::map<uint16_t, std::string>& names)
{
    StateNode arr = StateNode::Array();
    for (const auto& [code, name] : names)
    {
        if (code < PortDecoder_Sprinter::kTraceZ84Base)
            continue;
        StateNode n = StateNode::Object();
        n["port"] = StringHelper::Format("#xx%02X", code & 0xFFu);
        n["code"] = Hex16(code);
        n["name"] = name;
        arr.push(n);
    }
    return arr;
}

/// The sound devices (tdd-accel-sound-input §2): the PLD's AY, the beeper, the Covox / Covox-Blaster
StateNode SoundSummary(PortDecoder_Sprinter& decoder, EmulatorContext* context)
{
    StateNode snd = StateNode::Object();

    StateNode ay = StateNode::Object();
    ay["chip"] = "AY-3-8910 (in the PLD)";
    ay["clock_hz"] = 1750000;
    ay["clock_note"] = "42 MHz / 24 (MAME); the emulator's AY runs at 3.5 MHz / 2 on every model";
    const SoundChip_AY8910* chip = context->pSoundManager ? context->pSoundManager->getAYChip(0) : nullptr;
    const AYStereoMode stereo = chip ? chip->getStereoMode() : AYStereoMode::ABC;
    ay["stereo"] = stereo == AYStereoMode::ACB ? "ACB" : (stereo == AYStereoMode::Mono ? "mono" : "ABC");
    ay["chips"] = context->pSoundManager ? context->pSoundManager->getAYChipCount() : 0;
    ay["ports"] = "#FFFD select (code #90), #BFFD data (code #91), #FFFD read (code #52)";
    snd["ay"] = ay;
    snd["beeper"] = "#FE bit 4 (tape out bit 3), the same DAC";

    const CovoxBlaster& cbl = decoder.GetCovoxBlaster();
    const CovoxBlasterState& c = cbl.State();
    StateNode b = StateNode::Object();
    b["control"] = Hex8(c.control);
    const bool on = (c.control & CovoxBlaster::kControlCbl) != 0;
    b["mode"] = on ? "covox-blaster" : "covox";
    b["stereo"] = (c.control & CovoxBlaster::kControlStereo) != 0;
    b["bits"] = (c.control & CovoxBlaster::kControl16Bit) ? 16 : 8;
    b["int_enabled"] = (c.control & CovoxBlaster::kControlInt) != 0;
    b["rate"] = int(c.control & 0x0F);
    b["divider"] = int(CovoxBlaster::kDivider[c.control & 0x0F]);
    b["tick_tstates"] = static_cast<uint64_t>(CovoxBlaster::TickTstates(c.control));
    b["rate_hz"] = CovoxBlaster::RateHz(c.control);
    b["play_index"] = Hex8(c.cnt);
    b["write_index"] = Hex8(cbl.EffectiveWriteIndex());
    b["int_pending"] = c.intPending != 0;
    b["half_needs_data"] = on && (((c.cnt ^ cbl.EffectiveWriteIndex()) & 0x80) != 0);
    b["next_tick_tstate"] = static_cast<uint64_t>(c.nextTick);
    b["dac_left"] = Hex16(c.levelL);
    b["dac_right"] = Hex16(c.levelR);
    b["ticks"] = static_cast<uint64_t>(c.ticks);
    b["ring_writes"] = static_cast<uint64_t>(c.ringWrites);
    b["covox_writes"] = static_cast<uint64_t>(c.covoxWrites);
    b["int_requests"] = static_cast<uint64_t>(c.intRequests);
    b["ports"] = "data #FB / #4F (code #88), control #4E / #0046 (code #89), #FE bits 7 / 5; RAM page #FD (accelerator copies)";
    b["mixer"] = "COVOX slot, named Covox-Blaster (recording source COVOX)";
    snd["covox_blaster"] = b;
    return snd;
}
}  // namespace

namespace DeviceState
{

StateNode Sprinter(EmulatorContext* context)
{
    PortDecoder_Sprinter* decoder = SprinterDecoder(context);
    if (!decoder || !context->pMemory)
        return Unavailable("Not a Sprinter machine");

    const SprinterPldState& pld = decoder->GetPldState();
    const EmulatorState& state = context->emulatorState;
    StateNode ret = StateNode::Object();
    ret["available"] = true;

    // PLD configuration
    {
        StateNode p = StateNode::Object();
        p["state"] = ConfigStateName(pld.configState);
        const SprinterPldModuleDescriptor& module = decoder->ActiveModule().Descriptor();
        p["module_index"] = int(pld.configModule);
        p["module"] = module.name;
        StateNode modules = StateNode::Array();
        for (size_t i = 0; i < decoder->GetRegistry().Count(); i++)
            modules.push(decoder->GetRegistry().At(i).Descriptor().name);
        p["modules_known"] = modules;
        StateNode bitstream = StateNode::Object();
        bitstream["writes"] = static_cast<uint64_t>(pld.bitstreamCount);
        bitstream["writes_expected"] = static_cast<uint64_t>(SprinterPldConfig::kPldConfigurationWrites);
        bitstream["full_hash"] = Hex32(pld.bitstreamHashFull);
        bitstream["head_hash"] = Hex32(pld.bitstreamHashHead);
        bitstream["module_full_hash"] = Hex32(module.fullHash);
        bitstream["module_head_hash"] = Hex32(module.headHash);
        bitstream["fast_start"] = context->config.sprinter.fast_start != 0;
        p["bitstream"] = bitstream;
        if (pld.configState == SprinterConfigState::Loading)
            p["load_watchdog_frames"] = static_cast<uint64_t>(pld.loadWatchdog);
        p["dcp_open"] = pld.configState == SprinterConfigState::Configured && !pld.starting;
        p["dcp_opened_frame"] = decoder->DcpOpenedFrame();
        p["dcp_opened_pc"] = Hex16(decoder->DcpOpenedPc());
        ret["pld"] = p;
    }

    // The port decoder: which map and signals the table is read with now
    {
        StateNode d = StateNode::Object();
        d["map"] = int((pld.cnf >> 3) & 0x03);
        d["cnf"] = Hex8(pld.cnf);
        d["cnf_clean_rules"] = int(pld.cnf >> 5);
        d["dos"] = pld.dos == 0;
        d["pn5"] = (pld.pn & 0x20) != 0;
        d["port_7ffd"] = Hex8(pld.pn);
        d["port_1ffd"] = Hex8(pld.sc);
        d["port_table_page"] = Hex8(SprinterMemory::kPortTablePage);
        d["see"] = "GET /state/sprinter/ports (the table), /state/sprinter/ports/lookup?port= (one port)";
        ret["decoder"] = d;
    }

    // The four windows
    {
        StateNode windows = StateNode::Array();
        for (uint8_t w = 0; w < 4; w++)
            windows.push(Window(context, *decoder, w));
        ret["windows"] = windows;
    }

    // Registers and the cells #C0-#FF
    {
        StateNode r = StateNode::Object();
        r["rom_rg"] = Hex8(pld.romRg);
        r["sys_pg"] = int(pld.sysPg);
        r["rom_off"] = pld.romOff != 0;
        r["ram_sys"] = pld.ramSys != 0;
        r["arom16"] = pld.arom16 != 0;
        r["cache_on"] = pld.cacheOn != 0;
        StateNode allMode = StateNode::Object();
        allMode["value"] = Hex8(pld.allMode);
        allMode["zx_screen_shadow"] = (pld.allMode & 0x01) == 0;
        allMode["keyboard_int"] = (pld.allMode & 0x09) == 0x09;
        r["all_mode"] = allMode;
        r["port_y"] = Hex8(pld.portY);
        StateNode rgMod = StateNode::Object();
        rgMod["value"] = Hex8(pld.rgMod);
        rgMod["mode_page"] = int(pld.rgMod & 0x01);
        r["rgmod"] = rgMod;
        r["hold"] = Hex8(pld.hold);
        r["scale"] = Hex8(pld.Cell(SprinterCode::Scale));
        r["isa_addr_ext"] = Hex8(pld.isaAddrExt);
        r["cbl_control"] = Hex8(decoder->CblControl());
        ret["registers"] = r;

        StateNode cells = StateNode::Object();
        cells["C0-CF"] = HexRow(pld.cells, 16);
        cells["D0-DF"] = HexRow(pld.cells + 16, 16);
        cells["E0-EF"] = HexRow(pld.cells + 32, 16);
        cells["F0-FF"] = HexRow(pld.cells + 48, 16);
        cells["window_3_cell"] = StringHelper::Format("#%02X", 0xC0 + (pld.pg3 & 0x3F));
        ret["cells"] = cells;
    }

    // Clock and frame
    {
        StateNode c = StateNode::Object();
        c["turbo_requested"] = pld.turbo != 0;
        c["turbo_switch"] = pld.turboHard != 0;
        const unsigned ratio = state.hw_turbo_ratio ? state.hw_turbo_ratio : 1;
        c["ratio"] = ratio;
        c["mhz"] = ratio >= 6 ? "21" : "3.5";
        c["waits"] = WaitsSummary(*decoder, context);
        c["original_waits"] = OrigWaitsSummary(*decoder);
        ret["clock"] = c;

        // The tape input (KMPS, #FE bit 6) counts real time: base 3.5 MHz T-states whatever the CPU runs at
        StateNode t = StateNode::Object();
        const bool baseClock = context->pTape && context->pTape->IsBaseClockTimeBase();
        t["time_base"] = baseClock ? "base_clock" : "cpu_clock";
        t["note"] = "base_clock: a tape plays in real time, so at 21 MHz the ROM loader times its pulses six times too "
                    "long and fails, as on the board; load tapes in a 3.5 MHz mode (P128.ZX, ORIGIN.ZX)";
        ret["tape"] = t;

        auto* screen = dynamic_cast<ScreenSprinter*>(context->pScreen);
        StateNode f = StateNode::Object();
        f["lines_requested"] = pld.frameLines ? 312 : 320;
        f["lines"] = screen ? int(screen->FrameLines()) : (pld.frameLines ? 312 : 320);
        f["t_states"] = static_cast<uint64_t>(context->config.frame);
        f["note"] = "base T-states at 3.5 MHz: 320 x 224 = 71680 (code #2C), 312 x 224 = 69888 (code #2D)";
        ret["frame"] = f;
    }

    ret["video"] = VideoSummary(*decoder, context);
    ret["accelerator"] = AcceleratorSummary(*decoder, context);
    ret["sound"] = SoundSummary(*decoder, context);
    ret["z84c15"] = Z84Summary(*decoder, context);

    // Floppy: the WD1793 behind codes #10-#17
    {
        StateNode f = StateNode::Object();
        f["density_latch"] = pld.fdcHd ? "1.44 MB (code #17)" : "720 KB (code #16)";
        f["codes_off"] = pld.fdcOff != 0;
        if (WD1793* wd = context->pBetaDisk)
        {
            f["clock"] = wd->GetClock() == FdcClock::Clock2MHz ? "2 MHz" : "1 MHz";
            f["data_rate"] = wd->GetDataRate() == FdcDataRate::Rate500Kbps ? "500 kbit/s" : "250 kbit/s";
            f["clock_policy"] = wd->GetClockPolicy() == FdcClockPolicy::Latched ? "latched" : "other";
            f["drive"] = StringHelper::Format("%c", 'A' + (wd->getSelectedDriveIndex() & 3));
        }
        f["see"] = "GET /state/fdc (registers, drives), /media (slots fdd.a / fdd.b)";
        ret["fdc"] = f;
    }

    // CMOS: the DS12887A report is the rtc aspect
    {
        PortDecoder::RtcBinding binding = decoder->GetRtcBinding();
        StateNode c = StateNode::Object();
        c["chip"] = "DS12887A";
        c["ports"] = binding.ports;
        c["nvram_file"] = binding.nvramFile;
        c["see"] = "GET /state/rtc (CLI state rtc, Lua / Python rtc_state(), MCP inspect_state rtc)";
        ret["cmos"] = c;
    }

    // IDE (S3b): the shared IdeAdapter with the SPRINTER decode; drives and channels in GET /state/ide
    {
        const IdeAdapterState& ide = decoder->GetIdeAdapter().State();
        StateNode i = StateNode::Object();
        i["emulated"] = true;
        i["channel"] = ide.channel ? "secondary" : "primary";
        i["data_latch"] = Hex8(ide.readLatch);
        i["see"] = "GET /state/ide (CLI state ide, MCP inspect_state ide) for the drives on ide0 / ide1";
        ret["ide"] = i;
    }

    ret["bios"] = Bios(context);
    return ret;
}

StateNode SprinterPaging(EmulatorContext* context)
{
    PortDecoder_Sprinter* decoder = SprinterDecoder(context);
    if (!decoder || !context->pMemory)
        return Unavailable("Not a Sprinter machine");

    const SprinterPldState& pld = decoder->GetPldState();
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["pld_state"] = ConfigStateName(pld.configState);
    StateNode windows = StateNode::Array();
    for (uint8_t w = 0; w < 4; w++)
        windows.push(Window(context, *decoder, w));
    ret["windows"] = windows;
    ret["map"] = int((pld.cnf >> 3) & 0x03);
    ret["dos"] = pld.dos == 0;
    ret["turbo"] = context->emulatorState.hw_turbo_ratio > 1;
    ret["port_7ffd"] = Hex8(pld.pn);
    ret["port_1ffd"] = Hex8(pld.sc);
    ret["rom_rg"] = Hex8(pld.romRg);
    ret["cache_on"] = pld.cacheOn != 0;
    ret["cells_E8_EA"] = HexRow(pld.cells + 0x28, 3);
    ret["cells_F0_FF"] = HexRow(pld.cells + 0x30, 16);
    return ret;
}

StateNode SprinterText(EmulatorContext* context)
{
    PortDecoder_Sprinter* decoder = SprinterDecoder(context);
    if (!decoder)
        return Unavailable("Not a Sprinter machine");

    // The text squares of the picture (a = 0..39, b = 0..31) in the current mode page: a text
    // square's Mode1 byte is its character (tdd-video §3); a 640 square holds two (Line1, then
    // Line2 one row lower), a 320 square one, shown as the character and a space. Graphics,
    // border and blank squares read as spaces (SprinterSquare::TextCode: the video mapper's text
    // layer, /video/text and the screen OCR read the same cells)
    const SprinterVideoRam& vram = decoder->GetVideoRam();
    const uint8_t modePage = decoder->GetPldState().rgMod & 0x01;
    StateNode lines = StateNode::Array();
    int textSquares = 0;
    for (uint8_t b = 0; b < kPictureRows; b++)
    {
        std::string text;
        std::string codes;
        for (uint8_t a = 0; a < kPictureColumns; a++)
        {
            const uint8_t* line1 = vram.Data() + SprinterVideoRam::ModeAddress(a, b, modePage);
            for (unsigned half = 0; half < 2; half++)
            {
                uint8_t c = 0x20;
                SprinterSquare::TextCode(line1, half, c);
                text.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : (c == 0 ? ' ' : '.'));
                codes += StringHelper::Format("%02X", c);
            }
            textSquares += SprinterSquare::Decode(line1).IsText() ? 1 : 0;
        }
        // Trailing spaces carry nothing
        const size_t end = text.find_last_not_of(' ');
        StateNode line = StateNode::Object();
        line["row"] = int(b);
        line["text"] = end == std::string::npos ? std::string() : text.substr(0, end + 1);
        line["codes"] = codes;
        lines.push(line);
    }

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["mode_page"] = int(modePage);
    ret["columns"] = 80;
    ret["rows"] = int(kPictureRows);
    ret["text_squares"] = textSquares;
    // Spectrum mode (ALL_MODE bit 0 = 0: the Spectrum screen shadow on) draws the ZX screen with text
    // squares whose "font" is the screen bitmap: their codes are not text, the screen OCR reads that
    // picture as a ZX screen. Otherwise text is the picture when most squares are text (BIOS, DSS)
    const bool spectrumScreen = (decoder->GetPldState().allMode & 0x01) == 0;
    ret["spectrum_screen"] = spectrumScreen;
    ret["picture_is_text"] = !spectrumScreen && textSquares * 2 > kPictureColumns * kPictureRows;
    ret["lines"] = lines;
    return ret;
}

StateNode SprinterBios(EmulatorContext* context)
{
    PortDecoder_Sprinter* decoder = SprinterDecoder(context);
    if (!decoder || !context->pMemory)
        return Unavailable("Not a Sprinter machine");
    StateNode ret = Bios(context);
    ret["available"] = true;
    return ret;
}

StateNode SprinterBiosSelect(EmulatorContext* context, const SprinterBios::Options& options)
{
    PortDecoder_Sprinter* decoder = SprinterDecoder(context);
    if (!decoder || !context->pMemory)
        return Unavailable("Not a Sprinter machine");

    const std::string before = context->config.sprinter_rom_path;
    std::string error;
    if (!SprinterBios::ApplyToConfig(context->config, options, error))
    {
        StateNode n = StateNode::Object();
        n["available"] = false;
        n["description"] = error;
        return n;
    }
    // A new image is loaded by the next reset (Emulator::RequestRomReload: the flash is reread at
    // Reset, after TTD recording stopped); reset = true makes that reset now
    if (!options.bios.empty() && context->pEmulator)
        context->pEmulator->RequestRomReload();
    if (options.reset && context->pEmulator)
        context->pEmulator->Reset();

    StateNode ret = Bios(context);
    ret["available"] = true;
    ret["previous_rom_file"] = before;
    ret["reset_done"] = options.reset && context->pEmulator != nullptr;
    return ret;
}

bool SprinterVideoQueryFromStrings(const std::string& page, const std::string& all, const std::string& squares,
                                   SprinterVideoQuery& query, std::string& error)
{
    auto flag = [&](const std::string& text, const char* name, bool fallback, bool& out) {
        std::string v = text;
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (v.empty())
            out = fallback;
        else if (v == "1" || v == "on" || v == "true" || v == "yes")
            out = true;
        else if (v == "0" || v == "off" || v == "false" || v == "no")
            out = false;
        else
        {
            error = std::string(name) + " must be 0 / 1";
            return false;
        }
        return true;
    };
    query = SprinterVideoQuery();
    if (!page.empty() && page != "current")
    {
        if (page != "0" && page != "1")
        {
            error = "page must be 0 or 1 (the mode table page; omit for RGMOD's)";
            return false;
        }
        query.page = page[0] - '0';
    }
    return flag(all, "all", false, query.all) && flag(squares, "squares", true, query.squares);
}

namespace
{
/// The palettes a square's pixels take their pens from (bit k = palette k): graphics its own
/// (0-3), text the text palettes paper / ink and their flash phase (4-7), border and blank 4
uint8_t PalettesOf(const SprinterSquare& square)
{
    if (square.IsGraphics())
        return static_cast<uint8_t>(1u << square.Palette());
    if (square.IsText())
        return 0xF0;
    return 0x10;
}

const char* PaletteRole(unsigned k)
{
    static const char* const kRoles[8] = {"graphics 0", "graphics 1", "graphics 2", "graphics 3",
                                          "text paper", "text ink", "text paper, flash phase", "text ink, flash phase"};
    return kRoles[k & 7];
}
}  // namespace

StateNode SprinterVideo(EmulatorContext* context, const SprinterVideoQuery& query)
{
    PortDecoder_Sprinter* decoder = SprinterDecoder(context);
    if (!decoder)
        return Unavailable("Not a Sprinter machine");

    const SprinterPldState& pld = decoder->GetPldState();
    const SprinterVideoRam& vram = decoder->GetVideoRam();
    const uint8_t rgmodPage = pld.rgMod & 0x01;
    const uint8_t page = query.page >= 0 ? static_cast<uint8_t>(query.page & 1) : rgmodPage;
    const uint8_t columns = query.all ? SprinterIntSource::kSquareColumns : kPictureColumns;
    const uint8_t rows = query.all ? kSquareRowsAll : kPictureRows;
    auto* screen = dynamic_cast<ScreenSprinter*>(context->pScreen);

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["mode_page"] = int(page);
    ret["displayed"] = page == rgmodPage;
    ret["rgmod"] = Hex8(pld.rgMod);
    ret["text_page"] = int((pld.pn >> 3) & 1);
    StateNode hold = StateNode::Object();
    hold["value"] = Hex8(pld.hold);
    hold["x_pixels"] = (7 - static_cast<int>(pld.hold & 0x0F)) * 2;
    hold["y_lines"] = 7 - static_cast<int>(pld.hold >> 4);
    ret["hold"] = hold;
    StateNode frame = StateNode::Object();
    frame["lines_requested"] = pld.frameLines ? 312 : 320;
    frame["lines"] = screen ? int(screen->FrameLines()) : (pld.frameLines ? 312 : 320);
    frame["t_states"] = static_cast<uint64_t>(context->config.frame);
    ret["frame"] = frame;
    ret["port_y"] = Hex8(pld.portY);
    ret["border"] = int(pld.Cell(SprinterCode::Border) & 0x07);
    StateNode allMode = StateNode::Object();
    allMode["value"] = Hex8(pld.allMode);
    allMode["zx_screen_shadow"] = (pld.allMode & 0x01) == 0;
    ret["all_mode"] = allMode;
    ret["columns"] = int(columns);
    ret["rows"] = int(rows);
    ret["grid"] = query.all ? "the whole mode table: 56 x 40 squares (the beam's 896 pixels x 320 lines)"
                            : "the picture: 40 x 32 squares from (0, 0), 16 x 8 pixels each (all=1: the whole table)";
    ret["mode_table"] = "square (a, b): Mode0-Mode2 at video RAM row 1 + 2a + #80 x page, column #300 + 4b; Line2 "
                        "(the right character of an 80-column text square) one row lower";
    ret["legend"] = "G graphics 320 (256 colors), g graphics 640 (16 colors), T text 40, t text 80, B border, "
                    ". blank, * blank with the frame INT";

    constexpr int kKinds = static_cast<int>(SprinterSquare::Kind::Count);
    int counts[kKinds] = {};
    int lowres = 0;
    uint8_t palettes = 0;
    StateNode map = StateNode::Array();
    StateNode grid = StateNode::Array();
    for (uint8_t b = 0; b < rows; b++)
    {
        std::string line;
        StateNode row = StateNode::Array();
        for (uint8_t a = 0; a < columns; a++)
        {
            const uint32_t address = SprinterVideoRam::ModeAddress(a, b, page);
            const uint8_t* line1 = vram.Data() + address;
            const SprinterSquare square = SprinterSquare::Decode(line1);
            counts[static_cast<int>(square.kind)]++;
            lowres += square.LowRes() ? 1 : 0;
            palettes |= PalettesOf(square);
            line.push_back(square.Letter());
            if (!query.squares)
                continue;
            StateNode n = StateNode::Object();
            n["a"] = int(a);
            n["b"] = int(b);
            n["kind"] = SprinterSquare::Key(square.kind);
            n["m0"] = Hex8(square.m0);
            n["m1"] = Hex8(square.m1);
            n["m2"] = Hex8(square.m2);
            if (square.IsGraphics())
            {
                n["palette"] = int(square.Palette());
                n["source_column"] = Hex16(square.SourceColumn());
                n["source_row"] = int(square.SourceRow());
                n["low_res"] = square.LowRes();
                if (square.LowRes())
                    n["quarter"] = int(square.Quarter());
            }
            else if (square.IsText())
            {
                std::string chars;
                for (unsigned half = 0; half < 2; half++)
                {
                    uint8_t code = 0x20;
                    if (SprinterSquare::TextCode(line1, half, code))
                        chars += StringHelper::Format(chars.empty() ? "%02X" : " %02X", code);
                }
                n["chars"] = chars;
                if (square.kind == SprinterSquare::Kind::Text80)
                    n["line2_m0"] = Hex8(line1[SprinterVideoRam::kRowBytes]);
            }
            else if (square.IntArmed())
                n["int"] = true;
            row.push(n);
        }
        map.push(line);
        if (query.squares)
            grid.push(row);
    }

    StateNode summary = StateNode::Object();
    for (int k = 0; k < kKinds; k++)
        summary[SprinterSquare::Key(static_cast<SprinterSquare::Kind>(k))] = counts[k];
    summary["low_res"] = lowres;
    ret["counts"] = summary;
    StateNode used = StateNode::Array();
    for (unsigned k = 0; k < 8; k++)
        if (palettes & (1u << k))
            used.push(int(k));
    ret["palettes_used"] = used;
    ret["map"] = map;
    if (query.squares)
        ret["squares"] = grid;
    ret["see"] = "GET /state/sprinter/palette (the pens), /video/changes (mode / palette writes with their T), "
                 "/memory/region/vram (the bytes), /video/pixel?x=&y= (one pixel's sources)";
    return ret;
}

bool SprinterPaletteFromString(const std::string& text, int& palette, std::string& error)
{
    std::string v = text;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (v.empty() || v == "used")
        palette = kSprinterPalettesUsed;
    else if (v == "all")
        palette = kSprinterPalettesAll;
    else if (v.size() == 1 && v[0] >= '0' && v[0] <= '7')
        palette = v[0] - '0';
    else
    {
        error = "k must be 0-7, all or used (the default: the palettes the picture uses)";
        return false;
    }
    return true;
}

StateNode SprinterPalette(EmulatorContext* context, int palette)
{
    PortDecoder_Sprinter* decoder = SprinterDecoder(context);
    if (!decoder)
        return Unavailable("Not a Sprinter machine");

    const SprinterVideoRam& vram = decoder->GetVideoRam();
    const uint8_t page = decoder->GetPldState().rgMod & 0x01;
    uint8_t usedMask = 0;
    for (uint8_t b = 0; b < kPictureRows; b++)
        for (uint8_t a = 0; a < kPictureColumns; a++)
            usedMask |= PalettesOf(SprinterSquare::Decode(vram.Data() + SprinterVideoRam::ModeAddress(a, b, page)));
    uint8_t selected = usedMask;
    if (palette == kSprinterPalettesAll)
        selected = 0xFF;
    else if (palette >= 0)
        selected = static_cast<uint8_t>(1u << (palette & 7));

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["selection"] = palette == kSprinterPalettesAll ? "all" : (palette >= 0 ? "one" : "used by the picture");
    ret["layout"] = "pen k x 256 + n = the bytes R, G, B (this order) at video RAM row n, column #3E0 + 4k "
                    "(SprinterVideoRam::PenAddress); palettes 0-3 graphics, 4-7 text paper / ink / flash paper / "
                    "flash ink (border and blank: palette 4)";
    StateNode list = StateNode::Array();
    for (unsigned k = 0; k < 8; k++)
    {
        if (!(selected & (1u << k)))
            continue;
        StateNode p = StateNode::Object();
        p["k"] = int(k);
        p["role"] = PaletteRole(k);
        p["used_by_picture"] = (usedMask & (1u << k)) != 0;
        p["vram_column"] = StringHelper::Format("0x%03X", SprinterVideoRam::kPaletteColumn + 4 * k);
        StateNode pens = StateNode::Array();
        std::string compact;
        for (unsigned n = 0; n < 256; n++)
        {
            const uint32_t address = SprinterVideoRam::PenAddress(k * 256 + n);
            const std::string rgb =
                StringHelper::Format("%02X%02X%02X", vram.Read(address), vram.Read(address + 1), vram.Read(address + 2));
            StateNode pen = StateNode::Object();
            pen["n"] = int(n);
            pen["rgb"] = "#" + rgb;
            pen["vram"] = StringHelper::Format("0x%05X", address);
            pens.push(pen);
            compact += (n ? " " : "") + rgb;
        }
        p["pens"] = pens;
        p["rgb_row"] = compact;
        list.push(p);
    }
    ret["palettes"] = list;
    return ret;
}

StateNode SprinterSoundRing(EmulatorContext* context)
{
    PortDecoder_Sprinter* decoder = SprinterDecoder(context);
    if (!decoder)
        return Unavailable("Not a Sprinter machine");

    const CovoxBlaster& cbl = decoder->GetCovoxBlaster();
    const CovoxBlasterState& c = cbl.State();
    const uint8_t write = cbl.EffectiveWriteIndex();
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["control"] = Hex8(c.control);
    ret["mode"] = (c.control & CovoxBlaster::kControlCbl) ? "covox-blaster" : "covox";
    ret["stereo"] = (c.control & CovoxBlaster::kControlStereo) != 0;
    ret["play_index"] = Hex8(c.cnt);
    ret["write_index"] = Hex8(write);
    ret["playing_half"] = (c.cnt & 0x80) ? "upper (#80-#FF)" : "lower (#00-#7F)";
    ret["format"] = "256 unsigned 16-bit words, #8000 = silence; stereo: even entries left, odd right; rows of 16 "
                    "from the offset, [ ] = the entry playing, < > = the next write";
    StateNode rows = StateNode::Array();
    for (unsigned base = 0; base < 256; base += 16)
    {
        std::string line = StringHelper::Format("%02X:", base);
        for (unsigned i = base; i < base + 16; i++)
        {
            const char open = i == c.cnt ? '[' : (i == write ? '<' : ' ');
            const char close = i == c.cnt ? ']' : (i == write ? '>' : ' ');
            line += StringHelper::Format("%c%04X%c", open, c.ring[i], close);
        }
        rows.push(line);
    }
    ret["rows"] = rows;
    StateNode words = StateNode::Array();
    for (unsigned i = 0; i < 256; i++)
        words.push(int(c.ring[i]));
    ret["words"] = words;
    return ret;
}

bool SprinterPortFromString(const std::string& text, uint16_t& port)
{
    std::string t = text;
    if (!t.empty() && t[0] == '#')
        t = t.substr(1);
    else if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
        t = t.substr(2);
    if (t.empty() || t.size() > 4)
        return false;
    unsigned value = 0;
    for (char c : t)
    {
        const int digit = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                          : (c >= 'a' && c <= 'f')                    ? c - 'a' + 10
                          : (c >= 'A' && c <= 'F')                    ? c - 'A' + 10
                                                                      : -1;
        if (digit < 0)
            return false;
        value = value * 16 + static_cast<unsigned>(digit);
    }
    port = static_cast<uint16_t>(value);
    return true;
}

bool SprinterPortQueryFromStrings(const std::string& map, const std::string& dos, const std::string& pn5,
                                  const std::string& rw, SprinterPortQuery& query, std::string& error)
{
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    auto flag = [&](const std::string& text, const char* name, int& out) {
        const std::string v = lower(text);
        if (v.empty() || v == "current")
            out = -1;
        else if (v == "1" || v == "on" || v == "true" || v == "yes")
            out = 1;
        else if (v == "0" || v == "off" || v == "false" || v == "no")
            out = 0;
        else
        {
            error = std::string(name) + " must be 0 / 1 (or on / off; omit for the machine's current state)";
            return false;
        }
        return true;
    };

    query = SprinterPortQuery();
    const std::string m = lower(map);
    if (!m.empty() && m != "current")
    {
        if (m.size() != 1 || m[0] < '0' || m[0] > '3')
        {
            error = "map must be 0-3 (omit for the current CNF map)";
            return false;
        }
        query.map = m[0] - '0';
    }
    if (!flag(dos, "dos", query.dos) || !flag(pn5, "pn5", query.pn5))
        return false;
    const std::string d = lower(rw);
    if (d.empty() || d == "rw" || d == "both")
        query.direction = -1;
    else if (d == "r" || d == "read" || d == "in")
        query.direction = 1;
    else if (d == "w" || d == "write" || d == "out")
        query.direction = 0;
    else
    {
        error = "rw must be r, w or rw";
        return false;
    }
    return true;
}

StateNode SprinterPortTable(EmulatorContext* context, const SprinterPortQuery& query)
{
    PortDecoder_Sprinter* decoder = SprinterDecoder(context);
    if (!decoder || !context->pMemory)
        return Unavailable("Not a Sprinter machine");

    const SprinterPldState& pld = decoder->GetPldState();
    const QueryState s = Resolve(pld, query);
    const uint8_t* table = context->pMemory->RAMPageAddress(SprinterMemory::kPortTablePage);
    const std::map<uint16_t, std::string> names = CodeNames(*decoder);

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    PutQuery(ret, s, query);
    ret["direction"] = query.direction < 0 ? "rw" : (query.direction ? "r" : "w");
    ret["pld_state"] = ConfigStateName(pld.configState);
    if (pld.configState != SprinterConfigState::Configured || pld.starting)
        ret["note"] = "the BIOS has not opened the port decoder yet: page #40 may not hold its table";
    ret["source"] = StringHelper::Format("RAM page #40, offset #%04X", s.map * SprinterPortTable::kMapSize);

    StateNode rows = StateNode::Array();
    int unmapped = 0;
    for (int dir = 1; dir >= 0; dir--)
    {
        if (query.direction >= 0 && query.direction != dir)
            continue;
        std::map<uint8_t, std::set<uint16_t>> byCode;
        for (uint16_t bits = 0; bits < SprinterPortTable::kAddressCombinations; bits++)
        {
            const uint16_t index = SprinterPortTable::Index(s.map, s.pn5, !s.dosOn, dir != 0, SprinterPortTable::ExamplePort(bits));
            const uint8_t code = table[index];
            if (code)
                byCode[code].insert(bits);
            else
                unmapped++;
        }
        for (const auto& [code, points] : byCode)
        {
            for (const auto& [value, mask] : Cubes(points))
            {
                StateNode row = StateNode::Object();
                row["code"] = Hex8(code);
                row["name"] = CodeName(names, code);
                row["direction"] = dir ? "r" : "w";
                row["pattern"] = AddressPattern(value, mask);
                row["example"] = Hex16(SprinterPortTable::ExamplePort(value));
                int fixedBits = 0;
                for (uint16_t m = mask; m; m &= static_cast<uint16_t>(m - 1))
                    fixedBits++;
                row["addresses"] = 1 << (9 - fixedBits);
                rows.push(row);
            }
        }
    }
    ret["rows"] = rows;
    ret["unmapped_combinations"] = unmapped;
    ret["pattern_bits"] = "A15..A0; x = not decoded. Index bits: A15 A14 A6 A5 A13 A7 A2 A1 A0";
    ret["z84c15_ports"] = Z84Ports(names);
    return ret;
}

StateNode SprinterPortLookup(EmulatorContext* context, uint16_t port, const SprinterPortQuery& query)
{
    PortDecoder_Sprinter* decoder = SprinterDecoder(context);
    if (!decoder || !context->pMemory)
        return Unavailable("Not a Sprinter machine");

    const SprinterPldState& pld = decoder->GetPldState();
    const QueryState s = Resolve(pld, query);
    const uint8_t* table = context->pMemory->RAMPageAddress(SprinterMemory::kPortTablePage);
    const std::map<uint16_t, std::string> names = CodeNames(*decoder);
    const uint8_t low = static_cast<uint8_t>(port);

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["port"] = Hex16(port);
    PutQuery(ret, s, query);
    ret["pld_state"] = ConfigStateName(pld.configState);

    StateNode results = StateNode::Array();
    for (int dir = 1; dir >= 0; dir--)
    {
        if (query.direction >= 0 && query.direction != dir)
            continue;
        StateNode r = StateNode::Object();
        r["direction"] = dir ? "r" : "w";
        if (Z84Lib::Z84C15::Owns(low))
        {
            const uint16_t code = static_cast<uint16_t>(PortDecoder_Sprinter::kTraceZ84Base + low);
            r["answered_by"] = "Z84C15";
            r["code"] = Hex16(code);
            r["name"] = CodeName(names, code);
            r["note"] = dir ? "the chip answers; the PLD does not see the read"
                            : "the chip takes it; the PLD sees the write as well (its table code below)";
        }
        else
            r["answered_by"] = "PLD";
        if (!Z84Lib::Z84C15::Owns(low) || !dir)
        {
            const uint16_t index = SprinterPortTable::Index(s.map, s.pn5, !s.dosOn, dir != 0, port);
            const uint8_t code = table[index];
            r["index"] = Hex16(index);
            r["index_in_map"] = Hex16(index & (SprinterPortTable::kMapSize - 1));
            r["address_bits"] = Hex16(SprinterPortTable::AddressBits(port));
            r[Z84Lib::Z84C15::Owns(low) ? "pld_code" : "code"] = Hex8(code);
            r[Z84Lib::Z84C15::Owns(low) ? "pld_name" : "name"] = code ? CodeName(names, code) : std::string(dir ? "None (no device: reads #FF)" : "None (no device)");
        }
        // Decodes the PLD makes before the table (MAME dcp_r / dcp_w)
        if (dir && (port & 0x7F) == 0x7B)
            r["fixed_decode"] = "IN #FB / #7B: fast RAM in window 0 on / off (before the table)";
        if (!dir && (port & 0xBF) == 0x3C)
            r["fixed_decode"] = "OUT #3C / #7C: system ROM out of / into window 0 (before the table)";
        if (!dir && low == 0x5C)
            r["fixed_decode"] = "OUT #5C: ROM_RG while the system ROM is in (before the table)";
        // The #1F operand rewrite (hardware-reference §4.4): TR-DOS's IN A,(#1F) from RAM is a #0F access
        if (low == 0x1F)
            r["operand_rewrite"] = "IN A,(#1F) / OUT (#1F),A with the operand byte in RAM reach the bus as #xx0F "
                                   "(the WD1793 in TR-DOS): look up #0F for that case";
        results.push(r);
    }
    ret["results"] = results;
    return ret;
}

}  // namespace DeviceState
