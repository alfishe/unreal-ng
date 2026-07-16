#include "videorecordingwidget.h"

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QDir>

#include <QPointer>

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#include "emulator/recording/encoders/gif_encoder.h"
#include "emulator/recording/encoders/ffmpeg_pipe_encoder.h"
#include "benchmarkfeeder.h"
#include "multitrackdialog.h"

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/video/screen.h"
#include "emulator/recording/recordingmanager.h"
#include "emulator/recording/platform_encoder.h"
#include "emulator/recording/ffmpeg_probe.h"
#include "emulator/recording/realtime_estimator.h"
#include "base/featuremanager.h"

namespace
{
/// Repopulate a combo box, preserving the previous selection when still available.
/// Signals are blocked — callers refresh dependent state explicitly.
void repopulateCombo(QComboBox* combo, const QStringList& items)
{
    const QString prev = combo->currentText();
    combo->blockSignals(true);
    combo->clear();
    combo->addItems(items);
    int idx = combo->findText(prev);
    if (idx >= 0)
        combo->setCurrentIndex(idx);
    combo->blockSignals(false);
}
}  // namespace

VideoRecordingWidget::VideoRecordingWidget(EmulatorContext* context, QWidget* parent)
    : QWidget(parent), _context(context)
{
    setWindowTitle("Video Recording");
    setMinimumWidth(500);

    // Track the emulator ID for staleness detection
    if (_context && _context->pEmulator)
        _contextEmulatorId = _context->pEmulator->GetId();

    createUI();
    connectSignals();
    loadBenchmarkResults();
    refreshFromContext();

    // Stats update timer - display refresh only (auto-stop is via MessageCenter)
    _statsTimer = new QTimer(this);
    _statsTimer->setInterval(250);
    connect(_statsTimer, &QTimer::timeout, this, &VideoRecordingWidget::onUpdateStats);
}

VideoRecordingWidget::~VideoRecordingWidget()
{
    if (_statsTimer)
        _statsTimer->stop();

    // Stop any active recording before destroying the widget
    EmulatorContext* recCtx = recordingContext();
    if (recCtx && recCtx->pRecordingManager && recCtx->pRecordingManager->IsRecording())
    {
        recCtx->pRecordingManager->StopRecording();
    }
    _wasRecording = false;
    _recordingEmulatorId.clear();

    // Ensure recording feature is disabled on the active context
    validateContext();
    if (_context && _context->pFeatureManager)
    {
        _context->pFeatureManager->setFeature(Features::kRecording, false);
    }
}

void VideoRecordingWidget::validateContext()
{
    // The raw _context pointer can dangle after Emulator::Release() destroys
    // the context object. We verify via EmulatorManager that the emulator
    // still exists before trusting the pointer.
    if (!_contextEmulatorId.empty())
    {
        EmulatorManager* mgr = EmulatorManager::GetInstance();
        if (!mgr->HasEmulator(_contextEmulatorId))
        {
            _context = nullptr;
            _contextEmulatorId.clear();
        }
    }
}

EmulatorContext* VideoRecordingWidget::recordingContext() const
{
    // When recording, operate on the emulator we started recording with,
    // not the currently-active one. The user may have switched active
    // emulator while recording continues on the original instance.
    if (!_recordingEmulatorId.empty())
    {
        EmulatorManager* mgr = EmulatorManager::GetInstance();
        auto emu = mgr->GetEmulator(_recordingEmulatorId);
        if (emu)
            return emu->GetContext();
        return nullptr;  // Recording emulator was destroyed
    }
    return _context;
}

void VideoRecordingWidget::setContext(EmulatorContext* context)
{
    // Extract the emulator ID of the new context (for staleness checks)
    std::string newEmulatorId;
    if (context && context->pEmulator)
        newEmulatorId = context->pEmulator->GetId();

    // IMPORTANT: Do NOT stop recording on the old context when switching.
    // Recording tracks the emulator instance it started with and continues
    // until explicit stop or that emulator instance is destroyed.

    // Disable feature on old context (only if we're NOT recording on it)
    bool recordingOnOld = _wasRecording && !_recordingEmulatorId.empty() &&
                          _recordingEmulatorId == _contextEmulatorId;
    if (_context && _contextEmulatorId != newEmulatorId && !recordingOnOld &&
        _context->pFeatureManager)
    {
        _context->pFeatureManager->setFeature(Features::kRecording, false);
    }

    _context = context;
    _contextEmulatorId = newEmulatorId;

    // Enable feature on new context if widget is visible
    if (_context && _context->pFeatureManager && isVisible())
    {
        _context->pFeatureManager->setFeature(Features::kRecording, true);
    }

    // Refresh UI state for new context
    refreshFromContext();
    updateRecordingControls();
}

void VideoRecordingWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // Enable recording feature when dialog is shown
    validateContext();
    if (_context && _context->pFeatureManager)
    {
        if (!_context->pFeatureManager->isEnabled(Features::kRecording))
        {
            _context->pFeatureManager->setFeature(Features::kRecording, true);
        }
    }
}

void VideoRecordingWidget::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);

    // Stop any active recording before disabling the feature.
    // Synchronous here — the window is going away. StopRecording is
    // idempotent and mutex-guarded, so overlapping with an async stop is safe.
    EmulatorContext* recCtx = recordingContext();
    if (recCtx && recCtx->pRecordingManager && recCtx->pRecordingManager->IsRecording())
    {
        recCtx->pRecordingManager->StopRecording();
        _wasRecording = false;
        _recordingEmulatorId.clear();
    }

    // Disable recording feature when dialog is hidden
    validateContext();
    if (_context && _context->pFeatureManager)
    {
        _context->pFeatureManager->setFeature(Features::kRecording, false);
    }
}

