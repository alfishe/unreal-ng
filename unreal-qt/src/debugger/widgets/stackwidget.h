#ifndef STACKWIDGET_H
#define STACKWIDGET_H

#include <QLabel>
#include <QThread>
#include <QWidget>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent>

#include <array>
#include <cstdint>
#include <vector>

QT_BEGIN_NAMESPACE
namespace Ui { class StackWidget; }
QT_END_NAMESPACE

class DebuggerWindow;
class Emulator;
class EmulatorContext;
class Memory;
class QClickableLabel;

/// The stack around SP: 9 words from SP-2 (the word just popped or about to be pushed) to SP+14. A click shows the
/// word in the disassembly, a double click in the memory view; a word that follows a CALL or RST is marked as a
/// return address with that instruction
class StackWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StackWidget(QWidget *parent = nullptr);
    virtual ~StackWidget() override;

    static constexpr int kEntries = 9;
    static constexpr int kFirstOffset = -2;  ///< the first row is SP-2

    /// The stack entries a double click follows into the memory view, for the pane cursor
    std::vector<QWidget*> cursorCells() const;

    /// "CALL 0D6B" / "RST 38" when the instruction ending right before `address` is a call that pushes it, else ""
    static QString ReturnHint(Memory& memory, uint16_t address);

    // Helper methods
protected:
    Emulator* getEmulator();
    EmulatorContext* getEmulatorContext();
    Memory* getMemory();

    // Context menu handling
    bool eventFilter(QObject* obj, QEvent* event) override;
    void showStackAddressContextMenu(int stackIndex, const QPoint& pos);

signals:
    void changeMemoryViewZ80Address(uint16_t addr);
    void changeMemoryViewAddress(uint8_t* addr, size_t size, uint16_t offset, uint16_t curaddr);
    void jumpToAddressInDisassembly(uint16_t addr);

    /// region <Event handlers / Slots>
public slots:
    void reset();
    void refresh();
    void prepareForShutdown();  // Block refreshes during shutdown
    /// region </Event handlers / Slots>

    /// region <Helper methods>
protected:
    /// The words at SP-2, SP, ... SP+14; false without a machine
    bool readStack(std::array<uint16_t, kEntries>& out);
    /// endregion </Helper methods>

private:
    Ui::StackWidget* ui;
    QThread* m_mainThread;
    DebuggerWindow* m_debuggerWindow;

    std::array<QLabel*, kEntries> m_offsets{};
    std::array<QClickableLabel*, kEntries> m_values{};
    std::array<QLabel*, kEntries> m_hints{};

    bool m_isShuttingDown = false;  // Flag to block refreshes during shutdown
};

#endif // STACKWIDGET_H
