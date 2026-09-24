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

    _backendCombo = new QComboBox(this);
    _backendCombo->setToolTip(tr("Encoder Engine / Backend"));
    _backendCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _backendCombo->setFixedWidth(82);
    connect(_backendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordingWidget::onBackendChanged);

    _containerCombo = new QComboBox(this);
    _containerCombo->setToolTip(tr("Output Container / Format"));
    _containerCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _containerCombo->setFixedWidth(74);
    connect(_containerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordingWidget::onContainerChanged);

    _qualityCombo = new QComboBox(this);
    _qualityCombo->setToolTip(tr("Quality / Bitrate Setting"));
    _qualityCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _qualityCombo->setFixedWidth(88);
    connect(_qualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordingWidget::onQualityChanged);

    _regionCombo = new QComboBox(this);
    _regionCombo->addItem(tr("Full"), 0);
    _regionCombo->addItem(tr("Screen"), 1);
    _regionCombo->setToolTip(tr("Capture Region: Full Frame (with border) vs Screen Only (256x192)"));
    _regionCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _regionCombo->setFixedWidth(68);
    connect(_regionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordingWidget::onRegionChanged);

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
    controlLayout->addWidget(_backendCombo, 0, Qt::AlignVCenter);
    controlLayout->addWidget(_containerCombo, 0, Qt::AlignVCenter);
    controlLayout->addWidget(_qualityCombo, 0, Qt::AlignVCenter);
    controlLayout->addWidget(_regionCombo, 0, Qt::AlignVCenter);
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
            _containerCombo->setEnabled(false);
            _backendCombo->setEnabled(false);
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
            _containerCombo->setEnabled(true);
            _backendCombo->setEnabled(true);
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

void RecordingWidget::populateBackends()
{
    _backendCombo->blockSignals(true);
    _backendCombo->clear();

    _backendCombo->addItem(tr("Auto"), static_cast<int>(EncoderBackend::Auto));

    if (PlatformEncoderFactory::isNativeAvailable())
    {
        std::string nativeName = PlatformEncoderFactory::getNativeDisplayName();
        QString desc = nativeName.empty() ? tr("Native") : tr("Native (%1)").arg(QString::fromStdString(nativeName));
        _backendCombo->addItem(tr("Native"), static_cast<int>(EncoderBackend::Native));
        _backendCombo->setItemData(_backendCombo->count() - 1, desc, Qt::ToolTipRole);
    }

    if (!FFmpegProbe::findFFmpeg().empty())
    {
        _backendCombo->addItem(tr("FFmpeg"), static_cast<int>(EncoderBackend::FFmpeg));
        _backendCombo->setItemData(_backendCombo->count() - 1, tr("FFmpeg"), Qt::ToolTipRole);
    }

    int matchIdx = -1;
    for (int i = 0; i < _backendCombo->count(); ++i)
    {
        if (_backendCombo->itemData(i).toInt() == _settings.backend)
        {
            matchIdx = i;
            break;
        }
    }
    if (matchIdx >= 0)
    {
        _backendCombo->setCurrentIndex(matchIdx);
    }
    else
    {
        _backendCombo->setCurrentIndex(0);
        _settings.backend = _backendCombo->itemData(0).toInt();
    }
    _backendCombo->blockSignals(false);
    updateBackendTooltip();
}

void RecordingWidget::updateBackendTooltip()
{
    QString desc;
    int currentBackend = _backendCombo->currentData().toInt();
    if (currentBackend == static_cast<int>(EncoderBackend::Native))
    {
        std::string nativeName = PlatformEncoderFactory::getNativeDisplayName();
        desc = nativeName.empty() ? tr("Native") : tr("Native (%1)").arg(QString::fromStdString(nativeName));
    }
    else if (currentBackend == static_cast<int>(EncoderBackend::FFmpeg))
    {
        desc = tr("FFmpeg");
    }
    else
    {
        desc = tr("Auto (best available)");
    }
    _backendCombo->setToolTip(tr("Encoder Engine: %1").arg(desc));
}

