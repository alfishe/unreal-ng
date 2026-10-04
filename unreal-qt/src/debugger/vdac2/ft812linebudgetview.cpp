#include "ft812linebudgetview.h"

#include <algorithm>
#include <sstream>

namespace Ft812LineBudget
{

LineClass Classify(uint32_t clocks, bool measured, uint32_t soft, uint32_t hard)
{
    if (!measured)
        return LineClass::NotMeasured;
    if (clocks > hard)
        return LineClass::OverHard;
    if (clocks > soft)
        return LineClass::OverSoft;
    return LineClass::Ok;
}

View Build(const Vdac2Control::FrameMetrics& m)
{
    View view;
    view.hardBudget = m.hardBudget;
    view.softBudget = m.softBudget;
    const uint32_t count = static_cast<uint32_t>(m.lineClocks.size());
    view.lines.resize(count);

    const uint32_t passed = m.inFlightKnown ? std::min<uint32_t>(m.inFlightLinesPassed, count) : 0;
    view.composite = passed > 0 && passed < count && m.inFlightLineClocks.size() >= passed;
    view.boundary = view.composite ? passed : count;

    for (uint32_t i = 0; i < count; ++i)
    {
        Line& line = view.lines[i];
        if (view.composite && i < passed)
        {
            const int32_t clocks = m.inFlightLineClocks[i];
            line.clocks = clocks < 0 ? 0 : static_cast<uint32_t>(clocks);
            line.cls = Classify(line.clocks, clocks >= 0, m.softBudget, m.hardBudget);
        }
        else
        {
            line.clocks = m.lineClocks[i];
            line.cls = Classify(line.clocks, m.valid, m.softBudget, m.hardBudget);
            line.previousFrame = view.composite;
        }
        if (line.cls == LineClass::OverHard)
            ++view.overHard;
        else if (line.cls == LineClass::OverSoft)
            ++view.overSoft;
        if (line.cls != LineClass::NotMeasured && line.clocks > view.worstClocks)
        {
            view.worstClocks = line.clocks;
            view.worstLine = i;
        }
    }
    // A quarter beyond the hard budget, or the worst line if that is further
    view.axisClocks = std::max<uint32_t>({view.hardBudget + view.hardBudget / 4, view.worstClocks, 1});
    return view;
}

std::string Summary(const Vdac2Control::FrameMetrics& m, const View& view)
{
    std::ostringstream out;
    out << "FT812 frame " << m.frame;
    if (!m.valid && !view.composite)
    {
        out << ": not measured (the frame was not drawn)";
        return out.str();
    }
    if (view.composite)
        out << " + " << view.boundary << " line(s) of the frame in flight";
    out << " | worst line " << view.worstLine << ": " << view.worstClocks << " / " << view.hardBudget << " clocks ("
        << (view.hardBudget ? view.worstClocks * 100 / view.hardBudget : 0) << " %) | over soft " << view.overSoft
        << ", over hard " << view.overHard << " | soft " << view.softBudget << " (margin " << m.margin << " %)";
    return out.str();
}

}  // namespace Ft812LineBudget
