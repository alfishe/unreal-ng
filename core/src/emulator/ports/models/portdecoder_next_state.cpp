// DeviceState::Next / NextRegs / NextMmu - the ZX Spectrum Next reports every automation interface renders
// (declared in emulator/state/devicestate.h; built here, beside the Next code, so the shared state code names no Next
// type). Design: docs/inprogress/2026-10-07-zx-next/design-automation.md

#include "stdafx.h"

#include "emulator/state/devicestate.h"

#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextregjournal.h"
#include "emulator/io/z80n/nextdivmmcjournal.h"
#include "emulator/io/z80n/nextsdjournal.h"
#include "emulator/io/z80n/nextregtable.h"
#include "emulator/ports/models/portdecoder_next.h"

namespace
{
StateNode Unavailable()
{
    StateNode n = StateNode::Object();
    n["available"] = false;
    n["description"] = "not a ZX Spectrum Next";
    return n;
}

PortDecoder_Next* NextDecoder(EmulatorContext* context)
{
    return context ? dynamic_cast<PortDecoder_Next*>(context->pPortDecoder) : nullptr;
}

std::string Hex8(unsigned value) { return StringHelper::Format("0x%02X", value & 0xFFu); }
std::string Hex16(unsigned value) { return StringHelper::Format("0x%04X", value & 0xFFFFu); }

const char* TimingName(uint8_t timing)
{
    switch (timing)
    {
        case 1: return "48K";
        case 2: return "128K";
        case 3: return "+3";
        case 4: return "Pentagon";
        default: return "unknown";
    }
}

StateNode NextSlotNodes(EmulatorContext* context, PortDecoder_Next& decoder)
{
    (void)decoder;
    NextMemory& memory = *static_cast<NextMemory*>(context->pMemory);
    StateNode slots = StateNode::Array();
    for (unsigned s = 0; s < NextMemory::kSlots; s++)
    {
        StateNode slot = StateNode::Object();
        slot["slot"] = int(s);
        slot["address_range"] = StringHelper::Format("0x%04X-0x%04X", s * 0x2000, s * 0x2000 + 0x1FFF);
        slot["mmu"] = Hex8(memory.GetMmu(s));
        slot["kind"] = memory.SlotKind(s);
        slot["buffer_offset"] = StringHelper::Format("0x%08X", memory.SlotReadOffset(s));
        slots.push(std::move(slot));
    }
    return slots;
}
}  // namespace

namespace DeviceState
{
StateNode NextMmu(EmulatorContext* context)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextMemory& memory = *static_cast<NextMemory*>(context->pMemory);
    StateNode n = StateNode::Object();
    n["available"] = true;
    n["slots"] = NextSlotNodes(context, *decoder);
    n["rom_select"] = int(memory.GetRomSelect());
    n["config_mode"] = memory.InConfigMode();
    n["boot_rom"] = memory.BootRomEnabled();
    n["config_bank"] = int(memory.GetConfigBank());
    n["extended_bank"] = Hex8(memory.GetExtendedBank());
    n["p7ffd"] = Hex8(context->emulatorState.p7FFD);
    n["p1ffd"] = Hex8(context->emulatorState.p1FFD);
    const NextMemory::DivMmcView& div = memory.GetDivMmcView();
    n["divmmc_mapped"] = div.mapped;
    return n;
}

StateNode NextRegs(EmulatorContext* context)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    StateNode n = StateNode::Object();
    n["available"] = true;
    n["selected"] = Hex8(decoder->Board().SelectedRegister());
    StateNode regs = StateNode::Array();
    size_t count = 0;
    const NextRegInfo* table = NextRegTable(count);
    for (size_t i = 0; i < count; i++)
    {
        const NextRegInfo& r = table[i];
        StateNode reg = StateNode::Object();
        reg["nr"] = Hex8(r.number);
        reg["name"] = r.name;
        reg["access"] = std::string(r.readable ? "R" : "") + (r.writable ? "W" : "");
        reg["value"] = Hex8(r.readable ? decoder->Board().Read(r.number) : decoder->Board().Stored(r.number));
        if (r.hasReset)
            reg["reset"] = Hex8(r.reset);
        regs.push(std::move(reg));
    }
    n["registers"] = std::move(regs);
    return n;
}

