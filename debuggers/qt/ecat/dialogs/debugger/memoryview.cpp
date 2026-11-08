// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Memory dump of the debugger

#include "memoryview.h"

#include <utility>

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "addressmenu.h"
#include "bankstrip.h"
#include "celleditor.h"
#include "debugstyle.h"
#include "debugutil.h"
#include "pagedmemory.h"
#include "dialogs/numfmt.h"

static const unsigned int LINE_BYTES = 16;

static QString owner_text(MemoryMapper * mm, unsigned int address, int base)
{
    unsigned int offset = 0;
    AddressableDevice * d = mm->peek_owner(address, offset);
    if (d == nullptr) return QCoreApplication::translate("MemoryPanel", "nothing");
    return QString::fromStdString(d->name) + " + " + fmt_num(offset, base, addr_digits(offset, base));
}

MemorySource memory_of_cpu(CPU * cpu)
{
    MemorySource s;
    s.title = QString::fromStdString(cpu->name);
    s.size = code_space(cpu);
    s.base = view_base(cpu);
    s.read = [cpu](unsigned int a) { return cpu->peek_mem(a) & 0xFF; };
    s.write = [cpu](unsigned int a, unsigned int v) { cpu->poke_mem(a, v); };
    s.writable = [cpu](unsigned int a) { return cpu->mem_writable(a); };
    //The ВМ3 sees its memory through the MMU, the mapper is physical
    MemoryMapper * mm = cpu->mm;
    const int base = s.base;
    if (mm != nullptr && cpu->type != "1801vm3")
        s.owner = [mm, base](unsigned int a) { return owner_text(mm, a, base); };
    s.windows = [cpu]() { return cpu->get_windows(); };
    s.pc = [cpu]() { return cpu->get_pc(); };
    s.cpu = cpu;
    return s;
}

MemorySource memory_of_device(AddressableDevice * d)
{
    MemorySource s;
    s.title = QString::fromStdString(d->name);
    s.size = d->get_size();
    s.base = view_base(d);
    s.read = [d](unsigned int a) { return d->get_direct(a) & 0xFF; };
    if (MemoryMapper * mm = dynamic_cast<MemoryMapper*>(d)) {
        const int base = s.base;
        s.write = [mm](unsigned int a, unsigned int v) { mm->poke(a, v); };
        s.writable = [mm](unsigned int a) { return mm->writable(a); };
        s.owner = [mm, base](unsigned int a) { return owner_text(mm, a, base); };
    } else {
        //A register is shown, not written: see AddressableDevice::debug_writable()
        s.write = [d](unsigned int a, unsigned int v) { if (d->debug_writable(a)) d->set_value(a, v & 0xFF, true); };
        s.writable = [d](unsigned int a) { return d->debug_writable(a); };
    }
    return s;
}

//------------------------------- MemoryView -------------------------------//

MemoryView::MemoryView(Emulator * e, const MemorySource &source, QWidget *parent):
    QAbstractScrollArea(parent),
    e(e),
    m_src(source),
    m_base(source.base),
    m_word(source.base == 8),
    m_top(0),
    m_cursor(0),
    m_on_chars(false),
    m_snap_from(0),
    m_editing(false),
    m_edit_chars(false)
{
    if (m_src.size == 0) m_src.size = 0x10000;
    setFont(dbg::font());
    QFontMetrics fm(font());
    m_rh = dbg::row_height(fm);
    m_cw = dbg::text_width(fm, "0");
    setFocusPolicy(Qt::StrongFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    //Paints every pixel of itself: nothing beneath it needs painting first
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);

    m_editor = new CellEditor(viewport());
    connect(m_editor, &CellEditor::committed, this, &MemoryView::commit_edit);
    connect(m_editor, &CellEditor::cancelled, this, &MemoryView::close_edit);
    connect(m_editor, &CellEditor::tab_pressed, this, [this]() {
        //Tab keeps the value and goes on; in the characters it only goes on
        const unsigned int next = m_cursor + (m_edit_chars ? 1 : (unsigned int)cell_bytes());
        const bool chars = m_edit_chars;
        if (!chars) commit_edit();
        else close_edit();
        if (next < m_src.size) {
            set_cursor(next, chars);
            start_edit(next, chars);
        }
    });
    connect(m_editor, &QLineEdit::textEdited, this, &MemoryView::on_char_typed);
    update_scrollbar();
    mark_stop();
}

