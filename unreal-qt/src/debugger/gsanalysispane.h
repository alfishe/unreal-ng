#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "debugger/targets/debugcpuid.h"
#include "emulator/state/statenode.h"

class EmulatorContext;
class LabelManager;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QToolButton;

/// @file gsanalysispane.h
/// @brief The General Sound analysis tabs of the debugger whose data the core has (gs-debugger TODO P5.6,
/// gui-card-debugger.md A5.2a; requirements X7, AU6, M3, M4). One widget, four views, each a card side tab:
///   - Dispatch: the command table as the card holds it now (`gs-firmware` `dispatch`): command, name, handler, its
///     label (the profile's handler label, else the card CPU's labels), the ROM baseline; patched entries tinted with
///     the profile and the upload that patched them; above it the active profile stack ("gs105a ▸ nedoos-ngssd").
///   - Unknown: the commands the decoding profile does not name (`gs-firmware` `unknown_commands`, counted by the
///     command log while it records) with their counts since the tab's Clear and the handler the table holds.
///   - Objects: the firmware's structs by its profile (`gs-firmware` `instances`): one table per instance, columns from
///     the struct's fields, rows its instances; the profile's variables and the fixed RAM window's variables. A profile
///     picker reads another profile's layout on the card (default: the active one). It is the former "GS firmware" tab.
///   - Writers: the last writer of each card port (the port trace's card OUTs: value, card PC + label, frame) and of
///     the host ports #B3 / #BB / #33 (the command log: value, host PC + label, frame; #33 from the port trace).
/// No device code here: the reads are the report and history verbs every plane serves (GSFirmwareObjectsRead,
/// HistoryControl, the card's port trace selection). The pane never reads by itself: its owner calls refresh with a
/// context it holds a lease on (the card panes' live refresh while the tab is in front, once at a stop).
class GsAnalysisPane : public QWidget
{
    Q_OBJECT

public:
    enum class View
    {
        Dispatch,
        Unknown,
        Objects,
        Writers,
    };
    /// The rows Writers reads from the histories per refresh
    static constexpr size_t kTraceRows = 4096;
    static constexpr size_t kLogRows = 512;

    /// `follow`: the debugger CPU tab in front (the card it shows: CardPanes::Front); null: the GS slot's
    GsAnalysisPane(View view, std::shared_ptr<DebugCpuId> follow = {}, QWidget* parent = nullptr);

    View view() const { return _view; }
    /// Read the view's data on `context` and show it; `live`: the machine runs (no change marks)
    void refresh(EmulatorContext* context, bool live);
    /// Another card: no marks to compare with, the Unknown baseline starts again
    void reset();
    /// Unknown's Clear: the counts start again from the ones now (the command log keeps its own)
    void clearUnknown();
    /// Turn on what the view reads while it is off (Unknown: the command log; Writers: the command log and the port
    /// trace): history mode ring. False with `error` when a history refused
    bool turnOn(EmulatorContext* context, QString* error = nullptr);
    /// Objects: read the layout of profile `id` ("" the card's active one) / show object `name`
    void selectProfile(const QString& id);
    void selectObject(const QString& name);

    /// region <For the tests>
    QString status() const;
    QString headline() const;
    QTableWidget* table() const { return _table; }
    QToolButton* clearButton() const { return _clear; }
    QPushButton* turnOnButton() const { return _turnOn; }
    QComboBox* profiles() const { return _profiles; }
    QComboBox* objects() const { return _objects; }
    /// The text of row `row`'s cell under header `column`; empty when there is none
    QString cell(int row, const QString& column) const;
    /// The row whose cell under `column` reads `text`; -1 when none
    int findRow(const QString& column, const QString& text) const;
    bool isMarked(int row, int column) const;
    /// endregion

signals:
    void turnOnRequested();

private:
    void showUnavailable(const QString& why);
    void showBody(const QString& headline);
    void setColumns(const QStringList& columns);
    void setRowCount(int rows);
    void setCell(int row, int column, const QString& text, const QString& tooltip = QString());
    void tintRow(int row, const QColor& color);
    void finishMarks(bool live);

    void refreshDispatch(EmulatorContext* context, int target);
    void refreshUnknown(EmulatorContext* context, int target);
    void refreshObjects(EmulatorContext* context, int target);
    void refreshWriters(EmulatorContext* context, int target);

    View _view;
    std::shared_ptr<DebugCpuId> _follow;
    QLabel* _status = nullptr;
    QWidget* _body = nullptr;
    QLabel* _headline = nullptr;
    QWidget* _offBar = nullptr;
    QLabel* _offText = nullptr;
    QPushButton* _turnOn = nullptr;
    QComboBox* _profiles = nullptr;
    QComboBox* _objects = nullptr;
    QToolButton* _clear = nullptr;
    QTableWidget* _table = nullptr;

    std::string _card;                        ///< the card shown ("gs" / "ms"): another one starts afresh
    std::string _profile;                     ///< Objects: the profile picked ("" the active one)
    std::string _object;                      ///< Objects: the object shown
    std::map<int, int64_t> _unknown;          ///< Unknown: the log's count of each command
    std::map<int, int64_t> _unknownAtClear;   ///< ... at Clear
    std::map<QString, QString> _lastStop;     ///< every cell's text at the last stop refresh, by "row-key|column"
    std::map<QString, QString> _now;          ///< ... of this refresh
    std::map<std::pair<int, int>, bool> _marked;
    QString _scope;                           ///< the marks' scope: the object shown (another one: no marks)
};

/// The labels of a CPU for one refresh: the label at an address as the CPU's map shows it now (the card CPU's labels
/// are page-tied by the card's map); else, for a card, the firmware's label at that address in any page (a NeoGS runs
/// a RAM copy of its flash, a handler's page may not be mapped at the moment). The analysis tabs share it
class GsCardLabels
{
public:
    GsCardLabels(EmulatorContext* context, DebugCpuId cpu);
    QString at(uint16_t address);

private:
    LabelManager* _labels = nullptr;
    bool _anyPage = false;
    bool _built = false;
    std::map<uint16_t, std::string> _byAddress;
};
