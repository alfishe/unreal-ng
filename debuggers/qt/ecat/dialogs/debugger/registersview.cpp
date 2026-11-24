// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Registers and flags of a processor in the debugger

#include "registersview.h"

#include <QDateTime>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>

#include "addressmenu.h"
#include "celleditor.h"
#include "debugstyle.h"
#include "debugutil.h"
#include "dialogs/numfmt.h"

//A register written as a script would set it: the recording replays it
static void set_register(Emulator * e, CPU * cpu, const std::string &name, unsigned int value)
{
    e->invoke([cpu, name, value]() { cpu->set_context_value(name, value); });
    e->record_command(cpu->name, "setreg", name + ", $" + hex_str(value, 1));
}

//----------------------------- RegistersView ------------------------------//

RegistersView::RegistersView(Emulator * e, CPU * cpu, QWidget *parent):
    QAbstractScrollArea(parent),
    e(e),
    cpu(cpu),
    m_base(view_base(cpu)),
    m_name_chars(2),
    m_edit_row(-1),
    m_edit_base(16)
{
    setFont(dbg::font());
    QFontMetrics fm(font());
    m_rh = dbg::row_height(fm);
    m_cw = dbg::text_width(fm, "0");
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFocusPolicy(Qt::ClickFocus);
    //Paints every pixel of itself: nothing beneath it needs painting first
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);

    m_editor = new CellEditor(viewport());
    connect(m_editor, &CellEditor::committed, this, &RegistersView::commit_edit);
    connect(m_editor, &CellEditor::tab_pressed, this, &RegistersView::commit_edit);
    connect(m_editor, &CellEditor::cancelled, this, &RegistersView::close_edit);

    refresh();
    mark_stop();
}

//The card's clock (not in ecat): the cycle in the ZX frame and the cycles since the value shown before, as the
//embedded debugger's T field ("c119985 Δ12"); with no count since the card started (over the WebAPI), the change is
//taken within one frame. While the machine runs, read at the card line's pace
void RegistersView::add_cycle_row()
{
    m_cycle_row = -1;
    CPU::CardCycle now;
    const bool running = !e->debug_paused();
    const qint64 ms = QDateTime::currentMSecsSinceEpoch();
    const bool due = !running || m_cycle_read_ms < 0 || ms - m_cycle_read_ms >= (qint64)e->live_card_ms();
    if (due) {
        if (!cpu->card_cycle(now)) {
            m_cycle_text.clear();
            m_cycle_known = false;
            return;
        }
        m_cycle_read_ms = ms;
        if (m_cycle_known) {
            if (now.total >= 0 && m_cycle_shown.total >= 0) {
                if (now.total != m_cycle_shown.total) m_cycle_delta = now.total - m_cycle_shown.total;
            } else if (now.frame != m_cycle_shown.frame || now.frame_cycle != m_cycle_shown.frame_cycle) {
                m_cycle_delta = now.frame == m_cycle_shown.frame ? now.frame_cycle - m_cycle_shown.frame_cycle : 0;
            }
        }
        m_cycle_shown = now;
        m_cycle_known = true;
        m_cycle_text = QString("c%1").arg(now.frame_cycle);
        if (m_cycle_delta != 0) m_cycle_text += QString(" ") + QChar(0x0394) + QString::number(m_cycle_delta);
        m_cycle_tip = tr("The card's CPU cycle in the ZX frame (%1 MHz); %2: its cycles since the value shown before")
                          .arg(now.clock_hz / 1000000).arg(QChar(0x0394));
        if (now.total < 0) m_cycle_tip += tr(" (within one frame)");
    }
    if (!m_cycle_known) return;
    m_regs.push_back(CpuRegister());
    m_regs.push_back(CpuRegister{"T", (unsigned int)m_cycle_shown.frame_cycle, 0});
    m_cycle_row = (int)m_regs.size() - 1;
}

