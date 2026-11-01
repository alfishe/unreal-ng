// Not in ecat: the paged memories of a machine as one tab

#include "pagedmemory.h"

#include <algorithm>
#include <map>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollBar>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "bankstrip.h"
#include "debugstyle.h"
#include "memoryview.h"
#include "dialogs/numfmt.h"

PagedMemory::PagedMemory(Emulator * e, const std::vector<Memory*> &memories, QWidget *parent):
    QWidget(parent),
    e(e),
    m_memories(memories),
    m_panel(nullptr),
    m_shown(nullptr),
    m_picked(false)
{
    QHBoxLayout * h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    QSplitter * split = m_split = new QSplitter(Qt::Horizontal, this);
    h->addWidget(split);

    m_tree = new QTreeWidget(split);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels(QStringList() << tr("Page") << tr("Mapped") << tr("Holds"));
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setFont(dbg::font());
    //What a page holds is never cut: the column is as wide as its longest text, the tree scrolls
    m_tree->setTextElideMode(Qt::ElideNone);
    m_tree->header()->setStretchLastSection(false);
    for (int c = 0; c < 3; c++) m_tree->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);

    //A group per device (the machine's own memories, then each card's: "neogs.ram" is the NeoGS's), in it a group
    //per memory, a line per page
    const auto device_of = [](const Memory * m) {
        const size_t dot = m->name.rfind('.');
        return dot == std::string::npos ? std::string() : m->name.substr(0, dot);
    };
    const auto kind_of = [](const Memory * m) {
        const size_t dot = m->name.rfind('.');
        return dot == std::string::npos ? m->name : m->name.substr(dot + 1);
    };
    std::vector<std::string> devices;
    for (const Memory * m : m_memories)
        if (std::find(devices.begin(), devices.end(), device_of(m)) == devices.end()) devices.push_back(device_of(m));
    std::stable_partition(devices.begin(), devices.end(), [](const std::string &d) { return d.empty(); });
    for (const std::string &device : devices) {
        QTreeWidgetItem * top = new QTreeWidgetItem(m_tree);
        top->setText(0, device.empty() ? tr("system") : QString::fromStdString(device));
        top->setFlags(Qt::ItemIsEnabled);
        QStringList sizes;
        for (Memory * m : m_memories) {
            if (device_of(m) != device) continue;
            const unsigned int ps = m->page_size();
            const unsigned int n = (m->get_size() + ps - 1) / ps;
            const QString kind = QString::fromStdString(kind_of(m));
            sizes << kind + ' ' + QString::number(m->get_size() / 1024) + " KB";
            QTreeWidgetItem * group = new QTreeWidgetItem(top);
            group->setText(0, kind);
            group->setText(2, tr("%1 pages of %2 KB").arg(n).arg(ps / 1024));
            group->setFlags(Qt::ItemIsEnabled);
            for (unsigned int i = 0; i < n; i++) {
                QTreeWidgetItem * item = new QTreeWidgetItem(group);
                item->setText(0, kind + ' ' + QString::number(i));
                item->setData(0, Qt::UserRole, (int)m_pages.size());
                m_pages.push_back(Page{m, i, item});
            }
            //The machine's own pages open, a card's memories folded to their lines
            group->setExpanded(device.empty());
        }
        top->setText(2, sizes.join(", "));
        top->setExpanded(true);
    }

    m_right = new QWidget(split);
    m_right_layout = new QVBoxLayout(m_right);
    m_right_layout->setContentsMargins(0, 0, 0, 0);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);

    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem * item, QTreeWidgetItem *) {
        if (item == nullptr || !item->data(0, Qt::UserRole).isValid()) return;
        open(m_pages[(size_t)item->data(0, Qt::UserRole).toInt()]);
    });

    update_mapping(true);
    select_paged();
    //A page picked by hand stays; until then the one at #C000 follows the machine
    connect(m_tree, &QTreeWidget::itemPressed, this, [this]() { m_picked = true; });
    connect(m_tree, &QTreeWidget::itemActivated, this, [this]() { m_picked = true; });
}

void PagedMemory::select_paged()
{
    //The page the top window of the first processor shows: the one a program
    //pages most
    const Page * first = m_pages.empty() ? nullptr : &m_pages[0];
    for (const Page &p : m_pages)
        if (p.item->text(1).startsWith(m_first_cpu + " #C000") || p.item->text(1).contains(", " + m_first_cpu + " #C000")) {
            first = &p;
            break;
        }
    if (first != nullptr && m_tree->currentItem() != first->item) m_tree->setCurrentItem(first->item);
}

