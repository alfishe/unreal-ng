#include "speedcontrolwidget.h"

#include <QCoreApplication>
#include <QDebug>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QPainter>
#include <QPointer>
#include <QStyle>
#include <algorithm>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/runpacer.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

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

    const auto& presets = execution::RunPacer::Presets();

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
    _intervalSpinBox->setRange(execution::RunPacer::kIntervalMin, execution::RunPacer::kIntervalMax);
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

    // Connections
    connect(_slider, &QSlider::valueChanged, this, &SpeedControlWidget::onSliderChanged);
    connect(_playPauseButton, &QToolButton::clicked, this, &SpeedControlWidget::onPlayPauseClicked);
    connect(_stepForwardButton, &QToolButton::clicked, this, &SpeedControlWidget::onStepForwardClicked);
    connect(_stepBackButton, &QToolButton::clicked, this, &SpeedControlWidget::onStepBackwardClicked);

    // The interval: the pacer's (a running pace takes it at once)
    connect(_intervalSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (_syncing)
            return;
        StateNode params = StateNode::Object();
        params["interval_ms"] = static_cast<int64_t>(value);
        submit("run.pace_set", params);
    });

    syncFromPacer();
}

SpeedControlWidget::~SpeedControlWidget()
{
    unsubscribe();
}

void SpeedControlWidget::setEmulator(std::shared_ptr<Emulator> emulator)
{
    unsubscribe();
    _emulator = std::move(emulator);
    if (_emulator)
        subscribe();
    syncFromPacer();
}

void SpeedControlWidget::subscribe()
{
    // The observers run on the message centre's thread: they post to the GUI thread, where the widget may be gone
    QPointer<SpeedControlWidget> self(this);
    const std::string id = _emulator->GetId();
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    _paceObserver = messageCenter.AddObserver(NC_RUN_PACE, [self, id](int, Message* message) {
        auto* payload = message != nullptr ? dynamic_cast<RunPacePayload*>(message->obj) : nullptr;
        if (payload == nullptr || payload->emulatorId.toString() != id)
            return;
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self] {
                if (self)
                    self->syncFromPacer();
            },
            Qt::QueuedConnection);
    });
    // Each paced step ends with a CPU step post: the panes and the screen follow (one refresh queued at a time)
    std::shared_ptr<std::atomic<bool>> pending = _stepPending;
    _stepObserver = messageCenter.AddObserver(NC_EXECUTION_CPU_STEP, [self, id, pending](int, Message*) {
        const std::shared_ptr<execution::RunPacer> pacer = execution::RunPacer::Find(id);
        if (!pacer || !pacer->State().running || pending->exchange(true))
            return;
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, pending] {
                pending->store(false);
                if (self)
                    emit self->stepped();
            },
            Qt::QueuedConnection);
    });
}

void SpeedControlWidget::unsubscribe()
{
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    if (_paceObserver != 0)
        messageCenter.RemoveObserverById(NC_RUN_PACE, _paceObserver);
    if (_stepObserver != 0)
        messageCenter.RemoveObserverById(NC_EXECUTION_CPU_STEP, _stepObserver);
    _paceObserver = 0;
    _stepObserver = 0;
}

void SpeedControlWidget::submit(const char* action, const StateNode& params)
{
    if (!_emulator)
        return;
    actions::ActionValue value;
    value.params = params;
    actions::ActionOrigin origin;
    origin.origin = execution::Origin::Gui;
    origin.identity = "gui:speed-control";
    origin.client = "speed control";
    QPointer<SpeedControlWidget> self(this);
    actions::ActionRegistry::Instance().Submit(
        action, std::move(value), _emulator->GetId(), std::move(origin), [self](actions::ActionResult result) {
            const QString why = result.ok ? QString() : QString::fromStdString(result.message);
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [self, why] {
                    if (!self)
                        return;
                    self->_label->setToolTip(why);
                    if (!why.isEmpty())
                        qWarning() << "SpeedControlWidget:" << why;
                    self->syncFromPacer();
                },
                Qt::QueuedConnection);
        });
}

