#include "widgets/recordingwidget.h"
#include "widgets/recordingplan.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QStyleOptionButton>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QMessageBox>
#include <algorithm>
#include <QElapsedTimer>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "base/featuremanager.h"
#include "mainwindow.h"
#include "recording/src/recordingmanager.h"
#include "platform_encoder.h"
#include "ffmpeg_probe.h"
#include "widgets/tintedsvgicon.h"
#include "widgets/windowrecorder.h"
#include "emulator/video/windowview.h"

namespace {

class RecordingStatusLabel : public QLabel
{
public:
    explicit RecordingStatusLabel(const QString& text, QAbstractButton* referenceButton = nullptr, QWidget* parent = nullptr)
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

RecordingWidget::RecordingWidget(MainWindow* mainWindow, QWidget* parent)
    : QWidget(parent), _mainWindow(mainWindow)
{
    setObjectName(QStringLiteral("recordingWidget"));
    setAutoFillBackground(true);

    _settings.load();

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 4, 6, 4);
    mainLayout->setSpacing(4);

    QHBoxLayout* controlLayout = new QHBoxLayout();
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(6);
    controlLayout->setAlignment(Qt::AlignVCenter);

    _recordBtn = new QPushButton(tr("Start Rec"), this);

    _recordBtn->setObjectName("recRecordBtn");
    _recordBtn->setIcon(tintedSvgIcon(QStringLiteral("record"), /*tint=*/false));
    _recordBtn->setToolTip(tr("Start / Stop Audio/Video Recording"));
    _recordBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(_recordBtn, &QPushButton::clicked, this, &RecordingWidget::onRecordToggled);

    _pauseBtn = new QPushButton(tr("Pause"), this);
    _pauseBtn->setIcon(tintedSvgIcon(QStringLiteral("pause")));
    _pauseBtn->setToolTip(tr("Pause / Resume Current Recording"));
    _pauseBtn->setEnabled(false);
    _pauseBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(_pauseBtn, &QPushButton::clicked, this, &RecordingWidget::onPauseToggled);

    _formatCombo = new QComboBox(this);

    _formatCombo->setObjectName("recFormatCombo");
    _formatCombo->setToolTip(tr("Format: video codec and container (H.265 by default), GIF, or audio only"));
    _formatCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _formatCombo->setFixedWidth(112);
    connect(_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordingWidget::onFormatChanged);

    _qualityCombo = new QComboBox(this);

    _qualityCombo->setObjectName("recQualityCombo");
    _qualityCombo->setToolTip(tr("Quality / Bitrate Setting"));
    _qualityCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _qualityCombo->setFixedWidth(80);
    connect(_qualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordingWidget::onQualityChanged);

    _regionCombo = new QComboBox(this);

    _regionCombo->setObjectName("recRegionCombo");
    _regionCombo->addItem(tr("Full"), 0);
    _regionCombo->addItem(tr("Screen"), 1);
    _regionCombo->setToolTip(tr("Capture Region: Full Frame (with border) vs Screen Only (256x192)"));
    _regionCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _regionCombo->setFixedWidth(64);
    connect(_regionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordingWidget::onRegionChanged);

    _sourceCombo = new QComboBox(this);

    _sourceCombo->setObjectName("recSourceCombo");
    _sourceCombo->addItem(tr("Raw"), false);
    _sourceCombo->addItem(tr("Window"), true);
    _sourceCombo->setToolTip(tr("Picture source: the emulator's raw frame buffer, or the window as it draws it "
                                "(CRT shaders, temporal processing)"));
    _sourceCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _sourceCombo->setFixedWidth(76);
    connect(_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordingWidget::onSourceChanged);

    _profileCombo = new QComboBox(this);

    _profileCombo->setObjectName("recProfileCombo");
    _profileCombo->addItem(tr("Original"), QStringLiteral("native"));
    _profileCombo->addItem(tr("1080p"), QStringLiteral("1080p"));
    _profileCombo->addItem(tr("1440p"), QStringLiteral("1440p"));
    _profileCombo->addItem(tr("4K"), QStringLiteral("4k"));
    _profileCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _profileCombo->setFixedWidth(78);
    connect(_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordingWidget::onProfileChanged);

    const int iconMetric = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    const QSize toolbarIconSize(iconMetric, iconMetric);
    const int closeBtnDim = toolbarIconSize.height() + 4;