bool RegistersView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        const QHelpEvent * h = static_cast<QHelpEvent*>(event);
        if (m_cycle_row >= 0 && first_row() + h->pos().y() / m_rh == m_cycle_row) QToolTip::showText(h->globalPos(), m_cycle_tip, viewport());
        else QToolTip::hideText();
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
}

void RegistersView::refresh()
{
    m_regs = cpu->get_register_values();
    add_cycle_row();
    m_name_chars = 2;
    for (const CpuRegister &r : m_regs)
        if ((int)r.name.size() > m_name_chars) m_name_chars = (int)r.name.size();
    verticalScrollBar()->setRange(0, std::max(0, (int)m_regs.size() - viewport()->height() / m_rh));
    verticalScrollBar()->setPageStep(viewport()->height() / m_rh);
    //Only the rows that changed: the window's timer calls this while the
    //machine runs
    if (!isVisible()) return;
    for (int r = 0; first_row() + r < (int)m_regs.size(); r++)
        if (!m_painted.same(r, row_key(r))) viewport()->update(0, r * m_rh, viewport()->width(), m_rh);
}

QString RegistersView::row_key(int row) const
{
    const int i = first_row() + row;
    if (i < 0 || i >= (int)m_regs.size() || m_regs[i].name.empty()) return QString();
    const CpuRegister &reg = m_regs[i];
    auto it = m_prev.find(reg.name);
    const bool changed = it != m_prev.end() && it->second != reg.value;
    return QString::fromStdString(reg.name) + ' ' + QString::number(reg.value) + (changed ? " c " : " - ")
           + QString::number(x_value()) + (i == m_cycle_row ? ' ' + m_cycle_text : QString());
}

void RegistersView::mark_stop()
{
    m_prev.clear();
    for (const CpuRegister &r : m_regs)
        if (!r.name.empty()) m_prev[r.name] = r.value;
}

QSize RegistersView::sizeHint() const
{
    int w = 0;
    for (const CpuRegister &r : m_regs)
        w = std::max(w, x_binary() + m_cw * binary_chars(r));
    if (m_cycle_row >= 0) w = std::max(w, x_value() + m_cw * (int)m_cycle_text.size());
    return QSize(w + m_cw * 2, m_rh * ((int)m_regs.size() + 1));
}

int RegistersView::value_chars(const CpuRegister &r) const
{
    if (m_base == 8) return (r.bits + 2) / 3;
    return (r.bits + 3) / 4;
}

int RegistersView::binary_chars(const CpuRegister &r) const
{
    const int group = (m_base == 8) ? 3 : 4;
    return (int)r.bits + ((int)r.bits - 1) / group;
}

QString RegistersView::value_text(const CpuRegister &r) const
{
    return fmt_num(r.value, m_base, value_chars(r));
}

//Grouped as the digits of the notation: by three bits for octal
QString RegistersView::binary_text(const CpuRegister &r) const
{
    const int group = (m_base == 8) ? 3 : 4;
    QString s;
    for (int b = (int)r.bits - 1; b >= 0; b--) {
        s += ((r.value >> b) & 1) ? '1' : '0';
        if (b > 0 && b % group == 0) s += ' ';
    }
    return s;
}

int RegistersView::x_value() const
{
    return m_cw * (m_name_chars + 4);
}

int RegistersView::x_binary() const
{
    int chars = 0;
    for (const CpuRegister &r : m_regs) chars = std::max(chars, value_chars(r));
    return x_value() + m_cw * (chars + 3);
}

int RegistersView::first_row() const
{
    return verticalScrollBar()->value();
}

void RegistersView::changeEvent(QEvent *event)
{
    QAbstractScrollArea::changeEvent(event);
    if (event->type() != QEvent::FontChange) return;
    const QFontMetrics fm(font());
    m_rh = dbg::row_height(fm);
    m_cw = dbg::text_width(fm, "0");
    m_painted.clear();
    //The constructor's own font comes before the rest of the view
    if (!testAttribute(Qt::WA_WState_Polished)) return;
    updateGeometry();
    viewport()->update();
    refresh();
}

