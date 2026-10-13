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
#include "emulator/io/keyboard/pckey.h"
#include "emulator/io/z80n/nextextendedinput.h"
#include "emulator/io/z80n/nextintjournal.h"
#include "emulator/io/z80n/nextmagicport.h"
#include "emulator/io/z80n/nextsdjournal.h"
#include "emulator/io/z80n/nextuart.h"
#include "emulator/io/serial/serialpeer.h"
#include "emulator/io/z80n/nextinterrupts.h"
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
    if (e.beamLine >= 0)
    {
        n["beam_line"] = int(e.beamLine);
        n["beam_hc"] = int(e.beamHc);
    }
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

namespace
{
StateNode IntJournalState(const NextIntJournal& journal)
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

StateNode NamesNode(uint16_t mask)
{
    StateNode list = StateNode::Array();
    for (const char* name : NextInterruptSource::SourceNames(mask))
        list.push(StateNode(std::string(name)));
    return list;
}

/// zxnext.vhd nr_02_reset_type: 100 at power on, then 010, then 001 for every reset that follows; a read of NR #02 shows bits 1:0
const char* ResetBitsName(uint8_t bits)
{
    switch (bits & 3)
    {
        case 0: return "power_on";
        case 2: return "first_reset_after_power_on";
        default: return "later_reset";
    }
}
}  // namespace