void MemoryView::set_base(int base)
{
    close_edit();
    m_base = base;
    viewport()->update();
}

void MemoryView::set_word(bool word)
{
    close_edit();
    m_word = word;
    if (m_word) m_cursor &= ~1u;
    viewport()->update();
}

int MemoryView::rows() const
{
    const int r = viewport()->height() / m_rh;
    return (r > 0) ? r : 1;
}

unsigned int MemoryView::lines() const
{
    return (m_src.size + LINE_BYTES - 1) / LINE_BYTES;
}

int MemoryView::addr_chars() const
{
    return addr_digits(m_src.origin + m_src.size - 1, m_base);
}

int MemoryView::cell_chars() const
{
    return cell_digits(m_base, m_word);
}

int MemoryView::x_cells() const
{
    return m_cw * (addr_chars() + 2);
}

int MemoryView::x_cell(int index) const
{
    //A wider gap after the eighth byte
    const int gap = (!m_word && index >= 8) ? m_cw : 0;
    return x_cells() + index * m_cw * (cell_chars() + 1) + gap;
}

int MemoryView::x_chars() const
{
    const int cells = (int)LINE_BYTES / cell_bytes();
    return x_cell(cells) + m_cw * 2;
}

unsigned int MemoryView::value_at(unsigned int address) const
{
    unsigned int v = m_src.read(address) & 0xFF;
    if (m_word && address + 1 < m_src.size) v |= (m_src.read(address + 1) & 0xFF) << 8;
    return v;
}

void MemoryView::update_scrollbar()
{
    QScrollBar * sb = verticalScrollBar();
    QSignalBlocker block(sb);
    const int total = (int)lines();
    sb->setRange(0, std::max(0, total - rows()));
    sb->setPageStep(rows());
    sb->setValue((int)(m_top / LINE_BYTES));
    QScrollBar * hb = horizontalScrollBar();
    const int width = x_chars() + m_cw * ((int)LINE_BYTES + 2);
    hb->setRange(0, std::max(0, width - viewport()->width()));
    hb->setPageStep(viewport()->width());
}

void MemoryView::scrollContentsBy(int, int)
{
    m_top = (unsigned int)verticalScrollBar()->value() * LINE_BYTES;
    close_edit();
    viewport()->update();
}

void MemoryView::changeEvent(QEvent *event)
{
    QAbstractScrollArea::changeEvent(event);
    if (event->type() != QEvent::FontChange) return;
    const QFontMetrics fm(font());
    m_rh = dbg::row_height(fm);
    m_cw = dbg::text_width(fm, "0");
    m_painted.clear();
    //The constructor's own font comes before the rest of the view; the first show measures the scrollbar
    if (!testAttribute(Qt::WA_WState_Polished)) return;
    update_scrollbar();
    viewport()->update();
}

void MemoryView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    update_scrollbar();
}

void MemoryView::make_visible()
{
    const unsigned int line = m_cursor / LINE_BYTES;
    const unsigned int top = m_top / LINE_BYTES;
    if (line < top) m_top = line * LINE_BYTES;
    else if (line >= top + (unsigned int)rows()) m_top = (line - rows() + 1) * LINE_BYTES;
    update_scrollbar();
}

void MemoryView::set_cursor(unsigned int address, bool chars)
{
    if (address >= m_src.size) address = m_src.size - 1;
    if (m_word && !chars) address &= ~1u;
    m_cursor = address;
    m_on_chars = chars;
    m_selected = false;
    make_visible();
    viewport()->update();
    emit cursor_moved(m_cursor);
}

void MemoryView::select(unsigned int from, unsigned int to)
{
    if (to < from) std::swap(from, to);
    if (to >= m_src.size) to = m_src.size - 1;
    set_cursor(from, false);
    m_selected = from <= to;
    m_sel_from = from;
    m_sel_to = to;
    viewport()->update();
}

bool MemoryView::selection(unsigned int &from, unsigned int &to) const
{
    from = m_sel_from;
    to = m_sel_to;
    return m_selected;
}

void MemoryView::go_to(unsigned int address)
{
    close_edit();
    set_cursor(address, false);
    //The address at the top, so the place asked for is where the eye goes
    m_top = (m_cursor / LINE_BYTES) * LINE_BYTES;
    update_scrollbar();
    m_top = (unsigned int)verticalScrollBar()->value() * LINE_BYTES;
    viewport()->update();
}

