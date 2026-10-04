#include "ft812debugwindow.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platforms/tsconf/vdac2control.h"

/// region <Ft812DebugWindow>

Ft812DebugWindow::Ft812DebugWindow(QWidget* parent) : QWidget(parent)
{
    setWindowTitle(tr("FT812 Debug"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // The chart first and as tall as the window: docked beside the main window its
    // rows sit level with the screen's lines
    auto* caption = new QLabel(tr("line cost, FT812 clocks"), this);
    QFont small = caption->font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    caption->setFont(small);
    layout->addWidget(caption);
    _chart = new Ft812LineChart(this);
    layout->addWidget(_chart, 1);

    _summary = new QLabel(this);
    _summary->setWordWrap(true);
    _summary->setFont(small);
    _summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(_summary);

    auto* controls = new QHBoxLayout();
    controls->addWidget(new QLabel(tr("Soft margin:"), this));
    _margin = new QSpinBox(this);
    _margin->setRange(0, 50);
    _margin->setSuffix(tr(" %"));
    _margin->setToolTip(tr("The warning line: this many percent below the clocks a line has ([VDAC2] LineBudgetMargin). "
                           "Developers keep about 10 % free on a real card"));
    controls->addWidget(_margin);
    controls->addStretch(1);
    layout->addLayout(controls);

    _note = new QLabel(tr("One bar per screen line, in the main screen's scale. Green: within the soft budget, orange: "
                          "over it, red: over the line's clocks (broken on a real card), gray: not drawn. While open, "
                          "every frame is measured. After a time-travel seek inside a frame, the lines above the blue "
                          "line are the frame in flight, drawn so far."),
                       this);
    _note->setWordWrap(true);
    _note->setFont(small);
    layout->addWidget(_note);

    connect(_margin, &QSpinBox::valueChanged, this, [this](int value) {
        if (_loadingMargin)
            return;
        Vdac2Control::SetLineBudgetMargin(context(), static_cast<uint32_t>(value));
    });
}

Ft812DebugWindow::~Ft812DebugWindow() = default;

bool Ft812DebugWindow::Offered(EmulatorContext* context)
{
    return context && Vdac2Control::HasCard(context);
}

void Ft812DebugWindow::setBinding(EmulatorBinding* binding)
{
    if (_binding)
        disconnect(_binding, nullptr, this, nullptr);
    _binding = binding;
    if (!_binding)
        return;
    connect(_binding, &EmulatorBinding::bound, this, [this] {
        if (isVisible())
            claimMeasuring();
        refresh();
    });
    connect(_binding, &EmulatorBinding::unbound, this, [this] {
        _measuring = nullptr;  // the machine is gone with its card
        refresh();
    });
    // Paused: the frame in flight becomes readable
    connect(_binding, &EmulatorBinding::ready, this, &Ft812DebugWindow::refresh);
}

EmulatorContext* Ft812DebugWindow::context() const
{
    Emulator* emulator = _binding && _binding->isBound() ? _binding->emulator() : nullptr;
    return emulator ? emulator->GetContext() : nullptr;
}

void Ft812DebugWindow::claimMeasuring()
{
    EmulatorContext* ctx = context();
    if (!Offered(ctx) || _measuring == ctx)
        return;
    releaseMeasuring();
    Vdac2Control::FrameMetrics m;
    if (!Vdac2Control::GetFrameMetrics(ctx, m, false, false))
        return;
    _measuringWasOn = m.measureAlways;
    Vdac2Control::SetMeasureAlways(ctx, true);
    _measuring = ctx;
}

void Ft812DebugWindow::releaseMeasuring()
{
    if (_measuring && _measuring == context() && !_measuringWasOn)
        Vdac2Control::SetMeasureAlways(_measuring, false);
    _measuring = nullptr;
}

void Ft812DebugWindow::setPictureGeometry(Ft812LineChart::PictureGeometry geometry, QWidget* mainWindow)
{
    _chart->setPictureGeometry(std::move(geometry));
    if (mainWindow)
        mainWindow->installEventFilter(this);
}

bool Ft812DebugWindow::eventFilter(QObject* watched, QEvent* event)
{
    // The main window moved or resized: the picture moved, the rows follow
    if (event->type() == QEvent::Move || event->type() == QEvent::Resize)
    {
        if (event->type() == QEvent::Resize && isVisible())
            emit pictureGeometryChanged();
        _chart->update();
    }
    return QWidget::eventFilter(watched, event);
}

int Ft812DebugWindow::ChartTopInFrame() const
{
    return (geometry().top() - frameGeometry().top()) + _chart->mapTo(this, QPoint(0, 0)).y();
}

int Ft812DebugWindow::HeightAroundChart() const
{
    return height() - _chart->height();
}

void Ft812DebugWindow::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    _chart->update();
}

void Ft812DebugWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    _chart->update();
}

void Ft812DebugWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    claimMeasuring();
    refresh();
    emit visibilityChanged(true);
}

void Ft812DebugWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    releaseMeasuring();
    emit visibilityChanged(false);
}

void Ft812DebugWindow::refresh()
{
    if (!isVisible())
        return;
    EmulatorContext* ctx = context();
    Vdac2Control::FrameMetrics m;
    std::string error;
    if (!ctx || !Vdac2Control::HasCard(ctx, &error))
    {
        _summary->setText(ctx ? tr("No VDAC2 card: %1").arg(QString::fromStdString(error)) : tr("No emulator"));
        _chart->setView({});
        return;
    }
    // What the monitor shows: the metrics kept with the presented FT812 picture
    // (latched frame or TTD-composed picture). While the monitor shows the Evo,
    // the chip's last finished frame
    const bool presented = Vdac2Control::GetPresentedFrameMetrics(ctx, m);
    if (!presented)
        Vdac2Control::GetFrameMetrics(ctx, m, true, false);
    const Ft812LineBudget::View view = Ft812LineBudget::Build(m);
    QString summary = QString::fromStdString(Ft812LineBudget::Summary(m, view));
    if (!presented)
        summary += tr(" | the monitor shows the Evo: the FT812's last finished frame");
    _summary->setText(summary);
    if (!_margin->hasFocus() && _margin->value() != static_cast<int>(m.margin))
    {
        _loadingMargin = true;
        _margin->setValue(static_cast<int>(m.margin));
        _loadingMargin = false;
    }
    _chart->setView(view);
}

/// endregion
