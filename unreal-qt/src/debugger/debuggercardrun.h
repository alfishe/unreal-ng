#pragma once

// The Embedded debugger's card run controls (gs-debugger TODO P5.4; gui-card-debugger A4 "Toolbar", A6 S6), shown only
// while a sound card CPU is fitted:
// - Run to ▾ gains "Card cycle…", "Main T…", "Next card interrupt" and "Next host command"
//   (RunControl::CardRunTo "cycle" / "main_t" / "int" / "command"). The card they act on is the card in front, else
//   the first card fitted. A run that did not get there (a breakpoint, the deadline, a refusal) says why in the
//   window's stop line.
// - "Step main" / "Step other" (Alt+F11): with a card in front the main CPU's own step; with the main CPU in front the
//   card's step (RunControl::CardStep "in"), the main CPU's tab staying in front.
// - "Main CPU" (Ctrl+1) and "Card CPU" (Ctrl+2) put the CPU's tab in front; Ctrl+2 again goes to the next card.
//
// Every run is on the GUI thread of a paused machine, as the window's card steps (DebuggerWindow::cardStep): the
// machine resumes until the card or the main CPU ends the run, then pauses; the window refreshes afterwards. After
// "Main T" the card has its own position (it catches up lazily at a port access or the frame end).

#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "debugger/targets/debugcpuid.h"

class DebuggerCpuTabs;
class Emulator;
class QAction;
class QMenu;
class QToolBar;
class QWidget;

class DebuggerCardRun : public QObject
{
    Q_OBJECT

public:
    struct Hooks
    {
        /// Before the machine moves: the stop line goes, an auto-step stops
        std::function<void()> before;
        /// A run that did not get there (or was refused): its message for the stop line
        std::function<void(const QString&)> notice;
        /// After a run or a step: the panes and the screen refresh
        std::function<void()> after;
        /// A number from the user in min..max (false: cancelled); null: QInputDialog
        std::function<bool(const QString& title, const QString& label, int value, int min, int max, int& out)> ask;
    };

    /// Adds the Run to entries at the end of `runTo`, Step other to `toolbar` before `before` (null: at the end), and
    /// the CPU shortcuts to `window` and the tab bar's context menu
    DebuggerCardRun(QWidget* window, DebuggerCpuTabs* tabs, QMenu* runTo, QToolBar* toolbar, QAction* before,
                    Hooks hooks);

    void setEmulator(std::shared_ptr<Emulator> emulator);
    /// Shown with a card fitted, hidden without; Step other's text follows the CPU in front
    void update();
    /// The step and run entries usable (a paused machine)
    void setEnabled(bool enabled);

    /// The card the entries act on: the card in front, else the first fitted; Main: none
    DebugCpuId card() const;

    /// RunControl::CardRunTo on card(): the window's path for the Run to entries. False: refused or not there (the
    /// notice said why)
    bool runTo(const std::string& until, uint64_t value);
    /// The message of the last run or step that did not get there ("" after one that did)
    const QString& lastNotice() const { return _lastNotice; }

    QAction* cardCycleAction() const { return _cardCycle; }
    QAction* mainTAction() const { return _mainT; }
    QAction* cardInterruptAction() const { return _cardInterrupt; }
    QAction* hostCommandAction() const { return _hostCommand; }
    std::vector<QAction*> runToActions() const { return {_cardCycle, _mainT, _cardInterrupt, _hostCommand}; }
    QAction* stepOtherAction() const { return _stepOther; }
    QAction* mainCpuAction() const { return _mainCpu; }
    QAction* cardCpuAction() const { return _cardCpu; }

public slots:
    void runToCardCycle();
    void runToMainT();
    void runToCardInterrupt();
    void runToHostCommand();
    /// The other CPU's step: the main CPU's with a card in front, the card's with the main CPU in front
    void stepOther();
    void selectMainCpu();
    /// The first card's tab; with a card in front, the next card's
    void selectCardCpu();

private:
    bool ask(const QString& title, const QString& label, int value, int min, int max, int& out);
    /// A refusal or a run that did not get there: kept and told
    void tell(const QString& message);
    /// The machine may move: paused, nobody else's session holds it
    bool ready();

    QWidget* _window = nullptr;
    DebuggerCpuTabs* _tabs = nullptr;
    Hooks _hooks;
    std::shared_ptr<Emulator> _emulator;
    QString _lastNotice;
    bool _enabled = true;

    QAction* _separator = nullptr;
    QAction* _cardCycle = nullptr;
    QAction* _mainT = nullptr;
    QAction* _cardInterrupt = nullptr;
    QAction* _hostCommand = nullptr;
    QAction* _stepOther = nullptr;
    QAction* _mainCpu = nullptr;
    QAction* _cardCpu = nullptr;
};