namespace
{
StateNode EventNode(const NextRegWriteEvent& e, const NextRegInfo* table, size_t count)
{
    StateNode n = StateNode::Object();
    n["seq"] = static_cast<uint64_t>(e.seq);
    n["frame"] = static_cast<uint64_t>(e.frame);
    n["t"] = static_cast<uint64_t>(e.t);
    n["pc"] = StringHelper::Format("0x%04X", e.pc);
    n["source"] = NextRegSourceName(e.source);
    n["reg"] = Hex8(e.reg);
    for (size_t i = 0; i < count; i++)
        if (table[i].number == e.reg)
        {
            n["name"] = table[i].name;
            break;
        }
    n["value"] = Hex8(e.value);
    n["previous"] = Hex8(e.previous);
    const std::string decoded = NextRegDecode(e.reg, e.value);
    if (!decoded.empty())
        n["decoded"] = decoded;
    return n;
}

StateNode JournalState(const NextRegJournal& journal)
{
    StateNode n = StateNode::Object();
    n["available"] = true;
    n["enabled"] = journal.Enabled();
    n["size"] = static_cast<uint64_t>(journal.Size());
    n["capacity"] = static_cast<uint64_t>(journal.Capacity());
    n["evicted"] = journal.Evicted();
    n["last_seq"] = journal.LastSeq();
    return n;
}
}  // namespace

StateNode NextRegJournalReport(EmulatorContext* context, const NextRegJournalQuery& query)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    const NextRegJournal& journal = decoder->Board().Journal();
    StateNode n = JournalState(journal);
    size_t count;
    const NextRegInfo* table = NextRegTable(count);
    StateNode events = StateNode::Array();
    for (const NextRegWriteEvent& e : journal.Query(query))
        events.push(EventNode(e, table, count));
    n["events"] = std::move(events);
    n["now"] = StringHelper::Format("frame %llu", static_cast<unsigned long long>(context->emulatorState.frame_counter));
    return n;
}

StateNode NextRegJournalControl(EmulatorContext* context, int enable, bool clear, size_t capacity)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextRegJournal& journal = decoder->Board().Journal();
    if (capacity > 0)
        journal.SetCapacity(capacity);
    if (clear)
        journal.Clear();
    if (enable >= 0)
        journal.SetEnabled(enable != 0);
    return JournalState(journal);
}

namespace
{
StateNode SdJournalState(const NextSdJournal& journal)
{
    StateNode n = StateNode::Object();
    n["available"] = true;
    n["enabled"] = journal.Enabled();
    n["size"] = static_cast<uint64_t>(journal.Size());
    n["capacity"] = static_cast<uint64_t>(journal.Capacity());
    n["evicted"] = journal.Evicted();
    n["last_seq"] = journal.LastSeq();
    return n;
}
}  // namespace

