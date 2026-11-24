#pragma once

#include <QImage>
#include <QString>
#include <QWidget>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "debugger/targets/debugcpuid.h"

class EmulatorContext;
class QBoxLayout;
class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QSplitter;
class QTableWidget;
struct StateNode;

/// @file gscpuanalysispane.h
/// @brief The General Sound analysis tabs over the card CPU's profiler and its code / data analysis (gs-debugger TODO
/// P5.6, gui-card-debugger.md A5.2a rows Events, Profiler, Coverage, Heat map; requirements X2, X8, X9, X10, X11,
/// X20). One widget, four views, each a card side tab:
///   - Profiler: the ISR budget gauge (avg / period · max · headroom), overruns, latency and lost requests of the
///     last frame and since start; the cost of each host command; the routines (inclusive / exclusive, sortable);
///     the activity per frame (ISR / idle / halt / busy %). Start / Stop / Clear (`gs profile`).
///   - Events: the last complete host frame on a canvas, a row per card interrupt period, a column per card cycle:
///     the ISR as a bar, dots for the host's #B3 / #BB / #33 accesses, DAC fetches (coloured by channel), page and
///     volume writes, ZX-DMA bytes (`gs events`); categories hidden from the legend; a tooltip names the event.
///   - Coverage: the code / data log per space (ROM or flash, RAM) and page with its code / data / DAC ranges, the
///     dispatcher's handlers executed and not, the reads of RAM never written with the PC that read them
///     (`gs coverage`). On / Off / Reset / Release (`gs analysis`).
///   - Heat map: the card RAM as a grid of cells coloured by a channel (fetch, read, write, DAC, SD-DMA, ZX-DMA) or
///     all of them, the decay per frame, the ranges the DAC played in the last N frames (`gs heatmap`).
/// No device code here: the reads are the reports every plane serves (DeviceState::GsProfile, GsEvents, GsCoverage,
/// GsHeatmap) and the switches are their verbs (GSProfileControl, GSAnalysisControl, the port trace's history). The
/// pane never reads by itself: its owner calls refresh with a context it holds a lease on (the card panes' live
/// refresh while the tab is in front, once at a stop) and runAction for a button (actionRequested).
class GsCpuAnalysisPane : public QWidget
{
    Q_OBJECT

public:
    enum class View
    {
        Profiler,
        Events,
        Coverage,
        HeatMap,
    };
    /// The routines and frames the Profiler reads per refresh; the rows of its activity table (newest first)
    static constexpr size_t kRoutines = 64;
    static constexpr size_t kActivityFrames = 256;
    static constexpr int kActivityRows = 32;
    /// The heat map's grid: cells at most (neighbours merged by their maximum)
    static constexpr uint32_t kHeatCells = 16384;

    /// `follow`: the debugger CPU tab in front (the card it shows: CardPanes::Front); null: the GS slot's
    GsCpuAnalysisPane(View view, std::shared_ptr<DebugCpuId> follow = {}, QWidget* parent = nullptr);

    View view() const { return _view; }
    /// Read the view's data on `context` and show it; `live`: the machine runs (a frame not yet ended is not read
    /// again)
    void refresh(EmulatorContext* context, bool live);
    /// Another card: read afresh
    void reset();
    /// A button's verb on `context`: profile_start, profile_stop, profile_clear, trace_start (the card's port trace
    /// history to ring), analysis_on, analysis_off, analysis_reset, analysis_release, analysis_decay (the decay
    /// setting). False with `error` when the core refused
    bool runAction(EmulatorContext* context, const QString& action, QString* error = nullptr);

    /// region <For the tests>
    QString status() const;
    QString headline() const;
    /// The off / hint bar's text (empty: hidden)
    QString hint() const;
    /// The button of `action` (as runAction names it); null when the view has none
    QPushButton* button(const QString& action) const;
    QTableWidget* commands() const { return _commands; }
    QTableWidget* routines() const { return _routines; }
    QTableWidget* activity() const { return _activity; }
    QTableWidget* pages() const { return _pages; }
    QTableWidget* handlers() const { return _handlers; }
    QTableWidget* neverWritten() const { return _neverWritten; }
    QTableWidget* recent() const { return _recent; }
    QProgressBar* gauge() const { return _gauge; }
    class GsEventsCanvas* events() const { return _events; }
    class GsHeatmapCanvas* heatmap() const { return _heatmap; }
    QComboBox* channel() const { return _channel; }
    QSpinBox* decay() const { return _decay; }
    QSpinBox* frames() const { return _frames; }
    /// The caption over `table`
    QString caption(const QTableWidget* table) const;
    /// The text of `table`'s row `row` under header `column`; empty when there is none
    static QString Cell(const QTableWidget* table, int row, const QString& column);
    /// The row of `table` whose cell under `column` reads `text`; -1 when none
    static int FindRow(const QTableWidget* table, const QString& column, const QString& text);
    /// endregion

signals:
    void actionRequested(const QString& action);

private:
    void showUnavailable(const QString& why);
    void showBody(const QString& headline);
    void showHint(const QString& text);
    QPushButton* addButton(QBoxLayout* row, const QString& action, const QString& text, const QString& tooltip);
    /// A captioned table, the next pane of `split`
    QTableWidget* addTable(QSplitter* split, const QStringList& columns, const QString& caption);
    void setCaption(QTableWidget* table, const QString& text);

