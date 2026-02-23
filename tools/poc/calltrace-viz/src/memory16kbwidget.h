#pragma once

#include <QColor>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QWidget>

#include <vector>

#include "syntheticdata.h"

/// Theme-aware color pair for a single visualization overlay channel.
/// \c dark is the additive glow color (dark theme);
/// \c light is the subtractive ink color (light theme).
struct OverlayColor {
    QColor dark;
    QColor light;
    [[nodiscard]] const QColor& forTheme(bool isDark) const noexcept
    { return isDark ? dark : light; }
};

/// Named palette of all overlay channel colors for both themes.
/// Obtain the shared, lazily-initialized instance via OverlayPalette::defaults().
/// ALL color definitions live exclusively in OverlayPalette::defaults() (memory16kbwidget.cpp).
/// No color literals should appear anywhere else — add a field here and define it there.
struct OverlayPalette {
    // ── Access overlays (Read / Write / Execute) ─────────────────────────
    OverlayColor read;    ///< Read  counter heat — blue  (dark) / warm-amber (light)
    OverlayColor write;   ///< Write counter heat — red   (dark) / cyan       (light)
    OverlayColor exec;    ///< Exec  counter heat — green (both themes)

    // ── Control-flow overlays ─────────────────────────────────────────────
    OverlayColor cfHeat;  ///< CF density heatmap — gold (dark) / dark-orange (light)
    OverlayColor cfSrc;   ///< CF source cells    — orange (dark) / blue   (light)
    OverlayColor cfTgt;   ///< CF target cells    — cyan   (dark) / red    (light)

    // ── Region classification tints (index = RegionType enum value 0-9) ──
    // region[0] is REG_UNKNOWN and is always transparent in both themes.
    OverlayColor region[10];

    /// Returns the application-wide default overlay palette, initialized
    /// once on first call (Meyer's singleton).  All color literals live here.
    static const OverlayPalette& defaults();
};

/// Standalone memory visualization widget for prototyping.
/// Renders a 16KB bank as a 128×128 pixel grid with multiple overlay options.
/// Palettes and rendering approach ported from the Python (PySide6/numpy/scipy) prototype.
class Memory16KBWidget : public QWidget
{
    Q_OBJECT

public:
    explicit Memory16KBWidget(int bankIndex, QWidget* parent = nullptr);
    ~Memory16KBWidget() override = default;

    void setData(const SyntheticData* data);
    void setBankIndex(int bankIndex);

    void setShowReadOverlay(bool show);
    void setShowWriteOverlay(bool show);
    void setShowExecuteOverlay(bool show);
    void setShowOpcodeTraceOverlay(bool show);
    void setShowEntropyOverlay(bool show);
    void setShowFreshnessOverlay(bool show);
    void setShowRegionOverlay(bool show);
    void setHideValues(bool hide);
    void setDarkTheme(bool dark);
    void setGlowSigma(float sigma);

    // CF overlays — individual toggles (matching Python prototype)
    void setShowCFHeatmap(bool show);
    void setShowCFSources(bool show);
    void setShowCFTargets(bool show);
    void setShowCFArcs(bool show);

    void refresh();

signals:
    void addressHovered(int bankIndex, int offset, const QString& info);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void renderToPixmap();
    void updateHoverOverlay();
    QString buildTooltipText(int offset) const;
    void buildLUTs();   // Rebuild all theme-dependent 256-entry color LUTs
    void renderBaseCells(QPainter& p, float cellW, float cellH);
    void renderGridLines(QPainter& p, float cellW, float cellH, int totalW, int totalH);
    void renderRWXOverlay(QPainter& p, float cellW, float cellH);
    void renderCFGlow(QPainter& p, float cellW, float cellH, int totalW, int totalH);
    void renderFlowArcs(QPainter& p, float cellW, float cellH);
    void renderOpcodeTrace(QPainter& p, float cellW, float cellH);
    void renderEntropyOverlay(QPainter& p, float cellW, float cellH);
    void renderFreshnessOverlay(QPainter& p, float cellW, float cellH);
    void renderRegionOverlay(QPainter& p, float cellW, float cellH);
    void updateCounterLabels();
    int mapMouseToOffset(const QPoint& pos) const;

    /// Returns true when the currently-loaded bank carries ZX Spectrum screen data
    /// (RAM5=bankIndex 1, RAM7 if/when bank-paging is extended).
    bool isScreenBank() const;