//Only the lines that changed: the window's timer calls this while the machine
//runs
void MemoryView::refresh()
{
    if (!isVisible()) return;
    for (int r = 0; r <= rows() && m_top + (unsigned int)r * LINE_BYTES < m_src.size; r++)
        if (!m_painted.same(r, row_key(r))) viewport()->update(0, r * m_rh, viewport()->width(), m_rh);
}

QString MemoryView::row_key(int row) const
{
    const unsigned int line = m_top + (unsigned int)row * LINE_BYTES;
    QString key = QString::number(line) + ' ' + QString::number(m_base) + (m_word ? 'w' : 'b')
                  + QString::number(horizontalScrollBar()->value()) + (m_on_chars ? 'c' : 'v') + (hasFocus() ? 'f' : ' ');
    if (m_cursor >= line && m_cursor - line < LINE_BYTES) key += QString::number(m_cursor - line);
    if (selected(line, LINE_BYTES)) key += QString(" s%1-%2").arg(m_sel_from).arg(m_sel_to);
    key += window_start(line) + ' ';
    for (unsigned int i = 0; i < LINE_BYTES && line + i < m_src.size; i++) {
        const unsigned int a = line + i;
        const unsigned int v = m_src.read(a) & 0xFF;
        const bool changed = a >= m_snap_from && a - m_snap_from < m_snap.size() && m_snap[a - m_snap_from] != v;
        key += QString::number(v, 16) + (changed ? '*' : ',');
    }
    return key;
}

QString MemoryView::window_start(unsigned int line) const
{
    const int i = window_starting(m_windows, m_src.origin + line);
    return (i < 0) ? QString() : window_label(m_windows[(size_t)i], m_base);
}

void MemoryView::mark_stop()
{
    //A few screens around the view: scrolling a little still shows changes
    const unsigned int span = LINE_BYTES * (unsigned int)rows() * 4;
    m_snap_from = (m_top > span) ? m_top - span : 0;
    const unsigned int to = std::min(m_src.size, m_top + span * 2);
    m_snap.clear();
    for (unsigned int a = m_snap_from; a < to; a++) m_snap.push_back((uint8_t)m_src.read(a));
}

bool MemoryView::find(const std::vector<uint8_t> &pattern)
{
    if (pattern.empty() || pattern.size() > m_src.size) return false;
    const unsigned int n = m_src.size;
    for (unsigned int k = 1; k <= n; k++) {
        const unsigned int a = (m_cursor + k) % n;
        if (a + pattern.size() > n) continue;
        size_t j = 0;
        while (j < pattern.size() && (m_src.read(a + (unsigned int)j) & 0xFF) == pattern[j]) j++;
        if (j == pattern.size()) {
            go_to(a);
            return true;
        }
    }
    return false;
}

