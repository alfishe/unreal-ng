#include "disassemblerwidget.h"
#include "emulator/remotecontrolguard.h"

#include <algorithm>
#include <cstdlib>

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QThread>
#include <QToolTip>
#include <QVBoxLayout>

#include "common/dumphelper.h"
#include "common/stringhelper.h"
#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/markup/memorymarkup.h"
#include "debuggerwindow.h"
#include "metadatasettings.h"
#include "emulator/actions/steppingactions.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/rom.h"
#include "ui_disassemblerwidget.h"

namespace
{
// Row backgrounds: the cursor row under PC and breakpoints; the hover line over all of them
const QColor kCursorBackground(80, 140, 255, 60);
const QColor kPcBackground(255, 255, 0, 100);
const QColor kBreakpointBackground(255, 0, 0, 100);
const QColor kHoverBackground(0, 0, 0, 16);
// Column colours
const QColor kBreakpointColor(Qt::red);
const QColor kMarkColor(0xE0, 0x70, 0x00);
const QColor kAddressColor(0x00, 0x80, 0x00);   // Dark green for addresses
const QColor kBytesColor(0x00, 0x00, 0xFF);     // Blue for byte codes
const QColor kLabelColor(0x8A, 0x2B, 0xE2);     // Blue violet for labels
const QColor kMnemonicColor(0x00, 0x00, 0x00);  // Black for mnemonics
const QColor kCommentColor(0x80, 0x80, 0x80);   // Gray for comments
const QColor kDataColor(0xA0, 0x80, 0x60);      // Muted brown: `db` rows (bytes and mnemonic)

// An operand the disassembler named, "label (#ADDR)": the view keeps the label
const QRegularExpression kLabelAddress(QStringLiteral("([^\\s,()]+) \\(#[0-9A-Fa-f]{4}\\)"));
constexpr int kTextMargin = 4;          // px left of column 0, as the text edit's document margin
constexpr int kWheelUnitsPerRow = 40;   // a wheel notch (120) scrolls 3 rows
constexpr int kMarginTop = 3;           // PC this close to the top or the bottom: the view follows it
constexpr int kMarginBottom = 3;
constexpr int kPcRowsAbove = kMarginTop + 2;  // rows above PC when the view follows it

QString hexAddress(uint16_t value)
{
    return QString("$%1").arg(value, 4, 16, QLatin1Char('0')).toUpper();
}
}  // namespace

/// region <DisassemblyView>

DisassemblyView::DisassemblyView(QWidget* parent) : QAbstractScrollArea(parent)
{
    // Keys reach it from a click and from the controllers' pane keys (PaneFocus sends them to the first child that
    // takes strong focus)
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(true);  // the hover line
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // The 64K: the scrollbar is the top row's address; its arrows, pages and drags are the widget's to resolve
    // (rows through the strip, a snap to an instruction start)
    QScrollBar* bar = verticalScrollBar();
    bar->setRange(0, 0xFFFF);
    bar->setSingleStep(1);
    bar->setPageStep(64);
    bar->installEventFilter(this);  // the wheel over it scrolls rows as over the rows
    connect(bar, &QAbstractSlider::actionTriggered, this, [this, bar](int action) {
        if (!m_syncingScrollBar && action != QAbstractSlider::SliderNoAction)
            emit scrollBarAction(action, bar->sliderPosition());
    });
}

void DisassemblyView::setLines(std::vector<DisassemblyLine> lines)
{
    m_lines = std::move(lines);
    if (m_hoverLine >= static_cast<int>(m_lines.size()))
        m_hoverLine = -1;
    // The mnemonic column fits the widest mnemonic shown (never cut): the comments start after it
    int widest = 0;
    for (const DisassemblyLine& line : m_lines)
        if (line.kind != DisassemblyLine::Kind::Label)
            widest = std::max(widest, static_cast<int>(line.mnemonic.size()) + 1);
    m_mnemonicWidth = std::clamp(widest, MNEMONIC_COL_WIDTH, MNEMONIC_COL_MAX);
    updateHorizontalRange();
    viewport()->update();
}

void DisassemblyView::setPlaceholder(const QString& text)
{
    m_placeholder = text;
    viewport()->update();
}

void DisassemblyView::setScrollAddress(uint16_t address)
{
    m_syncingScrollBar = true;
    verticalScrollBar()->setValue(address);
    m_syncingScrollBar = false;
}

int DisassemblyView::lineHeight() const
{
    return std::max(1, QFontMetrics(font()).lineSpacing());
}

int DisassemblyView::charWidth() const
{
    return std::max(1, QFontMetrics(font()).horizontalAdvance(QLatin1Char('0')));
}

int DisassemblyView::lineCapacity() const
{
    return viewport()->height() / lineHeight() + 1;
}

int DisassemblyView::fullLines() const
{
    return viewport()->height() / lineHeight();
}

int DisassemblyView::lineAt(const QPoint& pos) const
{
    if (pos.y() < 0)
        return -1;
    const int line = pos.y() / lineHeight();
    return line < static_cast<int>(m_lines.size()) ? line : -1;
}

int DisassemblyView::textLeft() const
{
    return kTextMargin - horizontalScrollBar()->value();
}

QString DisassemblyView::lineText(const DisassemblyLine& line) const
{
    if (line.kind == DisassemblyLine::Kind::Label)
        return QString(LABEL_COL, QLatin1Char(' ')) + line.label;
    QString text = QString("[%1]%2%3: ")
                       .arg(line.breakpoint ? QStringLiteral("●") : QStringLiteral(" "))
                       .arg(line.mark.isNull() ? QChar(' ') : line.mark)
                       .arg(hexAddress(line.address));
    text += line.bytes.leftJustified(BYTECODE_COL_WIDTH) + QLatin1Char(' ');
    text += line.label.leftJustified(LABEL_COL_WIDTH) + QLatin1Char(' ');
    text += line.mnemonic.leftJustified(m_mnemonicWidth) + line.comment;
    while (text.endsWith(QLatin1Char(' ')))
        text.chop(1);
    return text;
}

void DisassemblyView::updateHorizontalRange()
{
    // The comments are cut at the viewport's edge (the whole one is the row's tooltip): the scroll range is the
    // columns up to them, or a mnemonic longer than its column
    int columns = MNEMONIC_COL + m_mnemonicWidth;
    for (const DisassemblyLine& line : m_lines)
        if (line.kind != DisassemblyLine::Kind::Label)
            columns = std::max(columns, MNEMONIC_COL + static_cast<int>(line.mnemonic.size()));
    const int width = kTextMargin * 2 + columns * charWidth();
    QScrollBar* bar = horizontalScrollBar();
    bar->setRange(0, std::max(0, width - viewport()->width()));
    bar->setPageStep(viewport()->width());
    bar->setSingleStep(charWidth());
}