    _advancedBtn = new QToolButton(this);
    _advancedBtn->setText(QStringLiteral("..."));
    _advancedBtn->setToolTip(tr("Open Advanced Recording Dialog..."));
    _advancedBtn->setAutoRaise(true);
    _advancedBtn->setCursor(Qt::PointingHandCursor);
    _advancedBtn->setFixedSize(closeBtnDim, closeBtnDim);
    _advancedBtn->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "    border: none;"
        "    background: transparent;"
        "    font-weight: bold;"
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
    connect(_advancedBtn, &QToolButton::clicked, this, &RecordingWidget::advancedSettingsRequested);

    _closeBtn = new QToolButton(this);
    _closeBtn->setIcon(tintedSvgIcon(QStringLiteral("close")));
    _closeBtn->setIconSize(toolbarIconSize);
    _closeBtn->setToolTip(tr("Close Recording Control Panel"));
    _closeBtn->setAutoRaise(true);
    _closeBtn->setCursor(Qt::PointingHandCursor);
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

    _rowHeight = std::max(_recordBtn->sizeHint().height(), 26);
    const int row1Height = _rowHeight;
    _recordBtn->setFixedHeight(row1Height);
    _pauseBtn->setFixedHeight(row1Height);

    auto* statusLabel = new RecordingStatusLabel(tr("Ready"), _recordBtn, this);
    statusLabel->setFont(_recordBtn->font());
    statusLabel->setFixedHeight(row1Height);
    statusLabel->setWordWrap(false);
    statusLabel->setMinimumWidth(0);
    statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _statusLabel = statusLabel;

    controlLayout->addWidget(_recordBtn, 0, Qt::AlignVCenter);
    controlLayout->addWidget(_pauseBtn, 0, Qt::AlignVCenter);
    controlLayout->addWidget(_formatCombo, 0, Qt::AlignVCenter);
    controlLayout->addWidget(_sourceCombo, 0, Qt::AlignVCenter);
    controlLayout->addWidget(_qualityCombo, 0, Qt::AlignVCenter);
    controlLayout->addWidget(_regionCombo, 0, Qt::AlignVCenter);
    controlLayout->addWidget(_profileCombo, 0, Qt::AlignVCenter);
    controlLayout->addWidget(_statusLabel, 1, Qt::AlignVCenter);
    controlLayout->addWidget(_advancedBtn, 0, Qt::AlignVCenter);
    controlLayout->addWidget(_closeBtn, 0, Qt::AlignVCenter);

    _controlContainer = new QWidget(this);
    _controlContainer->setLayout(controlLayout);
    _controlContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _controlContainer->setFixedHeight(row1Height);
    _controlContainer->setMinimumWidth(0);
    mainLayout->addWidget(_controlContainer);

    _statsTimer = new QTimer(this);
    _statsTimer->setInterval(250);
    connect(_statsTimer, &QTimer::timeout, this, &RecordingWidget::onUpdateStats);
    // The full dialog (or another recording UI) changed the shared options: show them here at once
    recordingsettingsbus::Subscribe(this, [this] {
        if (!_selfSave && !_isRecording)
            reloadSettings();
    });

