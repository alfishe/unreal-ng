// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Listing of a processor's code in the debugger

#include "disasmview.h"

#include <algorithm>
#include <cmath>
#include <set>

#include <QFontMetricsF>
#include <QHeaderView>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QToolTip>
#include <QWheelEvent>

#include "addressmenu.h"
#include "bankstrip.h"
#include "celleditor.h"
#include "debugstyle.h"
#include "debugutil.h"
#include "dialogs/numfmt.h"

static const int ARROW_LANES = 6;
static const int LANE_WIDTH = 7;

DisasmView::DisasmView(Emulator * e, CPU * cpu, QWidget *parent):
    QAbstractScrollArea(parent),
    e(e),
    cpu(cpu),
    m_top(0),
    m_cursor(0),
    m_edit_line(-1),
    m_shown_cursor(~0u)
{
    setFont(dbg::font());
    QFontMetrics fm(font());
    m_rh = dbg::row_height(fm);
    m_cw = dbg::text_width(fm, "0");
    setFocusPolicy(Qt::StrongFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setAutoFillBackground(false);
    //Paints every pixel of itself: nothing beneath it needs painting first
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);

    emulator::Result res = emulator::Result::ok();
    m_disasm = cpu->create_disasm(e->data_path, res);
    m_space = code_space(cpu);
    m_base = view_base(cpu);
    m_addr_digits = addr_digits(m_space - 1, m_base);
    m_word_code = (m_disasm->unit_bytes() == 2 && m_base == 8);

    m_buf.reset(new DisasmBuffer(m_disasm, [cpu](unsigned int a) { return (uint8_t)cpu->peek_mem(a); }, m_space));
    m_buf->reset(cpu->get_pc(), 64);

    m_editor = new CellEditor(viewport());
    connect(m_editor, &CellEditor::committed, this, &DisasmView::commit_edit);
    connect(m_editor, &CellEditor::cancelled, this, &DisasmView::close_edit);
    connect(m_editor, &CellEditor::tab_pressed, this, &DisasmView::commit_edit);

    connect(verticalScrollBar(), &QScrollBar::actionTriggered, this, &DisasmView::on_scroll_action);

    m_label_set = std::clamp(QString::fromStdString(e->read_setup("Debugger", "label_chars", "12")).toInt(), 4, 64);
    m_header = new QHeaderView(Qt::Horizontal, this);
    m_header->setProperty("dbg_fixed_sections", true);
    m_header->setModel(new QStandardItemModel(0, 5, m_header));
    m_header->model()->setHeaderData(1, Qt::Horizontal, tr("Addr"));
    m_header->model()->setHeaderData(2, Qt::Horizontal, tr("Bytes"));
    m_header->model()->setHeaderData(3, Qt::Horizontal, tr("Label"));
    m_header->model()->setHeaderData(4, Qt::Horizontal, tr("Instruction"));
    m_header->model()->setHeaderData(0, Qt::Horizontal, QString());
    m_header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_header->setTextElideMode(Qt::ElideNone);
    m_header->setSectionsClickable(false);
    m_header->setStretchLastSection(true);
    for (int i = 0; i < 4; i++) m_header->setSectionResizeMode(i, i == 3 ? QHeaderView::Interactive : QHeaderView::Fixed);
    m_header->setMinimumSectionSize(1);
    connect(m_header, &QHeaderView::sectionResized, this, [this](int section, int, int size) {
        if (m_syncing || section != 3 || !m_has_labels) return;
        const double w = QFontMetricsF(font()).boundingRect(QString(10, QChar('W'))).width() / 10.0;
        const int chars = std::clamp((int)std::lround((size - 2 * m_cw) / std::max(1.0, w)), 4, 64);
        if (chars == m_label_set) return;
        m_label_set = chars;
        this->e->write_setup("Debugger", "label_chars", std::to_string(chars));
        viewport()->update();
    });
    place_header();
    update_scrollbar();
}