void DisassemblyView::paintEvent(QPaintEvent*)
{
    QPainter painter(viewport());
    const QRect area = viewport()->rect();
    painter.fillRect(area, palette().base());
    painter.setFont(font());

    const QFontMetrics metrics(font());
    const int lh = lineHeight();
    const int cw = charWidth();
    const int left = textLeft();
    const int ascent = metrics.leading() / 2 + metrics.ascent();
    if (m_lines.empty())
    {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(left, ascent, m_placeholder);
        return;
    }

    QFont bold = font();
    bold.setBold(true);
    // Each piece at its column: a glyph of another width (the breakpoint dot) never shifts the columns after it
    const auto text = [&](int column, int y, const QString& value, const QColor& color) {
        painter.setPen(color);
        painter.drawText(left + column * cw, y + ascent, value);
    };
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        const int y = static_cast<int>(i) * lh;
        if (y >= area.bottom())
            break;
        const DisassemblyLine& line = m_lines[i];
        const QRect row(0, y, area.width(), lh);
        if (line.kind != DisassemblyLine::Kind::Label)
        {
            if (line.cursor)
                painter.fillRect(row, kCursorBackground);
            if (line.pc)
                painter.fillRect(row, kPcBackground);
            if (line.breakpoint)
                painter.fillRect(row, kBreakpointBackground);
        }
        if (static_cast<int>(i) == m_hoverLine)
            painter.fillRect(row, kHoverBackground);

        if (line.kind == DisassemblyLine::Kind::Label)
        {
            // A label wider than its column, on a line of its own above its instruction
            text(LABEL_COL, y, line.label, kLabelColor);
            continue;
        }
        const bool data = line.kind == DisassemblyLine::Kind::Data;
        text(0, y, QStringLiteral("["), kBreakpointColor);
        if (line.breakpoint)
            text(1, y, QStringLiteral("●"), kBreakpointColor);
        text(2, y, QStringLiteral("]"), kBreakpointColor);
        if (!line.mark.isNull())
        {
            painter.setFont(bold);
            text(BREAKPOINT_COL_WIDTH, y, QString(line.mark), kMarkColor);
            painter.setFont(font());
        }
        text(ADDRESS_COL, y, hexAddress(line.address), kAddressColor);
        text(ADDRESS_COL + ADDRESS_COL_WIDTH, y, QStringLiteral(":"), kMnemonicColor);
        text(BYTECODE_COL, y, line.bytes, data ? kDataColor : kBytesColor);
        if (!line.label.isEmpty())
            text(LABEL_COL, y, line.label, kLabelColor);
        text(MNEMONIC_COL, y, line.mnemonic, data ? kDataColor : kMnemonicColor);
        if (!line.comment.isEmpty())
        {
            // The comment fills what is left of the row; a longer one ends in an ellipsis (its whole text: the tip)
            const int column = MNEMONIC_COL + std::max(m_mnemonicWidth, static_cast<int>(line.mnemonic.size()) + 1);
            const int room = area.width() - (left + column * cw) - kTextMargin;
            if (room > cw * 3)
                text(column, y, metrics.elidedText(line.comment, Qt::ElideRight, room), kCommentColor);
        }
    }
}

void DisassemblyView::keyPressEvent(QKeyEvent* event)
{
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    const int key = event->key();
    if (key >= Qt::Key_1 && key <= Qt::Key_5 && (mods == Qt::ControlModifier || mods == Qt::AltModifier))
    {
        if (mods == Qt::ControlModifier)
            emit markSetRequested(key - Qt::Key_1 + 1);
        else
            emit markGoRequested(key - Qt::Key_1 + 1);
        event->accept();
        return;
    }
    if (key == Qt::Key_Home && mods == Qt::NoModifier)
    {
        emit goToPcRequested();
        event->accept();
        return;
    }
    if ((key == Qt::Key_Backspace && mods == Qt::NoModifier) || (key == Qt::Key_Left && mods == Qt::AltModifier) ||
        (key == Qt::Key_BracketLeft && mods == Qt::ControlModifier))
    {
        emit backRequested();
        event->accept();
        return;
    }
    if ((key == Qt::Key_Right && mods == Qt::AltModifier) ||
        (key == Qt::Key_BracketRight && mods == Qt::ControlModifier))
    {
        emit forwardRequested();
        event->accept();
        return;
    }
    if (key == Qt::Key_Up)
        emit keyUpPressed();
    else if (key == Qt::Key_Down)
        emit keyDownPressed();
    else if (key == Qt::Key_PageUp)
        emit pageUpPressed();
    else if (key == Qt::Key_PageDown)
        emit pageDownPressed();
    else if (key == Qt::Key_Return || key == Qt::Key_Enter)
        emit enterPressed();
    else if (key == Qt::Key_B)
        emit toggleScrollMode();
    else if (key == Qt::Key_G && (event->modifiers() == Qt::ControlModifier || event->modifiers() == Qt::MetaModifier))
        emit goToAddressRequested();  // Ctrl+G on Windows/Linux or Cmd+G on macOS
    else if (event->matches(QKeySequence::Copy))
        emit copyRequested();
    else
    {
        QAbstractScrollArea::keyPressEvent(event);  // Left / Right: the horizontal scroll
        return;
    }
    event->accept();
}

void DisassemblyView::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0)
    {
        QAbstractScrollArea::wheelEvent(event);  // horizontal scrolling: the horizontal bar
        return;
    }
    // Whole rows: a notch is 3 rows, a trackpad's small deltas add up
    if ((delta > 0) != (m_wheelAccumulator > 0))
        m_wheelAccumulator = 0;
    m_wheelAccumulator += delta;
    const int rows = m_wheelAccumulator / kWheelUnitsPerRow;
    m_wheelAccumulator -= rows * kWheelUnitsPerRow;
    if (rows != 0)
        emit wheelRows(-rows);
    event->accept();
}

void DisassemblyView::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    if (event->button() != Qt::LeftButton)
        return;
    const int line = lineAt(event->position().toPoint());
    if (line < 0)
        return;
    // The breakpoint column toggles a breakpoint; anywhere else names the cursor row
    if (event->position().x() - textLeft() < BREAKPOINT_COL_WIDTH * charWidth())
        emit breakpointClicked(line);
    else
        emit lineClicked(line);
    event->accept();
}

void DisassemblyView::mouseMoveEvent(QMouseEvent* event)
{
    const int line = lineAt(event->position().toPoint());
    if (line != m_hoverLine)
    {
        m_hoverLine = line;
        viewport()->update();
    }
}

void DisassemblyView::contextMenuEvent(QContextMenuEvent* event)
{
    emit contextMenuAt(lineAt(event->pos()), event->globalPos());
    event->accept();
}

void DisassemblyView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateHorizontalRange();
    verticalScrollBar()->setPageStep(std::max(16, fullLines() * 3));  // the handle: about a screen of bytes
    emit resized();
}

void DisassemblyView::scrollContentsBy(int, int)
{
    viewport()->update();  // painted from the scroll values, nothing to move
}

bool DisassemblyView::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::ToolTip)
    {
        // The row's whole text: the instruction with its addresses, the runtime hints, the comment
        auto* help = static_cast<QHelpEvent*>(event);
        const int line = lineAt(help->pos());
        const QString tip = line >= 0 ? m_lines[static_cast<size_t>(line)].tip : QString();
        if (tip.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(help->globalPos(), tip, viewport());
        return true;
    }
    if (event->type() == QEvent::Leave && m_hoverLine >= 0)
    {
        m_hoverLine = -1;
        viewport()->update();
    }
    return QAbstractScrollArea::viewportEvent(event);
}

