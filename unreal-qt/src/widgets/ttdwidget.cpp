#pragma push_macro("slots")
#undef slots
#include "debugger/ttd/ttdsession.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdfileinfo.h"
#include "common/filehelper.h"
#pragma pop_macro("slots")

#include "widgets/ttdwidget.h"

#include <QFileDialog>
#include <QMenu>
#include <QProgressDialog>
#include <QSignalBlocker>
#include <QStyleOptionSlider>
#include <atomic>
#include <map>
#include <memory>
#include <thread>
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

    _journalBtn = new QPushButton(tr("Journal"), this);
    _journalBtn->setCheckable(true);
    _journalBtn->setToolTip(tr("Write journal: record every memory write, so 'who wrote this address last' answers at "
                               "once. Off by default; switch it at any moment, also while recording (each on-off "
                               "span is a segment, shown as a band on the timeline). Without it the search replays "
                               "one frame - same answer, slower."));
    connect(_journalBtn, &QPushButton::toggled, this, &TtdWidget::onJournalToggled);

    _clearBtn = new QPushButton(tr("Clear"), this);
    _clearBtn->setToolTip(tr("Clear current timeline history and reset session"));
    connect(_clearBtn, &QPushButton::clicked, this, &TtdWidget::onClearSession);

    // History limit: the oldest frames are released beyond it while recording, so
    // a long session stays within memory. Applied to the active emulator (an
    // automation call may set another one afterwards); remembered across runs
    _historyCombo = new QComboBox(this);
    _historyCombo->addItem(tr("Keep all"), QVariant::fromValue<qulonglong>(0));
    for (int gb : {1, 2, 4, 8, 16})
        _historyCombo->addItem(tr("Keep %1 GB").arg(gb), QVariant::fromValue<qulonglong>(qulonglong(gb) << 30));
    _historyCombo->setToolTip(tr("History limit: while recording, the oldest frames are released once the history "
                                 "holds more than this; a session saved afterwards replays its remaining frames"));
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
        const qulonglong saved = settings.value(QStringLiteral("ttd/historyLimitBytes"), 0).toULongLong();
        const int index = _historyCombo->findData(QVariant::fromValue<qulonglong>(saved));
        _historyCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
    connect(_historyCombo, &QComboBox::currentIndexChanged, this, [this]() {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Unreal", "Unreal-NG");
        settings.setValue(QStringLiteral("ttd/historyLimitBytes"), _historyCombo->currentData().toULongLong());
        applyHistoryLimit();
        updateTelemetry();
    });

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
    _historyCombo->setFixedHeight(row1Height);

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
    controlLayout->addWidget(_journalBtn, 0, Qt::AlignVCenter);
    controlLayout->addWidget(_clearBtn, 0, Qt::AlignVCenter);
    controlLayout->addWidget(_historyCombo, 0, Qt::AlignVCenter);
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

    _timelineSlider = new JournalSpanSlider(Qt::Horizontal, _scrubberContainer);
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

    _buildJournalBtn = new QPushButton(tr("Build Journal"), _scrubberContainer);
    _buildJournalBtn->setToolTip(tr("Build the write journal by replaying recorded frames (about 2-4 ms per frame, "
                                    "cancelable): then 'who wrote this address last' answers at once there"));
    {
        QMenu* menu = new QMenu(_buildJournalBtn);
        auto frameNow = [this]() -> uint64_t {
            return _timelineSlider ? static_cast<uint64_t>(_timelineSlider->value()) : 0;
        };
        menu->addAction(tr("Whole session"), this, [this]() { buildJournal(0, UINT64_MAX); });
        menu->addAction(tr("From the start to here"), this, [this, frameNow]() { buildJournal(0, frameNow()); });
        menu->addAction(tr("From here to the end"), this, [this, frameNow]() { buildJournal(frameNow(), UINT64_MAX); });
        _buildJournalBtn->setMenu(menu);
    }

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
    _buildJournalBtn->setFixedHeight(row2Height);

    _scrubberContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _scrubberContainer->setFixedHeight(row2Height);

    scrubberLayout->addWidget(_jumpStartBtn, 0, Qt::AlignVCenter);
    scrubberLayout->addWidget(_stepBackBtn, 0, Qt::AlignVCenter);
    scrubberLayout->addWidget(_timelineSlider, 1, Qt::AlignVCenter);
    scrubberLayout->addWidget(_stepForwardBtn, 0, Qt::AlignVCenter);
    scrubberLayout->addWidget(_jumpEndBtn, 0, Qt::AlignVCenter);
    scrubberLayout->addWidget(_buildJournalBtn, 0, Qt::AlignVCenter);
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