void RegistersView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    refresh();
}

void RegistersView::scrollContentsBy(int, int)
{
    viewport()->update();
}

void RegistersView::paintEvent(QPaintEvent *event)
{
    QPainter p(viewport());
    const dbg::Colors c = dbg::colors(palette());
    p.fillRect(viewport()->rect(), c.back);
    p.setFont(font());
    const QFontMetrics fm(font());
    const int base_y = (m_rh - fm.height()) / 2 + fm.ascent();
    const int dx = -horizontalScrollBar()->value();

    for (int i = first_row(), r = 0; i < (int)m_regs.size(); i++, r++) {
        const QRect row(0, r * m_rh, viewport()->width(), m_rh);
        if (!event->region().intersects(row)) continue;
        m_painted.painted(event, r, row, row_key(r));
        const CpuRegister &reg = m_regs[i];
        if (reg.name.empty()) continue;
        const int y = r * m_rh + base_y;
        p.setPen(c.reg_name);
        p.drawText(dx + m_cw, y, QString::fromStdString(reg.name));
        auto it = m_prev.find(reg.name);
        const bool changed = it != m_prev.end() && it->second != reg.value;
        p.setPen(changed ? c.changed : c.dim);
        if (i == m_cycle_row) {
            p.drawText(dx + x_value(), y, m_cycle_text);
            continue;
        }
        p.drawText(dx + x_value(), y, value_text(reg));
        p.setPen(changed ? c.changed : c.dim);
        p.drawText(dx + x_binary(), y, binary_text(reg));
    }
}

void RegistersView::contextMenuEvent(QContextMenuEvent *event)
{
    const int i = first_row() + event->pos().y() / m_rh;
    if (i < 0 || i >= (int)m_regs.size() || m_regs[i].name.empty() || i == m_cycle_row) return;
    const CpuRegister &reg = m_regs[i];
    exec_address_menu(this, event->globalPos(),
                      AddressTarget{cpu, reg.value & 0xFFFF, QString::fromStdString(reg.name)});
}

void RegistersView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (AddressRouter::read_only_at(this)) return;
    const int i = first_row() + event->pos().y() / m_rh;
    if (i < 0 || i >= (int)m_regs.size() || m_regs[i].name.empty() || i == m_cycle_row) return;
    const CpuRegister &reg = m_regs[i];
    const int dx = -horizontalScrollBar()->value();
    const int x = event->pos().x() - dx;
    const int y = (i - first_row()) * m_rh;
    m_edit_row = i;
    if (x >= x_binary()) {
        m_edit_base = 2;
        m_editor->set_digits(2, binary_chars(reg));
        m_editor->open(QRect(dx + x_binary() - 2, y, m_cw * (binary_chars(reg) + 1) + 4, m_rh), binary_text(reg));
    } else if (x >= x_value()) {
        m_edit_base = m_base;
        m_editor->set_digits(m_base, value_chars(reg));
        m_editor->open(QRect(dx + x_value() - 2, y, m_cw * (value_chars(reg) + 1) + 4, m_rh), value_text(reg));
    } else {
        m_edit_row = -1;
    }
}

void RegistersView::close_edit()
{
    if (m_edit_row < 0) return;
    m_edit_row = -1;
    m_editor->hide();
    viewport()->setFocus();
}

void RegistersView::commit_edit()
{
    if (m_edit_row < 0) return;
    const std::string name = m_regs[m_edit_row].name;
    QString text = m_editor->text();
    text.remove(' ');
    close_edit();
    bool ok = false;
    const unsigned int v = text.toUInt(&ok, m_edit_base);
    if (!ok) return;
    set_register(e, cpu, name, v);
    refresh();
    emit changed();
}

//------------------------------- FlagsView --------------------------------//

