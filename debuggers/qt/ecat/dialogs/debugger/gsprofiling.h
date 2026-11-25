// unreal-debugger (not in ecat): the GS analysis tabs on the card CPU's profiler and analysis (gui-card-debugger
// A5.2a rows Profiler, Events, Coverage, Heat map; gs-debugger requirements X2, X8, X9, X10, X11, X20), after the
// other GS analysis tabs, for every card that carries a General Sound (the lightweight player has no card CPU: each
// says so):
//   GS profiler  the ISR budget (avg / period, max, headroom, overruns, latency, lost; last frame and since start),
//                each host command's cost, the routines (inclusive / exclusive, labels), the activity per frame with
//                its graph; Start / Stop / Clear (it slows the card while on)
//   GS events    the last complete host frame on a canvas: a row an interrupt period, a column a card cycle in it;
//                the ISR as a bar, the host's #B3 / #BB, DAC fetches, page / volume writes, ZX-DMA bytes as dots
//   GS coverage  the code / data log per space and page (counts, code / data / DAC ranges), the command handlers
//                executed and not, the reads of RAM never written; On / Off / Reset / Release
//   GS heat map  the card RAM as a grid coloured by channel (or all of them), the decay, the DAC ranges of the
//                last frames
// Every read is one core report as JSON (Emulator::read_gs_report: in process on this thread, over the WebAPI a
// GET), at the window's stops and, while in sight and the machine runs, every Emulator::live_card_ms; nothing pauses

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <QImage>
#include <QJsonObject>
#include <QWidget>

#include "gsanalysis.h"

class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QTableWidget;

//What the four tabs share: one report of their card read as JSON, the answer of a control in a line
class GsReportTab : public GsCardTab
{
    Q_OBJECT
public:
    GsReportTab(Emulator * e, const char * name, QWidget *parent);
    ~GsReportTab() override;
    //The last report read (empty before the first): the self-check reads it
    const QJsonObject &report() const { return m_report; }
    QLabel * answer() const { return m_answer; }

protected:
    //The card's report `report` with `query` (cpu added); false: m_line says why (not read, or not available)
    bool read(const char * report, std::map<std::string, std::string> query);
    //A control's answer under the line
    void say(bool ok, const std::string &message);
    //The card CPU's symbol at `address` ("" none): the card's own labels, as its listing shows them
    QString label_at(unsigned int address);
    void restart() override;

    QJsonObject m_report;
    QLabel * m_answer;

private:
    CPU * m_label_cpu = nullptr;
    DisAsm * m_label_disasm = nullptr;
};

//The ISR budget as a bar: the period, the average used (filled), the most (a mark)
class GsBudgetBar : public QWidget
{
    Q_OBJECT
public:
    explicit GsBudgetBar(QWidget *parent);
    void set(double avg, double max, double period, bool any);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    double m_avg = 0, m_max = 0, m_period = 0;
    bool m_any = false;
};

