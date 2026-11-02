// Not in ecat: the windows of a paged address space

#include "bankstrip.h"

#include <algorithm>

#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include "addressmenu.h"
#include "debugstyle.h"
#include "dialogs/numfmt.h"

QString window_page(const AddressWindow &w)
{
    if (w.kind.empty()) return QStringLiteral("none");
    return QString::fromStdString(w.kind) + ' ' + QString::number(w.page);
}

QString window_note(const AddressWindow &w)
{
    if (!w.name.empty()) return QString::fromStdString(w.name);
    return QString::fromStdString(w.role);
}

QString window_label(const AddressWindow &w, int base)
{
    QString note = w.switchable ? QStringLiteral("paged") : QStringLiteral("fixed");
    const QString what = window_note(w);
    if (!what.isEmpty()) note += ", " + what;
    return '#' + fmt_num(w.start, base, 4) + ' ' + window_page(w) + " (" + note + ')';
}

int window_at(const std::vector<AddressWindow> &windows, unsigned int address)
{
    for (size_t i = 0; i < windows.size(); i++)
        if (address >= windows[i].start && address - windows[i].start < windows[i].size) return (int)i;
    return -1;
}

int window_starting(const std::vector<AddressWindow> &windows, unsigned int address)
{
    for (size_t i = 0; i < windows.size(); i++)
        if (windows[i].start == address) return (int)i;
    return -1;
}

BankStrip::BankStrip(std::function<std::vector<AddressWindow>()> source, int base, QWidget *parent):
    QWidget(parent),
    m_source(std::move(source)),
    m_base(base)
{
    setFont(dbg::font());
    m_fit = font();
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    refresh();
    mark_stop();
}

void BankStrip::set_pc(std::function<unsigned int()> pc)
{
    m_pc_source = std::move(pc);
    m_key.clear();
    refresh();
}

QString BankStrip::key() const
{
    QString k = QString::number(m_pc) + ';';
    for (const AddressWindow &w : m_windows)
        k += window_label(w, m_base) + (w.writable ? 'w' : 'r') + ';';
    return k;
}

void BankStrip::refresh()
{
    m_windows = m_source();
    m_pc = m_pc_source ? (int)(m_pc_source() & 0xFFFF) : -1;
    setVisible(!m_windows.empty());
    const QString k = key();
    if (k == m_key) return;
    const bool relayout = m_key.section(';', 1) != k.section(';', 1) || (m_key.isEmpty() != k.isEmpty());
    m_key = k;
    if (relayout) layout();
    update();
}

void BankStrip::mark_stop()
{
    refresh();
    m_stop.clear();
    for (const AddressWindow &w : m_windows) m_stop.push_back(window_page(w));
    update();
}

QString BankStrip::pc_text() const
{
    return "PC #" + fmt_num(m_pc < 0 ? 0 : (unsigned int)m_pc, m_base, 4);
}

//The pieces of a segment, in the font given: the gap between segments, the coloured edge, the padding
struct Metrics {
    int cw, gap, accent, pad;
    explicit Metrics(const QFontMetrics &fm):
        cw(dbg::text_width(fm, "0")), gap(std::max(2, cw / 4)), accent(std::max(3, cw / 3)), pad(cw / 2)
    {}
};

static QString window_mark(const AddressWindow &w)
{
    QString mark = w.switchable ? QStringLiteral("⇄") : QString();
    if (!w.writable && w.kind == "RAM") mark = (mark.isEmpty() ? QString() : mark + ' ') + "ro";
    return mark;
}

static QFont bold_of(QFont f)
{
    f.setBold(true);
    return f;
}

//The chip and the segments in a font: a window's width is its size in units of the smallest one, every unit the
//same number of pixels, so windows of one size are of one width; the pixels left over go to the chip (or to the
//margins without one)
void BankStrip::place(const QFont &f)
{
    const QFontMetrics fm(f);
    const Metrics m(fm);
    m_rects.assign(m_windows.size(), QRect());
    if (m_windows.empty()) return;
    unsigned int unit = m_windows[0].size;
    for (const AddressWindow &w : m_windows) unit = std::min(unit, std::max(1u, w.size));
    unsigned int units = 0;
    for (const AddressWindow &w : m_windows) units += std::max(1u, w.size / std::max(1u, unit));
    const int chip = m_pc_source ? dbg::text_width(QFontMetrics(bold_of(f)), pc_text()) + 2 * m.cw
                                 : 0;
    const int gaps = m.gap * ((int)m_windows.size() - 1) + (chip ? m.gap : 0);
    const int unit_px = std::max(1, (width() - chip - gaps) / (int)units);
    const int spare = std::max(0, width() - chip - gaps - unit_px * (int)units);
    int x = 0;
    if (chip) {
        m_chip = QRect(0, 1, chip + spare, height() - 2);
        x = m_chip.right() + 1 + m.gap;
    } else {
        m_chip = QRect();
        x = spare / 2;
    }
    for (size_t i = 0; i < m_windows.size(); i++) {
        const int w = unit_px * (int)std::max(1u, m_windows[i].size / std::max(1u, unit));
        m_rects[i] = QRect(x, 1, w, height() - 2);
        x += w + m.gap;
    }
}

