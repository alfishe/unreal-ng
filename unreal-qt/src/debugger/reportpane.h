#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class EmulatorContext;
class QLabel;
class QTableWidget;
class QTreeWidget;

/// @file reportpane.h
/// @brief A debugger pane that shows one device state report (DeviceState, the StateNode every automation plane renders) as the
/// reports are: its scalar fields and nested objects in a two-column tree, one array of rows (the NextREG file, the MMU slots, the
/// copper list, the sprites, the CTC channels) in a table under it. No device code here: a pane is a builder plus the array's key.
///
/// Refresh rules (debugger-model rules.md section 4): a stop refresh marks every value that differs from the last stop (bold, the
/// highlight colour); a live refresh (the machine running) shows the values without marks and leaves the last stop's values as the
/// reference. The pane never reads by itself: its owner calls Refresh with a context it holds a lease on.
class ReportPane : public QWidget
{
    Q_OBJECT

public:
    using Builder = std::function<StateNode(EmulatorContext*)>;
    /// A row of the table to tint (the NextREG differing from its reset value, the copper's current word, a visible sprite)
    using RowMark = std::function<bool(const StateNode& row)>;

    /// `tableKey`: the member holding the table's rows (empty: no table); `columns`: the row members shown, in order (empty: every
    /// member of the rows, in the order they first appear)
    ReportPane(Builder build, std::string tableKey, QStringList columns, QWidget* parent = nullptr);

    void setRowMark(RowMark mark) { _rowMark = std::move(mark); }

    /// Build the report on `context` and show it; `live`: the machine runs (no change marks)
    void refresh(EmulatorContext* context, bool live);
    /// Forget the last stop's values (another machine)
    void clearMarks() { _lastStop.clear(); }

    /// The text a value shows (and the automation text renders): on / off, decimal, the string; a nested value as `key=value, ...`
    static QString ValueText(const StateNode& node);

    /// region <For the tests>
    QTreeWidget* fields() const { return _fields; }
    QTableWidget* table() const { return _table; }
    QString status() const;
    /// Whether the value at `path` (the tree's `a.b` or the table's `row.column`) was marked changed by the last refresh
    bool isMarked(const QString& path) const { return _marked.count(path) != 0; }
    /// endregion

private:
    void fillFields(const StateNode& report, bool live, std::map<QString, QString>& now);
    void fillTable(const StateNode* rows, bool live, std::map<QString, QString>& now);
    bool changed(const QString& path, const QString& text, bool live);

    Builder _build;
    std::string _tableKey;
    QStringList _columns;
    RowMark _rowMark;
    QLabel* _status = nullptr;
    QTreeWidget* _fields = nullptr;
    QTableWidget* _table = nullptr;
    std::map<QString, QString> _lastStop;  ///< every value's text at the last stop refresh, by path
    std::map<QString, bool> _marked;
};