bool DisassemblyView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == verticalScrollBar() && event->type() == QEvent::Wheel)
    {
        wheelEvent(static_cast<QWheelEvent*>(event));
        return true;
    }
    return QAbstractScrollArea::eventFilter(watched, event);
}

/// endregion </DisassemblyView>

DisassemblerWidget::DisassemblerWidget(QWidget* parent) : QWidget(parent), ui(new Ui::DisassemblerWidget)
{
    // Instantiate all child widgets (UI form auto-generated)
    ui->setupUi(this);

    m_mainThread = QApplication::instance()->thread();
    m_debuggerWindow = DebuggerWindow::Of(parent);

    // Initialize member variables
    m_currentPC = 0;
    m_displayAddress = 0;
    m_scrollMode = ScrollMode::Command;  // Default to command mode
    m_isActive = false;                  // Default to detached state

    // The painted rows
    m_view = new DisassemblyView(this);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setStyleHint(QFont::TypeWriter);
    font.setPointSize(12);  // Default is usually 9 points
    m_view->setFont(font);
    m_view->setPlaceholder("<Disassembly goes here>");
    QLayout* layout = ui->verticalLayout;
    layout->addWidget(m_view);

    // Create scroll mode indicator label
    m_scrollModeIndicator = new QLabel(this);
    m_scrollModeIndicator->setFrameStyle(QFrame::Panel | QFrame::Raised);
    m_scrollModeIndicator->setAlignment(Qt::AlignCenter);
    m_scrollModeIndicator->setFixedSize(50, 24);
    m_scrollModeIndicator->setStyleSheet(
        "QLabel { background-color: #333; color: #0f0; font-weight: bold; border: 1px solid #555; }");

    // Create debugger state indicator label
    m_stateIndicator = new QLabel(this);
    m_stateIndicator->setFrameStyle(QFrame::Panel | QFrame::Raised);
    m_stateIndicator->setAlignment(Qt::AlignCenter);
    m_stateIndicator->setFixedSize(80, 24);
    m_stateIndicator->setStyleSheet(
        "QLabel { background-color: #333; color: #0f0; font-weight: bold; border: 1px solid #555; }");

    // Create bank indicator label
    m_bankIndicator = new QLabel(this);
    m_bankIndicator->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_bankIndicator->setText("Bank: ROM");  // Default value
    // A long bank title gives way to the indicators (cut at the right; the whole text in the tooltip)
    m_bankIndicator->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    // Position the indicators in the top-right corner
    QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(layout);
    if (mainLayout)
    {
        QHBoxLayout* topLayout = new QHBoxLayout();

        // The bank on the left takes the free width (pushing the indicators to the right)
        topLayout->addWidget(m_bankIndicator, 1);

        // The machine's signals (lit: bold on green; out: gray)
        for (QLabel** indicator : {&m_dosIndicator, &m_romIndicator, &m_intIndicator})
        {
            *indicator = new QLabel(this);
            (*indicator)->setAlignment(Qt::AlignCenter);
            (*indicator)->setMinimumWidth(40);
            (*indicator)->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);  // never cut
            (*indicator)->setFixedHeight(24);
            topLayout->addWidget(*indicator);
            topLayout->addSpacing(3);
        }
        m_dosIndicator->setText("DOS");
        m_dosIndicator->setToolTip("TR-DOS: the DOS ROM and the Beta 128 ports are on (lit) or off");
        m_intIndicator->setText("INT");
        m_intIndicator->setToolTip("INT: the maskable interrupt is asserted at this instruction (NMI: in service)");
        topLayout->addSpacing(5);

        // Add state and scroll mode indicators on the right
        topLayout->addWidget(m_stateIndicator);
        topLayout->addSpacing(5);  // Add a small gap between indicators
        topLayout->addWidget(m_scrollModeIndicator);

        // Insert the top layout before the rows
        mainLayout->insertLayout(0, topLayout);
    }

    // Update the indicators to show initial states
    updateScrollModeIndicator();
    updateDebuggerStateIndicator();

    // The keys move the cursor row (run to the cursor, a breakpoint at it); the wheel and the scrollbar scroll the view
    connect(m_view, &DisassemblyView::keyUpPressed, this, [this] { moveCursorBy(-1); });
    connect(m_view, &DisassemblyView::keyDownPressed, this, [this] { moveCursorBy(1); });
    connect(m_view, &DisassemblyView::pageUpPressed, this, [this] { moveCursorBy(-16); });
    connect(m_view, &DisassemblyView::pageDownPressed, this, [this] { moveCursorBy(16); });
    connect(m_view, &DisassemblyView::enterPressed, this, &DisassemblerWidget::followOperand);
    connect(m_view, &DisassemblyView::goToPcRequested, this, &DisassemblerWidget::followPc);
    connect(m_view, &DisassemblyView::backRequested, this, &DisassemblerWidget::historyBack);
    connect(m_view, &DisassemblyView::forwardRequested, this, &DisassemblerWidget::historyForward);
    connect(m_view, &DisassemblyView::markSetRequested, this, &DisassemblerWidget::setMark);
    connect(m_view, &DisassemblyView::markGoRequested, this, &DisassemblerWidget::goToMark);
    connect(m_view, &DisassemblyView::toggleScrollMode, this, &DisassemblerWidget::toggleScrollMode);
    connect(m_view, &DisassemblyView::goToAddressRequested, this, &DisassemblerWidget::showGoToAddressDialog);
    connect(m_view, &DisassemblyView::copyRequested, this, &DisassemblerWidget::copyCursorLine);
    connect(m_view, &DisassemblyView::contextMenuAt, this, &DisassemblerWidget::showContextMenu);
    connect(m_view, &DisassemblyView::wheelRows, this, &DisassemblerWidget::scrollRows);
    connect(m_view, &DisassemblyView::scrollBarAction, this, &DisassemblerWidget::onScrollBarAction);
    connect(m_view, &DisassemblyView::lineClicked, this, &DisassemblerWidget::onLineClicked);
    connect(m_view, &DisassemblyView::breakpointClicked, this, [this](int line) {
        if (line >= 0 && line < static_cast<int>(m_view->lines().size()))
            toggleBreakpointAtAddress(m_view->lines()[static_cast<size_t>(line)].address);  // a label line: its row
    });
    // A taller view gets more rows from the same top row
    connect(m_view, &DisassemblyView::resized, this, [this] {
        if (!m_isShuttingDown && hasRows())
            layoutLines();
    });
}

void DisassemblerWidget::metadataSwitched()
{
    m_strip.Clear();  // other anchors and data rows
    refreshPreservingPosition(m_displayAddress);
}

void DisassemblerWidget::followPc()
{
    if (Z80Registers* registers = getZ80Registers())
        jumpTo(registers->pc);
}