    void refreshProfiler(EmulatorContext* context, const std::string& cpu, DebugCpuId cardCpu);
    void refreshEvents(EmulatorContext* context, const std::string& cpu);
    void refreshCoverage(EmulatorContext* context, const std::string& cpu, DebugCpuId cardCpu);
    void refreshHeatmap(EmulatorContext* context, const std::string& cpu);
    /// The analysis switch's buttons from its status (Coverage, Heat map); false when the analysis has no data
    bool showAnalysisState(const StateNode& report);

    View _view;
    std::shared_ptr<DebugCpuId> _follow;
    QLabel* _status = nullptr;
    QWidget* _body = nullptr;
    QLabel* _headline = nullptr;
    QWidget* _hintBar = nullptr;
    QLabel* _hint = nullptr;
    std::vector<std::pair<QString, QPushButton*>> _buttons;
    std::map<const QTableWidget*, QLabel*> _captions;

    // Profiler
    QProgressBar* _gauge = nullptr;
    QTableWidget* _commands = nullptr;
    QTableWidget* _routines = nullptr;
    QTableWidget* _activity = nullptr;
    // Events
    class GsEventsCanvas* _events = nullptr;
    QScrollArea* _eventsScroll = nullptr;
    std::vector<QCheckBox*> _categories;
    // Coverage
    QTableWidget* _pages = nullptr;
    QTableWidget* _handlers = nullptr;
    QTableWidget* _neverWritten = nullptr;
    // Heat map
    class GsHeatmapCanvas* _heatmap = nullptr;
    QComboBox* _channel = nullptr;
    QSpinBox* _decay = nullptr;
    QSpinBox* _frames = nullptr;
    QTableWidget* _recent = nullptr;

    std::string _card;          ///< the card shown ("gs" / "ms"): another one reads afresh
    uint64_t _lastFrame = ~0ull;  ///< the host frame of the last read (a live refresh within it reads nothing)
};

/// The Events canvas: a row per card interrupt period of one host frame, a column per card cycle of the period
class GsEventsCanvas : public QWidget
{
public:
    /// The event kinds of the `gs events` report, in its order
    enum Kind
    {
        HostDataWrite,
        HostDataRead,
        HostCommand,
        HostStatus,
        HostControl,
        Dac,
        Page,
        Volume,
        ZxDmaRead,
        ZxDmaWrite,
        KindCount
    };
    /// The legend's categories (each hidden or shown)
    enum Category
    {
        IsrBars,
        HostData,
        HostCommandStatus,
        HostControlWrites,
        DacFetches,
        PageWrites,
        VolumeWrites,
        ZxDma,
        CategoryCount
    };
    struct Bar
    {
        int64_t accept = 0;
        int64_t end = -1;  ///< -1: still running at the frame's end
        int entry = 0;
        bool nmi = false;
    };
    struct Dot
    {
        int64_t cycle = 0;
        int kind = 0;
        int value = 0;
        int64_t detail = 0;
    };
    struct Row
    {
        int64_t start = 0;  ///< the period's start in cycles from the frame's start
        std::vector<Bar> isrs;
        std::vector<Dot> events;
    };
    static constexpr int kRowPixels = 2;

    explicit GsEventsCanvas(QWidget* parent = nullptr);
    void setFrame(std::vector<Row> rows, int64_t periodCycles);
    void setShown(Category category, bool shown);

    int rowCount() const { return static_cast<int>(_rows.size()); }
    int64_t periodCycles() const { return _period; }
    const std::vector<Row>& rows() const { return _rows; }
    /// What lies at cycle `cycle` of period row `row` (the tooltip's text)
    QString describe(int row, int64_t cycle, int64_t tolerance = 2) const;

    static Kind KindOf(const std::string& name);
    static Category CategoryOf(int kind);
    static QColor Colour(int kind, int64_t detail);
    static QString CategoryName(Category category);
    static QColor CategoryColour(Category category);

protected:
    void paintEvent(class QPaintEvent* event) override;
    bool event(QEvent* event) override;
    QSize sizeHint() const override;

private:
    std::vector<Row> _rows;
    int64_t _period = 0;
    std::array<bool, CategoryCount> _shown{};
};

/// The Heat map canvas: the card RAM as a grid of cells, each the heat of one channel or of all of them
class GsHeatmapCanvas : public QWidget
{
public:
    static constexpr int kChannels = 6;  ///< fetch, read, write, dac, sd_dma, zx_dma (GSCardAnalysis::Channel)

    explicit GsHeatmapCanvas(QWidget* parent = nullptr);
    /// The grids of the channels (`grids[c]` empty: not read), `channel` the one shown (-1: all, blended)
    void setGrid(std::array<std::vector<uint8_t>, kChannels> grids, int channel, uint32_t columns, uint32_t cellBytes,
                 uint32_t group);
    /// Show channel `channel` (-1: all, blended) of the grids held
    void setChannel(int channel);

    int cells() const;
    int hotCells() const;  ///< cells of the channel shown with any heat
    int columns() const { return static_cast<int>(_columns); }
    /// The tooltip of cell `cell`: its RAM bytes, page and offset, the heat of each channel
    QString describe(int cell) const;

    static QString ChannelName(int channel);
    static QColor ChannelColour(int channel);

protected:
    void paintEvent(class QPaintEvent* event) override;
    bool event(QEvent* event) override;

private:
    int cellAt(const QPoint& point) const;
    QRect gridRect() const;

    std::array<std::vector<uint8_t>, kChannels> _grids;
    int _channel = -1;
    uint32_t _columns = 0;
    uint32_t _cellBytes = 0;
    uint32_t _group = 1;
    QImage _image;
};
