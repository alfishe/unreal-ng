#pragma once

/// @file ft812debugwindow.h
/// @brief Debug -> FT812 Debug: the VDAC2 card's FT812 line budget, one bar per
/// screen line against the soft and hard budget (line-budget-metrics.md §3.3,
/// line-budget-model.md).
///
/// Offered only while the active machine has the VDAC2 card (TSL-VDAC2). It
/// refreshes on the main screen's own refresh (a presented frame, a debugger
/// step, a TTD seek), never on a timer, so both always show the same moment.
/// While open the card draws and measures every frame (measure-always), shown
/// or not. A thin adapter like the automation surfaces: everything through
/// Vdac2Control.

#include <QWidget>

#include "debugger/vdac2/ft812linechart.h"

class EmulatorBinding;
class EmulatorContext;
class QLabel;
class QSpinBox;

class Ft812DebugWindow : public QWidget
{
    Q_OBJECT

public:
    explicit Ft812DebugWindow(QWidget* parent = nullptr);
    ~Ft812DebugWindow() override;

    void setBinding(EmulatorBinding* binding);
    /// The main screen's picture geometry (the chart draws in its scale), and the
    /// window whose moves and resizes move that picture
    void setPictureGeometry(Ft812LineChart::PictureGeometry geometry, QWidget* mainWindow);
    /// The active machine has the VDAC2 card (the menu item and the window exist only then)
    static bool Offered(EmulatorContext* context);

    /// Docking it level with the picture: the chart's top below the window frame's
    /// top, and the window's height outside the chart
    int ChartTopInFrame() const;
    int HeightAroundChart() const;

public slots:
    /// Read the metrics again: called on every main screen refresh
    void refresh();

signals:
    /// Visibility changed via the window's own close box (keeps the menu in sync)
    void visibilityChanged(bool visible);
    /// The main window moved or resized: the owner places the window level with the picture again
    void pictureGeometryChanged();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    EmulatorContext* context() const;
    /// measure-always on for the machine shown while the window is open, back off after
    void claimMeasuring();
    void releaseMeasuring();

    EmulatorBinding* _binding = nullptr;
    QLabel* _summary = nullptr;
    QLabel* _note = nullptr;
    QSpinBox* _margin = nullptr;
    Ft812LineChart* _chart = nullptr;
    EmulatorContext* _measuring = nullptr;  ///< the machine whose card measure-always this window turned on
    bool _measuringWasOn = false;           ///< it was on before (automation): leave it on
    bool _loadingMargin = false;
};
