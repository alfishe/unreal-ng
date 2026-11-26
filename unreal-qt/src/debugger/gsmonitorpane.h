#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "debugger/targets/debugcpuid.h"
#include "emulator/state/statenode.h"

class EmulatorContext;
struct HistoryRequest;
struct HistoryReply;
class QLabel;
class QMenu;
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
/// A log row's context menu (A5.4, rowMenu): break when the game sends the row's command (a host-side event breakpoint
/// on the main CPU, host_command = the command), break when the firmware takes it (card_command on the card's CPU, the
/// GS slot's or the MultiSound's; none on the lightweight player), break on data byte N of an upload (host_data on the
/// main CPU with the hit policy "N": the Nth data byte the host writes from the moment it is set), go to the row's
/// moment (a TTD seek to its frame and T when the recording holds it), copy the selected rows. The breakpoints are the
/// managers' own, the ones the breakpoint editor and list show; an equal one already there is taken (enabled, a hit
/// policy's count reset) instead of a second. The owner runs them under its lease (rowActionRequested, runRowAction).
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

    /// region <The log row's actions (A5.4)>
    enum class RowAction : int
    {
        BreakHostSends = 0,  ///< host_command = the row's command, main CPU
        BreakCardTakes,      ///< card_command = the row's command, the card's CPU
        BreakUploadByte,     ///< host_data, the hit policy "N", main CPU
        GoTo,                ///< TTD seek to the row's frame and T
        Copy,                ///< the selected rows' text to the clipboard
    };
    /// What a row is, as the actions read it
    struct LogRow
    {
        QString kind;           ///< command, param, data, reply, block, card_took, anomaly, reset
        int command = -1;       ///< the command the record belongs to, -1 none
        int64_t frame = 0;      ///< the machine's frame and the main CPU's T in it
        int64_t t = 0;
        int64_t bytes = -1;     ///< a block's byte count
        bool upload = false;    ///< a block the host wrote (an upload or the host data stream), not a download
        bool hostData = false;  ///< the profile's host data stream (no command opened it)
    };
    LogRow logRow(int row) const;
    /// The row's menu: the actions in order, each enabled by the row and the machine as the last refresh saw it, a
    /// disabled one saying why in its tooltip. Triggered: Copy copies, the others ask the owner (rowActionRequested;
    /// data byte N asks N first through askByte)
    QMenu* rowMenu(int row, QWidget* parent = nullptr);
    /// Runs `action` for `row` on `context` (the owner's lease; `byte` the N of BreakUploadByte). `message` says what
    /// was done or why not; also shown over the log (actionText)
    bool runRowAction(EmulatorContext* context, RowAction action, int row, uint32_t byte = 0, QString* message = nullptr);
    /// The rows' text as Copy puts it on the clipboard: the selected rows when `row` is one of them, else `row`; the
    /// cells tab-separated, a line per row
    QString copyText(int row) const;
    /// Asks N for "Break on data byte N" (1..max); false: cancelled. Default: an input box
    std::function<bool(int max, int& out)> askByte;
    QString actionText() const;
    /// endregion

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
    QToolButton* sendButton() const { return _sendButton; }
    /// endregion

signals:
    void turnOnRequested();
    void pauseRequested();
    void continueRequested();
    /// The strip's Send...: the owner brings the GS send tab to the front
    void sendConsoleRequested();
    /// A row action (RowAction as int) for the owner to run under its lease (runRowAction); `byte`: the N asked
    void rowActionRequested(int action, int row, quint32 byte);

private:
    void showUnavailable(const QString& why);
    void showStrip(const StateNode& gs, const StateNode* protocol);
    /// Appends `reply`'s page and the pages after it (up to kPagesPerRefresh), moving the cursor; `filling`: the first
    /// fill, whose start is not counted as missed
    void appendPages(EmulatorContext* context, HistoryRequest& read, HistoryReply reply, bool filling);
    void appendRow(const StateNode& record);
    void showTile();
    void scrollToTail();
    /// What the row actions need of the machine, read at each refresh: the card's CPU, the recorded history
    void readActionState(EmulatorContext* context, DebugCpuId front);
    bool recorded(const LogRow& row) const;
    void showAction(const QString& text);

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
    QToolButton* _sendButton = nullptr;
    QPushButton* _run = nullptr;
    QLabel* _rows = nullptr;
    QLabel* _action = nullptr;    ///< what the last row action did

    // The row actions' view of the machine at the last refresh
    bool _cardCpuThere = false;
    DebugCpuId _cardCpu = DebugCpuId::GS;
    QString _cardWhy;             ///< why there is no card side
    bool _history = false;        ///< a TTD history to seek in
    uint64_t _historyFirst = 0;
    uint64_t _historyEndFrame = 0;
    uint64_t _historyEndT = 0;
    uint32_t _ttdUnits = 1;       ///< the history's time units per main CPU T (a turbo model's top clock)
    QString _historyWhy;

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
