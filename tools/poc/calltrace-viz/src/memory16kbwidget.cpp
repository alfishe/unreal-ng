#include "memory16kbwidget.h"

#include <QApplication>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// Log-scale a counter value into 0.0-1.0 range
static inline float logScale(uint32_t count, float maxLog = 16.0f)
{
    if (count == 0) return 0.0f;
    return std::min(1.0f, std::log2f(static_cast<float>(count) + 1.0f) / maxLog);
}

// ── LUT helpers ──────────────────────────────────────────────────────────────

// Build a 256-entry ARGB LUT for one overlay channel.
// Index i represents normalised brightness i/255.
// dark: additive glow using col.dark;  !dark: col.light dimmed × 0.80.
// boost: CF_BOOST (1.5) for CF channels;  alphaMax: 220 RWX / 240 CF.
static void buildOverlayLUT(QRgb lut[256], const OverlayColor& col,
                             bool dark, float boost = 1.0f, int alphaMax = 220)
{
    const QColor& c   = col.forTheme(dark);
    const float scale = dark ? 1.0f : 0.80f;
    const float colR  = c.redF()   * scale;
    const float colG  = c.greenF() * scale;
    const float colB  = c.blueF()  * scale;

    lut[0] = qRgba(0, 0, 0, 0);
    for (int i = 1; i < 256; i++)
    {
        const float val    = i / 255.0f;
        const float intens = val * 255.0f * boost;
        const int alpha    = std::min(static_cast<int>(val * alphaMax), alphaMax);
        lut[i] = qRgba(std::min(static_cast<int>(intens * colR), 255),
                        std::min(static_cast<int>(intens * colG), 255),
                        std::min(static_cast<int>(intens * colB), 255),
                        alpha);
    }
}

// Software Plus blend (saturating add of R,G,B,A components)
static inline QRgb plusBlend(QRgb dst, QRgb src)
{
    return qRgba(std::min(qRed(dst)   + qRed(src),   255),
                 std::min(qGreen(dst) + qGreen(src), 255),
                 std::min(qBlue(dst)  + qBlue(src),  255),
                 std::min(qAlpha(dst) + qAlpha(src), 255));
}

// Returns the eye icon for the given theme from Qt resources.
static inline QIcon eyeIcon(bool dark)
{
    return QIcon(dark ? QStringLiteral(":/eye_dark.svg") : QStringLiteral(":/eye_light.svg"));
}

// ── OverlayPalette::defaults() ───────────────────────────────────────────────
// THE single source of truth for every color used in the visualization.
// Format for each OverlayColor: { darkThemeColor, lightThemeColor }.
// Dark  theme: colors are used in CompositionMode_Plus (additive glow).
// Light theme: colors are used in CompositionMode_SourceOver (ink tint).
// Alpha in region[] entries controls tint opacity (65/255 ≈ 25%).
const OverlayPalette& OverlayPalette::defaults()
{
    static const OverlayPalette p {
        // ── RWX access overlays ───────────────────────────────────────────
        .read   = { QColor::fromRgbF(0.30f, 0.55f, 1.00f),   // dark : vivid blue
                    QColor::fromRgbF(0.30f, 0.55f, 1.00f) },  // light: same blue (theme-neutral)
        .write  = { QColor::fromRgbF(1.00f, 0.27f, 0.24f),   // dark : vivid red
                    QColor::fromRgbF(1.00f, 0.27f, 0.24f) },  // light: same red  (theme-neutral)
        .exec   = { QColor::fromRgbF(0.22f, 0.90f, 0.39f),   // dark : vivid green
                    QColor::fromRgbF(0.22f, 0.90f, 0.39f) },  // light: same green (theme-neutral)

        // ── Control-flow overlays ─────────────────────────────────────────
        .cfHeat = { QColor::fromRgbF(1.00f, 0.67f, 0.08f),   // dark : gold/amber
                    QColor::fromRgbF(0.80f, 0.40f, 0.00f) },  // light: dark orange
        .cfSrc  = { QColor::fromRgbF(1.00f, 0.43f, 0.04f),   // dark : orange
                    QColor::fromRgbF(0.00f, 0.57f, 0.96f) },  // light: blue
        .cfTgt  = { QColor::fromRgbF(0.12f, 0.86f, 0.86f),   // dark : cyan
                    QColor::fromRgbF(0.88f, 0.14f, 0.14f) },  // light: red

        // ── Region classification tints (index = RegionType enum 0-9) ────
        // Each pair: { vivid glow tint for dark BG, saturated ink tint for light BG }.
        // Alpha 65 (≈ 25%) keeps the tint subtle so base cell values remain readable.
        .region = {
            { QColor(  0,   0,   0,  0), QColor(  0,   0,   0,  0) },  // [0] REG_UNKNOWN  — transparent
            { QColor( 30, 180, 160, 65), QColor(  0, 120, 100, 65) },  // [1] REG_CODE      — teal
            { QColor(140,  80, 200, 65), QColor(100,  20, 160, 65) },  // [2] REG_DATA      — violet
            { QColor(220,  60, 180, 65), QColor(180,   0, 130, 65) },  // [3] REG_SPRITE    — magenta
            { QColor(220, 180,  30, 65), QColor(160, 110,   0, 65) },  // [4] REG_MUSIC     — amber
            { QColor( 80, 200,  80, 65), QColor( 10, 140,  10, 65) },  // [5] REG_CALC      — green
            { QColor(220,  60,  60, 65), QColor(180,   0,   0, 65) },  // [6] REG_STACK     — red
            { QColor(100, 120, 200, 65), QColor( 40,  60, 160, 65) },  // [7] REG_SYSVARS   — steel blue
            { QColor(200, 120, 180, 65), QColor(150,  40, 110, 65) },  // [8] REG_SCREEN    — rose
            { QColor(180, 140,  60, 65), QColor(120,  70,   0, 65) },  // [9] REG_IO_BUFFER — brown
        },
    };
    return p;
}

// ── SquareLabel ─────────────────────────────────────────────────────────────
// QLabel subclass that tells the Qt layout system its height must equal its
// width.  When used as the memory grid display area this guarantees the grid
// is always square with xOff = yOff = 0 — no dynamic margin patching needed.
namespace {
class SquareLabel : public QLabel {
public:
    explicit SquareLabel(QWidget* parent = nullptr) : QLabel(parent) {}
    bool hasHeightForWidth() const override { return true; }
    int  heightForWidth(int w) const override { return w; }
    QSize sizeHint() const override {
        const int s = qMax(minimumWidth(), 200);
        return { s, s };
    }
    QSize minimumSizeHint() const override {
        const int s = minimumWidth();
        return { s, s };
    }
};
} // anonymous namespace