void FlagsView::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::FontChange) return;
    const QFontMetrics fm(font());
    m_rh = dbg::row_height(fm);
    m_cw = dbg::text_width(fm, "0");
    m_painted.clear();
    updateGeometry();
    update();
}

FlagsView::FlagsView(Emulator * e, CPU * cpu, QWidget *parent):
    QWidget(parent),
    e(e),
    cpu(cpu)
{
    setFont(dbg::font());
    QFontMetrics fm(font());
    m_rh = dbg::row_height(fm);
    m_cw = dbg::text_width(fm, "0");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    //Paints every pixel of itself: nothing beneath it needs painting first
    setAttribute(Qt::WA_OpaquePaintEvent);
    refresh();
    mark_stop();
}

void FlagsView::refresh()
{
    const std::vector<std::pair<std::string, std::string> > flags = cpu->get_flags();
    //A new size only with new flags: a layout request makes the tab widgets
    //above repaint whole
    bool same_names = flags.size() == m_flags.size();
    for (size_t i = 0; same_names && i < flags.size(); i++) same_names = flags[i].first == m_flags[i].first;
    m_flags = flags;
    if (!same_names) updateGeometry();
    if (isVisible() && !m_painted.same(0, key())) update();
}

QString FlagsView::key() const
{
    QString k = QString::number(x_first());
    for (const auto &f : m_flags) {
        auto it = m_prev.find(f.first);
        const bool changed = it != m_prev.end() && it->second != f.second;
        k += ' ' + QString::fromStdString(f.first) + '=' + QString::fromStdString(f.second) + (changed ? "c" : "");
    }
    return k;
}

void FlagsView::mark_stop()
{
    m_prev.clear();
    for (const auto &f : m_flags) m_prev[f.first] = f.second;
}

int FlagsView::cell_width(const std::string &name) const
{
    return m_cw * ((int)name.size() + 1);
}

//The letters in the middle of the field: a cell is the name and a gap after
//it, and the last gap is not part of the string
int FlagsView::x_first() const
{
    int w = -m_cw;
    for (const auto &f : m_flags) w += cell_width(f.first);
    return std::max(m_cw, (width() - w) / 2);
}

QSize FlagsView::sizeHint() const
{
    int w = m_cw;
    for (const auto &f : m_flags) w += cell_width(f.first);
    return QSize(w + m_cw, m_rh * 2);
}

void FlagsView::paintEvent(QPaintEvent *event)
{
    m_painted.painted(event, 0, rect(), key());
    QPainter p(this);
    const dbg::Colors c = dbg::colors(palette());
    p.fillRect(rect(), c.back);
    p.setPen(c.dim);
    p.drawLine(0, 0, width(), 0);
    p.setFont(font());
    const QFontMetrics fm(font());
    const int y = (height() - fm.height()) / 2 + fm.ascent();
    int x = x_first();
    for (const auto &f : m_flags) {
        const bool set = f.second != "0";
        auto it = m_prev.find(f.first);
        const bool changed = it != m_prev.end() && it->second != f.second;
        p.setPen(changed ? c.changed : (set ? c.text : c.dim));
        p.drawText(x, y, set ? QString::fromStdString(f.first).toUpper()
                             : QString(QChar('-')).repeated((int)f.first.size()));
        x += cell_width(f.first);
    }
}

void FlagsView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (AddressRouter::read_only_at(this)) return;
    int x = x_first();
    for (const auto &f : m_flags) {
        const int w = cell_width(f.first);
        if (event->pos().x() >= x && event->pos().x() < x + w) {
            const bool value = f.second == "0";
            const std::string name = f.first;
            CPU * c = cpu;
            bool done = false;
            e->invoke([c, name, value, &done]() { done = c->set_flag(name, value); });
            if (done) e->record_command(cpu->name, "setreg", name + ", " + (value ? "1" : "0"));
            refresh();
            emit changed();
            return;
        }
        x += w;
    }
}
