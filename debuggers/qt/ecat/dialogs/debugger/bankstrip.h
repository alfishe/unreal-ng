// Not in ecat: the windows of a paged address space. A Z80 of a ZX Spectrum or of a General Sound card
// sees 64 KB as four 16 KB windows, each showing one page of a ROM or a RAM: the strip names them above a listing
// or a dump, and the views mark where one ends and the next begins

#pragma once

#include <functional>
#include <vector>

#include <QString>
#include <QWidget>

#include "emulator/emulator.h"

//"ROM 3", "RAM 5"
QString window_page(const AddressWindow &w);
//What is there: the ROM's title by its signature or the page's role, "screen 0"
QString window_note(const AddressWindow &w);
//"#4000 RAM 5 (fixed, screen 0)": the line the views draw at the window's start
QString window_label(const AddressWindow &w, int base);
//The window holding an address, -1 when none
int window_at(const std::vector<AddressWindow> &windows, unsigned int address);
//The window starting at the address, -1 when none starts there
int window_starting(const std::vector<AddressWindow> &windows, unsigned int address);

//A segment per window: the address, the page and whether the ports can map
//another one there, what the page holds; the page in the colour of a changed
//value when it is not the one of the last stop. Windows of one size are
//segments of one width to the pixel; the text is at the views' size, smaller
//when it would not fit. A click asks to show the window's start; with a PC,
//a chip at the left shows it, marks its window and asks to show it
class BankStrip : public QWidget
{
    Q_OBJECT
public:
    BankStrip(std::function<std::vector<AddressWindow>()> source, int base, QWidget *parent);
    //The processor's PC for the chip; none without it
    void set_pc(std::function<unsigned int()> pc);
    //The processor the windows are of: the menu of a window's start acts on it
    void set_cpu(CPU * cpu) { m_cpu = cpu; }

    const std::vector<AddressWindow> & windows() const { return m_windows; }
    //Asks the windows again; repaints only when something in them changed
    void refresh();
    //The pages now are what changes are shown against
    void mark_stop();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void activated(unsigned int address);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;

private:
    std::function<std::vector<AddressWindow>()> m_source;
    int m_base;
    std::vector<AddressWindow> m_windows;
    QString m_key;
    std::vector<QString> m_stop;
    int m_hover = -1;        // a window, kChip or -1
    CPU * m_cpu = nullptr;
    std::function<unsigned int()> m_pc_source;
    int m_pc = -1;
    //The font the text fits in, the chip and the segments laid out in it
    QFont m_fit;
    QRect m_chip;
    std::vector<QRect> m_rects;

    static constexpr int kChip = -2;
    void layout();
    void place(const QFont &f);
    QString pc_text() const;
    int segment_at(const QPoint &pos) const;
    QString key() const;
};