Memory16KBWidget::Memory16KBWidget(int bankIndex, QWidget* parent)
    : QWidget(parent), _bankIndex(bankIndex)
{
    // ── Outer layout ─────────────────────────────────────────────────────────
    // A single HBox row that centres _gridContainer horizontally with elastic
    // spacers.  No vertical stretches: Memory16KBWidget takes only the height
    // it naturally needs so the parent grid controls inter-widget spacing.
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto* centreRow = new QHBoxLayout;
    centreRow->setContentsMargins(0, 0, 0, 0);
    centreRow->addStretch(1);

    // ── Grid container: title row + square grid, always same width ───────────
    // resizeEvent() calls _gridContainer->setFixedWidth(side) whenever the
    // widget is resized, which guarantees title and grid share the same width
    // at all times without any runtime margin patching.
    _gridContainer = new QWidget(this);
    _gridContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto* gridLayout = new QVBoxLayout(_gridContainer);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(2);

    // ── Title row ─────────────────────────────────────────────────────────────
    auto* titleRow = new QWidget(_gridContainer);
    titleRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* titleRowLayout = new QHBoxLayout(titleRow);
    titleRowLayout->setContentsMargins(0, 0, 0, 0);
    titleRowLayout->setSpacing(2);

    _titleLabel = new QLabel(titleRow);
    _titleLabel->setAlignment(Qt::AlignCenter);
    _titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    _titleLabel->setStyleSheet(
        "font-weight: bold; font-size: 11px; padding: 2px;"
        "color: #ffaa1e; font-family: 'Menlo', 'Consolas', monospace;");
    titleRowLayout->addWidget(_titleLabel, 1);

    // ── Opacity slider (appears when the eye toggle is on) ────────────────────
    _opacitySlider = new QSlider(Qt::Horizontal, titleRow);
    _opacitySlider->setRange(5, 100);     // 5% – 100%
    _opacitySlider->setValue(100);
    _opacitySlider->setFixedWidth(60);
    _opacitySlider->setFixedHeight(14);
    _opacitySlider->setVisible(false);    // hidden until eye is toggled on
    _opacitySlider->setToolTip("Screen view opacity");
    _opacitySlider->setCursor(Qt::PointingHandCursor);
    _opacitySlider->setStyleSheet(
        "QSlider::groove:horizontal { background: rgba(60,60,80,180); height: 4px;"
        "    border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #8899cc; width: 8px; height: 12px;"
        "    margin: -4px 0; border-radius: 3px; }"
        "QSlider::handle:horizontal:hover { background: #aabbee; }");
    titleRowLayout->addWidget(_opacitySlider);

    // ── Eye toggle button (right side of the title row) ───────────────────────
    _eyeButton = new QPushButton(titleRow);
    _eyeButton->setFixedSize(26, 18);
    _eyeButton->setCheckable(true);
    _eyeButton->setFlat(true);   // suppress native macOS button chrome
    _eyeButton->setVisible(isScreenBank());
    _eyeButton->setToolTip("Toggle ZX Spectrum screen view");
    _eyeButton->setCursor(Qt::PointingHandCursor);
    _eyeButton->setIcon(eyeIcon(_darkTheme));
    _eyeButton->setIconSize(QSize(20, 13));
    _eyeButton->setStyleSheet(_darkTheme
        ? "QPushButton { background: transparent;"
          "              border: 1px solid transparent; border-radius: 3px; padding: 0px; }"
          "QPushButton:hover   { background: rgba(80,80,120,120);"
          "                      border-color: #777799; }"
          "QPushButton:pressed { background: rgba(30,50,180,220);"
          "                      border-color: #4455dd; }"
          "QPushButton:checked { background: rgba(40,70,220,200);"
          "                      border-color: #5566ff; }"
        : "QPushButton { background: transparent;"
          "              border: 1px solid transparent; border-radius: 3px; padding: 0px; }"
          "QPushButton:hover   { background: rgba(60,130,180,80);"
          "                      border-color: #88bbdd; }"
          "QPushButton:pressed { background: rgba(40,120,170,180);"
          "                      border-color: #226688; }"
          "QPushButton:checked { background: rgba(50,150,200,140);"
          "                      border-color: #55aacc; }");
    titleRowLayout->addWidget(_eyeButton);
    gridLayout->addWidget(titleRow);

    connect(_eyeButton, &QPushButton::toggled, this, [this](bool on) {
        _showScreenView = on;
        if (_opacitySlider) {
            _opacitySlider->setVisible(on && isScreenBank());
            if (on) {
                _opacitySlider->setValue(100);
                _screenOpacity = 1.0f;
            }
        }
        refresh();
    });

    connect(_opacitySlider, &QSlider::valueChanged, this, [this](int val) {
        _screenOpacity = val / 100.0f;
        refresh();
    });

    // ── Grid display area (always square) ────────────────────────────────────
    // SquareLabel.hasHeightForWidth() returns true, heightForWidth(w) returns w.
    // resizeEvent() sets _gridContainer->setFixedWidth(side) so the label
    // always gets exactly side×side pixels and the title row matches.
    _imageLabel = new SquareLabel(_gridContainer);
    _imageLabel->setMinimumSize(200, 200);
    _imageLabel->setMouseTracking(true);
    _imageLabel->installEventFilter(this);
    _imageLabel->setAlignment(Qt::AlignCenter);
    _imageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    gridLayout->addWidget(_imageLabel);

    _countersLabel = new QLabel(_gridContainer);
    _countersLabel->setAlignment(Qt::AlignCenter);
    _countersLabel->setStyleSheet(
        "font-family: 'Menlo', 'Consolas', monospace; font-size: 10px; padding: 2px;");
    gridLayout->addWidget(_countersLabel);

    centreRow->addWidget(_gridContainer);
    centreRow->addStretch(1);
    outerLayout->addLayout(centreRow);

    // Resize debounce timer — avoids re-rendering on every pixel during resize drag
    _resizeTimer = new QTimer(this);
    _resizeTimer->setSingleShot(true);
    _resizeTimer->setInterval(50);
    connect(_resizeTimer, &QTimer::timeout, this, [this]() { refresh(); });

    setLayout(outerLayout);
    setMinimumSize(250, 280);
    setMouseTracking(true);

    buildLUTs();
}

// --- Setters ---

void Memory16KBWidget::setData(const SyntheticData* data) { _data = data; refresh(); }
void Memory16KBWidget::setBankIndex(int bankIndex)
{
    if (_bankIndex != bankIndex) {
        _bankIndex = bankIndex;
        // Reset screen view whenever the bank page changes — stale screen data
        // from a different bank should not persist in the toggled-on state.
        _showScreenView = false;
        if (_eyeButton) _eyeButton->setChecked(false);
    }
    if (_eyeButton) _eyeButton->setVisible(isScreenBank());
    refresh();
}
void Memory16KBWidget::setShowReadOverlay(bool show)     { _showReadOverlay = show; refresh(); }
void Memory16KBWidget::setShowWriteOverlay(bool show)    { _showWriteOverlay = show; refresh(); }
void Memory16KBWidget::setShowExecuteOverlay(bool show)  { _showExecuteOverlay = show; refresh(); }
void Memory16KBWidget::setShowOpcodeTraceOverlay(bool s) { _showOpcodeTraceOverlay = s; refresh(); }
void Memory16KBWidget::setShowEntropyOverlay(bool show)  { _showEntropyOverlay = show; refresh(); }
void Memory16KBWidget::setShowFreshnessOverlay(bool show){ _showFreshnessOverlay = show; refresh(); }
void Memory16KBWidget::setShowRegionOverlay(bool show)   { _showRegionOverlay = show; refresh(); }
void Memory16KBWidget::setHideValues(bool hide)          { _hideValues = hide; refresh(); }
void Memory16KBWidget::setShowCFHeatmap(bool show)       { _showCFHeatmap = show; refresh(); }
void Memory16KBWidget::setShowCFSources(bool show)       { _showCFSources = show; refresh(); }
void Memory16KBWidget::setShowCFTargets(bool show)       { _showCFTargets = show; refresh(); }
void Memory16KBWidget::setShowCFArcs(bool show)          { _showCFArcs = show; refresh(); }
void Memory16KBWidget::setGlowSigma(float sigma)         { _glowSigma = sigma; refresh(); }

