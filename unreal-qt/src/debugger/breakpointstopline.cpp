// The debugger's stop line: the breakpoint a machine stopped at, in words - the access, the condition that held,
// the hit count - with links to edit it ("edit") and to the list ("list")

#include "breakpointstopline.h"

#include <QStringList>

namespace
{
/// An event kind in words: who, what it does (now / in a stop), the thing with any value and with one ("%1" the value;
/// empty: the thing followed by ", value %1")
struct EventWords
{
    const char* present;
    const char* past;
    const char* anyValue;
    const char* oneValue;
};

EventWords WordsOf(GSCardEvent kind)
{
    switch (kind)
    {
        case GSCardEvent::Int: return {"the card accepts", "the card accepted", "an interrupt (INT)", ""};
        case GSCardEvent::Nmi: return {"the card accepts", "the card accepted", "an NMI", ""};
        case GSCardEvent::Reset: return {"the card is", "the card was", "reset", ""};
        case GSCardEvent::PageSwitch: return {"the card switches", "the card switched", "its memory page", "to page %1"};
        case GSCardEvent::CardCommandRead: return {"the card takes", "the card took", "a command", "command %1"};
        case GSCardEvent::CardDataRead: return {"the card takes", "the card took", "a data byte", "data byte %1"};
        case GSCardEvent::CardReplyWrite: return {"the card writes", "the card wrote", "its reply", "the reply %1"};
        case GSCardEvent::HostCommandWrite: return {"the host sends", "the host sent", "a command", "command %1"};
        case GSCardEvent::HostDataWrite: return {"the host sends", "the host sent", "a data byte", "data byte %1"};
        case GSCardEvent::HostReplyRead: return {"the host reads", "the host read", "the card's reply", "the reply %1"};
        case GSCardEvent::DacFetch: return {"the card feeds", "the card fed", "a DAC", "a DAC the byte %1"};
        case GSCardEvent::ClockChange: return {"the card changes", "the card changed", "its CPU clock", ""};
        case GSCardEvent::DmaStart: return {"the card starts", "the card started", "a DMA", ""};
        case GSCardEvent::DmaDone: return {"the card ends", "the card ended", "a DMA", ""};
        case GSCardEvent::ConfigWrite: return {"the card writes", "the card wrote", "GSCFG0", "GSCFG0 %1"};
        case GSCardEvent::CpuInt: return {"the CPU accepts", "the CPU accepted", "a maskable interrupt", "the interrupt of source %1"};
        case GSCardEvent::CpuReti: return {"RETI ends", "RETI ended", "an interrupt source", "interrupt source %1"};
        case GSCardEvent::CpuNmi: return {"the machine asks for", "the machine asked for", "an NMI", "an NMI of source %1"};
        case GSCardEvent::CpuRetn: return {"RETN ends", "RETN ended", "an NMI session", ""};
        case GSCardEvent::MachineReset: return {"the machine is", "the machine was", "reset", "reset (%1: 0 soft, 1 hard)"};
        case GSCardEvent::DivMmcMap: return {"the DivMMC maps", "the DivMMC mapped", "in", "in, entry %1"};
        case GSCardEvent::DivMmcUnmap: return {"the DivMMC maps", "the DivMMC mapped", "out", "out, reason %1"};
        case GSCardEvent::EsxDosCall: return {"a program calls", "a program called", "esxDOS", "esxDOS call %1"};
        case GSCardEvent::ZxnDmaStart: return {"the zxnDMA starts", "the zxnDMA started", "a transfer", ""};
        case GSCardEvent::ZxnDmaDone: return {"the zxnDMA stops", "the zxnDMA stopped", "a transfer", ""};
        case GSCardEvent::TsDmaStart: return {"the TS-Conf DMA starts", "the TS-Conf DMA started", "a transfer", ""};
        case GSCardEvent::TsDmaDone: return {"the TS-Conf DMA stops", "the TS-Conf DMA stopped", "a transfer", ""};
        case GSCardEvent::PldRead: return {"the PLD decodes", "the PLD decoded", "a port read", "a port read, code %1"};
        case GSCardEvent::PldWrite: return {"the PLD decodes", "the PLD decoded", "a port write", "a port write, code %1"};
        case GSCardEvent::PldEvent: return {"the PLD logs", "the PLD logged", "an event", "event %1"};
        case GSCardEvent::FdcCommand: return {"the disk controller takes", "the disk controller took", "a command", "command %1"};
        case GSCardEvent::FdcDone: return {"the disk controller ends", "the disk controller ended", "a command", "a command, status %1"};
        case GSCardEvent::IntAccept: return {"the CPU accepts", "the CPU accepted", "an INT or NMI", ""};
        case GSCardEvent::IntReturn: return {"the CPU returns", "the CPU returned", "from an interrupt", ""};
        case GSCardEvent::Count: break;
    }
    return {"event", "event", "unknown", ""};
}
}  // namespace

