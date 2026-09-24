#pragma push_macro("slots")
#undef slots
#include "debugger/ttd/timetravelmanager.h"
#pragma pop_macro("slots")

#include "widgets/ttdwidget.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QStyleOptionButton>
#include <algorithm>
#include <fstream>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "mainwindow.h"
#include "widgets/tintedsvgicon.h"

namespace {

class TtdStatusLabel : public QLabel
{
public:
    explicit TtdStatusLabel(const QString& text, QAbstractButton* referenceButton = nullptr, QWidget* parent = nullptr)
        : QLabel(text, parent), _referenceButton(referenceButton)
    {
    }

    void setReferenceButton(QAbstractButton* btn)
    {
        _referenceButton = btn;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setFont(font());
        painter.setPen(palette().color(foregroundRole()));

        const QFontMetrics fm = fontMetrics();
        const int leftPad = 6;
        const int rightPad = 6;
        const int availWidth = width() - leftPad - rightPad;
        if (availWidth <= 0)
            return;

        const QString elided = fm.elidedText(text(), Qt::ElideRight, availWidth);

        QRect targetRect;
        if (_referenceButton && parentWidget() && _referenceButton->parentWidget() == parentWidget()) {
            QStyleOptionButton btnOpt;
            btnOpt.initFrom(_referenceButton);
            btnOpt.rect = QRect(QPoint(0, 0), _referenceButton->size());
            btnOpt.text = _referenceButton->text();
            const QRect btnContent = _referenceButton->style()->subElementRect(
                QStyle::SE_PushButtonContents, &btnOpt, _referenceButton);

            const QRect btnContentInParent(_referenceButton->mapTo(parentWidget(), btnContent.topLeft()), btnContent.size());
            const QRect contentInSelf(mapFrom(parentWidget(), btnContentInParent.topLeft()), btnContent.size());
            targetRect = QRect(leftPad, contentInSelf.y(), availWidth, contentInSelf.height());
        } else {
            targetRect = QRect(leftPad, 0, availWidth, height());
        }

        painter.drawText(targetRect, Qt::AlignVCenter | Qt::AlignLeft, elided);
    }

private:
    QAbstractButton* _referenceButton = nullptr;
};

} // namespace