void TtdWidget::applyHistoryLimit()
{
    if (!_activeEmulator || !_historyCombo)
        return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !ttd::HasTimeTravelSession(context))
        return;
    (void)ttd::TTDControl(context).Execute(
        {"history-limit", {{"frames", "0"}, {"bytes", std::to_string(_historyCombo->currentData().toULongLong())}}});
}

void TtdWidget::updateState(std::shared_ptr<Emulator> activeEmulator)
{
    const bool changed = _activeEmulator != activeEmulator;
    _activeEmulator = activeEmulator;
    if (changed)
        applyHistoryLimit();

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
    if (_journalBuildRunning)
        return;   // a worker thread replays the session: the progress dialog reports
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

    // Leased for this handler: an automation thread may remove the instance
    const Emulator::ContextLease lease = _activeEmulator->LeaseContext();
    EmulatorContext* context = lease.get();
    if (!context || !ttd::HasTimeTravelSession(context))
    {
        _statusLabel->setText(tr("TTD: Unavailable"));
        _scrubberContainer->setVisible(false);
        return;
    }

    ttd::TTDSessionRef ttd(context);
    // The published snapshot, never the live session: this 100 ms timer runs
    // while the machine's thread records (and automation may stop or
    // invalidate the session at the same time)
    const ttd::TTDSessionInfo info = ttd->GetPublishedSessionInfo();
    ttd::TTDTimePoint currentPos = ttd->CurrentPosition();

    // Not available for this machine at all (a ZX-Poly member): say why
    if (!info.unavailableReason.empty())
    {
        _recordBtn->setEnabled(false);
        _recordBtn->setText(tr("Start Rec"));
        _loadBtn->setEnabled(false);
        _exportBtn->setEnabled(false);
        _clearBtn->setEnabled(false);
        _statusLabel->setText(tr("TTD: not available"));
        _statusLabel->setToolTip(QString::fromStdString(info.unavailableReason));
        _scrubberContainer->setVisible(false);
        return;
    }
    _statusLabel->setToolTip(QString());

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
    // The engine's controller browses while it records: the slider pauses the recording (D8)
    EmulatorContext* activeContext = _activeEmulator ? _activeEmulator->GetContext() : nullptr;
    const bool browsesWhileRecording = isRecording && activeContext && activeContext->pTimeTravelController;

    _recordBtn->setEnabled(true);
    _recordBtn->setText(isRecording || info.recordingPaused ? tr("Stop Rec") : tr("Start Rec"));
    {
        const QSignalBlocker block(_journalBtn);
        _journalBtn->setChecked(info.writeJournalEnabled);
    }
    _exportBtn->setEnabled(hasHistory);
    _clearBtn->setEnabled(hasHistory);

    const double memMb = static_cast<double>(info.sessionHeapBytes) / (1024.0 * 1024.0);
    QString provenanceStr = info.loadedFromFile
        ? tr(" [Loaded: %1]").arg(QFileInfo(QString::fromStdString(info.sourcePath)).fileName())
        : QString();

    // What the write journal covers (D40): searches for "who wrote this last"
    // answer at once inside its spans and replay one frame elsewhere
    if (hasHistory && !info.writeJournalSpans.empty())
        provenanceStr += info.writeJournalComplete ? tr(" | Journal: whole session")
                                                   : tr(" | Journal: %1 span(s)").arg(info.writeJournalSpans.size());
    QString tooltip = hasHistory && !info.writeJournalSpans.empty() && !info.writeJournalComplete
        ? tr("The write journal covers part of this session (the band on the timeline): write searches outside it "
             "replay one frame (same answer, slower). 'Build Journal' builds it for more.")
        : QString();
    if (hasHistory)
    {
        // The machine the session was recorded on: what an instance must match to load it
        const ttd::TTDRecordedMachine& m = info.machine;
        QStringList devices;
        for (const std::string& d : m.peripherals)
            devices << QString::fromStdString(d);
        const QString machine =
            tr("Recorded on %1 - General Sound: %2, TurboSound slot: %3\nDevices: %4")
                .arg(QString::fromStdString(m.model.empty() ? "model id " + std::to_string(m.modelId) : m.model))
                .arg(QString::fromUtf8(ttd::GeneralSoundName(m.generalSound)))
                .arg(QString::fromStdString(m.turboSound))
                .arg(devices.isEmpty() ? tr("none") : devices.join(QStringLiteral(", ")));
        tooltip = tooltip.isEmpty() ? machine : tooltip + QStringLiteral("\n\n") + machine;
    }
    _statusLabel->setToolTip(tooltip);

    const bool scrubberWasVisible = _scrubberContainer->isVisible();

    if (isRecording && !browsesWhileRecording)
    {
        // Actively recording: hide timeline scrubber and display live startFrame - currentFrame recording status
        _scrubberContainer->setVisible(false);
        const uint64_t startFrame = info.sessionStartFrame;
        const uint64_t curFrame = info.currentEndFrame;
        const QString released = info.evictedCheckpoints != 0
                                     ? tr(" | %1 oldest frames released").arg(info.evictedCheckpoints)
                                     : QString();
        _statusLabel->setText(tr("Rec | Frames: %1 - %2 | Memory: %3 MB%4%5")
                                  .arg(startFrame)
                                  .arg(curFrame)
                                  .arg(memMb, 0, 'f', 1)
                                  .arg(released)
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

            if (isRecording)
            {
                // Recording, the slider ready: moving it pauses the recording
                _statusLabel->setText(tr("Rec | Frames: %1 - %2 | Memory: %3 MB%4")
                                          .arg(startFrame)
                                          .arg(endFrame)
                                          .arg(memMb, 0, 'f', 1)
                                          .arg(provenanceStr));
            }
            else if (isDetached && info.recordingPaused)
            {
                // The recording paused for browsing: resumed at its end it goes on
                _statusLabel->setText(tr("Rec paused | Range: %1 - %2 | Frame: %3 | Memory: %4 MB%5")
                                          .arg(startFrame)
                                          .arg(endFrame)
                                          .arg(currentPos.frame)
                                          .arg(memMb, 0, 'f', 1)
                                          .arg(provenanceStr));
            }
            else if (isDetached)
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
            _buildJournalBtn->setEnabled(!info.writeJournalComplete);

            // The write journal's spans as a band along the timeline
            std::vector<std::pair<double, double>> spans;
            const double range = endFrame > startFrame ? static_cast<double>(endFrame - startFrame) : 1.0;
            for (const auto& [from, to] : info.writeJournalSpans)
                spans.emplace_back(std::clamp((static_cast<double>(from.frame) - startFrame) / range, 0.0, 1.0),
                                   std::clamp((static_cast<double>(to.frame) - startFrame) / range, 0.0, 1.0));
            _timelineSlider->setJournalSpans(std::move(spans));

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
            _buildJournalBtn->setEnabled(false);
            _timelineSlider->setJournalSpans({});
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
    if (!context || !ttd::HasTimeTravelSession(context)) return;
    // The panel's journal toggle sets the journal choice; start keeps it
    // A paused recording (D8) is still the recording: the button ends it
    const ttd::TTDSessionRef session(context);
    const bool recording = session->IsRecording() || session->GetSessionInfo().recordingPaused;
    (void)ttd::TTDControl(context).Execute({recording ? "stop" : "start", {}});
    updateTelemetry();
}

void TtdWidget::onLoadSession()
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !ttd::HasTimeTravelSession(context)) return;

    QString fileName = QFileDialog::getOpenFileName(this, tr("Load TTD Session"), QString(),
                                                   tr("Time Travel Session (*.ttd)"));
    if (fileName.isEmpty()) return;

    if (_activeEmulator->IsRunning() && !_activeEmulator->IsPaused())
    {
        _activeEmulator->Pause();
    }

    // The file's recorded machine first (headers only): a session loads only into
    // the model it was recorded on, with the same ROM set and slot cards
    const std::string path = fileName.toUtf8().toStdString();
    ttd::TTDFileInfo fileInfo;
    std::string err;
    if (!ttd::ReadTTDFileInfo(path, fileInfo, err))
    {
        QMessageBox::warning(this, tr("TTD Load Failed"), tr("Not a readable TTD session: %1").arg(QString::fromStdString(err)));
        return;
    }
    const ttd::TTDRecordedMachine& recorded = fileInfo.machine;
    const QString recordedText =
        tr("Recorded on %1, General Sound: %2, TurboSound slot: %3")
            .arg(QString::fromStdString(recorded.model.empty() ? "model id " + std::to_string(recorded.modelId) : recorded.model))
            .arg(QString::fromUtf8(ttd::GeneralSoundName(recorded.generalSound)))
            .arg(QString::fromStdString(recorded.turboSound));
    if (recorded.modelId != static_cast<uint8_t>(context->config.mem_model))
    {
        QMessageBox::warning(this, tr("TTD Load Failed"),
                             tr("%1.\nThis machine is a different model: create a machine of the recorded model and "
                                "load the session there.")
                                 .arg(recordedText));
        return;
    }

    ttd::TTDControl control(context);
    const ttd::TTDReply loaded = control.Execute({"load", {{"path", path}}});
    if (!loaded.Ok())
    {
        QMessageBox::warning(this, tr("TTD Load Failed"),
                             tr("Failed to load TTD session: %1\n\n%2").arg(QString::fromStdString(loaded.message), recordedText));
    }
    else
    {
        (void)control.Execute({"seek", {{"frame", std::to_string(loaded.body.find("session_start_frame")->i)},
                                        {"tinframe", "0"}}});
        _mainWindow->refreshViewport();
    }
    updateTelemetry();
}

