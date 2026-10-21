#pragma once

#ifndef DISASSEMBLERWIDGET_H
#define DISASSEMBLERWIDGET_H

#include <QKeyEvent>
#include <QLabel>
#include <QObject>
#include <QTextEdit>
#include <QTextCharFormat>
#include <QWidget>
#include <QInputDialog>
#include <QRegularExpression>

#include <atomic>
#include <optional>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "debugger/breakpoints/breakpointmanager.h"

QT_BEGIN_NAMESPACE
namespace Ui
{
class DisassemblerWidget;
}
QT_END_NAMESPACE

class DebuggerWindow;

// Custom QPlainTextEdit to handle keyboard navigation in disassembly view
class DisassemblyTextEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit DisassemblyTextEdit(QWidget* parent = nullptr) : QTextEdit(parent) {}

signals:
    void keyUpPressed();
    void keyDownPressed();
    void pageUpPressed();
    void pageDownPressed();
    void enterPressed();
    void toggleScrollMode();
    void goToAddressRequested();
    void wheelScrollUp();
    void wheelScrollDown();

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Up)
        {
            emit keyUpPressed();
            event->accept();
        }
        else if (event->key() == Qt::Key_Down)
        {
            emit keyDownPressed();
            event->accept();
        }
        else if (event->key() == Qt::Key_PageUp)
        {
            emit pageUpPressed();
            event->accept();
        }
        else if (event->key() == Qt::Key_PageDown)
        {
            emit pageDownPressed();
            event->accept();
        }
        else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        {
            emit enterPressed();
            event->accept();
        }
        else if (event->key() == Qt::Key_B)
        {
            emit toggleScrollMode();
            event->accept();
        }
        else if (event->key() == Qt::Key_G && 
                (event->modifiers() == Qt::ControlModifier || 
                 event->modifiers() == Qt::MetaModifier))
        {
            // Ctrl+G on Windows/Linux or Cmd+G on macOS
            emit goToAddressRequested();
            event->accept();
        }
        else
        {
            QTextEdit::keyPressEvent(event);
        }
    }
    
    void wheelEvent(QWheelEvent* event) override
    {
        // Check if the wheel event is vertical scrolling
        if (event->angleDelta().y() != 0)
        {
            // Determine scroll direction
            if (event->angleDelta().y() > 0)
            {
                // Scroll up (wheel moved up)
                emit wheelScrollUp();
            }
            else
            {
                // Scroll down (wheel moved down)
                emit wheelScrollDown();
            }
            event->accept();
        }
        else
        {
            // Pass horizontal scrolling to the parent class
            QTextEdit::wheelEvent(event);
        }
    }
};

// Enum for disassembly scroll modes
enum class ScrollMode
{
    Byte,    // Navigate by individual bytes
    Command  // Navigate by whole commands
};

class DisassemblerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DisassemblerWidget(QWidget* parent = nullptr);
    virtual ~DisassemblerWidget() override;

    void setDisassemblerAddress(uint16_t pc);

    /// region <Controls of the GUI actions (dbg.follow_pc, dbg.labels_toggle, dbg.scroll_mode_toggle, dbg.goto)>
    /// Show the disassembly at PC again (Enter)
    void followPc();
    /// Labels on / off in this view (operands and the label column); process-wide, readable from any thread
    void setShowLabels(bool show);
    static std::atomic<bool>& LabelsShown();
    /// Byte / command scroll (B)
    void switchScrollMode();
    /// The address of the line under the text cursor (a click, or the line kept across refreshes); published as the
    /// debugger's "disasm" cursor (actions::DebuggerCursors) for run.to_cursor
    std::optional<uint16_t> cursorAddress() const { return m_cursorAddress; }
    /// Move the cursor row by `rows` instructions (bytes in byte scroll) - Up / Down, PageUp / PageDown (16) and
    /// dbg.cursor_* / dbg.page_*: from the cursor row, else PC, else the top line; the view scrolls to keep it visible
    void moveCursorBy(int rows);
    /// Toggle an execution breakpoint at the cursor row, else the top line of the view (as a click in the
    /// breakpoint column)
    void toggleBreakpointAtCursor();
    /// endregion </Controls>

    // Helper methods
protected:
    Emulator* getEmulator();
    EmulatorContext* getEmulatorContext();
    Memory* getMemory();
    Z80Registers* getZ80Registers();
    std::unique_ptr<Z80Disassembler>& getDisassembler();
    BreakpointManager* getBreakpointManager() const;
    
    // Event handling
    bool eventFilter(QObject* obj, QEvent* event) override;

    std::string formatRuntimeInformation();
    void highlightCurrentPC();
    void updateScrollModeIndicator();
    void updateDebuggerStateIndicator();
    void updateBankIndicator(uint16_t address);
    uint16_t getNextCommandAddress(uint16_t currentAddress);
    uint16_t getPreviousCommandAddress(uint16_t currentAddress);
    uint16_t findInstructionBoundaryBefore(uint16_t targetAddress);
    uint16_t getCenteredStartAddress(uint16_t targetPC, size_t instructionsBeforePC);
    
    // Breakpoint methods
    void toggleBreakpointAtAddress(uint16_t address);
    bool hasBreakpointAtAddress(uint16_t address) const;
    void updateBreakpointHighlighting();

    // Keyboard navigation handlers
    void navigateUp();
    void navigateDown();
    void returnToCurrentPC();
    void toggleScrollMode();
    void handleBreakpointClick(int lineNumber);
    void showGoToAddressDialog();
    uint16_t parseAddressInput(const QString& input);

public slots:
    void reset();
    void refresh();
    void refreshPreservingPosition(uint16_t addressToKeep);
    void goToAddress(uint16_t address);
    void prepareForShutdown();  // Block all refreshes during app shutdown

signals:

private:
    Ui::DisassemblerWidget* ui;
    QThread* m_mainThread;
    DisassemblyTextEdit* m_disassemblyTextEdit;
    QLabel* m_scrollModeIndicator;  // Label to show current scroll mode
    QLabel* m_stateIndicator;       // Label to show debugger state (Active/Detached)
    QLabel* m_bankIndicator;        // Label to show current memory bank

    uint16_t m_currentPC;                 // Current program counter
    uint16_t m_displayAddress;            // Address currently displayed at the top of the disassembly view
    QTextCharFormat m_pcHighlightFormat;      // Format for highlighting current PC
    QTextCharFormat m_breakpointFormat;      // Format for highlighting breakpoints
    ScrollMode m_scrollMode;                  // Current scroll mode (byte or command)
    bool m_isActive;                          // Whether debugger is in active (paused) state
    std::map<uint16_t, uint16_t> m_addressMap; // Maps line numbers to addresses

    DebuggerWindow* m_debuggerWindow;
    bool m_isShuttingDown = false;  // Flag to block refreshes during shutdown

    // The cursor line (run to the cursor): set by the user's text cursor, kept and marked across refreshes
    void onTextCursorMoved();
    void markCursorLine();
    void publishCursor();
    bool lineOf(uint16_t address, int& line) const;
    QTextCharFormat m_cursorFormat;
    std::optional<uint16_t> m_cursorAddress;
    bool m_settingContent = false;  ///< setHtml / a programmatic caret move: not the user's cursor
};

#endif  // DISASSEMBLERWIDGET_H