void PagedMemory::update_mapping(bool titles)
{
    std::map<std::pair<Memory*, unsigned int>, QString> where;
    const std::vector<CPU*> cpus = e->dm->get_cpus();
    if (!cpus.empty()) m_first_cpu = QString::fromStdString(cpus.front()->name);
    for (CPU * cpu : cpus)
        for (const AddressWindow &w : cpu->get_windows()) {
            Memory * m = dynamic_cast<Memory*>(w.device);
            if (m == nullptr || m->page_size() == 0) continue;
            QString &t = where[std::make_pair(m, w.offset / m->page_size())];
            if (!t.isEmpty()) t += ", ";
            t += QString::fromStdString(cpu->name) + " #" + fmt_num(w.start, 16, 4);
        }
    for (const Page &p : m_pages) {
        //What a page holds: asked at a stop, not at each tick (a ROM's name is
        //its signature over 16 KB)
        if (titles) {
            QString holds = QString::fromStdString(p.memory->page_title(p.page));
            const QString role = QString::fromStdString(p.memory->page_role(p.page));
            if (!role.isEmpty() && role != holds) holds += holds.isEmpty() ? role : " (" + role + ')';
            if (p.item->text(2) != holds) p.item->setText(2, holds);
        }
        const auto it = where.find(std::make_pair(p.memory, p.page));
        const QString text = (it == where.end()) ? QString() : it->second;
        if (p.item->text(1) == text) continue;
        p.item->setText(1, text);
        QFont f = p.item->font(0);
        f.setBold(!text.isEmpty());
        p.item->setFont(0, f);
        p.item->setFont(1, f);
    }
    //At a stop, not while it runs: a game pages #C000 every frame, the dump
    //would be opened anew at each tick
    if (titles && !m_picked) select_paged();
}

void PagedMemory::open(const Page &p)
{
    if (m_shown == &p) return;
    m_shown = &p;
    Memory * m = p.memory;
    const unsigned int base = p.page * m->page_size();
    MemorySource s;
    s.title = QString::fromStdString(m->name) + '-' + QString::number(p.page);
    s.size = m->page_size();
    s.base = 16;
    s.read = [m, base](unsigned int a) { return m->get_direct(base + a) & 0xFF; };
    s.write = [m, base](unsigned int a, unsigned int v) {
        if (m->debug_writable(base + a)) m->set_value(base + a, v & 0xFF, true);
    };
    s.writable = [m, base](unsigned int a) { return m->debug_writable(base + a); };
    s.owner = [m, base](unsigned int a) {
        return QString::fromStdString(m->name) + " + " + fmt_num(base + a, 16, addr_digits(m->get_size() - 1, 16));
    };

    unsigned int cursor = 0;
    if (m_panel != nullptr) {
        cursor = m_panel->view()->cursor();
        delete m_panel;
    }
    m_panel = new MemoryPanel(e, s, m_right);
    m_right_layout->addWidget(m_panel);
    connect(m_panel->view(), &MemoryView::memory_changed, this, &PagedMemory::memory_changed);
    //The same offset in the next page: comparing two pages line by line
    m_panel->view()->go_to(cursor);
}

void PagedMemory::show_page(Memory * memory, unsigned int offset)
{
    for (const Page &p : m_pages)
        if (p.memory == memory && p.page == offset / memory->page_size()) {
            m_picked = true;
            m_tree->setCurrentItem(p.item);
            m_panel->view()->go_to(offset % memory->page_size());
            return;
        }
}

void PagedMemory::refresh()
{
    if (!isVisible()) return;
    update_mapping(false);
    if (m_panel != nullptr) m_panel->refresh();
}

void PagedMemory::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    update_mapping(true);
    if (!m_sized) size_tree();
}

void PagedMemory::size_tree()
{
    if (width() <= 0) return;
    m_sized = true;
    int want = m_tree->frameWidth() * 2 + m_tree->verticalScrollBar()->sizeHint().width();
    for (int c = 0; c < m_tree->columnCount(); c++)
        want += std::max(m_tree->columnWidth(c), m_tree->header()->sectionSizeHint(c));
    const int tree = std::min(want, width() * 3 / 5);
    m_split->setSizes(QList<int>() << tree << width() - tree);
}

void PagedMemory::mark_stop()
{
    if (isVisible()) update_mapping(true);
    if (m_panel != nullptr) m_panel->mark_stop();
}