TtdWidget::TtdWidget(MainWindow* mainWindow, QWidget* parent)
    : QWidget(parent), _mainWindow(mainWindow)
{
    setObjectName(QStringLiteral("ttdWidget"));
    setAutoFillBackground(true);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 4, 6, 4);
    mainLayout->setSpacing(4);

    // =========================================================================
    // Control Row (Top)
    // =========================================================================
    QHBoxLayout* controlLayout = new QHBoxLayout();
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(6);

    _recordBtn = new QPushButton(tr("Start Rec"), this);
    _recordBtn->setIcon(tintedSvgIcon(QStringLiteral("record"), /*tint=*/false));
    _recordBtn->setToolTip(tr("Start / Stop TTD Session Recording"));
    connect(_recordBtn, &QPushButton::clicked, this, &TtdWidget::onRecordToggled);

    _loadBtn = new QPushButton(tr("Load .ttd"), this);
    _loadBtn->setToolTip(tr("Load a saved Time Travel Debugging (.ttd) file"));
    connect(_loadBtn, &QPushButton::clicked, this, &TtdWidget::onLoadSession);

    _exportBtn = new QPushButton(tr("Export .ttd"), this);
    _exportBtn->setToolTip(tr("Export current TTD timeline session to a .ttd file"));
    connect(_exportBtn, &QPushButton::clicked, this, &TtdWidget::onExportSession);

    _clearBtn = new QPushButton(tr("Clear"), this);
    _clearBtn->setToolTip(tr("Clear current timeline history and reset session"));
    connect(_clearBtn, &QPushButton::clicked, this, &TtdWidget::onClearSession);

    const int iconMetric = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    const QSize toolbarIconSize(iconMetric, iconMetric);

    _closeBtn = new QToolButton(this);
    _closeBtn->setIcon(tintedSvgIcon(QStringLiteral("close")));
    _closeBtn->setIconSize(toolbarIconSize);
    _closeBtn->setToolTip(tr("Close TTD Control Panel"));
    _closeBtn->setAutoRaise(true);
    _closeBtn->setCursor(Qt::PointingHandCursor);
    const int closeBtnDim = toolbarIconSize.height() + 4;
    _closeBtn->setFixedSize(closeBtnDim, closeBtnDim);
    _closeBtn->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "    border: none;"
        "    background: transparent;"
        "    padding: 0px;"
        "    margin: 0px;"
        "}"
        "QToolButton:hover {"
        "    background: rgba(128, 128, 128, 40);"
        "    border-radius: 3px;"
        "}"
        "QToolButton:pressed {"
        "    background: rgba(128, 128, 128, 70);"
        "}"
    ));
    connect(_closeBtn, &QToolButton::clicked, this, [this]() {
        setVisibleByUser(false);
    });

    const int row1Height = std::max(_recordBtn->sizeHint().height(), 26);
    _recordBtn->setFixedHeight(row1Height);
    _loadBtn->setFixedHeight(row1Height);
    _exportBtn->setFixedHeight(row1Height);
    _clearBtn->setFixedHeight(row1Height);

    auto* statusLabel = new TtdStatusLabel(tr("TTD: Idle"), _loadBtn, this);
    statusLabel->setFont(_recordBtn->font());
    statusLabel->setFixedHeight(row1Height);
    statusLabel->setWordWrap(false);
    statusLabel->setMinimumWidth(0);
    statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _statusLabel = statusLabel;

    controlLayout->setAlignment(Qt::AlignVCenter);
    controlLayout->addWidget(_recordBtn, 0, Qt::AlignVCenter);
    controlLayout->addWidget(_loadBtn, 0, Qt::AlignVCenter);
    controlLayout->addWidget(_exportBtn, 0, Qt::AlignVCenter);
    controlLayout->addWidget(_clearBtn, 0, Qt::AlignVCenter);
    controlLayout->addWidget(_statusLabel, 1, Qt::AlignVCenter);
    controlLayout->addWidget(_closeBtn, 0, Qt::AlignVCenter);

    _controlContainer = new QWidget(this);
    _controlContainer->setLayout(controlLayout);
    _controlContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _controlContainer->setFixedHeight(row1Height);

    // =========================================================================
    // Scrubber Row Container (Bottom)
    // Hidden while actively recording; shown when recording stops or session is loaded.
    // =========================================================================
    _scrubberContainer = new QWidget(this);
    QHBoxLayout* scrubberLayout = new QHBoxLayout(_scrubberContainer);
    scrubberLayout->setContentsMargins(0, 0, 0, 0);
    scrubberLayout->setSpacing(4);
    scrubberLayout->setAlignment(Qt::AlignVCenter);

    const int stepBtnWidth = fontMetrics().horizontalAdvance(QStringLiteral(" +1F ")) + 10;
    const int jumpBtnWidth = fontMetrics().horizontalAdvance(QStringLiteral(" >| ")) + 10;

    _jumpStartBtn = new QPushButton(tr("|<"), _scrubberContainer);
    _jumpStartBtn->setToolTip(tr("Jump to start of session recording"));
    _jumpStartBtn->setFixedWidth(jumpBtnWidth);
    connect(_jumpStartBtn, &QPushButton::clicked, this, &TtdWidget::onJumpStart);

    _stepBackBtn = new QPushButton(tr("-1F"), _scrubberContainer);
    _stepBackBtn->setToolTip(tr("Step backward 1 frame"));
    _stepBackBtn->setFixedWidth(stepBtnWidth);
    connect(_stepBackBtn, &QPushButton::clicked, this, &TtdWidget::onStepBack);

    _timelineSlider = new QSlider(Qt::Horizontal, _scrubberContainer);
    _timelineSlider->setRange(0, 0);
    _timelineSlider->setEnabled(false);
    _timelineSlider->setToolTip(tr("TTD Timeline Scrubber (drag to seek within recorded range)"));
    connect(_timelineSlider, &QSlider::valueChanged, this, &TtdWidget::onSliderValueChanged);
    connect(_timelineSlider, &QSlider::sliderMoved, this, &TtdWidget::onSliderMoved);

    _stepForwardBtn = new QPushButton(tr("+1F"), _scrubberContainer);
    _stepForwardBtn->setToolTip(tr("Step forward 1 frame"));
    _stepForwardBtn->setFixedWidth(stepBtnWidth);
    connect(_stepForwardBtn, &QPushButton::clicked, this, &TtdWidget::onStepForward);

    _jumpEndBtn = new QPushButton(tr(">|"), _scrubberContainer);
    _jumpEndBtn->setToolTip(tr("Jump to end of session recording"));
    _jumpEndBtn->setFixedWidth(jumpBtnWidth);
    connect(_jumpEndBtn, &QPushButton::clicked, this, &TtdWidget::onJumpEnd);

    _resumeFromHereBtn = new QPushButton(tr("Rec From Here"), _scrubberContainer);
    _resumeFromHereBtn->setToolTip(tr("Truncate future history and resume live recording from current position"));
    connect(_resumeFromHereBtn, &QPushButton::clicked, this, &TtdWidget::onResumeFromHere);

    const int row2Height = std::max({_jumpStartBtn->sizeHint().height(), _timelineSlider->sizeHint().height(), 26});
    _jumpStartBtn->setFixedHeight(row2Height);
    _stepBackBtn->setFixedHeight(row2Height);
    _timelineSlider->setFixedHeight(row2Height);
    _stepForwardBtn->setFixedHeight(row2Height);
    _jumpEndBtn->setFixedHeight(row2Height);
    _resumeFromHereBtn->setFixedHeight(row2Height);

    _scrubberContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _scrubberContainer->setFixedHeight(row2Height);

    scrubberLayout->addWidget(_jumpStartBtn, 0, Qt::AlignVCenter);
    scrubberLayout->addWidget(_stepBackBtn, 0, Qt::AlignVCenter);
    scrubberLayout->addWidget(_timelineSlider, 1, Qt::AlignVCenter);
    scrubberLayout->addWidget(_stepForwardBtn, 0, Qt::AlignVCenter);
    scrubberLayout->addWidget(_jumpEndBtn, 0, Qt::AlignVCenter);
    scrubberLayout->addWidget(_resumeFromHereBtn, 0, Qt::AlignVCenter);

    mainLayout->addWidget(_controlContainer);
    mainLayout->addWidget(_scrubberContainer);

    _scrubberContainer->setVisible(false);
    setFixedHeight(singleRowHeight());

    // Telemetry Update Timer (100 ms interval)
    _telemetryTimer = new QTimer(this);
    _telemetryTimer->setInterval(100);
    connect(_telemetryTimer, &QTimer::timeout, this, &TtdWidget::updateTelemetry);
}

