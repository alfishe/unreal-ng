#pragma once

/// @file devicetestpanel.h
/// @brief The test panel of the controllers dialog (docs/inprogress/2026-10-17-controller-manager/ui-ux.md §3.4): what
/// the selected host device sends, as the router reads it. Per stick a plot with the radial dead zone and the direction
/// thresholds (on / off, the hysteresis) drawn, the raw position as a hollow dot and the position after the dead zone
/// and the saturation as a filled one; per trigger a bar with the click and release marks and the highest value seen
/// this session (an Elite hair-trigger stop saturates early: "full = 0.61"); a row of button lamps in the canonical
/// order. Below, the device's backend and the backends' probe line.
///
/// Fed by the dialog's 30 Hz live read (HostInputService::Live); paints only on a change. The thresholds are the
/// router's defaults (controllers::AnalogOptions).

#include <QString>
#include <QWidget>

#include <map>
#include <string>
#include <vector>

class DeviceTestPanel : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceTestPanel(QWidget* parent = nullptr);

    /// A new device: its controls (the database's `controls`, `dpad.*` / `lstick.*` as written) and what the backends
    /// say ("iohid · xbox-elite-2"; the probe line). The session's trigger maxima start over
    void setDevice(const std::string& id, const std::vector<std::string>& controls, const QString& detail);
    void setLive(const std::map<std::string, float>& live);

    /// For the tests: the lamps in drawing order with their state, a trigger's highest value this session
    std::vector<std::pair<std::string, bool>> lamps() const;
    float triggerMax(const std::string& trigger) const;
    bool hasStick(const std::string& stick) const;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    float value(const std::string& control) const;
    void paintStick(QPainter& painter, const QRectF& box, const std::string& stick, const QString& label) const;
    void paintTrigger(QPainter& painter, const QRectF& box, const std::string& trigger, const QString& label) const;

    std::string _device;
    std::vector<std::string> _sticks;    ///< "lstick", "rstick" the device has
    std::vector<std::string> _triggers;  ///< "lt", "rt"
    std::vector<std::string> _buttons;   ///< in the canonical order
    std::map<std::string, float> _live;
    std::map<std::string, float> _max;  ///< per trigger, this session
    QString _detail;
};