    applySettingsToUI();
    updateStatusLabel();
}

RecordingWidget::~RecordingWidget()
{
    if (_statsTimer)
        _statsTimer->stop();
}

int RecordingWidget::desiredHeight() const
{
    if (!_visibleByUser)
        return 0;

    const int rowHeight = (_controlContainer && _controlContainer->height() > 0)
        ? _controlContainer->height()
        : _rowHeight;
    const int topMargin = layout() ? layout()->contentsMargins().top() : 4;
    const int botMargin = layout() ? layout()->contentsMargins().bottom() : 4;
    return topMargin + rowHeight + botMargin;
}

QSize RecordingWidget::sizeHint() const
{
    return QSize(QWidget::sizeHint().width(), desiredHeight());
}

QSize RecordingWidget::minimumSizeHint() const
{
    return QSize(0, desiredHeight());
}

void RecordingWidget::setVisibleByUser(bool visible)
{
    if (_visibleByUser == visible)
        return;

    _visibleByUser = visible;
    setVisible(visible);

    if (visible)
    {
        setFixedHeight(desiredHeight());
        reloadSettings();
        EmulatorContext* context = getActiveContext();
        if (context && context->pFeatureManager)
        {
            context->pFeatureManager->setFeature(Features::kRecording, true);
        }
    }

    emit visibilityChanged(visible);
    emit heightChanged();
}

void RecordingWidget::updateState(std::shared_ptr<Emulator> activeEmulator)
{
    _activeEmulator = activeEmulator;

    if (_windowRecording)
        return;  // the window recording is not the recording manager's

    EmulatorContext* context = getActiveContext();
    if (context && context->pRecordingManager)
    {
        auto* rm = context->pRecordingManager;
        const bool prevRecording = _isRecording;
        _isRecording = rm->IsRecording() || rm->IsPaused();
        _isPaused = rm->IsPaused();

        if (prevRecording != _isRecording)
        {
            emit recordingStateChanged(_isRecording);
        }

        if (_isRecording)
        {
            _recordBtn->setText(tr("Stop Rec"));
            _pauseBtn->setEnabled(true);
            _pauseBtn->setText(_isPaused ? tr("Resume") : tr("Pause"));
            _formatCombo->setEnabled(false);
            _qualityCombo->setEnabled(false);
            _regionCombo->setEnabled(false);
            if (!_statsTimer->isActive())
                _statsTimer->start();
        }
        else
        {
            _recordBtn->setText(tr("Start Rec"));
            _pauseBtn->setEnabled(false);
            _pauseBtn->setText(tr("Pause"));
            _formatCombo->setEnabled(true);
            _qualityCombo->setEnabled(true);
            updateRegionAvailability();
            if (_statsTimer->isActive())
                _statsTimer->stop();
            updateStatusLabel();
        }
    }
}

void RecordingWidget::saveSettings()
{
    _selfSave = true;
    _settings.save();
    _selfSave = false;
}

void RecordingWidget::syncWindowRecordingState()
{
    if (!_mainWindow || _windowRecording)
        return;
    WindowRecordingStatus status;
    _mainWindow->windowRecorder()->status(status);
    // Another recording UI (the full dialog, the WebAPI) is recording the window: this one shows it and can stop it
    if (!status.recording && !status.finalizing)
        return;
    _windowRecording = true;
    _isRecording = true;
    _isPaused = false;
    _finishingPath = QString::fromStdString(status.path);
    _recordBtn->setText(tr("Stop Rec"));
    _recordBtn->setEnabled(status.recording);
    _pauseBtn->setEnabled(false);
    _formatCombo->setEnabled(false);
    _sourceCombo->setEnabled(false);
    _qualityCombo->setEnabled(false);
    _regionCombo->setEnabled(false);
    _profileCombo->setEnabled(false);
    _statsTimer->start();
    onUpdateStats();
    emit recordingStateChanged(true);
}

void RecordingWidget::reloadSettings()
{
    _settings.load();
    applySettingsToUI();
    updateStatusLabel();
}

QString RecordingWidget::currentContainer() const
{
    return _formatCombo->currentData().toString().section(QLatin1Char('|'), 0, 0).toUpper();
}

QString RecordingWidget::currentCodec() const
{
    return _formatCombo->currentData().toString().section(QLatin1Char('|'), 1, 1);
}

recordingplan::Environment RecordingWidget::detectEnvironment() const
{
    // The probes are slow (ffmpeg is asked once): the answer is kept for the run
    static const recordingplan::Environment probed = [] {
        recordingplan::Environment env;
        const auto encoders = PlatformEncoderFactory::getAvailableNativeEncoders();
        env.nativeAvailable = !encoders.empty();
        if (!encoders.empty())
        {
            const auto& enc = encoders[0];
            if (enc.supportsH264) env.nativeCodecs << "h264";
            if (enc.supportsHEVC) env.nativeCodecs << "h265";
            if (enc.supportsAV1)  env.nativeCodecs << "av1";
        }
        const std::string ffmpegPath = FFmpegProbe::findFFmpeg();
        env.ffmpegAvailable = !ffmpegPath.empty();
        env.ffmpegHasWebP = env.ffmpegAvailable && (FFmpegProbe::isEncoderAvailable("libwebp_anim", ffmpegPath) ||
                                                    FFmpegProbe::isEncoderAvailable("libwebp", ffmpegPath));
#ifdef Q_OS_MAC
        env.mac = true;
#endif
        return env;
    }();
    recordingplan::Environment env = probed;
    env.engine = _settings.backend;
    return env;
}

void RecordingWidget::populateFormats()
{
    _formatCombo->blockSignals(true);
    _formatCombo->clear();

    // The engine is chosen in the full recording dialog (Tools > Video Recording...) and shared by every recording UI;
    // the formats are the dialog's own catalog for it (recordingplan.h): both offer the same
    const recordingplan::Environment env = detectEnvironment();
    auto shortCodec = [](const QString& id) {
        return id == "h265" ? QStringLiteral("H.265") : recordingplan::CodecLabel(id);
    };
    for (const QString& container : recordingplan::Containers(env))
        for (const QString& codec : recordingplan::VideoCodecIds(env, container))
        {
            const bool single = codec == "gif" || codec == "apng" || codec == "webp";
            _formatCombo->addItem(single ? container : QStringLiteral("%1 · %2").arg(shortCodec(codec), container),
                                  QStringLiteral("%1|%2").arg(container, codec));
        }

    // Audio only
    _formatCombo->insertSeparator(_formatCombo->count());
    for (const char* audio : {"WAV", "MP3", "FLAC"})
        _formatCombo->addItem(QString::fromLatin1(audio), QStringLiteral("%1|").arg(QString::fromLatin1(audio)));

    // A format only the full dialog offers (AV1, VP8, AVI, OGG...): the panel shows it as it is, and keeps it, instead of
    // quietly showing another one
    {
        const QString savedKey = QStringLiteral("%1|%2").arg(_settings.container, _settings.videoCodec).toUpper();
        bool listed = false;
        for (int i = 0; i < _formatCombo->count() && !listed; ++i)
            listed = _formatCombo->itemData(i).toString().toUpper() == savedKey ||
                     (_settings.videoCodec.isEmpty() &&
                      _formatCombo->itemData(i).toString().section(QLatin1Char('|'), 0, 0).toUpper() ==
                          _settings.container.toUpper());
        if (!listed && !_settings.container.isEmpty())
        {
            const QString label = _settings.videoCodec.isEmpty()
                                      ? _settings.container.toUpper()
                                      : QStringLiteral("%1 · %2").arg(_settings.videoCodec.toUpper(), _settings.container.toUpper());
            _formatCombo->addItem(label, QStringLiteral("%1|%2").arg(_settings.container, _settings.videoCodec));
        }
    }

    // The saved format; H.265 MP4 when it is gone
    const QString want = QStringLiteral("%1|%2").arg(_settings.container, _settings.videoCodec).toUpper();
    int matchIdx = -1;
    for (int i = 0; i < _formatCombo->count(); ++i)
    {
        if (_formatCombo->itemData(i).toString().toUpper() == want ||
            (_settings.videoCodec.isEmpty() &&
             _formatCombo->itemData(i).toString().section(QLatin1Char('|'), 0, 0).toUpper() == _settings.container.toUpper()))
        {
            matchIdx = i;
            break;
        }
    }
    _formatCombo->setCurrentIndex(matchIdx >= 0 ? matchIdx : 0);
    _formatCombo->blockSignals(false);

    _settings.container = currentContainer();
    _settings.videoCodec = currentCodec();

    updateQualityOptions();
    updateRegionAvailability();
}

void RecordingWidget::updateQualityOptions()
{
    _qualityCombo->blockSignals(true);
    _qualityCombo->clear();

    const QString cont = currentContainer();

    if (cont == QStringLiteral("FLAC"))
    {
        _qualityCombo->addItem(tr("Lossless"), 0);
        _qualityCombo->setEnabled(false);
        _qualityCombo->setToolTip(tr("FLAC is lossless; no quality/bitrate setting needed"));
    }
    else if (cont == QStringLiteral("WAV"))
    {
        _qualityCombo->addItem(tr("16-bit PCM"), 0);
        _qualityCombo->setEnabled(false);
        _qualityCombo->setToolTip(tr("WAV is uncompressed 16-bit PCM (Lossless)"));
    }
    else if (cont == QStringLiteral("MP3"))
    {
        _qualityCombo->addItem(tr("128 kbps"), 128);
        _qualityCombo->addItem(tr("192 kbps"), 192);
        _qualityCombo->addItem(tr("256 kbps"), 256);
        _qualityCombo->addItem(tr("320 kbps"), 320);
        _qualityCombo->setEnabled(!_isRecording);
        _qualityCombo->setToolTip(tr("MP3 Audio Bitrate"));

        int idx = _qualityCombo->findData(_settings.audioBitrate > 0 ? _settings.audioBitrate : 192);
        if (idx >= 0)
            _qualityCombo->setCurrentIndex(idx);
        else
            _qualityCombo->setCurrentIndex(1); // 192 kbps
    }
    else if (cont == QStringLiteral("GIF"))
    {
        _qualityCombo->addItem(tr("Standard"), 0);
        _qualityCombo->addItem(tr("High Quality"), 1);
        _qualityCombo->setEnabled(!_isRecording);
        _qualityCombo->setToolTip(tr("GIF Quality Preset"));
        _qualityCombo->setCurrentIndex(std::min(_settings.quality, 1));
    }
    else // Video (MP4, MOV, MKV, WebM, WebP)
    {
        _qualityCombo->addItem(tr("Fastest"), 0);
        _qualityCombo->addItem(tr("Fast"), 1);
        _qualityCombo->addItem(tr("Medium"), 2);
        _qualityCombo->addItem(tr("High"), 3);
        _qualityCombo->addItem(tr("Best"), 4);
        _qualityCombo->setEnabled(!_isRecording);
        _qualityCombo->setToolTip(tr("Video Encoding Quality Preset"));

        if (_settings.quality >= 0 && _settings.quality < _qualityCombo->count())
            _qualityCombo->setCurrentIndex(_settings.quality);
        else
            _qualityCombo->setCurrentIndex(2); // Medium
    }

    _qualityCombo->blockSignals(false);
}

void RecordingWidget::updateRegionAvailability()
{
    const QString cont = currentContainer();
    const bool isAudio = recordingplan::IsAudioOnly(cont);

    // The same rules as the full dialog (recordingplan.h): which controls the format and the source leave free
    const recordingplan::Controls free = recordingplan::ControlsFor(
        cont, currentCodec(), _settings.windowSource, _settings.profile, true);
    _sourceCombo->setEnabled(free.source && !_isRecording);
    if (currentCodec() == QStringLiteral("prores"))  // ProRes is the window recording's alone
    {
        _sourceCombo->blockSignals(true);
        _sourceCombo->setCurrentIndex(1);
        _sourceCombo->blockSignals(false);
    }
    _sourceCombo->setToolTip(free.source || free.windowSource
                                 ? tr("Picture source: the emulator's raw frame buffer, or the window as it "
                                      "draws it (CRT shaders, temporal processing)")
                                 : tr("The window as source needs an MP4 / MKV / MOV format with H.264, H.265 or "
                                      "ProRes 4444"));
    _profileCombo->setEnabled(free.profile && !_isRecording);
    _profileCombo->setToolTip(free.profile
        ? tr("Output size: Original (the picture times the scale factor) or a fixed 1080p / 1440p / 4K frame. The "
             "picture is fitted into it (aspect kept, nearest neighbor, black bars, no blur)")
        : tr("Fixed output sizes need an H.264 / H.265 / ProRes format"));

    if (isAudio)
    {
        _regionCombo->setEnabled(false);
        _regionCombo->setToolTip(tr("Capture region applies to video/animation recording only"));
    }
    else if (free.windowSource)
    {
        _regionCombo->setEnabled(false);
        _regionCombo->setToolTip(tr("The window recording takes the picture as the window shows it"));
    }
    else
    {
        _regionCombo->setEnabled(!_isRecording);
        _regionCombo->setToolTip(tr("Capture Region: Full Frame (with border) vs Screen Only (256x192)"));
    }
}

void RecordingWidget::applySettingsToUI()
{
    populateFormats();

    if (_settings.captureRegion >= 0 && _settings.captureRegion < _regionCombo->count())
        _regionCombo->setCurrentIndex(_settings.captureRegion);

    _sourceCombo->blockSignals(true);
    _sourceCombo->setCurrentIndex(_settings.windowSource ? 1 : 0);
    _sourceCombo->blockSignals(false);

    _profileCombo->blockSignals(true);
    const int profileIdx = _profileCombo->findData(_settings.profile);
    _profileCombo->setCurrentIndex(profileIdx >= 0 ? profileIdx : 0);
    _profileCombo->blockSignals(false);
    updateRegionAvailability();
}

void RecordingWidget::updateStatusLabel()
{
    if (_isRecording)
        return;

    const QString cont = _settings.container.toUpper();
    const bool isAudio = (cont == "WAV" || cont == "MP3" || cont == "FLAC");
    const QString qualityStr = _qualityCombo->currentText();

    if (isAudio)
    {
        _statusLabel->setText(tr("Ready | %1 Audio | %2").arg(cont, qualityStr));
    }
    else
    {
        const QString regionStr = (windowSourceAvailable() && (_settings.windowSource || currentCodec() == QStringLiteral("prores")))
                                      ? tr("Window")
                                      : (_settings.captureRegion == 1) ? tr("Screen") : tr("Full");
        const QString profileStr = _profileCombo->isEnabled() && _settings.profile != QStringLiteral("native")
                                       ? _profileCombo->currentText() : QString();
        _statusLabel->setText(profileStr.isEmpty() ? tr("Ready | %1 | %2 | %3").arg(cont, qualityStr, regionStr)
                                                   : tr("Ready | %1 | %2 | %3 | %4")
                                                         .arg(cont, qualityStr, regionStr, profileStr));
    }
}

EmulatorContext* RecordingWidget::getActiveContext() const
{
    if (_activeEmulator)
        return _activeEmulator->GetContext();
    return nullptr;
}

QString RecordingWidget::generateOutputPath(const QString& container) const
{
    QSettings settings(QStringLiteral("unreal-ng"), QStringLiteral("recording"));
    const bool isAudio = (container == "WAV" || container == "MP3" || container == "FLAC");
    const QString key = isAudio ? QStringLiteral("lastAudioDir") : QStringLiteral("lastVideoDir");
    QString dir = settings.value(key).toString();

    if (dir.isEmpty() || !QDir(dir).exists())
    {
        dir = isAudio ? QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
                      : QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        if (dir.isEmpty())
            dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"));
    const QString ext = container.toLower();
    return dir + QStringLiteral("/unreal_%1.%2").arg(timestamp, ext);
}

void RecordingWidget::onRecordToggled()
{
    if (_windowRecording)
    {
        stopWindowRecording();
        return;
    }

    EmulatorContext* context = getActiveContext();
    if (!context || !context->pRecordingManager)
    {
        QMessageBox::warning(this, tr("Recording Error"), tr("No active emulator available for recording."));
        return;
    }

    auto* rm = context->pRecordingManager;
    if (rm->IsRecording() || rm->IsPaused())
    {
        // Stop recording
        rm->StopRecording();
        _isRecording = false;
        _isPaused = false;
        _statsTimer->stop();
        _recordBtn->setText(tr("Start Rec"));
        _pauseBtn->setEnabled(false);
        _pauseBtn->setText(tr("Pause"));
        _formatCombo->setEnabled(true);
        _qualityCombo->setEnabled(true);
        updateRegionAvailability();
        updateStatusLabel();
        emit recordingStateChanged(false);
    }
    else
    {
        _settings.load();  // the full dialog may have changed the shared settings
        if (recordingplan::MakePlan(_settings, _settings.backend == 1).windowSource)
        {
            startWindowRecording(_settings.container.toUpper());
            return;
        }

        // Start recording
        if (context->pFeatureManager && !context->pFeatureManager->isEnabled(Features::kRecording))
        {
            context->pFeatureManager->setFeature(Features::kRecording, true);
        }

        // Other recording UIs (the full dialog) change the shared settings
        _settings.load();
        applySettingsToUI();

        EncoderBackend backend = EncoderBackend::Auto;
        if (_settings.backend == 1) backend = EncoderBackend::Native;
        else if (_settings.backend == 2) backend = EncoderBackend::FFmpeg;
        rm->SetEncoderBackend(backend);

        // What the options mean is decided in one place, for this panel and the full dialog alike
        const recordingplan::Plan plan = recordingplan::MakePlan(_settings, backend == EncoderBackend::Native);
        if (!plan.problem.isEmpty())
        {
            QMessageBox::warning(this, tr("Recording"), plan.problem);
            return;
        }
        rm->SetQualityPreset(plan.qualityPreset);
        rm->SetCaptureRegion(plan.mainScreenOnly ? VideoCaptureRegion::MainScreen : VideoCaptureRegion::FullFrame);
        rm->SetScaleFactor(static_cast<uint32_t>(plan.scale));
        rm->SetOutputProfile(plan.profile.toStdString());
        // No switch of its own: recording follows View > GPU acceleration (on = a GPU encoder when there is one)
        rm->SetEncoderAcceleration(UiGpuAcceleration() ? EncoderAcceleration::Auto : EncoderAcceleration::Software);
        rm->SetVideoEnabled(!plan.audioOnly);

        const QString cont = plan.container;
        const QString filename = generateOutputPath(cont);
        const uint32_t aBitrate = static_cast<uint32_t>(plan.audioBitrate);
        bool ok = false;
        if (plan.audioOnly)
            ok = rm->StartRecording(filename.toStdString(), "", plan.audioCodec.toStdString(), 0, aBitrate);
        else
            ok = rm->StartRecording(filename.toStdString(), plan.videoCodec.toStdString(), plan.audioCodec.toStdString());

        if (!ok)
        {
            std::string err = rm->GetLastRecordingError();
            if (err.empty())
                err = "Failed to start recording.";
            QMessageBox::critical(this, tr("Recording Failed"), QString::fromStdString(err));
            return;
        }

        _isRecording = true;
        _isPaused = false;
        _recordBtn->setText(tr("Stop Rec"));
        _pauseBtn->setEnabled(true);
        _pauseBtn->setText(tr("Pause"));
        _formatCombo->setEnabled(false);
        _qualityCombo->setEnabled(false);
        _regionCombo->setEnabled(false);
        updateRegionAvailability();
        _statsTimer->start();
        onUpdateStats();
        emit recordingStateChanged(true);
    }
}

void RecordingWidget::onPauseToggled()
{
    if (_windowRecording)
        return;  // the window recording has no pause

    EmulatorContext* context = getActiveContext();
    if (!context || !context->pRecordingManager)
        return;

    auto* rm = context->pRecordingManager;
    if (rm->IsPaused())
    {
        rm->ResumeRecording();
        _isPaused = false;
        _pauseBtn->setText(tr("Pause"));
    }
    else if (rm->IsRecording())
    {
        rm->PauseRecording();
        _isPaused = true;
        _pauseBtn->setText(tr("Resume"));
    }
    onUpdateStats();
}

void RecordingWidget::onFormatChanged(int index)
{
    Q_UNUSED(index);
    _settings.container = currentContainer();
    _settings.videoCodec = currentCodec();
    saveSettings();
    updateQualityOptions();
    updateRegionAvailability();
    updateStatusLabel();
}

void RecordingWidget::onQualityChanged(int index)
{
    Q_UNUSED(index);
    const QString cont = currentContainer();
    if (cont == QStringLiteral("MP3"))
    {
        _settings.audioBitrate = _qualityCombo->currentData().toInt();
    }
    else
    {
        _settings.quality = _qualityCombo->currentIndex();
    }
    saveSettings();
    updateStatusLabel();
}

void RecordingWidget::onRegionChanged(int index)
{
    _settings.captureRegion = _regionCombo->currentData().toInt();
    saveSettings();
    updateStatusLabel();
}

void RecordingWidget::onProfileChanged(int)
{
    _settings.profile = _profileCombo->currentData().toString();
    saveSettings();
    updateStatusLabel();
}

void RecordingWidget::onSourceChanged(int)
{
    _settings.windowSource = _sourceCombo->currentData().toBool();
    saveSettings();
    updateRegionAvailability();
    updateStatusLabel();
}

bool RecordingWidget::windowSourceAvailable() const
{
    return recordingplan::WindowSourceTakes(currentContainer(), currentCodec());
}

bool RecordingWidget::startWindowRecording(const QString& container)
{
    if (!_mainWindow || !_activeEmulator)
    {
        QMessageBox::warning(this, tr("Recording Error"), tr("No active emulator available for recording."));
        return false;
    }

    const recordingplan::Plan plan = recordingplan::MakePlan(_settings, _settings.backend == 1);
    const WindowRecordingRequest request = MakeWindowRecordingRequest(
        container, plan.videoCodec, !plan.audioCodec.isEmpty(), UiGpuAcceleration(), plan.profile,
        generateOutputPath(container));

    WindowRecordingStatus status;
    std::string error;
    if (!_mainWindow->windowRecorder()->start(_mainWindow->windowRecorderScreen(), _activeEmulator, request, status,
                                              error))
    {
        QMessageBox::critical(this, tr("Recording Failed"), QString::fromStdString(error));
        return false;
    }

    _windowRecording = true;
    _isRecording = true;
    _isPaused = false;
    _recordBtn->setText(tr("Stop Rec"));
    _pauseBtn->setEnabled(false);
    _formatCombo->setEnabled(false);
    _sourceCombo->setEnabled(false);
    _qualityCombo->setEnabled(false);
    _regionCombo->setEnabled(false);
    _profileCombo->setEnabled(false);
    _statsTimer->start();
    onUpdateStats();
    emit recordingStateChanged(true);
    return true;
}

void RecordingWidget::stopWindowRecording()
{
    // Never waits: the file is finished by the recorder's thread; the stats timer sees when it is done
    WindowRecordingStatus status;
    std::string error;
    if (!_mainWindow->windowRecorder()->stop(status, error))
    {
        QMessageBox::warning(this, tr("Recording Error"), QString::fromStdString(error));
        finishRecordingUi();
        return;
    }
    _recordBtn->setEnabled(false);
    _finishingTimer.start();
    _finishingStartPending = 0;
    _finishingPath = QString::fromStdString(status.path);
    _statusLabel->setText(tr("Finishing the file..."));
    if (!_statsTimer->isActive())
        _statsTimer->start();
}

void RecordingWidget::finishRecordingUi()
{
    _windowRecording = false;
    _isRecording = false;
    _isPaused = false;
    _statsTimer->stop();
    _recordBtn->setEnabled(true);
    _recordBtn->setText(tr("Start Rec"));
    _pauseBtn->setEnabled(false);
    _pauseBtn->setText(tr("Pause"));
    _formatCombo->setEnabled(true);
    _qualityCombo->setEnabled(true);
    updateRegionAvailability();
    updateStatusLabel();
    emit recordingStateChanged(false);
}

void RecordingWidget::onUpdateStats()
{
    if (_windowRecording)
    {
        WindowRecordingStatus status;
        _mainWindow->windowRecorder()->status(status);
        if (status.finalizing)
        {
            const qint64 seconds = _finishingTimer.isValid() ? _finishingTimer.elapsed() / 1000 : 0;
            if (_finishingStartPending == 0)
                _finishingStartPending = status.framesPending;
            const double done = static_cast<double>(_finishingStartPending - std::min(_finishingStartPending, status.framesPending));
            const double rate = seconds > 2 ? done / static_cast<double>(seconds) : 0.0;  // frames per second
            QString eta;
            if (rate > 0.5)
                eta = tr(", about %1 s more").arg(static_cast<qint64>(static_cast<double>(status.framesPending) / rate));
            _statusLabel->setText(tr("Finishing the file: %1 frames left%2 (%3 s)")
                                      .arg(status.framesPending).arg(eta).arg(seconds));
            return;
        }
        if (!status.recording)  // finished (stopped here, or the window closed / ffmpeg died)
        {
            const QString savedPath = _finishingPath;
            if (!status.error.empty())
                QMessageBox::warning(this, tr("Recording Error"), QString::fromStdString(status.error));
            finishRecordingUi();
            if (status.error.empty() && !savedPath.isEmpty())
                _statusLabel->setText(tr("Saved %1").arg(savedPath));
            return;
        }
        const int totalSec = status.fps > 0 ? static_cast<int>(static_cast<double>(status.frames) / status.fps) : 0;
        _statusLabel->setText(tr("REC %1:%2:%3 | %4 frames | %5x%6 | window")
                                  .arg(totalSec / 3600, 2, 10, QChar('0'))
                                  .arg((totalSec % 3600) / 60, 2, 10, QChar('0'))
                                  .arg(totalSec % 60, 2, 10, QChar('0'))
                                  .arg(status.frames)
                                  .arg(status.width)
                                  .arg(status.height));
        return;
    }

    EmulatorContext* context = getActiveContext();
    if (!context || !context->pRecordingManager)
        return;

    auto* rm = context->pRecordingManager;
    if (!rm->IsRecording() && !rm->IsPaused())
    {
        _isRecording = false;
        _isPaused = false;
        _statsTimer->stop();
        _recordBtn->setText(tr("Start Rec"));
        _pauseBtn->setEnabled(false);
        _pauseBtn->setText(tr("Pause"));
        _formatCombo->setEnabled(true);
        _qualityCombo->setEnabled(true);
        updateRegionAvailability();
        updateStatusLabel();
        emit recordingStateChanged(false);
        return;
    }

    auto stats = rm->GetStats();
    const int totalSec = static_cast<int>(stats.recordedDuration);
    const int hh = totalSec / 3600;
    const int mm = (totalSec % 3600) / 60;
    const int ss = totalSec % 60;
    const QString timeStr = QStringLiteral("%1:%2:%3")
        .arg(hh, 2, 10, QChar('0'))
        .arg(mm, 2, 10, QChar('0'))
        .arg(ss, 2, 10, QChar('0'));

    const double sizeMb = static_cast<double>(stats.outputFileSize) / (1024.0 * 1024.0);

    if (rm->IsPaused())
    {
        _statusLabel->setText(tr("PAUSED %1 | %2 frames | %3 MB")
            .arg(timeStr)
            .arg(stats.framesRecorded)
            .arg(sizeMb, 0, 'f', 1));
    }
    else
    {
        _statusLabel->setText(tr("REC %1 | %2 frames | %3 MB | %4 FPS")
            .arg(timeStr)
            .arg(stats.framesRecorded)
            .arg(sizeMb, 0, 'f', 1)
            .arg(stats.recentFps, 0, 'f', 1));
    }
}
