// unreal-debugger (not in ecat): the ZX Spectrum Next's tabs of the debugger window (NextREG, MMU, Copper, Sprites,
// DMA, CTC / IM2) and a General Sound card's (Log, Protocol, Firmware, Hardware, Stats; on a NeoGS also DMA and
// SD · MP3 · Flash; on the lightweight player Interpreter and Player), each a device catalog report read whole: its
// fields in a tree, its lists of rows in tables; a shaped view shows the report decoded into groups (the NeoGS board,
// gui-card-debugger B3; the LW inspector, A10). A card's view reads the report of the card the window follows: the
// General Sound's (`gs`) or the ZX-MultiSound's GS (`ms`)

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <QString>
#include <QWidget>

#include "emulator/emulator.h"

class QLabel;
class QSplitter;
class QTableWidget;
class QTreeWidget;

class NextReportView : public QWidget
{
    Q_OBJECT
public:
    //@p device: the catalog report ("next-regs"); @p keys: its parts shown (empty: all); @p table: the part whose
    //items are the table's rows (empty: no table); @p columns: the rows' parts shown, in this order (the long text
    //last, where the table stretches); empty: every part, as they come
    NextReportView(Emulator * e, const std::string &device, const std::vector<std::string> &keys,
                   const std::string &table, const std::vector<std::string> &columns, QWidget *parent);
    //Reads the report (on the emulation thread) and shows it; a value that differs from the last stop in the
    //changed colour
    void refresh();
    //The values now are the ones the next stop compares with
    void mark_stop();
    //The machine has the report (a Next, a card)
    bool present() const;
    //One more list of the report as a table of its own, under the first (@p columns as the constructor's)
    void add_table(const std::string &table, const std::vector<std::string> &columns);

    //A card's view: the report it reads on the card @p card ("gs", "ms"): the catalog report @p device, or its part
    //@p part ("multisound", "gs": the MultiSound's GS part) taken as the report
    void add_source(const std::string &card, const std::string &device, const std::string &part = std::string());
    //Reads the card @p card's report from now on (none of its own: the view is absent), @p caption over the view
    //names it; true when the report read changed (the values at the last stop are dropped: another card's)
    bool use_source(const std::string &card, const QString &caption);
    //The card part of the report read (empty: the report whole)
    const std::string &part() const { return m_part; }
    //The catalog report read
    const std::string &report() const { return m_device; }

    //Turns the report into the parts shown (decoded groups; a list of rows the table may name); false: nothing of its
    //own on this machine
    typedef std::function<bool(const DeviceNode &report, DeviceNode &shown)> Shape;
    //A shaped view: with @p needs (a part of the report, "neogs") the view is there only while the report has it, and
    //says @p absent otherwise; without, a report the shape declines is shown as it is
    void set_shape(const Shape &shape, const std::string &needs = std::string(), const QString &absent = QString());
    //The report's part the view needs to be there (empty: none)
    const std::string &needs() const { return m_needs; }
    //What the view says without its report
    void set_absent(const QString &text) { m_absent = text; }
    //A button under the view; its answer goes to the line next to the buttons, then the view is read again
    void add_action(const QString &title, const std::function<QString()> &run);

    //For the self-check
    QTreeWidget * fields() const { return m_fields; }
    QTableWidget * table(size_t i = 0) const { return i < m_tables.size() ? m_tables[i].widget : nullptr; }
    QLabel * caption() const { return m_caption; }
    QLabel * answer() const { return m_answer; }

private:
    ComputerDevice * device() const;
    //The report of @p d (its card part when the view reads one); false: not read
    bool read(ComputerDevice * d, DeviceNode &report) const;
    struct Table {
        std::string key;
        std::vector<std::string> columns;
        QTableWidget * widget;
    };
    //A table's rows (none: empty)
    void fill(const Table &t, const DeviceNode * rows);
    //Keeps @p value as shown at @p path; true when it differs from the last stop
    bool changed(const std::string &path, const std::string &value);

    Emulator * e;
    std::string m_device;
    std::vector<std::string> m_keys;
    std::vector<Table> m_tables;
    QSplitter * m_split = nullptr;  //the fields over the tables, made by the first table
    std::string m_part;
    std::map<std::string, std::pair<std::string, std::string>> m_sources;  //card: device, part
    QLabel * m_caption;
    Shape m_shape;
    std::string m_needs;
    QString m_absent;
    QWidget * m_actions = nullptr;  //the buttons' row, made by the first action
    QLabel * m_answer = nullptr;
    QLabel * m_status;
    QTreeWidget * m_fields;
    std::map<std::string, std::string> m_now;   //every value shown, by path
    std::map<std::string, std::string> m_stop;  //the values at the last stop
};

struct NextTab {
    QString title;
    NextReportView * view;
};

//The six tabs, in their order (each view named "next:<report>")
std::vector<NextTab> CreateNextTabs(Emulator * e, QWidget *parent);
//The General Sound card's tabs (gs-debugger TODO P5.1, P5.5): Log, Protocol, Firmware, Hardware, Stats, the device
//catalog's gs-commands, gs, gs-firmware, gs, gs-stats (each view named "card:<report>"; on the MultiSound's GS
//ms-gs-commands, the multisound report's gs part, ms-gs-firmware, ms-gs-stats: CardTabsFollow picks the card); the
//Hardware tab shows the NeoGS board on a NeoGS (gui-card-debugger B3.1); then, only on a NeoGS, DMA (B3.2,
//"card:neogs-dma") and SD · MP3 · Flash (B3.3, "card:neogs-media"), both shaped from the gs report's neogs part; then,
//only on the lightweight player (A10, W2-W3), Interpreter ("card:lw-interpreter", the gs report's interpreter part)
//and Player ("card:lw-player", its player part: the song, the four channels and the samples as tables)
std::vector<NextTab> CreateCardTabs(Emulator * e, QWidget *parent);
//The card the card tabs follow (gs-debugger P5.5): with both a General Sound (`gs`) and a MultiSound's GS (`ms`), the
//card CPU @p front names (the card CPU's tab in front, else the first card CPU); else the one present; "" for none.
//@p e's catalog reports tell which are there (gs: the `gs` report; ms: `ms-gs-stats`)
std::string CardTabsCard(Emulator * e, const std::string &front);
//Points every card tab at @p card's reports, with its caption; true when one changed
bool CardTabsFollow(const std::vector<NextTab> &tabs, const std::string &card);
