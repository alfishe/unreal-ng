// unreal-debugger (not in ecat): the GS monitor tab (gui-card-debugger A5.3 "Protocol monitor", C10; gs-debugger
// P5.6), a card tab beside GS log and GS protocol for every card that carries a General Sound (GS, NeoGS, the
// lightweight player, the MultiSound's GS). Top to bottom: the protocol strip (what the host's conversation is at, the
// status register, the mailbox, the debugger's last send), the error tile (the anomalies of the command log and the
// firmware's unknown commands counted since the tab's Clear, the last anomaly), the command log following its tail
// (scrolling up stops following, Follow resumes), and the machine's Pause / Continue. With the command log off the
// tab says so and offers Turn on (history <slot>.commands mode ring). Send... beside the strip brings the GS send tab
// to the front (gssend.h)
//
// A row's context menu (A5.4): break when the game sends the row's command (event host_command = the command on the
// main CPU), break when the firmware takes it (card_command on the card's CPU, gs or ms; none on the lightweight
// player), break on data byte N of an upload (host_data on the main CPU, hit policy "N": the Nth data byte the host
// writes from the moment it is set), go to the row's moment (a TTD seek to its frame and T when the recording holds
// it), copy the selected rows. The breakpoints are the CPUs' event breakpoints the Breakpoints tab lists (the shim's,
// in process or over the WebAPI); an equal one already there is taken (enabled, a hit policy's count reset)

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <QWidget>

#include "emulator/emulator.h"

class QAction;
class QLabel;
class QMenu;
class QPushButton;
class QTableWidget;
class QTimer;
class QToolButton;

class GsMonitorView : public QWidget
{
    Q_OBJECT
public:
    //The newest records the tab fills with, then the records read after its cursor (the history's since) each time at
    //most (the rest next time), and the rows the table keeps
    static constexpr size_t kReadRows = 200;
    static constexpr size_t kReadAfter = 1600;
    static constexpr int kMaxRows = 2000;
    enum { ColFrame = 0, ColT, ColKind, ColCommand, ColName, ColText, ColCount };

    GsMonitorView(Emulator * e, QWidget *parent);
    //The card read: "gs" or "ms" ("" none), @p caption over the tab names it; another card starts the tab afresh
    void set_card(const std::string &card, const QString &caption);
    //The machine's Pause and Run (the toolbar's): the tab's button runs the one that applies
    void set_machine_actions(QAction * pause, QAction * run);
    //Reads the card once (Emulator::read_gs_monitor) and shows it
    void refresh();
    //The tile's counters back to zero (the log stays)
    void clear_counters();
    //Follows the log's tail again
    void follow();

    //The row actions (A5.4), in the menu's order
    enum class RowAction { BreakHostSends = 0, BreakCardTakes, BreakUploadByte, GoTo, Copy };
    //The row's menu: each action enabled by the row and the machine now (the card's CPU, the recorded history), a
    //disabled one saying why in its tooltip; triggered, it runs (data byte N asks N first through ask_byte)
    QMenu * row_menu(int row, QWidget * parent = nullptr);
    //Runs an action for the row (byte: the N of BreakUploadByte); message: what was done or why not (also shown over
    //the log)
    bool run_row_action(RowAction action, int row, unsigned int byte = 0, QString * message = nullptr);
    //The rows Copy puts on the clipboard: the selected ones when row is among them, else row; cells tab-separated
    QString copy_text(int row) const;
    //Asks N for "Break on data byte N" (1..max); false: cancelled. Default: an input box
    std::function<bool(int max, int &out)> ask_byte;

    //For the self-check
    QLabel * action_line() const { return m_action; }
    QLabel * strip() const { return m_strip; }
    QLabel * tile() const { return m_tile; }
    QTableWidget * table() const { return m_table; }
    QWidget * off_panel() const { return m_off; }
    QPushButton * turn_on_button() const { return m_turn_on; }
    QPushButton * clear_button() const { return m_clear; }
    QToolButton * follow_button() const { return m_follow_button; }
    QToolButton * machine_button() const { return m_machine; }
    QToolButton * send_button() const { return m_send; }
    uint64_t anomalies() const { return m_anomalies; }
    uint64_t unknown_commands() const { return m_unknown_seen; }
    bool following() const { return m_following; }

signals:
    //Send... (Ctrl+Shift+S in the window): the GS send tab to the front
    void send_requested();
    //A row action added or took a breakpoint: the lists and the listings' marks read again
    void breakpoints_changed();
    //A row action moved the machine in time: every view read again
    void moved();

protected:
    //Live only while in sight: the tab's own timer, every Emulator::live_card_ms while the machine runs
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void restart();
    void append(const Emulator::GsMonitorRow &r);
    void show_strip(const Emulator::GsMonitor &m);
    void show_tile();
    void update_machine_button();
    void turn_on();

    Emulator * e;
    std::string m_card;
    QLabel * m_caption;
    QLabel * m_strip;
    QWidget * m_tile_box;
    QLabel * m_tile;
    QPushButton * m_clear;
    QWidget * m_off;
    QLabel * m_off_text;
    QPushButton * m_turn_on;
    QLabel * m_answer;
    QLabel * m_action;
    QLabel * m_log_line;
    QToolButton * m_follow_button;
    QToolButton * m_machine;
    QToolButton * m_send;
    QTableWidget * m_table;
    QAction * m_pause = nullptr;
    QAction * m_run = nullptr;
    QTimer * m_timer;
    void on_timer();

    bool m_following = true;
    bool m_scrolling = false;     //the tab moves the bar itself: not the user scrolling up
    bool m_have_cursor = false;   //filled: the next read goes on after m_since
    uint64_t m_since = 0;         //the number of the newest record shown (the history's cursor)
    uint64_t m_unseen = 0;        //records gone from the history before the tab read them (not shown)
    uint64_t m_anomalies = 0;     //since Clear
    std::string m_last_anomaly;
    int64_t m_last_anomaly_frame = -1;
    uint64_t m_unknown_total = 0; //the firmware's count now
    uint64_t m_unknown_base = 0;  //and at Clear
    uint64_t m_unknown_seen = 0;
    bool m_unknown_present = false;
    unsigned int m_ttd_units = 1; //the history's units per main CPU T (GsMonitor::ttd_units)
};