//The views' font when the address and the page fit in it, else the largest smaller one they fit in (and cut only
//below 6 pt, past the 64 KB strips of any machine). Nothing is cut with an ellipsis. What a page holds does not take the size from the others: each segment fits its own
void BankStrip::layout()
{
    QFont f = font();
    for (int pass = 0; pass < 8; pass++) {
        place(f);
        const QFontMetrics fm(f);
        const QFontMetrics fb(bold_of(f));
        const Metrics m(fm);
        double ratio = 1.0;
        for (size_t i = 0; i < m_windows.size(); i++) {
            const AddressWindow &w = m_windows[i];
            const QString mark = window_mark(w);
            const int line1 = dbg::text_width(fm, '#' + fmt_num(w.start, m_base, 4) + ' ') +
                              dbg::text_width(fb, window_page(w)) + (mark.isEmpty() ? 0 : m.pad + dbg::text_width(fm, mark));
            const int need = m.accent + 2 * m.pad + line1;
            if (need > m_rects[i].width()) ratio = std::min(ratio, (double)m_rects[i].width() / need);
        }
        if (ratio >= 1.0 || f.pointSizeF() <= 6.0) break;
        f.setPointSizeF(std::max(6.0, f.pointSizeF() * ratio * 0.97));
    }
    const bool taller = QFontMetrics(f).height() != QFontMetrics(m_fit).height();
    m_fit = f;
    if (taller) updateGeometry();
}

void BankStrip::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layout();
}

QSize BankStrip::sizeHint() const
{
    const QFontMetrics fm(m_fit);
    return QSize(dbg::text_width(QFontMetrics(font()), "0") * 64, 2 * fm.height() + 8);
}

QSize BankStrip::minimumSizeHint() const
{
    const QFontMetrics fm(m_fit);
    return QSize(dbg::text_width(fm, "0") * 16, 2 * fm.height() + 8);
}

int BankStrip::segment_at(const QPoint &pos) const
{
    if (m_chip.contains(pos)) return kChip;
    for (int i = 0; i < (int)m_rects.size(); i++)
        if (m_rects[i].contains(pos)) return i;
    return -1;
}

//A hue per kind of memory: ROM warm, RAM cool, flash and cache violet, an empty window grey
static QColor kind_hue(const AddressWindow &w, bool dark)
{
    if (w.kind == "ROM") return dark ? QColor(230, 170, 80) : QColor(205, 125, 20);
    if (w.kind == "RAM") return dark ? QColor(100, 160, 240) : QColor(40, 110, 210);
    if (w.kind.empty()) return dark ? QColor(120, 120, 120) : QColor(150, 150, 150);
    return dark ? QColor(180, 140, 240) : QColor(130, 80, 200);
}