void VideoRecordingWidget::createUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    // === Backend Selection ===
    auto* backendGroup = new QGroupBox("Encoder Backend");
    auto* backendLayout = new QVBoxLayout(backendGroup);

    _backendGroup = new QButtonGroup(this);
    _nativeBackendRadio = new QRadioButton("Native (Hardware)");
    _ffmpegBackendRadio = new QRadioButton("FFmpeg");
    _autoBackendRadio = new QRadioButton("Auto (Recommended)");
    _autoBackendRadio->setChecked(true);
    _backendGroup->addButton(_nativeBackendRadio);
    _backendGroup->addButton(_ffmpegBackendRadio);
    _backendGroup->addButton(_autoBackendRadio);

    backendLayout->addWidget(_autoBackendRadio);
    backendLayout->addWidget(_nativeBackendRadio);
    backendLayout->addWidget(_ffmpegBackendRadio);

    _platformInfoLabel = new QLabel();
    backendLayout->addWidget(_platformInfoLabel);

    // FFmpeg path
    auto* ffmpegLayout = new QHBoxLayout();
    ffmpegLayout->addWidget(new QLabel("FFmpeg:"));
    _ffmpegPathEdit = new QLineEdit();
    _ffmpegPathEdit->setPlaceholderText("Auto-detect");
    ffmpegLayout->addWidget(_ffmpegPathEdit);
    _detectButton = new QPushButton("Detect");
    ffmpegLayout->addWidget(_detectButton);
    backendLayout->addLayout(ffmpegLayout);

    mainLayout->addWidget(backendGroup);

    // === Output Settings ===
    auto* outputGroup = new QGroupBox("Output");
    auto* outputLayout = new QVBoxLayout(outputGroup);

    // File path
    auto* fileLayout = new QHBoxLayout();
    fileLayout->addWidget(new QLabel("File:"));
    _filePathEdit = new QLineEdit();
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (defaultDir.isEmpty())
        defaultDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    _filePathEdit->setText(defaultDir + "/recording.mp4");
    fileLayout->addWidget(_filePathEdit);
    _browseButton = new QPushButton("Browse...");
    fileLayout->addWidget(_browseButton);
    outputLayout->addLayout(fileLayout);

    // Container and codec (lists are populated per-backend in populateContainers)
    auto* formatLayout = new QHBoxLayout();
    formatLayout->addWidget(new QLabel("Container:"));
    _containerCombo = new QComboBox();
    formatLayout->addWidget(_containerCombo);

    formatLayout->addWidget(new QLabel("Video:"));
    _videoCodecCombo = new QComboBox();
    formatLayout->addWidget(_videoCodecCombo);

    formatLayout->addWidget(new QLabel("Quality:"));
    _qualityCombo = new QComboBox();
    _qualityCombo->addItems({"Fastest", "Fast", "Medium", "High", "Best"});
    _qualityCombo->setCurrentIndex(2);
    formatLayout->addWidget(_qualityCombo);
    outputLayout->addLayout(formatLayout);

    // Capture region and output size
    auto* regionLayout = new QHBoxLayout();
    regionLayout->addWidget(new QLabel("Capture:"));
    _captureCombo = new QComboBox();
    _captureCombo->addItems({"Full frame (with border)", "Screen only (256×192)"});
    regionLayout->addWidget(_captureCombo);

    regionLayout->addWidget(new QLabel("Size:"));
    _sizeCombo = new QComboBox();
    _sizeCombo->addItems({"1× native", "2× (recommended)", "3×", "4×"});
    _sizeCombo->setCurrentIndex(1);
    _sizeCombo->setToolTip("Integer nearest-neighbor upscale. 2× or more keeps ZX pixels crisp and\n"
                           "preserves per-pixel color (multicolor) effects through video chroma subsampling.");
    regionLayout->addWidget(_sizeCombo);
    outputLayout->addLayout(regionLayout);

    // Audio settings
    auto* audioLayout = new QHBoxLayout();
    _includeAudioCheck = new QCheckBox("Include Audio");
    _includeAudioCheck->setChecked(true);
    audioLayout->addWidget(_includeAudioCheck);

    audioLayout->addWidget(new QLabel("Audio:"));
    _audioCodecCombo = new QComboBox();
    audioLayout->addWidget(_audioCodecCombo);
    _multiTrackButton = new QPushButton("Configure Multi-Track...");
    _multiTrackButton->setEnabled(false);
    _multiTrackButton->setToolTip("Multi-track recording is not yet supported by the available encoders");
    audioLayout->addWidget(_multiTrackButton);
    outputLayout->addLayout(audioLayout);

    mainLayout->addWidget(outputGroup);

    // === Realtime Estimation ===
    auto* estimateGroup = new QGroupBox("Realtime Estimation");
    auto* estimateLayout = new QVBoxLayout(estimateGroup);
    _estimateLabel = new QLabel();
    _estimateLabel->setWordWrap(true);
    _estimateLabel->setStyleSheet("padding: 8px; border-radius: 4px;");
    estimateLayout->addWidget(_estimateLabel);
    mainLayout->addWidget(estimateGroup);

    // === Benchmark ===
    _benchmarkButton = new QPushButton("Benchmark All Encoders");
    _benchmarkButton->setToolTip("Measure actual encoding performance for all available codec/backend combinations (5 seconds each)");
    mainLayout->addWidget(_benchmarkButton);

    // === Recording Controls ===
    auto* controlsLayout = new QHBoxLayout();
    _startButton = new QPushButton("Start");
    _pauseButton = new QPushButton("Pause");
    _resumeButton = new QPushButton("Resume");
    _stopButton = new QPushButton("Stop");
    controlsLayout->addWidget(_startButton);
    controlsLayout->addWidget(_pauseButton);
    controlsLayout->addWidget(_resumeButton);
    controlsLayout->addWidget(_stopButton);
    mainLayout->addLayout(controlsLayout);

    // === Stats (shown during recording) ===
    auto* statsGroup = new QGroupBox("Recording Status");
    auto* statsLayout = new QHBoxLayout(statsGroup);
    _recordingIndicator = new QLabel("IDLE");
    _recordingIndicator->setStyleSheet("font-weight: bold; color: gray;");
    _durationLabel = new QLabel("00:00:00");
    _sizeLabel = new QLabel("0 MB");
    _fpsLabel = new QLabel("0.0 fps");
    statsLayout->addWidget(_recordingIndicator);
    statsLayout->addWidget(new QLabel("Duration:"));
    statsLayout->addWidget(_durationLabel);
    statsLayout->addWidget(new QLabel("Size:"));
    statsLayout->addWidget(_sizeLabel);
    statsLayout->addWidget(new QLabel("FPS:"));
    statsLayout->addWidget(_fpsLabel);
    mainLayout->addWidget(statsGroup);

    // Initial state
    updateRecordingControls();
}

