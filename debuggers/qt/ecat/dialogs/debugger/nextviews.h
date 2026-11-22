// unreal-debugger (not in ecat): the ZX Spectrum Next's tabs of the debugger window (NextREG, MMU, Copper, Sprites,
// DMA, CTC / IM2) and a General Sound card's (Log, Protocol, Firmware, Hardware, Stats), each a device catalog report
// read whole: its fields in a tree, its list of rows in a table

#pragma once

#include <map>
#include <string>
#include <vector>

#include <QString>
#include <QWidget>

#include "emulator/emulator.h"

class QLabel;
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
    //The catalog report read
    const std::string &report() const { return m_device; }

    //For the self-check
    QTreeWidget * fields() const { return m_fields; }
    QTableWidget * table() const { return m_table; }

private:
    ComputerDevice * device() const;

    Emulator * e;
    std::string m_device;
    std::vector<std::string> m_keys;
    std::string m_table_key;
    std::vector<std::string> m_columns;
    QLabel * m_status;
    QTreeWidget * m_fields;
    QTableWidget * m_table = nullptr;
    std::map<std::string, std::string> m_now;   //every value shown, by path
    std::map<std::string, std::string> m_stop;  //the values at the last stop
};

struct NextTab {
    QString title;
    NextReportView * view;
};

//The six tabs, in their order (each view named "next:<report>")
std::vector<NextTab> CreateNextTabs(Emulator * e, QWidget *parent);
//The General Sound card's five tabs (gs-debugger TODO P5.1): Log, Protocol, Firmware, Hardware, Stats, the device
//catalog's gs-commands, gs, gs-firmware, gs, gs-stats (each view named "card:<report>")
std::vector<NextTab> CreateCardTabs(Emulator * e, QWidget *parent);
