#pragma once

/// @file savesnapshotchoices.h
/// @brief What the Save Snapshot menu and dialog offer (snapshot pipeline P6): the formats the running machine can be saved in
/// right now, with the reason for each it cannot. No widget in here: MenuManager and MainWindow render it, a test reads it.

#include <QString>
#include <vector>

#include "loaders/snapshot/snapshotcapture.h"

namespace SaveSnapshotChoices
{
struct Choice
{
    snapshot::SaveFormat format;
    QString extension;   ///< "szx", "z80", "sna"
    QString filter;      ///< the file dialog's filter text
    bool enabled = false;
    QString tip;         ///< what the format keeps, or why it is not offered
};

/// SZX first (it keeps the most state), then Z80, then SNA
std::vector<Choice> Build(const snapshot::SaveFormats& formats);

/// The enabled ones only, `preferred` ("szx" / "z80" / "sna" or empty) first
std::vector<Choice> Offered(const std::vector<Choice>& all, const QString& preferred);

/// One line per refused format: ".sna: the reason" (for a message box)
QString Refusals(const std::vector<Choice>& all);
}  // namespace SaveSnapshotChoices
