#include "hudoverlaywrapper.h"
#include "hudoverlay.h"
#include "hudoverlay_gl.h"

#include <QDebug>

HudOverlayWrapper::HudOverlayWrapper(QWidget* parent, bool useGPU)
    : QObject(parent)
    , _useGPU(false)  // Always use software HUD - composites properly over QOpenGLWidget
{
    // Software HUD composites correctly over both software and GPU device screens.
    // Two stacked QOpenGLWidgets have transparency compositing issues.
    _software = new HudOverlay(parent);
    _widget = _software;
    qInfo() << "HudOverlayWrapper: Using software HUD (composites over GPU/software screens)";
}

HudOverlayWrapper::~HudOverlayWrapper()
{
    // Widgets owned by Qt parent
}

void HudOverlayWrapper::setModel(std::shared_ptr<HudModel> model)
{
    if (_useGPU && _gpu)
        _gpu->setModel(model);
    else if (_software)
        _software->setModel(model);
}

std::shared_ptr<HudModel> HudOverlayWrapper::model() const
{
    if (_useGPU && _gpu)
        return _gpu->model();
    else if (_software)
        return _software->model();
    return nullptr;
}

void HudOverlayWrapper::setTheme(HudThemeId themeId)
{
    if (_useGPU && _gpu)
        _gpu->setTheme(themeId);
    else if (_software)
        _software->setTheme(themeId);
}

HudThemeId HudOverlayWrapper::theme() const
{
    if (_useGPU && _gpu)
        return _gpu->theme();
    else if (_software)
        return _software->theme();
    return HudThemeId::DarkGlass;
}

void HudOverlayWrapper::setScaleFactor(float factor)
{
    if (_useGPU && _gpu)
        _gpu->setScaleFactor(factor);
    else if (_software)
        _software->setScaleFactor(factor);
}

float HudOverlayWrapper::scaleFactor() const
{
    if (_useGPU && _gpu)
        return _gpu->scaleFactor();
    else if (_software)
        return _software->scaleFactor();
    return 2.0f;
}

void HudOverlayWrapper::setToastPosition(HudTilePosition position)
{
    if (_useGPU && _gpu)
        _gpu->setToastPosition(position);
    else if (_software)
        _software->setToastPosition(position);
}

HudTilePosition HudOverlayWrapper::toastPosition() const
{
    if (_useGPU && _gpu)
        return _gpu->toastPosition();
    else if (_software)
        return _software->toastPosition();
    return HudTilePosition::MidBottom;
}

void HudOverlayWrapper::setIndicatorPosition(HudTilePosition position)
{
    if (_useGPU && _gpu)
        _gpu->setIndicatorPosition(position);
    else if (_software)
        _software->setIndicatorPosition(position);
}

HudTilePosition HudOverlayWrapper::indicatorPosition() const
{
    if (_useGPU && _gpu)
        return _gpu->indicatorPosition();
    else if (_software)
        return _software->indicatorPosition();
    return HudTilePosition::TopRight;
}

void HudOverlayWrapper::syncGeometryWithParent()
{
    if (_useGPU && _gpu)
        _gpu->syncGeometryWithParent();
    else if (_software)
        _software->syncGeometryWithParent();
}

void HudOverlayWrapper::setVisible(bool visible)
{
    if (_widget)
        _widget->setVisible(visible);
}

bool HudOverlayWrapper::isVisible() const
{
    return _widget ? _widget->isVisible() : false;
}