int TtdWidget::singleRowHeight() const
{
    const int row1Height = _controlContainer ? _controlContainer->height() : 26;
    const int topMargin = layout() ? layout()->contentsMargins().top() : 4;
    const int botMargin = layout() ? layout()->contentsMargins().bottom() : 4;
    return topMargin + row1Height + botMargin;
}

int TtdWidget::doubleRowHeight() const
{
    const int row1Height = _controlContainer ? _controlContainer->height() : 26;
    const int row2Height = _scrubberContainer ? _scrubberContainer->height() : 26;
    const int topMargin = layout() ? layout()->contentsMargins().top() : 4;
    const int botMargin = layout() ? layout()->contentsMargins().bottom() : 4;
    const int spacing = layout() ? layout()->spacing() : 4;
    return topMargin + row1Height + spacing + row2Height + botMargin;
}

int TtdWidget::desiredHeight() const
{
    if (!_visibleByUser)
        return 0;
    return (_scrubberContainer && _scrubberContainer->isVisible())
        ? doubleRowHeight()
        : singleRowHeight();
}

QSize TtdWidget::sizeHint() const
{
    return QSize(QWidget::sizeHint().width(), desiredHeight());
}

QSize TtdWidget::minimumSizeHint() const
{
    return QSize(0, desiredHeight());
}