StateNode NextNmiReport(EmulatorContext* context, const NextIntJournalQuery& query)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextBoard& board = decoder->Board();
    NextMultiface& multiface = decoder->Multiface();
    NextDivMmc& divmmc = decoder->DivMmc();
    NextInterruptSource& irq = decoder->Interrupts();
    StateNode n = IntJournalState(decoder->IntJournal());

    // The NMI sources: what can raise /NMI, whether it would now, and why not
    StateNode sources = StateNode::Array();
    auto button = [&](const char* name, const char* via, bool enabled, const char* refusal) {
        StateNode s = StateNode::Object();
        s["name"] = name;
        s["via"] = via;
        s["modelled"] = true;
        s["enabled"] = enabled;
        s["would_fire"] = refusal == nullptr;
        if (refusal)
            s["blocked_by"] = refusal;
        sources.push(std::move(s));
    };
    button("m1_button", "NR #02 bit 3 write (the M1 key); needs NR #06 bit 3", (board.Stored(0x06) & 0x08) != 0, decoder->MultifaceNmiRefusal());
    button("drive_button", "NR #02 bit 2 write (the DRIVE key, the host's NMI action); needs NR #06 bit 4", (board.Stored(0x06) & 0x10) != 0,
           decoder->DriveNmiRefusal());
    {
        StateNode trap = StateNode::Object();
        trap["name"] = "io_trap";
        trap["via"] = "+3 FDC ports #2FFD read, #3FFD read / write while NR #D8 bit 0 is on; the M1 NMI path (needs NR #06 bit 3)";
        trap["modelled"] = true;
        const bool enabled = (board.Stored(0xD8) & 0x01) != 0;
        trap["enabled"] = enabled;
        const char* refusal = decoder->MultifaceNmiRefusal();
        trap["would_fire"] = enabled && refusal == nullptr;
        if (!enabled)
            trap["blocked_by"] = "the trap is off (NR #D8 bit 0)";
        else if (refusal)
            trap["blocked_by"] = refusal;
        const uint8_t cause = board.Stored(0xDA) & 3;
        trap["cause"] = cause == 0 ? "none" : (cause == 1 ? "read_2ffd" : (cause == 2 ? "read_3ffd" : "write_3ffd"));
        trap["cause_nr_da"] = int(cause);
        trap["write_nr_d9"] = Hex8(board.Stored(0xD9));
        sources.push(std::move(trap));
    }
    n["sources"] = std::move(sources);

    StateNode service = StateNode::Object();
    service["any"] = multiface.NmiHold() || divmmc.NmiHold();
    service["multiface_session"] = multiface.NmiHold();
    service["divmmc_handler"] = divmmc.NmiHold();
    service["divmmc_button_pending"] = divmmc.ButtonPending();
    n["in_service"] = std::move(service);

    StateNode mf = StateNode::Object();
    const unsigned type = multiface.Type();
    mf["enabled"] = multiface.Enabled();
    mf["type"] = int(type);
    mf["type_name"] = type == 0 ? "+3" : (type == 1 ? "128" : (type == 2 ? "48K" : "48K, always visible"));
    mf["enable_port"] = Hex8(multiface.EnablePort());
    mf["disable_port"] = Hex8(multiface.DisablePort());
    mf["memory_in"] = multiface.MemoryIn();
    mf["nmi_active"] = multiface.NmiHold();
    mf["invisible"] = multiface.Invisible();
    mf["active"] = multiface.IsActive();
    n["multiface"] = std::move(mf);

    StateNode nr02 = StateNode::Object();
    nr02["value"] = Hex8(board.Read(NextBoard::kRegResetType));
    nr02["bus_reset"] = (board.Read(NextBoard::kRegResetType) & 0x80) != 0;
    nr02["m1_nmi_generated"] = (board.Read(NextBoard::kRegResetType) & 0x08) != 0;
    nr02["drive_nmi_generated"] = (board.Read(NextBoard::kRegResetType) & 0x04) != 0;
    n["nr_02"] = std::move(nr02);

    StateNode stackless = StateNode::Object();
    stackless["enabled"] = irq.StacklessNmi();
    stackless["return_address"] = Hex16(irq.LoadNmiReturn());
    n["stackless_nmi"] = std::move(stackless);

    StateNode reset = StateNode::Object();
    reset["bits"] = int(board.ResetBits());
    reset["last"] = ResetBitsName(board.ResetBits());
    n["reset"] = std::move(reset);

    StateNode ints = StateNode::Object();
    ints["mode"] = irq.HardwareMode() ? "hardware im2" : "pulse";
    ints["pending"] = NamesNode(irq.PendingMask());
    ints["in_service"] = NamesNode(irq.InServiceMask());
    uint8_t control = 0;
    if (irq.ReadNr(NextInterruptSource::kRegControl, control))
        ints["vector_base"] = Hex8(control & 0xE0);
    n["interrupts"] = std::move(ints);

    StateNode events = StateNode::Array();
    for (const NextIntEvent& e : decoder->IntJournal().Query(query))
    {
        StateNode ev = StateNode::Object();
        ev["seq"] = e.seq;
        ev["frame"] = int(e.frame);
        ev["t"] = int(e.t);
        ev["pc"] = Hex16(e.pc);
        ev["kind"] = NextIntKindName(e.kind);
        ev["source"] = e.source;
        if (e.kind == NextIntKind::IntAck)
        {
            if (e.hasVector)
                ev["vector"] = Hex8(e.vector);
        }
        else if (e.kind == NextIntKind::NmiRequest)
        {
            ev["accepted"] = e.accepted;
            if (!e.accepted)
                ev["reason"] = e.reason;
        }
        else if (e.kind == NextIntKind::Reset)
        {
            ev["reset_bits"] = int(e.value);
            ev["reset_last"] = ResetBitsName(e.value);
        }
        events.push(std::move(ev));
    }
    n["events"] = std::move(events);
    return n;
}

StateNode NextNmiJournalControl(EmulatorContext* context, int enable, bool clear, size_t capacity)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextIntJournal& journal = decoder->IntJournal();
    if (capacity > 0)
        journal.SetCapacity(capacity);
    if (clear)
        journal.Clear();
    if (enable >= 0)
        journal.SetEnabled(enable != 0);
    return IntJournalState(journal);
}

namespace
{
StateNode MagicPortState(const NextMagicPort& magic)
{
    StateNode n = StateNode::Object();
    n["available"] = true;
    n["enabled"] = magic.Enabled();
    n["port"] = StringHelper::Format("0x%04X", magic.Port());
    n["mode"] = NextMagicPortModeName(magic.Mode());
    n["bytes"] = magic.Bytes();
    n["size"] = static_cast<uint64_t>(magic.Size());
    n["evicted"] = magic.Evicted();
    n["last_seq"] = magic.LastSeq();
    return n;
}
}  // namespace

