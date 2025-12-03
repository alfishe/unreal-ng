#include "registerswidget.h"
#include "ui_registerswidget.h"

#include <cstring>
#include <QMetaObject>
#include "debuggerwindow.h"

RegistersWidget::RegistersWidget(QWidget *parent) : QWidget(parent), ui(new Ui::RegistersWidget)
{
    // Instantiate all child widgets (UI form auto-generated)
    ui->setupUi(this);

    //memset(&m_z80Registers, 0x00, sizeof(Z80Registers));

    m_mainThread = QApplication::instance()->thread();

    m_debuggerWindow = static_cast<DebuggerWindow*>(parent);

    // Subscribe to double clicks on register values
    connect(ui->valBC, SIGNAL(doubleClicked()), this, SLOT(bc_doubleClicked()));
    connect(ui->valDE, SIGNAL(doubleClicked()), this, SLOT(de_doubleClicked()));
    connect(ui->valHL, SIGNAL(doubleClicked()), this, SLOT(hl_doubleClicked()));
    connect(ui->valBC1, SIGNAL(doubleClicked()), this, SLOT(bc1_doubleClicked()));
    connect(ui->valDE1, SIGNAL(doubleClicked()), this, SLOT(de1_doubleClicked()));
    connect(ui->valHL1, SIGNAL(doubleClicked()), this, SLOT(hl1_doubleClicked()));
    connect(ui->valSP, SIGNAL(doubleClicked()), this, SLOT(sp_doubleClicked()));
    connect(ui->valPC, SIGNAL(doubleClicked()), this, SLOT(pc_doubleClicked()));
    connect(ui->valIX, SIGNAL(doubleClicked()), this, SLOT(ix_doubleClicked()));
    connect(ui->valIY, SIGNAL(doubleClicked()), this, SLOT(iy_doubleClicked()));
    
    // Set up context menus for register values
    setupContextMenus();
}

RegistersWidget::~RegistersWidget()
{

}

///
/// \brief RegistersWidget::setZ80State set actual values for Z80 registers from Core state
/// \param state
///
void RegistersWidget::setZ80State(Z80State* state)
{
    if (state != nullptr)
    {
        m_z80Registers = static_cast<Z80Registers*>(state);
        //memcpy(&m_z80Registers, static_cast<Z80Registers*>(state), sizeof(Z80Registers));
    }
}

// Helper methods
Emulator* RegistersWidget::getEmulator()
{
    return m_debuggerWindow->getEmulator();
}

EmulatorContext* RegistersWidget::getEmulatorContext()
{
    return m_debuggerWindow->getEmulator()->GetContext();
}

Memory* RegistersWidget::getMemory()
{
    return m_debuggerWindow->getEmulator()->GetContext()->pMemory;
}

Z80Registers* RegistersWidget::getRegisters()
{
    return m_debuggerWindow->getEmulator()->GetContext()->pCore->GetZ80();
}

void RegistersWidget::setupContextMenus()
{
    // Install event filter on all register value labels
    ui->valBC->installEventFilter(this);
    ui->valDE->installEventFilter(this);
    ui->valHL->installEventFilter(this);
    ui->valBC1->installEventFilter(this);
    ui->valDE1->installEventFilter(this);
    ui->valHL1->installEventFilter(this);
    ui->valSP->installEventFilter(this);
    ui->valPC->installEventFilter(this);
    ui->valIX->installEventFilter(this);
    ui->valIY->installEventFilter(this);
}

bool RegistersWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::ContextMenu)
    {
        QContextMenuEvent* contextEvent = static_cast<QContextMenuEvent*>(event);
        QLabel* label = qobject_cast<QLabel*>(obj);
        
        if (label)
        {
            uint16_t address = 0;
            
            // Determine which register was right-clicked
            if (label == ui->valBC)
                address = m_z80Registers->bc;
            else if (label == ui->valDE)
                address = m_z80Registers->de;
            else if (label == ui->valHL)
                address = m_z80Registers->hl;
            else if (label == ui->valBC1)
                address = m_z80Registers->alt.bc;
            else if (label == ui->valDE1)
                address = m_z80Registers->alt.de;
            else if (label == ui->valHL1)
                address = m_z80Registers->alt.hl;
            else if (label == ui->valSP)
                address = m_z80Registers->sp;
            else if (label == ui->valPC)
                address = m_z80Registers->pc;
            else if (label == ui->valIX)
                address = m_z80Registers->ix;
            else if (label == ui->valIY)
                address = m_z80Registers->iy;
            else
                return QWidget::eventFilter(obj, event);
            
            // Show context menu for this register
            showRegisterContextMenu(label, address, contextEvent->globalPos());
            return true;
        }
    }
    
    // Pass the event to the parent class
    return QWidget::eventFilter(obj, event);
}

void RegistersWidget::showRegisterContextMenu(QLabel* label, uint16_t address, const QPoint& pos)
{
    QMenu contextMenu("Register Actions", this);
    
    // Create actions
    QAction* jumpToAction = new QAction("Jump to in Disassembly", &contextMenu);
    QAction* showInMemoryAction = new QAction("Show in Memory View", &contextMenu);
    
    // Add actions to menu
    contextMenu.addAction(jumpToAction);
    contextMenu.addAction(showInMemoryAction);
    
    // Connect actions
    connect(jumpToAction, &QAction::triggered, [this, address]() {
        emit jumpToAddressInDisassembly(address);
    });
    
    connect(showInMemoryAction, &QAction::triggered, [this, address]() {
        emit changeMemoryViewZ80Address(address);
    });
    
    // Show the menu at the cursor position
    contextMenu.exec(pos);
}

