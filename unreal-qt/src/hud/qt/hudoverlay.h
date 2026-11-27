#pragma once

#include <QWidget>
#include <QTimer>
#include <QPixmap>
#include <list>
#include <memory>
#include <unordered_map>

#include "hudmodel.h"
#include "hudsurface.h"
#include "hudtheme.h"

class DeviceScreen;

/// @brief Translucent on-screen HUD overlay widget rendered on top of DeviceScreen.
/// Fully mouse-transparent (clicks pass through to emulator screen).
class HudOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit HudOverlay(QWidget* parent = nullptr);
    ~HudOverlay() override;

    /// @brief Set the active HUD model to render
    void setModel(std::shared_ptr<HudModel> model);

    /// @brief Return the currently bound HUD model
    std::shared_ptr<HudModel> model() const { return _model; }

    /// @brief Set active visual theme
    void setTheme(HudThemeId themeId);

    /// @brief Return the active visual theme
    HudThemeId theme() const { return _themeId; }

    /// @brief Set base HiDPI UI scale multiplier (default 2.0x)
    void setScaleFactor(float factor);

    /// @brief Return the base HiDPI UI scale multiplier
    float scaleFactor() const { return _scaleFactor; }

    /// @brief Set predefined position for toasts
    void setToastPosition(HudTilePosition position);
    HudTilePosition toastPosition() const { return _toastPosition; }

    /// @brief Set predefined position for indicators
    void setIndicatorPosition(HudTilePosition position);
    HudTilePosition indicatorPosition() const { return _indicatorPosition; }

    /// @brief Synchronize overlay geometry with parent widget
    void syncGeometryWithParent();

    /// @brief Direct HUD rendering method for both software paintEvent and GPU paintGL
    ///
    /// A HUD that does not move with time (indicators and picture elements; no toast, no blinking recording
    /// indicator) is drawn once per change into a layer (prepareLayer) and blitted from it on every paint: the screen
    /// repaints the HUD with every frame, and an automation session keeps its indicator on screen for as long as it
    /// lasts. What moves with time is drawn directly on each paint.
    void renderHUD(QPainter& painter, int width, int height, float dpr);

    /// @brief The HUD for a surface of @p width x @p height (logical) at @p dpr does not move with time: its layer is
    /// up to date (drawn now if the model, the size or the look changed). False: it is drawn on each paint.
    bool prepareLayer(int width, int height, float dpr);

    /// @brief Changes whenever the layer is drawn again: a presenter that uploads the HUD keeps its upload while the
    /// serial and the size stay (prepareLayer true)
    quint64 layerSerial() const { return _layerSerial; }

    /// @brief The layer holds no pixels: nothing to draw (prepareLayer true)
    bool layerEmpty() const { return _layer.isNull(); }

    /// @brief Associate target window (e.g. QOpenGLWindow) to notify when HUD needs repainting
    void setTargetWindow(QWindow* targetWindow) { _targetWindow = targetWindow; }

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onModelChanged();
    void onAnimationTick();

private:
    friend class HudOverlay_Test;  // draws directly, to hold the layer's pixels against

    /// Every element of @p snap, drawn now (the layer's contents, or a HUD that moves with time)
    void drawElements(QPainter& painter, const HudSnapshot& snap, int width, int height, float dpr);
    /// Toasts (enter / exit animations) and blinking recording indicators change with time alone
    static bool MovesWithTime(const HudSnapshot& snap);
    static QRect tileDirtyBounds(const QRect& rect, float uiScale);
    void drawWholeTileFrame(QPainter& painter, const QRect& rect, const HudTileFrameStyle& frame, float uiScale);
    void drawAugmentation(QPainter& painter, const HudElement& el, const HudSurface& surface, const HudTheme& theme, float uiScale);
    void drawIndicator(QPainter& painter, const HudElement& el, const QRect& rect, float pulseAlpha, const HudTheme& theme, float uiScale);
    void drawToast(QPainter& painter, const HudElement& el, const QRect& rect, float opacity, float scale, const HudTheme& theme, float uiScale);
    void drawIcon(QPainter& painter, const QString& iconName, const QRect& iconRect, const QColor& color, float strokeWidth = 2.0f);

    QImage getImageFromBuffer(const std::shared_ptr<const HudImageBuffer>& buf);

