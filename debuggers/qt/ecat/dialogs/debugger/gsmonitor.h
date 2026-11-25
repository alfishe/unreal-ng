// unreal-debugger (not in ecat): the GS monitor tab (gui-card-debugger A5.3 "Protocol monitor", C10; gs-debugger
// P5.6), a card tab beside GS log and GS protocol for every card that carries a General Sound (GS, NeoGS, the
// lightweight player, the MultiSound's GS). Top to bottom: the protocol strip (what the host's conversation is at, the
// status register, the mailbox, the debugger's last send), the error tile (the anomalies of the command log and the
// firmware's unknown commands counted since the tab's Clear, the last anomaly), the command log following its tail
// (scrolling up stops following, Follow resumes), and the machine's Pause / Continue. With the command log off the
// tab says so and offers Turn on (history <slot>.commands mode ring). Send... beside the strip brings the GS send tab
// to the front (gssend.h)

#pragma once

#include <cstdint>
#include <string>

#include <QWidget>

#include "emulator/emulator.h"

class QAction;
class QLabel;
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

    //For the self-check
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
};