void SpeedControlWidget::syncFromPacer()
{
    const auto& presets = execution::RunPacer::Presets();
    const std::shared_ptr<execution::RunPacer> pacer =
        _emulator ? execution::RunPacer::Find(_emulator->GetId()) : nullptr;
    execution::PaceState state;
    if (pacer)
        state = pacer->State();
    else
    {
        state.preset = 0;
        state.intervalMs = 1000;
    }
    const execution::PacePreset& preset = presets[std::min(state.preset, presets.size() - 1)];

    _syncing = true;
    _slider->setValue(static_cast<int>(state.preset));
    const bool adjustable = preset.fixedIntervalMs == 0;
    _intervalSpinBox->setEnabled(adjustable);
    _intervalSpinBox->setReadOnly(!adjustable);
    if (adjustable)
        _intervalSpinBox->setSingleStep(preset.intervalStep);
    _intervalSpinBox->setValue(preset.fixedIntervalMs > 0 ? preset.fixedIntervalMs : state.intervalMs);
    _syncing = false;

    const bool fullSpeed = preset.unit == execution::PaceUnit::Full;
    _stepForwardButton->setEnabled(!fullSpeed);
    _stepBackButton->setEnabled(state.preset > 0);
    _playPauseButton->setChecked(state.running);
    _playPauseButton->setText(state.running ? "\u23F8" : "\u25B6");  // ⏸ / ▶
    updateLabel();

    if (state.running != _running)
    {
        _running = state.running;
        emit runningChanged(_running);
    }
}

void SpeedControlWidget::executeStep()
{
    submit("run.pace_step");
}

bool SpeedControlWidget::isFullSpeed() const
{
    return _slider->value() == 0;
}

bool SpeedControlWidget::isRunning() const
{
    return _running;
}

void SpeedControlWidget::stop()
{
    // At once on this thread (it only flags the pacer): a step the caller runs next does not race a tick
    if (_emulator)
        if (const std::shared_ptr<execution::RunPacer> pacer = execution::RunPacer::Find(_emulator->GetId()))
            pacer->Stop();
    syncFromPacer();
}

void SpeedControlWidget::onSliderChanged(int position)
{
    updateLabel();
    if (_syncing)
        return;
    const auto& presets = execution::RunPacer::Presets();
    if (position < 0 || position >= static_cast<int>(presets.size()))
        return;
    // The pacer acts as the slider always did: off full speed it pauses and paces, onto full speed it resumes
    StateNode params = StateNode::Object();
    params["preset"] = presets[static_cast<size_t>(position)].id;
    submit("run.pace_set", params);
}

void SpeedControlWidget::onPlayPauseClicked()
{
    // The click toggled the button: the pacer's state puts it back when it answers
    if (_running)
        submit("run.pace_stop");
    else if (!isFullSpeed() || (_emulator && _emulator->IsPaused()))
        submit("run.pace_toggle");  // paced: start; full speed and paused: continue
    else
        syncFromPacer();
}

void SpeedControlWidget::onStepForwardClicked()
{
    submit("run.pace_step");  // a running pace stops first
}

void SpeedControlWidget::onStepBackwardClicked()
{
    if (_slider->value() > 0)
        _slider->setValue(_slider->value() - 1);
}

void SpeedControlWidget::updateLabel()
{
    const auto& presets = execution::RunPacer::Presets();
    const int pos = _slider->value();
    if (pos < 0 || pos >= static_cast<int>(presets.size()))
        return;
    const execution::PacePreset& preset = presets[static_cast<size_t>(pos)];
    QString text = preset.label;
    EmulatorContext* context = _emulator ? _emulator->GetContext() : nullptr;
    if (context)
    {
        switch (preset.unit)
        {
            case execution::PaceUnit::Full: break;
            case execution::PaceUnit::Instructions: text += QString(" (4..23T)"); break;
            case execution::PaceUnit::Scanlines:
                text += QString(" (%1T)").arg(context->config.t_line * preset.count);
                break;
            case execution::PaceUnit::Frames: text += QString(" (%1T)").arg(context->config.frame); break;
        }
    }
    _label->setText(text);
}