void TtdWidget::updateState(std::shared_ptr<Emulator> activeEmulator)
{
    _activeEmulator = activeEmulator;

    if (_activeEmulator && isVisible())
    {
        if (!_telemetryTimer->isActive())
        {
            _telemetryTimer->start();
        }
    }
    else
    {
        _telemetryTimer->stop();
    }

    updateTelemetry();
}

void TtdWidget::setVisibleByUser(bool visible)
{
    if (_visibleByUser == visible)
        return;

    _visibleByUser = visible;
    setVisible(visible);
    emit visibilityChanged(visible);

    if ((visible || _lastIsRecording) && _activeEmulator)
    {
        if (!_telemetryTimer->isActive())
        {
            _telemetryTimer->start();
        }
        updateTelemetry();
    }
    else
    {
        _telemetryTimer->stop();
    }

    if (visible)
    {
        setFixedHeight(desiredHeight());
    }

    emit heightChanged();
}

void TtdWidget::updateTelemetry()
{
    if (!_activeEmulator)
    {
        _recordBtn->setEnabled(false);
        _recordBtn->setText(tr("Start Rec"));
        _loadBtn->setEnabled(false);
        _exportBtn->setEnabled(false);
        _clearBtn->setEnabled(false);
        _statusLabel->setText(tr("TTD: No Active Emulator"));
        _scrubberContainer->setVisible(false);
        return;
    }

    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !context->pTimeTravelManager)
    {
        _statusLabel->setText(tr("TTD: Unavailable"));
        _scrubberContainer->setVisible(false);
        return;
    }

    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;
    ttd::TTDSessionInfo info = ttd->GetSessionInfo();
    ttd::TTDTimePoint currentPos = ttd->CurrentPosition();

    _loadBtn->setEnabled(true);

    const bool isRecording = (info.state == ttd::TTDSessionState::Recording);
    if (_lastIsRecording != isRecording)
    {
        _lastIsRecording = isRecording;
        emit recordingStateChanged(isRecording);
        if (!isVisible() && !_lastIsRecording)
        {
            _telemetryTimer->stop();
        }
    }
    const bool isDetached = (info.state == ttd::TTDSessionState::Detached);
    const bool hasHistory = (info.checkpointCount > 0);

    _recordBtn->setEnabled(true);
    _recordBtn->setText(isRecording ? tr("Stop Rec") : tr("Start Rec"));
    _exportBtn->setEnabled(hasHistory);
    _clearBtn->setEnabled(hasHistory);

    const double memMb = static_cast<double>(info.sessionHeapBytes) / (1024.0 * 1024.0);
    QString provenanceStr = info.loadedFromFile
        ? tr(" [Loaded: %1]").arg(QFileInfo(QString::fromStdString(info.sourcePath)).fileName())
        : QString();

    const bool scrubberWasVisible = _scrubberContainer->isVisible();

    if (isRecording)
    {
        // Actively recording: hide timeline scrubber and display live startFrame - currentFrame recording status
        _scrubberContainer->setVisible(false);
        const uint64_t startFrame = info.sessionStartFrame;
        const uint64_t curFrame = info.currentEndFrame;
        _statusLabel->setText(tr("Rec | Frames: %1 - %2 | Memory: %3 MB%4")
                                  .arg(startFrame)
                                  .arg(curFrame)
                                  .arg(memMb, 0, 'f', 1)
                                  .arg(provenanceStr));
    }
    else
    {
        // Stopped, Detached, or Loaded: show timeline scrubber ONLY if history exists
        _scrubberContainer->setVisible(hasHistory);

        if (hasHistory)
        {
            const uint64_t startFrame = info.sessionStartFrame;
            const uint64_t endFrame = info.currentEndFrame;

            if (isDetached)
            {
                // User is actively scrubbing inside the recorded session history
                const uint64_t currentFrame = currentPos.frame;
                _statusLabel->setText(tr("Scrubbing | Range: %1 - %2 | Frame: %3 | Memory: %4 MB%5")
                                          .arg(startFrame)
                                          .arg(endFrame)
                                          .arg(currentFrame)
                                          .arg(memMb, 0, 'f', 1)
                                          .arg(provenanceStr));
            }
            else
            {
                // Recording stopped: live emulator state is running ahead of recorded session
                _statusLabel->setText(tr("Stopped | Range: %1 - %2 [Live Ahead] | Memory: %3 MB%4")
                                          .arg(startFrame)
                                          .arg(endFrame)
                                          .arg(memMb, 0, 'f', 1)
                                          .arg(provenanceStr));
            }

            const uint64_t activeFrame = isDetached ? currentPos.frame : endFrame;
            _jumpStartBtn->setEnabled(activeFrame > startFrame);
            _stepBackBtn->setEnabled(activeFrame > startFrame);
            _stepForwardBtn->setEnabled(activeFrame < endFrame);
            _jumpEndBtn->setEnabled(activeFrame < endFrame);
            _resumeFromHereBtn->setEnabled(true);

            // Update Slider Range to exact recorded frame bounds [sessionStartFrame, currentEndFrame]
            _isInternalSliderUpdate = true;
            _timelineSlider->setEnabled(true);
            _timelineSlider->setRange(static_cast<int>(startFrame), static_cast<int>(endFrame));
            _timelineSlider->setValue(static_cast<int>(activeFrame));
            _isInternalSliderUpdate = false;
        }
        else
        {
            _statusLabel->setText(tr("Ready | Memory: %1 MB%2")
                                      .arg(memMb, 0, 'f', 1)
                                      .arg(provenanceStr));
            _timelineSlider->setEnabled(false);
            _timelineSlider->setRange(0, 0);
            _jumpStartBtn->setEnabled(false);
            _stepBackBtn->setEnabled(false);
            _stepForwardBtn->setEnabled(false);
            _jumpEndBtn->setEnabled(false);
            _resumeFromHereBtn->setEnabled(false);
        }
    }

    if (scrubberWasVisible != _scrubberContainer->isVisible())
    {
        if (_visibleByUser)
        {
            setFixedHeight(desiredHeight());
        }
        updateGeometry();
        emit heightChanged();
    }
}

