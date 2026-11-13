#pragma once

// The Embedded debugger's time controls (TTD branches debuggers.md §4): a group on the debugger's own toolbar, shown
// only while the instance has a recorded history - Step Back, Step Over Back, Run Back (to the enabled execution
// breakpoints), Frame Back, Branch Here / Continue Rec - the moment ("History: frame ... replaying", "fork here")
// and the branch drop-down with the branch management. Every button is a TTDControl verb; nothing records unless
// asked (rule 7)

#include <QObject>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "widgets/ttdbranchcommands.h"

class BreakpointManager;
namespace ttd
{
struct TTDReply;
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
    /// From the published session info: the group's visibility, the moment, the buttons' state
    void refresh();

    /// The enabled user execution breakpoints as reverse-continue's `pcs` ("#8000,#8010"); ranges expand, at most
    /// kMaxRunBackPcs addresses. Empty: none
    static std::string RunBackPcs(const BreakpointManager& breakpoints);
    static constexpr size_t kMaxRunBackPcs = 1024;

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
    void buildBranchMenu();
    void setShown(bool shown);

    QWidget* _window = nullptr;
    std::shared_ptr<Emulator> _emulator;
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