StateNode NextSdReport(EmulatorContext* context, const NextSdJournalQuery& query)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    StateNode n = SdJournalState(decoder->SdJournal());
    StateNode cards = StateNode::Array();
    for (unsigned c = 0; c < 2; c++)
    {
        const SdCardSpi& card = decoder->SdCard(c);
        StateNode node = StateNode::Object();
        node["card"] = int(c);
        node["present"] = card.present();
        if (card.present())
        {
            node["type"] = card.isSdhc() ? "SDHC" : "SDSC";
            node["state"] = card.stateName();
            node["size_bytes"] = static_cast<uint64_t>(card.sizeBytes());
            node["in_transfer"] = card.InTransfer();
            if (card.lastCommand() != 0xFF)
            {
                node["last_command"] = NextSdCommandName(card.lastCommand(), false);
                node["last_argument"] = StringHelper::Format("0x%08X", card.lastArgument());
            }
        }
        node["blocks_read"] = static_cast<uint64_t>(card.blocksRead());
        node["blocks_written"] = static_cast<uint64_t>(card.blocksWritten());
        cards.push(std::move(node));
    }
    n["cards"] = std::move(cards);
    StateNode spi = StateNode::Object();
    const int line = decoder->SpiSelectedLine();
    spi["selected"] = line >= 0 ? StateNode(int(line)) : StateNode("none");
    spi["byte_in_flight"] = decoder->SpiByteInFlight();
    spi["last_received"] = Hex8(decoder->SpiLastReceived());
    spi["too_fast_accesses"] = int(decoder->SpiTooFastCount());
    n["spi"] = std::move(spi);
    StateNode events = StateNode::Array();
    for (const NextSdEvent& e : decoder->SdJournal().Query(query))
    {
        StateNode ev = StateNode::Object();
        ev["seq"] = e.seq;
        ev["frame"] = int(e.frame);
        ev["t"] = int(e.t);
        ev["pc"] = Hex16(e.pc);
        ev["card"] = int(e.card);
        ev["kind"] = NextSdKindName(e.kind);
        if (e.kind == NextSdKind::Command)
        {
            ev["command"] = NextSdCommandName(e.index, e.app);
            ev["argument"] = StringHelper::Format("0x%08X", e.argument);
            ev["r1"] = e.r1 == 0xFF ? StateNode("none") : StateNode(Hex8(e.r1));
            ev["crc_valid"] = e.crcValid;
        }
        else
        {
            ev["block"] = e.block;
            if (e.kind == NextSdKind::WriteBlock)
                ev["ok"] = e.ok;
        }
        events.push(std::move(ev));
    }
    n["events"] = std::move(events);
    return n;
}

StateNode NextSdJournalControl(EmulatorContext* context, int enable, bool clear, size_t capacity)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextSdJournal& journal = decoder->SdJournal();
    if (capacity > 0)
        journal.SetCapacity(capacity);
    if (clear)
        journal.Clear();
    if (enable >= 0)
        journal.SetEnabled(enable != 0);
    return SdJournalState(journal);
}

namespace
{
StateNode DivMmcJournalState(const NextDivMmcJournal& journal)
{
    StateNode n = StateNode::Object();
    n["available"] = true;
    n["enabled"] = journal.Enabled();
    n["size"] = static_cast<uint64_t>(journal.Size());
    n["capacity"] = static_cast<uint64_t>(journal.Capacity());
    n["evicted"] = journal.Evicted();
    n["last_seq"] = journal.LastSeq();
    return n;
}
}  // namespace

