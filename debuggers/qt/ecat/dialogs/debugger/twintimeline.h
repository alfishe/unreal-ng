// unreal-debugger (not in ecat): the twin timeline (gui-card-debugger A5.1, C03; gs-debugger design §5.4, §7.2), a
// full-width strip under the toolbar, shown while the machine has a sound card's CPU. Both CPUs on one clock:
//
//   MAIN  │LD A,(HL)│OUT (C),A [OCF 4][OD 3][IO 4]│ ...        6.6 / 11 T · 60% · port write next
//   NOW   ─────────────────────┃───────────────────           T 34,995.6 · frame 412 · gap 0.4 T
//   CARD  │..│LD A,(IX+2)│OUT (#03),A│ JR NZ (next) │          cycle 119,985 · 12.0 MHz
//
// Paused: the recent instructions of each lane as blocks whose width is their measured duration, the current main
// instruction's bus cycles when the main CPU stands inside one, the next instructions outlined, the markers (●
// breakpoint, ⇄ host port access, INT / NMI, ◆ the NeoGS clock change) with their text as tooltips, each lane's
// readout. Running: the density per frame (the main lane the game's GS port accesses, the card lane its busy / idle
// steps and interrupts). The wheel zooms (± 60 T around NOW at first; zoomed out the blocks merge into a density bar),
// a drag pans, a double-click recenters on NOW, a click on a block shows that CPU's listing at it.
//
// One read of the core's node a time (Emulator::read_timeline): at the window's stops and, running, every
// Emulator::live_card_ms. The histories the node needs record only while a client holds the timeline's lease: the
// strip holds it while it is shown (renewed now and then), releases it when hidden, closed or offline

#pragma once

#include <string>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QRectF>
#include <QWidget>

#include "emulator/emulator.h"

class QTimer;

class TwinTimeline : public QWidget
{
    Q_OBJECT
public:
    //The default half window: ± 60 main T around NOW
    static constexpr double kDefaultSpan = 60.0;
    static constexpr double kMinSpan = 4.0;
    static constexpr double kMaxSpan = 100000.0;

    TwinTimeline(Emulator * e, QWidget *parent);
    ~TwinTimeline() override;

    //The card CPU the card lane shows: "gs", "ms" ("" the first one there)
    void set_card(const std::string &card);
    //Read the node again (the machine paused: the detail; running: the density). Nothing while hidden. Never a wait
    //on the emulation thread: a busy machine (another client holding it, a stop on its way) keeps the last picture
    //and is read again a little later (RETRY_MS, at most RETRIES times)
    void refresh();
    static constexpr int RETRY_MS = 40;
    static constexpr int RETRIES = 25;
    //Reads the machine was busy for (tried again), reads done: for the self-check
    int busy_reads() const { return m_busy_reads; }
    int reads() const { return m_reads; }
    //The lease off (the window closes, the machine goes offline); a later show takes it again
    void release();
    //The machine's connection: offline nothing is read, the lease is let go; back, it is taken again while shown
    void set_online(bool on);

    //The view: its centre (main T relative to NOW) and half width
    double center() const { return m_center; }
    double span() const { return m_span; }
    void set_view(double center, double span);
    //Zoom by wheel notches (positive: in) around the main T `at` (relative to NOW)
    void zoom_at(double at, int notches);
    void recenter();