void VideoRecordingWidget::connectSignals()
{
    connect(_detectButton, &QPushButton::clicked, this, &VideoRecordingWidget::onDetectFFmpeg);
    connect(_backendGroup, &QButtonGroup::buttonClicked, this, &VideoRecordingWidget::onBackendChanged);
    connect(_containerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VideoRecordingWidget::onContainerChanged);
    connect(_videoCodecCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateRealtimeEstimate(); });
    connect(_audioCodecCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateRealtimeEstimate(); });
    connect(_browseButton, &QPushButton::clicked, this, &VideoRecordingWidget::onBrowseFile);
    connect(_startButton, &QPushButton::clicked, this, &VideoRecordingWidget::onStartRecording);
    connect(_pauseButton, &QPushButton::clicked, this, &VideoRecordingWidget::onPauseRecording);
    connect(_resumeButton, &QPushButton::clicked, this, &VideoRecordingWidget::onResumeRecording);
    connect(_stopButton, &QPushButton::clicked, this, &VideoRecordingWidget::onStopRecording);
    connect(_includeAudioCheck, &QCheckBox::checkStateChanged, this, &VideoRecordingWidget::onIncludeAudioChanged);
    connect(_multiTrackButton, &QPushButton::clicked, this, &VideoRecordingWidget::onMultiTrackConfigure);
    connect(_benchmarkButton, &QPushButton::clicked, this, &VideoRecordingWidget::onBenchmark);

    _signalsConnected = true;
}

void VideoRecordingWidget::refreshFromContext()
{
    detectPlatformInfo();
    onDetectFFmpeg();  // Also populates containers/codecs and the estimate
}

void VideoRecordingWidget::detectPlatformInfo()
{
    _nativeAvailable = PlatformEncoderFactory::isNativeAvailable();

    if (_nativeAvailable)
    {
        QString name = QString::fromStdString(PlatformEncoderFactory::getNativeDisplayName());
        _platformInfoLabel->setText("✓ " + name);
        _platformInfoLabel->setStyleSheet("color: green; font-weight: bold;");
        _nativeBackendRadio->setEnabled(true);
        _nativeBackendRadio->setText("Native (" + name + ")");
    }
    else
    {
        _platformInfoLabel->setText("✗ No native encoder available on this platform");
        _platformInfoLabel->setStyleSheet("color: gray;");
        _nativeBackendRadio->setEnabled(false);
        _nativeBackendRadio->setText("Native (unavailable)");
    }
}

void VideoRecordingWidget::onDetectFFmpeg()
{
    std::string path = FFmpegProbe::findFFmpeg();
    _ffmpegAvailable = !path.empty();

    if (_ffmpegAvailable)
    {
        _ffmpegPathEdit->setText(QString::fromStdString(path));
        _ffmpegPathEdit->setStyleSheet("color: green;");
        _ffmpegHasWebP = FFmpegProbe::isEncoderAvailable("libwebp_anim", path) ||
                         FFmpegProbe::isEncoderAvailable("libwebp", path);
    }
    else
    {
        _ffmpegPathEdit->setStyleSheet("color: red;");
        _ffmpegHasWebP = false;
    }

    _ffmpegBackendRadio->setEnabled(_ffmpegAvailable);
    if (!_ffmpegAvailable && _ffmpegBackendRadio->isChecked())
        _autoBackendRadio->setChecked(true);

    // Availability affects the whole capability matrix
    populateContainers();
    updateRealtimeEstimate();
}

void VideoRecordingWidget::onBackendChanged()
{
    // Enable/disable FFmpeg path based on backend selection
    bool ffmpegActive = _ffmpegBackendRadio->isChecked() || _autoBackendRadio->isChecked();
    _ffmpegPathEdit->setEnabled(ffmpegActive);
    _detectButton->setEnabled(ffmpegActive);

    // Rebuild container list for this backend
    populateContainers();

    updateRealtimeEstimate();
}

void VideoRecordingWidget::onContainerChanged(int index)
{
    Q_UNUSED(index)
    populateCodecCombos();
    populateAudioCodecs();
    updateFileExtension();
    updateRealtimeEstimate();
}

QString VideoRecordingWidget::extensionForContainer(const QString& container)
{
    // Container display names map 1:1 to lowercase file extensions
    return container.toLower();
}

void VideoRecordingWidget::updateFileExtension()
{
    // The destination path never changes when options change —
    // only the file extension follows the selected container.
    QString path = _filePathEdit->text();
    if (path.isEmpty())
        return;

    QFileInfo fi(path);
    QString base = fi.absolutePath() + "/" + fi.completeBaseName();
    _filePathEdit->setText(base + "." + extensionForContainer(_containerCombo->currentText()));
}

void VideoRecordingWidget::onBrowseFile()
{
    QString ext = extensionForContainer(_containerCombo->currentText());
    QString filter = QString("%1 Files (*.%2)").arg(_containerCombo->currentText()).arg(ext);
    QString path = QFileDialog::getSaveFileName(this, "Select Output File",
                                                  _filePathEdit->text(), filter);
    if (path.isEmpty())
        return;

    QFileInfo fi(path);
    QString chosenExt = fi.suffix().toLower();

    // If the user picked a filename with a known container extension,
    // switch the container to match. Otherwise enforce the current
    // container's extension (keeping the user's base name).
    int containerIdx = -1;
    for (int i = 0; i < _containerCombo->count(); i++)
    {
        if (extensionForContainer(_containerCombo->itemText(i)) == chosenExt)
        {
            containerIdx = i;
            break;
        }
    }

    _filePathEdit->setText(path);

    if (containerIdx >= 0)
    {
        if (containerIdx != _containerCombo->currentIndex())
            _containerCombo->setCurrentIndex(containerIdx);  // Triggers onContainerChanged
    }
    else
    {
        updateFileExtension();
    }
}

QStringList VideoRecordingWidget::containersForBackend() const
{
    // GIF is encoded natively by GIFEncoder and is always available
    const QStringList nativeContainers = {"MP4", "MOV", "GIF"};
    QStringList ffmpegContainers = {"MP4", "MKV", "MOV", "WebM", "AVI", "GIF", "APNG"};
    if (_ffmpegHasWebP)
        ffmpegContainers << "WebP";

    if (_nativeBackendRadio->isChecked())
        return nativeContainers;

    if (_ffmpegBackendRadio->isChecked())
        return ffmpegContainers;

    // Auto: whatever the available backends can produce
    if (_ffmpegAvailable)
        return ffmpegContainers;
    if (_nativeAvailable)
        return nativeContainers;
    return {"GIF"};
}