StateNode NextDivMmcReport(EmulatorContext* context, const NextDivMmcJournalQuery& query)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextDivMmc& divmmc = decoder->DivMmc();
    NextBoard& board = decoder->Board();
    StateNode n = DivMmcJournalState(divmmc.Journal());

    StateNode now = StateNode::Object();
    now["port_enabled"] = divmmc.PortEnabled();
    now["automap_armed"] = divmmc.AutomapArmed();
    now["control"] = Hex8(divmmc.ReadPort());
    now["conmem"] = (divmmc.ReadPort() & 0x80) != 0;
    now["mapram"] = divmmc.Mapram();
    now["bank"] = int(divmmc.Bank());
    now["automap_latch"] = divmmc.Automapped();
    now["mapped"] = divmmc.Mapped();
    now["button_pending"] = divmmc.ButtonPending();
    now["nmi_hold"] = divmmc.NmiHold();
    n["state"] = std::move(now);

    // NR #B8-#BB decoded: the eight RST entries, then the extras
    const uint8_t enable = board.Stored(0xB8), valid = board.Stored(0xB9), timing = board.Stored(0xBA), extra = board.Stored(0xBB);
    StateNode entries = StateNode::Array();
    for (unsigned i = 0; i < 8; i++)
    {
        StateNode e = StateNode::Object();
        e["address"] = Hex16(i * 8);
        e["enabled"] = ((enable >> i) & 1) != 0;
        e["when"] = ((valid >> i) & 1) ? "always" : "rom3_only";
        e["map"] = ((timing >> i) & 1) ? "instant" : "delayed";
        entries.push(std::move(e));
    }
    auto extraEntry = [&](const char* address, const char* what, bool on, const char* when, const char* map) {
        StateNode e = StateNode::Object();
        e["address"] = address;
        e["what"] = what;
        e["enabled"] = on;
        e["when"] = when;
        e["map"] = map;
        entries.push(std::move(e));
    };
    extraEntry("0x3Dxx", "tr-dos entry area", (extra & 0x80) != 0, "rom3_only", "instant");
    extraEntry("0x0066", "NMI (the DRIVE button)", (extra & 0x03) != 0, "button", (extra & 0x02) ? "instant" : "delayed");
    extraEntry("0x1FF8-0x1FFF", "map out", (extra & 0x40) != 0, "always", "after the fetch");
    extraEntry("0x056A", "ROM 3 routine", (extra & 0x20) != 0, "rom3_only", "delayed");
    extraEntry("0x04D7", "ROM 3 routine", (extra & 0x10) != 0, "rom3_only", "delayed");
    extraEntry("0x0562", "ROM 3 routine", (extra & 0x08) != 0, "rom3_only", "delayed");
    extraEntry("0x04C6", "ROM 3 routine", (extra & 0x04) != 0, "rom3_only", "delayed");
    n["entries"] = std::move(entries);

    StateNode events = StateNode::Array();
    for (const NextDivMmcEvent& e : divmmc.Journal().Query(query))
    {
        StateNode ev = StateNode::Object();
        ev["seq"] = e.seq;
        ev["frame"] = int(e.frame);
        ev["t"] = int(e.t);
        ev["pc"] = Hex16(e.pc);
        ev["kind"] = NextDivMmcKindName(e.kind);
        if (e.kind == NextDivMmcKind::MapIn || e.kind == NextDivMmcKind::MapOut)
            ev["address"] = Hex16(e.address);
        if (e.kind == NextDivMmcKind::MapIn)
        {
            ev["entry"] = NextDivMmcEntryName(e.address);
            ev["map"] = e.instant ? "instant" : "delayed";
        }
        if (e.kind == NextDivMmcKind::EsxCall)
        {
            ev["call"] = Hex8(e.call);
            if (const char* name = NextEsxCallName(e.call))
                ev["name"] = name;
            ev["return_address"] = Hex16(e.returnAddress);
            ev["basic_rom"] = e.basicRom;
            ev["af"] = Hex16(e.af);
            ev["bc"] = Hex16(e.bc);
            ev["de"] = Hex16(e.de);
            ev["hl"] = Hex16(e.hl);
        }
        else
        {
            ev["mapped"] = e.mapped;
            ev["control"] = Hex8(e.control);
        }
        events.push(std::move(ev));
    }
    n["events"] = std::move(events);
    return n;
}

StateNode NextDivMmcJournalControl(EmulatorContext* context, int enable, bool clear, size_t capacity)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextDivMmcJournal& journal = decoder->DivMmc().Journal();
    if (capacity > 0)
        journal.SetCapacity(capacity);
    if (clear)
        journal.Clear();
    if (enable >= 0)
        journal.SetEnabled(enable != 0);
    return DivMmcJournalState(journal);
}