void RecordingWidget::populateContainers()
{
    _containerCombo->blockSignals(true);
    QString currentCont = _settings.container.toUpper();
    _containerCombo->clear();

    EncoderBackend backend = static_cast<EncoderBackend>(_backendCombo->currentData().toInt());

    QStringList videoFormats;
    if (backend == EncoderBackend::Native)
    {
        videoFormats = {QStringLiteral("MP4"), QStringLiteral("MOV"), QStringLiteral("GIF")};
    }
    else if (backend == EncoderBackend::FFmpeg)
    {
        videoFormats = {QStringLiteral("MP4"), QStringLiteral("MKV"), QStringLiteral("MOV"),
                        QStringLiteral("WebM"), QStringLiteral("GIF")};
        std::string ffmpegPath = FFmpegProbe::findFFmpeg();
        if (!ffmpegPath.empty() && (FFmpegProbe::isEncoderAvailable("libwebp_anim", ffmpegPath) ||
                                    FFmpegProbe::isEncoderAvailable("libwebp", ffmpegPath)))
        {
            videoFormats << QStringLiteral("WebP");
        }
    }
    else // Auto
    {
        videoFormats = {QStringLiteral("MP4"), QStringLiteral("MOV"), QStringLiteral("MKV"),
                        QStringLiteral("WebM"), QStringLiteral("GIF")};
    }

    for (const QString& fmt : videoFormats)
    {
        _containerCombo->addItem(fmt, fmt);
    }

    // Visual splitter separating video from audio options
    _containerCombo->insertSeparator(_containerCombo->count());

    QStringList audioFormats = {QStringLiteral("WAV"), QStringLiteral("MP3"), QStringLiteral("FLAC")};
    for (const QString& fmt : audioFormats)
    {
        _containerCombo->addItem(fmt, fmt);
    }

    int matchIdx = _containerCombo->findData(currentCont);
    if (matchIdx >= 0)
    {
        _containerCombo->setCurrentIndex(matchIdx);
    }
    else
    {
        _containerCombo->setCurrentIndex(0);
        _settings.container = _containerCombo->currentText();
    }
    _containerCombo->blockSignals(false);

    updateQualityOptions();
    updateRegionAvailability();
}

void RecordingWidget::updateQualityOptions()
{
    _qualityCombo->blockSignals(true);
    _qualityCombo->clear();

    const QString cont = _containerCombo->currentText().toUpper();

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
    const QString cont = _containerCombo->currentText().toUpper();
    const bool isAudio = (cont == QStringLiteral("WAV") || cont == QStringLiteral("MP3") || cont == QStringLiteral("FLAC"));

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
    populateBackends();
    populateContainers();

    if (_settings.captureRegion >= 0 && _settings.captureRegion < _regionCombo->count())
        _regionCombo->setCurrentIndex(_settings.captureRegion);
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
        _statusLabel->setText(tr("Ready | %1 | %2 | %3").arg(cont, qualityStr, regionStr));
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
        _containerCombo->setEnabled(true);
        _backendCombo->setEnabled(true);
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

        EncoderBackend backend = EncoderBackend::Auto;
        if (_settings.backend == 1) backend = EncoderBackend::Native;
        else if (_settings.backend == 2) backend = EncoderBackend::FFmpeg;
        rm->SetEncoderBackend(backend);

        int presetVal = (_settings.quality == 0) ? 0 : (_settings.quality == 1) ? 2 :
                        (_settings.quality == 2) ? 5 : (_settings.quality == 3) ? 8 : 10;
        rm->SetQualityPreset(presetVal);
        rm->SetCaptureRegion(_settings.captureRegion == 1 ? VideoCaptureRegion::MainScreen : VideoCaptureRegion::FullFrame);
        rm->SetScaleFactor(_settings.scaleFactor);

        const QString cont = _settings.container.toUpper();
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
            if (cont == "GIF")
            {
                videoCodec = QStringLiteral("gif");
            }
            else if (cont == "WEBM")
            {
                videoCodec = QStringLiteral("vp9");
                audioCodec = _settings.includeAudio ? QStringLiteral("opus") : QString();
            }
            else // MP4, MOV, MKV
            {
                videoCodec = QStringLiteral("h264");
                audioCodec = _settings.includeAudio ? QStringLiteral("aac") : QString();
            }
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
        _containerCombo->setEnabled(false);
        _backendCombo->setEnabled(false);
        _qualityCombo->setEnabled(false);
        _regionCombo->setEnabled(false);
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

void RecordingWidget::onContainerChanged(int index)
{
    Q_UNUSED(index);
    _settings.container = _containerCombo->currentText();
    _settings.save();
    updateQualityOptions();
    updateRegionAvailability();
    updateStatusLabel();
}

void RecordingWidget::onBackendChanged(int index)
{
    Q_UNUSED(index);
    _settings.backend = _backendCombo->currentData().toInt();
    _settings.save();
    updateBackendTooltip();
    populateContainers();
    updateStatusLabel();
}

void RecordingWidget::onQualityChanged(int index)
{
    Q_UNUSED(index);
    const QString cont = _containerCombo->currentText().toUpper();
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
        _containerCombo->setEnabled(true);
        _backendCombo->setEnabled(true);
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