void DisassemblerWidget::jumpTo(uint16_t address)
{
    if (m_isShuttingDown || !getEmulator() || !getZ80Registers())
        return;
    if (hasRows())
    {
        m_back.push_back(currentPlace());
        if (m_back.size() > kHistoryDepth)
            m_back.pop_front();
    }
    m_forward.clear();
    m_cursorAddress = address;
    publishCursor();
    setDisassemblerAddress(address);
}

void DisassemblerWidget::showPlace(const ViewPlace& place)
{
    m_cursorAddress = place.cursor;
    publishCursor();
    setDisassemblerAddress(place.top);
}

void DisassemblerWidget::historyBack()
{
    if (m_back.empty() || !getEmulator())
        return;
    m_forward.push_back(currentPlace());
    const ViewPlace place = m_back.back();
    m_back.pop_back();
    showPlace(place);
}

void DisassemblerWidget::historyForward()
{
    if (m_forward.empty() || !getEmulator())
        return;
    m_back.push_back(currentPlace());
    const ViewPlace place = m_forward.back();
    m_forward.pop_back();
    showPlace(place);
}

void DisassemblerWidget::setMark(int n)
{
    if (n < 1 || n > static_cast<int>(m_marks.size()) || !hasRows())
        return;
    const uint16_t address = m_cursorAddress.value_or(m_displayAddress);
    std::optional<uint16_t>& mark = m_marks[static_cast<size_t>(n - 1)];
    mark = mark == address ? std::nullopt : std::optional<uint16_t>(address);
    layoutLines();  // the mark column
}

void DisassemblerWidget::goToMark(int n)
{
    if (n < 1 || n > static_cast<int>(m_marks.size()))
        return;
    if (const std::optional<uint16_t> mark = m_marks[static_cast<size_t>(n - 1)])
        jumpTo(*mark);
}

bool DisassemblerWidget::decodeAt(uint16_t address, DecodedInstruction& decoded)
{
    Memory* memory = getMemory();
    Z80Registers* registers = getZ80Registers();
    std::unique_ptr<Z80Disassembler>& disassembler = getDisassembler();
    if (!memory || !registers || !disassembler)
        return false;
    std::vector<uint8_t> buffer(Z80Disassembler::MAX_INSTRUCTION_LENGTH);
    for (size_t i = 0; i < buffer.size(); i++)
        buffer[i] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(address + i));
    uint8_t commandLen = 0;
    try
    {
        disassembler->disassembleSingleCommandWithRuntime(buffer, address, &commandLen, registers, memory, &decoded);
    }
    catch (const std::exception&)
    {
        return false;
    }
    return decoded.isValid;
}

void DisassemblerWidget::followOperand()
{
    DecodedInstruction decoded;
    if (!m_cursorAddress || !decodeAt(*m_cursorAddress, decoded))
    {
        followPc();
        return;
    }
    if (decoded.hasJump || decoded.hasRelativeJump || decoded.isRst)
        jumpTo(decoded.jumpAddr);
    else if (decoded.hasReturn)
        jumpTo(decoded.returnAddr);
    else if (decoded.hasWordOperand)
        jumpTo(decoded.wordOperand);
    else
        followPc();
}

void DisassemblerWidget::showContextMenu(int line, const QPoint& globalPos)
{
    // The row under the pointer becomes the cursor row, as a click
    if (line >= 0)
        onLineClicked(line);
    QMenu menu(this);
    DecodedInstruction decoded;
    if (m_cursorAddress && decodeAt(*m_cursorAddress, decoded))
    {
        std::optional<uint16_t> target;
        if (decoded.hasJump || decoded.hasRelativeJump || decoded.isRst)
            target = decoded.jumpAddr;
        else if (decoded.hasReturn)
            target = decoded.returnAddr;
        else if (decoded.hasWordOperand)
            target = decoded.wordOperand;
        if (target)
            menu.addAction(tr("Follow %1\tEnter").arg(hexAddress(*target)), this, &DisassemblerWidget::followOperand);
    }
    menu.addAction(tr("Go to PC\tHome"), this, &DisassemblerWidget::followPc);
    menu.addAction(tr("Go to address...\tCtrl+G"), this, &DisassemblerWidget::showGoToAddressDialog);
    QAction* back = menu.addAction(tr("Back\tBackspace"), this, &DisassemblerWidget::historyBack);
    back->setEnabled(!m_back.empty());
    QAction* forward = menu.addAction(tr("Forward\tAlt+Right"), this, &DisassemblerWidget::historyForward);
    forward->setEnabled(!m_forward.empty());
    menu.addSeparator();
    QMenu* marks = menu.addMenu(tr("Marks"));
    const uint16_t here = m_cursorAddress.value_or(m_displayAddress);
    for (int n = 1; n <= static_cast<int>(m_marks.size()); n++)
    {
        const std::optional<uint16_t>& mark = m_marks[static_cast<size_t>(n - 1)];
        if (mark)
            marks->addAction(tr("Go to mark %1: %2\tAlt+%1").arg(n).arg(hexAddress(*mark)), this,
                             [this, n] { goToMark(n); });
        marks->addAction(
            (mark == here ? tr("Unmark %1\tCtrl+%1") : tr("Mark %2 as %1\tCtrl+%1")).arg(n).arg(hexAddress(here)), this,
            [this, n] { setMark(n); });
    }
    menu.exec(globalPos);
}

void DisassemblerWidget::switchScrollMode()
{
    toggleScrollMode();
}

void DisassemblerWidget::onLineClicked(int line)
{
    if (m_isShuttingDown || line < 0 || line >= static_cast<int>(m_view->lines().size()))
        return;
    m_cursorAddress = m_view->lines()[static_cast<size_t>(line)].address;  // a long label's line: the row below
    publishCursor();
    layoutLines();
}

void DisassemblerWidget::publishCursor()
{
    if (Emulator* emulator = getEmulator())
        actions::DebuggerCursors::Instance().Set(emulator->GetId(), "disasm", m_cursorAddress);
}

void DisassemblerWidget::copyCursorLine()
{
    if (!m_cursorAddress)
        return;
    for (const DisassemblyLine& line : m_view->lines())
        if (line.kind != DisassemblyLine::Kind::Label && line.address == *m_cursorAddress)
        {
            QApplication::clipboard()->setText(m_view->lineText(line));
            return;
        }
}

int DisassemblerWidget::shownRowOf(uint16_t address) const
{
    int row = 0;
    for (const DisassemblyLine& line : m_view->lines())
    {
        if (line.kind == DisassemblyLine::Kind::Label)
            continue;
        if (line.address == address)
            return row;
        row++;
    }
    return -1;
}