void Memory16KBWidget::setDarkTheme(bool dark)
{
    _darkTheme = dark;
    _titleLabel->setStyleSheet(dark
        ? "font-weight: bold; font-size: 11px; padding: 2px; color: #ffaa1e; font-family: 'Menlo', 'Consolas', monospace;"
        : "font-weight: bold; font-size: 11px; padding: 2px; color: #335577; font-family: 'Menlo', 'Consolas', monospace;");
    // Re-skin eye button icon for the new theme
    if (_eyeButton) {
        _eyeButton->setIcon(eyeIcon(dark));
        if (dark)
            _eyeButton->setStyleSheet(
                "QPushButton { background: transparent;"
                "              border: 1px solid transparent; border-radius: 3px; padding: 0px; }"
                "QPushButton:hover   { background: rgba(80,80,120,120);"
                "                      border-color: #777799; }"
                "QPushButton:pressed { background: rgba(30,50,180,220);"
                "                      border-color: #4455dd; }"
                "QPushButton:checked { background: rgba(40,70,220,200);"
                "                      border-color: #5566ff; }");
        else
            _eyeButton->setStyleSheet(
                "QPushButton { background: transparent;"
                "              border: 1px solid transparent; border-radius: 3px; padding: 0px; }"
                "QPushButton:hover   { background: rgba(60,130,180,80);"
                "                      border-color: #88bbdd; }"
                "QPushButton:pressed { background: rgba(40,120,170,180);"
                "                      border-color: #226688; }"
                "QPushButton:checked { background: rgba(50,150,200,140);"
                "                      border-color: #55aacc; }");
    }
    buildLUTs();
    refresh();
}

// ── buildLUTs ─────────────────────────────────────────────────────────────────
// Builds every 256-entry QRgb table that does not depend on per-frame counter
// maxima.  Must be called after theme change or construction.
// Per-render LUTs (freshness, per-channel RWX/CF) are built inside their
// respective render functions because they depend on the channel's max value or
// on currentFrame.
void Memory16KBWidget::buildLUTs()
{
    // ── Base cell LUT (dark) ─────────────────────────────────────────────────
    // base = pow(v/255, DARK_GAMMA) * BASE_DIM * 255
    // then R*BASE_TINT_R, G*BASE_TINT_G, B*BASE_TINT_B  (opaque)
    for (int v = 0; v < 256; v++)
    {
        const float base = std::pow(v / 255.0f, DARK_GAMMA) * BASE_DIM * 255.0f;
        _baseLutDark[v] = qRgb(
            std::min(static_cast<int>(base * BASE_TINT_R), 255),
            std::min(static_cast<int>(base * BASE_TINT_G), 255),
            std::min(static_cast<int>(base * BASE_TINT_B), 255));
    }

    // ── Base cell LUT (light) ────────────────────────────────────────────────
    // ink = pow(v/255, LIGHT_GAMMA) * LIGHT_DIM * 255
    // R = LIGHT_BG - ink * LIGHT_TINT  (opaque)
    for (int v = 0; v < 256; v++)
    {
        const float ink = std::pow(v / 255.0f, LIGHT_GAMMA) * LIGHT_DIM * 255.0f;
        _baseLutLight[v] = qRgb(
            std::clamp(static_cast<int>(LIGHT_BG_R - ink * LIGHT_TINT_R), 0, 255),
            std::clamp(static_cast<int>(LIGHT_BG_G - ink * LIGHT_TINT_G), 0, 255),
            std::clamp(static_cast<int>(LIGHT_BG_B - ink * LIGHT_TINT_B), 0, 255));
    }

    // ── Entropy LUT ──────────────────────────────────────────────────────────
    // Index i = floor(entropy/8 * 255).  Viridis-inspired tri-linear ramp.
    {
        const int alphaScale = _darkTheme ? 160 : 120;
        _entropyLut[0] = qRgba(0, 0, 0, 0);
        for (int i = 1; i < 256; i++)
        {
            const float e = i / 255.0f;          // normalised entropy [0..1] (represents e/8)
            if (i < 13) { _entropyLut[i] = qRgba(0,0,0,0); continue; }  // skip e<0.05
            float er, eg, eb;
            if (e < 0.33f)
            {
                const float t = e / 0.33f;
                er = 50*(1-t)+30*t;  eg = 20*(1-t)+150*t;  eb = 140*(1-t)+140*t;
            }
            else if (e < 0.66f)
            {
                const float t = (e-0.33f)/0.33f;
                er = 30*(1-t)+120*t;  eg = 150*(1-t)+210*t;  eb = 140*(1-t)+40*t;
            }
            else
            {
                const float t = (e-0.66f)/0.34f;
                er = 120*(1-t)+250*t;  eg = 210*(1-t)+240*t;  eb = 40*(1-t)+10*t;
            }
            _entropyLut[i] = qRgba(
                std::clamp(static_cast<int>(er), 0, 255),
                std::clamp(static_cast<int>(eg), 0, 255),
                std::clamp(static_cast<int>(eb), 0, 255),
                std::min(static_cast<int>(e * alphaScale), 255));
        }
    }

    // ── Opcode trace LUT ─────────────────────────────────────────────────────
    // Dark:  Plus mode, bright purple/magenta additive glow.
    // Light: SourceOver, deep indigo/violet with strong alpha for contrast.
    _opcodeLut[0] = qRgba(0, 0, 0, 0);
    for (int i = 1; i < 256; i++)
    {
        const float t = i / 255.0f;
        if (t < 0.1f) { _opcodeLut[i] = qRgba(0,0,0,0); continue; }
        if (_darkTheme)
            _opcodeLut[i] = qRgba(180, 40, 220, std::min(static_cast<int>(t * 200), 255));
        else
            _opcodeLut[i] = qRgba(90, 0, 170, std::min(static_cast<int>(t * 220), 255));
    }

    // ── Region color LUT ─────────────────────────────────────────────────────
    // Colors come exclusively from OverlayPalette::defaults().region[].
    // No literals here — edit the palette definition to change region colors.
    {
        const OverlayPalette& pal = OverlayPalette::defaults();
        for (int i = 0; i < 10; i++)
            _regionColorLut[i] = pal.region[i].forTheme(_darkTheme).rgba();
    }
}

void Memory16KBWidget::refresh()
{
    if (!_data) return;
    renderToPixmap();
    updateCounterLabels();
    update();
}

// --- Paint/Resize ---

void Memory16KBWidget::paintEvent(QPaintEvent* event) { QWidget::paintEvent(event); }

void Memory16KBWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // Constrain _gridContainer to a square that fits within this widget.
    // Without this, the title row would span the full widget width while the
    // rendered grid is height-constrained and narrower — making the eye button
    // appear beyond the grid's right edge.
    if (_gridContainer && _titleLabel && _countersLabel) {
        const int titleH    = _titleLabel->sizeHint().height();
        const int countersH = _countersLabel->sizeHint().height();
        const int spacing   = 4;  // 2 internal gaps × 2px
        const int available = height() - titleH - countersH - spacing;
        const int side      = qBound(200, qMin(width(), qMax(0, available)), width());
        _gridContainer->setFixedWidth(side);
    }

    // Debounce: restart timer on each resize event, render only when stable
    _resizeTimer->start();
}

// ============================================================================
// QPainter-based rendering pipeline
// ============================================================================

