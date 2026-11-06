#include "registerswidget.h"

#include <initializer_list>
#include <utility>

#include <QInputDialog>
#include <QMessageBox>
#include <QMetaObject>

#include "common/widgets/qclickablelabel.h"
#include "debugger/debuggerwindow.h"
#include "emulator/memory/memory.h"
#include "emulator/remotecontrolguard.h"
#include "ui_registerswidget.h"

RegistersWidget::RegistersWidget(QWidget *parent) : QWidget(parent), ui(new Ui::RegistersWidget)
{
    // Instantiate all child widgets (UI form auto-generated)
    ui->setupUi(this);

    //memset(&m_z80Registers, 0x00, sizeof(Z80Registers));

    m_mainThread = QApplication::instance()->thread();

    m_debuggerWindow = DebuggerWindow::Of(parent);

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
    buildInterruptAndFlagCells();
}

void RegistersWidget::buildInterruptAndFlagCells()
{
    // Flags: one cell per bit of F, a click flips it (the form's text cell stays hidden)
    static const char* const kFlagNames[8] = {"S", "Z", "5", "H", "3", "P", "N", "C"};
    static const char* const kFlagTips[8] = {"S: sign", "Z: zero", "5 (Y): bit 5 of the result", "H: half carry",
                                             "3 (X): bit 3 of the result", "P/V: parity / overflow", "N: subtract",
                                             "C: carry"};
    ui->valFlags->hide();
    int at = ui->horizontalLayout_21->indexOf(ui->valFlags);
    for (int i = 0; i < 8; i++)
    {
        auto* cell = new QClickableLabel(this);
        cell->setText(QString::fromLatin1(kFlagNames[i]));
        cell->setAlignment(Qt::AlignCenter);
        cell->setMinimumWidth(cell->fontMetrics().horizontalAdvance(QStringLiteral("W")) + 2);
        cell->setCursor(Qt::PointingHandCursor);
        cell->setToolTip(tr("%1 - click: flip it (a paused machine)").arg(QString::fromLatin1(kFlagTips[i])));
        const int bit = 7 - i;
        connect(cell, &QClickableLabel::clicked, this, [this, bit] { toggleFlag(bit); });
        ui->horizontalLayout_21->insertWidget(++at, cell);
        m_flagCells[static_cast<size_t>(i)] = cell;
    }

    // Interrupts: the mode, IFF1 and IFF2 (a click flips one), the address an INT would go to (a click shows it)
    at = ui->horizontalLayout_20->indexOf(ui->valINT);
    for (int n = 1; n <= 2; n++)
    {
        auto* cell = new QClickableLabel(this);
        cell->setText(QStringLiteral("IFF%1").arg(n));
        cell->setCursor(Qt::PointingHandCursor);
        cell->setToolTip(n == 1 ? tr("IFF1: maskable interrupts are taken - click: flip it (a paused machine)")
                                : tr("IFF2: the copy NMI keeps for RETN, read by LD A,I / LD A,R - click: flip it"));
        connect(cell, &QClickableLabel::clicked, this, [this, n] { toggleIff(n); });
        ui->horizontalLayout_20->insertWidget(++at, cell);
        m_iffCells[static_cast<size_t>(n - 1)] = cell;
    }
    m_isrCell = new QClickableLabel(this);
    m_isrCell->setCursor(Qt::PointingHandCursor);
    m_isrCell->setStyleSheet(QStringLiteral("QLabel { color: #2060C0; text-decoration: underline; }"));
    connect(m_isrCell, &QClickableLabel::clicked, this, [this] {
        if (m_z80Registers)
            emit jumpToAddressInDisassembly(isrAddress());
    });
    ui->horizontalLayout_20->insertWidget(++at, m_isrCell);
}

