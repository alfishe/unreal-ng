#pragma once

#include <QWidget>
#include <QTimer>
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

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onModelChanged();
    void onAnimationTick();

private:
    void drawWholeTileFrame(QPainter& painter, const QRect& rect, const HudTileFrameStyle& frame, float uiScale);
    void drawAugmentation(QPainter& painter, const HudElement& el, const HudSurface& surface, const HudTheme& theme, float uiScale);
    void drawIndicator(QPainter& painter, const HudElement& el, const QRect& rect, float pulseAlpha, const HudTheme& theme, float uiScale);
    void drawToast(QPainter& painter, const HudElement& el, const QRect& rect, float opacity, float scale, const HudTheme& theme, float uiScale);
    void drawIcon(QPainter& painter, const QString& iconName, const QRect& iconRect, const QColor& color, float strokeWidth = 2.0f);

    QImage getImageFromBuffer(const std::shared_ptr<const HudImageBuffer>& buf);

private:
    std::shared_ptr<HudModel> _model;
    QTimer* _animTimer = nullptr;
    HudClock::time_point _lastTick;
    bool _hasActiveAnimations = false;

    HudThemeId _themeId{HudThemeId::DarkGlass};
    float _scaleFactor{2.0f}; // HiDPI base scale (2.0x)
    HudTilePosition _toastPosition{HudTilePosition::MidBottom};
    HudTilePosition _indicatorPosition{HudTilePosition::TopRight};

    // Cache of converted QImages by buffer pointer
    std::unordered_map<const HudImageBuffer*, QImage> _imageCache;
};