void DisasmView::place_header()
{
    const int h = m_header->sizeHint().height();
    setViewportMargins(0, h, 0, 0);
    const QRect vp = viewport()->geometry();
    m_header->setGeometry(vp.left(), vp.top() - h, vp.width(), h);
    sync_header();
}

void DisasmView::sync_header()
{
    if (m_header == nullptr) return;
    m_syncing = true;
    m_header->resizeSection(0, x_address());
    m_header->resizeSection(1, x_bytes() - x_address());
    m_header->resizeSection(2, x_label() - x_bytes());
    m_header->setSectionHidden(3, m_label_px == 0);
    if (m_label_px) m_header->resizeSection(3, m_label_px);
    m_syncing = false;
}

DisasmView::~DisasmView()
{
    m_buf.reset();
    delete m_disasm;
}

int DisasmView::rows() const
{
    const int r = viewport()->height() / m_rh;
    return (r > 0) ? r : 1;
}

int DisasmView::bytes_chars() const
{
    const unsigned int n = m_disasm->max_command_length;
    if (m_word_code) return 7 * (int)((n + 1) / 2);
    return 3 * (int)n;
}

int DisasmView::x_marks() const   { return ARROW_LANES * LANE_WIDTH + 14; }
int DisasmView::x_address() const { return x_marks() + m_cw * 2; }
int DisasmView::x_bytes() const   { return x_address() + m_cw * (format_address(0).size() + 2); }
int DisasmView::x_label() const   { return x_bytes() + m_cw * (bytes_chars() + 1) + m_cw / 2; }
int DisasmView::x_text() const    { return x_label() + m_label_px; }

bool DisasmView::follow_labels()
{
    const unsigned int serial = m_disasm->labels_serial();
    const bool has = m_disasm->label_chars() > 0;
    if (serial == m_labels_serial && has == m_has_labels) return false;
    m_labels_serial = serial;
    m_has_labels = has;
    return true;
}

bool DisasmView::label_cut(const std::string &label) const
{
    return QFontMetricsF(font()).boundingRect(QString::fromStdString(label)).width() > m_label_px - 2 * m_cw;
}

void DisasmView::contextMenuEvent(QContextMenuEvent *event)
{
    //Coming through the viewport, the position is the viewport's
    const int i = row_at(event->pos().y());
    if (i < 0 || i >= (int)m_buf->size()) return;
    const DisasmBuffer::Line &l = m_buf->at(i);
    QMenu own;
    if (l.d.has_target) {
        const unsigned int t = l.d.target & 0xFFFF;
        QMenu * target = own.addMenu(tr("Target #%1").arg(QString::number(t, 16).toUpper().rightJustified(4, '0')));
        add_address_actions(target, this, AddressTarget{cpu, t, tr("target")});
    }
    exec_address_menu(this, event->globalPos(), AddressTarget{cpu, l.address, QString()}, &own);
}

bool DisasmView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::ToolTip && m_label_chars) {
        const QHelpEvent * h = static_cast<QHelpEvent*>(event);
        const int i = row_at(h->pos().y());
        if (i >= 0 && i < (int)m_buf->size() && h->pos().x() >= x_label() && h->pos().x() < x_text()) {
            const std::string &label = m_buf->at(i).d.label;
            if (!label.empty()) {
                QToolTip::showText(h->globalPos(), QString::fromStdString(label), viewport());
                return true;
            }
        }
        QToolTip::hideText();
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
}

QString DisasmView::format_address(unsigned int address) const
{
    return code_address(cpu, address, m_base, m_addr_digits);
}

QString DisasmView::describe(unsigned int address) const
{
    uint8_t b[15] = {0};
    unsigned int n = m_disasm->max_command_length;
    if (n > sizeof(b)) n = sizeof(b);
    for (unsigned int i = 0; i < n; i++) b[i] = (uint8_t)cpu->peek_mem(address + i);
    DisasmLine l;
    m_disasm->decode(&b, address, n, &l);
    return format_address(address) + "  " + QString::fromStdString(l.text);
}