void DisassemblerWidget::moveCursorBy(int rows)
{
    if (rows == 0 || m_isShuttingDown || !getEmulator() || !getZ80Registers() || !hasRows())
        return;
    // From the cursor row; without one, from PC when it is shown, else the top line
    uint16_t address = m_displayAddress;
    if (m_cursorAddress)
        address = *m_cursorAddress;
    else if (shownRowOf(m_currentPC) >= 0)
        address = m_currentPC;
    // Kept visible: up past the top, the row becomes the top; down past the bottom, a few rows stay below it
    const int keep = std::max(0, m_rowsShown - 4);
    const auto outOfView = [&](int row) { return row < 0 || row >= m_rowsShown || (rows > 0 && row >= m_rowsShown - 2); };

    if (m_scrollMode == ScrollMode::Byte)
    {
        address = static_cast<uint16_t>(address + rows);
        m_cursorAddress = address;
        publishCursor();
        if (outOfView(shownRowOf(address)))
            setDisassemblerAddress(rows > 0 ? static_cast<uint16_t>(address - keep) : address);
        else
            layoutLines();
        return;
    }

    const int capacity = std::max(1, m_view->lineCapacity());
    withWindow([&](disasm::Window& window, const disasm::Source& source, uint64_t revision) {
        int index = m_strip.IndexOf(address);
        if (index < 0)
        {
            m_strip.ResetAt(window, source, revision, address, static_cast<size_t>(capacity));
            index = 0;
        }
        index = m_strip.Step(window, source, index, rows);
        m_cursorAddress = m_strip.Rows()[static_cast<size_t>(index)].address;
        if (outOfView(index - static_cast<int>(m_strip.Top())))
        {
            m_strip.SetTop(static_cast<size_t>(index));
            if (rows > 0)
                m_strip.Scroll(window, source, -keep, static_cast<size_t>(capacity));
        }
    });
    publishCursor();
    layoutLines();
}

DisassemblerWidget::~DisassemblerWidget()
{
    delete ui;
}

// Helper methods
Emulator* DisassemblerWidget::getEmulator()
{
    return m_debuggerWindow->getEmulator();
}

EmulatorContext* DisassemblerWidget::getEmulatorContext()
{
    Emulator* emu = m_debuggerWindow->getEmulator();
    return emu ? emu->GetContext() : nullptr;
}

Memory* DisassemblerWidget::getMemory()
{
    Emulator* emu = m_debuggerWindow->getEmulator();
    if (!emu) return nullptr;
    EmulatorContext* ctx = emu->GetContext();
    return ctx ? ctx->pMemory : nullptr;
}

Z80Registers* DisassemblerWidget::getZ80Registers()
{
    Emulator* emu = m_debuggerWindow->getEmulator();
    if (!emu) return nullptr;
    EmulatorContext* ctx = emu->GetContext();
    if (!ctx || !ctx->pCore) return nullptr;
    return ctx->pCore->GetZ80();
}

std::unique_ptr<Z80Disassembler>& DisassemblerWidget::getDisassembler()
{
    // Static empty unique_ptr for null case
    static std::unique_ptr<Z80Disassembler> nullDisassembler;
    
    Emulator* emu = m_debuggerWindow->getEmulator();
    if (!emu) return nullDisassembler;
    EmulatorContext* ctx = emu->GetContext();
    if (!ctx || !ctx->pDebugManager) return nullDisassembler;
    return ctx->pDebugManager->GetDisassembler();
}

BreakpointManager* DisassemblerWidget::getBreakpointManager() const
{
    Emulator* emu = m_debuggerWindow->getEmulator();
    return emu ? emu->GetBreakpointManager() : nullptr;
}

void DisassemblerWidget::setDisassemblerAddress(uint16_t pc)
{
    // Block all operations during shutdown
    if (m_isShuttingDown)
    {
        return;
    }
    if (QThread::currentThread() != QCoreApplication::instance()->thread())
    {
        qWarning() << "setDisassemblerAddress called from non-main thread";
        // Schedule the update to happen in the main thread
        QMetaObject::invokeMethod(this, [this, pc]() { setDisassemblerAddress(pc); }, Qt::QueuedConnection);
        return;
    }

    // Every getter is documented to return null when the emulator, its context or the debug manager is not
    // available, and this runs on the GUI thread in response to state-change signals — including the ones a TTD
    // seek emits while the machine is being rebuilt (withWindow checks them all). Bailing out is safe: another
    // refresh follows once the emulator settles.
    if (!getEmulator() || !getMemory())
        return;
    const bool shown = withWindow([&](disasm::Window& window, const disasm::Source& source, uint64_t revision) {
        // A row the strip has: its rows above stay (up then down shows the same rows); else a new strip from there
        const int index = m_strip.IndexOf(pc);
        if (index >= 0)
            m_strip.SetTop(static_cast<size_t>(index));
        else
            m_strip.ResetAt(window, source, revision, pc, static_cast<size_t>(std::max(1, m_view->lineCapacity())));
    });
    if (!shown)
        return;
    m_displayAddress = pc;

    // Update the bank indicator
    updateBankIndicator(pc);
    updateSignalIndicators();
    layoutLines();
}

uint64_t DisassemblerWidget::markupRevision()
{
    EmulatorContext* context = getEmulatorContext();
    markup::MemoryMarkup* markup =
        context && context->pDebugManager ? context->pDebugManager->GetMemoryMarkup() : nullptr;
    return markup ? markup->Revision() : 0;
}

bool DisassemblerWidget::withWindow(
    const std::function<void(disasm::Window&, const disasm::Source&, uint64_t revision)>& body)
{
    EmulatorContext* context = getEmulatorContext();
    std::unique_ptr<Z80Disassembler>& disassembler = getDisassembler();
    if (m_isShuttingDown || !context || !context->pMemory || !getZ80Registers() || !disassembler)
        return false;

    // Labels off (dbg.labels_toggle): plain addresses in the operands; this thread only
    const MetadataSettings& settings = MetadataSettings::Instance();
    std::optional<Z80Disassembler::NoLabelsScope> noLabels;
    if (!settings.Uses(MetadataSettings::Switch::Labels))
        noLabels.emplace();

    // Direct reads of the memory as mapped now: the view never holds the emulation thread. The metadata's regions
    // off: no anchors or data rows from the markup
    disasm::EmulatorSource source(context, settings.Uses(MetadataSettings::Switch::Regions));
    disasm::Window window(*disassembler, source);
    const uint64_t revision = markupRevision();
    if (!m_strip.Validate(source, revision))
        m_strip.ResetAt(window, source, revision, m_displayAddress,
                        static_cast<size_t>(std::max(1, m_view->lineCapacity())));
    body(window, source, revision);
    return true;
}

