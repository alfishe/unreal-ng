// unreal-debugger (not in ecat): the machine's time travel in the debugger window (TTD branches debuggers.md §4).
// A group on the window's toolbar, shown only while the machine has a recorded history: Step Back, Step Over Back,
// Run Back (to the breakpoints), Frame Back, Branch Here / Continue Rec, and the branch drop-down with the branch
// management. Every button is a TTDControl verb through Emulator::ttd_call (in process or over the WebAPI); nothing
// records unless asked (Branch Here / Continue Rec only)

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
    TtdControls(QWidget * window, QToolBar * toolbar, Emulator * e, std::function<CPU *()> main_cpu);

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
    //Asked before a delete; the default is a question box (the self-check answers itself)
    std::function<bool(const QString &title, const QString &text)> confirm;

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
    //A moving verb: the window redraws after it. True when it ran
    bool move(const std::string &verb, std::map<std::string, std::string> options, const QString &title,
              QJsonObject * out = nullptr);
    //The branch of the last list, null when unknown
    const QJsonObject * branch(unsigned int id) const;
    void set_shown(bool shown);
    void update_actions();

    QWidget * m_window;
    Emulator * e;
    std::function<CPU *()> m_main_cpu;
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
    QString m_last_result;  //what the last back step said (the tooltip)
};
