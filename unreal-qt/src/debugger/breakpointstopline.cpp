// The debugger's stop line: the breakpoint a machine stopped at, in words - the access, the condition that held,
// the hit count - with links to edit it ("edit") and to the list ("list")

#include "breakpointstopline.h"

#include <QStringList>

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
        default: what = QString::fromLatin1(BreakpointHitKindName(hit.kind)) + " " + hex4(hit.address); break;
    }
    // The breakpoint's own span when the hit is one point of a range
    if (breakpoint.isRange && (breakpoint.type == BRK_MEMORY || breakpoint.type == BRK_IO))
        what += QString(" (watching %1-%2)").arg(hex4(breakpoint.z80address), hex4(breakpoint.z80addressEnd));
    if (hit.cpu != "main")
        what = QString::fromStdString(hit.cpu) + " CPU: " + what + " by the instruction at " + hex4(hit.cpuPc);

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
