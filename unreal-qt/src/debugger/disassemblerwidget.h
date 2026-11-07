#pragma once

#ifndef DISASSEMBLERWIDGET_H
#define DISASSEMBLERWIDGET_H

#include <QAbstractScrollArea>
#include <QKeyEvent>
#include <QLabel>
#include <QObject>
#include <QWidget>
#include <QInputDialog>
#include <QRegularExpression>

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <optional>
#include <vector>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/disassembler/disassemblywindow.h"
#include "disassemblystrip.h"

QT_BEGIN_NAMESPACE
namespace Ui
{
class DisassemblerWidget;
}
QT_END_NAMESPACE

class DebuggerWindow;

/// A text line of the disassembly view: an instruction or `db` row, or the line of its own a long label takes
/// above its instruction
struct DisassemblyLine
{
    enum class Kind : uint8_t
    {
        Instruction,
        Data,   ///< a `db` row (a data range of the markup, or an instruction cut at a boundary)
        Label,  ///< a long label above the row at `address`
    };
    Kind kind = Kind::Instruction;
    uint16_t address = 0;  ///< the row's address (a label line: the row below it)
    bool breakpoint = false;
    bool pc = false;
    bool cursor = false;
    QChar mark;  ///< '1'..'5' (the lowest mark of the row), null without one
    QString bytes;
    QString label;
    QString mnemonic;
    QString comment;  ///< the markup comment, else the description of the block the row starts; PC: the runtime hints
    QString tip;      ///< the row's tooltip: the instruction with its addresses, the runtime hints, the whole comment
};

/// The disassembly rows painted in fixed character columns (docs/inprogress/2026-11-05-disasm-virtual-scroll §5):
/// only the lines shown, no document. The vertical scrollbar is the top row's address over 0000-FFFF; its actions,
/// the wheel, keys and clicks go to the DisassemblerWidget as signals, which lays out the lines again
class DisassemblyView : public QAbstractScrollArea
{
    Q_OBJECT
public:
    // Column widths for disassembly output (characters)
    static constexpr int BREAKPOINT_COL_WIDTH = 3;  // [●]
    static constexpr int MARK_COL_WIDTH = 1;        // 1-5
    static constexpr int ADDRESS_COL_WIDTH = 5;     // $XXXX
    static constexpr int BYTECODE_COL_WIDTH = 11;   // XX XX XX XX
    static constexpr int LABEL_COL_WIDTH = 15;      // LABEL_NAME:
    static constexpr int MNEMONIC_COL_WIDTH = 20;   // Instruction mnemonic: the least; the widest shown widens it
    static constexpr int MNEMONIC_COL_MAX = 44;     // ... up to this (a longer one pushes its own comment only)
    // Where the columns start: "[●]" + mark + "$XXXX: " + bytes + space + label + space + mnemonic
    static constexpr int ADDRESS_COL = BREAKPOINT_COL_WIDTH + MARK_COL_WIDTH;
    static constexpr int BYTECODE_COL = ADDRESS_COL + ADDRESS_COL_WIDTH + 2;
    static constexpr int LABEL_COL = BYTECODE_COL + BYTECODE_COL_WIDTH + 1;
    static constexpr int MNEMONIC_COL = LABEL_COL + LABEL_COL_WIDTH + 1;
    static constexpr int COMMENT_COL = MNEMONIC_COL + MNEMONIC_COL_WIDTH;  // the least: see mnemonicWidth()

    explicit DisassemblyView(QWidget* parent = nullptr);

    void setLines(std::vector<DisassemblyLine> lines);
    const std::vector<DisassemblyLine>& lines() const { return m_lines; }
    /// Shown while there are no lines (no machine)
    void setPlaceholder(const QString& text);
    /// The scrollbar at the top row's address (no action of its own)
    void setScrollAddress(uint16_t address);

    int lineHeight() const;
    /// Lines the viewport holds, one partly shown at the bottom included / whole lines only
    int lineCapacity() const;
    int fullLines() const;
    /// The line at a viewport point, -1 below the last one
    int lineAt(const QPoint& pos) const;
    /// A line as plain text, the columns as painted (copy)
    QString lineText(const DisassemblyLine& line) const;
    /// The mnemonic column's width now: the widest mnemonic of the lines shown, MNEMONIC_COL_WIDTH..MNEMONIC_COL_MAX
    int mnemonicWidth() const { return m_mnemonicWidth; }

signals:
    void keyUpPressed();
    void keyDownPressed();
    void pageUpPressed();
    void pageDownPressed();
    void enterPressed();
    void toggleScrollMode();
    void goToAddressRequested();
    void goToPcRequested();       ///< Home
    void backRequested();         ///< Backspace, Alt+Left, Cmd+[
    void forwardRequested();      ///< Alt+Right, Cmd+]
    void markSetRequested(int n); ///< Ctrl+1..5 (Cmd on macOS)
    void markGoRequested(int n);  ///< Alt+1..5
    void copyRequested();         ///< Ctrl+C (Cmd+C): the cursor row
    /// The wheel (over the rows or the scrollbar): rows to scroll, up negative
    void wheelRows(int rows);
    /// A press on a line: in the breakpoint column, or anywhere else (the cursor row)
    void breakpointClicked(int line);
    void lineClicked(int line);
    void contextMenuAt(int line, const QPoint& globalPos);
    /// A scrollbar action (QAbstractSlider::SliderAction) with the position it asks for
    void scrollBarAction(int action, int position);
    void resized();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    bool viewportEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    int charWidth() const;
    int textLeft() const;  ///< x of column 0 after the horizontal scroll
    void updateHorizontalRange();

