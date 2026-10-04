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
#include "emulator/ports/models/sprinter/sprinterpldgame.h"
#include "emulator/ports/models/sprinter/sprinterporttable.h"
#include "emulator/ports/models/sprinter/sprinterzxports.h"
#include "emulator/machineeventjournal.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdportsearch.h"
#include "emulator/video/sprinter/sprintervideorenderer.h"
#include <cstdlib>
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
            note = "ISA view (#1FFD bit 4, pages #D0-#D6): cycles of an ISA slot (page bit 1 = slot, bit 2 = I/O), "
                   "address #9FBD bits 5-0 << 14 | A13-A0; an empty slot reads #FF (/state/isa)";
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

    // The 56 x 40 squares of the mode page (SprinterSquare: the classifier ScreenSprinter::DescribeScreenState
    // shares); the picture, the 40 x 32 squares from (0, 0), by SprinterPicture below
    constexpr int kKinds = static_cast<int>(SprinterSquare::Kind::Count);
    int all[kKinds] = {};
    int lowres = 0;
    int intArmed = 0;
    for (uint8_t b = 0; b < kSquareRowsAll; b++)
    {
        for (uint8_t a = 0; a < SprinterIntSource::kSquareColumns; a++)
        {
            const SprinterSquare square = SprinterSquare::Decode(vram.Data() + SprinterVideoRam::ModeAddress(a, b, modePage));
            all[static_cast<int>(square.kind)]++;
            lowres += square.LowRes() ? 1 : 0;
            intArmed += square.IntArmed() ? 1 : 0;
        }
    }

    StateNode v = StateNode::Object();
    v["mode_page"] = int(modePage);
    // Whose rules draw the picture: the square kinds below are Standard's classification
    v["renderer"] = decoder.ActiveModule().Descriptor().name;
    if (decoder.BeamVideo())
        v["renderer_note"] = "Game configuration: every square is graphics 320 with the grid offset (pld.game); the "
                             "kinds below classify the mode bytes by the Standard rules";
    // The picture's mode by the summary the GUI status bar shows (SprinterPicture: border / blank squares
    // frame a picture, a Spectrum screen is its ZX-40 squares)
    const SprinterPicture shown = SprinterPicture::Of(vram.Data(), modePage);
    v["picture_mode"] = SprinterSquare::Name(shown.mode);
    v["picture_mode_key"] = SprinterSquare::Key(shown.mode);
    v["picture_mode_squares"] = StringHelper::Format("%d of 1280", shown.Count(shown.mode));
    v["picture_mixed"] = shown.mixed;
    v["picture_brief"] = shown.Brief(static_cast<uint8_t>((pld.pn >> 3) & 1));
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

    // The CTC: programming, the board's CLK/TRG wiring and the live count (Z84Ctc's lazy view, no side effects)
    const Z84Lib::Z84Ctc& c84 = chip.ctc;
    StateNode ctc = StateNode::Object();
    ctc["vector"] = Hex8(c84.Vector());
    ctc["time_base_hz"] = c84.UnitsPerSecond();
    ctc["cpu_clock_hz"] = c84.SystemClockNum() ? static_cast<double>(c84.UnitsPerSecond()) * c84.SystemClockDen() / c84.SystemClockNum() : 0.0;
    StateNode channels = StateNode::Array();
    static const char* const kZcUse[4] = {"SIO B receive / transmit clock (serial mouse)", "not connected", "TRG3",
                                          "no ZC/TO pin"};
    for (uint8_t c = 0; c < 4; c++)
    {
        const Z84Lib::Z84Ctc::ChannelState& ch = c84.GetChannel(c);
        const Z84Lib::Z84Ctc::Trigger& trg = c84.GetTrigger(c);
        StateNode n = StateNode::Object();
        n["channel"] = int(c);
        n["control"] = Hex8(ch.control);
        n["mode"] = (ch.control & 0x40) ? "counter" : "timer";
        n["interrupt"] = (ch.control & 0x80) != 0;
        n["prescaler"] = (ch.control & 0x20) ? 256 : 16;
        n["edge"] = (ch.control & 0x10) ? "rising" : "falling";
        n["timer_start"] = (ch.control & 0x08) ? "trigger edge" : "time constant";
        n["time_constant"] = ch.timeConstant ? int(ch.timeConstant) : 256;
        n["running"] = ch.running != 0 && !ch.waitingTrigger;
        n["waiting_trigger"] = ch.running != 0 && ch.waitingTrigger != 0;
        const uint8_t count = c84.Count(c);
        n["count"] = count ? int(count) : 256;
        n["zero_counts"] = c84.ZeroCounts(c);
        StateNode input = StateNode::Object();
        switch (trg.kind)
        {
            case Z84Lib::Z84Ctc::TriggerKind::Clock:
                input["kind"] = "clock";
                input["hz"] = trg.hz;
                break;
            case Z84Lib::Z84Ctc::TriggerKind::Cascade:
                input["kind"] = "cascade";
                input["source"] = StringHelper::Format("ZC/TO%u", static_cast<unsigned>(trg.source));
                break;
            default:
                input["kind"] = "none";
                break;
        }
        n["trigger_input"] = input;
        n["zc_to_hz"] = c84.OutputHz(c);
        n["zc_to_drives"] = kZcUse[c];
        n["ip"] = ch.ip != 0;
        n["ius"] = ch.ius != 0;
        channels.push(n);
    }
    ctc["channels"] = channels;
    z["ctc"] = ctc;

    StateNode sio = StateNode::Array();
    static const char* const kSioUse[2] = {"AT keyboard (set 2 scan codes)", "serial mouse (Microsoft, 1200 baud; clock: CTC ZC/TO0)"};
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
        n["overrun"] = chip.sio.OverrunLatched(c);  // RR1 bit 5
        n["overrun_in_fifo"] = (ch.overrun & 0x0E) != 0;  // a written-over character still waits to be read
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
    mouse["mouse_baud"] = int(MsSerialMouse::kBaud);
    mouse["sio_b_baud"] = input.MouseReceiverBaud();  // CTC ZC/TO0 / the WR4 clock mode
    mouse["sio_b_in_tune"] = input.MouseReceiverInTune();
    mouse["framing_errors"] = input.MouseFramingErrors();
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
    w["ct5_rise_t"] = static_cast<uint64_t>(SprinterOrigWaits::kCt5RiseT);
    StateNode byT = StateNode::Array();
    for (uint8_t waits : SprinterOrigWaits::kWaitsFromRise)
        byT.push(static_cast<uint64_t>(waits));
    w["waits_by_t1_from_int"] = byT;
    w["phase_note"] = "derived from the PLD: INT and the 4-T CT5 wave share the CT5 rise (frame T mod 4 = ct5_rise_t); "
                      "waits by an access's T1 from INT mod 4 (tdd-zx-mode Q1; a board measurement would confirm it)";
    StateNode windows = StateNode::Array();
    for (uint8_t window = 0; window < 4; window++)
        windows.push(active && SprinterOrigWaits::WindowWaits(window, pld.pn));
    w["windows_waiting"] = windows;
    return w;
}

/// The PLD configuration module that runs and why (the machine report, the BIOS report, the ZX-mode report)
StateNode PldModule(PortDecoder_Sprinter& decoder)
{
    const SprinterPldState& pld = decoder.GetPldState();
    const SprinterPldModuleDescriptor& module = decoder.ActiveModule().Descriptor();
    StateNode m = StateNode::Object();
    m["module"] = module.name;
    m["module_index"] = int(pld.configModule);
    std::string key, why;
    decoder.ModuleSelection(key, why);
    m["selected_by"] = key;
    m["why"] = why;
    m["full_hash"] = Hex32(pld.bitstreamHashFull);
    m["head_hash"] = Hex32(pld.bitstreamHashHead);
    m["cell_EE"] = Hex8(pld.Cell(0xEE));
    m["cell_EE_note"] = "RET_PORT: the BIOS reads and clears it after every load and reset; non-zero = return to the "
                        "program whose windows and address are in that page at #FFF0-#FFF5 (the Game bitstream sets #41)";
    return m;
}