uint16_t RegistersWidget::isrAddress() const
{
    if (!m_z80Registers || m_z80Registers->im != 2)
        return 0x0038;
    // IM2 reads its vector at I * 256 + the bus byte; the ULA leaves the bus at FF (a device may drive another one)
    Memory* memory = const_cast<RegistersWidget*>(this)->getMemory();
    if (!memory)
        return 0x0038;
    const uint16_t table = static_cast<uint16_t>((m_z80Registers->i << 8) | 0xFF);
    return static_cast<uint16_t>(memory->DirectReadFromZ80Memory(table) |
                                 (memory->DirectReadFromZ80Memory(static_cast<uint16_t>(table + 1)) << 8));
}

bool RegistersWidget::setRegister(const QString& name, uint16_t value)
{
    if (RemoteControlGuard::Refuse(this))  // an automation session holds the machine
        return false;
    Emulator* emulator = getEmulator();
    if (emulator == nullptr || m_z80Registers == nullptr)
        return false;
    if (!emulator->IsPaused())
    {
        QMessageBox::information(this, tr("Edit %1").arg(name), tr("Pause the machine to edit its registers."));
        return false;
    }
    std::string refused;
    if (!emulator->SetRegisterFromTool("qt registers", name.toStdString(), value, &refused))
    {
        QMessageBox::warning(this, tr("Edit %1").arg(name),
                             refused.empty() ? tr("%1 was not changed.").arg(name) : QString::fromStdString(refused));
        return false;
    }
    refresh();
    emit registerEdited(name);
    return true;
}

void RegistersWidget::toggleFlag(int bit)
{
    if (m_z80Registers)
        setRegister(QStringLiteral("F"), static_cast<uint16_t>(m_z80Registers->f ^ (1u << bit)));
}

void RegistersWidget::toggleIff(int n)
{
    if (m_z80Registers)
        setRegister(QStringLiteral("IFF%1").arg(n), (n == 1 ? m_z80Registers->iff1 : m_z80Registers->iff2) ? 0 : 1);
}

RegistersWidget::~RegistersWidget()
{

}

///
/// \brief RegistersWidget::setZ80State set actual values for Z80 registers from Core state
/// \param state Live pointer into the bound emulator's Z80, or nullptr to detach.
///
/// The pointer is owned by the emulator and dies with it, so the caller must pass
/// nullptr on unbind and a fresh pointer for every newly bound emulator; refresh()
/// otherwise dereferences freed memory after a Stop / Start cycle.
///
void RegistersWidget::setZ80State(Z80State* state)
{
    m_z80Registers = static_cast<Z80Registers*>(state);
}

// Helper methods
Emulator* RegistersWidget::getEmulator()
{
    return m_debuggerWindow->getEmulator();
}

EmulatorContext* RegistersWidget::getEmulatorContext()
{
    Emulator* emu = m_debuggerWindow->getEmulator();
    return emu ? emu->GetContext() : nullptr;
}

Memory* RegistersWidget::getMemory()
{
    Emulator* emu = m_debuggerWindow->getEmulator();
    if (!emu) return nullptr;
    EmulatorContext* ctx = emu->GetContext();
    return ctx ? ctx->pMemory : nullptr;
}

Z80Registers* RegistersWidget::getRegisters()
{
    Emulator* emu = m_debuggerWindow->getEmulator();
    if (!emu) return nullptr;
    EmulatorContext* ctx = emu->GetContext();
    if (!ctx || !ctx->pCore) return nullptr;
    return ctx->pCore->GetZ80();
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
    // Not memory pointers, but editable
    ui->valAF->installEventFilter(this);
    ui->valAF1->installEventFilter(this);
    ui->valIR->installEventFilter(this);
    ui->valWZ->installEventFilter(this);
    for (QLabel* label : std::initializer_list<QLabel*>{ui->valAF, ui->valBC, ui->valDE, ui->valHL, ui->valAF1,
                                                        ui->valBC1, ui->valDE1, ui->valHL1, ui->valSP, ui->valPC, ui->valIX, ui->valIY, ui->valIR, ui->valWZ})
        label->setToolTip(tr("Right click: edit %1 (on a paused machine), follow it in the disassembly or memory")
                              .arg(registerOfLabel(label)));
}

