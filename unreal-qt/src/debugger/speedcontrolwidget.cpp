#include "speedcontrolwidget.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QStyle>
#include <vector>

#include "emulator/emulator.h"

// ============================================================================
// Preset collection — fully dynamic, no fixed indices needed.
// Each entry defines: label, step type, timer behavior, spinner config,
// and t-state calculation parameters (frameNum, lineNum, divisor).
//
// T-state formula: result = (F * frameNum + L * lineNum) / divisor
//   where F = config.frame, L = config.t_line
// ============================================================================
static const std::vector<PresetConfig>& getPresets()
{
    static const std::vector<PresetConfig> presets =
    {
        //  label          stepType           fixedMs  min   max   step  fN  lN  div
        { "1x (50fps)",   StepType::FullSpeed,   -1,    0,    0,    0,   0,  0,  1 },
        { "25 fps",       StepType::Frame,       40,    0,    0,    0,   1,  0,  1 },
        { "5 fps",        StepType::Frame,      200,    0,    0,    0,   1,  0,  1 },
        { "1 fps",        StepType::Frame,     1000,    0,    0,    0,   1,  0,  1 },
        { "1 frame",      StepType::Frame,        0,   10, 5000,   10,   1,  0,  1 },
        { "1/2 frame",    StepType::TStates,      0,   10, 5000,   10,   1,  0,  2 },
        { "100 lines",    StepType::TStates,      0,   10, 5000,   10,   0,100,  1 },
        { "10 lines",     StepType::TStates,      0,    1, 5000,    1,   0, 10,  1 },
        { "1 line",       StepType::TStates,      0,    1, 5000,    1,   0,  1,  1 },
        { "1/2 line",     StepType::TStates,      0,    1, 5000,    1,   0,  1,  2 },
        { "1/4 line",     StepType::TStates,      0,    1, 5000,    1,   0,  1,  4 },
        { "1/8 line",     StepType::TStates,      0,    1, 5000,    1,   0,  1,  8 },
        { "1/16 line",    StepType::TStates,      0,    1, 5000,    1,   0,  1, 16 },
        { "1 T",          StepType::TStates,      0,    1, 5000,    1,   0,  0,  1 },  // special: 1 T-state
    };

    return presets;
}

// ============================================================================
// Palette-aware stylesheet — adapts to both light and dark system themes.
// Uses palette() references so colors follow the active QPalette.
// ============================================================================
static const char* widgetStyleSheet = R"(
    SpeedControlWidget {
        font-family: "Consolas", "Monaco", "Courier New", monospace;
        font-size: 11px;
        border: 1px solid palette(mid);
        border-radius: 4px;
        margin-top: 8px;
        padding: 6px 6px 4px 6px;
    }
    SpeedControlWidget::title {
        subcontrol-origin: margin;
        subcontrol-position: top left;
        left: 8px;
        padding: 0 4px;
        color: palette(text);
    }
    QToolButton {
        background-color: palette(button);
        border: 1px solid palette(mid);
        border-radius: 3px;
        color: palette(button-text);
        padding: 2px;
        min-width: 24px;
        min-height: 24px;
        font-size: 14px;
    }
    QToolButton:hover {
        background-color: palette(light);
        border-color: palette(dark);
    }
    QToolButton:pressed {
        background-color: palette(dark);
    }
    QToolButton:checked {
        background-color: palette(highlight);
        border-color: palette(highlight);
        color: palette(highlighted-text);
    }
    QToolButton:disabled {
        color: palette(mid);
        background-color: palette(window);
        border-color: palette(mid);
    }
    QSlider::groove:horizontal {
        background: palette(mid);
        height: 4px;
        border-radius: 2px;
    }
    QSlider::handle:horizontal {
        background: palette(button);
        border: 1px solid palette(mid);
        width: 12px;
        height: 16px;
        margin: -6px 0;
        border-radius: 3px;
    }
    QSlider::handle:horizontal:hover {
        background: palette(light);
    }
    QSlider::sub-page:horizontal {
        background: palette(highlight);
        border-radius: 2px;
    }
    QLabel {
        color: palette(text);
        font-family: "Consolas", "Monaco", "Courier New", monospace;
        font-size: 12px;
        padding: 0 4px;
        border: none;
        background: transparent;
    }
    QLabel#sliderHint {
        color: palette(mid);
        font-size: 9px;
        padding: 0;
    }
    QSpinBox {
        background-color: palette(base);
        border: 1px solid palette(mid);
        border-radius: 2px;
        color: palette(text);
        padding: 1px 2px;
        font-size: 11px;
        min-width: 60px;
        max-height: 22px;
    }
    QSpinBox:disabled {
        color: palette(mid);
        background-color: palette(window);
    }
    QSpinBox::up-button, QSpinBox::down-button {
        background-color: palette(button);
        border: 1px solid palette(mid);
        width: 14px;
    }
    QSpinBox::up-button:hover, QSpinBox::down-button:hover {
        background-color: palette(light);
    }
)";

