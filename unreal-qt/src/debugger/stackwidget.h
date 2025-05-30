#ifndef STACKWIDGET_H
#define STACKWIDGET_H

#include <QLabel>
#include <QThread>
#include <QWidget>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent>

QT_BEGIN_NAMESPACE
namespace Ui { class StackWidget; }
QT_END_NAMESPACE

class DebuggerWindow;
class Emulator;
class EmulatorContext;
class Memory;

class StackWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StackWidget(QWidget *parent = nullptr);
    virtual ~StackWidget() override;

    // Helper methods
protected:
    Emulator* getEmulator();
    EmulatorContext* getEmulatorContext();
    Memory* getMemory();
    
    // Context menu handling
    bool eventFilter(QObject* obj, QEvent* event) override;
    void setupContextMenus();
    void showStackAddressContextMenu(QLabel* label, int stackIndex, const QPoint& pos);

signals:
    void changeMemoryViewZ80Address(uint16_t addr);
    void changeMemoryViewAddress(uint8_t* addr, size_t size, uint16_t offset, uint16_t curaddr);
    void jumpToAddressInDisassembly(uint16_t addr);

    /// region <Event handlers / Slots>
public slots:
    void reset();
    void refresh();

    void sp0Value_doubleClicked();
    void sp1Value_doubleClicked();
    void sp2Value_doubleClicked();
    void sp3Value_doubleClicked();

    /// region </Event handlers / Slots>

    /// region <Helper methods>
protected:
    void readStackIntoArray(uint16_t* outArray, uint16_t depth);
    /// endregion </Helper methods>

private:
    Ui::StackWidget* ui;
    QThread* m_mainThread;
    DebuggerWindow* m_debuggerWindow;

    QLabel* sp0Value;
    QLabel* sp1Value;
    QLabel* sp2Value;
    QLabel* sp3Value;
};

#endif // STACKWIDGET_H
