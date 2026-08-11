#include "videowall/VideowallRecordingWidget.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QDebug>

#include "ffmpeg_probe.h"
#include "platform_encoder.h"

namespace
{
constexpr const char* kDefaultFilenamePattern = "videowall_recording";

void repopulateCombo(QComboBox* combo, const QStringList& items)
{
    const QString prev = combo->currentText();
    combo->blockSignals(true);
    combo->clear();
    combo->addItems(items);
    int idx = combo->findText(prev);
    if (idx >= 0)
        combo->setCurrentIndex(idx);
    else if (!items.isEmpty())
        combo->setCurrentIndex(0);
    combo->blockSignals(false);
}
}  // namespace

VideowallRecordingWidget::VideowallRecordingWidget(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("Video Wall Recording");
    setMinimumWidth(560);

    createUI();
    connectSignals();

    _statsTimer = new QTimer(this);
    connect(_statsTimer, &QTimer::timeout, this, &VideowallRecordingWidget::onUpdateStats);

    startAsyncDetection();
}

VideowallRecordingWidget::~VideowallRecordingWidget()
{
}

void VideowallRecordingWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateRecordingControls();
    if (VideowallRecorder::instance().isRecording())
    {
        _statsTimer->start(250);
    }
}

void VideowallRecordingWidget::hideEvent(QHideEvent* event)
{
    _statsTimer->stop();
    QWidget::hideEvent(event);
}

void VideowallRecordingWidget::createUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    _tabWidget = new QTabWidget();
    createVideoTab();
    createAudioTab();
    mainLayout->addWidget(_tabWidget);

    // === Recording Controls & Live Statistics ===
    auto* controlsGroup = new QGroupBox("Controls & Statistics");
    auto* controlsLayout = new QVBoxLayout(controlsGroup);

    auto* buttonLayout = new QHBoxLayout();
    _startButton = new QPushButton("🔴 Start Recording");
    _startButton->setStyleSheet("font-weight: bold; background-color: #d9534f; color: white; padding: 6px 12px;");

    _pauseButton = new QPushButton("⏸ Pause");
    _resumeButton = new QPushButton("▶ Resume");
    _stopButton = new QPushButton("⏹ Stop");

    buttonLayout->addWidget(_startButton);
    buttonLayout->addWidget(_pauseButton);
    buttonLayout->addWidget(_resumeButton);
    buttonLayout->addWidget(_stopButton);
    controlsLayout->addLayout(buttonLayout);

    // Statistics labels
    auto* statsLayout = new QHBoxLayout();
    _statusLabel = new QLabel("Ready");
    _statusLabel->setStyleSheet("font-weight: bold; color: #555;");

    _durationLabel = new QLabel("Duration: 00:00");
    _fileSizeLabel = new QLabel("Size: 0.0 MB");
    _fpsLabel = new QLabel("FPS: 0.0");

    statsLayout->addWidget(_statusLabel);
    statsLayout->addStretch();
    statsLayout->addWidget(_durationLabel);
    statsLayout->addWidget(_fileSizeLabel);
    statsLayout->addWidget(_fpsLabel);
    controlsLayout->addLayout(statsLayout);

    mainLayout->addWidget(controlsGroup);

    updateRecordingControls();
}