/// The Game configuration's picture state (its grid-offset register, sprintergamevideo.h); null node otherwise
StateNode GameVideo(PortDecoder_Sprinter& decoder)
{
    StateNode g = StateNode::Object();
    const auto* game = dynamic_cast<const SprinterPldGame*>(&decoder.ActiveModule());
    g["active"] = game != nullptr && decoder.BeamVideo() != nullptr;
    if (!game)
        return g;
    const SprinterGameVideoState& v = game->Video().State();
    g["grid_offset"] = Hex8(v.offset);
    g["grid_offset_x"] = int(v.offset & 0x0F);
    g["grid_offset_y"] = int(v.offset >> 4);
    g["frame_start_offset"] = Hex8(v.frameOffset);
    g["beam_t"] = static_cast<uint64_t>(v.beamT);
    g["picture"] = "every square graphics 320 x 256 colors from any byte corner of the 1024 x 256 virtual screen "
                   "(Mode0 bits 1-0 + Mode1 = column, Mode2 = row, bits 7-6 = palette); Mode0 bit 2: Mode3 becomes the "
                   "grid offset after the square (X bits 3-0 x 2 pixels, Y bits 7-4 lines); no text squares";
    return g;
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
        StateNode issues = StateNode::Array();
        for (const std::string& issue : SprinterBios::KnownIssues(known.crc32))
            issues.push(issue);
        n["known_issues"] = issues;
        if (loadedCrc == known.crc32)
            loadedName = known.file;
        images.push(n);
    }
    b["images"] = images;
    b["loaded"] = loadedName.empty() ? std::string("not a shipped image (") + selectedName + ")" : loadedName;
    // What a user should know about the image that runs (bios-versions.md §5.2); empty for the others
    StateNode loadedIssues = StateNode::Array();
    for (const std::string& issue : SprinterBios::KnownIssues(loadedCrc))
        loadedIssues.push(issue);
    b["known_issues"] = loadedIssues;
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
        {
            std::string key, why;
            decoder->ModuleSelection(key, why);
            p["selected_by"] = key;
            p["why"] = why;
        }
        p["cell_EE"] = Hex8(pld.Cell(0xEE));
        p["game"] = GameVideo(*decoder);
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
    // The ISA slots (Sprinter ISA tdd §10): the same report as /state/isa
    {
        StateNode isa = Isa(context);
        if (const StateNode* available = isa.find("available"); available && available->b)
            isa.members.erase(isa.members.begin());
        ret["isa"] = isa;
    }
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
    // The ZX (Spectrum) mode: the launcher configuration in effect (SprinterZxMode, also /state/sprinter/zx-mode);
    // the whole-RAM search for the launcher's option table is left to that view
    ret["zx_mode"] = SprinterZxMode(context, false);
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
    // The Spectrum mode draws the ZX screen with ZX-40 squares whose "font" is the screen bitmap
    // (SprinterSquare::Kind::Spectrum: not text, TextCode skips them): the screen OCR reads that picture as a
    // ZX screen. The squares decide, as the renderer does - not ALL_MODE bit 0, which only turns on the
    // Spectrum screen shadow. Otherwise text is the picture when most squares are text (BIOS, DSS)
    const bool spectrumScreen = SprinterPicture::Of(vram.Data(), modePage).mode == SprinterSquare::Kind::Spectrum;
    ret["spectrum_screen"] = spectrumScreen;
    ret["picture_is_text"] = !spectrumScreen && textSquares * 2 > kPictureColumns * kPictureRows;
    ret["lines"] = lines;
    return ret;
}

std::vector<std::string> SprinterBiosKnownIssues(EmulatorContext* context)
{
    if (!SprinterDecoder(context) || !context->pMemory || !context->pMemory->ROMBase())
        return {};
    return SprinterBios::KnownIssues(SprinterBios::Crc32(context->pMemory->ROMBase(), 16 * 0x4000));
}