//The activity per frame as lines: busy % and ISR % of the newest frames
class GsActivityGraph : public QWidget
{
    Q_OBJECT
public:
    explicit GsActivityGraph(QWidget *parent);
    void set(const std::vector<double> &busy, const std::vector<double> &isr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    std::vector<double> m_busy, m_isr;
};

class GsProfilerView : public GsReportTab
{
    Q_OBJECT
public:
    enum { RColAddress = 0, RColPage, RColLabel, RColCalls, RColInclusive, RColExclusive, RColAvg, RColMax, RColPct, RColCount };
    enum { CColCode = 0, CColName, CColHandler, CColCount, CColAvg, CColMin, CColMax, CColTotal, CColCols };
    explicit GsProfilerView(Emulator * e, QWidget *parent);
    void refresh() override;
    //For the self-check
    QLabel * budget() const { return m_budget; }
    QLabel * state() const { return m_state; }
    QTableWidget * routines() const { return m_routines; }
    QTableWidget * commands() const { return m_commands; }
    QTableWidget * activity() const { return m_activity; }
    QPushButton * start_button() const { return m_start; }
    QPushButton * stop_button() const { return m_stop; }
    QPushButton * clear_button() const { return m_clear; }

private:
    void control(const char * action);
    QPushButton * m_start;
    QPushButton * m_stop;
    QPushButton * m_clear;
    GsBudgetBar * m_bar;
    QLabel * m_budget;   //the ISR lines: last frame, since start
    QLabel * m_state;    //why a table is empty (interrupts off, no routine yet)
    QTableWidget * m_commands;
    QTableWidget * m_routines;
    GsActivityGraph * m_graph;
    QTableWidget * m_activity;
};

//The frame map: a row an interrupt period, a column a card cycle of it
class GsEventsCanvas : public QWidget
{
    Q_OBJECT
public:
    struct Bar {
        int64_t accept = 0, end = -1;  //cycles from the period's start; end -1: still running at the frame's end
        bool nmi = false;
    };
    struct Dot {
        int64_t cycle = 0;
        int kind = 0;                  //an index of kinds()
        int value = 0;
        int64_t detail = 0;
    };
    struct Row {
        int64_t period = 0, start = 0;
        std::vector<Bar> bars;
        std::vector<Dot> dots;
    };
    explicit GsEventsCanvas(QWidget *parent);
    void set(int64_t period_cycles, std::vector<Row> rows);
    const std::vector<Row> &rows() const { return m_rows; }
    //The kinds of the core's report, in its order, and their colour (a DAC fetch's by its channel)
    static const std::vector<const char *> &kinds();
    static QColor color(int kind, int64_t detail);
    static QString kind_text(int kind);
    //The point's description (the tooltip): period, cycle, the bar or the dots there
    QString describe(const QPoint &at) const;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;

private:
    int row_height() const { return 2; }
    double scale() const;
    int64_t m_period = 0;
    std::vector<Row> m_rows;
};

class GsEventsView : public GsReportTab
{
    Q_OBJECT
public:
    explicit GsEventsView(Emulator * e, QWidget *parent);
    void refresh() override;
    //For the self-check
    GsEventsCanvas * canvas() const { return m_canvas; }
    QLabel * hint() const { return m_hint; }
    QPushButton * profiler_button() const { return m_profiler; }
    QPushButton * trace_button() const { return m_trace; }

private:
    QPushButton * m_profiler;
    QPushButton * m_trace;
    QLabel * m_hint;
    QLabel * m_legend;
    QScrollArea * m_scroll;
    GsEventsCanvas * m_canvas;
};

class GsCoverageView : public GsReportTab
{
    Q_OBJECT
public:
    enum { PColSpace = 0, PColPage, PColExec, PColCode, PColRead, PColWrite, PColDac, PColWritten, PColCodeRanges,
           PColDataRanges, PColDacRanges, PColCount };
    enum { HColCommand = 0, HColName, HColHandler, HColWhere, HColState, HColCount };
    enum { NColAddress = 0, NColPlace, NColPc, NColLabel, NColFrame, NColKind, NColCount };
    explicit GsCoverageView(Emulator * e, QWidget *parent);
    void refresh() override;
    //For the self-check
    QTableWidget * pages() const { return m_pages; }
    QTableWidget * handlers() const { return m_handlers; }
    QTableWidget * never_written() const { return m_never; }
    QLabel * handlers_line() const { return m_handlers_line; }
    QPushButton * on_button() const { return m_on; }
    QPushButton * off_button() const { return m_off; }
    QPushButton * reset_button() const { return m_reset; }
    QPushButton * release_button() const { return m_release; }
    QPushButton * break_new_button() const { return m_break_new; }

private:
    void control(const char * action);
    QPushButton * m_on;
    QPushButton * m_off;
    QPushButton * m_reset;
    QPushButton * m_release;
    QPushButton * m_break_new;
    QTableWidget * m_pages;
    QLabel * m_handlers_line;
    QTableWidget * m_handlers;
    QLabel * m_never_line;
    QTableWidget * m_never;
};

//The heat map: a cell a group of RAM bytes, a colour by channel (one channel: its colour by heat; all: added)
class GsHeatCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit GsHeatCanvas(QWidget *parent);
    //`grids` a channel's heat per cell (0-255), `names` its channel; `shown` -1 all, else the index
    void set(int columns, int64_t cells, int64_t cell_bytes, std::vector<std::vector<uint8_t>> grids,
             std::vector<std::string> names, int shown);
    static QColor color(const std::string &channel);
    QString describe(const QPoint &at) const;
    const QImage &image() const { return m_image; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;

private:
    QRect target() const;
    int m_columns = 0;
    int64_t m_cells = 0, m_cell_bytes = 0;
    std::vector<std::vector<uint8_t>> m_grids;
    std::vector<std::string> m_names;
    QImage m_image;
};

class GsHeatmapView : public GsReportTab
{
    Q_OBJECT
public:
    enum { RColChannel = 0, RColStart, RColEnd, RColLength, RColPlace, RColCount };
    explicit GsHeatmapView(Emulator * e, QWidget *parent);
    void refresh() override;
    //For the self-check: the cells with any heat in the grid read last (every channel)
    int64_t hot_cells() const { return m_hot; }
    GsHeatCanvas * canvas() const { return m_canvas; }
    QTableWidget * recent() const { return m_recent; }
    QComboBox * channel() const { return m_channel; }
    QPushButton * on_button() const { return m_on; }

private:
    QPushButton * m_on;
    QComboBox * m_channel;
    QSpinBox * m_decay;
    GsHeatCanvas * m_canvas;
    QLabel * m_recent_line;
    QTableWidget * m_recent;
    int64_t m_hot = 0;
};