SpeedControlWidget::SpeedControlWidget(QWidget* parent)
    : QGroupBox("Speed Control", parent)
{
    setStyleSheet(widgetStyleSheet);

    const auto& presets = getPresets();

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 4);
    layout->setSpacing(4);

    // ⏮ Step Back button — hidden for now but kept in code
    _stepBackButton = new QToolButton(this);
    _stepBackButton->setText("\u23EE");  // ⏮
    _stepBackButton->setToolTip("Step back (coarser granularity)");
    _stepBackButton->setFixedSize(26, 24);
    _stepBackButton->setVisible(false);
    layout->addWidget(_stepBackButton);

    // ▶/⏸ Play/Pause toggle
    _playPauseButton = new QToolButton(this);
    _playPauseButton->setText("\u25B6");  // ▶
    _playPauseButton->setToolTip("Start/stop auto-run at selected speed");
    _playPauseButton->setCheckable(true);
    _playPauseButton->setFixedSize(26, 24);
    layout->addWidget(_playPauseButton);

    // ⏭ Step Forward button (execute one step)
    _stepForwardButton = new QToolButton(this);
    _stepForwardButton->setText("\u23ED");  // ⏭
    _stepForwardButton->setToolTip("Execute one step at current granularity");
    _stepForwardButton->setFixedSize(26, 24);
    layout->addWidget(_stepForwardButton);

    // Small separator
    layout->addSpacing(6);

    // Interval label + spinbox in a fixed-width container to avoid layout jitter
    auto* intervalContainer = new QWidget(this);
    intervalContainer->setFixedWidth(140);
    auto* intervalLayout = new QHBoxLayout(intervalContainer);
    intervalLayout->setContentsMargins(0, 0, 0, 0);
    intervalLayout->setSpacing(4);

    auto* intervalLabel = new QLabel("Interval", this);
    intervalLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    intervalLayout->addWidget(intervalLabel);

    _intervalSpinBox = new QSpinBox(this);
    _intervalSpinBox->setRange(1, 5000);
    _intervalSpinBox->setValue(1000);
    _intervalSpinBox->setSingleStep(100);
    _intervalSpinBox->setSuffix(" ms");
    _intervalSpinBox->setToolTip("Auto-run timer interval (1-5000ms)");
    _intervalSpinBox->setFixedWidth(80);
    intervalLayout->addWidget(_intervalSpinBox);

    layout->addWidget(intervalContainer);

    layout->addSpacing(6);

    // Slider area: vertical layout with slider + ticks + Fast/Fine labels
    auto* sliderArea = new QVBoxLayout();
    sliderArea->setContentsMargins(0, 0, 0, 0);
    sliderArea->setSpacing(0);

    _slider = new QSlider(Qt::Horizontal, this);
    _slider->setRange(0, static_cast<int>(presets.size()) - 1);
    _slider->setValue(0);
    _slider->setTickPosition(QSlider::NoTicks);  // We draw our own ticks
    _slider->setMinimumWidth(180);
    _slider->setToolTip("Step granularity: left = fast, right = fine");
    sliderArea->addWidget(_slider);

    // Custom tick marks — dynamically positioned based on preset count
    auto* ticks = new TickMarkWidget(static_cast<int>(presets.size()), _slider, this);
    sliderArea->addWidget(ticks);

    // Fast / Fine hint labels below ticks
    auto* hintsRow = new QHBoxLayout();
    hintsRow->setContentsMargins(2, 0, 2, 0);
    auto* fastLabel = new QLabel("Fast", this);
    fastLabel->setObjectName("sliderHint");
    fastLabel->setAlignment(Qt::AlignLeft);
    auto* fineLabel = new QLabel("Fine", this);
    fineLabel->setObjectName("sliderHint");
    fineLabel->setAlignment(Qt::AlignRight);
    hintsRow->addWidget(fastLabel);
    hintsRow->addStretch();
    hintsRow->addWidget(fineLabel);
    sliderArea->addLayout(hintsRow);

    layout->addLayout(sliderArea, 1);  // stretch factor 1

    // Preset label with T-state count
    _label = new QLabel(this);
    _label->setMinimumWidth(110);
    _label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(_label);

    // Timer for auto-run orchestration
    _timer = new QTimer(this);
    _timer->setSingleShot(false);

    // Connections
    connect(_slider, &QSlider::valueChanged, this, &SpeedControlWidget::onSliderChanged);
    connect(_playPauseButton, &QToolButton::clicked, this, &SpeedControlWidget::onPlayPauseClicked);
    connect(_stepForwardButton, &QToolButton::clicked, this, &SpeedControlWidget::onStepForwardClicked);
    connect(_stepBackButton, &QToolButton::clicked, this, &SpeedControlWidget::onStepBackwardClicked);
    connect(_timer, &QTimer::timeout, this, &SpeedControlWidget::onTimerTick);

    // When interval changes during auto-run, update the timer
    connect(_intervalSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (_timer->isActive())
        {
            _timer->setInterval(value);
        }
    });

    updateLabel();
}