    //What the self-check and the GUI plane read
    const QJsonObject &node() const { return m_node; }
    bool paused_detail() const { return m_detail; }
    bool armed() const { return m_armed; }
    //Blocks of the lane read ("main", "card"); markers of a kind ("" all)
    int block_count(const QString &lane) const;
    int marker_count(const QString &kind) const;
    //Density ticks read (running)
    int density_count() const { return (int)m_density.size(); }
    //The bus-cycle segments of the main instruction in progress (a card's stop inside one)
    int segment_count() const { return (int)m_segments.size(); }
    //Whether the lane merges its blocks into a density bar at this zoom
    bool merged(const QString &lane) const;
    //The point of the newest past block of the lane (its middle), for a click; invalid when none is in sight
    QPoint block_point(const QString &lane) const;
    //The NOW line's x
    int now_x() const;
    //The last read's problem ("" none)
    QString message() const { return m_message; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    //A click on a block: that CPU's listing at the address ("main", "gs", "ms")
    void block_activated(const QString &cpu, unsigned int address);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool event(QEvent *event) override;

private:
    struct Block {
        double at = 0;          //start, main T relative to NOW
        double duration = 0;    //main T
        unsigned int pc = 0;
        QString text;           //the instruction, its label before it
        QString kind;           //instruction, int, nmi, idle
        QString tip;
        bool open = false;      //the newest: up to the lane's time now
        bool next = false;      //the next instruction (outlined)
    };
    struct Marker {
        double at = 0;
        QString lane;           //"main" or the card's
        QString kind;           //breakpoint, host_port, int, nmi, clock_change
        QString text;
    };
    struct Segment {
        double at = 0;
        double duration = 0;
        QString kind;
        bool current = false;
    };
    struct Tick {
        qint64 frame = 0;
        int commands = 0, data_writes = 0, data_reads = 0, status_polls = 0;
        int steps = 0, idle_steps = 0, busy_steps = 0, interrupts = 0, nmis = 0;
    };
    //Laid out in paintEvent and kept for the tooltips and the clicks
    struct Hit {
        QRectF rect;
        QString tip;
        QString cpu;            //a block's CPU ("" a marker)
        unsigned int pc = 0;
    };

    Emulator * e;
    std::string m_card;
    QJsonObject m_node;
    bool m_detail = false;      //the last read was the paused detail
    bool m_armed = false;
    bool m_online = true;
    QString m_message;
    QString m_card_cpu;         //the card lane's CPU name ("gs", "ms")
    QString m_card_title;       //"GS Z80 · 12.0 MHz"
    std::vector<Block> m_main, m_card_blocks;
    std::vector<Marker> m_markers;
    std::vector<Segment> m_segments;
    std::vector<Tick> m_density;
    QString m_readout_main, m_readout_now, m_readout_card;
    double m_gap = 0;           //main's moment, NOW + gap
    double m_read_span = 0;     //the span of the last read

    double m_center = 0;
    double m_span = kDefaultSpan;
    int m_wheel = 0;            //wheel turns not yet a notch
    QPoint m_press;
    double m_press_center = 0;
    bool m_dragging = false;
    QTimer * m_reread;          //a zoom or a pan past what was read: read again, once it settles
    QTimer * m_retry;           //a read the machine was busy for: again
    int m_retries_left = 0;
    int m_busy_reads = 0;
    int m_reads = 0;
    void read();
    QTimer * m_renew;           //the lease renewed while shown
    std::vector<Hit> m_hits;

    void arm(bool on);
    void parse();
    //The window the read must cover: ± (|centre| + span), at least the default
    double wanted_span() const;
    //The drawing areas
    int row_height() const;
    int label_width() const;
    int readout_width() const;
    QRectF lanes_area() const;
    double x_of(double t, const QRectF &area) const;
    double t_of(double x, const QRectF &area) const;
    void paint_blocks(QPainter &p, const std::vector<Block> &blocks, const QRectF &lane, const QRectF &area,
                      const QString &cpu, const QColor &tint);
    void paint_merged(QPainter &p, const std::vector<Block> &blocks, const QRectF &lane, const QRectF &area, const QColor &tint);
    void paint_density(QPainter &p, const QRectF &main_lane, const QRectF &card_lane, const QRectF &area);
    void paint_markers(QPainter &p, const QString &lane_cpu, const QRectF &row, const QRectF &area);
    void paint_axis(QPainter &p, const QRectF &row, const QRectF &area);
    const std::vector<Block> &lane_blocks(const QString &lane) const;
};