void Memory16KBWidget::renderToPixmap()
{
    if (!_data || _imageLabel->width() < 10 || _imageLabel->height() < 10)
        return;

    int bank = _bankIndex;

    // Title
    const char* bankNames[] = {
        "Bank 0 (0x0000) \u2014 ROM 48K", "Bank 1 (0x4000) \u2014 RAM 5",
        "Bank 2 (0x8000) \u2014 RAM 2",   "Bank 3 (0xC000) \u2014 RAM 0"};
    QString title = bankNames[bank];
    if (title != _lastTitle) { _titleLabel->setText(title); _lastTitle = title; }

    // Determine render size (square, fit inside the label)
    int labelW = _imageLabel->width();
    int labelH = _imageLabel->height();
    int gridSide = std::min(labelW, labelH);
    if (gridSide < 50) return;

    float cellW = static_cast<float>(gridSide) / GRID_W;
    float cellH = static_cast<float>(gridSide) / GRID_H;

    // SquareLabel guarantees labelW == labelH == gridSide, so xOff = yOff = 0.
    // Keep the arithmetic for safety in case geometry is briefly mismatched.
    int xOff = (labelW - gridSide) / 2;
    int yOff = (labelH - gridSide) / 2;
    _imageRect = QRect(xOff, yOff, gridSide, gridSide);

    // Create pixmap at label size — Python: BG_DEEP = (14, 14, 18), Light: (240,240,244)
    QPixmap pixmap(labelW, labelH);
    pixmap.fill(_darkTheme ? QColor(14, 14, 18) : QColor(240, 240, 244));

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Translate so (0,0) is grid top-left
    p.translate(xOff, yOff);

    // 1. Base cells (gamma-corrected phosphor tint)
    renderBaseCells(p, cellW, cellH);

    // ── ZX Spectrum screen view overlay ──────────────────────────────────────
    // When the eye toggle is active, decode bank bytes 0-6911 as a ZX Spectrum
    // bitmap+attribute frame and paint the result over the matching cell rows
    // (rows 0-53).  The rest of the bank (rows 54-127) keeps its normal look,
    // and all overlays (RWX, freshness, …) continue to apply on top.
    if (_showScreenView && isScreenBank())
        renderScreenView(p, gridSide, cellW);

    // 2. Overlays (underneath grid lines)
    if (_showRegionOverlay)
        renderRegionOverlay(p, cellW, cellH);
    if (_showReadOverlay || _showWriteOverlay || _showExecuteOverlay)
        renderRWXOverlay(p, cellW, cellH);
    if (_showEntropyOverlay)
        renderEntropyOverlay(p, cellW, cellH);
    if (_showFreshnessOverlay)
        renderFreshnessOverlay(p, cellW, cellH);
    if (_showOpcodeTraceOverlay)
        renderOpcodeTrace(p, cellW, cellH);

    // 3. Grid lines (on top of base+overlays, under glow)
    renderGridLines(p, cellW, cellH, gridSide, gridSide);

    // Clip everything to grid area
    p.setClipRect(QRectF(0, 0, gridSide, gridSide));

    // 4. CF glow effects (individual toggles)
    if (_showCFHeatmap || _showCFSources || _showCFTargets)
        renderCFGlow(p, cellW, cellH, gridSide, gridSide);

    // 5. Flow arcs
    if (_showCFArcs)
        renderFlowArcs(p, cellW, cellH);

    p.end();

    // Cache the base render (no hover highlight)
    _basePixmap = pixmap;

    // Composite hover overlay on top
    updateHoverOverlay();
}

// --- Lightweight hover overlay: copies cached base, draws highlight ---
void Memory16KBWidget::updateHoverOverlay()
{
    if (_basePixmap.isNull())
        return;

    QPixmap display = _basePixmap;

    if (_hoveredOffset >= 0 && _hoveredOffset < BANK_SIZE && !_imageRect.isEmpty())
    {
        int gridSide = _imageRect.width();
        float cellW = static_cast<float>(gridSide) / GRID_W;
        float cellH = static_cast<float>(gridSide) / GRID_H;
        int xOff = _imageRect.x();
        int yOff = _imageRect.y();

        int gx = _hoveredOffset % GRID_W;
        int gy = _hoveredOffset / GRID_W;

        QPainter p(&display);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.translate(xOff, yOff);

        QRectF cellRect(gx * cellW, gy * cellH, cellW, cellH);
        // Python: dark = white pen, white brush; light = black pen, black brush
        QPen hoverPen(_darkTheme ? QColor(255, 255, 255, 210) : QColor(0, 0, 0, 200), 1.6);
        p.setPen(hoverPen);
        p.setBrush(_darkTheme ? QColor(255, 255, 255, 25) : QColor(0, 0, 0, 20));
        p.drawRect(cellRect.adjusted(0.3, 0.3, -0.3, -0.3));
        p.end();
    }

    _imageLabel->setPixmap(display);
}

// ── renderBaseCells ───────────────────────────────────────────────────────────
// Fills a GRID_W×GRID_H QImage using the precomputed base LUT, then scales it
// to the grid area with a single drawImage — no per-cell pow() at paint time.
void Memory16KBWidget::renderBaseCells(QPainter& p, float cellW, float cellH)
{
    int bank = _bankIndex;
    const uint8_t* mem = nullptr;
    switch (bank)
    {
        case 0: mem = _data->bank0_rom.data(); break;
        case 1: mem = _data->bank1_ram.data(); break;
        case 2: mem = _data->bank2_ram.data(); break;
        case 3: mem = _data->bank3_ram.data(); break;
        default: return;
    }

    QImage img(GRID_W, GRID_H, QImage::Format_RGB32);

    if (_hideValues)
    {
        img.fill(_darkTheme ? QColor(22, 22, 28) : QColor(235, 235, 240));
    }
    else
    {
        const QRgb* lut = _darkTheme ? _baseLutDark.data() : _baseLutLight.data();
        uint32_t* px = reinterpret_cast<uint32_t*>(img.bits());
        for (int i = 0; i < BANK_SIZE; i++)
            px[i] = lut[mem[i]];
    }

    // One scaled drawImage replaces BANK_SIZE fillRect() calls
    p.drawImage(QRectF(0, 0, GRID_W * cellW, GRID_H * cellH),
                img, QRectF(0, 0, GRID_W, GRID_H));
}

// --- Grid lines ---
void Memory16KBWidget::renderGridLines(QPainter& p, float cellW, float cellH, int totalW, int totalH)
{
    // Only draw grid lines if cells are large enough to see them
    if (cellW < 2.5f) return;

    // Python: dark = white alpha 18, light = black alpha 20 (very subtle)
    // We keep slightly more visible for the grid-based PoC
    QColor lineColor = _darkTheme ? QColor(255, 255, 255, 22) : QColor(0, 0, 0, 22);
    QColor majorLine = _darkTheme ? QColor(255, 255, 255, 50) : QColor(0, 0, 0, 45);

    p.setRenderHint(QPainter::Antialiasing, false);

    // Minor grid: every cell
    if (cellW >= 4.0f)
    {
        QPen minorPen(lineColor, 0.5);
        p.setPen(minorPen);
        for (int gx = 1; gx < GRID_W; gx++)
        {
            if (gx % 16 == 0) continue;  // Skip major positions
            float x = gx * cellW;
            p.drawLine(QPointF(x, 0), QPointF(x, totalH));
        }
        for (int gy = 1; gy < GRID_H; gy++)
        {
            if (gy % 16 == 0) continue;
            float y = gy * cellH;
            p.drawLine(QPointF(0, y), QPointF(totalW, y));
        }
    }

    // Major grid: every 16 cells (256 bytes = one row of text)
    QPen majorPen(majorLine, 1.2);
    p.setPen(majorPen);
    for (int gx = 16; gx < GRID_W; gx += 16)
    {
        float x = gx * cellW;
        p.drawLine(QPointF(x, 0), QPointF(x, totalH));
    }
    for (int gy = 16; gy < GRID_H; gy += 16)
    {
        float y = gy * cellH;
        p.drawLine(QPointF(0, y), QPointF(totalW, y));
    }

    // Border — Python: dark (80,80,100,180), light (140,140,160,200)
    // Inset by 0.5px so the 1px stroke stays fully within the pixmap bounds
    // (a rect edge at exactly totalW/H puts half the pen outside → bottom/right clipped).
    QPen borderPen(_darkTheme ? QColor(80, 80, 100, 180) : QColor(140, 140, 160, 200), 1.0);
    p.setPen(borderPen);
    p.drawRect(QRectF(0.5, 0.5, totalW - 1.0, totalH - 1.0));

    p.setRenderHint(QPainter::Antialiasing, true);
}

