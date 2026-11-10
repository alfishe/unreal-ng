// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Listing of a processor's code in the debugger, header

#pragma once

#include <memory>
#include <vector>

#include <QAbstractScrollArea>

#include "emulator/emulator.h"
#include "emulator/disasm_buffer.h"
#include "debugutil.h"

class CellEditor;

//The code of one processor: jump arrows, the PC and breakpoint marks, the
//address, the bytes and the instruction. The listing is a DisasmBuffer that
//lives as long as the view, so what scrolled away is there when the program
//comes back; the keys and the wheel move over it, adding lines below and,
//through the backward disassembly, above
class QHeaderView;

class DisasmView : public QAbstractScrollArea
{
    Q_OBJECT
public:
    DisasmView(Emulator * e, CPU * cpu, QWidget *parent);
    ~DisasmView() override;

    //After a stop: PC gets a line of its own, is brought into view if it is
    //not there, and the cursor goes to it
    void show_pc();
    //The cursor to the address, the listing built there if needed
    void go_to(unsigned int address);
    unsigned int cursor_address() const;
    //The address of the top row
    unsigned int top_address() const;
    //The cursor to the address, the address at the top
    void show_at_top(unsigned int address);
    //The cursor to the address; the listing scrolls only when the row is not in view
    void place_cursor(unsigned int address);
    //The cursor rows down (below 0 up); a page moves the rows shown with it
    void move_cursor(int delta, bool page);
    //By bytes: a step of the cursor decodes the listing again from the next or the previous byte (code that is
    //data, a jump into an instruction); off: by whole instructions
    void set_byte_scroll(bool on) { m_byte_scroll = on; }
    bool byte_scroll() const { return m_byte_scroll; }
    //Memory may have changed: the bytes are read again
    void refresh_memory();
    //The same while the machine runs: only the rows that changed are
    //repainted, and not under the line being edited
    void follow_memory();
    //The addresses the backward disassembly trusts: where PC has been
    void update_anchors();
    void toggle_breakpoint(unsigned int address);
    //The instruction under the cursor
    bool cursor_line(DisasmLine &line) const;
    //The windows of a paged space: a line marks where each one starts
    void set_windows(const std::vector<AddressWindow> &windows) { m_windows = windows; }

    unsigned int space() const { return m_space; }
    int base() const { return m_base; }
    //An address of the code as the window writes it: CS:IP on the 8086
    QString format_address(unsigned int address) const;
    //The address and the instruction there, decoded by the view's own
    //decoder: CPU::disasm() belongs to the emulation thread
    QString describe(unsigned int address) const;

signals:
    void breakpoints_changed();
    void cursor_moved(unsigned int address);
    //The listing scrolled: another address at the top
    void top_moved(unsigned int address);
    void memory_changed();

protected:
    bool viewportEvent(QEvent *event) override;
    //The row's address, and the target of its jump or call when it has one
    void contextMenuEvent(QContextMenuEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    //Another font (the window's scale): rows and columns measured again
    void changeEvent(QEvent *event) override;

private:
    Emulator * e;
    CPU * cpu;
    DisAsm * m_disasm;
    std::unique_ptr<DisasmBuffer> m_buf;
    unsigned int m_space;
    int m_base;
    int m_addr_digits;
    bool m_word_code;       // PDP-11: code shown as octal words
    int m_top;              // buffer line at the top of the view
    int m_cursor;           // buffer line of the cursor
    int m_rh;               // row height
    int m_cw;               // character width
    CellEditor * m_editor;
    int m_edit_line;
    unsigned int m_shown_cursor;    //What cursor_moved() said last
    unsigned int m_shown_top = ~0u;  //What top_moved() said last
    bool m_byte_scroll = false;
    PaintedRows m_painted;
    //The labels' column: its width in characters (0: none), whether there are labels, the labels it was laid out for
    int m_label_chars = 0;
    int m_label_px = 0;     // its width measured in the font (glyphs advance by fractions of a pixel)
    bool m_has_labels = false;
    unsigned int m_labels_serial = 0;
    //The column is of one width (none without labels), 12 characters until the header's edge is dragged: a longer
    //label fades out at its edge, the tooltip has it whole
    static constexpr int kLabelChars = 12;
    int m_label_set = kLabelChars;
    //The columns' titles above the rows, staying there as they scroll; only the labels' edge moves
    QHeaderView * m_header = nullptr;
    bool m_syncing = false;
    void sync_header();
    void place_header();
    bool label_cut(const std::string &label) const;
    PaintedRows m_painted_arrows;   //The arrows' strip as one row
    std::vector<AddressWindow> m_windows;

    int rows() const;
    int bytes_chars() const;
    int x_marks() const;
    int x_address() const;
    int x_bytes() const;
    int x_label() const;
    int x_text() const;
    //The line before a column: the same place in the header and in the rows
    int x_divider(int column_x) const;
    //The labels changed: the lines decoded again, the column measured again
    bool follow_labels();
    QString bytes_text(const DisasmBuffer::Line &l) const;
    int row_at(int y) const;
    //The label of the window that starts at the buffer line (or inside the
    //instruction above it), empty when none does
    QString window_start(int line) const;
    //What a row shows and what the arrows' strip shows, for PaintedRows
    QString row_key(int row, unsigned int pc) const;
    QString arrows_key(const std::vector<DisasmBuffer::Arrow> &arrows, int pc_row) const;
    //The buffer line of PC among the rows shown, -1 when it is not there
    int pc_row(unsigned int pc) const;
    //Repaints the rows and the arrows that changed since they were painted
    void repaint_changed();

    //Runs a change of the buffer keeping the top and the cursor on their
    //addresses: lines added above move them, lines dropped may lose them
    template <typename F> void keep_place(F op);
    void fill();
    void scroll_rows(int delta);
    void top_to(unsigned int address);
    void make_visible();
    void update_scrollbar();
    void on_scroll_action(int action);
    void start_edit(int line);
    void commit_edit();
    void close_edit();
};
