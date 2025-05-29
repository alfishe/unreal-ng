#pragma once

#ifndef DISASSEMBLERWIDGET_H
#define DISASSEMBLERWIDGET_H

#include <QKeyEvent>
#include <QLabel>
#include <QObject>
#include <QPlainTextEdit>
#include <QTextCharFormat>
#include <QWidget>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

QT_BEGIN_NAMESPACE
namespace Ui
{
class DisassemblerWidget;
}
QT_END_NAMESPACE

class DebuggerWindow;

// Custom QPlainTextEdit to handle keyboard navigation in disassembly view
class DisassemblyTextEdit : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit DisassemblyTextEdit(QWidget* parent = nullptr) : QPlainTextEdit(parent) {}

signals:
    void keyUpPressed();
    void keyDownPressed();
    void enterPressed();
    void toggleScrollMode();

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
        else
        {
            QPlainTextEdit::keyPressEvent(event);
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

    // Helper methods
protected:
    Emulator* getEmulator();
    EmulatorContext* getEmulatorContext();
    Memory* getMemory();
    Z80Registers* getZ80Registers();
    std::unique_ptr<Z80Disassembler>& getDisassembler();

    std::string formatRuntimeInformation();
    void highlightCurrentPC();
    void updateScrollModeIndicator();
    void updateDebuggerStateIndicator();
    uint16_t getNextCommandAddress(uint16_t currentAddress);
    uint16_t getPreviousCommandAddress(uint16_t currentAddress);

    // Keyboard navigation handlers
    void navigateUp();
    void navigateDown();
    void returnToCurrentPC();
    void toggleScrollMode();

public slots:
    void reset();
    void refresh();

signals:

private:
    Ui::DisassemblerWidget* ui;
    QThread* m_mainThread;
    DisassemblyTextEdit* m_disassemblyTextEdit;
    QLabel* m_scrollModeIndicator;  // Label to show current scroll mode
    QLabel* m_stateIndicator;       // Label to show debugger state (Active/Detached)

    uint16_t m_currentPC;                 // Current program counter
    uint16_t m_displayAddress;            // Address currently displayed at the top of the disassembly view
    QTextCharFormat m_pcHighlightFormat;  // Format for highlighting current PC
    ScrollMode m_scrollMode;              // Current scroll mode (byte or command)
    bool m_isActive;                      // Whether debugger is in active (paused) state

    DebuggerWindow* m_debuggerWindow;
};

#endif  // DISASSEMBLERWIDGET_H