void DisassemblerWidget::layoutLines()
{
    EmulatorContext* context = getEmulatorContext();
    Z80Registers* registers = getZ80Registers();
    std::unique_ptr<Z80Disassembler>& disassemblerPtr = getDisassembler();
    if (m_isShuttingDown || !context || !context->pDebugManager || !registers || !disassemblerPtr)
        return;
    Z80Disassembler& disassembler = *disassemblerPtr;
    // Labels off: no label column either
    const MetadataSettings& settings = MetadataSettings::Instance();
    LabelManager* labelManager =
        settings.Uses(MetadataSettings::Switch::Labels) ? context->pDebugManager->GetLabelManager() : nullptr;
    // The metadata's notes off: no block descriptions or comments from the markup
    markup::MemoryMarkup* markup =
        settings.Uses(MetadataSettings::Switch::Notes) ? context->pDebugManager->GetMemoryMarkup() : nullptr;

    const size_t capacity = static_cast<size_t>(std::max(1, m_view->lineCapacity()));
    const size_t fullLines = static_cast<size_t>(std::max(0, m_view->fullLines()));
    std::vector<DisassemblyLine> lines;
    int rowsShown = 0;
    const bool shown = withWindow([&](disasm::Window& window, const disasm::Source& source, uint64_t) {
        m_strip.Ensure(window, source, 0, capacity);
        const std::vector<disasm::Row>& rows = m_strip.Rows();
        std::vector<uint8_t> buffer(Z80Disassembler::MAX_INSTRUCTION_LENGTH);
        for (size_t i = m_strip.Top(); i < rows.size() && lines.size() < capacity; i++)
        {
            const disasm::Row& row = rows[i];
            DisassemblyLine line;
            line.address = row.address;
            line.kind = row.kind == disasm::RowKind::Data ? DisassemblyLine::Kind::Data
                                                          : DisassemblyLine::Kind::Instruction;

            // Format value like: [B] $15FB: CD 2C 16   LABEL:    call $162C (init_screen)
            // A `db` row of more bytes than the column holds: its first ones (the mnemonic lists them all)
            uint8_t bytes[disasm::kDataRowBytes];
            std::copy(std::begin(row.bytes), std::end(row.bytes), bytes);
            line.bytes = row.length <= 4
                             ? QString::fromStdString(DumpHelper::HexDumpBuffer(bytes, row.length))
                             : QString::fromStdString(DumpHelper::HexDumpBuffer(bytes, 3)) + QStringLiteral(" ..");

            if (row.kind == disasm::RowKind::Instruction)
            {
                // Decoded again with the registers now: the runtime hints of the rows the strip keeps follow them
                for (size_t j = 0; j < buffer.size(); j++)
                    buffer[j] = source.Read(static_cast<uint16_t>(row.address + j));
                uint8_t commandLen = 0;
                DecodedInstruction decoded;
                std::string command = disassembler.disassembleSingleCommandWithRuntime(
                    buffer, row.address, &commandLen, source.Registers(), source.RuntimeMemory(), &decoded);

                // An operand with a label shows the label only (its address is in the row's tooltip): the mnemonic
                // stays short and is never cut
                const QString full = QString::fromStdString(command);
                line.mnemonic = full;
                line.mnemonic.replace(kLabelAddress, QStringLiteral("\\1"));

                std::string runtime;
                if (decoded.hasRuntime)
                    runtime = disassembler.getRuntimeHints(decoded);
                // Labels at the effective address of indexed (IX/IY+d) instructions. Label information for
                // jump/call targets and absolute memory operands is already embedded into the mnemonic by
                // Z80Disassembler::formatOperandString as 'label (#ADDR)'
                if (decoded.hasDisplacement && decoded.hasRuntime && labelManager)
                    if (std::shared_ptr<Label> target = labelManager->GetLabelByZ80Address(decoded.displacementAddr))
                        runtime += StringHelper::Format("%s%s", runtime.empty() ? "" : "; ", target->name.c_str());
                // The runtime hints read the registers now: true on the PC row only, there they are the comment
                line.tip = full;
                if (!runtime.empty())
                {
                    line.tip += QStringLiteral("\n") + QString::fromStdString(runtime);
                    if (row.address == registers->pc)
                        line.comment = QString::fromStdString(runtime);
                }
            }
            else
            {
                line.mnemonic = QString::fromStdString(row.mnemonic);
                line.tip = line.mnemonic;
            }
            // The markup's comment at the row, else the description of the block the row starts (its title without
            // the name the label column shows)
            if (markup)
            {
                QString note;
                for (const markup::CommentAt& comment : markup->Comments(row.address, row.address))
                    note += (note.isEmpty() ? QString() : QStringLiteral("; ")) + QString::fromStdString(comment.text);
                // A block starts where the byte before it has another title (the spans the markup returns are cut at
                // its anchors: their starts alone say nothing)
                if (note.isEmpty())
                    if (const std::optional<markup::Span> span = markup->At(row.address))
                        if (!span->title.empty() &&
                            [&] {
                                const std::optional<markup::Span> before =
                                    markup->At(static_cast<uint16_t>(row.address - 1));
                                return row.address == 0 || !before || before->title != span->title;
                            }())
                        {
                            note = QString::fromStdString(span->title);
                            const qsizetype colon = note.indexOf(QStringLiteral(": "));
                            if (colon > 0 && !note.left(colon).contains(QLatin1Char(' ')))
                                note = note.mid(colon + 2);
                        }
                if (!note.isEmpty())
                {
                    line.comment = line.comment.isEmpty() ? QStringLiteral("; ") + note
                                                          : line.comment + QStringLiteral("  ; ") + note;
                    line.tip += QStringLiteral("\n; ") + note;
                }
            }

            line.breakpoint = hasBreakpointAtAddress(row.address);
            // Marked addresses (Ctrl+1..5): the lowest mark's digit between the breakpoint column and the address
            for (size_t m = 0; m < m_marks.size(); m++)
                if (m_marks[m] == row.address)
                {
                    line.mark = QChar('1' + static_cast<int>(m));
                    break;
                }
            line.pc = row.address == registers->pc;
            line.cursor = m_cursorAddress == row.address;

            if (std::shared_ptr<Label> label = labelManager ? labelManager->GetLabelByZ80Address(row.address) : nullptr)
                line.label = QString::fromStdString(label->name) + QLatin1Char(':');
            // A label wider than its column takes a line of its own above the instruction, as in assembler sources,
            // so the mnemonic column stays aligned. Clicks on that line go to the instruction below
            if (line.label.size() > DisassemblyView::LABEL_COL_WIDTH)
            {
                DisassemblyLine labelLine;
                labelLine.kind = DisassemblyLine::Kind::Label;
                labelLine.address = row.address;
                labelLine.label = line.label;
                lines.push_back(std::move(labelLine));
                line.label.clear();
                if (lines.size() >= capacity)
                    break;
            }
            lines.push_back(std::move(line));
            if (lines.size() <= fullLines)
                rowsShown++;
        }
    });
    if (!shown)
        return;

    m_currentPC = registers->pc;
    m_displayAddress = m_strip.TopAddress();
    m_rowsShown = rowsShown;
    m_view->setLines(std::move(lines));
    m_view->setScrollAddress(m_displayAddress);
}

void DisassemblerWidget::scrollRows(int rows)
{
    if (rows == 0 || m_isShuttingDown || !hasRows())
        return;
    if (m_scrollMode == ScrollMode::Byte)
    {
        // The top address moves by bytes, decoding starts there
        setDisassemblerAddress(static_cast<uint16_t>(std::clamp(m_displayAddress + rows, 0, 0xFFFF)));
        return;
    }
    // Rows through the strip: up N then down N shows the same rows
    withWindow([&](disasm::Window& window, const disasm::Source& source, uint64_t) {
        m_strip.Scroll(window, source, rows, static_cast<size_t>(std::max(1, m_view->lineCapacity())));
    });
    updateBankIndicator(m_strip.TopAddress());
    layoutLines();
}

