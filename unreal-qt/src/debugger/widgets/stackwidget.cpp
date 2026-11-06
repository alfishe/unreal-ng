#include "stackwidget.h"

#include <cstdlib>

#include <QGridLayout>

#include "common/widgets/qclickablelabel.h"
#include "debugger/debuggerwindow.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "ui_stackwidget.h"

StackWidget::StackWidget(QWidget* parent) : QWidget(parent), ui(new Ui::StackWidget)
{
    // Instantiate all child widgets (UI form auto-generated)
    ui->setupUi(this);

    m_mainThread = QApplication::instance()->thread();

    m_debuggerWindow = DebuggerWindow::Of(parent);

    // One row per word: the offset from SP, the word (click: disassembly, double click: memory), the return hint;
    // the text in the debugger's panel font
    for (int i = 0; i < kEntries; i++)
    {
        const int offset = kFirstOffset + 2 * i;
        auto* offsetLabel = new QLabel(offset == 0 ? QStringLiteral("SP") : QStringLiteral("%1%2")
                                                                                .arg(offset < 0 ? '-' : '+')
                                                                                .arg(std::abs(offset), 0, 16)
                                                                                .toUpper(),
                                       this);
        offsetLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        offsetLabel->setStyleSheet(offset == 0 ? QStringLiteral("QLabel { font-weight: bold; }")
                                               : QStringLiteral("QLabel { color: #808080; }"));

        auto* value = new QClickableLabel(this);
        value->setText(QStringLiteral("----"));
        value->setCursor(Qt::PointingHandCursor);
        value->setToolTip(tr("Click: show it in the disassembly; double click: in the memory view; right click: menu"));
        if (offset == 0)
            value->setStyleSheet(QStringLiteral("QLabel { font-weight: bold; }"));
        value->installEventFilter(this);

        auto* hint = new QLabel(this);
        hint->setStyleSheet(QStringLiteral("QLabel { color: #808080; }"));

        ui->stackGrid->addWidget(offsetLabel, i, 0);
        ui->stackGrid->addWidget(value, i, 1);
        ui->stackGrid->addWidget(hint, i, 2);
        m_offsets[static_cast<size_t>(i)] = offsetLabel;
        m_values[static_cast<size_t>(i)] = value;
        m_hints[static_cast<size_t>(i)] = hint;

        connect(value, &QClickableLabel::clicked, this, [this, i] {
            std::array<uint16_t, kEntries> words{};
            if (readStack(words))
                emit jumpToAddressInDisassembly(words[static_cast<size_t>(i)]);
        });
        connect(value, &QClickableLabel::doubleClicked, this, [this, i] {
            std::array<uint16_t, kEntries> words{};
            if (readStack(words))
                emit changeMemoryViewZ80Address(words[static_cast<size_t>(i)]);
        });
    }
    ui->stackGrid->setColumnStretch(2, 1);
}

StackWidget::~StackWidget() {}

// Helper methods
Emulator* StackWidget::getEmulator()
{
    return m_debuggerWindow->getEmulator();
}

EmulatorContext* StackWidget::getEmulatorContext()
{
    Emulator* emulator = m_debuggerWindow->getEmulator();
    return emulator ? emulator->GetContext() : nullptr;
}

Memory* StackWidget::getMemory()
{
    Emulator* emulator = m_debuggerWindow->getEmulator();
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    return context ? context->pMemory : nullptr;
}

bool StackWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::ContextMenu)
    {
        for (int i = 0; i < kEntries; i++)
            if (obj == m_values[static_cast<size_t>(i)])
            {
                showStackAddressContextMenu(i, static_cast<QContextMenuEvent*>(event)->globalPos());
                return true;
            }
    }

    // Pass the event to the parent class
    return QWidget::eventFilter(obj, event);
}