QStringList VideoRecordingWidget::videoCodecsFor(const QString& container) const
{
    const QString c = container.toUpper();

    // Fixed-codec animation formats
    if (c == "GIF")  return {"GIF"};
    if (c == "APNG") return {"APNG"};
    if (c == "WEBP") return {"WebP"};

    const bool nativeOnly = _nativeBackendRadio->isChecked();
    const bool ffmpegUsable = !nativeOnly && _ffmpegAvailable;

    if (nativeOnly)
        return {"H.264", "H.265 (HEVC)"};  // VideoToolbox: H.264/HEVC in MP4/MOV

    QStringList codecs;
    if (c == "MP4")
    {
        codecs = {"H.264", "H.265 (HEVC)"};
        if (ffmpegUsable)
            codecs << "AV1";
    }
    else if (c == "MOV")
    {
        codecs = {"H.264", "H.265 (HEVC)"};
    }
    else if (c == "MKV")
    {
        codecs = {"H.264", "H.265 (HEVC)", "VP9", "AV1"};
    }
    else if (c == "WEBM")
    {
        codecs = {"VP9", "VP8", "AV1"};
    }
    else if (c == "AVI")
    {
        codecs = {"H.264"};
    }

    return codecs;
}

QStringList VideoRecordingWidget::audioCodecsFor(const QString& container) const
{
    const QString c = container.toUpper();

    // Video-only animation formats
    if (c == "GIF" || c == "APNG" || c == "WEBP")
        return {};

    if (_nativeBackendRadio->isChecked())
        return {"AAC"};  // AVAssetWriter writes AAC

    if (c == "MP4")  return {"AAC", "MP3"};
    if (c == "MOV")  return {"AAC", "PCM"};
    if (c == "MKV")  return {"AAC", "MP3", "FLAC", "PCM", "Opus", "Vorbis"};
    if (c == "WEBM") return {"Opus", "Vorbis"};
    if (c == "AVI")  return {"MP3", "PCM"};

    return {"AAC"};
}

void VideoRecordingWidget::populateContainers()
{
    repopulateCombo(_containerCombo, containersForBackend());

    // Refresh everything that depends on the container
    populateCodecCombos();
    populateAudioCodecs();
    updateFileExtension();
}

void VideoRecordingWidget::populateCodecCombos()
{
    QStringList codecs = videoCodecsFor(_containerCombo->currentText());
    repopulateCombo(_videoCodecCombo, codecs);

    // Fixed-codec formats need no codec choice
    _videoCodecCombo->setEnabled(codecs.size() > 1);
}

void VideoRecordingWidget::populateAudioCodecs()
{
    QStringList codecs = audioCodecsFor(_containerCombo->currentText());
    repopulateCombo(_audioCodecCombo, codecs);

    bool audioSupported = !codecs.isEmpty();
    _includeAudioCheck->setEnabled(audioSupported);
    _audioCodecCombo->setEnabled(audioSupported && _includeAudioCheck->isChecked());
}

QString VideoRecordingWidget::benchmarkKey(const QString& backend, const QString& codec)
{
    return backend.toLower() + ":" + codec.toLower();
}

QString VideoRecordingWidget::currentSelectionBenchmarkKey() const
{
    QString container = _containerCombo->currentText().toUpper();

    // Animation formats have fixed codecs
    if (container == "GIF")
        return benchmarkKey("gif", "gif");
    if (container == "APNG")
        return benchmarkKey("ffmpeg", "apng");
    if (container == "WEBP")
        return benchmarkKey("ffmpeg", "webp");

    QString codec = _videoCodecCombo->currentText().split(" ")[0].toLower();
    if (codec == "h.264") codec = "h264";
    if (codec == "h.265") codec = "h265";

    // Mirror RecordingManager's Auto routing
    bool nativeEligible = (container == "MP4" || container == "MOV") &&
                          (codec == "h264" || codec == "h265") &&
                          (!_includeAudioCheck->isChecked() || _audioCodecCombo->currentText() == "AAC");
    bool useNative = (_nativeBackendRadio->isChecked() && _nativeAvailable) ||
                     (_autoBackendRadio->isChecked() && _nativeAvailable && nativeEligible);

    return benchmarkKey(useNative ? "native" : "ffmpeg", codec);
}

void VideoRecordingWidget::loadBenchmarkResults()
{
    QSettings settings("unreal-ng", "recording");
    int count = settings.beginReadArray("benchmark");
    for (int i = 0; i < count; i++)
    {
        settings.setArrayIndex(i);
        QString key = settings.value("key").toString();
        BenchmarkEntry entry;
        entry.msPerFrame = settings.value("msPerFrame").toDouble();
        entry.achievedFps = settings.value("fps").toDouble();
        if (!key.isEmpty() && entry.msPerFrame > 0)
            _benchmarkResults[key] = entry;
    }
    settings.endArray();
}

void VideoRecordingWidget::saveBenchmarkResults()
{
    QSettings settings("unreal-ng", "recording");
    settings.beginWriteArray("benchmark");
    int i = 0;
    for (auto it = _benchmarkResults.constBegin(); it != _benchmarkResults.constEnd(); ++it, ++i)
    {
        settings.setArrayIndex(i);
        settings.setValue("key", it.key());
        settings.setValue("msPerFrame", it.value().msPerFrame);
        settings.setValue("fps", it.value().achievedFps);
    }
    settings.endArray();
}