void DisassemblerWidget::onScrollBarAction(int action, int position)
{
    switch (action)
    {
        case QAbstractSlider::SliderSingleStepAdd:
            scrollRows(1);
            break;
        case QAbstractSlider::SliderSingleStepSub:
            scrollRows(-1);
            break;
        case QAbstractSlider::SliderPageStepAdd:
            scrollRows(std::max(1, m_rowsShown - 1));
            break;
        case QAbstractSlider::SliderPageStepSub:
            scrollRows(-std::max(1, m_rowsShown - 1));
            break;
        default:
        {
            // A drag (or top / bottom): the address it lands on, snapped to the start of the row that holds it
            // (byte scroll: decoding starts right there)
            auto address = static_cast<uint16_t>(std::clamp(position, 0, 0xFFFF));
            if (m_scrollMode == ScrollMode::Command)
                withWindow([&](disasm::Window& window, const disasm::Source&, uint64_t) {
                    address = window.SnapToInstruction(address);
                });
            setDisassemblerAddress(address);
            break;
        }
    }
    // The scrollbar shows the top row, whatever it asked for
    m_view->setScrollAddress(m_displayAddress);
}

void DisassemblerWidget::updateScrollModeIndicator()
{
    // Update the indicator label based on current mode
    if (m_scrollMode == ScrollMode::Byte)
    {
        m_scrollModeIndicator->setText("BYTE");
    }
    else
    {
        m_scrollModeIndicator->setText("CMD");
    }
}

void DisassemblerWidget::updateDebuggerStateIndicator()
{
    // Check if we have a valid emulator
    Emulator* emulator = getEmulator();
    if (!emulator)
    {

        m_stateIndicator->setText("DETACHED");
        m_stateIndicator->setStyleSheet(
            "QLabel { background-color: #333; color: #777; font-weight: bold; border: 1px solid #555; }");
        m_isActive = false;
        return;
    }

    // Check if the emulator is paused (active debugging state)
    bool isPaused = emulator->IsPaused();

    // Update the state indicator
    if (isPaused)
    {

        m_stateIndicator->setText("ACTIVE");
        m_stateIndicator->setStyleSheet(
            "QLabel { background-color: #333; color: #0f0; font-weight: bold; border: 1px solid #555; }");
        m_isActive = true;
    }
    else
    {

        m_stateIndicator->setText("DETACHED");
        m_stateIndicator->setStyleSheet(
            "QLabel { background-color: #333; color: #777; font-weight: bold; border: 1px solid #555; }");
        m_isActive = false;
    }
}

void DisassemblerWidget::toggleScrollMode()
{
    // Toggle between byte and command modes
    if (m_scrollMode == ScrollMode::Byte)
    {
        m_scrollMode = ScrollMode::Command;
    }
    else
    {
        m_scrollMode = ScrollMode::Byte;
    }

    // Update the indicator
    updateScrollModeIndicator();
}

void DisassemblerWidget::reset()
{
    m_strip.Clear();
    m_view->setLines({});
    m_view->setScrollAddress(0);
    m_currentPC = 0;
    m_displayAddress = 0;
    m_rowsShown = 0;

    // Reset to default scroll mode (Command)
    m_scrollMode = ScrollMode::Command;
    updateScrollModeIndicator();

    // Update debugger state indicator
    updateDebuggerStateIndicator();
}

void DisassemblerWidget::updateBankIndicator(uint16_t address)
{
    if (!getEmulator() || !getMemory())
        return;

    Memory& memory = *getMemory();
    EmulatorContext* ctx = getEmulator()->GetContext();

    // Get the physical address for the current address
    uint8_t* physicalAddress = memory.MapZ80AddressToPhysicalAddress(address);
    std::string bankName = "Unknown";
    std::string romTitle;

    // Determine bank based on address range
    if (address < 0x4000)
    {
        // ROM at 0x0000-0x3FFF - get human-readable ROM title
        uint8_t romPage = memory.GetROMPage();
        if (ctx && ctx->pCore && ctx->pCore->GetROM())
        {
            romTitle = ctx->pCore->GetROM()->GetROMTitleByAddress(physicalAddress);
        }
        if (romTitle.empty())
        {
            bankName = "ROM " + std::to_string(romPage);
        }
        else
        {
            bankName = "ROM " + std::to_string(romPage) + " - " + romTitle;
        }
    }
    else if (address < 0x8000)
    {
        // RAM 5 (fixed) or ROM for some models at 0x4000-0x7FFF
        bankName = "RAM 5";
    }
    else if (address < 0xC000)
    {
        // RAM 2 (fixed) at 0x8000-0xBFFF
        bankName = "RAM 2";
    }
    else
    {
        // Paged RAM at 0xC000-0xFFFF
        uint8_t ramPage = memory.GetRAMPageForBank(3);  // Bank 3 is 0xC000-0xFFFF
        bankName = "RAM " + std::to_string(ramPage);
    }

    // Add the address range for clarity
    QString addressRangeStr;
    if (address < 0x4000)
        addressRangeStr = "0000-3FFF";
    else if (address < 0x8000)
        addressRangeStr = "4000-7FFF";
    else if (address < 0xC000)
        addressRangeStr = "8000-BFFF";
    else
        addressRangeStr = "C000-FFFF";

    // Update the bank indicator label with both bank name and address range
    m_bankIndicator->setText(QString("Bank: %1 (%2)").arg(QString::fromStdString(bankName), addressRangeStr));
    m_bankIndicator->setToolTip(m_bankIndicator->text());
}

void DisassemblerWidget::updateSignalIndicators()
{
    static const QString lit =
        "QLabel { background-color: #2E7D32; color: #fff; font-weight: bold; border: 1px solid #555; padding: 0 4px; }";
    static const QString out = "QLabel { background-color: #333; color: #777; border: 1px solid #555; padding: 0 4px; }";
    static const QString shown = "QLabel { background-color: #333; color: #ddd; border: 1px solid #555; padding: 0 4px; }";
    Memory* memory = getMemory();
    Z80Registers* registers = getZ80Registers();
    EmulatorContext* ctx = getEmulatorContext();
    if (!ctx || !memory || !registers || !m_dosIndicator)
        return;

    const bool dos = (ctx->emulatorState.flags & CF_TRDOS) != 0;
    m_dosIndicator->setStyleSheet(dos ? lit : out);

    // The ROM the CPU sees at 0000: its page and title ("48K BASIC", "TR-DOS"); RAM there (Profi CP/M, ATM, +3
    // special paging) is shown as RAM
    QString rom;
    QString romTip;
    if (memory->MapZ80AddressToPhysicalAddress(0x0000) && memory->IsBank0ROM())
    {
        std::string title;
        if (ctx->pCore && ctx->pCore->GetROM())
            title = ctx->pCore->GetROM()->GetROMTitleByAddress(memory->MapZ80AddressToPhysicalAddress(0x0000));
        rom = QString("ROM %1").arg(memory->GetROMPage());
        romTip = title.empty() ? rom : QString("%1: %2").arg(rom, QString::fromStdString(title));
    }
    else
    {
        rom = "RAM";
        romTip = "RAM in 0000-3FFF";
    }
    m_romIndicator->setText(rom);
    m_romIndicator->setToolTip(romTip);
    m_romIndicator->setStyleSheet(shown);

    Z80* z80 = ctx->pCore ? ctx->pCore->GetZ80() : nullptr;
    const bool nmi = registers->nmi_in_progress;
    const bool intAsserted = z80 && z80->int_pending;
    m_intIndicator->setText(nmi ? "NMI" : "INT");
    m_intIndicator->setStyleSheet(nmi || intAsserted ? lit : out);
}