    std::vector<DisassemblyLine> m_lines;
    QString m_placeholder;
    int m_hoverLine = -1;
    int m_mnemonicWidth = MNEMONIC_COL_WIDTH;
    int m_wheelAccumulator = 0;  ///< angle delta short of a row
    bool m_syncingScrollBar = false;
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
    /// Show the disassembly at PC again (Home; dbg.follow_pc); a jump of the address history
    void followPc();
    /// Enter: the cursor row's target - a JP / CALL / JR / DJNZ / RST address, JP (HL) through its register, RET's
    /// return address from the stack, else a 16-bit operand (LD HL,nn); a row without one shows PC again
    void followOperand();
    /// The address history (every jump: go to, follow, PC, a mark): back (Backspace, Alt+Left) and forward (Alt+Right)
    void historyBack();
    void historyForward();
    /// Marked addresses 1-5: Ctrl+N marks the cursor row (again: unmarks it), Alt+N shows it
    void setMark(int n);
    void goToMark(int n);
    /// The labels or the metadata switched (MetadataSettings, dbg.labels_toggle, dbg.metadata_*): the rows again
    void metadataSwitched();
    /// Byte / command scroll (B)
    void switchScrollMode();
    /// The address of the cursor row (a click, or the row kept across refreshes); published as the debugger's
    /// "disasm" cursor (actions::DebuggerCursors) for run.to_cursor
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

    void updateScrollModeIndicator();
    void updateDebuggerStateIndicator();
    void updateBankIndicator(uint16_t address);
    /// The machine's signals now: DOS (the TR-DOS latch), the ROM in 0000-3FFF, INT (asserted) / NMI (in service)
    void updateSignalIndicators();

    // Breakpoint methods
    void toggleBreakpointAtAddress(uint16_t address);
    bool hasBreakpointAtAddress(uint16_t address) const;

    // Navigation handlers
    /// The wheel, the scrollbar arrows and pages: rows through the strip (bytes in byte scroll)
    void scrollRows(int rows);
    void onScrollBarAction(int action, int position);
    void toggleScrollMode();
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
    DisassemblyView* m_view;
    QLabel* m_scrollModeIndicator;  // Label to show current scroll mode
    QLabel* m_stateIndicator;       // Label to show debugger state (Active/Detached)
    QLabel* m_bankIndicator;        // Label to show current memory bank
    QLabel* m_dosIndicator = nullptr;
    QLabel* m_romIndicator = nullptr;
    QLabel* m_intIndicator = nullptr;

    uint16_t m_currentPC;                 // Current program counter
    uint16_t m_displayAddress;            // Address currently displayed at the top of the disassembly view
    ScrollMode m_scrollMode;              // Current scroll mode (byte or command)
    bool m_isActive;                      // Whether debugger is in active (paused) state

    DebuggerWindow* m_debuggerWindow;
    bool m_isShuttingDown = false;  // Flag to block refreshes during shutdown

    /// A place of the address history: the top row and the cursor row
    struct ViewPlace
    {
        uint16_t top = 0;
        std::optional<uint16_t> cursor;
    };
    static constexpr size_t kHistoryDepth = 64;
    /// Shows `address` as a jump: the place left goes on the back history, the forward one is dropped
    void jumpTo(uint16_t address);
    void showPlace(const ViewPlace& place);
    ViewPlace currentPlace() const { return {m_displayAddress, m_cursorAddress}; }
    /// The instruction at `address`, decoded with the live registers (JP (HL), RET); false without a machine
    bool decodeAt(uint16_t address, DecodedInstruction& decoded);
    void showContextMenu(int line, const QPoint& globalPos);
    std::deque<ViewPlace> m_back;
    std::deque<ViewPlace> m_forward;
    std::array<std::optional<uint16_t>, 5> m_marks{};

    /// region <The rows: the strip (README §3.4) and the lines laid out from its top>
    /// Runs `body` with a window over the machine (labels off: under NoLabelsScope) after the strip is checked
    /// against it (rebuilt from the top address when dropped); false without a machine
    bool withWindow(const std::function<void(disasm::Window&, const disasm::Source&, uint64_t revision)>& body);
    /// The markup revision the strip is checked against
    uint64_t markupRevision();
    /// The row (label lines not counted) of the last layout that starts at `address`, -1 when none is shown
    int shownRowOf(uint16_t address) const;
    /// Lines from the strip's top for the view's height: rows decoded again with the live registers (runtime
    /// hints), long labels on lines of their own, PC, breakpoints, marks and the cursor; the scrollbar follows
    void layoutLines();
    void onLineClicked(int line);
    void publishCursor();
    void copyCursorLine();
    bool hasRows() const { return !m_view->lines().empty(); }
    DisassemblyStrip m_strip;
    int m_rowsShown = 0;  ///< rows (not label lines) wholly visible in the last layout
    std::optional<uint16_t> m_cursorAddress;
    /// endregion </The rows>
};

#endif  // DISASSEMBLERWIDGET_H