void TtdWidget::onExportSession()
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !ttd::HasTimeTravelSession(context)) return;
    ttd::TTDSessionRef ttd(context);

    QString fileName = QFileDialog::getSaveFileName(this, tr("Export TTD Session"), QString(),
                                                   tr("Time Travel Session (*.ttd)"));
    if (fileName.isEmpty()) return;

    if (_activeEmulator->IsRunning() && !_activeEmulator->IsPaused())
    {
        _activeEmulator->Pause();
    }

    // UTF-8: the dump verb opens it through FileHelper (non-ASCII names on Windows)
    const std::string path = fileName.toUtf8().toStdString();
    const ttd::TTDReply dumped = ttd::TTDControl(context).Execute({"dump", {{"path", path}}});
    if (!dumped.Ok())
    {
        QMessageBox::warning(this, tr("TTD Export Failed"),
                             tr("Failed to export TTD session: %1").arg(QString::fromStdString(dumped.message)));
    }
    else
    {
        ttd->SetSessionSourcePath(path);
        QMessageBox::information(this, tr("TTD Export Successful"),
                                tr("Session saved to %1").arg(fileName));
    }
    updateTelemetry();
}

void TtdWidget::onClearSession()
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !ttd::HasTimeTravelSession(context)) return;
    ttd::TTDSessionRef ttd(context);

    // B9: clearing while recording would drop the history being recorded
    if (ttd->IsRecording())
    {
        QMessageBox::warning(this, tr("TTD Recording Active"),
                             QString::fromStdString(ttd::TTDControl(context).Execute({"invalidate", {}}).message));
        return;
    }
    // A Detached machine that runs replays the recorded history (input
    // playback, the session-end check at every frame): park it before the
    // history is freed, as every other action here does
    if (ttd->GetState() == ttd::TTDSessionState::Detached && _activeEmulator->IsRunning() &&
        !_activeEmulator->IsPaused())
    {
        _activeEmulator->Pause();
        _activeEmulator->WaitForPauseConfirmation(1000);
    }
    (void)ttd::TTDControl(context).Execute({"invalidate", {{"reason", "User cleared session in Qt GUI"}}});
    updateTelemetry();
}

