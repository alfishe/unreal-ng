// unreal-debugger (not in ecat): the GS analysis tabs whose data the core has (gui-card-debugger A5.2a; gs-debugger
// requirements M3, M4, M5, AU6, X7), card side tabs after the existing card tabs, for every card that carries a
// General Sound (GS, NeoGS, the lightweight player, the MultiSound's GS):
//   GS dispatch  the command table as the card holds it now: command, name, handler, the handler's label, the ROM's
//                handler, the patched entries tinted with the upload that patched them; over it the profile stack
//                (`GS v1.05a ▸ gscode.bin`)
//   GS unknown   the commands the decoding profile does not name, with their counts since the tab's Clear
//   GS objects   the profile's structs as tables (columns from the schema, rows the instances), its variables; read
//                as the active profile or as another one that declares structs
//   GS writers   the last write to each card port and to the host's #B3 / #BB / #33: value, writer PC + label, frame
// Each reads at the window's stops and, only while in sight and the machine runs, every Emulator::live_card_ms (a read
// where the machine stands still in process, plain GETs over the WebAPI); nothing pauses

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <QWidget>

#include "emulator/emulator.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;
class QVBoxLayout;

//What the four tabs share: the card followed, the caption naming it, the live timer while in sight
class GsCardTab : public QWidget
{
    Q_OBJECT
public:
    GsCardTab(Emulator * e, const char * name, QWidget *parent);
    //The card read: "gs" or "ms" ("" none), @p caption over the tab names it; another card starts the tab afresh
    void set_card(const std::string &card, const QString &caption);
    const std::string &card() const { return m_card; }
    //Reads the card once and shows it
    virtual void refresh() = 0;
    QLabel * line() const { return m_line; }

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    //The card changed: what the tab kept of the last one goes
    virtual void restart() {}
    //The profiles known (read once): id -> title
    QString profile_title(const std::string &id);
    //A table of the tabs' look: rows selected whole, no editing, the debugger's font
    QTableWidget * make_table(const char * name, const QStringList &columns);

    Emulator * e;
    std::string m_card;
    QVBoxLayout * m_layout;
    QLabel * m_caption;
    QLabel * m_line;
    std::map<std::string, std::string> m_titles;

private:
    void on_timer();
    QTimer * m_timer;
};

class GsDispatchView : public GsCardTab
{
    Q_OBJECT
public:
    enum { ColCommand = 0, ColName, ColHandler, ColLabel, ColRom, ColPatched, ColCount };
    explicit GsDispatchView(Emulator * e, QWidget *parent);
    void refresh() override;
    //For the self-check
    QLabel * stack() const { return m_stack; }
    QTableWidget * table() const { return m_table; }
    QCheckBox * named_only() const { return m_named; }

private:
    QLabel * m_stack;
    QCheckBox * m_named;
    QTableWidget * m_table;
};

class GsUnknownView : public GsCardTab
{
    Q_OBJECT
public:
    enum { ColCommand = 0, ColSince, ColTotal, ColDispatch, ColCount };
    explicit GsUnknownView(Emulator * e, QWidget *parent);
    void refresh() override;
    //The counts now are the baseline: the rows go until a command is sent again (the command log keeps its counts)
    void clear_counts();
    //For the self-check
    QTableWidget * table() const { return m_table; }
    QPushButton * clear_button() const { return m_clear; }
    QPushButton * turn_on_button() const { return m_turn_on; }

protected:
    void restart() override;

private:
    QPushButton * m_clear;
    QPushButton * m_turn_on;
    QTableWidget * m_table;
    std::map<int, uint64_t> m_totals;  //the firmware's counts at the last read
    std::map<int, uint64_t> m_base;    //and at Clear
};

class GsObjectsView : public GsCardTab
{
    Q_OBJECT
public:
    explicit GsObjectsView(Emulator * e, QWidget *parent);
    void refresh() override;
    //Read as profile @p id ("" the active one); false: no such entry in the picker
    bool read_as(const std::string &id);
    //For the self-check: the struct tables, the variables' table
    std::vector<QTableWidget *> struct_tables() const;
    QTableWidget * variables() const { return m_variables; }
    QComboBox * picker() const { return m_picker; }

protected:
    void restart() override;

private:
    void fill_picker();
    QComboBox * m_picker;
    QWidget * m_body;
    QVBoxLayout * m_body_layout;
    struct Shown {
        std::string key;             //name + struct + columns: the table is rebuilt when it changes
        QLabel * title;
        QTableWidget * table;
    };
    std::vector<Shown> m_shown;
    QLabel * m_variables_title;
    QTableWidget * m_variables;
    std::map<std::string, std::string> m_last;  //each cell's text at the last read: a change in the changed colour
};

class GsWritersView : public GsCardTab
{
    Q_OBJECT
public:
    enum { ColPort = 0, ColRegister, ColSide, ColValue, ColPc, ColLabel, ColFrame, ColSource, ColWrites, ColCount };
    explicit GsWritersView(Emulator * e, QWidget *parent);
    void refresh() override;
    //For the self-check: the row of a port (host #B3 / #BB / #33 or a card port), -1 none
    int row_of(bool host, unsigned int port) const;
    QTableWidget * table() const { return m_table; }
    QPushButton * start_button() const { return m_start; }
    QPushButton * turn_on_button() const { return m_turn_on; }

private:
    QPushButton * m_start;
    QPushButton * m_turn_on;
    QLabel * m_answer;
    QTableWidget * m_table;
};