void TtdWidget::onRecordToggled()
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !context->pTimeTravelManager) return;
    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;

    if (ttd->IsRecording())
    {
        ttd->StopRecording();
    }
    else
    {
        ttd->StartRecording();
    }
    updateTelemetry();
}

void TtdWidget::onLoadSession()
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !context->pTimeTravelManager) return;
    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;

    QString fileName = QFileDialog::getOpenFileName(this, tr("Load TTD Session"), QString(),
                                                   tr("Time Travel Session (*.ttd)"));
    if (fileName.isEmpty()) return;

    if (_activeEmulator->IsRunning() && !_activeEmulator->IsPaused())
    {
        _activeEmulator->Pause();
    }

    std::ifstream in(fileName.toStdString(), std::ios::binary);
    std::string err;
    if (!in.is_open() || !ttd->DeserializeSession(in, err))
    {
        QMessageBox::warning(this, tr("TTD Load Failed"),
                             tr("Failed to load TTD session: %1").arg(QString::fromStdString(err)));
    }
    else
    {
        ttd->SetSessionSourcePath(fileName.toStdString());
        ttd::TTDSessionInfo info = ttd->GetSessionInfo();
        ttd->SeekTo(ttd::TTDTimePoint{info.sessionStartFrame, 0});
        _mainWindow->refreshViewport();
    }
    updateTelemetry();
}