QString EventBreakpointWords(GSCardEvent kind, uint8_t value, uint8_t mask, bool past)
{
    const EventWords words = WordsOf(kind);
    const auto hex2 = [](unsigned byte) { return QString("#%1").arg(byte, 2, 16, QChar('0')).toUpper(); };
    QString thing = QString::fromLatin1(words.anyValue);
    if (mask != 0)
    {
        // One value, or a value under a mask
        const QString shown = mask == 0xFF ? hex2(value) : hex2(value & mask) + " & " + hex2(mask);
        thing = words.oneValue[0] != '\0' ? QString::fromLatin1(words.oneValue).arg(shown) : thing + ", value " + shown;
    }
    return QString::fromLatin1(past ? words.past : words.present) + " " + thing;
}

QString EventBreakpointGroup(GSCardEvent kind)
{
    switch (kind)
    {
        case GSCardEvent::CardCommandRead:
        case GSCardEvent::CardDataRead:
        case GSCardEvent::CardReplyWrite:
            return "Protocol, card side";
        case GSCardEvent::HostCommandWrite:
        case GSCardEvent::HostDataWrite:
        case GSCardEvent::HostReplyRead:
            return "Protocol, host side";
        default:
            return GSCardEventIsHostSide(kind) ? "Machine" : "Card";
    }
}

QString BreakpointStopLine(const BreakpointDescriptor& breakpoint, const BreakpointHit& hit)
{
    const auto hex4 = [](unsigned value) { return QString("$%1").arg(value, 4, 16, QChar('0')).toUpper(); };
    const auto hex2 = [](unsigned value) { return QString("$%1").arg(value, 2, 16, QChar('0')).toUpper(); };

    // What happened, in the access's words
    QString what;
    switch (hit.kind)
    {
        case BreakpointHitKind::Execute: what = "execution at " + hex4(hit.address); break;
        case BreakpointHitKind::MemoryRead: what = "read of " + hex4(hit.address); break;
        case BreakpointHitKind::MemoryWrite: what = "write to " + hex4(hit.address); break;
        case BreakpointHitKind::PortIn: what = "IN from port " + hex4(hit.address); break;
        case BreakpointHitKind::PortOut: what = "OUT to port " + hex4(hit.address); break;
        case BreakpointHitKind::NextRegRead: what = "NextREG " + hex2(hit.address) + " read"; break;
        case BreakpointHitKind::NextRegWrite: what = "NextREG " + hex2(hit.address) + " write"; break;
        case BreakpointHitKind::Beam:
            what = QString("beam at line %1, pixel clock %2").arg(breakpoint.z80address).arg(breakpoint.beamHc);
            break;
        case BreakpointHitKind::CopperInstruction: what = QString("copper instruction %1").arg(hit.address); break;
        case BreakpointHitKind::Magic: what = "magic opcode " + hex4(hit.address).mid(1); break;
        case BreakpointHitKind::Keyboard: what = "keyboard read of port " + hex4(hit.address); break;
        case BreakpointHitKind::ChipRegister: what = "sound chip register " + hex2(hit.address) + " write"; break;
        case BreakpointHitKind::DmaWrite: what = "DMA write to " + hex4(hit.address); break;
        case BreakpointHitKind::DmaRead: what = "DMA read of " + hex4(hit.address); break;
        case BreakpointHitKind::Event:
            // The kind is the hit's address; the stop does not carry the byte, the breakpoint's filter tells it
            what = hit.address < static_cast<uint16_t>(GSCardEvent::Count)
                       ? EventBreakpointWords(static_cast<GSCardEvent>(hit.address), breakpoint.nrValue, breakpoint.nrMask,
                                              true)
                       : QString("event %1").arg(hit.address);
            break;
        default: what = QString::fromLatin1(BreakpointHitKindName(hit.kind)) + " " + hex4(hit.address); break;
    }
    // The breakpoint's own span when the hit is one point of a range
    if (breakpoint.isRange && (breakpoint.type == BRK_MEMORY || breakpoint.type == BRK_IO))
        what += QString(" (watching %1-%2)").arg(hex4(breakpoint.z80address), hex4(breakpoint.z80addressEnd));
    if (hit.cpu != "main")
        what = QString::fromStdString(hit.cpu) + " CPU: " + what +
               (hit.kind == BreakpointHitKind::Event ? " at " : " by the instruction at ") + hex4(hit.cpuPc);

    QStringList parts;
    parts << QString("<b>Stopped by breakpoint #%1</b>: %2").arg(breakpoint.breakpointID).arg(what.toHtmlEscaped());
    if (breakpoint.condition)
        parts << "when <code>" + QString::fromStdString(breakpoint.condition->Source()).toHtmlEscaped() + "</code>";
    QString hits = QString("hit %1").arg(breakpoint.hitCount);
    if (breakpoint.hitMode != BRK_HIT_ALWAYS)
        hits += " (stops on " + QString::fromStdString(BreakpointManager::HitSpecName(breakpoint)).toHtmlEscaped() + ")";
    parts << hits;
    if (!breakpoint.note.empty())
        parts << "<i>" + QString::fromStdString(breakpoint.note).toHtmlEscaped() + "</i>";
    parts << "<a href='edit'>Edit...</a>  <a href='list'>Breakpoints...</a>";
    return parts.join(" &nbsp;&middot;&nbsp; ");
}