private:
    std::shared_ptr<HudModel> _model;
    QTimer* _animTimer = nullptr;
    QWindow* _targetWindow = nullptr;
    HudClock::time_point _lastTick;
    bool _hasActiveAnimations = false;
    bool _hasBlinkingIndicators = false;  // Recording indicators need periodic repaint

    HudThemeId _themeId{HudThemeId::DarkGlass};
    float _scaleFactor{2.0f}; // HiDPI base scale (2.0x)
    HudTilePosition _toastPosition{HudTilePosition::MidBottom};
    HudTilePosition _indicatorPosition{HudTilePosition::TopRight};

    // Converted QImages by buffer: each entry holds its buffer, so a freed buffer's address cannot come back as a key
    // for another picture while the entry lives; bounded (kImageCacheMaxSize)
    struct CachedImage
    {
        std::shared_ptr<const HudImageBuffer> buffer;
        QImage image;
    };
    static constexpr size_t kImageCacheMaxSize = 32;
    std::unordered_map<const HudImageBuffer*, CachedImage> _imageCache;

    // The HUD drawn once per change (renderHUD): the pixels within _layerRect (device pixels of the surface), for the
    // snapshot and the surface below; the snapshot is held, so its address is not another's while the layer lives
    struct LayerKey
    {
        int width = 0;
        int height = 0;
        float dpr = 0.0f;
        HudThemeId themeId{HudThemeId::DarkGlass};
        float scaleFactor = 0.0f;
        HudTilePosition toastPosition{HudTilePosition::MidBottom};
        HudTilePosition indicatorPosition{HudTilePosition::TopRight};

        bool operator==(const LayerKey& other) const
        {
            return width == other.width && height == other.height && dpr == other.dpr &&
                   themeId == other.themeId && scaleFactor == other.scaleFactor &&
                   toastPosition == other.toastPosition && indicatorPosition == other.indicatorPosition;
        }
    };
    std::shared_ptr<const HudSnapshot> _layerSnapshot;
    LayerKey _layerKey;
    QImage _layer;
    QRect _layerRect;
    quint64 _layerSerial = 0;

    // Dirty region tracking - last known bounds of rendered elements
    QRect _lastIndicatorBounds;
    QRect _lastToastBounds;

    // LRU cache for pre-rendered tile frame backgrounds
    struct TileFrameCacheKey
    {
        int width;
        int height;
        uint32_t backgroundColor;
        uint32_t backgroundGradientEnd;
        uint32_t borderColor;
        uint32_t shadowColor;
        float borderRadius;
        float borderWidth;
        float shadowBlur;
        bool glassEffect;

        bool operator==(const TileFrameCacheKey& other) const
        {
            return width == other.width && height == other.height &&
                   backgroundColor == other.backgroundColor &&
                   backgroundGradientEnd == other.backgroundGradientEnd &&
                   borderColor == other.borderColor && shadowColor == other.shadowColor &&
                   borderRadius == other.borderRadius && borderWidth == other.borderWidth &&
                   shadowBlur == other.shadowBlur && glassEffect == other.glassEffect;
        }
    };

    struct TileFrameCacheKeyHash
    {
        size_t operator()(const TileFrameCacheKey& k) const
        {
            size_t h = std::hash<int>{}(k.width);
            h ^= std::hash<int>{}(k.height) << 1;
            h ^= std::hash<uint32_t>{}(k.backgroundColor) << 2;
            h ^= std::hash<uint32_t>{}(k.borderColor) << 3;
            h ^= std::hash<uint32_t>{}(k.shadowColor) << 4;
            return h;
        }
    };

    static constexpr size_t kTileFrameCacheMaxSize = 32;
    std::list<TileFrameCacheKey> _tileFrameLruOrder;
    std::unordered_map<TileFrameCacheKey, std::pair<QPixmap, std::list<TileFrameCacheKey>::iterator>, TileFrameCacheKeyHash> _tileFrameCache;

    QPixmap getCachedTileFrame(const QSize& size, const HudTileFrameStyle& frame, float uiScale);
    void renderTileFrameToPixmap(QPixmap& pixmap, const HudTileFrameStyle& frame, float uiScale);
};