QString DisasmView::bytes_text(const DisasmBuffer::Line &l) const
{
    QString s;
    if (m_word_code) {
        for (size_t i = 0; i + 1 < l.bytes.size(); i += 2) {
            if (!s.isEmpty()) s += ' ';
            s += fmt_num(l.bytes[i] | (l.bytes[i + 1] << 8), 8, 6);
        }
        if (l.bytes.size() % 2) s += (s.isEmpty() ? "" : " ") + fmt_num(l.bytes.back(), 8, 3);
    } else {
        for (size_t i = 0; i < l.bytes.size(); i++) {
            if (!s.isEmpty()) s += ' ';
            s += fmt_num(l.bytes[i], 16, 2);
        }
    }
    return s;
}

int DisasmView::row_at(int y) const
{
    return m_top + y / m_rh;
}

QString DisasmView::window_start(int line) const
{
    if (m_windows.empty() || line < 0 || line >= (int)m_buf->size()) return QString();
    const unsigned int a = m_buf->at(line).address;
    const bool prev = line > 0 && m_buf->at(line - 1).address < a;
    const unsigned int from = prev ? m_buf->at(line - 1).address : a;
    for (const AddressWindow &w : m_windows)
        if (w.start == a || (prev && w.start > from && w.start <= a)) return window_label(w, m_base);
    return QString();
}

template <typename F> void DisasmView::keep_place(F op)
{
    const int size = (int)m_buf->size();
    const bool had = size > 0;
    auto line = [size](int i) { return (i < 0) ? 0 : (i >= size ? size - 1 : i); };
    const unsigned int top = had ? m_buf->at(line(m_top)).address : 0;
    const unsigned int cur = had ? m_buf->at(line(m_cursor)).address : 0;
    op();
    if (m_buf->size() == 0) { m_top = m_cursor = 0; return; }
    //An address that no longer starts a line (new bytes decoded differently)
    //keeps the line it now lies in
    auto place = [this](unsigned int a) {
        const int i = m_buf->find(a);
        return (i >= 0) ? i : m_buf->find_containing(a);
    };
    int t = had ? place(top) : -1;
    int c = had ? place(cur) : -1;
    m_top = (t >= 0) ? t : 0;
    m_cursor = (c >= 0) ? c : m_top;
}

//Lines enough below the top for a full view
void DisasmView::fill()
{
    const int need = m_top + rows() + 1 - (int)m_buf->size();
    if (need > 0) keep_place([&]() { m_buf->extend_down((size_t)need); });
    if (m_top >= (int)m_buf->size()) m_top = (int)m_buf->size() - 1;
    if (m_cursor >= (int)m_buf->size()) m_cursor = (int)m_buf->size() - 1;
    if (m_top < 0) m_top = 0;
    if (m_cursor < 0) m_cursor = 0;
}

void DisasmView::make_visible()
{
    if (m_cursor < m_top) m_top = m_cursor;
    if (m_cursor >= m_top + rows()) m_top = m_cursor - rows() + 1;
    if (m_top < 0) m_top = 0;
    fill();
}

void DisasmView::move_cursor(int delta, bool page)
{
    int target = m_cursor + delta;
    if (target < 0) {
        keep_place([&]() { m_buf->extend_up((size_t)(-target)); });
        target = m_cursor + delta;
        if (target < 0) target = 0;
    }
    if (target >= (int)m_buf->size()) {
        keep_place([&]() { m_buf->extend_down((size_t)(target - (int)m_buf->size() + 1)); });
        if (target >= (int)m_buf->size()) target = (int)m_buf->size() - 1;
    }
    if (page) {
        //The page moves with the cursor, as far as the listing allows
        m_top += target - m_cursor;
        if (m_top < 0) m_top = 0;
    }
    m_cursor = target;
    make_visible();
    update_scrollbar();
    viewport()->update();
}

