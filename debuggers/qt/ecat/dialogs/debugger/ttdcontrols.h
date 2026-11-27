// unreal-debugger (not in ecat): the machine's time travel in the debugger window (TTD branches debuggers.md §4).
// A group on the window's toolbar, shown only while the machine has a recorded history: Step Back, Step Over Back,
// Run Back (to the breakpoints), Frame Back, Branch Here / Continue Rec, and the branch drop-down with the branch
// management. Every button is a TTDControl verb through Emulator::ttd_call (in process or over the WebAPI); nothing
// records unless asked (Branch Here / Continue Rec only).
// With a sound card's tab in front (gs, ms) the back steps are the card's (gs-debugger design §8): Step Back and the
// Step of the past (F7) move by card instructions (`step-instruction {cpu, dir}`), Step over / Step out of the past
// (F8 / Ctrl+F8) are the card's steps through the history (`step-instruction {cpu, kind}`; at its end they run on
// live, D-3), Step Over Back is `reverse-step-over {cpu}`, Run Back goes to the card's previous breakpoint hit
// (`reverse-continue {cpu}`). At a card position the
// moment names both CPUs: the card's instruction count and PC, the main CPU's PC and the T-states into it

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "emulator/emulator.h"

class QAction;
class QMenu;
class QToolBar;
class QToolButton;
class QWidget;

class TtdControls : public QObject
{
    Q_OBJECT
public:
    //Adds the group to the toolbar at its end. window: the questions' parent and the shortcuts' window
    //(Shift+F7 / F8 / F9); main_cpu: the processor whose breakpoints Run Back goes back to
    //current_cpu: the processor whose tab is in front (a card's: the back steps act on the card)
    TtdControls(QWidget * window, QToolBar * toolbar, Emulator * e, std::function<CPU *()> main_cpu,
                std::function<CPU *()> current_cpu = nullptr);

    //Reads the session (status, position, branches) and sets what shows; force: now, else once in five calls (the
    //window's timer ticks five times a second)
    void poll(bool force = false);
    //The machine can be acted on (loaded, online, not only seen): the buttons follow
    void set_live(bool live);
    //The moment for the window's state label: "History: frame 12 of 40, replaying · on 'b' (2 branches)";
    //empty without a history
    QString moment() const;
    bool shown() const { return m_shown; }

    //A verb; the answer's JSON in out. title not empty: a refusal shows in a warning box under it
    bool call(const std::string &verb, std::map<std::string, std::string> options, QJsonObject * out = nullptr,
              const QString &title = QString());
    //The same without waiting (not in ecat): then gets the answer, at once in process, later from a remote
    //machine (never after this is gone)
    void call_then(const std::string &verb, std::map<std::string, std::string> options, const QString &title,
                   std::function<void(bool ok, const QJsonObject &answer)> then);
    //Asked before a delete; the default is a question box (the self-check answers itself)
    std::function<bool(const QString &title, const QString &text)> confirm;
    //A refusal or an answer that moved nothing (warning: a refusal); the default is a message box (the self-check
    //reads them)
    std::function<void(const QString &title, const QString &text, bool warning)> inform;

    //The card CPU the back steps act on: "gs" / "ms" while a card's tab is in front, else empty (the main CPU)
    QString card_cpu() const;
    //The card's readout of the last poll (a `cards[]` entry of `position`), empty when the machine has no such card
    QJsonObject card_readout(const QString &cpu) const;
    //A card readout as the moment shows it: "gs at instruction 1200, PC #0A3C; main CPU PC #8009, +7 T into the
    //next instruction" at a card position (", at its instruction boundary" when not inside one); "gs at instruction
    //1200, PC #0A3C" without one; "gs: no instruction counts in this recording" when the recording has none
    static QString describe_card(const QJsonObject &card);
    //Run Back's breakpoint conditions as one reverse-continue `condition`, from {address, its condition} (empty: it
    //stops there whatever holds): "(PC == #8000 && (A == 5)) || (PC == #9000)"; empty when no address has one (the
    //pcs alone say it all)
    static std::string run_back_condition(const std::vector<std::pair<unsigned int, std::string>> &breakpoints);
    //The Step (F7, kind "in"), Step over (F8, "over"), Step out (Ctrl+F8, "out") or Run to the cursor (F4, "to", to
    //`address`) of the card's tab in the past: the card's step or run through the history (a run to the cursor not
    //reached in the history says so and moves nothing). False when that is not the case (the main CPU's tab, not in
    //the past, or the card at the history's end: D-3, it steps on live): the window's own step runs
    //A remote machine's answer comes after the return: its at_end then runs `live` (the window's own step)
    bool step_forward(const std::string &kind = "in", unsigned int address = 0, std::function<void()> live = {});
    //What the last move said (the branch button's tooltip)
    const QString &last_result() const { return m_last_result; }

