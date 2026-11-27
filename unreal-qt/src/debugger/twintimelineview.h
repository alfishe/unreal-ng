#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "debugger/targets/debugcpuid.h"

class Emulator;
class QAction;
class QTimer;
struct StateNode;

namespace CardPanes
{
class LiveLine;
}

/// @file twintimelineview.h
/// @brief The twin timeline strip of the embedded debugger (gs-debugger TODO P5.6 phase 2, gui-card-debugger.md A5.1,
/// C03): both CPUs on one clock, full width under the toolbar, shown only while a card with a CPU is fitted.
///
///   MAIN  | blocks of the main CPU's recent instructions, width = measured T; the current one in bus cycles |  readout
///   NOW   ------------------------------|---------- the axis in main T relative to NOW ------------------  T · frame · gap
///   card  | the card's blocks (its units converted to main T), the next instruction outlined           |  cycle · MHz
///
/// Markers: a breakpoint (filled dot), a host port access (two arrows, the access in the tooltip), INT / NMI, the
/// NeoGS clock change (a diamond). Paused (or stopped): the detail of timeline::Read. Running: per-frame density bars
/// (main: the game's GS port accesses; card: busy / idle steps, interrupts) read at most 10 times a second on the
/// LiveLine pattern; a running read never parks the emulation thread.
///
/// Interaction: the wheel zooms around the pointer (default ± 60 T around NOW; zoomed out, the blocks merge into a
/// density bar), a drag pans, a double-click recenters on NOW, a click on a block (or the next instruction) asks for
/// that CPU's disassembly at its address (cpuTargetRequested).
///
/// No load on the emulation while not shown: the timeline is armed (a lease of this view, until released) only while
/// the strip is visible; hidden, closed, destroyed or another machine set, the lease is released. The strip is opt-in:
/// Debug > Show GS Timeline (TwinTimelineSettings), off by default.
///
/// Never a wait on the emulation thread (owner report 2026-11-26: Step Over / Step Out froze the GUI thread): a paused
/// read is one try (timeline::Query::waitMs = 0). Not parked yet (a stop on its way), stepped by another thread, or
/// another client holding the CPU (eCat over --connect reading at the same stop): the last picture stays and the read
/// is tried again a little later (kRetryMs), never blocked for.
class TwinTimelineView : public QWidget
{
    Q_OBJECT

public:
    static constexpr double kDefaultSpanT = 60.0;
    static constexpr double kMinSpanT = 4.0;
    static constexpr double kMaxSpanT = 100000.0;
    static constexpr int kRunningIntervalMs = 100;  ///< 10 Hz at most
    static constexpr int kRetryMs = 30;             ///< a paused read that found the machine busy: again after this
    static constexpr int kRetries = 30;             ///< then the line says why the picture is old
    /// Below this many pixels per T the blocks merge into a density bar
    static constexpr double kMergePixelsPerT = 0.4;

    enum Lane
    {
        MainLane = 0,
        CardLane = 1,
    };

    struct Block
    {
        double at = 0.0;        ///< main T relative to NOW
        double duration = 0.0;  ///< main T
        int pc = 0;
        QString kind;           ///< instruction, idle, int, nmi
        QString text;
        QString label;
        bool open = false;
        qint64 idleIterations = 0;
    };
    struct Next
    {
        bool known = false;
        double at = 0.0;
        int pc = 0;
        QString text;
        QString label;
    };
    struct Marker
    {
        Lane lane = MainLane;
        QString kind;  ///< breakpoint, host_port, int, nmi, clock_change
        double at = 0.0;
        int pc = 0;
        QString text;
    };
    /// A bus cycle of main's current instruction (a card's stop parked main inside it)
    struct Segment
    {
        QString kind;  ///< ocf, od, mr, mw, pr, pw, int
        double at = 0.0;
        double duration = 0.0;
    };
    struct Tick
    {
        qint64 frame = 0;
        int commands = 0, dataWrites = 0, dataReads = 0, statusPolls = 0, accesses = 0;
        int steps = 0, idleSteps = 0, busySteps = 0, interrupts = 0, nmis = 0;
        bool profiled = false;
        qint64 cycles = 0, busyCycles = 0;
    };

    explicit TwinTimelineView(QWidget* parent = nullptr);
    ~TwinTimelineView() override;

    /// The machine shown: the lease on the previous one ends; a visible strip arms the new one
    void setEmulator(std::shared_ptr<Emulator> emulator);
    /// The debugger's call: the machine while a card CPU is fitted (null: none) and the card lane's CPU. Shown (armed,
    /// read) only while Debug > Show GS Timeline is on as well; the switch's change shows or hides it
    void follow(std::shared_ptr<Emulator> emulator, DebugCpuId cardCpu);
    /// The card lane's CPU (the card tab in front); Main: the first card CPU fitted
    void setCardCpu(DebugCpuId cpu);
    /// A stop's refresh: the paused detail (the running density while it runs). Nothing while hidden
    void refresh();