void TtdWidget::onExportSession()
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !context->pTimeTravelManager) return;
    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;

    QString fileName = QFileDialog::getSaveFileName(this, tr("Export TTD Session"), QString(),
                                                   tr("Time Travel Session (*.ttd)"));
    if (fileName.isEmpty()) return;

    if (_activeEmulator->IsRunning() && !_activeEmulator->IsPaused())
    {
        _activeEmulator->Pause();
    }

    std::ofstream out(fileName.toStdString(), std::ios::binary);
    std::string err;
    if (!out.is_open() || !ttd->SerializeSession(out, err))
    {
        QMessageBox::warning(this, tr("TTD Export Failed"),
                             tr("Failed to export TTD session: %1").arg(QString::fromStdString(err)));
    }
    else
    {
        ttd->SetSessionSourcePath(fileName.toStdString());
        QMessageBox::information(this, tr("TTD Export Successful"),
                                tr("Session saved to %1").arg(fileName));
    }
    updateTelemetry();
}

void TtdWidget::onClearSession()
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !context->pTimeTravelManager) return;
    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;

    ttd->InvalidateSession("User cleared session in Qt GUI");
    updateTelemetry();
}

void TtdWidget::performSeekToFrame(uint64_t targetFrame)
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !context->pTimeTravelManager) return;
    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;

    if (_activeEmulator->IsRunning() && !_activeEmulator->IsPaused())
    {
        _activeEmulator->Pause();
    }

    ttd::TTDTimePoint target{targetFrame, 0};
    ttd->SeekTo(target);

    _mainWindow->refreshViewport();
    updateTelemetry();
}

void TtdWidget::onJumpStart()
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !context->pTimeTravelManager) return;
    ttd::TTDSessionInfo info = context->pTimeTravelManager->GetSessionInfo();
    performSeekToFrame(info.sessionStartFrame);
}

void TtdWidget::onStepBack()
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !context->pTimeTravelManager) return;
    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;

    if (_activeEmulator->IsRunning() && !_activeEmulator->IsPaused())
    {
        _activeEmulator->Pause();
    }

    if (ttd->StepBackFrame())
    {
        _mainWindow->refreshViewport();
        updateTelemetry();
    }
}

void TtdWidget::onStepForward()
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !context->pTimeTravelManager) return;
    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;

    if (_activeEmulator->IsRunning() && !_activeEmulator->IsPaused())
    {
        _activeEmulator->Pause();
    }

    if (ttd->StepForwardFrame())
    {
        _mainWindow->refreshViewport();
        updateTelemetry();
    }
}

void TtdWidget::onJumpEnd()
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !context->pTimeTravelManager) return;
    ttd::TTDTimePoint endPos = context->pTimeTravelManager->SessionEndPosition();
    performSeekToFrame(endPos.frame);
}

void TtdWidget::onResumeFromHere()
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !context->pTimeTravelManager) return;
    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;

    ttd::TTDTimePoint currentPos = ttd->CurrentPosition();
    ttd->ResumeRecordingFrom(currentPos);
    _activeEmulator->Start();
    updateTelemetry();
}

void TtdWidget::onSliderMoved(int value)
{
    if (_isInternalSliderUpdate) return;
    performSeekToFrame(static_cast<uint64_t>(value));
}

void TtdWidget::onSliderValueChanged(int value)
{
    if (_isInternalSliderUpdate) return;
    performSeekToFrame(static_cast<uint64_t>(value));
}