void DisasmView::scroll_rows(int delta)
{
    int target = m_top + delta;
    if (target < 0) {
        keep_place([&]() { m_buf->extend_up((size_t)(-target)); });
        target = m_top + delta;
        if (target < 0) target = 0;
    }
    m_top = target;
    fill();
    if (m_top > (int)m_buf->size() - 1) m_top = (int)m_buf->size() - 1;
    update_scrollbar();
    viewport()->update();
}

void DisasmView::top_to(unsigned int address)
{
    int i = m_buf->find_containing(address);
    if (i < 0) {
        m_buf->reset(address, rows() + 1);
        i = 0;
        m_cursor = 0;
    }
    m_top = i;
    fill();
    update_scrollbar();
    viewport()->update();
}

void DisasmView::update_scrollbar()
{
    QScrollBar * sb = verticalScrollBar();
    const unsigned int unit = m_disasm->unit_bytes();
    QSignalBlocker block(sb);
    sb->setRange(0, (int)((m_space - 1) / unit));
    sb->setPageStep(rows());
    if (m_buf->size() > 0) sb->setValue((int)(m_buf->at(m_top).address / unit));
}

void DisasmView::on_scroll_action(int action)
{
    QScrollBar * sb = verticalScrollBar();
    switch (action) {
    case QAbstractSlider::SliderSingleStepAdd: scroll_rows(1); break;
    case QAbstractSlider::SliderSingleStepSub: scroll_rows(-1); break;
    case QAbstractSlider::SliderPageStepAdd:   scroll_rows(rows()); break;
    case QAbstractSlider::SliderPageStepSub:   scroll_rows(-rows()); break;
    case QAbstractSlider::SliderMove:
    case QAbstractSlider::SliderToMinimum:
    case QAbstractSlider::SliderToMaximum:
        top_to((unsigned int)sb->sliderPosition() * m_disasm->unit_bytes());
        break;
    default:
        break;
    }
    if (m_buf->size() > 0)
        sb->setSliderPosition((int)(m_buf->at(m_top).address / m_disasm->unit_bytes()));
}

void DisasmView::scrollContentsBy(int, int)
{
    //The scroll bar is driven by on_scroll_action(): its position is an
    //address, not a pixel offset
}

void DisasmView::update_anchors()
{
    std::set<unsigned int> anchors;
    for (unsigned int a : cpu->get_history()) anchors.insert(a);
    for (unsigned int a : cpu->get_breakpoints()) anchors.insert(a);
    anchors.insert(cpu->get_pc());
    m_buf->set_anchors(anchors);
}

void DisasmView::show_pc()
{
    close_edit();
    const unsigned int pc = cpu->get_pc();
    update_anchors();
    keep_place([&]() { m_buf->refresh(); });
    int i = m_buf->find(pc);
    if (i < 0) {
        m_buf->reset(pc, rows() + 1);
        //Some context above PC: where it came from is usually just there
        m_buf->extend_up((size_t)(rows() / 4));
        i = m_buf->find(pc);
        m_top = 0;
    }
    m_cursor = (i >= 0) ? i : 0;
    //Kept where it is if PC is already in view, else a quarter from the top
    //(m_top stays a line of the buffer until then: keep_place() reads it)
    if (m_cursor < m_top || m_cursor >= m_top + rows()) {
        const int above = rows() / 4;
        if (m_cursor < above)
            keep_place([&]() { m_buf->extend_up((size_t)(above - m_cursor)); });
        m_top = m_cursor - above;
        if (m_top < 0) m_top = 0;
    }
    fill();
    update_scrollbar();
    viewport()->update();
}