    /// region <For the tests>
    bool armed() const { return _leased; }
    bool running() const { return _running; }
    bool hasDetail() const { return _detail; }
    QString state() const { return _state; }
    QString cardCpuName() const { return _cardCpuName; }
    const std::vector<Block>& blocks(Lane lane) const { return _blocks[lane]; }
    const Next& next(Lane lane) const { return _next[lane]; }
    const std::vector<Marker>& markers() const { return _markers; }
    const std::vector<Segment>& segments() const { return _segments; }
    int segmentBlock() const { return _segmentBlock; }
    const std::vector<Tick>& density() const { return _density; }
    double gapT() const { return _gap; }
    double nowT() const { return _nowT; }
    qint64 nowFrame() const { return _nowFrame; }
    QString laneReadout(Lane lane) const { return _readout[lane]; }
    QString nowReadout() const;
    double span() const { return _span; }
    double center() const { return _center; }
    void setSpan(double span);
    void setCenter(double center);
    /// The wheel: `steps` notches (positive zooms in) around main T `anchor`
    void zoomBy(double steps, double anchor);
    /// A drag by `dx` pixels (positive: to the right, the view shows earlier time)
    void panByPixels(int dx);
    /// The double-click: NOW back in the middle
    void recenter();
    bool merged() const;
    double pixelsPerT() const;
    double xOfT(double t) const;
    QRectF blockRect(Lane lane, size_t index) const;
    QRectF nextRect(Lane lane) const;
    /// A click at `pos`: true when it hit a block (cpuTargetRequested sent)
    bool clickAt(const QPoint& pos);
    /// The tooltip at `pos` ("" none)
    QString toolTipAt(const QPoint& pos) const;
    int runningReads() const { return _runningReads; }
    /// Paused reads that found the machine busy (tried again later), for the tests
    int busyReads() const { return _busyReads; }
    /// Reads of timeline::Read done (both kinds), for the tests
    int reads() const { return _reads; }
    CardPanes::LiveLine* liveLine() const { return _live; }
    QString client() const { return _client; }
    /// endregion

signals:
    /// A block clicked: that CPU's disassembly at `pc`
    void cpuTargetRequested(DebugCpuId cpu, uint16_t pc);

public:
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool event(QEvent* event) override;

private:
    struct Geometry
    {
        int left = 0, right = 0;  ///< the canvas columns (labels left of it, readouts right)
        int mainMarkTop = 0, mainTop = 0, mainBottom = 0;
        int axisTop = 0, axisBottom = 0;
        int cardTop = 0, cardBottom = 0, cardMarkBottom = 0;
    };
    Geometry geometry() const;
    void arm(bool on);
    /// The followed machine and the switch: shown with its machine, or hidden with none
    void applyShown();
    /// One read of timeline::Read; `live`: the running density tick
    void read(bool live);
    /// A paused read the machine was busy for: again in kRetryMs (at most kRetries times per stop)
    void retryLater();
    void take(const StateNode& node);
    void viewChanged();
    DebugCpuId laneCpu(Lane lane) const;
    QString blockTip(Lane lane, const Block& block) const;
    int markerAt(const QPoint& pos) const;
    int blockAt(Lane lane, const QPoint& pos) const;

    void paintDetail(QPainter& p, const Geometry& g);
    void paintLane(QPainter& p, const Geometry& g, Lane lane, int top, int bottom);
    void paintMerged(QPainter& p, const Geometry& g, Lane lane, int top, int bottom);
    void paintSegments(QPainter& p, const Geometry& g);
    void paintMarkers(QPainter& p, const Geometry& g);
    void paintAxis(QPainter& p, const Geometry& g);
    void paintDensity(QPainter& p, const Geometry& g);
    void paintReadouts(QPainter& p, const Geometry& g);

    std::shared_ptr<Emulator> _emulator;
    std::shared_ptr<Emulator> _followed;  ///< follow()'s machine, shown only with the switch on
    DebugCpuId _cardCpu = DebugCpuId::Main;
    QString _client;
    bool _leased = false;
    CardPanes::LiveLine* _live = nullptr;
    int _runningReads = 0;
    int _busyReads = 0;
    int _reads = 0;
    QTimer* _retry = nullptr;  ///< a paused read the machine was busy for, again (single shot)
    int _retriesLeft = 0;

    // The last node
    bool _detail = false;
    bool _running = false;
    QString _state;
    QString _cardCpuName;
    QString _laneName[2];
    QString _readout[2];
    QString _message;
    std::vector<Block> _blocks[2];
    Next _next[2];
    std::vector<Marker> _markers;
    std::vector<Segment> _segments;
    int _segmentBlock = -1;  ///< the main block drawn in its bus cycles (-1 none)
    std::vector<Tick> _density;
    quint64 _revision = 0;
    double _gap = 0.0;
    double _nowT = 0.0;
    qint64 _nowFrame = 0;
    double _readSpan = 0.0;  ///< the ± T the last detail covers
    bool _neoGS = false;

    // The view
    double _span = kDefaultSpanT;
    double _center = 0.0;
    std::optional<QPoint> _press;
    double _pressCenter = 0.0;
    bool _dragged = false;
};

/// Whether the debugger shows the twin timeline (Debug > Show GS Timeline): a global switch of the application, off by
/// default, kept in the settings (Debugger/ShowGsTimeline). Every debugger window follows it; off, the strip is hidden,
/// nothing is read and the timeline is not armed (zero cost)
class TwinTimelineSettings : public QObject
{
    Q_OBJECT

public:
    static TwinTimelineSettings& Instance();
    bool Shown() const { return _shown; }
    /// Set and kept; `changed` when it differs
    void SetShown(bool shown);
    /// The checkable menu action over the switch (Debug > Show GS Timeline), following it
    QAction* CreateAction(QObject* parent);

signals:
    void changed(bool shown);

private:
    TwinTimelineSettings();
    bool _shown = false;
};