StateNode NextMagicPortReport(EmulatorContext* context, uint64_t since, size_t limit)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    const NextMagicPort& magic = decoder->MagicPort();
    StateNode n = MagicPortState(magic);
    if (magic.Mode() == NextMagicPortMode::Line)
        n["partial"] = magic.Partial();
    StateNode items = StateNode::Array();
    for (const NextMagicPortItem& item : magic.Query(since, limit))
    {
        StateNode e = StateNode::Object();
        e["seq"] = item.seq;
        e["frame"] = static_cast<uint64_t>(item.frame);
        e["t"] = static_cast<uint64_t>(item.t);
        e["pc"] = StringHelper::Format("0x%04X", item.pc);
        e["text"] = item.text;
        items.push(std::move(e));
    }
    n["items"] = std::move(items);
    return n;
}

StateNode NextMagicPortControl(EmulatorContext* context, int enable, int port, const std::string& mode, bool clear)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextMagicPort& magic = decoder->MagicPort();
    NextMagicPortMode chosen = magic.Mode();
    if (!mode.empty() && !NextMagicPortModeFromString(mode, chosen))
    {
        StateNode n = StateNode::Object();
        n["available"] = true;
        n["error"] = "mode: hex, dec, ascii or line";
        return n;
    }
    if (clear)
        magic.Clear();
    magic.Configure(enable >= 0 ? enable != 0 : magic.Enabled(), port >= 0 ? static_cast<uint16_t>(port) : magic.Port(), chosen);
    return MagicPortState(magic);
}

namespace
{
/// A pan nibble as the program sent it with #FFFD (bit 1 left, bit 0 right)
const char* PanName(uint8_t pan)
{
    switch (pan & 3)
    {
        case 3: return "stereo";
        case 2: return "left";
        case 1: return "right";
        default: return "muted";
    }
}

/// NR #05 joystick mode codes (zxnext.vhd nr_05_joy0 / joy1 = bit 3 or 1 & bits 7:6 or 5:4): which port the mode answers
struct JoystickMode
{
    const char* name;
    const char* port;
};
const JoystickMode kJoystickModes[8] = {
    {"sinclair2", "keyboard"}, {"kempston1", "1F"}, {"cursor", "keyboard"}, {"sinclair1", "keyboard"},
    {"kempston2", "37"},       {"md1", "1F"},       {"md2", "37"},          {"io_mode", "user port"}};
}  // namespace

StateNode NextAudioReport(EmulatorContext* context)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextBoard& board = decoder->Board();
    NextAudio& audio = decoder->Audio();
    StateNode n = StateNode::Object();
    n["available"] = true;

    StateNode config = StateNode::Object();
    config["nr_06"] = Hex8(board.Stored(0x06));
    config["nr_08"] = Hex8(board.Stored(0x08));
    config["nr_09"] = Hex8(board.Stored(0x09));
    config["chip_model"] = audio.YmMode() ? "ym2149" : "ay8910";
    config["stereo_mode"] = audio.Acb() ? "acb" : "abc";
    config["turbosound"] = audio.TurboSound();
    config["dac"] = audio.DacEnabled();
    config["beeper_excluded"] = audio.SpeakerExcluded();
    n["config"] = std::move(config);
    n["selected_chip"] = int(audio.SelectedChip());

    StateNode chips = StateNode::Array();
    for (unsigned i = 0; i < NextAudio::kChips; i++)
    {
        SoundChip_AY8910* chip = audio.Chip(i);
        StateNode c = StateNode::Object();
        c["index"] = int(i);
        c["selected"] = i == audio.SelectedChip();
        c["pan"] = PanName(audio.PanMask(i));
        c["mono"] = ((audio.MonoMask() >> i) & 1) != 0;
        if (chip)
        {
            StateNode regs = StateNode::Array();
            uint8_t r[16];
            for (uint8_t reg = 0; reg < 16; reg++)
            {
                r[reg] = chip->readRegister(reg);
                regs.push(StateNode(Hex8(r[reg])));
            }
            c["registers"] = std::move(regs);
            StateNode tones = StateNode::Array();
            for (unsigned ch = 0; ch < 3; ch++)
                tones.push(StateNode(int(r[ch * 2] | ((r[ch * 2 + 1] & 0x0F) << 8))));
            c["tone_periods"] = std::move(tones);
            c["noise_period"] = int(r[6] & 0x1F);
            c["mixer"] = Hex8(r[7]);
            StateNode volumes = StateNode::Array();
            for (unsigned ch = 0; ch < 3; ch++)
                volumes.push(StateNode(int(r[8 + ch] & 0x1F)));  // bit 4: the envelope drives the channel
            c["volumes"] = std::move(volumes);
            c["envelope_period"] = int(r[11] | (r[12] << 8));
            c["envelope_shape"] = int(r[13] & 0x0F);
        }
        chips.push(std::move(c));
    }
    n["chips"] = std::move(chips);

    StateNode dac = StateNode::Object();
    dac["enabled"] = audio.DacEnabled();
    dac["a"] = Hex8(audio.Dac(0));
    dac["b"] = Hex8(audio.Dac(1));
    dac["c"] = Hex8(audio.Dac(2));
    dac["d"] = Hex8(audio.Dac(3));
    dac["left_sum"] = int(audio.Dac(0)) + int(audio.Dac(1));   // left = A + B
    dac["right_sum"] = int(audio.Dac(2)) + int(audio.Dac(3));  // right = C + D
    dac["silence"] = Hex8(0x80);
    n["dac"] = std::move(dac);
    return n;
}