QString RegistersWidget::registerOfLabel(const QLabel* label) const
{
    const std::pair<const QLabel*, const char*> names[] = {
        {ui->valAF, "AF"},   {ui->valBC, "BC"},   {ui->valDE, "DE"},   {ui->valHL, "HL"},   {ui->valAF1, "AF'"},
        {ui->valBC1, "BC'"}, {ui->valDE1, "DE'"}, {ui->valHL1, "HL'"}, {ui->valSP, "SP"},   {ui->valPC, "PC"},
        {ui->valIX, "IX"},   {ui->valIY, "IY"},   {ui->valIR, "IR"},   {ui->valWZ, "MEMPTR"}};
    for (const auto& [candidate, name] : names)
        if (candidate == label)
            return QString::fromLatin1(name);
    return {};
}

void RegistersWidget::editRegister(const QString& name)
{
    if (RemoteControlGuard::Refuse(this))  // an automation session holds the machine
        return;
    Emulator* emulator = getEmulator();
    const Z80::RegisterInfo* info = Z80::FindRegister(name.toStdString());
    if (emulator == nullptr || info == nullptr || m_z80Registers == nullptr)
        return;
    if (!emulator->IsPaused())
    {
        QMessageBox::information(this, tr("Edit %1").arg(name), tr("Pause the machine to edit its registers."));
        return;
    }
    const int digits = info->is16bit ? 4 : 2;
    const unsigned maximum = info->maxValue != 0 ? info->maxValue : (info->is16bit ? 0xFFFFu : 0xFFu);
    const uint16_t now = info->getter(static_cast<const Z80State*>(m_z80Registers));
    const QString current = QStringLiteral("%1").arg(now, digits, 16, QLatin1Char('0')).toUpper();
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Edit %1").arg(name),
                                               tr("New value of %1 (hex; #, $ or 0x prefix optional, or a decimal "
                                                  "number with a d suffix; at most %2):")
                                                   .arg(name)
                                                   .arg(maximum, digits, 16, QLatin1Char('0')),
                                               QLineEdit::Normal, current, &ok)
                             .trimmed();
    if (!ok || text.isEmpty())
        return;
    QString digitsText = text;
    int base = 16;
    if (digitsText.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
        digitsText = digitsText.mid(2);
    else if (digitsText.startsWith(QLatin1Char('#')) || digitsText.startsWith(QLatin1Char('$')))
        digitsText = digitsText.mid(1);
    else if (digitsText.endsWith(QLatin1Char('d'), Qt::CaseInsensitive))
    {
        digitsText.chop(1);
        base = 10;
    }
    bool parsed = false;
    const uint value = digitsText.toUInt(&parsed, base);
    if (!parsed || value > maximum)
    {
        QMessageBox::warning(this, tr("Edit %1").arg(name),
                             tr("'%1' is not a value %2 can hold (0..%3 hex).")
                                 .arg(text, name)
                                 .arg(maximum, 0, 16));
        return;
    }
    std::string refused;
    if (!emulator->SetRegisterFromTool("qt registers", name.toStdString(), static_cast<uint16_t>(value), &refused))
    {
        QMessageBox::warning(this, tr("Edit %1").arg(name),
                             refused.empty() ? tr("%1 was not changed.").arg(name) : QString::fromStdString(refused));
        return;
    }
    refresh();
    emit registerEdited(name);
}