void MemoryView::paintEvent(QPaintEvent *event)
{
    QPainter p(viewport());
    const dbg::Colors c = dbg::colors(palette());
    p.fillRect(viewport()->rect(), c.back);
    p.setFont(font());
    const QFontMetrics fm(font());
    const int base_y = (m_rh - fm.height()) / 2 + fm.ascent();
    const int dx = -horizontalScrollBar()->value();
    const int cells = (int)LINE_BYTES / cell_bytes();

    for (int r = 0; r <= rows(); r++) {
        const unsigned int line = m_top + (unsigned int)r * LINE_BYTES;
        if (line >= m_src.size) break;
        const int y = r * m_rh;
        const QRect row(0, y, viewport()->width(), m_rh);
        if (!event->region().intersects(row)) continue;
        m_painted.painted(event, r, row, row_key(r));
        p.setPen(c.address);
        p.drawText(dx + m_cw, y + base_y, fmt_num(m_src.origin + line, m_base, addr_chars()));

        for (int i = 0; i < cells; i++) {
            const unsigned int a = line + (unsigned int)(i * cell_bytes());
            if (a >= m_src.size) break;
            const unsigned int v = value_at(a);
            bool changed = false;
            for (int b = 0; b < cell_bytes(); b++) {
                const unsigned int ab = a + (unsigned int)b;
                if (ab >= m_snap_from && ab - m_snap_from < m_snap.size())
                    changed = changed || m_snap[ab - m_snap_from] != ((v >> (8 * b)) & 0xFF);
            }
            const int x = dx + x_cell(i);
            const bool cur = (m_cursor >= a && m_cursor < a + (unsigned int)cell_bytes());
            if (cur) p.fillRect(x - 1, y, m_cw * cell_chars() + 2, m_rh,
                                (!m_on_chars && hasFocus()) ? c.selection : c.selection.lighter(150));
            else if (selected(a, (unsigned int)cell_bytes())) p.fillRect(x - 1, y, m_cw * cell_chars() + 2, m_rh, c.selection.lighter(170));
            p.setPen(cur && !m_on_chars && hasFocus() ? c.selection_text : (changed ? c.changed : c.text));
            p.drawText(x, y + base_y, fmt_num(v, m_base, cell_chars()));
        }

        for (unsigned int i = 0; i < LINE_BYTES; i++) {
            const unsigned int a = line + i;
            if (a >= m_src.size) break;
            const int x = dx + x_chars() + (int)i * m_cw;
            const bool cur = (a == m_cursor) || (!m_on_chars && m_word && a == m_cursor + 1);
            if (cur) p.fillRect(x, y, m_cw, m_rh,
                                (m_on_chars && hasFocus()) ? c.selection : c.selection.lighter(150));
            else if (selected(a, 1)) p.fillRect(x, y, m_cw, m_rh, c.selection.lighter(170));
            p.setPen(cur && m_on_chars && hasFocus() ? c.selection_text : c.text);
            p.drawText(x, y + base_y, QString::fromStdString(e->translate_char(m_src.read(a) & 0xFF)));
        }

        //A window of the paged space starts at the line: a line over it (the
        //strip above names the page)
        if (!window_start(line).isEmpty()) {
            p.setPen(c.dim);
            p.drawLine(0, y, viewport()->width(), y);
        }
    }
}

bool MemoryView::hit(const QPoint &pos, unsigned int &address, bool &chars) const
{
    const int dx = -horizontalScrollBar()->value();
    const int x = pos.x() - dx;
    const unsigned int line = m_top + (unsigned int)(pos.y() / m_rh) * LINE_BYTES;
    if (line >= m_src.size) return false;
    if (x >= x_chars()) {
        const int i = (x - x_chars()) / m_cw;
        if (i >= (int)LINE_BYTES) return false;
        address = line + (unsigned int)i;
        chars = true;
        return address < m_src.size;
    }
    const int cells = (int)LINE_BYTES / cell_bytes();
    for (int i = cells - 1; i >= 0; i--)
        if (x >= x_cell(i) - m_cw / 2) {
            address = line + (unsigned int)(i * cell_bytes());
            chars = false;
            return address < m_src.size;
        }
    return false;
}

void MemoryView::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    unsigned int a;
    bool chars;
    if (hit(event->pos(), a, chars)) set_cursor(a, chars);
}

void MemoryView::mouseDoubleClickEvent(QMouseEvent *event)
{
    unsigned int a;
    bool chars;
    if (!hit(event->pos(), a, chars)) return;
    set_cursor(a, chars);
    start_edit(m_cursor, chars);
}

void MemoryView::contextMenuEvent(QContextMenuEvent *event)
{
    unsigned int a;
    bool chars;
    if (!hit(event->pos(), a, chars)) return;
    set_cursor(a, chars);
    exec_address_menu(this, event->globalPos(), AddressTarget{m_src.cpu, m_src.origin + a, QString()});
}

void MemoryView::wheelEvent(QWheelEvent *event)
{
    QScrollBar * sb = verticalScrollBar();
    sb->setValue(sb->value() - event->angleDelta().y() / 40);
    event->accept();
}

void MemoryView::keyPressEvent(QKeyEvent *event)
{
    const unsigned int step = m_on_chars ? 1 : (unsigned int)cell_bytes();
    const unsigned int page = LINE_BYTES * (unsigned int)rows();
    switch (event->key()) {
    case Qt::Key_Left:
        if (m_cursor >= step) set_cursor(m_cursor - step, m_on_chars);
        break;
    case Qt::Key_Right:
        set_cursor(m_cursor + step, m_on_chars);
        break;
    case Qt::Key_Up:
        if (m_cursor >= LINE_BYTES) set_cursor(m_cursor - LINE_BYTES, m_on_chars);
        break;
    case Qt::Key_Down:
        if (m_cursor + LINE_BYTES < m_src.size) set_cursor(m_cursor + LINE_BYTES, m_on_chars);
        break;
    case Qt::Key_PageUp:
        m_top = (m_top > page) ? m_top - page : 0;
        set_cursor((m_cursor > page) ? m_cursor - page : m_cursor % LINE_BYTES, m_on_chars);
        break;
    case Qt::Key_PageDown:
        if (m_cursor + page < m_src.size) {
            m_top += page;
            set_cursor(m_cursor + page, m_on_chars);
        }
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        start_edit(m_cursor, m_on_chars);
        break;
    case Qt::Key_Tab:
        set_cursor(m_cursor, !m_on_chars);
        break;
    default:
        QAbstractScrollArea::keyPressEvent(event);
    }
}

