// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Memory dump of the debugger, header

#pragma once

#include <functional>
#include <vector>

#include <QAbstractScrollArea>
#include <QTabWidget>
#include <QWidget>

#include "emulator/emulator.h"
#include "debugutil.h"

class BankStrip;
class PagedMemory;
class CellEditor;
class QLabel;
class QHBoxLayout;
class QLineEdit;
class QToolButton;

//What a dump shows: the address space of a processor or one device. Reads
//have no side effects (peek_mem, get_direct); writes are the debugger's own
//(poke: a ROM takes them, a register is not written) and run on the
//emulation thread. writable says which cells take them, so that a register
//is not offered for editing
struct MemorySource {
    QString title;
    unsigned int size = 0;
    int base = 16;
    //The address the first byte is shown at: a window into a larger space
    unsigned int origin = 0;
    std::function<unsigned int(unsigned int)> read;
    std::function<void(unsigned int, unsigned int)> write;
    std::function<bool(unsigned int)> writable;
    //The device that answers an address, for a mapper or a processor
    std::function<QString(unsigned int)> owner;
    //The windows of a paged space (a processor's): a strip above the dump
    std::function<std::vector<AddressWindow>()> windows;
    //The processor's PC, for the strip's chip
    std::function<unsigned int()> pc;
    //The processor whose space it is: the address menu acts on it
    CPU * cpu = nullptr;
};

MemorySource memory_of_cpu(CPU * cpu);
MemorySource memory_of_device(AddressableDevice * d);

//Sixteen bytes a line, as bytes or as words, in hex or octal, with the
//characters in the machine's own encoding. A double click edits a value or a
//character in place: Enter keeps it, Esc drops it, Tab keeps it and goes on
//to the next one; a character is kept as soon as it is typed
class MemoryView : public QAbstractScrollArea
{
    Q_OBJECT
public:
    MemoryView(Emulator * e, const MemorySource &source, QWidget *parent);

    const MemorySource & source() const { return m_src; }
    void set_base(int base);
    void set_word(bool word);
    int base() const { return m_base; }
    bool word() const { return m_word; }

    void go_to(unsigned int address);
    unsigned int cursor() const { return m_cursor; }
    void refresh();
    //The values now are what changes are shown against
    void mark_stop();
    //The next place after the cursor holding the bytes, wrapping around
    bool find(const std::vector<uint8_t> &pattern);
    //A line marks where each window starts
    void set_windows(const std::vector<AddressWindow> &windows) { m_windows = windows; }

signals:
    void cursor_moved(unsigned int address);
    void memory_changed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    //The cell's address: its menu (a processor's dump; a page's has only the copies)
    void contextMenuEvent(QContextMenuEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    //Another font (the window's scale): rows and columns measured again
    void changeEvent(QEvent *event) override;

private:
    Emulator * e;
    MemorySource m_src;
    int m_base;
    bool m_word;
    unsigned int m_top;         // address of the first line
    unsigned int m_cursor;
    bool m_on_chars;            // the cursor is in the character column
    int m_rh;
    int m_cw;
    std::vector<uint8_t> m_snap;
    unsigned int m_snap_from;
    CellEditor * m_editor;
    bool m_editing;
    bool m_edit_chars;

    int rows() const;
    unsigned int lines() const;
    int addr_chars() const;
    int cell_chars() const;
    int cell_bytes() const { return m_word ? 2 : 1; }
    int x_cells() const;
    int x_cell(int index) const;
    int x_chars() const;
    unsigned int value_at(unsigned int address) const;
    bool hit(const QPoint &pos, unsigned int &address, bool &chars) const;
    void set_cursor(unsigned int address, bool chars);
    void make_visible();
    void update_scrollbar();
    void start_edit(unsigned int address, bool chars);
    bool can_write(unsigned int address, unsigned int count) const;
    void commit_edit();
    void close_edit();
    void on_char_typed(const QString &text);
    void write(unsigned int address, const std::vector<uint8_t> &bytes);

    PaintedRows m_painted;
    std::vector<AddressWindow> m_windows;
    //What a line of the view shows, for PaintedRows
    QString row_key(int row) const;
    //The label of the window starting at the line, empty when none does
    QString window_start(unsigned int line) const;
};

//The dump with its controls: address, notation, bytes or words, search,
//load and save, and below the place under the cursor
class MemoryPanel : public QWidget
{
    Q_OBJECT
public:
    MemoryPanel(Emulator * e, const MemorySource &source, QWidget *parent);
    MemoryView * view() const { return m_view; }
    void refresh();
    void mark_stop();

private:
    Emulator * e;
    MemoryView * m_view;
    BankStrip * m_banks;
    QLineEdit * m_address;
    QToolButton * m_base_button;
    QToolButton * m_word_button;
    QLineEdit * m_find;
    QHBoxLayout * m_row;
    QLabel * m_status;

    void go();
    void find_next();
    void load_file();
    void save_file();
    void apply_view();
    void show_cursor(unsigned int address);

protected:
    //The window's scale: the address field as wide as its digits again
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
};

//The dumps of a machine as tabs: each processor's address space, the paged
//memories as one tab, the mappers and the other memories; any other device
//with an address space from the button at the corner
class MemoryTabs : public QTabWidget
{
    Q_OBJECT
public:
    MemoryTabs(Emulator * e, QWidget *parent);
    void refresh();
    void mark_stop();
    //The processor's dump at an address
    void show_address(CPU * cpu, unsigned int address);
    //The page that holds a processor's address, in the pages tab, at that byte
    void show_page(CPU * cpu, unsigned int address);

signals:
    //A value was changed in one of the dumps
    void memory_changed();

private:
    Emulator * e;
    std::vector<MemoryPanel*> m_panels;
    PagedMemory * m_paged;

    void add_panel(const MemorySource &s, const QString &title);
};