void SpeedControlWidget::setEmulator(Emulator* emulator)
{
    _emulator = emulator;
    updateLabel();
}

void SpeedControlWidget::executeStep()
{
    if (!_emulator)
        return;

    int pos = _slider->value();
    const auto& presets = getPresets();
    if (pos < 0 || pos >= static_cast<int>(presets.size()))
        return;

    const PresetConfig& cfg = presets[pos];



    switch (cfg.stepType)
    {
        case StepType::FullSpeed:
            // Just resume normal execution
            if (_emulator->IsPaused())
                _emulator->Resume();
            break;

        case StepType::Frame:
            _emulator->RunFrame(true);  // skipBreakpoints = true
            break;

        case StepType::TStates:
        {
            unsigned tstates = getTStatesForPreset(pos);
            if (tstates > 0)
                _emulator->RunTStates(tstates, true);  // skipBreakpoints = true
            break;
        }
    }
}

bool SpeedControlWidget::isFullSpeed() const
{
    const auto& presets = getPresets();
    int pos = _slider->value();
    return (pos >= 0 && pos < static_cast<int>(presets.size()) &&
            presets[pos].stepType == StepType::FullSpeed);
}

bool SpeedControlWidget::isRunning() const
{
    return _timer->isActive();
}

void SpeedControlWidget::stop()
{
    if (_timer->isActive())
    {
        qDebug() << "SpeedControlWidget: Playback stopped";
        _timer->stop();
        _playPauseButton->setChecked(false);
        _playPauseButton->setText("\u25B6");  // ▶
        emit runningChanged(false);
    }
}

const std::vector<PresetConfig>& SpeedControlWidget::presets()
{
    return getPresets();
}

void SpeedControlWidget::onSliderChanged(int position)
{
    Q_UNUSED(position);
    updateLabel();

    int pos = _slider->value();
    const auto& presets = getPresets();
    if (pos < 0 || pos >= static_cast<int>(presets.size()))
        return;

    const PresetConfig& cfg = presets[pos];

    qDebug() << "SpeedControlWidget: Preset changed to" << cfg.label;

    // Configure spinner from per-preset config
    bool enableInterval = (cfg.fixedIntervalMs == 0);  // 0 = use spinner
    _intervalSpinBox->setReadOnly(!enableInterval);
    _intervalSpinBox->setEnabled(enableInterval);

    // No extra dynamic styling needed — QSpinBox:disabled in stylesheet handles contrast

    if (enableInterval)
    {
        _intervalSpinBox->setMinimum(cfg.spinnerMin);
        _intervalSpinBox->setMaximum(cfg.spinnerMax);
        _intervalSpinBox->setSingleStep(cfg.spinnerStep);
    }

    // Handle FullSpeed transitions
    bool isFullSpd = (cfg.stepType == StepType::FullSpeed);
    bool emulatorRunning = _emulator && !_emulator->IsPaused();

    if (_timer->isActive())
    {
        if (isFullSpd)
        {
            // Switching TO FullSpeed while auto-running → stop timer, resume emulator
            stop();
            if (_emulator && _emulator->IsPaused())
                _emulator->Resume();
        }
        else
        {
            // Switching between non-FullSpeed presets → just update interval
            int interval = getTimerIntervalMs(pos);
            if (interval > 0)
                _timer->setInterval(interval);
        }
    }
    else if (emulatorRunning && !isFullSpd)
    {
        // Emulator is running at full speed (no timer) and user selected a timer-driven preset.
        // Pause emulator and start timer-driven stepping.
        _emulator->Pause();

        int interval = getTimerIntervalMs(pos);
        if (interval > 0)
        {
            // Pause() is flag-based — the emulator thread must finish its current frame
            // before entering the paused state. Use a deferred start to avoid racing with
            // the emulator thread's RunFrame() on Thread 18.
            _playPauseButton->setChecked(true);
            _playPauseButton->setText("\u23F8");  // ⏸
            emit runningChanged(true);

            QTimer::singleShot(50, this, [this, interval]() {
                if (!_emulator || !_emulator->IsPaused())
                    return;  // Emulator was stopped or already resumed

                _timer->setInterval(interval);
                _timer->start();
                qDebug() << "SpeedControlWidget: Playback resumed - interval:" << interval << "ms";

                executeStep();
                emit stepped();
            });
        }
    }

    // Disable step buttons at full speed
    _stepForwardButton->setEnabled(!isFullSpd);
    _stepBackButton->setEnabled(pos > 0);
}