// ── renderRWXOverlay ──────────────────────────────────────────────────────────
// Flat (no blur) log-normalised per-cell color.  All enabled channels are
// Plus-blended in software into one ARGB QImage → single drawImage call.
// Per-channel: 33-entry bit-length LUT (integer log2 ≈ no float in inner loop).
void Memory16KBWidget::renderRWXOverlay(QPainter& p, float cellW, float cellH)
{
    int bank = _bankIndex;
    const auto& readCnt  = _data->readCounters[bank];
    const auto& writeCnt = _data->writeCounters[bank];
    const auto& execCnt  = _data->execCounters[bank];

    QImage img(GRID_W, GRID_H, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    uint32_t* px = reinterpret_cast<uint32_t*>(img.bits());

    const OverlayPalette& pal = OverlayPalette::defaults();
    auto addChannel = [&](const std::array<uint32_t, BANK_SIZE>& counters,
                          const OverlayColor& col)
    {
        uint32_t maxVal = 1;
        for (int i = 0; i < BANK_SIZE; i++)
            if (counters[i] > maxVal) maxVal = counters[i];
        const int   maxBits    = 32 - __builtin_clz(maxVal);
        const float invMaxBits = 1.0f / static_cast<float>(maxBits);
        const QColor& c    = col.forTheme(_darkTheme);
        const float scale  = _darkTheme ? 1.0f : 0.80f;
        const float cr     = c.redF()   * scale;
        const float cg     = c.greenF() * scale;
        const float cb     = c.blueF()  * scale;

        // 33-entry LUT: index = bit_length(count) ∈ [1..32]
        uint32_t lut[33]{};
        for (int k = 1; k <= 32; k++)
        {
            const float t = std::min(static_cast<float>(k) * invMaxBits, 1.0f);
            const float I = t * OVERLAY_PEAK;
            lut[k] = qRgba(std::min(static_cast<int>(I * cr), 255),
                           std::min(static_cast<int>(I * cg), 255),
                           std::min(static_cast<int>(I * cb), 255),
                           static_cast<int>(t * 220));
        }

        for (int i = 0; i < BANK_SIZE; i++)
        {
            const uint32_t v = counters[i];
            if (v == 0) continue;
            px[i] = plusBlend(px[i], lut[32 - __builtin_clz(v)]);
        }
    };

    if (_showExecuteOverlay)
        addChannel(execCnt,  pal.exec);
    if (_showWriteOverlay)
        addChannel(writeCnt, pal.write);
    if (_showReadOverlay)
        addChannel(readCnt,  pal.read);

    p.setCompositionMode(_darkTheme ? QPainter::CompositionMode_Plus
                                    : QPainter::CompositionMode_SourceOver);
    p.drawImage(QRectF(0, 0, GRID_W * cellW, GRID_H * cellH),
                img, QRectF(0, 0, GRID_W, GRID_H));
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
}

// ── renderCFGlow ──────────────────────────────────────────────────────────────
// Gaussian-blurred glow for CF channels.  Blur remains the same (unavoidable);
// the output mapping now uses a 256-entry LUT → QImage → single drawImage call.
// All enabled CF channels are Plus-blended in software into one shared image.
void Memory16KBWidget::renderCFGlow(QPainter& p, float cellW, float cellH, int totalW, int totalH)
{
    int bank = _bankIndex;
    const auto& cfHeat = _data->cfHeatmap[bank];
    const auto& cfSrc  = _data->cfSources[bank];
    const auto& cfTgt  = _data->cfTargets[bank];

    // Shared float workspace (avoid repeated heap allocation per channel)
    std::vector<float> buffer(BANK_SIZE);
    std::vector<float> tmp(BANK_SIZE);
    const int W = GRID_W, H = GRID_H;
    const int blurR = std::max(1, static_cast<int>(std::round(_glowSigma)));

    // Shared ARGB accumulator image — all CF channels Plus-blended in software
    QImage img(GRID_W, GRID_H, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    uint32_t* px = reinterpret_cast<uint32_t*>(img.bits());

    const OverlayPalette& pal = OverlayPalette::defaults();
    auto addCFChannel = [&](const std::array<uint32_t, BANK_SIZE>& counters,
                             const OverlayColor& col)
    {
        // -- log-normalise ------------------------------------------------
        uint32_t maxVal = 1;
        for (int i = 0; i < BANK_SIZE; i++)
            if (counters[i] > maxVal) maxVal = counters[i];
        const float logMax = std::log2f(static_cast<float>(maxVal) + 1.0f);

        bool anyData = false;
        for (int i = 0; i < BANK_SIZE; i++)
        {
            if (counters[i] == 0) { buffer[i] = 0.0f; continue; }
            buffer[i] = std::log2f(static_cast<float>(counters[i]) + 1.0f) / logMax;
            anyData = true;
        }
        if (!anyData) return;

        // -- 3-pass box-blur (gaussian approximation) ----------------------
        for (int pass = 0; pass < 3; pass++)
        {
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                {
                    float sum = 0; int cnt = 0;
                    for (int dx = -blurR; dx <= blurR; dx++)
                    { int nx = x+dx; if (nx>=0 && nx<W) { sum += buffer[y*W+nx]; cnt++; } }
                    tmp[y*W+x] = sum / cnt;
                }
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                {
                    float sum = 0; int cnt = 0;
                    for (int dy = -blurR; dy <= blurR; dy++)
                    { int ny = y+dy; if (ny>=0 && ny<H) { sum += tmp[ny*W+x]; cnt++; } }
                    buffer[y*W+x] = sum / cnt;
                }
        }

        // -- normalize peak ------------------------------------------------
        float glowMax = *std::max_element(buffer.begin(), buffer.end());
        if (glowMax < 1e-6f) return;
        const float invMax = 1.0f / glowMax;

        // -- 256-entry output LUT ------------------------------------------
        QRgb lut[256];
        buildOverlayLUT(lut, col, _darkTheme, CF_BOOST, 240);

        // -- write pixels via LUT, Plus-blend into accumulator -------------
        for (int i = 0; i < BANK_SIZE; i++)
        {
            const float val = buffer[i] * invMax;
            if (val < 0.008f) continue;
            const int idx = std::min(static_cast<int>(val * 255.0f), 255);
            px[i] = plusBlend(px[i], lut[idx]);
        }
    };

    if (_showCFHeatmap)
        addCFChannel(cfHeat, pal.cfHeat);
    if (_showCFSources)
        addCFChannel(cfSrc,  pal.cfSrc);
    if (_showCFTargets)
        addCFChannel(cfTgt,  pal.cfTgt);

    p.setCompositionMode(_darkTheme ? QPainter::CompositionMode_Plus
                                    : QPainter::CompositionMode_SourceOver);
    p.drawImage(QRectF(0, 0, GRID_W * cellW, GRID_H * cellH),
                img, QRectF(0, 0, GRID_W, GRID_H));
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setBrush(Qt::NoBrush);
}


// --- Flow arcs: bezier curves with terminal dots (from Python prototype) ---
void Memory16KBWidget::renderFlowArcs(QPainter& p, float cellW, float cellH)
{
    int bank = _bankIndex;
    const auto& arcs = _data->flowArcs[bank];
    if (arcs.empty()) return;

    // Sort by hitCount descending, take top ARC_TOP_N
    std::vector<SyntheticData::FlowArc> topArcs(arcs.begin(), arcs.end());
    std::sort(topArcs.begin(), topArcs.end(),
              [](const auto& a, const auto& b) { return a.hitCount > b.hitCount; });
    if (static_cast<int>(topArcs.size()) > ARC_TOP_N)
        topArcs.resize(ARC_TOP_N);

    // Find max hit count for normalization
    uint32_t maxHits = 1;
    for (const auto& arc : topArcs)
        if (arc.hitCount > maxHits) maxHits = arc.hitCount;
    float logMax = std::log2f(static_cast<float>(maxHits) + 1.0f);

    // Scale arc lift to widget size
    int gridSide = _imageRect.width();
    float liftScale = gridSide > 0 ? gridSide / 400.0f : 1.0f;

    p.setRenderHint(QPainter::Antialiasing, true);

    for (const auto& arc : topArcs)
    {
        if (arc.srcOffset >= BANK_SIZE || arc.tgtOffset >= BANK_SIZE) continue;

        float w = std::log2f(static_cast<float>(arc.hitCount) + 1.0f) / logMax;
        if (w < 0.05f) continue;

        int srcX = arc.srcOffset % GRID_W;
        int srcY = arc.srcOffset / GRID_W;
        int tgtX = arc.tgtOffset % GRID_W;
        int tgtY = arc.tgtOffset / GRID_W;

        float x1 = (srcX + 0.5f) * cellW;
        float y1 = (srcY + 0.5f) * cellH;
        float x2 = (tgtX + 0.5f) * cellW;
        float y2 = (tgtY + 0.5f) * cellH;

        // Python gradient: QColor(255, 100+80*w, 20, alpha)
        int alpha = ARC_ALPHA_MIN + static_cast<int>(w * (ARC_ALPHA_MAX - ARC_ALPHA_MIN));
        QColor arcColor(255, static_cast<int>(100 + 80 * w), 20, alpha);

        float lineWidth = ARC_W_MIN + w * (ARC_W_MAX - ARC_W_MIN);

        QPen pen(arcColor, lineWidth);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);

        // Bezier: lift control point above both endpoints
        float midX = (x1 + x2) / 2.0f;
        float minY = std::min(y1, y2);
        float ctrlY = minY - ARC_LIFT * (0.4f + w * 0.6f) * liftScale;

        QPainterPath path;
        path.moveTo(x1, y1);
        path.quadTo(midX, ctrlY, x2, y2);
        p.drawPath(path);

        // Terminal dots (Python: source=orange, target=cyan)
        float dotR = DOT_R_MIN + (DOT_R_MAX - DOT_R_MIN) * w;

        // Source dot: orange
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 120, 20, std::min(alpha + 50, 255)));
        p.drawEllipse(QPointF(x1, y1), dotR, dotR);

        // Target dot: cyan (slightly smaller)
        p.setBrush(QColor(30, 220, 210, std::min(alpha + 30, 255)));
        p.drawEllipse(QPointF(x2, y2), dotR * 0.75f, dotR * 0.75f);

        p.setBrush(Qt::NoBrush);
    }
}

