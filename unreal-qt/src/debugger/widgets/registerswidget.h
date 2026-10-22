#ifndef REGISTERWIDGET_H
#define REGISTERWIDGET_H

#include <QThread>
#include <QWidget>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent>
#include <QLabel>

#include <vector>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/z80.h"

QT_BEGIN_NAMESPACE
namespace Ui { class RegistersWidget; }
QT_END_NAMESPACE

class DebuggerWindow;

class RegistersWidget : public QWidget
{
    Q_OBJECT
public:
    explicit RegistersWidget(QWidget *parent = nullptr);
    virtual ~RegistersWidget() override;

    void setZ80State(Z80State* state);
    /// The 16-bit value cells a double click follows into the memory view, for the pane cursor
    std::vector<QWidget*> cursorCells() const;

    // Helper methods
protected:
    Emulator* getEmulator();
    EmulatorContext* getEmulatorContext();
    Memory* getMemory();
    Z80Registers* getRegisters();
    
    // Context menu handling
    bool eventFilter(QObject* obj, QEvent* event) override;
    void setupContextMenus();
    void showRegisterContextMenu(QLabel* label, uint16_t address, const QPoint& pos);
    /// The register a value label shows ("BC", "AF'", "IR", "MEMPTR"), "" for none
    QString registerOfLabel(const QLabel* label) const;

    /// region <Event handlers / Slots>
public slots:
    void reset();
    void refresh();
    void prepareForShutdown();  // Block refreshes during shutdown

    void bc_doubleClicked();
    void de_doubleClicked();
    void hl_doubleClicked();
    void bc1_doubleClicked();
    void de1_doubleClicked();
    void hl1_doubleClicked();
    void sp_doubleClicked();
    void pc_doubleClicked();
    void ix_doubleClicked();
    void iy_doubleClicked();

    /// Ask for a new value of the register (a name Z80::FindRegister knows) and set it on a paused machine
    /// (Emulator::SetRegisterFromTool: a tool edit, refused in the recorded past of time travel)
    void editRegister(const QString& name);

    /// endregion </Event handlers / Slots>

signals:
    void changeMemoryViewZ80Address(uint16_t addr);
    void changeMemoryViewAddress(uint8_t* addr, size_t size, uint16_t offset, uint16_t curaddr);
    void jumpToAddressInDisassembly(uint16_t addr);
    /// A register was set from here: the other panes (disassembly at a new PC, stack) follow
    void registerEdited(const QString& name);

private:
    Ui::RegistersWidget* ui;
    QThread* m_mainThread;
    DebuggerWindow* m_debuggerWindow;

    Z80Registers* m_z80Registers;
    bool m_isShuttingDown = false;  // Flag to block refreshes during shutdown
};

#endif // REGISTERWIDGET_H