void TtdWidget::performSeekToFrame(uint64_t targetFrame, bool frameStart)
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !ttd::HasTimeTravelSession(context)) return;
    // The verb parks the machine and leaves it paused at the target. A frame
    // alone is the frame's end on the engine (D13); the start is asked for by T-state 0
    std::map<std::string, std::string> options{{"frame", std::to_string(targetFrame)}};
    if (frameStart)
        options["tinframe"] = "0";
    (void)ttd::TTDControl(context).Execute({"seek", options});

    _mainWindow->refreshViewport();
    updateTelemetry();
}

void TtdWidget::onJumpStart()
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !ttd::HasTimeTravelSession(context)) return;
    // Read before the seek pauses the machine: the published snapshot
    const ttd::TTDSessionInfo info = ttd::TTDSessionRef(context)->GetPublishedSessionInfo();
    performSeekToFrame(info.sessionStartFrame, /*frameStart=*/true);
}

void TtdWidget::onStepBack()
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !ttd::HasTimeTravelSession(context)) return;
    const ttd::TTDReply stepped = ttd::TTDControl(context).Execute({"step-back", {}});
    if (stepped.Ok() && stepped.body.find("stepped")->b)
    {
        _mainWindow->refreshViewport();
        updateTelemetry();
    }
}

void TtdWidget::onStepForward()
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !ttd::HasTimeTravelSession(context)) return;
    const ttd::TTDReply stepped = ttd::TTDControl(context).Execute({"step-forward", {}});
    if (stepped.Ok() && stepped.body.find("stepped")->b)
    {
        _mainWindow->refreshViewport();
        updateTelemetry();
    }
}