// --- Opcode trace: purple/magenta glow ---
// ── renderOpcodeTrace ─────────────────────────────────────────────────────────
// Base purple layer via _opcodeLut[] QImage; hotspot ellipses (t > 0.6) drawn
// separately since they are vector shapes that can't fit in a grid image.
void Memory16KBWidget::renderOpcodeTrace(QPainter& p, float cellW, float cellH)
{
    int bank = _bankIndex;
    const auto& opcExec = _data->opcodeExecCount[bank];

    uint32_t maxExec = 1;
    for (int i = 0; i < BANK_SIZE; i++)
        if (opcExec[i] > maxExec) maxExec = opcExec[i];
    const float logMax = std::log2f(static_cast<float>(maxExec) + 1.0f);

    // -- fill QImage from precomputed _opcodeLut --
    QImage img(GRID_W, GRID_H, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    uint32_t* px = reinterpret_cast<uint32_t*>(img.bits());

    for (int i = 0; i < BANK_SIZE; i++)
    {
        if (opcExec[i] == 0) continue;
        const float t = std::log2f(static_cast<float>(opcExec[i]) + 1.0f) / logMax;
        const int idx = std::min(static_cast<int>(t * 255.0f), 255);
        px[i] = _opcodeLut[idx];   // transparent if t < 0.1 (see buildLUTs)
    }

    p.setCompositionMode(_darkTheme ? QPainter::CompositionMode_Plus
                                    : QPainter::CompositionMode_SourceOver);
    p.drawImage(QRectF(0, 0, GRID_W * cellW, GRID_H * cellH),
                img, QRectF(0, 0, GRID_W, GRID_H));

    // -- hotspot ellipses (t > 0.6) — vector, drawn on top of the image --
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < BANK_SIZE; i++)
    {
        if (opcExec[i] == 0) continue;
        const float t = std::log2f(static_cast<float>(opcExec[i]) + 1.0f) / logMax;
        if (t <= 0.6f) continue;
        const int gx = i % GRID_W, gy = i / GRID_W;
        const float dotR = cellW * 0.3f;
        const QColor hotCol = _darkTheme ? QColor(220, 120, 255, std::min(static_cast<int>(t * 210), 255))
                                         : QColor(110, 0, 200, std::min(static_cast<int>(t * 230), 255));
        p.setBrush(hotCol);
        p.drawEllipse(QPointF((gx + 0.5f) * cellW, (gy + 0.5f) * cellH), dotR, dotR);
    }

    p.setBrush(Qt::NoBrush);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
}