StateNode Next(EmulatorContext* context)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextMemory& memory = *static_cast<NextMemory*>(context->pMemory);
    NextBoard& board = decoder->Board();
    const EmulatorState& state = context->emulatorState;

    StateNode n = StateNode::Object();
    n["available"] = true;

    StateNode machine = StateNode::Object();
    machine["machine_type"] = int(board.MachineType());
    machine["timing"] = TimingName(board.Timing());
    machine["config_mode"] = memory.InConfigMode();
    machine["boot_rom"] = memory.BootRomEnabled();
    machine["cpu_clock_hz"] = static_cast<double>(state.current_z80_frequency);
    machine["speed_ratio"] = int(state.hw_turbo_ratio);
    machine["contention_disabled"] = state.hw_contention_disabled != 0;
    machine["frame_tstates"] = int(context->config.frame);
    n["machine"] = std::move(machine);

    n["mmu"] = NextMmu(context);

    StateNode div = StateNode::Object();
    NextDivMmc& divmmc = decoder->DivMmc();
    div["control"] = Hex8(divmmc.ReadPort());
    div["mapped"] = divmmc.Mapped();
    div["automapped"] = divmmc.Automapped();
    div["mapram"] = divmmc.Mapram();
    div["bank"] = int(divmmc.Bank());
    div["automap_enabled"] = (board.Stored(NextBoard::kRegPeripheral3) & 0x10) != 0;
    div["entry_points"] = Hex8(board.Stored(0xB8));
    div["entry_valid"] = Hex8(board.Stored(0xB9));
    div["entry_timing"] = Hex8(board.Stored(0xBA));
    div["entry_extra"] = Hex8(board.Stored(0xBB));
    n["divmmc"] = std::move(div);

    NextInterruptSource& irq = decoder->Interrupts();
    StateNode ints = StateNode::Object();
    ints["mode"] = irq.HardwareMode() ? "hardware im2" : "pulse";
    ints["pending"] = Hex16(irq.PendingMask());
    ints["in_service"] = Hex16(irq.InServiceMask());
    uint8_t value = 0;
    for (uint8_t reg : {NextInterruptSource::kRegControl, NextInterruptSource::kRegEnable0, NextInterruptSource::kRegEnableCtc,
                        NextInterruptSource::kRegEnableUart, NextInterruptSource::kRegLineControl, NextInterruptSource::kRegLineValue})
        if (irq.ReadNr(reg, value))
            ints[StringHelper::Format("nr_%02x", reg)] = Hex8(value);
    n["interrupts"] = std::move(ints);

    StateNode ctc = StateNode::Array();
    for (unsigned c = 0; c < NextCtc::kChannels; c++)
    {
        StateNode ch = StateNode::Object();
        ch["channel"] = int(c);
        ch["control"] = Hex8(decoder->Ctc().Control(c));
        ch["time_constant"] = int(decoder->Ctc().TimeConstant(c));
        ch["running"] = decoder->Ctc().Running(c);
        ch["zero_counts"] = int(decoder->Ctc().Zeros(c));
        ctc.push(std::move(ch));
    }
    n["ctc"] = std::move(ctc);

    StateNode spi = StateNode::Object();
    for (unsigned c = 0; c < 2; c++)
    {
        StateNode card = StateNode::Object();
        card["present"] = decoder->SdCard(c).present();
        card["blocks_read"] = static_cast<double>(decoder->SdCard(c).blocksRead());
        card["blocks_written"] = static_cast<double>(decoder->SdCard(c).blocksWritten());
        spi[c == 0 ? "card0" : "card1"] = std::move(card);
    }
    spi["too_fast_accesses"] = int(decoder->SpiTooFastCount());
    n["spi"] = std::move(spi);

    Ds1307& rtc = decoder->I2c().Rtc();
    StateNode r = StateNode::Object();
    r["time_bcd"] = StringHelper::Format("%02X:%02X:%02X", rtc.Reg(2), rtc.Reg(1), rtc.Reg(0) & 0x7F);
    r["date_bcd"] = StringHelper::Format("20%02X-%02X-%02X", rtc.Reg(6), rtc.Reg(5), rtc.Reg(4));
    n["rtc"] = std::move(r);
    return n;
}
}  // namespace DeviceState