//Every byte of the cell is memory: a register is shown, not edited
bool MemoryView::can_write(unsigned int address, unsigned int count) const
{
    if (!m_src.write || AddressRouter::read_only_at(this)) return false;
    if (!m_src.writable) return true;
    for (unsigned int i = 0; i < count; i++)
        if (address + i < m_src.size && !m_src.writable(address + i)) return false;
    return true;
}

void MemoryView::start_edit(unsigned int address, bool chars)
{
    if (!can_write(address, chars ? 1 : (unsigned int)cell_bytes())) {
        QApplication::beep();
        return;
    }
    make_visible();
    const int dx = -horizontalScrollBar()->value();
    const int row = (int)((address - m_top) / LINE_BYTES);
    const int col = (int)(address % LINE_BYTES);
    const int y = row * m_rh;
    m_editing = true;
    m_edit_chars = chars;
    if (chars) {
        m_editor->set_digits(0, 1);
        m_editor->open(QRect(dx + x_chars() + col * m_cw - 2, y, m_cw * 2 + 4, m_rh), QString());
    } else {
        m_editor->set_digits(m_base, cell_chars());
        m_editor->open(QRect(dx + x_cell(col / cell_bytes()) - 2, y, m_cw * (cell_chars() + 1) + 4, m_rh),
                       fmt_num(value_at(address), m_base, cell_chars()));
    }
}

void MemoryView::close_edit()
{
    if (!m_editing) return;
    m_editing = false;
    m_editor->hide();
    setFocus();
}

void MemoryView::write(unsigned int address, const std::vector<uint8_t> &bytes)
{
    const MemorySource &s = m_src;
    e->invoke([&s, address, &bytes]() {
        for (size_t i = 0; i < bytes.size(); i++)
            if (address + i < s.size) s.write(address + (unsigned int)i, bytes[i]);
    });
    viewport()->update();
    emit memory_changed();
}

void MemoryView::commit_edit()
{
    if (!m_editing) return;
    if (m_edit_chars) {
        close_edit();
        return;
    }
    bool ok = false;
    const unsigned int v = m_editor->text().toUInt(&ok, m_base);
    const unsigned int address = m_cursor;
    close_edit();
    if (!ok) return;
    std::vector<uint8_t> bytes;
    bytes.push_back(v & 0xFF);
    if (m_word) bytes.push_back((v >> 8) & 0xFF);
    write(address, bytes);
}

//A character is kept the moment it is typed, and the editor moves on
void MemoryView::on_char_typed(const QString &text)
{
    if (!m_editing || !m_edit_chars || text.isEmpty()) return;
    const int code = e->char_code(text.left(1).toStdString());
    const unsigned int address = m_cursor;
    close_edit();
    if (code < 0) return;
    write(address, std::vector<uint8_t>(1, (uint8_t)code));
    if (address + 1 < m_src.size) {
        set_cursor(address + 1, true);
        start_edit(address + 1, true);
    }
}

//------------------------------- MemoryPanel ------------------------------//