void DisasmView::go_to(unsigned int address)
{
    close_edit();
    address %= m_space;
    int i = m_buf->find(address);
    if (i < 0) {
        m_buf->reset(address, rows() + 1);
        i = 0;
        m_top = 0;
    }
    m_cursor = i;
    make_visible();
    update_scrollbar();
    viewport()->update();
}

unsigned int DisasmView::cursor_address() const
{
    return (m_buf->size() > 0) ? m_buf->at(m_cursor).address : 0;
}

bool DisasmView::cursor_line(DisasmLine &line) const
{
    if (m_buf->size() == 0) return false;
    line = m_buf->at(m_cursor).d;
    return true;
}

void DisasmView::refresh_memory()
{
    const bool labels = follow_labels();
    keep_place([&]() { m_buf->refresh(labels); });
    fill();
    viewport()->update();
}

void DisasmView::follow_memory()
{
    if (m_edit_line >= 0) return;
    const bool labels = follow_labels();
    bool changed = labels;
    keep_place([&]() { changed = m_buf->refresh(labels) || changed; });
    if (changed) fill();
    repaint_changed();
}

QString DisasmView::row_key(int row, unsigned int pc) const
{
    const int i = m_top + row;
    if (i >= (int)m_buf->size()) return QString();
    const DisasmBuffer::Line &l = m_buf->at(i);
    const int cursor = (i == m_cursor) ? (hasFocus() ? 2 : 1) : 0;
    return QString::number(l.address) + ' ' + bytes_text(l) + ' ' + QString::fromStdString(l.d.text) + ' '
           + QString::fromStdString(l.d.label) + ' ' + QString::number(m_label_chars) + ' ' + QString::number(l.d.valid ? 1 : 0) + QString::number(l.d.flow) + QString::number(l.address == pc ? 1 : 0)
           + QString::number(cpu->has_breakpoint(l.address) ? 1 : 0) + QString::number(cursor) + window_start(i);
}

QString DisasmView::arrows_key(const std::vector<DisasmBuffer::Arrow> &arrows, int pc_row) const
{
    QString key;
    for (const DisasmBuffer::Arrow &a : arrows)
        key += QString::number(a.from - m_top) + ',' + QString::number(a.to - m_top) + ',' + QString::number(a.lane)
               + (a.conditional ? 'c' : 'u') + (a.from == pc_row ? 'a' : ' ');
    return key;
}

int DisasmView::pc_row(unsigned int pc) const
{
    int row = -1;
    for (int r = 0; r < rows() + 1 && m_top + r < (int)m_buf->size(); r++)
        if (m_buf->at(m_top + r).address == pc) row = m_top + r;
    return row;
}

void DisasmView::repaint_changed()
{
    if (!isVisible()) return;
    const unsigned int pc = cpu->get_pc();
    const int n = rows() + 1;
    const int w = viewport()->width();
    for (int r = 0; r < n; r++)
        if (!m_painted.same(r, row_key(r, pc))) viewport()->update(0, r * m_rh, w, m_rh);
    const std::vector<DisasmBuffer::Arrow> arrows = m_buf->layout_jumps(m_top, m_top + n - 1, ARROW_LANES);
    if (!m_painted_arrows.same(0, arrows_key(arrows, pc_row(pc))))
        viewport()->update(0, 0, x_marks(), viewport()->height());
}

void DisasmView::toggle_breakpoint(unsigned int address)
{
    CPU * c = cpu;
    Emulator * em = e;
    e->invoke([c, em, address]() {
        if (c->has_breakpoint(address)) c->remove_breakpoint(address);
        else c->add_breakpoint(address);
        //A processor running without them has to look from now on, and one
        //left with nothing to look for need not
        if (!em->debug_paused() && c->m_debug == DEBUG_OFF && !c->get_breakpoints().empty())
            c->m_debug = DEBUG_BRAKES;
        else if (c->m_debug == DEBUG_BRAKES && c->get_breakpoints().empty() && !c->has_temp_break())
            c->m_debug = DEBUG_OFF;
    });
    viewport()->update();
    emit breakpoints_changed();
}

