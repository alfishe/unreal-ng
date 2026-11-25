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
struct HistoryRequest;
struct HistoryReply;
class QLabel;
class QPushButton;
class QTableWidget;
class QToolButton;

/// @file gsmonitorpane.h
/// @brief The General Sound protocol monitor (gs-debugger TODO P5.6, gui-card-debugger.md A5.3, C10): a card side tab of
/// the debugger ("GS monitor") watched next to the game while it plays (U1c). Top to bottom:
///   - the protocol strip: the `gs` report's protocol (the command log's decoder), the mailbox status, the last send;
///   - the error tile: the command log's anomalies and the commands the firmware profile does not name (`gs-firmware`
///     `unknown_commands`) counted since the tile's Clear, the last anomaly with its frame; tinted when non-zero;
///   - the log: the command log's records (`gs-commands`: frame, t, kind, command, name, text), anomalies tinted,
///     following the tail; scrolling up stops following, Follow (or scrolling back to the end) resumes. It fills with
///     the newest kReadRows, then reads after its cursor (the history's `since`): every record between two refreshes,
///     the ones gone from the history before they were read counted as missed;
///   - Pause / Continue: the debugger window's own actions (pauseRequested / continueRequested).
/// With the command log off it says so, with Turn on (turnOnRequested: the owner sets history `<slot>.commands` to ring
/// through turnOn under its lease). No device code here: the reads are the history and report verbs every plane serves.
/// The pane never reads by itself: its owner calls refresh with a context it holds a lease on (the card panes' live
/// refresh while the machine runs, once at a stop).
class GsMonitorPane : public QWidget
{
    Q_OBJECT

public:
    /// The records read per page (and the first fill), the pages per refresh at most, and the rows the log keeps
    static constexpr size_t kReadRows = 200;
    static constexpr int kPagesPerRefresh = 8;
    static constexpr int kMaxRows = 1000;

    /// `follow`: the debugger CPU tab in front (the card it shows: CardPanes::Front); null: the GS slot's
    explicit GsMonitorPane(std::shared_ptr<DebugCpuId> follow = {}, QWidget* parent = nullptr);

    /// Read the card's protocol, command log and unknown commands on `context` and show them; `live`: the machine runs
    /// (the button offers Pause, else Continue)
    void refresh(EmulatorContext* context, bool live);
    /// Turn the card's command log on (history mode ring); false with `error` when the history refused
    bool turnOn(EmulatorContext* context, QString* error = nullptr);
    /// The error tile's Clear: its counters start again (the log and the history stay)
    void clearCounters();
    /// Follow the log's tail (the Follow button)
    void setFollowing(bool on);

    /// The command log's history id of the card `follow` names on `context` ("zxbus.1.commands"); empty without a card
    static std::string HistoryDevice(EmulatorContext* context, DebugCpuId front);

    /// region <For the tests>
    QString status() const;
    QString stripText() const;
    QString tileText() const;
    bool tileTinted() const { return _tinted; }
    int anomalies() const { return _anomalies; }
    int64_t unknownCommands() const;
    bool following() const { return _following; }
    bool logOff() const { return _off; }
    int64_t missed() const { return _missed; }
    QString rowsText() const;
    QTableWidget* table() const { return _table; }
    QPushButton* turnOnButton() const { return _turnOn; }
    QPushButton* runButton() const { return _run; }
    QToolButton* clearButton() const { return _clear; }
    QToolButton* followButton() const { return _followButton; }
    /// endregion

signals:
    void turnOnRequested();
    void pauseRequested();
    void continueRequested();

private:
    void showUnavailable(const QString& why);
    void showStrip(const StateNode& gs, const StateNode* protocol);
    /// Appends `reply`'s page and the pages after it (up to kPagesPerRefresh), moving the cursor; `filling`: the first
    /// fill, whose start is not counted as missed
    void appendPages(EmulatorContext* context, HistoryRequest& read, HistoryReply reply, bool filling);
    void appendRow(const StateNode& record);
    void showTile();
    void scrollToTail();

    std::shared_ptr<DebugCpuId> _follow;
    QLabel* _status = nullptr;
    QWidget* _body = nullptr;
    QLabel* _strip = nullptr;
    QWidget* _offBar = nullptr;
    QLabel* _offText = nullptr;
    QPushButton* _turnOn = nullptr;
    QWidget* _tile = nullptr;
    QLabel* _tileText = nullptr;
    QToolButton* _clear = nullptr;
    QTableWidget* _table = nullptr;
    QToolButton* _followButton = nullptr;
    QPushButton* _run = nullptr;
    QLabel* _rows = nullptr;

    bool _live = false;
    bool _off = false;
    bool _following = true;
    bool _scrolling = false;      ///< the pane moves the scroll bar itself (not the user)
    bool _tinted = false;
    std::string _device;          ///< the history the log reads (another one: the log starts afresh)
    bool _hasCursor = false;      ///< the log was filled: the next read goes on after _since
    uint64_t _since = 0;          ///< the number of the newest record shown (the history's cursor)
    int64_t _missed = 0;          ///< records gone from the history before they were read, since the log started
    int _anomalies = 0;           ///< since the tile's Clear
    QString _lastAnomaly;         ///< its text and frame
    std::map<int, int64_t> _unknown;         ///< how many times the host sent each unknown command (the log's count)
    std::map<int, int64_t> _unknownAtClear;  ///< ... at the tile's Clear
};
