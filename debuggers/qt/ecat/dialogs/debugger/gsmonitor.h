// unreal-debugger (not in ecat): the GS monitor tab (gui-card-debugger A5.3 "Protocol monitor", C10; gs-debugger
// P5.6), a card tab beside GS log and GS protocol for every card that carries a General Sound (GS, NeoGS, the
// lightweight player, the MultiSound's GS). Top to bottom: the protocol strip (what the host's conversation is at, the
// status register, the mailbox, the debugger's last send), the error tile (the anomalies of the command log and the
// firmware's unknown commands counted since the tab's Clear, the last anomaly), the command log following its tail
// (scrolling up stops following, Follow resumes), and the machine's Pause / Continue. With the command log off the
// tab says so and offers Turn on (history <slot>.commands mode ring)

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
    //The newest records read each time (the history read has no since cursor: the rows are taken after the last one
    //shown), and the rows the table keeps
    static constexpr size_t kReadRows = 200;
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
    uint64_t anomalies() const { return m_anomalies; }
    uint64_t unknown_commands() const { return m_unknown_seen; }
    bool following() const { return m_following; }

protected:
    //Live only while in sight: the tab's own timer, every Emulator::live_card_ms while the machine runs
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    struct Key {
        int64_t frame = 0, t = 0, cycle = 0;
        std::string kind, text;
        bool operator==(const Key &o) const
        {
            return frame == o.frame && t == o.t && cycle == o.cycle && kind == o.kind && text == o.text;
        }
    };
    static Key key_of(const Emulator::GsMonitorRow &r);
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
    QTableWidget * m_table;
    QAction * m_pause = nullptr;
    QAction * m_run = nullptr;
    QTimer * m_timer;
    void on_timer();

    bool m_following = true;
    bool m_scrolling = false;     //the tab moves the bar itself: not the user scrolling up
    bool m_have_key = false;
    Key m_last;                   //the newest record shown
    uint64_t m_written = 0;       //the history's count at the last read
    uint64_t m_unseen = 0;        //records written between two reads past the rows read (not shown)
    uint64_t m_anomalies = 0;     //since Clear
    std::string m_last_anomaly;
    int64_t m_last_anomaly_frame = -1;
    uint64_t m_unknown_total = 0; //the firmware's count now
    uint64_t m_unknown_base = 0;  //and at Clear
    uint64_t m_unknown_seen = 0;
    bool m_unknown_present = false;
};