MemoryPanel::MemoryPanel(Emulator * e, const MemorySource &source, QWidget *parent):
    QWidget(parent),
    e(e)
{
    m_view = new MemoryView(e, source, this);
    m_banks = nullptr;

    QVBoxLayout * v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);

    QHBoxLayout * h = m_row = new QHBoxLayout();
    h->setContentsMargins(2, 2, 2, 0);
    h->setSpacing(2);

    m_address = new QLineEdit(this);
    m_address->setFont(dbg::font());
    m_address->setMaximumWidth(dbg::text_width(QFontMetrics(dbg::font()), "0000000000"));
    m_address->setToolTip(tr("Address"));
    connect(m_address, &QLineEdit::returnPressed, this, &MemoryPanel::go);
    h->addWidget(m_address);

    QToolButton * go_button = new QToolButton(this);
    go_button->setIcon(QIcon(":/icons/next"));
    go_button->setToolTip(tr("Go to the address"));
    connect(go_button, &QToolButton::clicked, this, &MemoryPanel::go);
    h->addWidget(go_button);

    m_base_button = new QToolButton(this);
    m_base_button->setCheckable(true);
    m_base_button->setChecked(m_view->base() == 8);
    m_base_button->setToolTip(tr("Octal or hexadecimal"));
    connect(m_base_button, &QToolButton::toggled, this, &MemoryPanel::apply_view);
    h->addWidget(m_base_button);

    m_word_button = new QToolButton(this);
    m_word_button->setCheckable(true);
    m_word_button->setChecked(m_view->word());
    m_word_button->setToolTip(tr("Bytes or words"));
    connect(m_word_button, &QToolButton::toggled, this, &MemoryPanel::apply_view);
    h->addWidget(m_word_button);

    m_find = new QLineEdit(this);
    m_find->setFont(dbg::font());
    m_find->setToolTip(tr("Find: values in the notation of the dump, or \"text\" in the machine's encoding"));
    m_find->setPlaceholderText(tr("Find"));
    connect(m_find, &QLineEdit::returnPressed, this, &MemoryPanel::find_next);
    h->addWidget(m_find, 1);

    QToolButton * find_button = new QToolButton(this);
    find_button->setIcon(QIcon(":/icons/forward"));
    find_button->setToolTip(tr("Find next"));
    connect(find_button, &QToolButton::clicked, this, &MemoryPanel::find_next);
    h->addWidget(find_button);

    QToolButton * load_button = new QToolButton(this);
    load_button->setIcon(QIcon(":/icons/open"));
    load_button->setToolTip(tr("Load a file at the cursor"));
    connect(load_button, &QToolButton::clicked, this, &MemoryPanel::load_file);
    m_load_button = load_button;
    h->addWidget(load_button);

    QToolButton * save_button = new QToolButton(this);
    save_button->setIcon(QIcon(":/icons/file_save"));
    save_button->setToolTip(tr("Save to a file"));
    connect(save_button, &QToolButton::clicked, this, &MemoryPanel::save_file);
    h->addWidget(save_button);

    v->addLayout(h);
    if (source.windows) {
        m_banks = new BankStrip(source.windows, m_view->base(), this);
        if (source.pc) m_banks->set_pc(source.pc);
        m_banks->set_cpu(source.cpu);
        connect(m_banks, &BankStrip::activated, this, [this](unsigned int address) {
            m_view->go_to(address - m_view->source().origin);
            m_view->setFocus();
        });
        m_view->set_windows(m_banks->windows());
        v->addWidget(m_banks);
    }
    v->addWidget(m_view, 1);

    m_status = new QLabel(this);
    m_status->setFont(dbg::font());
    v->addWidget(m_status);

    connect(m_view, &MemoryView::cursor_moved, this, &MemoryPanel::show_cursor);
    apply_view();
    show_cursor(0);
    //Opened in a scaled window (a page of the pages tab): its icons at the scale too
    scale_icons(this, dbg::zoom(dbg::Zoom::Icons));
    //After the fields take the new font and the style its sizes
    QMetaObject::invokeMethod(this, [this]() { even_row(m_row); }, Qt::QueuedConnection);
}

void MemoryPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::FontChange) return;
    m_address->setMaximumWidth(dbg::text_width(QFontMetrics(dbg::font()), "0000000000"));
    scale_icons(this, dbg::zoom(dbg::Zoom::Icons));
    //After the fields take the new font and the style its sizes
    QMetaObject::invokeMethod(this, [this]() { even_row(m_row); }, Qt::QueuedConnection);
}

void MemoryPanel::refresh()
{
    if (m_banks != nullptr && isVisible()) {
        m_banks->refresh();
        m_view->set_windows(m_banks->windows());
    }
    m_view->refresh();
    show_cursor(m_view->cursor());
}

void MemoryPanel::set_read_only(bool on)
{
    if (m_load_button != nullptr) m_load_button->setEnabled(!on);
}

void MemoryPanel::mark_stop()
{
    if (m_banks != nullptr) {
        m_banks->mark_stop();
        m_view->set_windows(m_banks->windows());
    }
    m_view->mark_stop();
}

void MemoryPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    //Hidden, it was not refreshed: the pages may have moved since
    if (m_banks != nullptr) {
        m_banks->refresh();
        m_view->set_windows(m_banks->windows());
    }
}

void MemoryPanel::apply_view()
{
    const int base = m_base_button->isChecked() ? 8 : 16;
    const bool word = m_word_button->isChecked();
    m_base_button->setText(base == 8 ? "Oct" : "Hex");
    m_word_button->setText(word ? tr("Word") : tr("Byte"));
    m_view->set_base(base);
    m_view->set_word(word);
    show_cursor(m_view->cursor());
}

void MemoryPanel::show_cursor(unsigned int address)
{
    const int base = m_view->base();
    const MemorySource &s = m_view->source();
    const int digits = addr_digits(s.origin + s.size - 1, base);
    QString text = fmt_num(s.origin + address, base, digits);
    if (s.owner) text += "  " + s.owner(address);
    m_status->setText(text);
    if (!m_address->hasFocus()) m_address->setText(fmt_num(s.origin + address, base, digits));
}

void MemoryPanel::go()
{
    try {
        const unsigned int a = parse_input(m_address->text(), m_view->base());
        const unsigned int origin = m_view->source().origin;
        m_view->go_to((a > origin) ? a - origin : 0);
        m_view->setFocus();
    } catch (const std::exception &) {
        QMessageBox::critical(this, tr("Error"), tr("Incorrect address value"));
    }
}

void MemoryPanel::find_next()
{
    const QString text = m_find->text().trimmed();
    if (text.isEmpty()) return;
    std::vector<uint8_t> pattern;
    if (text.startsWith('"')) {
        QString body = text.mid(1);
        if (body.endsWith('"')) body.chop(1);
        for (int i = 0; i < body.size(); i++) {
            const int code = e->char_code(QString(body[i]).toStdString());
            if (code < 0) {
                QMessageBox::warning(this, tr("Find"), tr("The machine has no character '%1'").arg(body[i]));
                return;
            }
            pattern.push_back((uint8_t)code);
        }
    } else {
        for (const QString &t : split_words(text)) {
            bool ok = false;
            const unsigned int v = t.toUInt(&ok, m_view->base());
            if (!ok) {
                QMessageBox::warning(this, tr("Find"), tr("Incorrect value: %1").arg(t));
                return;
            }
            pattern.push_back(v & 0xFF);
            if (m_view->word()) pattern.push_back((v >> 8) & 0xFF);
        }
    }
    if (!m_view->find(pattern))
        QMessageBox::information(this, tr("Find"), tr("Not found"));
}

void MemoryPanel::load_file()
{
    if (AddressRouter::read_only_at(this)) return;
    const QString name = QFileDialog::getOpenFileName(this, tr("Load into memory"),
                                                      QString::fromStdString(e->get_last_path()));
    if (name.isEmpty()) return;
    e->set_last_path(QFileInfo(name).absolutePath().toStdString());
    QFile f(name);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Error"), tr("Cannot read the file"));
        return;
    }
    const QByteArray data = f.readAll();
    const MemorySource &s = m_view->source();
    const unsigned int at = m_view->cursor();
    e->invoke([&s, &data, at]() {
        for (int i = 0; i < data.size() && at + (unsigned int)i < s.size; i++)
            s.write(at + (unsigned int)i, (uint8_t)data[i]);
    });
    m_view->refresh();
}

void MemoryPanel::save_file()
{
    const MemorySource &s = m_view->source();
    const QString name = QFileDialog::getSaveFileName(this, tr("Save memory"),
                                                      QString::fromStdString(e->get_last_path()) + "/" + s.title + ".bin",
                                                      tr("Binary files (*.bin);;All files (*.*)"));
    if (name.isEmpty()) return;
    e->set_last_path(QFileInfo(name).absolutePath().toStdString());
    QByteArray data;
    data.resize((int)s.size);
    for (unsigned int a = 0; a < s.size; a++) data[(int)a] = (char)(s.read(a) & 0xFF);
    QFile f(name);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size())
        QMessageBox::critical(this, tr("Error"), tr("Cannot write the file"));
}

//------------------------------- MemoryTabs -------------------------------//