void DisassemblerWidget::refresh()
{
    // Block all operations during shutdown
    if (m_isShuttingDown)
    {
        return;
    }

    // Update the disassembly view with current PC
    Z80Registers* registers = getZ80Registers();
    if (getEmulator() && registers)
    {
        const uint16_t currentPC = registers->pc;
        // The rows shown follow the view height; the first refresh has none yet
        const int rowsShown = hasRows() ? m_rowsShown : m_view->fullLines();
        const int rowsAbove = std::max(0, std::min(kPcRowsAbove, (rowsShown - 1) / 2));
        const auto capacity = static_cast<size_t>(std::max(1, m_view->lineCapacity()));

        const bool shown = withWindow([&](disasm::Window& window, const disasm::Source& source, uint64_t revision) {
            // PC visible and not at the edge margins (counted in rows: long labels' lines do not count): the
            // view stays; else PC gets a few rows above it, from the strip when it has PC (PC is an anchor: when
            // in the strip, a row starts there)
            const int index = m_strip.IndexOf(currentPC);
            const int row = index - static_cast<int>(m_strip.Top());
            if (index >= 0 && hasRows() && row >= kMarginTop && row < rowsShown - kMarginBottom)
                return;
            if (index >= 0)
            {
                m_strip.SetTop(static_cast<size_t>(index));
                m_strip.Scroll(window, source, -rowsAbove, capacity);
            }
            else
                m_strip.ResetAround(window, source, revision, currentPC, static_cast<size_t>(rowsAbove), capacity);
        });
        if (shown)
        {
            updateSignalIndicators();
            layoutLines();
            // The bank of PC
            updateBankIndicator(currentPC);
        }
    }

    // Update the debugger state indicator
    updateDebuggerStateIndicator();
}

void DisassemblerWidget::refreshPreservingPosition(uint16_t addressToKeep)
{
    qDebug() << "DisassemblerWidget::refreshPreservingPosition() called with address:"
             << QString("0x%1").arg(addressToKeep, 4, 16, QLatin1Char('0')).toUpper();

    // Store the current address to preserve
    m_displayAddress = addressToKeep;

    // Refresh the disassembly view but keep the current address
    setDisassemblerAddress(addressToKeep);

    // Update the debugger state indicator
    updateDebuggerStateIndicator();
}

// Breakpoint methods
bool DisassemblerWidget::hasBreakpointAtAddress(uint16_t address) const
{
    const BreakpointManager* bpManager = getBreakpointManager();
    if (!bpManager)
        return false;

    // Check all breakpoints to see if there's an execution breakpoint at this address
    const auto& allBreakpoints = bpManager->GetAllBreakpoints();
    for (const auto& pair : allBreakpoints)
    {
        const BreakpointDescriptor* bp = pair.second.get();
        if (bp->type == BRK_MEMORY && (bp->memoryType & BRK_MEM_EXECUTE) && bp->z80address == address && bp->active)
        {
            return true;
        }
    }

    return false;
}

void DisassemblerWidget::toggleBreakpointAtAddress(uint16_t address)
{
    if (RemoteControlGuard::Refuse(this))  // an automation session holds the machine
        return;
    BreakpointManager* bpManager = getBreakpointManager();
    if (!bpManager)
        return;

    // Store current view address before toggling breakpoint
    uint16_t currentViewAddress = m_displayAddress;

    // Check if there's already a breakpoint at this address
    if (hasBreakpointAtAddress(address))
    {
        // Find the breakpoint ID and remove it
        const auto& allBreakpoints = bpManager->GetAllBreakpoints();
        for (const auto& pair : allBreakpoints)
        {
            const BreakpointDescriptor* bp = pair.second.get();
            if (bp->type == BRK_MEMORY && (bp->memoryType & BRK_MEM_EXECUTE) && bp->z80address == address)
            {
                bpManager->RemoveBreakpointByID(bp->breakpointID);
                break;
            }
        }
    }
    else
    {
        // Add a new execution breakpoint
        bpManager->AddCombinedMemoryBreakpoint(address, BRK_MEM_EXECUTE);
    }

    // Refresh the disassembly but maintain the current view position
    refreshPreservingPosition(currentViewAddress);
}

void DisassemblerWidget::toggleBreakpointAtCursor()
{
    toggleBreakpointAtAddress(m_cursorAddress.value_or(m_displayAddress));
}

void DisassemblerWidget::showGoToAddressDialog()
{
    bool ok;
    QString text =
        QInputDialog::getText(this, tr("Go to Address"), tr("Enter address (decimal or hex with 0x, $, or # prefix):"),
                              QLineEdit::Normal, QString(), &ok);
    if (ok && !text.isEmpty())
    {
        uint16_t address = parseAddressInput(text);
        goToAddress(address);
    }
}

uint16_t DisassemblerWidget::parseAddressInput(const QString& input)
{
    QString trimmed = input.trimmed();
    bool ok;
    uint16_t address = 0;

    // Check for hex format with various prefixes
    if (trimmed.startsWith("0x", Qt::CaseInsensitive) || trimmed.startsWith("$") || trimmed.startsWith("#"))
    {
        // Remove the prefix
        if (trimmed.startsWith("0x", Qt::CaseInsensitive))
            trimmed = trimmed.mid(2);
        else
            trimmed = trimmed.mid(1);

        // Convert from hex
        address = trimmed.toUInt(&ok, 16);
    }
    else
    {
        // Check if the input looks like hex (contains a-f or A-F)
        static QRegularExpression hexPattern("[a-fA-F]");
        if (hexPattern.match(trimmed).hasMatch())
        {
            // If it contains hex digits, treat as hex
            address = trimmed.toUInt(&ok, 16);
            qDebug() << "Detected hex format without prefix:" << trimmed << "-> 0x" << QString::number(address, 16);
        }
        else
        {
            // Try decimal format
            address = trimmed.toUInt(&ok, 10);
        }
    }

    // Ensure the address is within valid range (0-65535)
    return address & 0xFFFF;
}

void DisassemblerWidget::goToAddress(uint16_t address)
{
    // Go to (Ctrl+G, dbg.goto), a register or stack entry: a jump of the address history
    jumpTo(address);
}

void DisassemblerWidget::prepareForShutdown()
{
    qDebug() << "DisassemblerWidget::prepareForShutdown()";
    m_isShuttingDown = true;
}
