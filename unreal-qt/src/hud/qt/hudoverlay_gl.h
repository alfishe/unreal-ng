#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QTimer>
#include <QFont>
#include <list>
#include <memory>
#include <unordered_map>

#include "hudmodel.h"
#include "hudtheme.h"

/// @brief GPU-accelerated translucent HUD overlay using OpenGL.
/// Stacks on top of DeviceScreenGL with proper alpha blending.
class HudOverlayGL : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit HudOverlayGL(QWidget* parent = nullptr);
    ~HudOverlayGL() override;

    void setModel(std::shared_ptr<HudModel> model);
    std::shared_ptr<HudModel> model() const { return _model; }

    void setTheme(HudThemeId themeId);
    HudThemeId theme() const { return _themeId; }

    void setScaleFactor(float factor);
    float scaleFactor() const { return _scaleFactor; }

    void setToastPosition(HudTilePosition position);
    HudTilePosition toastPosition() const { return _toastPosition; }

    void setIndicatorPosition(HudTilePosition position);
    HudTilePosition indicatorPosition() const { return _indicatorPosition; }

    void syncGeometryWithParent();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onModelChanged();
    void onAnimationTick();

private:
    void drawIndicatorQuad(const QRect& rect, const QColor& bgColor, const QColor& borderColor,
                           float borderRadius, float borderWidth, float alpha);
    void drawToastQuad(const QRect& rect, const QColor& bgColor, const QColor& borderColor,
                       float borderRadius, float borderWidth, float opacity, float scale);

    std::shared_ptr<HudModel> _model;
    QTimer* _animTimer = nullptr;
    HudClock::time_point _lastTick;
    bool _hasActiveAnimations = false;

    HudThemeId _themeId{HudThemeId::DarkGlass};
    float _scaleFactor{2.0f};
    HudTilePosition _toastPosition{HudTilePosition::MidBottom};
    HudTilePosition _indicatorPosition{HudTilePosition::TopRight};

    QRect _lastIndicatorBounds;
    QRect _lastToastBounds;
};