void VideoRecordingWidget::updateRealtimeEstimate()
{
    validateContext();
    if (!_context || !_context->pRecordingManager)
    {
        _estimateLabel->setText("No active emulator");
        _estimateLabel->setStyleSheet("padding: 8px; background-color: #f0f0f0; border-radius: 4px;");
        return;
    }

    // Prefer MEASURED performance from a previous benchmark run for the
    // exact backend+codec now selected — far more trustworthy than heuristics
    auto it = _benchmarkResults.constFind(currentSelectionBenchmarkKey());
    if (it != _benchmarkResults.constEnd())
    {
        const BenchmarkEntry& m = it.value();
        QString detail = QString("measured %1 ms/frame (%2 fps) on this machine")
                             .arg(m.msPerFrame, 0, 'f', 2)
                             .arg(m.achievedFps, 0, 'f', 0);
        if (m.msPerFrame < 20.0)
        {
            _estimateLabel->setText(QString("⚡ REALTIME — %1").arg(detail));
            _estimateLabel->setStyleSheet("padding: 8px; background-color: #d4edda; color: #155724; border-radius: 4px; font-weight: bold;");
        }
        else
        {
            _estimateLabel->setText(QString("⚠ NON-REALTIME — %1\nEmulation will slow to guarantee perfect 50Hz output.").arg(detail));
            _estimateLabel->setStyleSheet("padding: 8px; background-color: #fff3cd; color: #856404; border-radius: 4px; font-weight: bold;");
        }
        return;
    }

    // No benchmark data for this combination — fall back to the heuristic estimator
    QString codecText = _videoCodecCombo->currentText().split(" ")[0].toLower();
    if (codecText == "h.264") codecText = "h264";
    if (codecText == "h.265") codecText = "h265";

    // Mirror RecordingManager's Auto routing: native only for H.264/H.265
    // in MP4/MOV with AAC (or no) audio — everything else goes to FFmpeg
    QString container = _containerCombo->currentText().toUpper();
    bool nativeEligible = (container == "MP4" || container == "MOV") &&
                          (codecText == "h264" || codecText == "h265") &&
                          (!_includeAudioCheck->isChecked() || _audioCodecCombo->currentText() == "AAC");
    bool useNative = (_nativeBackendRadio->isChecked() && _nativeAvailable) ||
                     (_autoBackendRadio->isChecked() && _nativeAvailable && nativeEligible);

    EncoderConfig config;
    config.videoCodec = codecText.toStdString();
    config.frameRate = 50.0f;

    auto capability = RealtimeEstimator::estimate(config, useNative);
    std::string reason = RealtimeEstimator::getReason(config, useNative);

    if (capability == RealtimeEstimator::RealtimeCapability::Realtime)
    {
        _estimateLabel->setText(QString("⚡ REALTIME — %1").arg(QString::fromStdString(reason)));
        _estimateLabel->setStyleSheet("padding: 8px; background-color: #d4edda; color: #155724; border-radius: 4px; font-weight: bold;");
    }
    else if (capability == RealtimeEstimator::RealtimeCapability::NonRealtime)
    {
        _estimateLabel->setText(QString("⚠ NON-REALTIME — %1\nEmulation will slow to guarantee perfect 50Hz output.")
                                .arg(QString::fromStdString(reason)));
        _estimateLabel->setStyleSheet("padding: 8px; background-color: #fff3cd; color: #856404; border-radius: 4px; font-weight: bold;");
    }
    else
    {
        _estimateLabel->setText(QString("? UNKNOWN — %1").arg(QString::fromStdString(reason)));
        _estimateLabel->setStyleSheet("padding: 8px; background-color: #e2e3e5; color: #383d41; border-radius: 4px;");
    }
}

void VideoRecordingWidget::onStartRecording()
{
    validateContext();
    if (!_context || !_context->pRecordingManager)
        return;

    QString filename = _filePathEdit->text();
    if (filename.isEmpty())
    {
        QMessageBox::warning(this, "Error", "Please specify an output file path.");
        return;
    }

    // Determine codec strings
    QString container = _containerCombo->currentText().toUpper();
    bool videoOnly = (container == "GIF" || container == "APNG" || container == "WEBP");

    QString videoCodec;
    if (videoOnly)
    {
        videoCodec = container.toLower();
    }
    else
    {
        QString codecText = _videoCodecCombo->currentText();
        videoCodec = codecText.split(" ")[0].toLower();
        if (videoCodec == "h.264") videoCodec = "h264";
        if (videoCodec == "h.265") videoCodec = "h265";
    }

    QString audioCodec;
    if (_includeAudioCheck->isChecked() && !videoOnly)
    {
        audioCodec = _audioCodecCombo->currentText().toLower();
        if (audioCodec == "pcm") audioCodec = "pcm_s16le";
    }

    // Configure recording manager
    RecordingManager* rm = _context->pRecordingManager;

    // Capture region + output size; resolution derives from the region
    rm->SetCaptureRegion(_captureCombo->currentIndex() == 1 ? VideoCaptureRegion::MainScreen
                                                            : VideoCaptureRegion::FullFrame);
    rm->SetScaleFactor(static_cast<uint32_t>(_sizeCombo->currentIndex() + 1));
    rm->SetVideoResolution(0, 0);  // Re-derive from screen + capture region
    rm->SetVideoCodec(videoCodec.toStdString());

    // Set encoder backend from UI selection
    if (_nativeBackendRadio->isChecked())
        rm->SetEncoderBackend(EncoderBackend::Native);
    else if (_ffmpegBackendRadio->isChecked())
        rm->SetEncoderBackend(EncoderBackend::FFmpeg);
    else
        rm->SetEncoderBackend(EncoderBackend::Auto);

    // Quality preset
    int qualityIdx = _qualityCombo->currentIndex();
    int qualityPreset = 5; // Medium
    if (qualityIdx == 0) qualityPreset = 1; // Fastest
    else if (qualityIdx == 1) qualityPreset = 3; // Fast
    else if (qualityIdx == 2) qualityPreset = 5; // Medium
    else if (qualityIdx == 3) qualityPreset = 7; // High
    else if (qualityIdx == 4) qualityPreset = 9; // Best
    rm->SetQualityPreset(qualityPreset);

    // Explicit ffmpeg path from the UI (empty = auto-detect)
    rm->SetFFmpegPath(_ffmpegPathEdit->text().toStdString());

    bool success = rm->StartRecording(filename.toStdString(),
                                       videoCodec.toStdString(),
                                       audioCodec.toStdString(),
                                       0, 0); // Auto bitrate

    if (success)
    {
        _wasRecording = true;
        // Track which emulator instance we started recording on.
        // Recording continues on this instance even if the user switches
        // the active emulator, until explicit stop or instance destruction.
        if (_context && _context->pEmulator)
            _recordingEmulatorId = _context->pEmulator->GetId();

        // Measured benchmark data beats the manager's heuristic estimate.
        // Without this, encoders the heuristic misjudges as non-realtime
        // (e.g. VP9 at ZX sizes) make the mainloop skip its 50Hz pacing and
        // run emulation at encoder speed (150+ fps capture, turbo gameplay).
        auto measured = _benchmarkResults.constFind(currentSelectionBenchmarkKey());
        if (measured != _benchmarkResults.constEnd())
            rm->SetRealtimeCapable(measured.value().msPerFrame < 20.0);

        _statsTimer->start();
        updateRecordingControls();
        updateRealtimeEstimate();
    }
    else
    {
        std::string err = rm->GetLastRecordingError();
        if (err.empty())
            err = "Unknown recording failure (no error detail provided).";

        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Recording Failed");
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setText("Failed to start recording.");
        msgBox.setInformativeText(QString::fromStdString(err));
        msgBox.setDetailedText(QString::fromStdString(err));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
    }
}

void VideoRecordingWidget::onPauseRecording()
{
    EmulatorContext* recCtx = recordingContext();
    if (recCtx && recCtx->pRecordingManager)
    {
        recCtx->pRecordingManager->PauseRecording();
        updateRecordingControls();
    }
}