// ── renderEntropyOverlay ──────────────────────────────────────────────────────
// Viridis-inspired ramp from precomputed _entropyLut[].
void Memory16KBWidget::renderEntropyOverlay(QPainter& p, float cellW, float cellH)
{
    int bank = _bankIndex;
    const auto& entropy = _data->entropyMap[bank];

    QImage img(GRID_W, GRID_H, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    uint32_t* px = reinterpret_cast<uint32_t*>(img.bits());

    for (int i = 0; i < BANK_SIZE; i++)
    {
        const float e = entropy[i] / 8.0f;       // normalise to [0..1]
        const int idx = static_cast<int>(e * 255.0f);
        if (idx < 13) continue;                   // e < 0.05 → skip
        px[i] = _entropyLut[std::min(idx, 255)];
    }

    p.drawImage(QRectF(0, 0, GRID_W * cellW, GRID_H * cellH),
                img, QRectF(0, 0, GRID_W, GRID_H));
}

// ── renderFreshnessOverlay ────────────────────────────────────────────────────
// Freshness depends on currentFrame → 256-entry LUT built per render call.
void Memory16KBWidget::renderFreshnessOverlay(QPainter& p, float cellW, float cellH)
{
    int bank = _bankIndex;
    const auto& fresh = _data->freshnessMap[bank];

    // Build the per-render freshness LUT (t ∈ [0..1] → RGBA)
    // Dark  (Plus):       black-red → orange → bright amber  – additive glow
    // Light (SourceOver): dark brown → burnt orange → vivid red-orange – opaque overlay
    QRgb freshnessLut[256];
    freshnessLut[0] = qRgba(0, 0, 0, 0);
    for (int i = 1; i < 256; i++)
    {
        const float t = i / 255.0f;
        float fr, fg, fb, fa;
        if (_darkTheme)
        {
            // Plus-mode: colours start dark so they add gently at low density,
            // peak at vivid amber/yellow at full freshness (never go white).
            if (t > 0.7f) {
                const float t2 = (t-0.7f)/0.3f;
                fr = 255;  fg = 140+t2*90;  fb = 20+t2*30;
            } else if (t > 0.35f) {
                const float t2 = (t-0.35f)/0.35f;
                fr = 200+t2*55;  fg = 55+t2*85;  fb = 5+t2*15;
            } else {
                const float t2 = t/0.35f;
                fr = 60+t2*140;  fg = 10+t2*45;  fb = 0;
            }
            fa = t * 210;
        }
        else
        {
            // SourceOver on light BG: deeply saturated warm tones for contrast.
            if (t > 0.65f) {
                const float t2 = (t-0.65f)/0.35f;
                fr = 210+t2*20;  fg = 55-t2*30;  fb = 0;
            } else if (t > 0.3f) {
                const float t2 = (t-0.3f)/0.35f;
                fr = 160+t2*50;  fg = 60+t2*(-5);  fb = 0;
            } else {
                const float t2 = t/0.3f;
                fr = 80+t2*80;   fg = 30+t2*30;  fb = 0;
            }
            fa = 60 + t * 175;   // minimum opacity 60/255 even at low freshness
        }
        freshnessLut[i] = qRgba(
            std::clamp(static_cast<int>(fr), 0, 255),
            std::clamp(static_cast<int>(fg), 0, 255),
            std::clamp(static_cast<int>(fb), 0, 255),
            std::min(static_cast<int>(fa), 235));
    }

    QImage img(GRID_W, GRID_H, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    uint32_t* px = reinterpret_cast<uint32_t*>(img.bits());

    const uint32_t curFrame = _data->currentFrame;
    for (int i = 0; i < BANK_SIZE; i++)
    {
        if (fresh[i] == 0) continue;
        const uint32_t age = curFrame - fresh[i];
        const float t = 1.0f - std::min(1.0f, static_cast<float>(age) / 500.0f);
        if (t < 0.05f) continue;
        px[i] = freshnessLut[std::min(static_cast<int>(t * 255.0f), 255)];
    }

    p.setCompositionMode(_darkTheme ? QPainter::CompositionMode_Plus
                                    : QPainter::CompositionMode_SourceOver);
    p.drawImage(QRectF(0, 0, GRID_W * cellW, GRID_H * cellH),
                img, QRectF(0, 0, GRID_W, GRID_H));
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
}

// ── renderRegionOverlay ───────────────────────────────────────────────────────
// Filled from _regionColorLut[] (rebuilt in buildLUTs() per theme).
// Dark: vivid glow tints; Light: saturated ink tints for pale BG contrast.
// Composition: SourceOver (default) — region is a base-level tint, not glow.
void Memory16KBWidget::renderRegionOverlay(QPainter& p, float cellW, float cellH)
{
    int bank = _bankIndex;
    const auto& region = _data->regionMap[bank];

    QImage img(GRID_W, GRID_H, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    uint32_t* px = reinterpret_cast<uint32_t*>(img.bits());

    for (int i = 0; i < BANK_SIZE; i++)
    {
        const int rtype = static_cast<int>(region[i]);
        if (rtype <= 0 || rtype >= 10) continue;
        px[i] = _regionColorLut[rtype];
    }

    p.drawImage(QRectF(0, 0, GRID_W * cellW, GRID_H * cellH),
                img, QRectF(0, 0, GRID_W, GRID_H));
}

// ── isScreenBank ─────────────────────────────────────────────────────────────
/// Returns true when the active bank holds ZX Spectrum screen data.
/// In the standard 128K mapping: RAM5 = bankIndex 1 (0x4000), RAM7 = alt-screen.
/// RAM7 will match here too as soon as bank-paging extends bankIndex assignments.
bool Memory16KBWidget::isScreenBank() const
{
    // bankIndex 1 → "Bank 1 (0x4000) — RAM 5" (primary screen bank in 48K and 128K modes)
    // bankIndex for RAM7 would be added here when bank-switching is exposed to the view.
    return _bankIndex == 1;
}

// ── renderScreenView ─────────────────────────────────────────────────────────
// Decodes the 6912-byte ZX Spectrum display file stored at the start of the bank:
//   bytes   0–6143 : pixel bitmap, 3 thirds × 8 char rows × 8 scan lines × 32 bytes
//   bytes 6144–6911: attribute grid, 24 rows × 32 cols (FLASH|BRIGHT|PAPER[2:0]|INK[2:0])
// Output: 256×192 QImage scaled aspect-correct into totalW×totalH.
void Memory16KBWidget::renderScreenView(QPainter& p, int gridSide, float cellSize)
{
    // Resolve bank memory pointer — only bankIndex 1 (RAM5) supported so far
    const uint8_t* mem = nullptr;
    switch (_bankIndex) {
        case 1: mem = _data->bank1_ram.data(); break;
        default: return;
    }

    // The ZX Spectrum display file occupies 6912 bytes = 54 rows × 128 cols.
    // Map that to a pixel rectangle at the top of the grid.
    const int   screenRows = 6912 / GRID_W;                          // 54
    const int   areaW      = gridSide;
    const int   areaH      = static_cast<int>(screenRows * cellSize); // ≈ 54/128 of gridSide

    // ── ZX Spectrum 8-colour palette (normal + bright variants) ──────────────
    // Index 0-7: normal colours; 8-15: bright (bit 3 = bright flag from attribute).
    // Bright black (8) stays black; all others saturate to 255.
    static const QRgb zxPal[16] = {
        qRgb(  0,   0,   0),  // 0  Black
        qRgb(  0,   0, 215),  // 1  Blue
        qRgb(215,   0,   0),  // 2  Red
        qRgb(215,   0, 215),  // 3  Magenta
        qRgb(  0, 215,   0),  // 4  Green
        qRgb(  0, 215, 215),  // 5  Cyan
        qRgb(215, 215,   0),  // 6  Yellow
        qRgb(215, 215, 215),  // 7  White
        qRgb(  0,   0,   0),  // 8  Bright Black (same as black)
        qRgb(  0,   0, 255),  // 9  Bright Blue
        qRgb(255,   0,   0),  // 10 Bright Red
        qRgb(255,   0, 255),  // 11 Bright Magenta
        qRgb(  0, 255,   0),  // 12 Bright Green
        qRgb(  0, 255, 255),  // 13 Bright Cyan
        qRgb(255, 255,   0),  // 14 Bright Yellow
        qRgb(255, 255, 255),  // 15 Bright White
    };

    // ── Decode 256×192 pixel image ────────────────────────────────────────────
    QImage img(256, 192, QImage::Format_RGB32);
    uint32_t* px = reinterpret_cast<uint32_t*>(img.bits());

    for (int y = 0; y < 192; y++) {
        for (int bx = 0; bx < 32; bx++) {
            // Pixel byte address — ZX Spectrum interleaved row layout:
            //   bits 12-11: y third  (y >> 6)
            //   bits 10-8 : scan line within char cell (y & 7)
            //   bits  7-5 : char row within third ((y >> 3) & 7)
            //   bits  4-0 : byte column (bx)
            const int pixAddr  = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | bx;
            const int attrAddr = 6144 + ((y >> 3) * 32) + bx;

            if (pixAddr >= 6144 || attrAddr >= 6912) continue;

            const uint8_t pixByte = mem[pixAddr];
            const uint8_t attr    = mem[attrAddr];

            // Attribute byte: [FLASH|BRIGHT|PAPER2|PAPER1|PAPER0|INK2|INK1|INK0]
            const bool bright   = (attr >> 6) & 1;
            const int  inkIdx   = (attr & 0x07)        | (bright ? 8 : 0);
            const int  paperIdx = ((attr >> 3) & 0x07) | (bright ? 8 : 0);
            // Note: FLASH (bit 7) is ignored for static display — paper is always background

            // Unpack 8 pixels from the byte, MSB = leftmost
            for (int bit = 7; bit >= 0; bit--) {
                const int  xPixel = bx * 8 + (7 - bit);
                const bool set    = (pixByte >> bit) & 1;
                px[y * 256 + xPixel] = zxPal[set ? inkIdx : paperIdx];
            }
        }
    }

    // ── Scale 256×192 to fit areaW×areaH, preserving 4:3 aspect ratio ─────────
    const float scaleX = static_cast<float>(areaW) / 256.0f;
    const float scaleY = static_cast<float>(areaH) / 192.0f;
    const float scale  = std::min(scaleX, scaleY);
    const int   dstW   = static_cast<int>(256.0f * scale);
    const int   dstH   = static_cast<int>(192.0f * scale);
    const int   dstX   = (areaW - dstW) / 2;
    const int   dstY   = (areaH - dstH) / 2;

    // Apply the user-chosen screen-view opacity (slider range 5–100%)
    p.setOpacity(static_cast<double>(_screenOpacity));

    p.drawImage(QRectF(dstX, dstY, dstW, dstH),
                img,  QRectF(0, 0, 256, 192));

    // Restore full opacity so subsequent drawing is not dimmed
    p.setOpacity(1.0);
}

// --- Counter labels ---
void Memory16KBWidget::updateCounterLabels()
{
    if (!_data) { _countersLabel->setText(""); return; }

    int bank = _bankIndex;
    const auto& s = _data->stats[bank];

    auto fmt = [](uint32_t count) -> QString {
        if (count >= 1000000) return QString("%1M").arg(count / 1000000.0, 0, 'f', 1);
        if (count >= 1000) return QString("%1K").arg(count / 1000.0, 0, 'f', 1);
        return QString::number(count);
    };

    // R=blue, W=red, X=green — identical in both themes (theme-neutral semantic colors).
    // CF uses amber (dark) / dark-orange (light) to stay readable on each background.
    QStringList parts;
    bool anyCF = _showCFHeatmap || _showCFSources || _showCFTargets;
    parts << QString("<span style='color:#4d7aff'>R:%1</span>").arg(fmt(s.totalReads))
          << QString("<span style='color:#ff4444'>W:%1</span>").arg(fmt(s.totalWrites))
          << QString("<span style='color:#33cc66'>X:%1</span>").arg(fmt(s.totalExecs));
    if (anyCF)
        parts << (_darkTheme
            ? QString("<span style='color:#ffaa1e'>CF⁺:%1</span>").arg(fmt(s.totalCFEvents))
            : QString("<span style='color:#cc6600'>CF⁺:%1</span>").arg(fmt(s.totalCFEvents)));

    _countersLabel->setTextFormat(Qt::RichText);
    _countersLabel->setText(parts.join("  "));
}

// --- Mouse mapping ---
int Memory16KBWidget::mapMouseToOffset(const QPoint& pos) const
{
    if (_imageRect.isEmpty()) return -1;

    QPoint localPos = _imageLabel->mapFrom(this, pos);
    int relX = localPos.x() - _imageRect.x();
    int relY = localPos.y() - _imageRect.y();

    if (relX < 0 || relY < 0 || relX >= _imageRect.width() || relY >= _imageRect.height())
        return -1;

    int gx = (relX * GRID_W) / _imageRect.width();
    int gy = (relY * GRID_H) / _imageRect.height();

    gx = std::clamp(gx, 0, GRID_W - 1);
    gy = std::clamp(gy, 0, GRID_H - 1);

    return gy * GRID_W + gx;
}

// --- Tooltip text builder ---
QString Memory16KBWidget::buildTooltipText(int offset) const
{
    if (!_data || offset < 0 || offset >= BANK_SIZE)
        return {};

    int bank = _bankIndex;
    uint16_t addr = bank * BANK_SIZE + offset;

    const uint8_t* mem = nullptr;
    switch (bank)
    {
        case 0: mem = _data->bank0_rom.data(); break;
        case 1: mem = _data->bank1_ram.data(); break;
        case 2: mem = _data->bank2_ram.data(); break;
        case 3: mem = _data->bank3_ram.data(); break;
    }
    uint8_t value = mem ? mem[offset] : 0;

    QString text = QString("Addr: 0x%1 (%2)\nValue: 0x%3 (%4)\n"
                           "R:%5  W:%6  X:%7")
        .arg(addr, 4, 16, QChar('0')).arg(addr)
        .arg(value, 2, 16, QChar('0')).arg(value)
        .arg(_data->readCounters[bank][offset])
        .arg(_data->writeCounters[bank][offset])
        .arg(_data->execCounters[bank][offset]);

    uint32_t cf = _data->cfHeatmap[bank][offset];
    if (cf > 0)
    {
        const char* typeNames[] = {"", "JP", "JR", "CALL", "RET", "RST", "DJNZ", "RETI"};
        auto t = _data->cfDominantType[bank][offset];
        text += QString("\nCF: %1 x%2").arg(typeNames[static_cast<int>(t)]).arg(cf);
    }

    uint32_t opc = _data->opcodeExecCount[bank][offset];
    if (opc > 0) text += QString("\nOpcode exec: %1").arg(opc);

    float ent = _data->entropyMap[bank][offset];
    if (ent > 0.01f) text += QString("\nEntropy: %1 bits").arg(ent, 0, 'f', 2);

    uint32_t fr = _data->freshnessMap[bank][offset];
    if (fr > 0)
    {
        uint32_t age = _data->currentFrame - fr;
        text += QString("\nLast write: %1 frames ago").arg(age);
    }

    auto reg = _data->regionMap[bank][offset];
    if (reg != SyntheticData::REG_UNKNOWN)
    {
        const char* regionNames[] = {"", "Code", "Data", "Sprite/Gfx", "Music/SFX",
                                      "Calc/Scratch", "Stack", "SysVars", "Screen", "I/O Buffer"};
        text += QString("\nRegion: %1").arg(regionNames[static_cast<int>(reg)]);
    }

    return text;
}

// --- Event handler (minimal — tooltip moved to eventFilter) ---
bool Memory16KBWidget::event(QEvent* e)
{
    return QWidget::event(e);
}

// --- Event filter on _imageLabel for hover tracking + tooltips ---
bool Memory16KBWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _imageLabel && _data)
    {
        if (event->type() == QEvent::MouseMove)
        {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            QPoint widgetPos = _imageLabel->mapTo(this, mouseEvent->pos());
            int offset = mapMouseToOffset(widgetPos);

            if (offset != _hoveredOffset)
            {
                _hoveredOffset = offset;
                updateHoverOverlay();
            }

            // Show tooltip immediately on mouse move
            if (offset >= 0 && offset < BANK_SIZE)
            {
                QString tip = buildTooltipText(offset);
                QToolTip::showText(mouseEvent->globalPosition().toPoint(), tip, _imageLabel);
            }
            else
            {
                QToolTip::hideText();
            }
            return false;
        }
        else if (event->type() == QEvent::Leave)
        {
            if (_hoveredOffset != -1)
            {
                _hoveredOffset = -1;
                updateHoverOverlay();
            }
            QToolTip::hideText();
            return false;
        }
    }
    return QWidget::eventFilter(watched, event);
}
