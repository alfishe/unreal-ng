#include "rzxpopover.h"

#include <QApplication>
#include <QFileInfo>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <thread>

#include "emulator/emulator.h"
#include "emulator/rzx/rzxlauncher.h"

namespace
{
    /// A slider whose groove click jumps to the clicked frame (Qt pages by
    /// default); the handle still drags as usual
    class JumpSlider : public QSlider
    {
    public:
        using QSlider::QSlider;

    protected:
        void mousePressEvent(QMouseEvent* event) override
        {
            QStyleOptionSlider option;
            initStyleOption(&option);
            const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
            if (event->button() == Qt::LeftButton && !handle.contains(event->pos()))
            {
                const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
                const int span = groove.width() - handle.width();
                const int x = event->pos().x() - groove.x() - handle.width() / 2;
                const int value =
                    QStyle::sliderValueFromPosition(minimum(), maximum(), x, std::max(span, 1), option.upsideDown);
                setSliderPosition(value);  // the handle stands at the target at once
                emit sliderReleased();     // the owner seeks, as after a drag
                event->accept();
                return;
            }
            QSlider::mousePressEvent(event);
        }
    };
}  // namespace

RzxPopover::RzxPopover(QWidget* parent) : QFrame(parent, Qt::Tool | Qt::FramelessWindowHint)
{
    setAutoFillBackground(true);
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet("RzxPopover { border: 1px solid palette(mid); background: palette(window); }");
    hide();

    // Another application in front: the popover goes (it lives with its window)
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive)
            hide();
    });
    setMinimumWidth(420);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    _title = new QLabel(this);
    QFont titleFont = _title->font();
    titleFont.setBold(true);
    _title->setFont(titleFont);
    layout->addWidget(_title);

    auto* row = new QHBoxLayout();
    row->setSpacing(4);
    _startButton = new QPushButton(tr("|<"), this);
    _startButton->setToolTip(tr("Back to the start of the recording"));
    _startButton->setFixedWidth(fontMetrics().horizontalAdvance(QStringLiteral(" >| ")) + 10);
    _slider = new JumpSlider(Qt::Horizontal, this);
    _slider->setToolTip(tr("Drag to seek (back through keyframes, forward by playing on)"));
    _endButton = new QPushButton(tr(">|"), this);
    _endButton->setToolTip(tr("To the end of the recording"));
    _endButton->setFixedWidth(_startButton->width());
    row->addWidget(_startButton);
    row->addWidget(_slider, 1);
    row->addWidget(_endButton);
    layout->addLayout(row);

    QFont mono("Consolas", font().pointSize());
    mono.setStyleHint(QFont::Monospace);
    _position = new QLabel(this);
    _position->setFont(mono);
    layout->addWidget(_position);

    _state = new QLabel(this);
    _state->setWordWrap(true);
    layout->addWidget(_state);

    auto* bottom = new QHBoxLayout();
    _keyframes = new QLabel(this);
    _keyframes->setStyleSheet("QLabel { color: gray; }");
    _stopButton = new QPushButton(tr("Stop Playback"), this);
    _stopButton->setToolTip(tr("Stop playing; the machine continues live"));
    bottom->addWidget(_keyframes, 1);
    bottom->addWidget(_stopButton);
    layout->addLayout(bottom);

    connect(_slider, &QSlider::sliderPressed, this, [this]() { _dragging = true; });
    connect(_slider, &QSlider::sliderMoved, this, &RzxPopover::onSliderMoved);
    connect(_slider, &QSlider::sliderReleased, this, &RzxPopover::onSliderReleased);
    connect(_startButton, &QPushButton::clicked, this, &RzxPopover::onJumpStart);
    connect(_endButton, &QPushButton::clicked, this, &RzxPopover::onJumpEnd);
    connect(_stopButton, &QPushButton::clicked, this, &RzxPopover::onStop);

    _timer.setInterval(200);
    connect(&_timer, &QTimer::timeout, this, &RzxPopover::refresh);
}

void RzxPopover::popup(QWidget* anchor, std::weak_ptr<Emulator> emulator)
{
    _emulator = std::move(emulator);
    _anchor = anchor;
    refresh();
    adjustSize();
    reposition();
    show();
    raise();
}

void RzxPopover::reposition()
{
    QWidget* window = parentWidget();
    if (!window || !_anchor)
        return;
    const QPoint anchorTopRight = _anchor->mapToGlobal(QPoint(_anchor->width(), 0));
    const QRect frame = window->frameGeometry();
    const int x = std::max(frame.left() + 4, std::min(anchorTopRight.x() - width(), frame.right() - width() - 4));
    const int y = std::max(frame.top() + 4, anchorTopRight.y() - height() - 4);
    move(x, y);
}

void RzxPopover::closeUnlessActive()
{
    QWidget* active = QApplication::activeWindow();
    if (active != this && active != parentWidget())
        hide();
}

bool RzxPopover::eventFilter(QObject* watched, QEvent* event)
{
    if (!isVisible())
        return QFrame::eventFilter(watched, event);

    QWidget* window = parentWidget();
    switch (event->type())
    {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::LayoutRequest:
            if (watched == window)
                reposition();
            break;
        case QEvent::WindowStateChange:
            if (watched == window && window->isMinimized())
                hide();
            break;
        case QEvent::Hide:
            if (watched == window)
                hide();
            break;
        case QEvent::WindowDeactivate:
            // Clicking the popover deactivates the main window and the other
            // way round: close only when neither is active once it settles
            if (watched == window || watched == this)
                QTimer::singleShot(0, this, &RzxPopover::closeUnlessActive);
            break;
        case QEvent::MouseButtonPress:
        {
            // Outside the popover and its anchor (a click on the anchor toggles it)
            const QPoint global = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
            const bool inside = frameGeometry().contains(global);
            const bool onAnchor = _anchor && _anchor->rect().contains(_anchor->mapFromGlobal(global));
            if (!inside && !onAnchor)
                hide();
            break;
        }
        case QEvent::KeyPress:
            if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape)
            {
                hide();
                return true;
            }
            break;
        default:
            break;
    }
    return QFrame::eventFilter(watched, event);
}

