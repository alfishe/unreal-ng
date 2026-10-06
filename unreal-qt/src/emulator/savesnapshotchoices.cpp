#include "emulator/savesnapshotchoices.h"

#include <algorithm>

#include <QCoreApplication>
#include <QStringList>

namespace SaveSnapshotChoices
{
std::vector<Choice> Build(const snapshot::SaveFormats& formats)
{
    struct Row
    {
        snapshot::SaveFormat format;
        const char* extension;
        const char* filter;
        const char* tip;
    };
    const Row rows[] = {
        {snapshot::SaveFormat::Szx, "szx", "SZX Snapshots (*.szx)", "Save the machine to an SZX snapshot (the most state)"},
        {snapshot::SaveFormat::Z80, "z80", "Z80 Snapshots (*.z80)", "Save the machine to a Z80 v3 snapshot"},
        {snapshot::SaveFormat::Sna, "sna", "SNA Snapshots (*.sna)", "Save the machine to an SNA snapshot"},
    };
    std::vector<Choice> out;
    for (const Row& row : rows)
    {
        Choice choice;
        choice.format = row.format;
        choice.extension = row.extension;
        choice.filter = QCoreApplication::translate("SaveSnapshotChoices", row.filter);
        if (!formats.formats.empty())
        {
            const snapshot::FormatStatus& status = formats.For(row.format);
            choice.enabled = status.available;
            choice.tip = status.available ? QCoreApplication::translate("SaveSnapshotChoices", row.tip)
                                          : QString::fromStdString(status.reason);
        }
        else
            choice.tip = QCoreApplication::translate("SaveSnapshotChoices", "no machine");
        out.push_back(choice);
    }
    return out;
}

std::vector<Choice> Offered(const std::vector<Choice>& all, const QString& preferred)
{
    std::vector<Choice> offered;
    for (const Choice& choice : all)
    {
        if (choice.enabled)
            offered.push_back(choice);
    }
    const auto it = std::find_if(offered.begin(), offered.end(), [&](const Choice& c) { return c.extension == preferred; });
    if (it != offered.end())
        std::rotate(offered.begin(), it, it + 1);
    return offered;
}

QString Refusals(const std::vector<Choice>& all)
{
    QStringList lines;
    for (const Choice& choice : all)
    {
        if (!choice.enabled)
            lines << QString(".%1: %2").arg(choice.extension, choice.tip);
    }
    return lines.join("\n");
}
}  // namespace SaveSnapshotChoices
