#pragma once

#include <QWidget>
#include <QPointer>
#include <memory>

#include "hudmodel.h"
#include "hudtheme.h"
#include "hudsnapshot.h"

class HudOverlay;
class HudOverlayGL;

/// @brief Unified wrapper for HUD overlay implementations.
/// Automatically selects GPU or software backend to match DeviceScreen.
class HudOverlayWrapper : public QObject
{
    Q_OBJECT

public:
    /// @param parent The parent widget (should be the DeviceScreen widget)
    /// @param useGPU Whether to use GPU-accelerated HUD (should match DeviceScreen)
    explicit HudOverlayWrapper(QWidget* parent, bool useGPU);
    ~HudOverlayWrapper();

    /// @brief Set the HUD model
    void setModel(std::shared_ptr<HudModel> model);

    /// @brief Get the current model
    std::shared_ptr<HudModel> model() const;

    /// @brief Set visual theme
    void setTheme(HudThemeId themeId);

    /// @brief Get current theme
    HudThemeId theme() const;

    /// @brief Set UI scale factor
    void setScaleFactor(float factor);

    /// @brief Get UI scale factor
    float scaleFactor() const;

    /// @brief Set toast position
    void setToastPosition(HudTilePosition position);

    /// @brief Get toast position
    HudTilePosition toastPosition() const;

    /// @brief Set indicator position
    void setIndicatorPosition(HudTilePosition position);

    /// @brief Get indicator position
    HudTilePosition indicatorPosition() const;

    /// @brief Sync geometry with parent widget
    void syncGeometryWithParent();

    /// @brief Show/hide the overlay
    void setVisible(bool visible);

    /// @brief Check visibility
    bool isVisible() const;

    /// @brief Get the underlying widget
    QWidget* widget() const { return _widget; }

    /// @brief Returns true if GPU-accelerated rendering is active
    bool isGPUAccelerated() const { return _useGPU; }

private:
    QPointer<QWidget> _widget;  // Guarded: the parent may destroy the widget before the wrapper
    HudOverlay* _software = nullptr;
    HudOverlayGL* _gpu = nullptr;
    bool _useGPU = false;
};