void TtdWidget::onJumpEnd()
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !ttd::HasTimeTravelSession(context)) return;
    // Read before the seek pauses the machine: the published snapshot
    const ttd::TTDSessionInfo info = ttd::TTDSessionRef(context)->GetPublishedSessionInfo();
    performSeekToFrame(info.currentEndFrame);
}

void TtdWidget::onResumeFromHere()
{
    if (!_activeEmulator) return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !ttd::HasTimeTravelSession(context)) return;
    // From exactly where the machine stands; the verb runs it again
    (void)ttd::TTDControl(context).Execute({"resume", {}});
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

void TtdWidget::onJournalToggled(bool on)
{
    if (!_activeEmulator)
        return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !ttd::HasTimeTravelSession(context))
        return;
    (void)ttd::TTDControl(context).Execute({"journal", {{"enabled", on ? "true" : "false"}}});
    updateTelemetry();
}

void TtdWidget::buildJournal(uint64_t fromFrame, uint64_t toFrame)
{
    if (!_activeEmulator || _journalBuildRunning)
        return;
    EmulatorContext* context = _activeEmulator->GetContext();
    if (!context || !ttd::HasTimeTravelSession(context))
        return;
    if (ttd::TTDSessionRef(context)->IsRecording())
    {
        QMessageBox::information(this, tr("Build Journal"),
                                 tr("Stop the recording first: the journal is built by replaying recorded history."));
        return;
    }

    // The verb parks the machine and replays; the dialog follows it through the journal verb
    _journalBuildRunning = true;
    auto result = std::make_shared<ttd::TTDReply>();
    auto finished = std::make_shared<std::atomic<bool>>(false);
    std::thread worker([context, fromFrame, toFrame, result, finished]() {
        *result = ttd::TTDControl(context).Execute(
            {"journal-build", {{"from_frame", std::to_string(fromFrame)}, {"to_frame", std::to_string(toFrame)}}});
        finished->store(true);
    });

    QProgressDialog progress(tr("Building the write journal by replay..."), tr("Cancel"), 0, 1, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    QTimer poll;
    poll.setInterval(100);
    connect(&poll, &QTimer::timeout, this, [&]() {
        const ttd::TTDReply journal = ttd::TTDControl(context).Execute({"journal", {}});
        if (const StateNode* build = journal.body.find("write_journal_build"); build && build->find("total")->i)
        {
            progress.setMaximum(static_cast<int>(build->find("total")->i));
            progress.setValue(static_cast<int>(build->find("done")->i));
        }
        if (progress.wasCanceled())
            (void)ttd::TTDControl(context).Execute({"journal-build-cancel", {}});
        if (finished->load())
            progress.close();
    });
    poll.start();
    progress.exec();
    poll.stop();
    if (!finished->load())
        (void)ttd::TTDControl(context).Execute({"journal-build-cancel", {}});   // the dialog went away some other way
    worker.join();
    _journalBuildRunning = false;

    if (!result->Ok())
        QMessageBox::warning(this, tr("Build Journal"), QString::fromStdString(result->message));
    else
        _statusLabel->setToolTip(tr("Write journal built for %1 frame(s), %2 writes%3")
                                     .arg(result->body.find("frames_built")->i)
                                     .arg(result->body.find("records")->i)
                                     .arg(result->body.find("cancelled")->b ? tr(" (cancelled)") : QString()));
    _mainWindow->refreshViewport();
    updateTelemetry();
}