void StackWidget::showStackAddressContextMenu(int stackIndex, const QPoint& pos)
{
    std::array<uint16_t, kEntries> words{};
    if (!readStack(words))
        return;
    const uint16_t address = words[static_cast<size_t>(stackIndex)];

    QMenu contextMenu("Stack Address Actions", this);
    QAction* jumpToAction = contextMenu.addAction(tr("Jump to in Disassembly"));
    QAction* showInMemoryAction = contextMenu.addAction(tr("Show in Memory View"));
    connect(jumpToAction, &QAction::triggered, [this, address]() { emit jumpToAddressInDisassembly(address); });
    connect(showInMemoryAction, &QAction::triggered, [this, address]() { emit changeMemoryViewZ80Address(address); });

    // Show the menu at the cursor position
    contextMenu.exec(pos);
}

void StackWidget::reset()
{
    // Clear labels without reading from emulator
    // (reset is called when emulator is being released, so don't access it)
    for (size_t i = 0; i < m_values.size(); i++)
    {
        m_values[i]->setText(QStringLiteral("----"));
        m_hints[i]->clear();
    }
    update();
}

void StackWidget::refresh()
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
        return;
    }

    std::array<uint16_t, kEntries> words{};
    Memory* memory = getMemory();
    if (!memory || !readStack(words))
    {
        reset();
        return;
    }
    for (size_t i = 0; i < words.size(); i++)
    {
        m_values[i]->setText(QStringLiteral("$%1").arg(words[i], 4, 16, QLatin1Char('0')).toUpper());
        const QString hint = ReturnHint(*memory, words[i]);
        m_hints[i]->setText(hint.isEmpty() ? QString() : QStringLiteral("<- %1").arg(hint));
        m_hints[i]->setToolTip(hint.isEmpty() ? QString()
                                              : tr("A return address: the %1 that ends right before it pushes it")
                                                    .arg(hint.section(' ', 0, 0)));
    }
    update();
}

QString StackWidget::ReturnHint(Memory& memory, uint16_t address)
{
    // CALL nn (CD) and CALL cc,nn (C4 CC D4 DC E4 EC F4 FC) end 3 bytes before the return address
    const uint8_t call = memory.DirectReadFromZ80Memory(static_cast<uint16_t>(address - 3));
    if (call == 0xCD || (call & 0xC7) == 0xC4)
    {
        const uint16_t target = static_cast<uint16_t>(memory.DirectReadFromZ80Memory(static_cast<uint16_t>(address - 2)) |
                                                      (memory.DirectReadFromZ80Memory(static_cast<uint16_t>(address - 1)) << 8));
        return QStringLiteral("CALL %1").arg(target, 4, 16, QLatin1Char('0')).toUpper();
    }
    // RST n one byte before; RST 38 (FF) is left out: FF fills empty memory, a word after it is rarely a return
    const uint8_t rst = memory.DirectReadFromZ80Memory(static_cast<uint16_t>(address - 1));
    if ((rst & 0xC7) == 0xC7 && rst != 0xFF)
        return QStringLiteral("RST %1").arg(rst & 0x38, 2, 16, QLatin1Char('0')).toUpper();
    return {};
}

std::vector<QWidget*> StackWidget::cursorCells() const
{
    return {m_values.begin(), m_values.end()};
}

/// region <Helper methods>
bool StackWidget::readStack(std::array<uint16_t, kEntries>& out)
{
    Memory* memory = getMemory();
    EmulatorContext* context = getEmulatorContext();
    if (!memory || !context || !context->pCore)
        return false;
    Z80State* z80 = context->pCore->GetZ80();
    uint16_t sp = static_cast<uint16_t>(z80->Z80Registers::sp + kFirstOffset);
    for (uint16_t& word : out)
    {
        const uint8_t loByte = memory->DirectReadFromZ80Memory(sp++);
        const uint16_t hiByte = memory->DirectReadFromZ80Memory(sp++);
        word = static_cast<uint16_t>((hiByte << 8) | loByte);
    }
    return true;
}
/// endregion </Helper methods>

void StackWidget::prepareForShutdown()
{
    qDebug() << "StackWidget::prepareForShutdown()";
    m_isShuttingDown = true;
}