    /// Decodes the bank as a 256×192 ZX Spectrum bitmap+attribute frame and
    /// draws it aspect-ratio-correct inside the region that covers the first
    /// 6912 bytes (54 rows × 128 cols) of the grid.
    /// @param gridSide  pixel side length of the square 128×128 grid
    /// @param cellSize  size in pixels of one cell (= gridSide / GRID_W)
    void renderScreenView(QPainter& p, int gridSide, float cellSize);

    const SyntheticData* _data = nullptr;
    int _bankIndex = 0;

    QLabel* _titleLabel = nullptr;
    QWidget* _gridContainer = nullptr;
    QLabel* _imageLabel = nullptr;
    QLabel* _countersLabel = nullptr;
    QTimer* _resizeTimer = nullptr;

    // Overlay visibility
    bool _showReadOverlay = true;
    bool _showWriteOverlay = true;
    bool _showExecuteOverlay = true;
    bool _showOpcodeTraceOverlay = false;
    bool _showEntropyOverlay = false;
    bool _showFreshnessOverlay = false;
    bool _showRegionOverlay = false;
    bool _hideValues = false;
    bool _darkTheme = true;

    // CF overlays — individual booleans (from Python prototype)
    bool _showCFHeatmap = true;
    bool _showCFSources = false;
    bool _showCFTargets = false;
    bool _showCFArcs = true;

    // Glow parameters
    float _glowSigma = 1.8f;   // Gaussian blur sigma (Python default)

    QPixmap _basePixmap;
    QRect _imageRect;
    int _hoveredOffset = -1;

    QString _lastTitle;

    static constexpr int GRID_W = 128;
    static constexpr int GRID_H = 128;
    static constexpr int BANK_SIZE = 16384;

    // --- Color palette constants (from Python prototype) ---

    // Dark theme base rendering
    static constexpr float DARK_GAMMA   = 0.55f;   // Expands midtones
    static constexpr float BASE_DIM     = 0.65f;   // Max base brightness (0xFF → ~166)
    static constexpr float BASE_TINT_R  = 0.70f;   // Blue-gray phosphor tint
    static constexpr float BASE_TINT_G  = 0.78f;
    static constexpr float BASE_TINT_B  = 1.00f;

    // Light theme base rendering
    static constexpr float LIGHT_GAMMA  = 0.65f;
    static constexpr float LIGHT_DIM    = 0.78f;
    static constexpr float LIGHT_BG_R   = 240.0f;   // Match #f0f0f4 window background
    static constexpr float LIGHT_BG_G   = 240.0f;
    static constexpr float LIGHT_BG_B   = 244.0f;
    static constexpr float LIGHT_TINT_R = 0.85f;   // Warm gray ink
    static constexpr float LIGHT_TINT_G = 0.80f;
    static constexpr float LIGHT_TINT_B = 0.70f;

    // Overlay colors — see OverlayPalette::defaults() defined above the class.

    static constexpr float OVERLAY_PEAK = 255.0f;
    static constexpr float CF_BOOST     = 1.5f;

    // Arc rendering constants (from Python prototype)
    static constexpr int   ARC_TOP_N     = 18;
    static constexpr int   ARC_ALPHA_MAX = 220;
    static constexpr int   ARC_ALPHA_MIN = 55;
    static constexpr float ARC_W_MAX     = 2.2f;
    static constexpr float ARC_W_MIN     = 0.7f;
    static constexpr float ARC_LIFT      = 52.0f;
    static constexpr float DOT_R_MAX     = 3.5f;
    static constexpr float DOT_R_MIN     = 1.2f;

    // ── Screen view toggle ───────────────────────────────────────────────
    /// Eye button shown only on screen-capable banks; floats over grid top-right.
    QPushButton* _eyeButton        = nullptr;
    /// Opacity slider for the ZX screen overlay (5-100%), visible only when eye is on.
    QSlider*     _opacitySlider    = nullptr;
    /// Current screen-view opacity (0.05 – 1.0).
    float        _screenOpacity    = 1.0f;
    /// True while the screen view (ZX Spectrum bitmap decoder) is active.
    bool         _showScreenView   = false;

    // ── Precomputed 256-entry color LUTs ────────────────────────────────
    // Base cell LUTs (opaque QRgb, theme-independent after buildLUTs()):
    std::array<QRgb, 256> _baseLutDark{};
    std::array<QRgb, 256> _baseLutLight{};
    // Per-tone overlays (alpha included):
    std::array<QRgb, 256> _entropyLut{};   // rebuilt on theme change
    std::array<QRgb, 256> _opcodeLut{};    // rebuilt on theme change
    std::array<QRgb, 10>  _regionColorLut{}; // 10 region types, rebuilt on theme change
};
