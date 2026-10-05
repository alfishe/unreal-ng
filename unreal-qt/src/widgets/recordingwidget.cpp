#include "widgets/recordingwidget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QStyleOptionButton>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QMessageBox>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "base/featuremanager.h"
#include "mainwindow.h"
#include "recording/src/recordingmanager.h"
#include "platform_encoder.h"
#include "ffmpeg_probe.h"
#include "widgets/tintedsvgicon.h"

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
    _formatCombo->setToolTip(tr("Format: video codec and container (H.265 by default), GIF, or audio only"));
    _formatCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _formatCombo->setFixedWidth(112);
    connect(_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordingWidget::onFormatChanged);

    _qualityCombo = new QComboBox(this);
    _qualityCombo->setToolTip(tr("Quality / Bitrate Setting"));
    _qualityCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _qualityCombo->setFixedWidth(80);
    connect(_qualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordingWidget::onQualityChanged);

    _regionCombo = new QComboBox(this);
    _regionCombo->addItem(tr("Full"), 0);
    _regionCombo->addItem(tr("Screen"), 1);
    _regionCombo->setToolTip(tr("Capture Region: Full Frame (with border) vs Screen Only (256x192)"));
    _regionCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _regionCombo->setFixedWidth(64);
    connect(_regionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordingWidget::onRegionChanged);

    _profileCombo = new QComboBox(this);
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

void RecordingWidget::populateFormats()
{
    _formatCombo->blockSignals(true);
    _formatCombo->clear();

    // The engine is chosen in the full recording dialog (Tools > Video Recording...) and shared by every
    // recording UI: it only decides which formats exist here
    const EncoderBackend backend = static_cast<EncoderBackend>(_settings.backend);
    const bool nativeOnly = backend == EncoderBackend::Native;

    struct Format
    {
        const char* label;
        const char* container;
        const char* codec;
        bool native;  // the native encoders write it (mp4 / mov / gif)
    };
    const Format video[] = {
        {"H.265 · MP4", "MP4", "h265", true},  {"H.264 · MP4", "MP4", "h264", true},
        {"H.265 · MKV", "MKV", "h265", false}, {"H.264 · MKV", "MKV", "h264", false},
        {"H.265 · MOV", "MOV", "h265", true},  {"H.264 · MOV", "MOV", "h264", true},
        {"VP9 · WebM", "WebM", "vp9", false},  {"GIF", "GIF", "gif", true},
    };
    for (const Format& f : video)
    {
        if (nativeOnly && !f.native)
            continue;
        _formatCombo->addItem(QString::fromUtf8(f.label), QStringLiteral("%1|%2").arg(f.container, f.codec));
    }
    if (!nativeOnly)
    {
        const std::string ffmpegPath = FFmpegProbe::findFFmpeg();
        if (!ffmpegPath.empty() && (FFmpegProbe::isEncoderAvailable("libwebp_anim", ffmpegPath) ||
                                    FFmpegProbe::isEncoderAvailable("libwebp", ffmpegPath)))
            _formatCombo->addItem(QStringLiteral("WebP"), QStringLiteral("WebP|webp"));
    }

    // Audio only
    _formatCombo->insertSeparator(_formatCombo->count());
    for (const char* audio : {"WAV", "MP3", "FLAC"})
        _formatCombo->addItem(QString::fromLatin1(audio), QStringLiteral("%1|").arg(QString::fromLatin1(audio)));

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
    const bool isAudio = (cont == QStringLiteral("WAV") || cont == QStringLiteral("MP3") || cont == QStringLiteral("FLAC"));

    // The profiles are H.264 / H.265 frames: the other formats (VP9, GIF, WebP, audio) have no fixed size
    const QString codec = currentCodec();
    const bool profileFormat = (codec == QStringLiteral("h264") || codec == QStringLiteral("h265"));
    _profileCombo->setEnabled(profileFormat && !_isRecording);
    _profileCombo->setToolTip(profileFormat
        ? tr("Output size: Original (the picture times the scale factor) or a fixed 1080p / 1440p / 4K frame. The "
             "picture is fitted into it (aspect kept, nearest neighbor, black bars, no blur)")
        : tr("Fixed output sizes need an H.264 / H.265 format"));

    if (isAudio)
    {
        _regionCombo->setEnabled(false);
        _regionCombo->setToolTip(tr("Capture region applies to video/animation recording only"));
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
        const QString regionStr = (_settings.captureRegion == 1) ? tr("Screen") : tr("Full");
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

        int presetVal = (_settings.quality == 0) ? 0 : (_settings.quality == 1) ? 2 :
                        (_settings.quality == 2) ? 5 : (_settings.quality == 3) ? 8 : 10;
        rm->SetQualityPreset(presetVal);
        rm->SetCaptureRegion(_settings.captureRegion == 1 ? VideoCaptureRegion::MainScreen : VideoCaptureRegion::FullFrame);
        rm->SetScaleFactor(_settings.scaleFactor);
        const QString codecChoice = currentCodec();
        const bool fixedSizeFormat = codecChoice == QStringLiteral("h264") || codecChoice == QStringLiteral("h265");
        rm->SetOutputProfile(fixedSizeFormat ? _settings.profile.toStdString() : std::string("native"));
        // No switch of its own: recording follows View > GPU acceleration (on = a GPU encoder when there is one)
        rm->SetEncoderAcceleration(UiGpuAcceleration() ? EncoderAcceleration::Auto : EncoderAcceleration::Software);

        const QString cont = currentContainer();
        const bool isAudioOnly = (cont == "WAV" || cont == "MP3" || cont == "FLAC");
        rm->SetVideoEnabled(!isAudioOnly);

        QString videoCodec;
        QString audioCodec;

        if (isAudioOnly)
        {
            if (cont == "WAV") audioCodec = QStringLiteral("pcm_s16le");
            else if (cont == "MP3") audioCodec = QStringLiteral("mp3");
            else if (cont == "FLAC") audioCodec = QStringLiteral("flac");
            else audioCodec = QStringLiteral("pcm_s16le");
        }
        else
        {
            // The format's own codec; GIF and WebP carry no sound, WebM holds Opus, the rest AAC
            videoCodec = codecChoice;
            if (cont == "GIF" || cont == "WEBP")
                audioCodec.clear();
            else if (cont == "WEBM")
                audioCodec = _settings.includeAudio ? QStringLiteral("opus") : QString();
            else // MP4, MOV, MKV
                audioCodec = _settings.includeAudio ? QStringLiteral("aac") : QString();
        }

        const QString filename = generateOutputPath(cont);
        const uint32_t aBitrate = (cont == "MP3") ? static_cast<uint32_t>(_settings.audioBitrate) : 0;
        bool ok = false;
        if (isAudioOnly)
        {
            ok = rm->StartRecording(filename.toStdString(), "", audioCodec.toStdString(), 0, aBitrate);
        }
        else
        {
            ok = rm->StartRecording(filename.toStdString(), videoCodec.toStdString(), audioCodec.toStdString());
        }

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
    _settings.save();
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
    _settings.save();
    updateStatusLabel();
}

void RecordingWidget::onRegionChanged(int index)
{
    _settings.captureRegion = _regionCombo->currentData().toInt();
    _settings.save();
    updateStatusLabel();
}

void RecordingWidget::onProfileChanged(int)
{
    _settings.profile = _profileCombo->currentData().toString();
    _settings.save();
    updateStatusLabel();
}

void RecordingWidget::onUpdateStats()
{
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