bool RegistersWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::ContextMenu)
    {
        QContextMenuEvent* contextEvent = static_cast<QContextMenuEvent*>(event);
        QLabel* label = qobject_cast<QLabel*>(obj);
        
        if (label)
        {
            // Guard against null m_z80Registers
            if (!m_z80Registers)
                return QWidget::eventFilter(obj, event);
            
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
            else if (label == ui->valAF)
                address = m_z80Registers->af;
            else if (label == ui->valAF1)
                address = m_z80Registers->alt.af;
            else if (label == ui->valIR)
                address = static_cast<uint16_t>((m_z80Registers->i << 8) | Z80::RegisterR(m_z80Registers));
            else if (label == ui->valWZ)
                address = m_z80Registers->memptr;
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

    // Edit the register itself (a paused machine)
    const QString name = registerOfLabel(label);
    if (!name.isEmpty())
    {
        QAction* editAction = contextMenu.addAction(tr("Edit %1...").arg(name));
        connect(editAction, &QAction::triggered, this, [this, name]() { editRegister(name); });
        contextMenu.addSeparator();
    }

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
        ui->valWZ->setText("");
        ui->valHALT->setText("");
        ui->valFlags->setText("");
        for (QClickableLabel* cell : m_flagCells)
            cell->setStyleSheet(QString());
        for (QClickableLabel* cell : m_iffCells)
            cell->setStyleSheet(QString());
        m_isrCell->setText(QString());

        update();
    }
}

void RegistersWidget::refresh()
{
    // Block all operations during shutdown
    if (m_isShuttingDown)
    {
        return;
    }

    QThread* currentThread = QThread::currentThread();

    // Ensure GUI update is in main thread
    if (currentThread != m_mainThread)
    {
        QMetaObject::invokeMethod(this, "refresh", Qt::QueuedConnection);
    }
    else
    {
        // Guard against null m_z80Registers
        if (!m_z80Registers)
        {
            reset();
            return;
        }
        
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

        ui->valIR->setText(QStringLiteral("%1").arg((m_z80Registers->i << 8) | Z80::RegisterR(m_z80Registers), 4, 16, QLatin1Char('0')).toUpper());
        ui->valT->setText(QStringLiteral("%1").arg(m_z80Registers->t, 4, 16, QLatin1Char('0')).toUpper());
        // Interrupt mode, the enable flip-flops (set: bold, clear: gray) and where an INT would go
        ui->valINT->setText(QStringLiteral("IM%1").arg(m_z80Registers->im));
        const QString on = QStringLiteral("QLabel { font-weight: bold; }");
        const QString off = QStringLiteral("QLabel { color: #A0A0A0; }");
        m_iffCells[0]->setStyleSheet(m_z80Registers->iff1 ? on : off);
        m_iffCells[1]->setStyleSheet(m_z80Registers->iff2 ? on : off);
        const uint16_t isr = isrAddress();
        m_isrCell->setText(QStringLiteral("ISR %1").arg(isr, 4, 16, QLatin1Char('0')).toUpper());
        m_isrCell->setToolTip(m_z80Registers->im == 2
                                  ? tr("IM2: the word at %1FF (the bus floats to FF; a device may drive another "
                                       "vector) - click: show it in the disassembly")
                                        .arg(m_z80Registers->i, 2, 16, QLatin1Char('0'))
                                        .toUpper()
                                  : tr("IM%1: RST #38 - click: show it in the disassembly").arg(m_z80Registers->im));
        for (size_t i = 0; i < m_flagCells.size(); i++)
            m_flagCells[i]->setStyleSheet((m_z80Registers->f >> (7 - i)) & 1 ? on : off);
        ui->valWZ->setText(QStringLiteral("%1").arg(m_z80Registers->memptr, 4, 16, QLatin1Char('0')).toUpper());
        ui->valHALT->setText(m_z80Registers->halted ? QStringLiteral("yes") : QStringLiteral("no"));
        ui->valFlags->setText(flagString);

        update();
    }
}

std::vector<QWidget*> RegistersWidget::cursorCells() const
{
    return {ui->valBC, ui->valDE, ui->valHL, ui->valBC1, ui->valDE1, ui->valHL1,
            ui->valSP, ui->valPC, ui->valIX, ui->valIY};
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

void RegistersWidget::prepareForShutdown()
{
    qDebug() << "RegistersWidget::prepareForShutdown()";
    m_isShuttingDown = true;
}