void VideoRecordingWidget::onResumeRecording()
{
    EmulatorContext* recCtx = recordingContext();
    if (recCtx && recCtx->pRecordingManager)
    {
        recCtx->pRecordingManager->ResumeRecording();
        updateRecordingControls();
    }
}

void VideoRecordingWidget::onStopRecording()
{
    if (_isStopping)
        return;

    EmulatorContext* recCtx = recordingContext();

    // Recording emulator gone (destroyed) - just reset UI
    if (!recCtx || !recCtx->pRecordingManager)
    {
        _wasRecording = false;
        _isStopping = false;
        _recordingEmulatorId.clear();
        _statsTimer->stop();
        _recordingIndicator->setText("IDLE");
        _recordingIndicator->setStyleSheet("font-weight: bold; color: gray;");
        updateRecordingControls();
        return;
    }

    // StopRecording drains queues and waits for the encoder to finalize the
    // container — seconds for slow encoders. Run it off the UI thread so the
    // window stays responsive, and show a FINALIZING state meanwhile.
    _isStopping = true;
    _recordingIndicator->setText("⏳ FINALIZING…");
    _recordingIndicator->setStyleSheet("font-weight: bold; color: orange;");
    updateRecordingControls();

    RecordingManager* rm = recCtx->pRecordingManager;
    QPointer<VideoRecordingWidget> self(this);

    std::thread([self, rm]() {
        rm->StopRecording();

        // Hop back to the UI thread; widget may have been closed meanwhile
        QMetaObject::invokeMethod(qApp, [self]() {
            if (!self)
                return;
            self->_wasRecording = false;
            self->_isStopping = false;
            self->_recordingEmulatorId.clear();
            self->_statsTimer->stop();
            self->updateRecordingControls();
            self->onUpdateStats();
        }, Qt::QueuedConnection);
    }).detach();
}

void VideoRecordingWidget::onUpdateStats()
{
    // Always validate the active context pointer — the emulator may have been
    // destroyed while this timer was running
    validateContext();

    // Use the RECORDING context (the emulator we started recording with),
    // not the currently-active one — the user may have switched emulators
    EmulatorContext* recCtx = recordingContext();

    // Recording emulator gone while recording? Auto-stop UI state
    if (_wasRecording && (!recCtx || !recCtx->pRecordingManager))
    {
        _wasRecording = false;
        _isStopping = false;
        _recordingEmulatorId.clear();
        _statsTimer->stop();
        _recordingIndicator->setText("STOPPED (emulator closed)");
        _recordingIndicator->setStyleSheet("font-weight: bold; color: orange;");
        updateRecordingControls();
        return;
    }

    if (!_wasRecording || !recCtx || !recCtx->pRecordingManager)
    {
        // Not recording or no recording context — update idle display
        if (_recordingIndicator->text() != "IDLE" && !_isStopping)
        {
            _recordingIndicator->setText("IDLE");
            _recordingIndicator->setStyleSheet("font-weight: bold; color: gray;");
        }
        return;
    }

    RecordingManager* rm = recCtx->pRecordingManager;

    // Detect auto-stop: RecordingManager was stopped externally (e.g. via
    // MessageCenter state change) but the widget still thinks it's recording
    if (!rm->IsRecording() && !rm->IsPaused() && !_isStopping)
    {
        _wasRecording = false;
        _recordingEmulatorId.clear();
        _statsTimer->stop();
        _recordingIndicator->setText("IDLE");
        _recordingIndicator->setStyleSheet("font-weight: bold; color: gray;");
        updateRecordingControls();
        return;
    }

    if (_isStopping)
    {
        // Keep the FINALIZING indicator until the stop thread reports back
    }
    else if (rm->IsRecording())
    {
        _recordingIndicator->setText("● RECORDING");
        _recordingIndicator->setStyleSheet("font-weight: bold; color: red;");
    }
    else if (rm->IsPaused())
    {
        _recordingIndicator->setText("⏸ PAUSED");
        _recordingIndicator->setStyleSheet("font-weight: bold; color: orange;");
    }

    auto stats = rm->GetStats();

    // Duration
    int totalSec = static_cast<int>(stats.recordedDuration);
    int hours = totalSec / 3600;
    int mins = (totalSec % 3600) / 60;
    int secs = totalSec % 60;
    _durationLabel->setText(QString::asprintf("%02d:%02d:%02d", hours, mins, secs));

    // File size
    double sizeMB = static_cast<double>(stats.outputFileSize) / (1024.0 * 1024.0);
    if (sizeMB < 1.0)
        _sizeLabel->setText(QString::asprintf("%.1f KB", static_cast<double>(stats.outputFileSize) / 1024.0));
    else
        _sizeLabel->setText(QString::asprintf("%.1f MB", sizeMB));

    // FPS (rolling window, not session average)
    if (stats.recentFps > 0)
        _fpsLabel->setText(QString::asprintf("%.1f fps", stats.recentFps));
    else if (stats.recordedDuration > 0)
        _fpsLabel->setText(QString::asprintf("%.1f fps", static_cast<double>(stats.framesRecorded) / stats.recordedDuration));
    else
        _fpsLabel->setText("0.0 fps");
}

void VideoRecordingWidget::updateRecordingControls()
{
    // Recording state is tracked on the RECORDING emulator, which may differ
    // from the currently active one (user may have switched emulators)
    EmulatorContext* recCtx = recordingContext();
    bool hasRecContext = recCtx && recCtx->pRecordingManager;
    bool recording = hasRecContext && recCtx->pRecordingManager->IsRecording();
    bool paused = hasRecContext && recCtx->pRecordingManager->IsPaused();

    // Idle = active context available AND not currently in any recording session
    validateContext();
    bool hasActiveContext = _context && _context->pRecordingManager;
    bool idle = hasActiveContext && !recording && !paused && !_isStopping && !_wasRecording;

    // Stop must work even if context is gone (orphaned recording state)
    bool canStop = (recording || paused || _wasRecording) && !_isStopping;

    _startButton->setEnabled(idle);
    _pauseButton->setEnabled(recording && !paused && !_isStopping);
    _resumeButton->setEnabled(paused && !_isStopping);
    _stopButton->setEnabled(canStop);

    // Disable config controls while recording; honor the capability matrix when idle
    bool audioSupported = _audioCodecCombo->count() > 0;
    _containerCombo->setEnabled(idle);
    _videoCodecCombo->setEnabled(idle && _videoCodecCombo->count() > 1);
    _audioCodecCombo->setEnabled(idle && audioSupported && _includeAudioCheck->isChecked());
    _includeAudioCheck->setEnabled(idle && audioSupported);
    _qualityCombo->setEnabled(idle);
    _captureCombo->setEnabled(idle);
    _sizeCombo->setEnabled(idle);
    _filePathEdit->setEnabled(idle);
    _browseButton->setEnabled(idle);
    _backendGroup->setExclusive(false);
    for (auto* btn : _backendGroup->buttons())
        btn->setEnabled(idle);
    _backendGroup->setExclusive(true);
}