void DisasmView::changeEvent(QEvent *event)
{
    QAbstractScrollArea::changeEvent(event);
    if (event->type() != QEvent::FontChange) return;
    const QFontMetrics fm(font());
    m_rh = dbg::row_height(fm);
    m_cw = dbg::text_width(fm, "0");
    m_painted.clear();
    m_painted_arrows.clear();
    //The constructor's own font comes before the rest of the view; the first show measures the scrollbar
    if (!testAttribute(Qt::WA_WState_Polished)) return;
    place_header();
    update_scrollbar();
    viewport()->update();
}

void DisasmView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    place_header();
    fill();
    update_scrollbar();
}

void DisasmView::paintEvent(QPaintEvent *event)
{
    //The labels' column, measured in the font (glyphs advance by fractions of a pixel): on a new width every row is
    //drawn again
    const int label_chars = m_has_labels ? m_label_set : 0;
    const int label_px = label_chars
        ? (int)std::ceil(QFontMetricsF(font()).boundingRect(QString(label_chars, QChar('W'))).width()) + 2 * m_cw
        : 0;
    if (label_chars != m_label_chars || label_px != m_label_px) {
        m_label_chars = label_chars;
        m_label_px = label_px;
        sync_header();
        m_painted.clear();
        if (!event->rect().contains(viewport()->rect())) viewport()->update();
    }
    QPainter p(viewport());
    const dbg::Colors c = dbg::colors(palette());
    const int w = viewport()->width();
    const int h = viewport()->height();
    p.fillRect(0, 0, w, h, c.back);
    p.fillRect(0, 0, x_marks() - 2, h, c.gutter);
    p.setFont(font());
    const QFontMetrics fm(font());
    const int base_y = (m_rh - fm.height()) / 2 + fm.ascent();

    const unsigned int pc = cpu->get_pc();
    const int n = rows() + 1;
    const int active_row = pc_row(pc);

    for (int r = 0; r < n; r++) {
        const int i = m_top + r;
        const int y = r * m_rh;
        const QRect row(0, y, w, m_rh);
        if (!event->region().intersects(row)) continue;
        m_painted.painted(event, r, row, row_key(r, pc));
        if (i >= (int)m_buf->size()) continue;
        const DisasmBuffer::Line &l = m_buf->at(i);
        const bool at_pc = (l.address == pc);

        if (i == m_cursor)
            p.fillRect(x_marks(), y, w - x_marks(), m_rh, hasFocus() ? c.selection : c.selection.lighter(130));
        else if (at_pc)
            p.fillRect(x_marks(), y, w - x_marks(), m_rh, c.pc_back);

        //Marks: a breakpoint dot, the PC triangle over it
        const int mx = x_marks() + 1;
        const int my = y + m_rh / 2;
        if (cpu->has_breakpoint(l.address)) {
            p.setPen(Qt::NoPen);
            p.setBrush(c.breakpoint);
            p.drawEllipse(QPoint(mx + m_cw / 2 + 1, my), m_rh / 3, m_rh / 3);
        }
        if (at_pc) {
            QPainterPath tri;
            tri.moveTo(mx, my - m_rh / 3);
            tri.lineTo(mx + m_cw + 2, my);
            tri.lineTo(mx, my + m_rh / 3);
            tri.closeSubpath();
            p.fillPath(tri, (i == m_cursor) ? c.selection_text : c.text);
        }

        const bool sel = (i == m_cursor);
        p.setPen(sel ? c.selection_text : (cpu->has_breakpoint(l.address) ? c.breakpoint : c.address));
        p.drawText(x_address(), y + base_y, format_address(l.address));
        p.setPen(sel ? c.selection_text : c.dim);
        p.drawText(x_bytes(), y + base_y, bytes_text(l));
        if (m_label_chars && !l.d.label.empty()) {
            const QColor lc = sel ? c.selection_text : c.reg_name;
            const int edge = x_text() - 2 * m_cw;
            if (label_cut(l.d.label)) {
                //Cut at the column's edge, the last two characters fading out to it
                QLinearGradient fade(edge - 2 * m_cw, 0, edge, 0);
                QColor clear = lc;
                clear.setAlpha(0);
                fade.setColorAt(0, lc);
                fade.setColorAt(1, clear);
                p.save();
                p.setClipRect(x_label(), y, edge - x_label(), m_rh);
                p.setPen(QPen(QBrush(fade), 1));
                p.drawText(x_label(), y + base_y, QString::fromStdString(l.d.label));
                p.restore();
            } else {
                p.setPen(lc);
                p.drawText(x_label(), y + base_y, QString::fromStdString(l.d.label));
            }
        }

        QColor tc = c.text;
        if (!l.d.valid) tc = c.dim;
        else if (l.d.flow == FLOW_JUMP) tc = c.jump;
        else if (l.d.flow == FLOW_CALL) tc = c.call;
        else if (l.d.flow == FLOW_RET) tc = c.ret;
        p.setPen(sel ? c.selection_text : tc);
        p.drawText(x_text(), y + base_y, QString::fromStdString(l.d.text));

        //A window of the paged space starts here: a line over the row and its
        //page at the right
        const QString start = window_start(i);
        if (!start.isEmpty()) {
            p.setPen(c.dim);
            p.drawLine(x_marks(), y, w, y);
            const int tw = dbg::text_width(fm, start) + m_cw;
            const int tx = std::max(x_text() + m_cw * 16, w - tw);
            if (!sel) p.fillRect(tx - m_cw / 2, y + 1, w - tx + m_cw / 2, m_rh - 1, at_pc ? c.pc_back : c.back);
            p.setPen(sel ? c.selection_text : c.dim);
            p.drawText(tx, y + base_y, start);
        }
    }

    //Thin lines between the address, the bytes and the instruction
    p.setPen(c.separator);
    const int x1 = x_bytes() - m_cw;
    const int x2 = x_text() - m_cw;
    p.drawLine(x1, 0, x1, h);
    p.drawLine(x2, 0, x2, h);
    if (m_label_chars) p.drawLine(x_label() - m_cw, 0, x_label() - m_cw, h);

    //Every move of the cursor ends in a repaint: that is where it is told
    const unsigned int cur = cursor_address();
    if (cur != m_shown_cursor) {
        m_shown_cursor = cur;
        emit cursor_moved(cur);
    }

    //Jump arrows, the shortest nearest to the listing
    const std::vector<DisasmBuffer::Arrow> arrows = m_buf->layout_jumps(m_top, m_top + n - 1, ARROW_LANES);
    m_painted_arrows.painted(event, 0, QRect(0, 0, x_marks(), h), arrows_key(arrows, active_row));
    const int edge = x_marks() - 3;
    p.setBrush(Qt::NoBrush);
    for (const DisasmBuffer::Arrow &a : arrows) {
        const bool active = (a.from == active_row);
        QPen pen(active ? c.arrow_active : c.arrow);
        pen.setWidth(active ? 2 : 1);
        if (a.conditional) pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        const int x = edge - 9 - a.lane * LANE_WIDTH;
        auto row_y = [&](int row) -> int {
            const int rr = row - m_top;
            if (rr < 0) return -1;
            if (rr >= n) return h + 1;
            return rr * m_rh + m_rh / 2;
        };
        const int y1 = row_y(a.from);
        const int y2 = row_y(a.to);
        const bool from_in = y1 >= 0 && y1 <= h;
        const bool to_in = y2 >= 0 && y2 <= h;
        if (from_in) p.drawLine(edge, y1, x, y1);
        p.drawLine(x, y1 < 0 ? 0 : (y1 > h ? h : y1), x, y2 < 0 ? 0 : (y2 > h ? h : y2));
        if (to_in) {
            p.drawLine(x, y2, edge, y2);
            QPainterPath head;
            head.moveTo(edge + 1, y2);
            head.lineTo(edge - 5, y2 - 4);
            head.lineTo(edge - 5, y2 + 4);
            head.closeSubpath();
            p.fillPath(head, pen.color());
        }
    }
}