void VideowallRecordingWidget::createVideoTab()
{
    auto* videoTab = new QWidget();
    auto* videoLayout = new QVBoxLayout(videoTab);

    // === Encoder Backend ===
    auto* backendGroup = new QGroupBox("Encoder Backend");
    auto* backendLayout = new QVBoxLayout(backendGroup);

    _autoBackendRadio = new QRadioButton("Auto (Recommended - Hardware Native with FFmpeg fallback)");
    _nativeBackendRadio = new QRadioButton("Native (Hardware - VideoToolbox / NVENC)");
    _ffmpegBackendRadio = new QRadioButton("FFmpeg Pipe");
    _autoBackendRadio->setChecked(true);

    _backendGroup = new QButtonGroup(this);
    _backendGroup->addButton(_autoBackendRadio);
    _backendGroup->addButton(_nativeBackendRadio);
    _backendGroup->addButton(_ffmpegBackendRadio);

    backendLayout->addWidget(_autoBackendRadio);
    backendLayout->addWidget(_nativeBackendRadio);
    backendLayout->addWidget(_ffmpegBackendRadio);

    _platformInfoLabel = new QLabel("Detecting encoders...");
    backendLayout->addWidget(_platformInfoLabel);

    auto* ffmpegLayout = new QHBoxLayout();
    ffmpegLayout->addWidget(new QLabel("FFmpeg:"));
    _ffmpegPathEdit = new QLineEdit();
    _ffmpegPathEdit->setPlaceholderText("Auto-detect");
    ffmpegLayout->addWidget(_ffmpegPathEdit);
    _detectButton = new QPushButton("Detect");
    ffmpegLayout->addWidget(_detectButton);
    backendLayout->addLayout(ffmpegLayout);

    videoLayout->addWidget(backendGroup);

    // === Output Settings ===
    auto* outputGroup = new QGroupBox("Output Settings");
    auto* outputLayout = new QVBoxLayout(outputGroup);

    auto* fileLayout = new QHBoxLayout();
    fileLayout->addWidget(new QLabel("File:"));
    _filePathEdit = new QLineEdit();

    QSettings settings("unreal-ng", "videowall_recording");
    QString videoDir = settings.value("lastVideoDir").toString();
    if (videoDir.isEmpty() || !QDir(videoDir).exists())
    {
        videoDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        if (videoDir.isEmpty())
            videoDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    _filePathEdit->setText(videoDir + "/" + kDefaultFilenamePattern + ".mp4");
    fileLayout->addWidget(_filePathEdit);
    _browseButton = new QPushButton("Browse...");
    fileLayout->addWidget(_browseButton);
    outputLayout->addLayout(fileLayout);

    auto* formatLayout = new QHBoxLayout();
    formatLayout->addWidget(new QLabel("Container:"));
    _containerCombo = new QComboBox();
    formatLayout->addWidget(_containerCombo);

    formatLayout->addWidget(new QLabel("Video Codec:"));
    _videoCodecCombo = new QComboBox();
    formatLayout->addWidget(_videoCodecCombo);

    formatLayout->addWidget(new QLabel("Quality:"));
    _qualityCombo = new QComboBox();
    _qualityCombo->addItems({"Fastest", "Fast", "Medium", "High", "Best"});
    _qualityCombo->setCurrentIndex(2);
    formatLayout->addWidget(_qualityCombo);
    outputLayout->addLayout(formatLayout);

    // Capture resolution choices
    auto* regionLayout = new QHBoxLayout();
    regionLayout->addWidget(new QLabel("Resolution:"));
    _sizeCombo = new QComboBox();
    _sizeCombo->addItems({
        "1× Native Grid (Current Screen)",
        "2× Scaling",
        "3× Scaling",
        "4× Scaling",
        "1080p Full HD (1920×1080)",
        "1440p Quad HD (2560×1440)",
        "4K UHD (3840×2160)"
    });
    _sizeCombo->setCurrentIndex(0);
    _sizeCombo->setToolTip("Resolution of the captured Video Wall buffer.\n4K UHD or integer scaling provides pristine high-resolution video recordings.");
    regionLayout->addWidget(_sizeCombo);
    outputLayout->addLayout(regionLayout);

    // Audio inclusion
    auto* audioLayout = new QHBoxLayout();
    _includeAudioCheck = new QCheckBox("Include Active Tile Audio");
    _includeAudioCheck->setChecked(true);
    audioLayout->addWidget(_includeAudioCheck);

    audioLayout->addWidget(new QLabel("Audio Codec:"));
    _audioCodecCombo = new QComboBox();
    audioLayout->addWidget(_audioCodecCombo);
    outputLayout->addLayout(audioLayout);

    videoLayout->addWidget(outputGroup);
    _tabWidget->addTab(videoTab, "Video + Audio");
}

void VideowallRecordingWidget::createAudioTab()
{
    auto* audioTab = new QWidget();
    auto* audioLayout = new QVBoxLayout(audioTab);

    auto* formatGroup = new QGroupBox("Audio Format (Active Tile PCM)");
    auto* formatLayout = new QVBoxLayout(formatGroup);

    auto* fileLayout = new QHBoxLayout();
    fileLayout->addWidget(new QLabel("File:"));
    _audioFilePathEdit = new QLineEdit();
    QString videoDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    _audioFilePathEdit->setText(videoDir + "/" + kDefaultFilenamePattern + ".wav");
    fileLayout->addWidget(_audioFilePathEdit);
    _audioBrowseButton = new QPushButton("Browse...");
    fileLayout->addWidget(_audioBrowseButton);
    formatLayout->addLayout(fileLayout);

    auto* formatRow = new QHBoxLayout();
    formatRow->addWidget(new QLabel("Format:"));
    _audioFormatCombo = new QComboBox();
    _audioFormatCombo->addItems({"WAV (PCM)", "MP3", "FLAC", "OGG Vorbis"});
    formatRow->addWidget(_audioFormatCombo);

    formatRow->addWidget(new QLabel("Quality:"));
    _audioQualityCombo = new QComboBox();
    _audioQualityCombo->addItems({"128 kbps", "192 kbps", "256 kbps", "320 kbps"});
    _audioQualityCombo->setCurrentIndex(1);
    formatRow->addWidget(_audioQualityCombo);
    formatLayout->addLayout(formatRow);

    audioLayout->addWidget(formatGroup);
    _tabWidget->addTab(audioTab, "Audio Only");
}

void VideowallRecordingWidget::connectSignals()
{
    connect(_autoBackendRadio, &QRadioButton::toggled, this, &VideowallRecordingWidget::onBackendChanged);
    connect(_nativeBackendRadio, &QRadioButton::toggled, this, &VideowallRecordingWidget::onBackendChanged);
    connect(_ffmpegBackendRadio, &QRadioButton::toggled, this, &VideowallRecordingWidget::onBackendChanged);
    connect(_detectButton, &QPushButton::clicked, this, &VideowallRecordingWidget::onDetectFFmpeg);

    connect(_containerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VideowallRecordingWidget::onContainerChanged);
    connect(_browseButton, &QPushButton::clicked, this, &VideowallRecordingWidget::onBrowseFile);
    connect(_includeAudioCheck, &QCheckBox::checkStateChanged, this, &VideowallRecordingWidget::onIncludeAudioChanged);

    connect(_audioBrowseButton, &QPushButton::clicked, this, [this]() {
        QString filter = "Audio Files (*.wav *.mp3 *.flac *.ogg);;All Files (*)";
        QString filename = QFileDialog::getSaveFileName(this, "Save Audio Recording", _audioFilePathEdit->text(), filter);
        if (!filename.isEmpty())
            _audioFilePathEdit->setText(filename);
    });

    connect(_startButton, &QPushButton::clicked, this, &VideowallRecordingWidget::onStartRecording);
    connect(_pauseButton, &QPushButton::clicked, this, &VideowallRecordingWidget::onPauseRecording);
    connect(_resumeButton, &QPushButton::clicked, this, &VideowallRecordingWidget::onResumeRecording);
    connect(_stopButton, &QPushButton::clicked, this, &VideowallRecordingWidget::onStopRecording);
}

void VideowallRecordingWidget::startAsyncDetection()
{
    std::thread([this]() {
        bool nativeAvailable = PlatformEncoderFactory::isNativeAvailable();
        std::string nativeName;
        if (nativeAvailable)
            nativeName = PlatformEncoderFactory::getNativeDisplayName();

        std::string ffmpegPath = FFmpegProbe::findFFmpeg();

        QMetaObject::invokeMethod(qApp, [this, nativeAvailable, nativeName, ffmpegPath]() {
            _nativeAvailable = nativeAvailable;
            if (nativeAvailable)
            {
                QString name = QString::fromStdString(nativeName);
                _platformInfoLabel->setText("✓ " + name);
                _platformInfoLabel->setStyleSheet("color: green; font-weight: bold;");
                _nativeBackendRadio->setEnabled(true);
                _nativeBackendRadio->setText("Native (" + name + ")");
            }
            else
            {
                _platformInfoLabel->setText("✗ No native hardware encoder available");
                _platformInfoLabel->setStyleSheet("color: gray;");
                _nativeBackendRadio->setEnabled(false);
            }

            _ffmpegAvailable = !ffmpegPath.empty();
            if (_ffmpegAvailable)
            {
                _ffmpegPathEdit->setText(QString::fromStdString(ffmpegPath));
                _ffmpegPathEdit->setStyleSheet("color: green;");
            }
            else
            {
                _ffmpegPathEdit->setPlaceholderText("FFmpeg not found");
                _ffmpegPathEdit->setStyleSheet("color: red;");
            }
            _ffmpegBackendRadio->setEnabled(_ffmpegAvailable);

            populateContainers();
        }, Qt::QueuedConnection);
    }).detach();
}

void VideowallRecordingWidget::onDetectFFmpeg()
{
    startAsyncDetection();
}

void VideowallRecordingWidget::onBackendChanged()
{
    populateContainers();
}

void VideowallRecordingWidget::onContainerChanged(int index)
{
    Q_UNUSED(index)
    populateCodecCombos();
    populateAudioCodecs();
    updateFileExtension();
}

QString VideowallRecordingWidget::extensionForContainer(const QString& container)
{
    return container.toLower();
}

void VideowallRecordingWidget::updateFileExtension()
{
    QString newExt = extensionForContainer(_containerCombo->currentText());
    QString path = _filePathEdit->text();
    if (!path.isEmpty())
    {
        QFileInfo fi(path);
        QString base = fi.absolutePath() + "/" + fi.completeBaseName();
        _filePathEdit->setText(base + "." + newExt);
    }
}

void VideowallRecordingWidget::onBrowseFile()
{
    QString ext = extensionForContainer(_containerCombo->currentText());
    QString filter = QString("%1 Files (*.%2)").arg(_containerCombo->currentText()).arg(ext);
    QString path = QFileDialog::getSaveFileName(this, "Select Output File", _filePathEdit->text(), filter);
    if (!path.isEmpty())
    {
        _filePathEdit->setText(path);
    }
}

void VideowallRecordingWidget::onIncludeAudioChanged(int state)
{
    _audioCodecCombo->setEnabled(state == Qt::Checked);
}

bool VideowallRecordingWidget::isAudioOnlyMode() const
{
    return _tabWidget->currentIndex() == 1;
}

void VideowallRecordingWidget::getTargetDimensions(uint32_t& width, uint32_t& height) const
{
    width = 0;
    height = 0;

    int idx = _sizeCombo->currentIndex();
    if (idx == 4)  // 1080p
    {
        width = 1920;
        height = 1080;
    }
    else if (idx == 5)  // 1440p
    {
        width = 2560;
        height = 1440;
    }
    else if (idx == 6)  // 4K UHD
    {
        width = 3840;
        height = 2160;
    }
    // Note: 1x, 2x, 3x, 4x scaling are handled dynamically based on native Qt widget size
}

QStringList VideowallRecordingWidget::containersForBackend() const
{
    const QStringList nativeContainers = {"MP4", "MOV", "GIF"};
    QStringList ffmpegContainers = {"MP4", "MKV", "MOV", "WebM", "AVI", "GIF"};

    if (_nativeBackendRadio->isChecked())
        return nativeContainers;
    if (_ffmpegBackendRadio->isChecked())
        return ffmpegContainers;

    return _ffmpegAvailable ? ffmpegContainers : nativeContainers;
}

QStringList VideowallRecordingWidget::videoCodecsFor(const QString& container) const
{
    const QString c = container.toUpper();
    if (c == "GIF") return {"GIF"};

    if (_nativeBackendRadio->isChecked())
    {
        return {"H.264", "H.265 (HEVC)"};
    }

    if (c == "MP4" || c == "MOV") return {"H.264", "H.265 (HEVC)", "AV1"};
    if (c == "MKV") return {"H.264", "H.265 (HEVC)", "VP9", "AV1"};
    if (c == "WEBM") return {"VP9", "VP8", "AV1"};
    if (c == "AVI") return {"H.264"};

    return {"H.264"};
}

QStringList VideowallRecordingWidget::audioCodecsFor(const QString& container) const
{
    const QString c = container.toUpper();
    if (c == "GIF") return {};
    if (c == "MP4") return {"AAC", "MP3"};
    if (c == "MOV") return {"AAC", "PCM"};
    if (c == "MKV") return {"AAC", "MP3", "FLAC", "PCM", "Opus"};
    if (c == "WEBM") return {"Opus", "Vorbis"};
    return {"AAC"};
}

void VideowallRecordingWidget::populateContainers()
{
    repopulateCombo(_containerCombo, containersForBackend());
    populateCodecCombos();
    populateAudioCodecs();
    updateFileExtension();
}

void VideowallRecordingWidget::populateCodecCombos()
{
    QStringList codecs = videoCodecsFor(_containerCombo->currentText());
    repopulateCombo(_videoCodecCombo, codecs);
    _videoCodecCombo->setEnabled(codecs.size() > 1);
}

void VideowallRecordingWidget::populateAudioCodecs()
{
    QStringList codecs = audioCodecsFor(_containerCombo->currentText());
    repopulateCombo(_audioCodecCombo, codecs);
    bool audioSupported = !codecs.isEmpty();
    _includeAudioCheck->setEnabled(audioSupported);
    _audioCodecCombo->setEnabled(audioSupported && _includeAudioCheck->isChecked());
}

void VideowallRecordingWidget::onStartRecording()
{
    QString filename = isAudioOnlyMode() ? _audioFilePathEdit->text() : _filePathEdit->text();
    if (filename.isEmpty())
    {
        QMessageBox::warning(this, "Recording Error", "Please specify an output filename.");
        return;
    }

    // Save directory setting
    QFileInfo fi(filename);
    QSettings settings("unreal-ng", "videowall_recording");
    settings.setValue("lastVideoDir", fi.absolutePath());

    std::string videoCodec = "";
    std::string audioCodec = "aac";
    uint32_t targetWidth = 0;
    uint32_t targetHeight = 0;

    if (!isAudioOnlyMode())
    {
        QString vc = _videoCodecCombo->currentText().split(" ")[0].toLower();
        if (vc == "h.264") vc = "h264";
        if (vc == "h.265") vc = "h265";
        videoCodec = vc.toStdString();

        if (_includeAudioCheck->isChecked())
        {
            audioCodec = _audioCodecCombo->currentText().toLower().toStdString();
        }
        else
        {
            audioCodec = "";
        }

        getTargetDimensions(targetWidth, targetHeight);
    }
    else
    {
        audioCodec = _audioFormatCombo->currentText().split(" ")[0].toLower().toStdString();
    }

    bool ok = VideowallRecorder::instance().startRecording(
        filename.toStdString(), videoCodec, audioCodec, 0, 0, targetWidth, targetHeight);

    if (ok)
    {
        updateRecordingControls();
        _statsTimer->start(250);
    }
    else
    {
        QString err = QString::fromStdString(VideowallRecorder::instance().recordingManager()->GetLastRecordingError());
        QMessageBox::critical(this, "Recording Failed", "Failed to start recording:\n" + err);
    }
}

void VideowallRecordingWidget::onPauseRecording()
{
    VideowallRecorder::instance().pauseRecording();
    updateRecordingControls();
}

void VideowallRecordingWidget::onResumeRecording()
{
    VideowallRecorder::instance().resumeRecording();
    updateRecordingControls();
}

void VideowallRecordingWidget::onStopRecording()
{
    _statsTimer->stop();
    VideowallRecorder::instance().stopRecording();
    updateRecordingControls();
}

void VideowallRecordingWidget::onUpdateStats()
{
    if (!VideowallRecorder::instance().isRecording() && !VideowallRecorder::instance().isPaused())
    {
        _statsTimer->stop();
        updateRecordingControls();
        return;
    }

    auto stats = VideowallRecorder::instance().getStats();

    int totalSecs = static_cast<int>(stats.recordedDuration);
    int mins = totalSecs / 60;
    int secs = totalSecs % 60;
    _durationLabel->setText(QString("Duration: %1:%2")
                                .arg(mins, 2, 10, QChar('0'))
                                .arg(secs, 2, 10, QChar('0')));

    double sizeMB = static_cast<double>(stats.outputFileSize) / (1024.0 * 1024.0);
    _fileSizeLabel->setText(QString("Size: %1 MB").arg(sizeMB, 0, 'f', 1));

    _fpsLabel->setText(QString("FPS: %1").arg(stats.recentFps, 0, 'f', 1));
}

void VideowallRecordingWidget::updateRecordingControls()
{
    bool recording = VideowallRecorder::instance().isRecording();
    bool paused = VideowallRecorder::instance().isPaused();

    _startButton->setEnabled(!recording && !paused);
    _pauseButton->setEnabled(recording && !paused);
    _resumeButton->setEnabled(paused);
    _stopButton->setEnabled(recording || paused);

    _tabWidget->setEnabled(!recording && !paused);

    if (paused)
    {
        _statusLabel->setText("PAUSED");
        _statusLabel->setStyleSheet("font-weight: bold; color: orange;");
    }
    else if (recording)
    {
        _statusLabel->setText("● RECORDING");
        _statusLabel->setStyleSheet("font-weight: bold; color: red;");
    }
    else
    {
        _statusLabel->setText("Ready");
        _statusLabel->setStyleSheet("font-weight: bold; color: #555;");
    }
}

#include "moc_VideowallRecordingWidget.cpp"