    QAction * step_back_action() const { return m_step_back; }
    QAction * step_over_back_action() const { return m_step_over_back; }
    QAction * run_back_action() const { return m_run_back; }
    QAction * frame_back_action() const { return m_frame_back; }
    QAction * branch_here_action() const { return m_branch_here; }
    QToolButton * branch_button() const { return m_branch_button; }
    //The drop-down as it is built when opened
    QMenu * branch_menu() const { return m_branch_menu; }
    void build_branch_menu();

    //The branch commands of the drop-down (the TTD panel's questions)
    bool switch_branch(unsigned int id);
    void rename_branch(unsigned int id);
    void tag_branch(unsigned int id);
    void note_branch(unsigned int id);
    void pin_branch(unsigned int id);
    bool delete_branch(unsigned int id);
    void save_lineage();

signals:
    //The machine moved in time: the window's views read it again
    void moved();
    //The moment changed: the window's state label
    void moment_changed();

public slots:
    void step_back();
    void step_over_back();
    void run_back();
    void frame_back();
    void branch_here();

private:
    //A move of the card CPU: the verb with `cpu`; an answer that moved nothing (at_start, at_end, no_hit) is said
    //under title. True when it moved
    //`at_end` given: an at_end answer is not said but calls it (the caller steps on live). A remote machine's
    //answer comes later: true while it is to come
    bool card_move(const QString &cpu, const std::string &verb, std::map<std::string, std::string> options,
                   const QString &title, std::function<void()> at_end = {});
    bool card_moved(const QString &cpu, const QString &title, bool ok, const QJsonObject &answer,
                    const std::function<void()> &at_end);
    //A moving verb: the window redraws after it; then gets the answer of one that ran, before the redraw. True
    //when it ran (or, a remote machine's, while its answer is to come)
    bool move(const std::string &verb, std::map<std::string, std::string> options, const QString &title,
              std::function<void(const QJsonObject &answer)> then = {});
    //Branch Here's resume, asked again with the user's decision on the media
    void resume_here(const QString &title, std::map<std::string, std::string> options);
    //The branch of the last list, null when unknown
    const QJsonObject * branch(unsigned int id) const;
    void set_shown(bool shown);
    void update_actions();

    QWidget * m_window;
    Emulator * e;
    std::function<CPU *()> m_main_cpu;
    std::function<CPU *()> m_current_cpu;
    std::vector<QAction *> m_group;
    QAction * m_step_back = nullptr;
    QAction * m_step_over_back = nullptr;
    QAction * m_run_back = nullptr;
    QAction * m_frame_back = nullptr;
    QAction * m_branch_here = nullptr;
    QToolButton * m_branch_button = nullptr;
    QAction * m_branch_button_action = nullptr;
    QMenu * m_branch_menu = nullptr;
    bool m_shown = true;
    bool m_live = true;
    int m_tick = 0;

    //What the last poll read
    QJsonObject m_status;
    QJsonObject m_position;
    std::vector<QJsonObject> m_branches;    //the list, the active lineage first
    QJsonObject m_branch_list;              //the answer of `branches` (active, viewed, lineage, forks_here)
    QString m_state;        //"Recording", "History: ...", "Live, not recorded"; empty: no history
    QString m_branch_part;  //"on 'b' (2 branches)"
    QString m_fork_part;    //"fork here: 1 other future"
    QString m_card_part;    //the card position, or the readout of the card in front (describe_card)
    QString m_last_result;  //what the last back step said (the tooltip)
};