StateNode NextUartReport(EmulatorContext* context)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    const NextUart& uart = decoder->Uart();
    NextBoard& board = decoder->Board();
    constexpr unsigned kPreview = 16;
    StateNode n = StateNode::Object();
    n["available"] = true;
    n["selected"] = int(uart.Selected());
    n["selected_name"] = uart.Selected() ? "pi" : "esp";

    StateNode ports = StateNode::Object();
    ports["tx_status"] = "#133B";
    ports["rx_prescaler"] = "#143B";
    ports["select"] = "#153B";
    ports["frame"] = "#163B";
    ports["decode"] = "A15:A11 = 00010, A10 xor (A9 and A8) = 1, A7:A0 = #3B";
    ports["enabled"] = decoder->PortEnabledForReport(12);
    ports["enabled_by"] = "NR #83 bit 4 (internal port enable word bit 12)";
    n["ports"] = std::move(ports);

    const uint8_t enable = decoder->Interrupts().UartEnables();
    const uint16_t pending = decoder->Interrupts().PendingMask();
    StateNode channels = StateNode::Array();
    for (unsigned i = 0; i < NextUart::kChannels; i++)
    {
        const NextUart::View v = uart.GetView(i);
        StateNode c = StateNode::Object();
        c["index"] = int(i);
        c["name"] = i ? "pi" : "esp";
        c["selected"] = v.selected;
        c["prescaler"] = int(v.prescaler);
        c["baud"] = int(v.baud);
        StateNode frame = StateNode::Object();
        frame["value"] = Hex8(v.frame);
        frame["data_bits"] = int(v.dataBits);
        frame["parity"] = std::string(1, v.parity);
        frame["stop_bits"] = int(v.stopBits);
        frame["hardware_flow"] = v.hardwareFlow;
        frame["break"] = v.breakOut;
        frame["fifo_reset"] = (v.frame & NextUart::kFrameReset) != 0;
        frame["clocks_per_byte"] = static_cast<int64_t>(uart.FrameClocks(i));
        c["frame"] = std::move(frame);
        c["rts"] = v.rtsAsserted;

        StateNode rx = StateNode::Object();
        rx["count"] = int(v.rxCount);
        rx["capacity"] = int(NextUart::kRxFifo);
        rx["near_full"] = v.rxCount >= NextUart::kNearFull;
        rx["busy"] = v.rxBusy;
        uint16_t bytes[kPreview];
        const unsigned shown = uart.PeekRx(i, bytes, kPreview);
        std::string hex;
        bool anyError = false;
        for (unsigned b = 0; b < shown; b++)
        {
            hex += StringHelper::Format("%02X", bytes[b] & 0xFF);
            anyError = anyError || (bytes[b] & 0x100);
        }
        rx["preview_hex"] = hex;
        rx["preview_has_error"] = anyError;
        c["rx"] = std::move(rx);

        StateNode tx = StateNode::Object();
        tx["count"] = int(v.txCount);
        tx["capacity"] = int(NextUart::kTxFifo);
        tx["busy"] = v.txBusy;
        tx["empty"] = (v.status & NextUart::kStatusTxEmpty) != 0;
        uint8_t out[kPreview];
        const unsigned shownTx = uart.PeekTx(i, out, kPreview);
        std::string txHex;
        for (unsigned b = 0; b < shownTx; b++)
            txHex += StringHelper::Format("%02X", out[b]);
        tx["preview_hex"] = txHex;
        c["tx"] = std::move(tx);

        StateNode status = StateNode::Object();
        status["value"] = Hex8(v.status);
        status["rx_available"] = (v.status & NextUart::kStatusRxAvail) != 0;
        status["tx_full"] = (v.status & NextUart::kStatusTxFull) != 0;
        status["overflow"] = (v.status & NextUart::kStatusOverflow) != 0;
        status["rx_near_full"] = (v.status & NextUart::kStatusNearFull) != 0;
        status["tx_empty"] = (v.status & NextUart::kStatusTxEmpty) != 0;
        status["byte_error"] = (v.status & NextUart::kStatusByteError) != 0;
        status["framing_error"] = (v.status & NextUart::kStatusFraming) != 0;
        status["break"] = (v.status & NextUart::kStatusBreak) != 0;
        status["rx_line_low"] = v.rxLineLow;
        status["note"] = "the sticky bits (overflow, framing) are cleared by a read of #133B; this report does not clear them";
        c["status"] = std::move(status);

        StateNode counters = StateNode::Object();
        counters["sent"] = static_cast<int64_t>(v.sent);
        counters["received"] = static_cast<int64_t>(v.received);
        counters["lost"] = static_cast<int64_t>(v.lost);
        c["bytes"] = std::move(counters);

        StateNode irq = StateNode::Object();
        const uint8_t bits = static_cast<uint8_t>((enable >> (i ? 4 : 0)) & 7);
        irq["nr_c6_bits"] = Hex8(bits);
        irq["rx_enabled"] = (bits & 3) != 0;
        irq["rx_on_near_full_only"] = (bits & 2) != 0;
        irq["tx_enabled"] = (bits & 4) != 0;
        irq["rx_pending"] = ((pending >> (i ? NextInterruptSource::kUart1Rx : NextInterruptSource::kUart0Rx)) & 1) != 0;
        irq["tx_pending"] = ((pending >> (i ? NextInterruptSource::kUart1Tx : NextInterruptSource::kUart0Tx)) & 1) != 0;
        irq["vector_rx"] = int((i ? NextInterruptSource::kUart1Rx : NextInterruptSource::kUart0Rx) * 2);
        irq["vector_tx"] = int((i ? NextInterruptSource::kUart1Tx : NextInterruptSource::kUart0Tx) * 2);
        c["interrupts"] = std::move(irq);

        StateNode peer = StateNode::Object();
        // the device the line goes to now: the joystick connector's while NR #0B has taken the UART
        const ISerialPeer* p = v.joystick ? uart.Wire(i) : uart.Peer(i);
        peer["attached"] = p != nullptr;
        peer["via_joystick_connector"] = v.joystick;
        if (v.joystick)
        {
            peer["module_cut_off"] = i ? "the Pi" : "the ESP";
            peer["note"] = p ? "the joystick connector's device (ComPortJoy=); the module of this UART sees no data" :
                               "nothing on the joystick connector (ComPortJoy=): bytes sent are lost, nothing arrives";
        }
        if (p)
        {
            peer["kind"] = p->Kind();
            peer["target"] = p->Target();
            peer["connected"] = p->Connected();
            peer["pending_bytes"] = int(p->Pending());
        }
        else if (!v.joystick)
            peer["note"] = i ? "nothing on the Pi's line (ComPort2=): bytes sent are lost, nothing arrives"
                             : "nothing on the ESP's line (ComPort=): bytes sent are lost, nothing arrives";
        c["peer"] = std::move(peer);
        channels.push(std::move(c));
    }
    n["channels"] = std::move(channels);

    StateNode extra = StateNode::Object();
    extra["nr_a0"] = Hex8(board.Read(0xA0));
    extra["pi_uart_on_gpio"] = (board.Stored(0xA0) & 0x10) != 0;
    extra["pi_uart_rx_tx_crossed"] = (board.Stored(0xA0) & 0x20) != 0;
    extra["nr_0b"] = Hex8(board.Stored(0x0B));
    extra["uart_on_joystick_port"] = (board.Stored(0x0B) & 0x80) && (board.Stored(0x0B) & 0x20);
    extra["joystick_port_uart"] = uart.JoystickRouted() ? int(uart.JoystickChannel()) : -1;
    extra["joystick_port_peer"] = uart.JoystickPeer() ? uart.JoystickPeer()->Kind() : "none";
    extra["note"] = "NR #0B bits 7 and 5 take the UART bit 0 selects to the joystick connector (its device: ComPortJoy=); the Pi's GPIO routing "
                    "(NR #A0) is a register only: the Pi's peer is on UART 1 whatever it says";
    n["routing"] = std::move(extra);
    return n;
}