void RzxPopover::showEvent(QShowEvent* event)
{
    QFrame::showEvent(event);
    _timer.start();
    installEventFilter(this);
    if (QWidget* window = parentWidget())
        window->installEventFilter(this);
    qApp->installEventFilter(this);
    emit visibilityChanged(true);
}

void RzxPopover::hideEvent(QHideEvent* event)
{
    _timer.stop();
    _dragging = false;
    qApp->removeEventFilter(this);
    removeEventFilter(this);
    if (QWidget* window = parentWidget())
        window->removeEventFilter(this);
    QFrame::hideEvent(event);
    emit visibilityChanged(false);
}

QString RzxPopover::FrameTime(uint64_t frame)
{
    const uint64_t tenths = frame * 10 / 50;
    return QString("%1:%2.%3")
        .arg(tenths / 600)
        .arg((tenths / 10) % 60, 2, 10, QChar('0'))
        .arg(tenths % 10);
}

void RzxPopover::refresh()
{
    if (!isVisible() && _timer.isActive())
        _timer.stop();

    std::shared_ptr<Emulator> emulator = _emulator.lock();
    const rzx::SessionStatus status = emulator ? emulator->GetRzxStatus() : rzx::SessionStatus{};
    const bool seeking = _seeking->load();
    if (!status.loaded)
    {
        _title->setText(tr("No RZX recording"));
        _slider->setEnabled(false);
        _startButton->setEnabled(false);
        _endButton->setEnabled(false);
        _stopButton->setEnabled(false);
        return;
    }

    const rzx::PlayerStatus& player = status.player;
    QString title = QFileInfo(QString::fromStdString(status.path)).fileName();
    if (!status.creator.empty())
        title += QString("  (%1)").arg(QString::fromStdString(status.creator));
    _title->setText(title);

    const bool canSeek = player.keyframes > 0 && !seeking;
    _slider->setEnabled(canSeek);
    _startButton->setEnabled(canSeek);
    _endButton->setEnabled(canSeek);
    _stopButton->setEnabled(status.active && !seeking);
    if (seeking)
    {
        // The handle stays at the target; the label shows how far the seek is
        _slider->setValue(static_cast<int>(_seekTarget));
        _position->setText(QString("frame %1 / %2   seeking to %3 (%4)")
                               .arg(player.frame)
                               .arg(player.totalFrames)
                               .arg(_seekTarget)
                               .arg(FrameTime(_seekTarget)));
    }
    else if (!_dragging)
    {
        _slider->setRange(0, static_cast<int>(player.totalFrames));
        _slider->setValue(static_cast<int>(player.frame));
        _position->setText(QString("frame %1 / %2   %3 / %4")
                               .arg(player.frame)
                               .arg(player.totalFrames)
                               .arg(FrameTime(player.frame))
                               .arg(FrameTime(player.totalFrames)));
    }

    QString state = seeking ? tr("Seeking to frame %1...").arg(_seekTarget)
                            : QString::fromLatin1(rzx::StateName(player.state));
    if (!seeking && !player.stopReason.empty())
        state += ": " + QString::fromStdString(player.stopReason);
    _state->setText(state);
    _state->setStyleSheet(player.desyncs > 0 ? "QLabel { color: #B22222; }" : "");

    _keyframes->setText(player.keyframes
                            ? tr("%1 keyframes, %2 KB (every %3 frames)")
                                  .arg(player.keyframes)
                                  .arg(player.keyframeBytes / 1024)
                                  .arg(player.keyframeInterval)
                            : tr("no keyframes: no seeking back"));
}

void RzxPopover::onSliderMoved(int value)
{
    const uint64_t total = static_cast<uint64_t>(_slider->maximum());
    _position->setText(QString("frame %1 / %2   %3 / %4")
                           .arg(value)
                           .arg(total)
                           .arg(FrameTime(static_cast<uint64_t>(value)))
                           .arg(FrameTime(total)));
}

void RzxPopover::onSliderReleased()
{
    _dragging = false;
    seekTo(static_cast<uint64_t>(_slider->value()));
}

void RzxPopover::onJumpStart()
{
    seekTo(0);
}

void RzxPopover::onJumpEnd()
{
    seekTo(static_cast<uint64_t>(_slider->maximum()));
}

void RzxPopover::onStop()
{
    if (std::shared_ptr<Emulator> emulator = _emulator.lock())
        emulator->StopRzx();
    refresh();
}

void RzxPopover::seekTo(uint64_t frame)
{
    std::shared_ptr<Emulator> emulator = _emulator.lock();
    if (!emulator || _seeking->exchange(true))
        return;

    // 1. the handle at the target, 2. controls off and the message, 3. the
    // seek on a worker thread: the GUI keeps running and shows the progress
    _seekTarget = std::min<uint64_t>(frame, static_cast<uint64_t>(_slider->maximum()));
    _slider->setValue(static_cast<int>(_seekTarget));
    refresh();

    // The emulator is held for the seek; the popover may close meanwhile
    std::shared_ptr<std::atomic<bool>> seeking = _seeking;
    QPointer<RzxPopover> self(this);
    const uint64_t target = _seekTarget;
    std::thread([emulator, target, seeking, self]() {
        emulator->SeekRzx(target);
        seeking->store(false);
        QMetaObject::invokeMethod(qApp, [self]() {
            if (self)
                self->refresh();
        });
    }).detach();
}
