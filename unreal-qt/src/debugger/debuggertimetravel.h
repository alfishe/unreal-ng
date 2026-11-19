#pragma once

// The Embedded debugger's time controls (TTD branches debuggers.md §4): a group on the debugger's own toolbar, shown
// only while the instance has a recorded history - Step Back, Step Over Back, Run Back (to the enabled execution
// breakpoints), Frame Back, Branch Here / Continue Rec - the moment ("History: frame ... replaying", "fork here")
// and the branch drop-down with the branch management. Every button is a TTDControl verb; nothing records unless
// asked (rule 7).
// With a sound card's tab in front (debuggercpu.h) the group acts on that card's CPU, as eCat's does (TTD branches
// debuggers.md §8): Step Back is `step-instruction {cpu, dir: back}`, Run Back `reverse-continue {cpu}` (the card's
// own breakpoints), the window's Step In in the past `step-instruction {cpu, dir: forward}` (stepForward); Step Over
// Back is the main CPU's only. At a position on a card's CPU (gs-debugger design §8) the moment names both CPUs - the
// card's instruction count and PC, the main CPU's PC and the T-states the card is into its instruction; without one,
// the card's tab adds where that card stands

#include <QObject>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "debugger/targets/debugcpuid.h"
#include "widgets/ttdbranchcommands.h"

class BreakpointManager;
namespace ttd
{
struct TTDReply;
struct TTDCardPosition;
}
class Emulator;
class QAction;
class QLabel;
class QMenu;
class QTimer;
class QToolBar;
class QToolButton;
class QWidget;

class DebuggerTimeTravel : public QObject
{
    Q_OBJECT

public:
    /// Adds the group to toolbar before `before` (null: at the end). window: the questions' parent and the
    /// shortcuts' window (Shift+F7 / F8 / F9)
    DebuggerTimeTravel(QWidget* window, QToolBar* toolbar, QAction* before);
    ~DebuggerTimeTravel() override;

    void setEmulator(std::shared_ptr<Emulator> emulator);
    /// The CPU in front (the window's processor tabs); unset: the main CPU
    void setCpu(std::function<DebugCpuId()> cpu);
    /// What a move said that the user should read (a refusal: `warning`; an answer that moved nothing): a box by
    /// default, the tests take it
    std::function<void(const QString& title, const QString& text, bool warning)> inform;
    /// From the published session info: the group's visibility, the moment, the buttons' state
    void refresh();

    /// The enabled user execution breakpoints as reverse-continue's `pcs` ("#8000,#8010"); ranges expand, at most
    /// kMaxRunBackPcs addresses. Empty: none
    static std::string RunBackPcs(const BreakpointManager& breakpoints);
    static constexpr size_t kMaxRunBackPcs = 1024;
    /// The moment's card part: "gs at instruction 1200, PC #0A3C; main CPU PC #8009, +7 T into the next instruction"
    /// for the card the machine stands at a position of (", at its instruction boundary" when not inside one); empty
    /// without a card position
    static QString CardMoment(const std::vector<ttd::TTDCardPosition>& cards);
    /// Where one card stands without a card position: "gs at instruction 1200, PC #0A3C" ("gs: no instruction counts
    /// in this recording" without counts)
    static QString CardReadout(const ttd::TTDCardPosition& card);

    QAction* stepBackAction() const { return _stepBack; }
    QAction* stepOverBackAction() const { return _stepOverBack; }
    QAction* runBackAction() const { return _runBack; }
    QAction* frameBackAction() const { return _frameBack; }
    QAction* branchHereAction() const { return _branchHere; }
    QLabel* momentLabel() const { return _moment; }
    QToolButton* branchButton() const { return _branchButton; }
    /// The drop-down as it is built when opened
    QMenu* branchMenu() const { return _branchMenu; }
    ttdgui::BranchCommands& commands() { return *_commands; }
    /// The group shows (a history exists)
    bool shown() const { return _shown; }
    /// What the last move said (the moment's tooltip)
    QString lastResult() const { return _lastResult; }
    /// The window's Step In with a card's tab in front in the recorded past: one card instruction forward through the
    /// history (rule 3: forward actions there replay). False when it is not for the group (the main CPU in front, live,
    /// no history): the window steps as before
    bool stepForward();

signals:
    /// The machine moved in time: the debugger's views redraw
    void moved();
    /// "Show in the TTD panel": the main window opens the panel with its Branches row
    void showInTtdPanel();

public slots:
    void stepBack();
    void stepOverBack();
    void runBack();
    void frameBack();
    void branchHere();

private:
    /// A moving verb; the refusal or what it said goes to the moment's tooltip. True when it ran
    bool move(const std::string& verb, std::map<std::string, std::string> options, const QString& title,
              ttd::TTDReply* out = nullptr);
    /// A card's move (eCat's card_move): the verb with `cpu`; an answer that moved nothing (at_start, at_end, no_hit)
    /// is said and moves nothing. True when the machine moved
    bool cardMove(DebugCpuId cpu, const std::string& verb, std::map<std::string, std::string> options,
                  const QString& title);
    DebugCpuId cpu() const;
    void buildBranchMenu();
    void setShown(bool shown);

    QWidget* _window = nullptr;
    std::shared_ptr<Emulator> _emulator;
    std::function<DebugCpuId()> _cpu;
    std::unique_ptr<ttdgui::BranchCommands> _commands;
    std::vector<QAction*> _group;   // every toolbar entry of the group (shown together)
    QAction* _stepBack = nullptr;
    QAction* _stepOverBack = nullptr;
    QAction* _runBack = nullptr;
    QAction* _frameBack = nullptr;
    QAction* _branchHere = nullptr;
    QLabel* _moment = nullptr;
    QToolButton* _branchButton = nullptr;
    QAction* _branchButtonAction = nullptr;
    QMenu* _branchMenu = nullptr;
    QTimer* _timer = nullptr;
    bool _shown = true;
    QString _lastResult;   // what the last back step said (the moment's tooltip)
};