static QColor mix(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

void BankStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const dbg::Colors c = dbg::colors(palette());
    const bool dark = c.back.lightness() < 128;
    p.fillRect(rect(), palette().color(QPalette::Window));
    const QFont bold = bold_of(m_fit);
    const QFontMetrics fm(m_fit);
    const QFontMetrics fb(bold);
    const Metrics m(fm);
    const qreal radius = std::max(3, m.cw / 2);
    const QColor pc_hue = dark ? QColor(230, 200, 60) : QColor(215, 165, 0);
    const int text_top = (height() - 2 * fm.height()) / 2;
    const int y1 = text_top + fm.ascent();
    const int y2 = y1 + fm.height();

    //The PC chip: the instruction-pointer yellow of the listing, a click shows the PC
    if (!m_chip.isNull()) {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(mix(c.back, pc_hue, (dark ? 0.25 : 0.22) + (m_hover == kChip ? 0.12 : 0.0)));
        p.drawRoundedRect(m_chip.adjusted(0, 0, -1, -1), radius, radius);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setClipRect(m_chip);
        p.setFont(bold);
        p.setPen(c.text);
        const QString t = pc_text();
        p.drawText(m_chip.left() + (m_chip.width() - dbg::text_width(fb, t)) / 2, y1, t);
        p.setFont(m_fit);
        p.setPen(c.dim);
        const QString go = QStringLiteral("go to");
        p.drawText(m_chip.left() + (m_chip.width() - dbg::text_width(fm, go)) / 2, y2, go);
    }

    for (int i = 0; i < (int)m_windows.size() && i < (int)m_rects.size(); i++) {
        const AddressWindow &w = m_windows[i];
        const QRect r = m_rects[i].adjusted(0, 0, -1, -1);
        const QString page = window_page(w);
        const bool changed = i < (int)m_stop.size() && m_stop[i] != page;
        const QColor hue = changed ? c.changed : kind_hue(w, dark);
        const bool has_pc = m_pc >= 0 && (unsigned int)m_pc >= w.start && (unsigned int)m_pc - w.start < w.size;

        //The segment: a light tone of its kind, a little stronger under the mouse, the kind's colour at the left edge
        p.setClipping(false);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(mix(c.back, hue, (dark ? 0.16 : 0.10) + (i == m_hover ? 0.08 : 0.0)));
        p.drawRoundedRect(r, radius, radius);
        p.setClipRect(QRect(r.left(), r.top(), m.accent, r.height()));
        p.setBrush(hue);
        p.drawRoundedRect(r, radius, radius);
        //The window the PC is in: a line of the chip's colour under it
        if (has_pc) {
            p.setClipRect(QRect(r.left(), r.bottom() - 2, r.width(), 3));
            p.setBrush(pc_hue);
            p.drawRoundedRect(r, radius, radius);
        }
        p.setRenderHint(QPainter::Antialiasing, false);

        const QRect inner = r.adjusted(m.accent + m.pad, 0, -m.pad, 0);
        p.setClipRect(inner);
        const int x = inner.left();
        const int right = inner.right();

        //The address, the page in its kind's colour, the mark of a window the ports page at the right
        const QString mark = window_mark(w);
        const int mark_w = mark.isEmpty() ? 0 : dbg::text_width(fm, mark) + m.pad;
        const QString head = '#' + fmt_num(w.start, m_base, 4) + ' ';
        p.setFont(m_fit);
        p.setPen(c.dim);
        p.drawText(x, y1, head);
        const int hx = x + dbg::text_width(fm, head);
        p.setFont(bold);
        p.setPen(changed ? c.changed : mix(hue, c.text, dark ? 0.15 : 0.35));
        p.drawText(hx, y1, page);
        p.setFont(m_fit);
        if (!mark.isEmpty() && hx + dbg::text_width(fb, page) + mark_w <= right + 1) {
            p.setPen(c.dim);
            p.drawText(right - dbg::text_width(fm, mark) + 1, y1, mark);
        }

        //What the page holds: at the size of the line above when it fits, else smaller down to three quarters of it;
        //past that not at all (never cut: the tooltip has it)
        const QString note = window_note(w);
        QFont nf = m_fit;
        const int nw = dbg::text_width(fm, note);
        if (nw > inner.width() && nw > 0)
            nf.setPointSizeF(std::max(m_fit.pointSizeF() * 0.75, m_fit.pointSizeF() * inner.width() / nw * 0.98));
        const QFontMetrics nm(nf);
        p.setFont(nf);
        p.setPen(c.dim);
        if (dbg::text_width(nm, note) <= inner.width()) p.drawText(x, y2, note);
        p.setFont(m_fit);
    }
}

void BankStrip::contextMenuEvent(QContextMenuEvent *event)
{
    const int i = segment_at(event->pos());
    if (i == kChip && m_pc >= 0) exec_address_menu(this, event->globalPos(), AddressTarget{m_cpu, (unsigned int)m_pc, "PC"});
    else if (i >= 0)
        exec_address_menu(this, event->globalPos(), AddressTarget{m_cpu, m_windows[i].start, window_page(m_windows[i])});
}

void BankStrip::mouseMoveEvent(QMouseEvent *event)
{
    const int i = segment_at(event->pos());
    if (i == m_hover) return;
    m_hover = i;
    update();
}

void BankStrip::leaveEvent(QEvent *)
{
    if (m_hover < 0) return;
    m_hover = -1;
    update();
}

void BankStrip::mousePressEvent(QMouseEvent *event)
{
    const int i = segment_at(event->pos());
    if (i == kChip && m_pc >= 0) emit activated((unsigned int)m_pc);
    else if (i >= 0) emit activated(m_windows[i].start);
}

bool BankStrip::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        QHelpEvent * h = static_cast<QHelpEvent*>(event);
        const int i = segment_at(h->pos());
        if (i == kChip) {
            QToolTip::showText(h->globalPos(), tr("Show the PC (%1)").arg(pc_text()), this);
            return true;
        }
        if (i < 0) {
            QToolTip::hideText();
            return true;
        }
        const AddressWindow &w = m_windows[i];
        QString tip = window_label(w, m_base) + "\n#" + fmt_num(w.start, m_base, 4) + "-#" +
                      fmt_num(w.start + w.size - 1, m_base, 4) + (w.writable ? ", read/write" : ", read-only");
        if (!w.role.empty() && w.role != w.name) tip += "\nlayout: " + QString::fromStdString(w.role);
        if (w.device != nullptr)
            tip += "\n" + QString::fromStdString(w.device->name) + " + " + fmt_num(w.offset, m_base, addr_digits(w.offset, m_base));
        tip += w.switchable ? "\nThe ports can map another page here" : "\nThe model keeps this page here";
        QToolTip::showText(h->globalPos(), tip, this);
        return true;
    }
    if (event->type() == QEvent::FontChange) {
        layout();
        updateGeometry();
    }
    return QWidget::event(event);
}
