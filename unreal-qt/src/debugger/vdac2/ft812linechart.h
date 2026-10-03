#pragma once

/// @file ft812linechart.h
/// @brief The FT812 Debug window's chart: one horizontal bar per screen line,
/// in the main screen's vertical scale (level with its line when docked beside
/// it), its length the line's FT812 clocks; green within the soft budget,
/// orange over it, red over the hard budget, gray not measured; the budgets as
/// vertical lines, the end of the frame in flight as a blue line with the
/// previous frame's lines faded below it (line-budget-metrics.md §3.3)

#include <QColor>
#include <QRect>
#include <QWidget>

#include <functional>

#include "debugger/vdac2/ft812linebudgetview.h"

/// The bars: rows are screen lines top to bottom, length is the line's cost
class Ft812LineChart : public QWidget
{
    Q_OBJECT

public:
    explicit Ft812LineChart(QWidget* parent = nullptr);
    void setView(Ft812LineBudget::View view);
    /// The main screen's picture: its rectangle in global coordinates and the
    /// framebuffer rows it shows (FT812 visible line n = framebuffer row n). The
    /// rows are drawn in its scale, and level with the picture when the window
    /// sits beside it; without it (or out of reach) they fill the chart's height
    using PictureGeometry = std::function<bool(QRect& global, double& firstRow, double& rows)>;
    /// Room above the first line for the budget labels: a chart placed this much
    /// above the picture and this much taller lines its rows up with the picture's
    static constexpr int kLabelHeadroom = 14;
    void setPictureGeometry(PictureGeometry geometry) { _geometry = std::move(geometry); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    QSize sizeHint() const override { return QSize(240, 600); }

private:
    /// Line n's top edge and the height of one line, in widget pixels
    void RowMapping(double& top, double& lineHeight) const;

    Ft812LineBudget::View _view;
    PictureGeometry _geometry;
};

/// The bar color of a line class (the window's legend uses the same)
QColor Ft812LineColor(Ft812LineBudget::LineClass cls);