StateNode NextInputReport(EmulatorContext* context)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextBoard& board = decoder->Board();
    const uint8_t nr05 = board.Stored(0x05), nr0b = board.Stored(0x0B);
    StateNode n = StateNode::Object();
    n["available"] = true;

    StateNode modes = StateNode::Object();
    modes["nr_05"] = Hex8(nr05);
    const unsigned left = (((nr05 >> 3) & 1) << 2) | ((nr05 >> 6) & 3);
    const unsigned right = (((nr05 >> 1) & 1) << 2) | ((nr05 >> 4) & 3);
    auto side = [&](unsigned code) {
        StateNode s = StateNode::Object();
        s["code"] = int(code);
        s["name"] = kJoystickModes[code].name;
        s["port"] = kJoystickModes[code].port;
        return s;
    };
    modes["left"] = side(left);
    modes["right"] = side(right);
    n["joystick_modes"] = std::move(modes);

    StateNode io = StateNode::Object();
    io["nr_0b"] = Hex8(nr0b);
    io["enabled"] = (nr0b & 0x80) != 0;
    io["mode"] = (nr0b & 0x30) == 0x00 ? "pin7 = bit 0" : ((nr0b & 0x30) == 0x10 ? "pin7 toggled by CTC channel 3" : "pin7 = UART TX");
    io["bit_0"] = (nr0b & 0x01) != 0;  // pin 7 level, or which UART: 0 the ESP's, 1 the Pi's
    io["pin7"] = decoder->JoystickPin7();  // the level on the connector now (mode 01: toggled by CTC channel 3's ZC/TO)
    io["uart_on_joystick_port"] = (nr0b & 0x80) && (nr0b & 0x20);
    n["io_mode"] = std::move(io);

    // zxnext.vhd: port #1F answers when either side is Kempston 1 or an MD pad on #1F (and NR #82 bit 6 lets it), #37 for Kempston 2 / MD 2
    uint8_t value = decoder->KempstonJoystickNow();
    uint8_t answered = 0;
    const bool rtlAnswers = decoder->JoystickPortAnswers(false, answered);
    StateNode k = StateNode::Object();
    k["value"] = Hex8(value);
    StateNode bits = StateNode::Object();
    bits["right"] = (value & 0x01) != 0;
    bits["left"] = (value & 0x02) != 0;
    bits["down"] = (value & 0x04) != 0;
    bits["up"] = (value & 0x08) != 0;
    bits["fire"] = (value & 0x10) != 0;
    k["bits"] = std::move(bits);
    k["selected_by_nr_05"] = rtlAnswers;
    k["answers_here"] = rtlAnswers;
    k["note"] = rtlAnswers ? "" : "NR #05 selects no Kempston 1 / MD pad on #1F (or NR #82 bit 6 is off): the board does not drive the port, a read gives #FF";
    n["kempston_1f"] = std::move(k);

    uint8_t value37 = 0;
    const bool answers37 = decoder->JoystickPortAnswers(true, value37);
    StateNode k37 = StateNode::Object();
    k37["value"] = Hex8(value37);
    k37["answers_here"] = answers37;
    k37["note"] = answers37 ? "" : "NR #05 selects no Kempston 2 / MD pad 2 on #37 (or NR #82 bit 7 is off): a read gives #FF";
    n["kempston_37"] = std::move(k37);

    // NR #B0 / #B1: the keys the ZX matrix has no place for; NR #B2: the pads' MODE X Y Z (zxnext.vhd 6155-6167)
    const NextExtendedInput& input = decoder->ExtendedInput();
    StateNode keys = StateNode::Object();
    keys["nr_b0"] = Hex8(board.Read(0xB0));
    keys["nr_b1"] = Hex8(board.Read(0xB1));
    StateNode pressed = StateNode::Array();
    for (unsigned bit = 0; bit < NextExtendedInput::kKeyCount; bit++)
        if (input.Keys() & (1u << bit))
            pressed.push(NextExtendedInput::KeyName(bit));
    keys["pressed"] = std::move(pressed);
    n["extended_keys"] = std::move(keys);

    auto pad = [&](bool leftSide) {
        const uint8_t buttons = input.Pad(leftSide);
        StateNode p = StateNode::Object();
        p["mode"] = (buttons & NextExtendedInput::kPadMode) != 0;
        p["x"] = (buttons & NextExtendedInput::kPadX) != 0;
        p["y"] = (buttons & NextExtendedInput::kPadY) != 0;
        p["z"] = (buttons & NextExtendedInput::kPadZ) != 0;
        return p;
    };
    StateNode md = StateNode::Object();
    md["nr_b2"] = Hex8(board.Read(0xB2));
    md["left"] = pad(true);
    md["right"] = pad(false);
    // A, C and START are bits 6, 5 and 7 of the Kempston byte (an MD pad mode passes them); the host's pad is the left connector
    md["host_keys"] = std::string("x:") + pckey::Name(input.PadKey(3)) + ",y:" + pckey::Name(input.PadKey(1)) + ",z:" +
                      pckey::Name(input.PadKey(2)) + ",mode:" + pckey::Name(input.PadKey(0));
    n["md_pad_extra"] = std::move(md);

    // The right connector: every button (the left one's Kempston byte is the joystick device); a tool puts them there
    StateNode rightPad = StateNode::Object();
    const uint16_t rightState = input.Right();
    rightPad["state"] = Hex8(static_cast<uint8_t>(rightState & 0xFF));
    rightPad["extras"] = Hex8(static_cast<uint8_t>(rightState >> 8));
    StateNode rightButtons = StateNode::Array();
    constexpr const char* kPadNames[12] = {"right", "left", "down", "up", "fire", "c", "a", "start", "y", "z", "x", "mode"};
    for (unsigned bit = 0; bit < 12; bit++)
        if (rightState & (1u << bit))
            rightButtons.push(kPadNames[bit]);
    rightPad["buttons"] = std::move(rightButtons);
    n["right_pad"] = std::move(rightPad);
    return n;
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

    // The board's small registers (G8): the expansion bus port enables NR #86-#89 (the internal ones are NR #82-#85 in `ports`), the debug
    // LED NR #FF, and the +3 FDC I/O trap NR #D8-#DA (the enable, the written byte and the cause)
    StateNode boardBlock = StateNode::Object();
    const uint32_t busWord = static_cast<uint32_t>(board.Stored(0x86)) | (static_cast<uint32_t>(board.Stored(0x87)) << 8) |
                             (static_cast<uint32_t>(board.Stored(0x88)) << 16) | (static_cast<uint32_t>(board.Stored(0x89)) << 24);
    StateNode bus = StateNode::Object();
    for (uint8_t reg = 0x86; reg <= 0x89; reg++)
        bus[StringHelper::Format("nr_%02x", reg)] = Hex8(board.Stored(reg));
    bus["restored_by"] = (busWord & 0x80000000u) ? "soft reset" : "hard reset";
    boardBlock["bus_port_enable"] = std::move(bus);
    boardBlock["debug_led"] = Hex8(board.Stored(0xFF));
    StateNode trap = StateNode::Object();
    trap["modelled"] = true;
    trap["enabled"] = (board.Stored(0xD8) & 0x01) != 0;
    trap["nr_d8"] = Hex8(board.Stored(0xD8));
    trap["nr_d9"] = Hex8(board.Stored(0xD9));
    trap["nr_da"] = Hex8(board.Stored(0xDA));
    boardBlock["io_trap"] = std::move(trap);
    n["board"] = std::move(boardBlock);

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