void DisasmView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Up:       move_cursor(-1, false); break;
    case Qt::Key_Down:     move_cursor(1, false); break;
    case Qt::Key_PageUp:   move_cursor(-rows(), true); break;
    case Qt::Key_PageDown: move_cursor(rows(), true); break;
    case Qt::Key_Home:     show_pc(); break;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        //Into the jump or the call
        DisasmLine l;
        if (cursor_line(l) && l.has_target) go_to(l.target);
        break;
    }
    default:
        QAbstractScrollArea::keyPressEvent(event);
    }
}

void DisasmView::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    const int i = row_at(event->pos().y());
    if (i >= 0 && i < (int)m_buf->size()) {
        if (event->pos().x() >= x_marks() && event->pos().x() < x_address() && event->button() == Qt::LeftButton) {
            toggle_breakpoint(m_buf->at(i).address);
            return;
        }
        m_cursor = i;
        viewport()->update();
    }
}

void DisasmView::mouseDoubleClickEvent(QMouseEvent *event)
{
    const int i = row_at(event->pos().y());
    if (i < 0 || i >= (int)m_buf->size()) return;
    const int x = event->pos().x();
    if (x >= x_address() && x < x_bytes()) toggle_breakpoint(m_buf->at(i).address);
    else if (x >= x_bytes() && x < x_label()) start_edit(i);
    else if (x >= x_text()) {
        const DisasmLine &l = m_buf->at(i).d;
        if (l.has_target) go_to(l.target);
    }
}