void VideoRecordingWidget::onIncludeAudioChanged(int state)
{
    _audioCodecCombo->setEnabled(state == Qt::Checked && _audioCodecCombo->count() > 0);
    updateRealtimeEstimate();  // Audio codec affects Auto backend routing
}

void VideoRecordingWidget::onMultiTrackConfigure()
{
    validateContext();
    if (!_context || !_context->pRecordingManager)
        return;

    auto* rm = _context->pRecordingManager;

    // Gather current tracks
    std::vector<AudioTrackConfig> tracks;
    for (size_t i = 0; i < rm->GetAudioTrackCount(); ++i)
    {
        tracks.push_back(rm->GetAudioTrack(i));
    }

    // If no tracks exist, add a default one
    if (tracks.empty())
    {
        AudioTrackConfig defaultTrack;
        defaultTrack.name = "Master Audio";
        defaultTrack.source = AudioSourceType::MasterMix;
        defaultTrack.codec = "aac";
        defaultTrack.bitrate = 192;
        tracks.push_back(defaultTrack);
    }

    MultiTrackDialog dialog(this);
    dialog.setTracks(tracks);

    if (dialog.exec() == QDialog::Accepted)
    {
        auto newTracks = dialog.getTracks();
        rm->ClearAudioTracks();
        for (const auto& track : newTracks)
        {
            rm->AddAudioTrack(track);
        }

        // If multi-track, switch to MultiTrack recording mode
        if (newTracks.size() > 1)
        {
            rm->SetRecordingMode(RecordingMode::MultiTrack);
        }
    }
}

void VideoRecordingWidget::onBenchmark()
{
    runBenchmark();
}