/// region <Event handlers / Slots>

void RegistersWidget::reset()
{
    QThread* currentThread = QThread::currentThread();

    // Ensure GUI update is in main thread
    if (currentThread != m_mainThread)
    {
        QMetaObject::invokeMethod(this, "reset", Qt::QueuedConnection);
    }
    else
    {
        ui->valAF->setText("");
        ui->valBC->setText("");
        ui->valDE->setText("");
        ui->valHL->setText("");

        ui->valAF1->setText("");
        ui->valBC1->setText("");
        ui->valDE1->setText("");
        ui->valHL1->setText("");

        ui->valSP->setText("");
        ui->valPC->setText("");
        ui->valIX->setText("");
        ui->valIY->setText("");

        ui->valIR->setText("");
        ui->valT->setText("");
        ui->valINT->setText("");
        ui->valFlags->setText("");

        update();
    }
}

void RegistersWidget::refresh()
{
    QThread* currentThread = QThread::currentThread();

    // Ensure GUI update is in main thread
    if (currentThread != m_mainThread)
    {
        QMetaObject::invokeMethod(this, "refresh", Qt::QueuedConnection);
    }
    else
    {
        QString flagString = Z80::DumpFlags(m_z80Registers->f).c_str();

        ui->valAF->setText(QStringLiteral("%1").arg(m_z80Registers->af, 4, 16, QLatin1Char('0')).toUpper());
        ui->valBC->setText(QStringLiteral("%1").arg(m_z80Registers->bc, 4, 16, QLatin1Char('0')).toUpper());
        ui->valDE->setText(QStringLiteral("%1").arg(m_z80Registers->de, 4, 16, QLatin1Char('0')).toUpper());
        ui->valHL->setText(QStringLiteral("%1").arg(m_z80Registers->hl, 4, 16, QLatin1Char('0')).toUpper());

        ui->valAF1->setText(QStringLiteral("%1").arg(m_z80Registers->alt.af, 4, 16, QLatin1Char('0')).toUpper());
        ui->valBC1->setText(QStringLiteral("%1").arg(m_z80Registers->alt.bc, 4, 16, QLatin1Char('0')).toUpper());
        ui->valDE1->setText(QStringLiteral("%1").arg(m_z80Registers->alt.de, 4, 16, QLatin1Char('0')).toUpper());
        ui->valHL1->setText(QStringLiteral("%1").arg(m_z80Registers->alt.hl, 4, 16, QLatin1Char('0')).toUpper());

        ui->valSP->setText(QStringLiteral("%1").arg(m_z80Registers->sp, 4, 16, QLatin1Char('0')).toUpper());
        ui->valPC->setText(QStringLiteral("%1").arg(m_z80Registers->pc, 4, 16, QLatin1Char('0')).toUpper());
        ui->valIX->setText(QStringLiteral("%1").arg(m_z80Registers->ix, 4, 16, QLatin1Char('0')).toUpper());
        ui->valIY->setText(QStringLiteral("%1").arg(m_z80Registers->iy, 4, 16, QLatin1Char('0')).toUpper());

        ui->valIR->setText(QStringLiteral("%1").arg(m_z80Registers->ir_, 4, 16, QLatin1Char('0')).toUpper());
        ui->valT->setText(QStringLiteral("%1").arg(m_z80Registers->t, 4, 16, QLatin1Char('0')).toUpper());
        ui->valINT->setText(QStringLiteral("%1").arg(m_z80Registers->im, 2, 16, QLatin1Char('0')).toUpper());
        ui->valFlags->setText(flagString);

        update();
    }
}

void RegistersWidget::bc_doubleClicked()
{
    emit changeMemoryViewZ80Address(m_z80Registers->bc);
}

void RegistersWidget::de_doubleClicked()
{
    emit changeMemoryViewZ80Address(m_z80Registers->de);
}

void RegistersWidget::hl_doubleClicked()
{
    emit changeMemoryViewZ80Address(m_z80Registers->hl);
}

void RegistersWidget::bc1_doubleClicked()
{
    emit changeMemoryViewZ80Address(m_z80Registers->alt.bc);
}

void RegistersWidget::de1_doubleClicked()
{
    emit changeMemoryViewZ80Address(m_z80Registers->alt.de);
}

void RegistersWidget::hl1_doubleClicked()
{
    emit changeMemoryViewZ80Address(m_z80Registers->alt.hl);
}

void RegistersWidget::sp_doubleClicked()
{
    emit changeMemoryViewZ80Address(m_z80Registers->sp);
}

void RegistersWidget::pc_doubleClicked()
{
    emit changeMemoryViewZ80Address(m_z80Registers->pc);
}

void RegistersWidget::ix_doubleClicked()
{
    emit changeMemoryViewZ80Address(m_z80Registers->ix);
}

void RegistersWidget::iy_doubleClicked()
{
    emit changeMemoryViewZ80Address(m_z80Registers->iy);
}

/// endregion </Event handlers / Slots>