StateNode SprinterBios(EmulatorContext* context)
{
    PortDecoder_Sprinter* decoder = SprinterDecoder(context);
    if (!decoder || !context->pMemory)
        return Unavailable("Not a Sprinter machine");
    StateNode ret = Bios(context);
    ret["available"] = true;
    ret["pld"] = PldModule(*decoder);  // the configuration the BIOS runs on: Game, after a program reloaded it
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
    if (square.UsesTextPalettes())
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
    ret["legend"] = "G graphics 320 (256 colors), g graphics 640 (16 colors), T text 40, t text 80, Z Spectrum "
                    "screen cell (ZX-40), B border, . blank, * blank with the frame INT";

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
            else if (square.kind == SprinterSquare::Kind::Spectrum)
            {
                n["zx_row"] = int(square.ZxRow());
                n["zx_column"] = int(square.ZxColumn());
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
    // What the picture shows (SprinterPicture, the GUI status bar's summary): the dominant content kind
    const SprinterPicture shown = SprinterPicture::Of(vram.Data(), page);
    ret["picture_mode"] = SprinterSquare::Key(shown.mode);
    ret["picture_mixed"] = shown.mixed;
    ret["picture_brief"] = shown.Brief(static_cast<uint8_t>((pld.pn >> 3) & 1));
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

// ---------------------------------------------------------------------------------------------------------------------
// The ZX mode report and the PLD journal (tdd-zx-mode.md §12)
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
/// The `.ZX` options the hardware shows
struct ZxOptions
{
    bool turbo = false;
    bool sprinter = false;
    bool p7ffd = false;
    bool p1ffd = false;
    bool mem512 = false;
    bool lines312 = false;
    bool origin = false;
};

/// A known launcher mode file
struct KnownZxMode
{
    const char* file;
    const char* name;
    const char* launcher;  ///< "community" (SPECTRUM.EXE v2.03, BIOS 3.06+) or "peters-plus" (2002)
    ZxOptions options;
    const char* romSet;    ///< "sprinter-community", "sprinter-pp", "original", "scorpion"
    const char* optionLine;
};

/// The mode files on the MAME-pack disk (C:\ZX, launcher v2.03) and on the DSS 1.62 disk (\ZX, Peters Plus).
/// /sc-int in SC256.ZX / SCORPION.ZX is not an option either parser knows (it reads `int-sc`): no INT change
const KnownZxMode kKnownModes[] = {
    {"SP.ZX", "Sprinter ZX", "community", {true, true, true, true, false, false, false}, "sprinter-community",
     "/sprinter /turbo /7FFD /1FFD /ret-fn"},
    {"P128.ZX", "Pentagon 128", "community", {false, false, true, false, false, false, false}, "sprinter-community",
     "/7FFD /ret-fn"},
    {"P512.ZX", "Pentagon 512", "community", {true, false, true, false, true, false, false}, "sprinter-community",
     "/turbo /7FFD /mem512 /ret-fn"},
    {"SC256.ZX", "Scorpion 256", "community", {true, false, true, true, false, true, false}, "scorpion",
     "/turbo /7FFD /1FFD /sc-int /lines312 /ret-fn"},
    {"ORIGIN.ZX", "Original ZX Spectrum", "community", {false, false, true, false, false, true, true}, "original",
     "/7FFD /origin /lines312 /ret-fn"},
    {"SPRINTER.ZX", "Sprinter ZX", "peters-plus", {true, true, true, true, false, false, false}, "sprinter-pp",
     "/turbo /sprinter /7FFD /1FFD /ret-fn"},
    {"PENT128.ZX", "Pentagon 128", "peters-plus", {true, false, true, false, false, false, false}, "sprinter-pp",
     "/turbo /7FFD /ret-fn"},
    {"PENT512.ZX", "Pentagon 512", "peters-plus", {true, false, true, true, true, false, false}, "sprinter-pp",
     "/turbo /7FFD /1FFD /mem512 /ret-fn"},
    {"SCORPION.ZX", "Scorpion 256", "peters-plus", {true, false, true, true, false, true, false}, "scorpion",
     "/turbo /7FFD /1FFD /lines312 /sc-int /ret-fn"},
    {"ORIGINAL.ZX", "ZX Spectrum", "peters-plus", {true, false, true, false, false, true, true}, "original",
     "/turbo /7FFD /lines312 /origin /ret-fn"},
};

/// The CNF/SYS byte the launcher computes (spectrum.asm PARAMS: the "set" / "not set" bytes added up; the
/// Peters Plus launcher leaves /7FFD out of it and writes #7FFD itself)
uint8_t ExpectedCnf(const ZxOptions& o, bool community)
{
    return static_cast<uint8_t>((o.turbo ? 0x03 : 0x02) + (o.sprinter ? 0x04 : 0x0C) + (community && !o.p7ffd ? 0x30 : 0x00) +
                                (o.p1ffd ? 0x00 : 0x40) + (o.mem512 ? 0x80 : 0x00));
}

/// A vROM image by its CRC-32
struct KnownRom
{
    uint32_t crc;
    const char* name;
    const char* set;
};
const KnownRom kKnownRoms[] = {
    {0xE509CC39, "SP_128 (Sprinter BASIC 128, menu \"Sprinter\"; also BIOS 3.06 / 3.07 flash page 2)", "sprinter-community"},
    {0x0229DDE9, "SP__48 (BASIC 48; also BIOS 3.06 / 3.07 flash page 3)", "sprinter-community"},
    {0xD9B613C5, "SP_TRD (Sprinter TR-DOS 7.03; also BIOS 3.06 HF2 flash page 4)", "sprinter-community"},
    {0x51C21367, "SP_TRD (Sprinter TR-DOS 7.03, BIOS 3.07 beta 1 flash page 4)", "sprinter-community"},
    {0x02BB12AC, "SP_128.BIN (Peters Plus 2002)", "sprinter-pp"},
    {0x8F4EDB0F, "SP__48.BIN (Peters Plus 2002)", "sprinter-pp"},
    {0x47F39C0D, "SP_TRD.BIN (Sprinter TR-DOS 7.01, Peters Plus 2002)", "sprinter-pp"},
    {0xEA8E9F2F, "SP_EXP.BIN (Peters Plus 2002)", "sprinter-pp"},
    {0xBB208860, "SP_EXP2.BIN (Peters Plus 2002)", "sprinter-pp"},
    {0x124AD9E0, "BASIC128 (Sinclair 128 ROM 0)", "original"},
    {0xB96A36BE, "BASIC_48 (Sinclair 48 ROM)", "original"},
    {0xFCBF11E8, "TRD_4EM (TR-DOS 5.04Em)", "original"},
    {0x10751ABA, "TRD503 (TR-DOS 5.03)", "original"},
    {0x2334B8C6, "TRD504TM (TR-DOS 5.04T)", "original"},
    {0x0EB40A09, "SC_128 (Scorpion ZS 256 BASIC 128)", "scorpion"},
    {0x64D46229, "SC__48 (Scorpion BASIC 48)", "scorpion"},
    {0xC5CA0423, "SC_TRD (Scorpion TR-DOS)", "scorpion"},
    {0x60D40D28, "SC_EXP (Scorpion service ROM)", "scorpion"},
};

const KnownRom* FindRom(uint32_t crc)
{
    for (const KnownRom& r : kKnownRoms)
        if (r.crc == crc)
            return &r;
    return nullptr;
}

const char* RomSetName(const std::string& set)
{
    if (set == "sprinter-community")
        return "Sprinter ROMs, community build (SP_128 / SP__48 / SP_TRD 7.03)";
    if (set == "sprinter-pp")
        return "Sprinter ROMs, Peters Plus 2002 (SP_128 / SP__48 / SP_TRD 7.01)";
    if (set == "original")
        return "original Sinclair ROMs + TR-DOS 5.0x";
    if (set == "scorpion")
        return "Scorpion ROMs";
    return "unknown";
}

/// The vROM cells (SP2000.inc ZX ROM cells): #E0 EXP, #E1 TR-DOS, #E2 BASIC 128, #E3 BASIC 48, +4 for the second set
const char* VromRole(uint8_t cell)
{
    switch (cell)
    {
        case 0xE0: case 0xE4: return "expansion (#1FFD bit 1)";
        case 0xE1: case 0xE5: return "TR-DOS";
        case 0xE2: case 0xE6: return "BASIC 128";
        case 0xE3: case 0xE7: return "BASIC 48";
        case 0xEB: return "ZX BIOS 1";
        case 0xEF: return "ZX BIOS 2";
        default: return "vROM";
    }
}

/// The cell window 0 shows in the ZX mode (SprinterMemory::StandardUpdateBanks, the vROM branch)
uint8_t Window0Cell(const SprinterPldState& pld)
{
    const bool sc0 = (pld.sc & 0x01) != 0;
    const bool ramSys = pld.ramSys != 0;
    const bool scLc = !(sc0 && ramSys);
    const uint8_t spr = (pld.sc & 0x02) ? 0 : static_cast<uint8_t>((pld.dos << 1) | (((pld.pn & 0x10) || !pld.dos) ? 1 : 0));
    return static_cast<uint8_t>(0xC0 + (0x20 | ((sc0 || !ramSys) ? 0x08 : 0) | ((pld.arom16 && !(sc0 && ramSys)) ? 0x04 : 0) |
                                        ((((spr & 0x02) && scLc) || !ramSys) ? 0x02 : 0) |
                                        ((((spr & 0x01) && scLc) || !ramSys) ? 0x01 : 0)));
}

/// NUL-terminated lines at the start of a page (the launcher's copy of the .ZX text)
std::vector<std::string> NulLines(const uint8_t* page, size_t maxLines)
{
    std::vector<std::string> lines;
    size_t at = 0;
    while (lines.size() < maxLines && at < 2048)
    {
        std::string line;
        while (at < 2048 && page[at] != 0)
        {
            uint8_t c = page[at++];
            if (c == '\r' || c == '\n')
                continue;
            if (c == '\t')
                c = ' ';
            if (c < 0x20 || c > 0x7E)
                return {};  // not text
            line.push_back(static_cast<char>(c));
            if (line.size() > 128)
                return {};
        }
        at++;
        // trim
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

/// The option line among a .ZX file's lines: the first one that starts with '/'
std::string OptionLine(const std::vector<std::string>& lines)
{
    for (const std::string& l : lines)
    {
        size_t i = 0;
        while (i < l.size() && l[i] == ' ')
            i++;
        if (i < l.size() && l[i] == '/')
            return l.substr(i);
    }
    return std::string();
}

bool LineHasOption(const std::string& line, const std::string& option)
{
    const std::string needle = "/" + option;
    for (size_t at = line.find(needle); at != std::string::npos; at = line.find(needle, at + 1))
    {
        const size_t end = at + needle.size();
        if (end == line.size() || line[end] == ' ' || line[end] == '\t')
            return true;
    }
    return false;
}

/// The launcher's option table (spectrum.asm PARAMS: DW name, DB current, DB "set"; set when the two bytes are
/// equal). Found by the names' block ("turbo",#FF,0,"lines312",#FF,0,"sprinter",#FF,0 - the same in both
/// launchers) and the words that point at each name
struct LauncherParams
{
    bool found = false;
    uint8_t page = 0;
    uint16_t base = 0;  ///< the CPU address of the page's start in the launcher's map
    std::vector<std::pair<std::string, bool>> flags;
};

const char* const kParamNames[] = {"turbo", "lines312", "sprinter", "7FFD", "1FFD", "mem512", "int-sc", "to-trdos",
                                   "no-run", "origin", "ret-zx", "ret-fn", "load-pal", "RMD-Keep"};

bool ParseParams(const uint8_t* page, uint8_t pageNumber, LauncherParams& out)
{
    // Bytes, not chars: #FF must compare as 255 with the page's bytes
    auto bytes = [](const std::string& text) {
        std::vector<uint8_t> out;
        for (char c : text)
            out.push_back(c == '|' ? 0xFF : (c == '~' ? 0x00 : static_cast<uint8_t>(c)));
        return out;
    };
    const std::vector<uint8_t> signature = bytes("turbo|~lines312|~sprinter|");
    const uint8_t* end = page + PAGE_SIZE;
    const uint8_t* hit = std::search(page, end, signature.begin(), signature.end());
    if (hit == end)
        return false;

    // Each name's offset (searched from the block on), then the word that points at it
    std::map<uint16_t, int> baseVotes;
    std::vector<std::pair<std::string, size_t>> nameAt;
    for (const char* name : kParamNames)
    {
        const std::vector<uint8_t> pattern = bytes(std::string(name) + "|~");
        const uint8_t* n = std::search(hit, end, pattern.begin(), pattern.end());
        if (n == end)
            continue;
        nameAt.emplace_back(name, static_cast<size_t>(n - page));
    }
    std::vector<std::pair<std::string, size_t>> entryAt;  // name -> offset of its table entry
    for (const auto& [name, offset] : nameAt)
    {
        for (size_t i = 0; i + 3 < PAGE_SIZE; i++)
        {
            const uint16_t w = static_cast<uint16_t>(page[i] | page[i + 1] << 8);
            if ((w & 0x3FFF) != offset)
                continue;
            const uint16_t base = static_cast<uint16_t>(w - offset);
            baseVotes[base]++;
            entryAt.emplace_back(name, i);
        }
    }
    if (baseVotes.empty())
        return false;
    uint16_t base = 0;
    int best = 0;
    for (const auto& [b, votes] : baseVotes)
        if (votes > best)
        {
            best = votes;
            base = b;
        }
    if (best < 4)
        return false;
    out.found = true;
    out.page = pageNumber;
    out.base = base;
    for (const auto& [name, offset] : nameAt)
    {
        for (const auto& [ename, at] : entryAt)
        {
            if (ename != name)
                continue;
            const uint16_t w = static_cast<uint16_t>(page[at] | page[at + 1] << 8);
            if (static_cast<uint16_t>(w - offset) != base)
                continue;
            out.flags.emplace_back(name, page[at + 2] == page[at + 3]);
            break;
        }
    }
    return true;
}

bool ParamFlag(const LauncherParams& p, const std::string& name, bool& value)
{
    for (const auto& [n, v] : p.flags)
        if (n == name)
        {
            value = v;
            return true;
        }
    return false;
}

std::string OptionText(const ZxOptions& o)
{
    std::string s;
    auto add = [&](bool on, const char* text) {
        if (!on)
            return;
        if (!s.empty())
            s += ' ';
        s += text;
    };
    add(o.sprinter, "/sprinter");
    add(o.turbo, "/turbo");
    add(o.p7ffd, "/7FFD");
    add(o.p1ffd, "/1FFD");
    add(o.mem512, "/mem512");
    add(o.origin, "/origin");
    add(o.lines312, "/lines312");
    return s;
}

std::vector<std::string> OptionDifferences(const ZxOptions& want, const ZxOptions& have)
{
    std::vector<std::string> d;
    auto cmp = [&](bool w, bool h, const char* name) {
        if (w != h)
            d.push_back(StringHelper::Format("%s %s here, %s in the file", name, h ? "on" : "off", w ? "on" : "off"));
    };
    cmp(want.turbo, have.turbo, "/turbo");
    cmp(want.sprinter, have.sprinter, "/sprinter");
    cmp(want.p7ffd, have.p7ffd, "/7FFD");
    cmp(want.p1ffd, have.p1ffd, "/1FFD");
    cmp(want.mem512, have.mem512, "/mem512");
    cmp(want.lines312, have.lines312, "/lines312");
    cmp(want.origin, have.origin, "/origin");
    return d;
}

/// What a write to a configuration code does now (the CNF clean rules applied)
std::string WriteEffect(uint8_t code, const SprinterPldState& pld)
{
    switch (code)
    {
        case 0x00: return "nothing (no device)";
        case 0xC0: case 0xC8:
            return (pld.cnf & 0x40) ? "stores cell #C0 only: CNF bit 6 'SC clean' keeps the #1FFD latch at 0 (/1FFD off)"
                                    : "the #1FFD latch: Scorpion paging (bit 4: +8 pages in window 3, bit 1: expansion ROM, bit 0: RAM at 0)";
        case 0xC1: case 0xC9:
        {
            std::string e = "the #7FFD latch";
            if (pld.cnf & 0x20)
                e += ": CNF bit 5 'PN clean' drops bits 4-0 (/7FFD off: no 128K paging)";
            else if (!(pld.cnf & 0x80))
                e += ": 128K paging, bits 7-6 cleaned (no /mem512)";
            else
                e += ": 128K paging with bits 7-6 (Pentagon 512, /mem512)";
            return e;
        }
        case 0xC2: return "border, beeper, tape out";
        case 0xC3: return "ALL_MODE";
        case 0xC6: case 0xCE: return "CNF/SYS: turbo request, map, clean rules";
        case 0x90: return "AY register select";
        case 0x91: return "AY data";
        default: break;
    }
    if (code >= 0x10 && code <= 0x14)
        return "WD1793 / Beta 128";
    if (code >= 0xF0)
        return "the cell of the current Spectrum page";
    return std::string();
}

std::string ReadEffect(uint8_t code)
{
    switch (code)
    {
        case 0x00: return "#FF (no device)";
        case 0x40: return "keyboard matrix, tape in";
        case 0x52: return "AY register read";
        case 0x15: return "Kempston joystick (+ Beta DRQ / INTRQ)";
        default: break;
    }
    if (code >= 0x10 && code <= 0x13)
        return "WD1793";
    if (code >= 0xC0 && code < 0xF0)
        return "the cell reads back";
    return std::string();
}

/// The ports (value, mask, 16 bits) that reach `codes` in the table for (map, pn5, dos, direction): the TTD
/// port-events queries
std::vector<std::pair<uint16_t, uint16_t>> PortsReaching(const uint8_t* table, uint8_t map, bool pn5, bool dosOn, bool isRead,
                                                         const std::vector<uint8_t>& codes)
{
    std::set<uint16_t> points;
    for (uint16_t bits = 0; bits < SprinterPortTable::kAddressCombinations; bits++)
    {
        const uint8_t code = table[SprinterPortTable::Index(map, pn5, !dosOn, isRead, SprinterPortTable::ExamplePort(bits))];
        if (std::find(codes.begin(), codes.end(), code) != codes.end())
            points.insert(bits);
    }
    std::vector<std::pair<uint16_t, uint16_t>> out;
    for (const auto& [value, mask] : Cubes(points))
        out.emplace_back(SprinterPortTable::ExamplePort(value), SprinterPortTable::ExamplePort(mask));
    return out;
}

/// The configuration codes the journal follows, by the journal kind they make
struct JournalCodes
{
    const char* kind;
    const char* what;
    std::vector<uint8_t> codes;
};
const std::vector<JournalCodes>& TrackedCodes()
{
    static const std::vector<JournalCodes> kCodes = {
        {"cnf", "CNF/SYS (turbo request, map, clean rules)", {0xC6, 0xCE}},
        {"port_1ffd", "#1FFD", {0xC0, 0xC8}},
        {"port_7ffd", "#7FFD", {0xC1, 0xC9}},
        {"all_mode", "ALL_MODE", {0xC3}},
        {"rgmod", "RGMOD", {0xC5, 0xCD}},
        {"hold", "HOLD", {0xCB}},
        {"frame_lines", "frame length (#2C 320, #2D 312 lines)", {0x2C, 0x2D}},
        {"pld_load", "PLD reload (#2E)", {0x2E}},
    };
    return kCodes;
}

StateNode TtdQueries(EmulatorContext* context, PortDecoder_Sprinter& decoder)
{
    const SprinterPldState& pld = decoder.GetPldState();
    const uint8_t* table = context->pMemory->RAMPageAddress(SprinterMemory::kPortTablePage);
    const uint8_t map = static_cast<uint8_t>((pld.cnf >> 3) & 3);
    StateNode arr = StateNode::Array();
    for (const JournalCodes& c : TrackedCodes())
    {
        StateNode q = StateNode::Object();
        q["kind"] = c.kind;
        q["what"] = c.what;
        StateNode ports = StateNode::Array();
        for (const auto& [port, mask] : PortsReaching(table, map, (pld.pn & 0x20) != 0, pld.dos == 0, false, c.codes))
        {
            StateNode p = StateNode::Object();
            p["port"] = Hex16(port);
            p["port_mask"] = Hex16(mask);
            p["request"] = StringHelper::Format("POST /ttd/port-events {\"event\":\"out\",\"port\":\"0x%04X\",\"port_mask\":\"0x%04X\"}", port, mask);
            ports.push(p);
        }
        q["ports"] = ports;
        arr.push(q);
    }
    return arr;
}

StateNode EventNode(const MachineEvent& e)
{
    StateNode n = StateNode::Object();
    n["seq"] = e.seq;
    n["epoch"] = static_cast<uint64_t>(e.epoch);
    n["frame"] = e.frame;
    n["t"] = static_cast<uint64_t>(e.t);
    n["line"] = static_cast<uint64_t>(e.t / SprinterIntSource::kLineTStates);
    n["t_in_line"] = static_cast<uint64_t>(e.t % SprinterIntSource::kLineTStates);
    n["pc"] = Hex16(e.pc);
    n["kind"] = e.kind;
    if (e.port >= 0)
        n["port"] = Hex16(static_cast<unsigned>(e.port));
    if (e.value >= 0)
        n["value"] = e.kind == std::string("port_table") || e.kind == std::string("clock") || e.kind == std::string("frame_lines") ||
                             e.kind == std::string("pld_configured") || e.kind == std::string("f12")
                         ? StateNode(static_cast<int64_t>(e.value))
                         : StateNode(Hex8(static_cast<unsigned>(e.value)));
    if (e.previous >= 0)
        n["previous"] = e.kind == std::string("clock") || e.kind == std::string("frame_lines") || e.kind == std::string("f12")
                            ? StateNode(static_cast<int64_t>(e.previous))
                            : StateNode(Hex8(static_cast<unsigned>(e.previous)));
    n["text"] = e.text;
    if (!e.details.empty())
    {
        StateNode d = StateNode::Array();
        for (const std::string& line : e.details)
            d.push(line);
        n["details"] = d;
    }
    return n;
}

const char* const kJournalKinds[][2] = {
    {"port_table", "page #40 written: one event per frame, the key ZX port decodes it changed"},
    {"cnf", "CNF/SYS write that changed the turbo request or the CNF byte (map, clean rules)"},
    {"clock", "the CPU clock changed (3.5 / 21 MHz) and why"},
    {"port_7ffd", "#7FFD write (a new value or a latch change), the port used"},
    {"port_1ffd", "#1FFD write (a new value or a latch change), the port used (#01FD is #1FFD to the PLD)"},
    {"all_mode", "ALL_MODE change (ZX screen + keyboard, original waits)"},
    {"rgmod", "RGMOD change (mode table page)"},
    {"hold", "HOLD change (picture shift)"},
    {"frame_lines", "frame length change (codes #2C / #2D)"},
    {"pld_load", "the PLD loads a configuration (power-on, RESET, code #2E)"},
    {"pld_configured", "the load finished: the module chosen, the bitstream hashes"},
    {"f12", "the front-panel turbo switch (F12)"},
    {"ctrl_alt_del", "Ctrl+Alt+Del: CPU reset by the PLD's keyboard block"},
    {"reset", "power on, the RESET button, the page #A0 soft restart"},
};
/// The status line: the launcher's mode name from RAM, else the best-matching file, and the clock / paging facts
DeviceState::SprinterZxBrief ZxBriefLine(EmulatorContext* context)
{
    DeviceState::SprinterZxBrief b;
    PortDecoder_Sprinter* decoder = SprinterDecoder(context);
    if (!decoder || !context->pMemory)
        return b;
    b.sprinter = true;
    const SprinterPldState& pld = decoder->GetPldState();
    b.active = pld.configState == SprinterConfigState::Configured && pld.romOff && !pld.cacheOn && (pld.allMode & 0x01) == 0;
    if (!b.active)
    {
        b.text = "ZX: off";
        b.details = "Not in the ZX (Spectrum) mode: the Sprinter runs its own software (DSS, the BIOS)";
        return b;
    }
    std::string name;
    for (const uint8_t page : {static_cast<uint8_t>(0xFF), static_cast<uint8_t>(0x41)})
    {
        const std::vector<std::string> l = NulLines(context->pMemory->RAMPageAddress(page), page == 0xFF ? 13 : 8);
        if (l.size() >= 4 && !l[0].empty() && !OptionLine(l).empty())
        {
            name = l[0];
            break;
        }
    }
    ZxOptions hw;
    hw.turbo = (pld.cnf & 0x01) != 0;
    hw.sprinter = ((pld.cnf >> 3) & 1) == 0;
    hw.p7ffd = !(pld.cnf & 0x20);
    hw.p1ffd = !(pld.cnf & 0x40);
    hw.mem512 = (pld.cnf & 0x80) != 0;
    hw.lines312 = pld.frameLines != 0;
    hw.origin = (pld.allMode & 0x04) == 0;
    if (name.empty())
    {
        size_t bestDiffs = 100;
        for (const KnownZxMode& m : kKnownModes)
        {
            size_t d = OptionDifferences(m.options, hw).size();
            if ((pld.cnf & 0x04) && ExpectedCnf(m.options, std::string(m.launcher) == "community") != pld.cnf)
                d++;
            if (d < bestDiffs)
            {
                bestDiffs = d;
                name = std::string(m.name) + (d ? "?" : "");
            }
        }
    }
    std::vector<std::string> parts;
    const unsigned ratio = context->emulatorState.hw_turbo_ratio ? context->emulatorState.hw_turbo_ratio : 1;
    if (pld.turbo)
        parts.push_back("turbo req");
    if (pld.turbo && !pld.turboHard)
        parts.push_back("F12 3.5 MHz");
    else
        parts.push_back(ratio >= 6 ? "21 MHz" : "3.5 MHz");
    if (hw.p1ffd)
        parts.push_back("/1FFD");
    if (hw.mem512)
        parts.push_back("/mem512");
    if (hw.lines312)
        parts.push_back("312 lines");
    if (hw.origin)
        parts.push_back("orig waits");
    std::string text = "ZX: " + name + " (";
    for (size_t i = 0; i < parts.size(); i++)
        text += (i ? ", " : "") + parts[i];
    text += ")";
    b.text = text;
    return b;
}
}  // namespace

namespace DeviceState
{

StateNode SprinterZxMode(EmulatorContext* context, bool deep)
{
    PortDecoder_Sprinter* decoder = SprinterDecoder(context);
    if (!decoder || !context->pMemory)
        return Unavailable("Not a Sprinter machine");

    const SprinterPldState& pld = decoder->GetPldState();
    Memory& memory = *context->pMemory;
    const EmulatorState& state = context->emulatorState;
    const std::map<uint16_t, std::string> names = CodeNames(*decoder);

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["pld"] = PldModule(*decoder);  // the ZX mode needs the Standard configuration (its screen, ports, waits)

    const bool configured = pld.configState == SprinterConfigState::Configured;
    const bool vrom = configured && pld.romOff && !pld.cacheOn;
    const bool zxScreen = (pld.allMode & 0x01) == 0;
    const bool active = vrom && zxScreen;
    ret["active"] = active;
    ret["active_rule"] = StringHelper::Format(
        "window 0 shows a vROM page (system ROM out, fast RAM off: %s) and ALL_MODE bit 0 = 0 (ZX screen shadow + ZX "
        "keyboard: %s)",
        vrom ? "yes" : "no", zxScreen ? "yes" : "no");

    // The picture
    {
        StateNode p = StateNode::Object();
        const SprinterPicture shown = SprinterPicture::Of(decoder->GetVideoRam().Data(), pld.rgMod & 0x01);
        p["mode"] = SprinterSquare::Name(shown.mode);
        p["brief"] = shown.Brief(static_cast<uint8_t>((pld.pn >> 3) & 1));
        ret["picture"] = p;
    }

    // The ROMs in the vROM cells
    std::string romSet;
    StateNode rom = StateNode::Object();
    {
        const uint8_t w0 = Window0Cell(pld);
        StateNode cells = StateNode::Array();
        std::map<std::string, int> sets;
        for (uint8_t cell : {0xE2, 0xE3, 0xE1, 0xE0})
        {
            const uint8_t page = pld.Cell(cell);
            const uint32_t crc = Crc32(memory.RAMPageAddress(page), PAGE_SIZE);
            const KnownRom* known = FindRom(crc);
            StateNode c = StateNode::Object();
            c["cell"] = StringHelper::Format("#%02X", cell);
            c["role"] = VromRole(cell);
            c["page"] = Hex8(page);
            c["crc32"] = Hex32(crc);
            c["rom"] = known ? known->name : (page == 0x41 ? "none (the cell points at page #41: slot not loaded)" : "unknown image");
            c["in_window_0"] = vrom && w0 == cell;
            if (known && cell != 0xE0)
                sets[known->set]++;
            cells.push(c);
        }
        int best = 0;
        for (const auto& [set, count] : sets)
            if (count > best)
            {
                best = count;
                romSet = set;
            }
        rom["window_0_cell"] = vrom ? StringHelper::Format("#%02X", w0) : std::string("system ROM / fast RAM (not a vROM)");
        rom["window_0_role"] = vrom ? VromRole(w0) : "";
        rom["set"] = romSet.empty() ? "unknown" : romSet;
        rom["set_name"] = RomSetName(romSet);
        rom["cells"] = cells;
        rom["note"] = "vROM pages are RAM: the CRC-32 of each 16 KB page against the launchers' ROM files and the BIOS flash copies";
    }

    // The options as the hardware implements them
    const bool cnfValid = (pld.cnf & 0x04) != 0;
    ZxOptions hw;
    hw.turbo = (pld.cnf & 0x01) != 0;
    hw.sprinter = ((pld.cnf >> 3) & 1) == 0;
    hw.p7ffd = !(pld.cnf & 0x20);
    hw.p1ffd = !(pld.cnf & 0x40);
    hw.mem512 = (pld.cnf & 0x80) != 0;
    hw.lines312 = pld.frameLines != 0;
    hw.origin = (pld.allMode & 0x04) == 0;

    // The INT position
    auto* screen = dynamic_cast<ScreenSprinter*>(context->pScreen);
    const uint16_t lines = screen ? screen->FrameLines() : (pld.frameLines ? 312 : 320);
    const std::vector<uint32_t> ints = SprinterIntSource::ComputePositions(decoder->GetVideoRam(), pld.rgMod & 0x01, lines);
    std::string intKind = "none";
    if (!ints.empty())
    {
        const uint32_t line = ints.front() / SprinterIntSource::kLineTStates;
        intKind = line == 287 ? "pentagon" : line == 295 ? "spectrum (original)" : line == 271 ? "scorpion" : "other";
    }

    // The launcher's RAM
    StateNode launcher = StateNode::Object();
    std::string launcherName, launcherOptions;
    bool launcherFound = false;
    int retFn = -1;  // 1 = /ret-fn, 0 = /ret-zx (restart), -1 unknown
    {
        // The community launcher's .ZX text: SHARED_PAGE #FF from #0000, 13 NUL-terminated lines
        // (spectrum.asm READ_FILE_1 into SHARED_PAGE); Peters Plus: page #41 from #0000, 8 lines
        struct Source
        {
            uint8_t page;
            size_t lines;
            const char* who;
        };
        for (const Source& src : {Source{0xFF, 13, "community launcher (SPECTRUM.EXE v2.x): page #FF, the .ZX text it read"},
                                  Source{0x41, 8, "Peters Plus launcher: page #41, the .ZX text it read"}})
        {
            const std::vector<std::string> l = NulLines(memory.RAMPageAddress(src.page), src.lines);
            const std::string opts = OptionLine(l);
            if (l.size() < 4 || l[0].empty() || opts.empty())
                continue;
            launcherFound = true;
            launcherName = l[0];
            launcherOptions = opts;
            launcher["mode_text_source"] = src.who;
            launcher["mode_name"] = launcherName;
            launcher["option_line"] = launcherOptions;
            StateNode roms = StateNode::Array();
            for (size_t i = 1; i < l.size() && i < 4; i++)
                roms.push(l[i]);
            launcher["rom_files"] = roms;
            break;
        }
        launcher["mode_text_found"] = launcherFound;

        // The BIOS system page (community BIOS 3.06+ GOTO_SPECTRUM): #FE:#013A = the CNF byte, #013B = the start
        const uint8_t* sys = memory.RAMPageAddress(0xFE);
        StateNode bios = StateNode::Object();
        bios["cnf"] = Hex8(sys[0x013A]);
        bios["start"] = int(sys[0x013B]);
        bios["vrom_block"] = int(sys[0x012E]);
        bios["vram_block"] = int(sys[0x012F]);
        bios["cnf_matches_pld"] = cnfValid && sys[0x013A] == pld.cnf;
        bios["note"] = "community BIOS (3.06+) GOTO_SPECTRUM keeps its CNF and start arguments in the system page; the Peters "
                       "Plus launcher does not write them";
        launcher["bios_system_page"] = bios;

        // The reset intercept: cell #EE (RET_PORT) = #41 and page #41 #FFF0-#FFF6
        const uint8_t* p41 = memory.RAMPageAddress(0x41);
        StateNode hook = StateNode::Object();
        hook["cell_EE"] = Hex8(pld.Cell(0xEE));
        hook["installed"] = pld.Cell(0xEE) == 0x41;
        hook["launcher_pages"] = HexRow(p41 + 0x3FF0, 4);
        hook["handler"] = Hex16(static_cast<unsigned>(p41[0x3FF4] | p41[0x3FF5] << 8));
        hook["byte_FFF6"] = Hex8(p41[0x3FF6]);
        hook["zx_mark"] = p41[0x3FFE] == 'Z' && p41[0x3FFF] == 'X';
        launcher["reset_intercept"] = hook;

        // The option table: the launcher's pages from the intercept first, then (deep) every page
        LauncherParams params;
        std::set<uint8_t> tried;
        for (int i = 0; i < 4 && !params.found; i++)
        {
            const uint8_t page = p41[0x3FF0 + i];
            if (tried.insert(page).second)
                ParseParams(memory.RAMPageAddress(page), page, params);
        }
        std::vector<LauncherParams> all;
        if (!params.found && deep)
        {
            for (unsigned page = 0; page < 256; page++)
            {
                if (tried.count(static_cast<uint8_t>(page)))
                    continue;
                LauncherParams p;
                if (ParseParams(memory.RAMPageAddress(static_cast<uint8_t>(page)), static_cast<uint8_t>(page), p))
                    all.push_back(p);
            }
            if (!all.empty())
                params = all.back();
        }
        StateNode t = StateNode::Object();
        t["found"] = params.found;
        if (params.found)
        {
            t["page"] = Hex8(params.page);
            t["address"] = Hex16(params.base);
            if (all.size() > 1)
                t["note"] = StringHelper::Format("%zu copies in RAM (earlier launches); the last page is shown", all.size());
            StateNode flags = StateNode::Object();
            for (const auto& [name, on] : params.flags)
                flags[name] = on;
            t["flags"] = flags;
            bool fn = false, zx = false;
            const bool haveFn = ParamFlag(params, "ret-fn", fn);
            const bool haveZx = ParamFlag(params, "ret-zx", zx);
            if (haveFn && haveZx)
                retFn = (fn != zx) ? (fn ? 1 : 0) : -1;
        }
        else
            t["searched"] = deep ? "the launcher's pages (#41:#FFF0-#FFF3) and all RAM" : "the launcher's pages (#41:#FFF0-#FFF3)";
        launcher["option_table"] = t;
        // Peters Plus: #41:#FFF6 = #41 with /ret-fn, 0 without (it never reads /ret-zx)
        if (retFn < 0 && pld.Cell(0xEE) == 0x41 && p41[0x3FF6] == 0x41)
            retFn = 1;
        if (retFn < 0 && launcherFound)
            retFn = LineHasOption(launcherOptions, "ret-fn") ? 1 : (LineHasOption(launcherOptions, "ret-zx") ? 0 : -1);
    }

    // Options with their evidence
    StateNode config = StateNode::Object();
    {
        StateNode opts = StateNode::Array();
        auto opt = [&](const char* name, bool on, std::string evidence) {
            StateNode o = StateNode::Object();
            o["option"] = name;
            o["on"] = on;
            o["evidence"] = std::move(evidence);
            if (launcherFound)
                o["in_launcher_line"] = LineHasOption(launcherOptions, name + 1);
            opts.push(o);
        };
        opt("/turbo", hw.turbo, StringHelper::Format("CNF #%02X bit 0 (the launcher's turbo request); the live request is %s", pld.cnf,
                                                     pld.turbo ? "on" : "off"));
        opt("/sprinter", hw.sprinter, StringHelper::Format("CNF bits 4-3 = map %u: map 0 keeps the Sprinter ports reachable "
                                                           "with TR-DOS off, map 1 only with TR-DOS on",
                                                           (pld.cnf >> 3) & 3));
        opt("/7FFD", hw.p7ffd, StringHelper::Format("CNF bit 5 'PN clean' = %u", (pld.cnf >> 5) & 1));
        opt("/1FFD", hw.p1ffd, StringHelper::Format("CNF bit 6 'SC clean' = %u: %s", (pld.cnf >> 6) & 1,
                                                    hw.p1ffd ? "#1FFD / #01FD writes reach the Scorpion latch"
                                                             : "#1FFD / #01FD writes store the cell, the latch stays 0"));
        opt("/mem512", hw.mem512, StringHelper::Format("CNF bit 7 = %u (1: #7FFD bits 7-6 kept, Pentagon 512)", (pld.cnf >> 7) & 1));
        opt("/lines312", hw.lines312, StringHelper::Format("the PLD frame latch: %u lines", hw.lines312 ? 312 : 320));
        opt("/origin", hw.origin, StringHelper::Format("ALL_MODE #%02X bit 2 = %u (original waits %s)", pld.allMode, (pld.allMode >> 2) & 1,
                                                       hw.origin ? "on" : "off"));
        opt("/int-sc", intKind == "scorpion", "the INT position from the mode table: " + intKind +
                                                  " (SC256.ZX's '/sc-int' is not an option the launchers parse: its INT stays Pentagon)");
        config["options"] = opts;
        config["cnf"] = Hex8(pld.cnf);
        config["cnf_valid"] = cnfValid;
        config["all_mode"] = Hex8(pld.allMode);
        std::string line = OptionText(hw);
        if (retFn >= 0)
            line += retFn ? " /ret-fn" : " /ret-zx";
        config["option_line"] = line;
        config["return"] = retFn < 0 ? "unknown" : (retFn ? "/ret-fn: Ctrl+Alt+Del returns to DSS" : "/ret-zx: Ctrl+Alt+Del restarts the Spectrum");

        // The known mode files, best first
        struct Scored
        {
            const KnownZxMode* mode;
            std::vector<std::string> diffs;
            int score;
        };
        std::vector<Scored> scored;
        for (const KnownZxMode& m : kKnownModes)
        {
            Scored s{&m, OptionDifferences(m.options, hw), 0};
            const bool community = std::string(m.launcher) == "community";
            const uint8_t expected = ExpectedCnf(m.options, community);
            if (cnfValid && expected != pld.cnf)
                s.diffs.push_back(StringHelper::Format("CNF #%02X here, #%02X from the file", pld.cnf, expected));
            if (!romSet.empty() && romSet != m.romSet)
                s.diffs.push_back(std::string("ROMs: ") + RomSetName(romSet) + ", the file loads " + RomSetName(m.romSet));
            if (launcherFound && !LineHasOption(launcherOptions, "sprinter") == m.options.sprinter)
                s.diffs.push_back("the launcher's option line differs");
            s.score = static_cast<int>(s.diffs.size());
            if (launcherFound && launcherName.find(m.name) == std::string::npos)
                s.score += 1;  // the name in RAM breaks ties (SPECTRUM.CFG names itself "Default (Sprinter ZX)")
            scored.push_back(std::move(s));
        }
        std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) { return a.score < b.score; });

        const Scored& best = scored.front();
        StateNode b = StateNode::Object();
        b["file"] = best.mode->file;
        b["name"] = best.mode->name;
        b["launcher"] = best.mode->launcher;
        b["option_line"] = best.mode->optionLine;
        std::string confidence;
        std::string why;
        if (!active)
        {
            confidence = "none";
            why = "the machine is not in the ZX mode now";
        }
        else if (!cnfValid)
        {
            confidence = "low";
            why = "no CNF byte with bit 2 written since the reset: the options are not known";
        }
        else if (launcherFound && best.diffs.empty())
        {
            confidence = "certain";
            why = "the launcher's mode text in RAM (\"" + launcherName + "\": " + launcherOptions + ") and the hardware agree";
        }
        else if (best.diffs.empty() && !romSet.empty())
        {
            confidence = "high";
            why = "the CNF byte, ALL_MODE, the frame length and the ROM set all match the file";
        }
        else if (best.diffs.empty())
        {
            confidence = "medium";
            why = "the options match; the ROM set is not identified";
        }
        else
        {
            confidence = "low";
            why = "closest file, " + std::to_string(best.diffs.size()) + " difference(s)";
        }
        if (active && launcherFound && !best.diffs.empty())
            why += "; the launcher's text in RAM says \"" + launcherName + "\" (" + launcherOptions + ")";
        b["confidence"] = confidence;
        b["explanation"] = why;
        StateNode bd = StateNode::Array();
        for (const std::string& d : best.diffs)
            bd.push(d);
        b["differences"] = bd;
        config["best_match"] = b;

        StateNode others = StateNode::Array();
        for (size_t i = 1; i < scored.size() && i < 5; i++)
        {
            StateNode o = StateNode::Object();
            o["file"] = scored[i].mode->file;
            o["name"] = scored[i].mode->name;
            o["launcher"] = scored[i].mode->launcher;
            StateNode d = StateNode::Array();
            for (const std::string& s : scored[i].diffs)
                d.push(s);
            o["differences"] = d;
            others.push(o);
        }
        config["other_candidates"] = others;
        if (launcherFound)
        {
            StateNode agree = StateNode::Array();
            for (const char* name : {"turbo", "sprinter", "7FFD", "1FFD", "mem512", "lines312", "origin"})
            {
                const bool inLine = LineHasOption(launcherOptions, name);
                const std::string key = std::string("/") + name;
                bool hwOn = false;
                if (key == "/turbo") hwOn = hw.turbo;
                else if (key == "/sprinter") hwOn = hw.sprinter;
                else if (key == "/7FFD") hwOn = hw.p7ffd;
                else if (key == "/1FFD") hwOn = hw.p1ffd;
                else if (key == "/mem512") hwOn = hw.mem512;
                else if (key == "/lines312") hwOn = hw.lines312;
                else hwOn = hw.origin;
                if (inLine != hwOn)
                    agree.push(StringHelper::Format("%s: %s in the launcher's line, %s in the hardware", key.c_str(), inLine ? "on" : "off",
                                                    hwOn ? "on" : "off"));
            }
            launcher["hardware_disagrees"] = agree;
        }
    }
    ret["config"] = config;
    ret["launcher"] = launcher;

    // Clock
    {
        StateNode c = StateNode::Object();
        const unsigned ratio = state.hw_turbo_ratio ? state.hw_turbo_ratio : 1;
        c["requested"] = pld.turbo != 0;
        c["f12_switch"] = pld.turboHard != 0;
        c["mhz"] = ratio >= 6 ? "21" : "3.5";
        c["ratio"] = ratio;
        std::string why;
        if (pld.turbo && pld.turboHard)
            why = "21 MHz: the CNF turbo request is on and the front-panel switch (F12) allows it";
        else if (pld.turbo)
            why = "3.5 MHz: turbo requested (CNF), but the front-panel switch (F12) is off";
        else
            why = std::string("3.5 MHz: no turbo request (CNF bit 0 = 0)") + (pld.turboHard ? "; F12 would allow it" : "; F12 is off too");
        c["why"] = why;
        ret["clock"] = c;
    }

    // Frame and INT
    {
        StateNode f = StateNode::Object();
        f["lines"] = int(lines);
        f["t_states"] = static_cast<uint64_t>(lines) * SprinterIntSource::kLineTStates;
        StateNode i = StateNode::Object();
        i["kind"] = intKind;
        if (!ints.empty())
        {
            i["t_in_frame"] = static_cast<uint64_t>(ints.front());
            i["line"] = static_cast<uint64_t>(ints.front() / SprinterIntSource::kLineTStates);
            i["t_in_line"] = static_cast<uint64_t>(ints.front() % SprinterIntSource::kLineTStates);
        }
        i["count"] = static_cast<uint64_t>(ints.size());
        i["note"] = "base T-states (3.5 MHz); the mode table's blank + INT squares place it (FN_SYNC: Pentagon line 287, "
                    "original line 295, Scorpion line 271)";
        f["int"] = i;
        ret["frame"] = f;
    }

    ret["rom"] = rom;

    // Paging
    {
        StateNode p = StateNode::Object();
        p["port_7ffd"] = Hex8(pld.pn);
        p["port_1ffd"] = Hex8(pld.sc);
        p["cell_C0_raw_1ffd"] = Hex8(pld.Cell(0xC0));
        p["cell_C1_raw_7ffd"] = Hex8(pld.Cell(0xC1));
        p["map"] = int((pld.cnf >> 3) & 3);
        p["dos"] = pld.dos == 0;
        p["window_3_cell"] = StringHelper::Format("#%02X", 0xC0 + (pld.pg3 & 0x3F));
        p["window_3_page"] = Hex8(pld.cells[pld.pg3 & 0x3F]);
        p["spectrum_pages_F0_FF"] = HexRow(pld.cells + 0x30, 16);
        ret["paging"] = p;
    }

    // The key ports through the live table
    {
        const uint8_t* table = memory.RAMPageAddress(SprinterMemory::kPortTablePage);
        const uint8_t map = static_cast<uint8_t>((pld.cnf >> 3) & 3);
        const bool pn5 = (pld.pn & 0x20) != 0;
        StateNode ports = StateNode::Array();
        for (const SprinterZxPorts::KeyPort& kp : SprinterZxPorts::kKeyPorts)
        {
            StateNode p = StateNode::Object();
            p["port"] = Hex16(kp.port);
            p["label"] = kp.label;
            for (int dosOn = 0; dosOn < 2; dosOn++)
            {
                StateNode side = StateNode::Object();
                const uint8_t w = table[SprinterPortTable::Index(map, pn5, dosOn == 0, false, kp.port)];
                const uint8_t r = table[SprinterPortTable::Index(map, pn5, dosOn == 0, true, kp.port)];
                StateNode out = StateNode::Object();
                out["code"] = Hex8(w);
                out["name"] = w ? CodeName(names, w) : std::string("None");
                out["effect"] = WriteEffect(w, pld);
                side["out"] = out;
                StateNode in = StateNode::Object();
                if (Z84Lib::Z84C15::Owns(static_cast<uint8_t>(kp.port)))
                    in["answered_by"] = "Z84C15";
                in["code"] = Hex8(r);
                in["name"] = r ? CodeName(names, r) : std::string("None");
                in["effect"] = ReadEffect(r);
                side["in"] = in;
                p[dosOn ? "tr_dos_on" : "tr_dos_off"] = side;
            }
            p["index_out"] = Hex16(SprinterPortTable::Index(map, pn5, pld.dos != 0, false, kp.port));
            StateNode q = StateNode::Object();
            q["port"] = Hex16(kp.port & SprinterZxPorts::kDecodedAddressMask);
            q["port_mask"] = Hex16(SprinterZxPorts::kDecodedAddressMask);
            p["ttd_query"] = q;
            ports.push(p);
        }
        StateNode pt = StateNode::Object();
        pt["map"] = int(map);
        pt["pn5"] = pn5;
        pt["dos_now"] = pld.dos == 0;
        pt["decoded_bits"] = "A15 A14 A13 A7 A6 A5 A2 A1 A0: #01FD = #1FFD, #C0FD = #DFFD";
        pt["rows"] = ports;
        pt["ttd_note"] = "ttd_query: POST /ttd/port-events {\"event\":\"out\",\"port\":..,\"port_mask\":..} finds every spelling "
                         "of the port in a recording";
        ret["ports"] = pt;
    }

    ret["summary"] = ZxBriefLine(context).text;
    return ret;
}

SprinterZxBrief SprinterZxModeBrief(EmulatorContext* context, bool details)
{
    SprinterZxBrief b = ZxBriefLine(context);
    if (!b.sprinter || !b.active || !details)
        return b;
    // The tooltip: the report as text, without the RAM-wide search and the port rows
    const StateNode full = SprinterZxMode(context, false);
    StateNode trimmed = StateNode::Object();
    for (const char* key : {"summary", "config", "clock", "frame", "rom", "paging", "launcher"})
        if (const StateNode* n = full.find(key))
            trimmed[key] = *n;
    b.details = ToText(trimmed);
    return b;
}

bool SprinterJournalQueryFromStrings(const std::string& kinds, const std::string& since, const std::string& from,
                                     const std::string& to, const std::string& limit, const std::string& source,
                                     SprinterJournalQuery& query, std::string& error)
{
    query = SprinterJournalQuery();
    auto number = [&](const std::string& text, const char* name, int64_t& out) {
        if (text.empty())
            return true;
        char* end = nullptr;
        const long long v = std::strtoll(text.c_str(), &end, 10);
        if (!end || *end || v < 0)
        {
            error = std::string(name) + " must be a non-negative number";
            return false;
        }
        out = v;
        return true;
    };
    query.kinds = kinds;
    for (const std::string& k : MachineEventJournal::SplitKinds(kinds))
    {
        bool known = false;
        for (const auto& entry : kJournalKinds)
            known |= k == entry[0];
        if (!known)
        {
            error = "unknown kind '" + k + "' (port_table, cnf, clock, port_7ffd, port_1ffd, all_mode, rgmod, hold, frame_lines, "
                    "pld_load, pld_configured, f12, ctrl_alt_del, reset)";
            return false;
        }
    }
    int64_t s = 0, l = static_cast<int64_t>(query.limit);
    if (!number(since, "since", s) || !number(from, "from", query.frameFrom) || !number(to, "to", query.frameTo) ||
        !number(limit, "limit", l))
        return false;
    query.since = static_cast<uint64_t>(s);
    query.limit = static_cast<size_t>(l);
    if (source.empty() || source == "live")
        query.ttd = false;
    else if (source == "ttd")
        query.ttd = true;
    else
    {
        error = "source must be live or ttd";
        return false;
    }
    return true;
}

StateNode SprinterJournal(EmulatorContext* context, const SprinterJournalQuery& query)
{
    PortDecoder_Sprinter* decoder = SprinterDecoder(context);
    if (!decoder || !context->pMemory)
        return Unavailable("Not a Sprinter machine");

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["source"] = query.ttd ? "ttd" : "live";
    const std::vector<std::string> kinds = MachineEventJournal::SplitKinds(query.kinds);

    if (!query.ttd)
    {
        MachineEventJournal::Filter f;
        f.kinds = kinds;
        f.sinceSeq = query.since;
        f.frameFrom = query.frameFrom;
        f.frameTo = query.frameTo;
        f.limit = query.limit;
        const MachineEventJournal::Snapshot snap = decoder->PldJournal().Read(f);
        ret["enabled"] = snap.enabled;
        ret["epoch"] = static_cast<uint64_t>(snap.epoch);
        ret["held"] = static_cast<uint64_t>(snap.held);
        ret["appended"] = snap.appended;
        ret["dropped"] = snap.dropped;
        ret["rewound"] = snap.rewound;
        ret["matched"] = static_cast<uint64_t>(snap.matched);
        ret["capacity"] = static_cast<uint64_t>(MachineEventJournal::kCapacity);
        StateNode events = StateNode::Array();
        for (const MachineEvent& e : snap.events)
            events.push(EventNode(e));
        ret["events"] = events;
        ret["now"] = StringHelper::Format("frame %llu, T %u", static_cast<unsigned long long>(context->emulatorState.frame_counter),
                                          decoder->BaseTstate());
    }
    else
    {
        ttd::TimeTravelManager* mgr = context->pTimeTravelManager;
        StateNode events = StateNode::Array();
        if (!mgr)
            ret["error"] = "no TTD manager";
        else
        {
            const SprinterPldState& pld = decoder->GetPldState();
            const uint8_t* table = context->pMemory->RAMPageAddress(SprinterMemory::kPortTablePage);
            const uint8_t map = static_cast<uint8_t>((pld.cnf >> 3) & 3);
            const uint32_t units = context->emulatorState.ttd_clock_units ? context->emulatorState.ttd_clock_units : 1;
            struct Hit
            {
                ttd::TTDPortHit hit;
                const JournalCodes* codes;
            };
            std::vector<Hit> hits;
            std::string error;
            bool truncated = false;
            for (const JournalCodes& c : TrackedCodes())
            {
                if (!kinds.empty() && std::find(kinds.begin(), kinds.end(), std::string(c.kind)) == kinds.end())
                    continue;
                for (const auto& [port, mask] : PortsReaching(table, map, (pld.pn & 0x20) != 0, pld.dos == 0, false, c.codes))
                {
                    ttd::TTDPortQuery q;
                    q.direction = ttd::TTDPortJournal::Direction::Write;
                    q.portValue = port;
                    q.portMask = mask;
                    if (query.frameFrom >= 0)
                        q.from = ttd::TTDTimePoint{static_cast<uint64_t>(query.frameFrom), 0};
                    if (query.frameTo >= 0)
                        q.to = ttd::TTDTimePoint{static_cast<uint64_t>(query.frameTo), UINT32_MAX};
                    q.limit = 100000;
                    const ttd::TTDPortSearchResult r = mgr->SearchPortEvents(q);
                    if (!r.ok)
                    {
                        error = r.error;
                        break;
                    }
                    truncated |= r.truncated;
                    for (const ttd::TTDPortHit& h : r.hits)
                        hits.push_back({h, &c});
                }
                if (!error.empty())
                    break;
            }
            if (!error.empty())
                ret["error"] = error;
            std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.hit.record.Time() < b.hit.record.Time(); });
            // As the live journal: the writes that change something. A CNF/SYS write counts with bit 1 (the turbo
            // request) or bit 2 (the CNF byte) set - #3C / #7C writes with neither switch the ROM / vROM set on
            // every BIOS call; the other kinds count when the value differs from the kind's last write
            {
                std::map<std::string, int> last;
                std::vector<Hit> kept;
                for (const Hit& h : hits)
                {
                    const std::string kind = h.codes->kind;
                    const uint8_t v = h.hit.record.value;
                    if (kind == "cnf")
                    {
                        if (!(v & 0x06))
                            continue;
                        const int key = v;
                        if (last.count(kind) && last[kind] == key)
                            continue;
                        last[kind] = key;
                    }
                    else if (kind != "pld_load")
                    {
                        if (last.count(kind) && last[kind] == v)
                            continue;
                        last[kind] = v;
                    }
                    kept.push_back(h);
                }
                hits.swap(kept);
            }
            if (query.limit && hits.size() > query.limit)
                hits.erase(hits.begin(), hits.end() - static_cast<std::ptrdiff_t>(query.limit));
            for (const Hit& h : hits)
            {
                const ttd::TTDPortRecord& r = h.hit.record;
                const uint32_t t = r.tInFrame / units;
                StateNode n = StateNode::Object();
                n["frame"] = r.frame;
                n["t"] = static_cast<uint64_t>(t);
                n["line"] = static_cast<uint64_t>(t / SprinterIntSource::kLineTStates);
                n["t_in_line"] = static_cast<uint64_t>(t % SprinterIntSource::kLineTStates);
                n["pc"] = Hex16(r.pc);
                n["kind"] = h.codes->kind;
                n["port"] = Hex16(r.port);
                n["value"] = Hex8(r.value);
                n["text"] = StringHelper::Format("OUT (#%04X) <- #%02X: %s", r.port, r.value, h.codes->what);
                events.push(n);
            }
            ret["truncated"] = truncated;
            ret["decoded_with"] = StringHelper::Format("the port table as it is now: map %u, TR-DOS %s, PN5 %u (a recording that "
                                                       "changed the map or wrote the table decodes with today's table)",
                                                       map, pld.dos == 0 ? "on" : "off", (pld.pn >> 5) & 1);
        }
        ret["events"] = events;
    }

    ret["ttd_queries"] = TtdQueries(context, *decoder);
    StateNode k = StateNode::Array();
    for (const auto& entry : kJournalKinds)
    {
        StateNode n = StateNode::Object();
        n["kind"] = entry[0];
        n["about"] = entry[1];
        k.push(n);
    }
    ret["kinds"] = k;
    return ret;
}

StateNode SprinterJournalControl(EmulatorContext* context, int enable, bool clear)
{
    PortDecoder_Sprinter* decoder = SprinterDecoder(context);
    if (!decoder || !context->pMemory)
        return Unavailable("Not a Sprinter machine");
    if (enable >= 0)
        decoder->SetPldJournalEnabled(enable != 0);
    if (clear)
        decoder->PldJournal().Clear();
    const MachineEventJournal::Snapshot snap = decoder->PldJournal().Read(MachineEventJournal::Filter{{}, UINT64_MAX, -1, -1, 1});
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["enabled"] = snap.enabled;
    ret["held"] = static_cast<uint64_t>(snap.held);
    ret["appended"] = snap.appended;
    ret["cleared"] = clear;
    return ret;
}

}  // namespace DeviceState