void DisasmView::wheelEvent(QWheelEvent *event)
{
    const int steps = event->angleDelta().y() / 40;
    if (steps != 0) scroll_rows(-steps);
    event->accept();
}

void DisasmView::start_edit(int line)
{
    const DisasmBuffer::Line &l = m_buf->at(line);
    m_edit_line = line;
    const QString text = bytes_text(l);
    m_editor->set_digits(m_word_code ? 8 : 16, text.size() + 8);
    const int y = (line - m_top) * m_rh;
    m_editor->open(QRect(x_bytes() - 2, y, m_cw * (bytes_chars() + 1) + 4, m_rh), text);
}

void DisasmView::close_edit()
{
    if (m_edit_line < 0) return;
    m_edit_line = -1;
    m_editor->hide();
    setFocus();
}

//The bytes as typed: octal words on the PDP-11, hex bytes elsewhere. They go
//to memory as the debugger writes (CPU::poke_mem: a ROM takes them too)
void DisasmView::commit_edit()
{
    if (m_edit_line < 0) return;
    const unsigned int address = m_buf->at(m_edit_line).address;
    const QStringList parts = split_words(m_editor->text());
    std::vector<uint8_t> bytes;
    for (const QString &t : parts) {
        bool ok = false;
        const unsigned int v = t.toUInt(&ok, m_word_code ? 8 : 16);
        if (!ok) continue;
        if (m_word_code) {
            bytes.push_back(v & 0xFF);
            bytes.push_back((v >> 8) & 0xFF);
        } else {
            bytes.push_back(v & 0xFF);
        }
    }
    close_edit();
    if (bytes.empty()) return;
    CPU * c = cpu;
    e->invoke([c, address, bytes]() {
        for (size_t i = 0; i < bytes.size(); i++) c->poke_mem(address + (unsigned int)i, bytes[i]);
    });
    refresh_memory();
    emit memory_changed();
}