MemoryTabs::MemoryTabs(Emulator * e, QWidget *parent):
    QTabWidget(parent),
    e(e),
    m_paged(nullptr)
{
    setUsesScrollButtons(true);

    //The main processor's space first (what its code sees), its pages, then the cards' processors and the rest
    const std::vector<CPU*> cpus = e->dm->get_cpus();
    if (!cpus.empty()) add_panel(memory_of_cpu(cpus.front()), QString::fromStdString(cpus.front()->name));

    //The pages the processors map, a page at a time
    std::vector<Memory*> paged;
    for (unsigned int i = 0; i < e->dm->device_count; i++) {
        Memory * m = dynamic_cast<Memory*>(e->dm->get_device(i)->device.get());
        if (m != nullptr && m->page_size() > 0 && m->get_size() > 0) paged.push_back(m);
    }
    if (!paged.empty()) {
        m_paged = new PagedMemory(e, paged, this);
        addTab(m_paged, tr("pages"));
        connect(m_paged, &PagedMemory::memory_changed, this, [this]() {
            refresh();
            emit memory_changed();
        });
    }
    for (size_t i = 1; i < cpus.size(); i++) add_panel(memory_of_cpu(cpus[i]), QString::fromStdString(cpus[i]->name));

    //The mappers and the other memories; the rest on demand
    QMenu * more = new QMenu(this);
    for (unsigned int i = 0; i < e->dm->device_count; i++) {
        ComputerDevice * d = e->dm->get_device(i)->device.get();
        AddressableDevice * a = dynamic_cast<AddressableDevice*>(d);
        if (a == nullptr || a->get_size() == 0) continue;
        if (Memory * m = dynamic_cast<Memory*>(d))
            if (m->page_size() > 0) continue;
        if (dynamic_cast<MemoryMapper*>(d) != nullptr || dynamic_cast<Memory*>(d) != nullptr) {
            add_panel(memory_of_device(a), QString::fromStdString(d->name));
        } else {
            QAction * action = more->addAction(QString::fromStdString(d->name + " : " + d->type));
            connect(action, &QAction::triggered, this, [this, a]() {
                add_panel(memory_of_device(a), QString::fromStdString(a->name));
                setCurrentIndex(count() - 1);
            });
        }
    }
    QToolButton * plus = new QToolButton(this);
    plus->setIcon(QIcon(":/icons/cfg_copy"));
    plus->setToolTip(tr("Another device"));
    plus->setPopupMode(QToolButton::InstantPopup);
    plus->setMenu(more);
    plus->setEnabled(!more->isEmpty());
    setCornerWidget(plus, Qt::TopRightCorner);
}

void MemoryTabs::add_panel(const MemorySource &s, const QString &title)
{
    MemoryPanel * m = new MemoryPanel(e, s, this);
    addTab(m, title);
    m_panels.push_back(m);
    connect(m->view(), &MemoryView::memory_changed, this, [this]() {
        refresh();
        emit memory_changed();
    });
}

void MemoryTabs::show_address(CPU * cpu, unsigned int address)
{
    for (MemoryPanel * p : m_panels)
        if (p->view()->source().cpu == cpu) {
            setCurrentWidget(p);
            p->view()->go_to(address - p->view()->source().origin);
            p->view()->setFocus();
            return;
        }
}

MemoryView * MemoryTabs::view_of(CPU * cpu, bool shown)
{
    for (MemoryPanel * p : m_panels)
        if (p->view()->source().cpu == cpu) {
            if (shown) setCurrentWidget(p);
            return p->view();
        }
    return nullptr;
}

void MemoryTabs::show_page(CPU * cpu, unsigned int address)
{
    if (m_paged == nullptr || cpu == nullptr) return;
    const std::vector<AddressWindow> windows = cpu->get_windows();
    const int i = window_at(windows, address);
    if (i < 0) return;
    Memory * m = dynamic_cast<Memory*>(windows[i].device);
    if (m == nullptr || m->page_size() == 0) return;
    setCurrentWidget(m_paged);
    m_paged->show_page(m, windows[i].offset + (address - windows[i].start));
}

void MemoryTabs::refresh()
{
    for (MemoryPanel * m : m_panels) m->refresh();
    if (m_paged != nullptr) m_paged->refresh();
}

void MemoryTabs::set_read_only(bool on)
{
    for (MemoryPanel * m : m_panels) m->set_read_only(on);
    if (m_paged != nullptr) m_paged->set_read_only(on);
}

void MemoryTabs::mark_stop()
{
    for (MemoryPanel * m : m_panels) m->mark_stop();
    if (m_paged != nullptr) m_paged->mark_stop();
}