void VideoRecordingWidget::runBenchmark()
{
    // Standard ZX Spectrum full-frame size — no emulator needed.
    // The BenchmarkFeeder generates complex synthetic content (sliding
    // checkerboard, bouncing pong elements, particles, light gradients)
    // that stresses every part of the encoder pipeline.
    const int fbWidth = 352;
    const int fbHeight = 288;

    // Determine available encoders
    std::string ffmpegPath = FFmpegProbe::findFFmpeg();
    bool ffmpegAvailable = !ffmpegPath.empty();
    bool nativeAvailable = PlatformEncoderFactory::isNativeAvailable();

    struct TestEntry { QString codec; QString backend; QString label; };
    std::vector<TestEntry> tests;

    // GIF always available
    tests.push_back({"gif", "Palette", "GIF (FixedZX16)"});

    // H.264 / H.265 via native hardware encoder
    if (nativeAvailable)
    {
        QString nativeName = QString::fromStdString(PlatformEncoderFactory::getNativeDisplayName());
        tests.push_back({"h264", "Native", QString("H.264 (%1)").arg(nativeName)});
        tests.push_back({"h265", "Native", QString("H.265 (%1)").arg(nativeName)});
    }

    // FFmpeg codecs — the combinations the recording UI actually offers
    if (ffmpegAvailable)
    {
        tests.push_back({"h264", "FFmpeg", "H.264 (FFmpeg)"});
        tests.push_back({"h265", "FFmpeg", "H.265 (FFmpeg)"});
        tests.push_back({"vp9", "FFmpeg", "VP9/WebM (FFmpeg)"});
        tests.push_back({"apng", "FFmpeg", "APNG (FFmpeg)"});
        if (_ffmpegHasWebP)
            tests.push_back({"webp", "FFmpeg", "WebP (FFmpeg)"});
    }

    if (tests.empty())
    {
        QMessageBox::information(this, "Benchmark", "No encoders available to benchmark.");
        return;
    }

    constexpr int SECONDS = 5;
    float fps = 50.0f;
    int actualFrames = static_cast<int>(fps * SECONDS);

    // Progress dialog
    QProgressDialog progress("Benchmarking encoders…", "Cancel", 0,
                             static_cast<int>(tests.size() * actualFrames), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setWindowTitle("Benchmark");
    progress.show();

    // Framebuffer + autonomous feeder — no emulator needed.
    // Each encoder test gets fresh content from frame 0 so results aren't
    // polluted by a warm encoder cache from the previous run.
    size_t fbSize = static_cast<size_t>(fbWidth) * fbHeight * 4;
    std::vector<uint8_t> fbData(fbSize);

    FramebufferDescriptor benchFb = {};
    benchFb.width = static_cast<uint16_t>(fbWidth);
    benchFb.height = static_cast<uint16_t>(fbHeight);
    benchFb.memoryBuffer = fbData.data();
    benchFb.memoryBufferSize = fbSize;

    struct Result {
        QString label;
        double msPerFrame;
        double achievedFps;
        bool success;
        QString error;
    };
    std::vector<Result> results;
    int progressCounter = 0;

    for (const auto& test : tests)
    {
        if (progress.wasCanceled())
            break;

        progress.setLabelText(QString("Benchmarking: %1…").arg(test.label));

        // Build config — video-only. Leaving audio enabled makes ffmpeg's muxer
        // buffer video indefinitely waiting for an audio stream that never comes.
        EncoderConfig config;
        config.videoCodec = test.codec.toStdString();
        config.videoWidth = fbWidth;
        config.videoHeight = fbHeight;
        config.frameRate = fps;
        config.videoBitrate = 1500;
        config.audioChannels = 0;
        config.audioCodec.clear();
        config.gifPaletteMode = EncoderConfig::GifPaletteMode::FixedZX16;
        config.gifDelayMs = 20;

        // YUV codecs record at the UI default of 2× — measure the real cost
        bool rgbFormat = (test.codec == "gif" || test.codec == "apng" || test.codec == "webp");
        config.scaleFactor = rgbFormat ? 1 : 2;

        // Temp output file
        QString ext = (test.codec == "gif") ? "gif" :
                      (test.codec == "vp9") ? "webm" :
                      (test.codec == "apng") ? "apng" :
                      (test.codec == "webp") ? "webp" : "mp4";
        QString tempFile = QDir::tempPath() + "/unreal_bench_" +
            QString::number(QDateTime::currentMSecsSinceEpoch()) + "." + ext;

        // Create encoder
        EncoderPtr encoder;
        if (test.backend == "Palette")
            encoder = std::make_unique<GIFEncoder>();
        else if (test.backend == "Native")
            encoder = PlatformEncoderFactory::createNativeEncoder(config);
        else  // FFmpeg
        {
            auto ff = std::make_unique<FFmpegPipeEncoder>();
            ff->setFFmpegPath(ffmpegPath);
            // Blocking mode: without it a slow encoder silently drops frames
            // and the benchmark reports fantasy throughput
            ff->setBlocking(true);
            encoder = std::move(ff);
        }

        Result result;
        result.label = test.label;
        result.success = false;
        result.msPerFrame = 0;
        result.achievedFps = 0;

        if (!encoder)
        {
            result.error = "Encoder unavailable";
            results.push_back(result);
            progressCounter += actualFrames;
            progress.setValue(progressCounter);
            QApplication::processEvents();
            continue;
        }

        if (!encoder->Start(tempFile.toStdString(), config))
        {
            result.error = QString::fromStdString(encoder->GetLastError());
            if (result.error.isEmpty())
                result.error = "Start failed";
            results.push_back(result);
            progressCounter += actualFrames;
            progress.setValue(progressCounter);
            QApplication::processEvents();
            continue;
        }

        // Feed frames and measure. Timing includes Stop() — async encoders
        // (VideoToolbox, FFmpeg pipe) queue internally, so enqueue time alone
        // would understate the real cost. Feed + drain = sustained throughput.
        double frameInterval = 1.0 / fps;
        int framesFed = 0;
        bool canceled = false;
        auto startTime = std::chrono::steady_clock::now();

        // Fresh feeder per encoder — deterministic content, same difficulty
        BenchmarkFeeder feeder(fbWidth, fbHeight, fps);

        for (int f = 0; f < actualFrames; f++)
        {
            feeder.renderFrame(fbData.data(), f);

            encoder->OnVideoFrame(benchFb, f * frameInterval);
            framesFed++;

            if ((f + 1) % 10 == 0)
            {
                progressCounter += 10;
                progress.setValue(progressCounter);
                QApplication::processEvents();
                if (progress.wasCanceled())
                {
                    canceled = true;
                    break;
                }
            }
        }

        encoder->Stop();
        auto endTime = std::chrono::steady_clock::now();
        QFile::remove(tempFile);

        if (canceled || framesFed == 0)
            break;

        if (!encoder->GetLastError().empty() && encoder->GetFramesEncoded() == 0)
        {
            // Encoder died mid-run without producing anything
            result.error = QString::fromStdString(encoder->GetLastError());
            results.push_back(result);
            continue;
        }

        double totalMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        result.msPerFrame = totalMs / framesFed;
        result.achievedFps = 1000.0 / result.msPerFrame;
        result.success = true;
        results.push_back(result);

        // Keep the measurement — updateRealtimeEstimate uses it for this
        // backend+codec from now on (persisted across sessions)
        QString backendKey = (test.backend == "Palette") ? "gif" :
                             (test.backend == "Native") ? "native" : "ffmpeg";
        BenchmarkEntry entry;
        entry.msPerFrame = result.msPerFrame;
        entry.achievedFps = result.achievedFps;
        _benchmarkResults[benchmarkKey(backendKey, test.codec)] = entry;
    }

    saveBenchmarkResults();
    updateRealtimeEstimate();

    progress.cancel();

    if (results.empty())
        return;

    // Sort fastest first — makes choosing an encoder trivial
    std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
        if (a.success != b.success) return a.success;
        return a.msPerFrame < b.msPerFrame;
    });

    // Results table dialog
    QDialog dialog(this);
    dialog.setWindowTitle("Encoding Performance Benchmark");
    auto* layout = new QVBoxLayout(&dialog);

    auto* table = new QTableWidget(static_cast<int>(results.size()), 5, &dialog);
    table->setHorizontalHeaderLabels({"Encoder", "ms/frame", "fps", "Speed", "Status"});
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);

    auto makeItem = [](const QString& text, bool numeric) {
        auto* item = new QTableWidgetItem(text);
        item->setTextAlignment(numeric ? (Qt::AlignRight | Qt::AlignVCenter)
                                       : (Qt::AlignLeft | Qt::AlignVCenter));
        return item;
    };

    for (int row = 0; row < static_cast<int>(results.size()); row++)
    {
        const Result& r = results[row];
        table->setItem(row, 0, makeItem(r.label, false));

        if (r.success)
        {
            double ratio = r.msPerFrame / 20.0;  // 20ms = realtime at 50fps
            QString tier;
            QColor tierColor;
            if (ratio < 0.05)      { tier = "⚡ Negligible"; tierColor = QColor("#1a7f37"); }
            else if (ratio < 0.5)  { tier = "✓ Fast";        tierColor = QColor("#1a7f37"); }
            else if (ratio < 1.0)  { tier = "◷ Realtime";    tierColor = QColor("#9a6700"); }
            else                   { tier = "⚠ Slow";        tierColor = QColor("#cf222e"); }

            table->setItem(row, 1, makeItem(QString::number(r.msPerFrame, 'f', 2), true));
            table->setItem(row, 2, makeItem(QString::number(r.achievedFps, 'f', 1), true));
            auto* tierItem = makeItem(tier, false);
            tierItem->setForeground(tierColor);
            table->setItem(row, 3, tierItem);
            table->setItem(row, 4, makeItem("OK", false));
        }
        else
        {
            table->setItem(row, 1, makeItem("—", true));
            table->setItem(row, 2, makeItem("—", true));
            table->setItem(row, 3, makeItem("—", false));
            auto* errItem = makeItem(r.error.isEmpty() ? "Failed" : r.error, false);
            errItem->setForeground(QColor("#cf222e"));
            errItem->setToolTip(errItem->text());
            table->setItem(row, 4, errItem);
        }
    }

    table->resizeColumnsToContents();
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table->setMinimumSize(560, static_cast<int>(results.size() + 1) * 28 + 40);

    layout->addWidget(table);

    auto* footer = new QLabel(QString("Realtime budget: 20 ms/frame at 50 fps. Resolution %1×%2. "
                                      "Synthetic content: checkerboard + pong + particles + light. "
                                      "Results are saved and used for the realtime estimate.")
                                  .arg(fbWidth).arg(fbHeight));
    footer->setWordWrap(true);
    footer->setStyleSheet("color: gray; font-size: 11px; padding: 4px;");
    layout->addWidget(footer);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.resize(640, static_cast<int>(results.size() + 1) * 30 + 130);
    dialog.exec();
}