void SpeedControlWidget::onPlayPauseClicked()
{
    int pos = _slider->value();
    const auto& presets = getPresets();
    if (pos < 0 || pos >= static_cast<int>(presets.size()))
        return;

    const PresetConfig& cfg = presets[pos];

    if (_timer->isActive())
    {
        // Stop auto-run
        stop();
    }
    else
    {
        if (cfg.stepType == StepType::FullSpeed)
        {
            // Full speed — just resume
            if (_emulator && _emulator->IsPaused())
                _emulator->Resume();
            return;
        }

        // Start auto-run at configured interval
        int interval = getTimerIntervalMs(pos);
        if (interval > 0)
        {
            _timer->setInterval(interval);
            _timer->start();
            qDebug() << "SpeedControlWidget: Playback started - interval:" << interval << "ms";
            _playPauseButton->setChecked(true);
            _playPauseButton->setText("\u23F8");  // ⏸
            emit runningChanged(true);

            // Execute first step immediately
            executeStep();
            emit stepped();
        }
    }
}

void SpeedControlWidget::onStepForwardClicked()
{
    qDebug() << "SpeedControlWidget::onStepForwardClicked()";

    // If auto-running, stop first
    if (_timer->isActive())
    {
        stop();
    }

    executeStep();
    emit stepped();
}

void SpeedControlWidget::onStepBackwardClicked()
{
    // Move slider to coarser granularity
    int pos = _slider->value();
    if (pos > 0)
    {
        _slider->setValue(pos - 1);
    }
}

void SpeedControlWidget::onTimerTick()
{
    if (!_emulator)
        return;


    executeStep();
    emit stepped();
}

void SpeedControlWidget::updateLabel()
{
    int pos = _slider->value();
    const auto& presets = getPresets();
    if (pos < 0 || pos >= static_cast<int>(presets.size()))
        return;

    const PresetConfig& cfg = presets[pos];
    QString text = cfg.label;

    // Append computed T-state count for sub-frame presets
    if (_emulator && cfg.stepType == StepType::TStates)
    {
        unsigned tstates = getTStatesForPreset(pos);
        text += QString(" (%1T)").arg(tstates);
    }
    else if (_emulator && cfg.stepType == StepType::Frame)
    {
        auto* ctx = _emulator->GetContext();
        unsigned F = ctx->config.frame;
        text += QString(" (%1T)").arg(F);
    }

    _label->setText(text);
}

unsigned SpeedControlWidget::getTStatesForPreset(int position) const
{
    if (!_emulator)
        return 0;

    const auto& presets = getPresets();
    if (position < 0 || position >= static_cast<int>(presets.size()))
        return 0;

    const PresetConfig& cfg = presets[position];

    // Special case: 1 T-state (when both frameNum and lineNum are 0)
    if (cfg.frameNum == 0 && cfg.lineNum == 0)
        return 1;

    auto* ctx = _emulator->GetContext();
    unsigned F = ctx->config.frame;    // t-states per frame
    unsigned L = ctx->config.t_line;   // t-states per scanline

    return (F * cfg.frameNum + L * cfg.lineNum) / cfg.divisor;
}

int SpeedControlWidget::getTimerIntervalMs(int position) const
{
    const auto& presets = getPresets();
    if (position < 0 || position >= static_cast<int>(presets.size()))
        return 0;

    const PresetConfig& cfg = presets[position];
    if (cfg.fixedIntervalMs < 0)
        return 0;  // No timer (full speed)
    if (cfg.fixedIntervalMs > 0)
        return cfg.fixedIntervalMs;  // Fixed interval
    return _intervalSpinBox->value();  // User-adjustable
}
