// POC 041 (not in ecat): the paged memories of a machine (its RAM and ROM, a sound card's RAM and flash) as one
// tab: the pages grouped by memory on the left, where each one is mapped now, and the dump of the page chosen

#pragma once

#include <vector>

#include <QWidget>

#include "emulator/emulator.h"

class MemoryPanel;
class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

class PagedMemory : public QWidget
{
    Q_OBJECT
public:
    PagedMemory(Emulator * e, const std::vector<Memory*> &memories, QWidget *parent);

    void refresh();
    void mark_stop();
    //The page holding the offset of the memory, its dump at the place
    void show_page(Memory * memory, unsigned int offset);

signals:
    void memory_changed();

protected:
    //Shown again, on a paused machine too: where the pages are now
    void showEvent(QShowEvent *event) override;

private:
    struct Page {
        Memory * memory;
        unsigned int page;
        QTreeWidgetItem * item;
    };

    Emulator * e;
    std::vector<Memory*> m_memories;
    std::vector<Page> m_pages;
    QTreeWidget * m_tree;
    QSplitter * m_split;
    bool m_sized = false;
    //The tree as wide as its columns (what the pages hold in full), up to three fifths of the tab: once, when first shown
    void size_tree();
    QWidget * m_right;
    QVBoxLayout * m_right_layout;
    MemoryPanel * m_panel;
    const Page * m_shown;
    bool m_picked;          //A page chosen by hand: the selection stays on it
    QString m_first_cpu;

    //Where each page is mapped: "cpu #C000", the processors' windows asked
    //once; with titles also what each page holds
    void update_mapping(bool titles);
    //The page at #C000 of the first processor
    void select_paged();
    void open(const Page &p);
};
