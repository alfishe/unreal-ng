#include "temporaleffectsdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSignalBlocker>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"
#include "emulator/video/zxdlss/algorithm.h"
#include "widgets/devicescreenwrapper.h"

namespace
{
/// The algorithm offered first: the accepted baseline (regression gate)
constexpr const char* kDefaultAlgorithm = "mod-tpgwafsd";

Screen* ScreenOf(Emulator* emulator)
{
    return emulator && emulator->GetContext() ? emulator->GetContext()->pScreen : nullptr;
}
}  // namespace

TemporalEffectsDialog::TemporalEffectsDialog(DeviceScreenWrapper* screenWrapper, EmulatorSource emulator,
                                             QWidget* parent)
    : QDialog(parent)
    , _screenWrapper(screenWrapper)
    , _emulator(std::move(emulator))
{
    setWindowTitle(tr("Temporal Effects Settings"));
    setMinimumWidth(380);

    auto* mainLayout = new QVBoxLayout(this);

    // Enable checkbox
    _enabledCheck = new QCheckBox(tr("Enable Temporal Effects"));
    _enabledCheck->setToolTip(tr("Combine several frames into one to reduce flickering (GigaScreen, interlace)"));
    mainLayout->addWidget(_enabledCheck);

    // Effect
    auto* effectLayout = new QHBoxLayout();
    effectLayout->addWidget(new QLabel(tr("Effect:")));
    _effectCombo = new QComboBox();
    _effectCombo->addItem(tr("Frame blending"), EffectBlend);
    _effectCombo->addItem(tr("ZX DLSS de-flicker"), EffectDlss);
    _effectCombo->setToolTip(tr("Frame blending averages the last frames everywhere.\n"
                                "ZX DLSS finds what flickers (GigaScreen colors, two-page animation) and "
                                "mixes only that, keeping motion sharp."));
    effectLayout->addWidget(_effectCombo, 1);
    mainLayout->addLayout(effectLayout);

    // Blending settings
    _blendGroup = new QGroupBox(tr("Blending Settings"));
    auto* settingsLayout = new QVBoxLayout(_blendGroup);

    auto* depthLayout = new QHBoxLayout();
    depthLayout->addWidget(new QLabel(tr("Frame History:")));
    _historySizeSlider = new QSlider(Qt::Horizontal);
    _historySizeSlider->setRange(2, 5);
    _historySizeSlider->setSingleStep(1);
    _historySizeSlider->setPageStep(1);
    _historySizeSlider->setTracking(true);
    _historySizeSlider->setTickPosition(QSlider::TicksBelow);
    _historySizeSlider->setTickInterval(1);
    _historySizeSlider->setMinimumWidth(100);
    _historySizeSlider->setToolTip(tr("Number of frames to blend (2-5). More frames = smoother but more ghosting."));
    depthLayout->addWidget(_historySizeSlider);
    _historySizeLabel = new QLabel("2");
    _historySizeLabel->setMinimumWidth(20);
    depthLayout->addWidget(_historySizeLabel);
    settingsLayout->addLayout(depthLayout);

    auto* modeLayout = new QHBoxLayout();
    modeLayout->addWidget(new QLabel(tr("Blend Mode:")));
    _weightModeCombo = new QComboBox();
    _weightModeCombo->addItem(tr("Equal (all frames same weight)"), 0);
    _weightModeCombo->addItem(tr("Exponential (newer frames stronger)"), 1);
    _weightModeCombo->setToolTip(tr("How frame weights are distributed. Exponential gives less ghosting."));
    modeLayout->addWidget(_weightModeCombo);
    settingsLayout->addLayout(modeLayout);

    auto* decayLayout = new QHBoxLayout();
    _decayLabel = new QLabel(tr("Decay Factor:"));
    decayLayout->addWidget(_decayLabel);
    _decaySpin = new QDoubleSpinBox();
    _decaySpin->setRange(0.1, 0.9);
    _decaySpin->setSingleStep(0.1);
    _decaySpin->setValue(0.5);
    _decaySpin->setToolTip(tr("Each older frame has this fraction of the previous frame's weight.\n"
                              "Lower = less ghosting, higher = smoother blending."));
    decayLayout->addWidget(_decaySpin);
    decayLayout->addStretch();
    settingsLayout->addLayout(decayLayout);
    mainLayout->addWidget(_blendGroup);

    // ZX DLSS settings
    _dlssGroup = new QGroupBox(tr("ZX DLSS Settings"));
    auto* dlssLayout = new QVBoxLayout(_dlssGroup);
    auto* algorithmLayout = new QHBoxLayout();
    algorithmLayout->addWidget(new QLabel(tr("Algorithm:")));
    _algorithmCombo = new QComboBox();
    for (const std::string& name : zxdlss::algorithmNames())
    {
        // raw is a pass-through for checking the tools; the -ref implementations are
        // the literal, slow specifications
        if (name == "raw" || (name.size() > 4 && name.compare(name.size() - 4, 4, "-ref") == 0))
            continue;
        _algorithmCombo->addItem(QString::fromStdString(name), QString::fromStdString(name));
    }
    const int preferred = _algorithmCombo->findData(QString::fromLatin1(kDefaultAlgorithm));
    if (preferred >= 0)
        _algorithmCombo->setCurrentIndex(preferred);
    _algorithmCombo->setToolTip(tr("%1 is the accepted baseline").arg(QString::fromLatin1(kDefaultAlgorithm)));
    algorithmLayout->addWidget(_algorithmCombo, 1);
    dlssLayout->addLayout(algorithmLayout);

    // Correction LED: lit while the frame on screen was corrected - a detector fired
    // and an averaging mask formed. Dark otherwise: nothing detected (the frame
    // passes unchanged), raw frames shown (warming up, late), or off
    auto* ledLayout = new QHBoxLayout();
    _correctionLed = new QLabel();
    _correctionLed->setFixedSize(14, 14);
    ledLayout->addWidget(_correctionLed);
    _correctionLabel = new QLabel();
    ledLayout->addWidget(_correctionLabel, 1);
    dlssLayout->addLayout(ledLayout);
    setLed(false, tr("Off"));

    _dlssStatus = new QLabel();
    _dlssStatus->setWordWrap(true);
    _dlssStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dlssLayout->addWidget(_dlssStatus);
    _dlssDetectors = new QLabel();
    _dlssDetectors->setWordWrap(true);
    _dlssDetectors->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _dlssDetectors->setStyleSheet("color: gray;");
    dlssLayout->addWidget(_dlssDetectors);
    mainLayout->addWidget(_dlssGroup);

    auto* infoLabel = new QLabel(tr(
        "<b>Frame blending</b> averages the last frames: a phosphor-like afterglow that also "
        "smears motion.<br><b>ZX DLSS</b> mixes only what flickers. It looks a few frames ahead, "
        "so the picture and the sound are both delayed by that much (about 0.1 s) to stay in sync."));
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: gray; font-size: 11px;");
    mainLayout->addWidget(infoLabel);

    mainLayout->addStretch();

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    connect(_enabledCheck, &QCheckBox::toggled, this, &TemporalEffectsDialog::onEnabledChanged);
    connect(_effectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &TemporalEffectsDialog::onEffectChanged);
    connect(_algorithmCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &TemporalEffectsDialog::onAlgorithmChanged);
    connect(_historySizeSlider, &QSlider::valueChanged, this, &TemporalEffectsDialog::onHistorySizeChanged);
    connect(_weightModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &TemporalEffectsDialog::onWeightModeChanged);
    connect(_decaySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &TemporalEffectsDialog::onDecayChanged);
    connect(&_statusTimer, &QTimer::timeout, this, &TemporalEffectsDialog::refreshDlssStatus);
    _statusTimer.start(200);  // the LED should follow what is on screen

    updateFromScreen();

    // Rebind when screen/emulator changes
    connect(_screenWrapper, &DeviceScreenWrapper::screenInitialized, this, &TemporalEffectsDialog::updateFromScreen);
}

std::string TemporalEffectsDialog::currentAlgorithm() const
{
    return _algorithmCombo->currentData().toString().toStdString();
}

void TemporalEffectsDialog::updateFromScreen()
{
    if (!_screenWrapper)
        return;

    Screen* screen = ScreenOf(_emulator ? _emulator() : nullptr);
    const std::string dlss = screen ? screen->GetTemporalAlgorithm() : std::string();
    const bool blend = _screenWrapper->temporalBlendingEnabled();
    const int historySize = _screenWrapper->temporalHistorySize();
    const int weightMode = _screenWrapper->temporalWeightMode();

    QSignalBlocker checkBlocker(_enabledCheck);
    QSignalBlocker effectBlocker(_effectCombo);
    QSignalBlocker algorithmBlocker(_algorithmCombo);
    QSignalBlocker sliderBlocker(_historySizeSlider);
    QSignalBlocker comboBlocker(_weightModeCombo);

    _enabledCheck->setChecked(blend || !dlss.empty());
    if (!dlss.empty())
    {
        _effectCombo->setCurrentIndex(EffectDlss);
        const int index = _algorithmCombo->findData(QString::fromStdString(dlss));
        if (index >= 0)
            _algorithmCombo->setCurrentIndex(index);
    }
    else if (blend)
    {
        _effectCombo->setCurrentIndex(EffectBlend);
    }
    _historySizeSlider->setValue(historySize);
    _historySizeLabel->setText(QString::number(historySize));
    _weightModeCombo->setCurrentIndex(weightMode);
    updateGroups();
    refreshDlssStatus();
}

void TemporalEffectsDialog::updateGroups()
{
    const bool enabled = _enabledCheck->isChecked();
    const bool dlss = _effectCombo->currentIndex() == EffectDlss;
    _blendGroup->setVisible(!dlss);
    _dlssGroup->setVisible(dlss);
    _historySizeSlider->setEnabled(enabled);
    _weightModeCombo->setEnabled(enabled);
    _decaySpin->setEnabled(enabled && _weightModeCombo->currentIndex() == 1);
    _decayLabel->setEnabled(_weightModeCombo->currentIndex() == 1);
    _algorithmCombo->setEnabled(enabled);
    adjustSize();
}

/// Switch the selected effect on (and the other one off), or both off
void TemporalEffectsDialog::applyEffect()
{
    const bool enabled = _enabledCheck->isChecked();
    const bool dlss = enabled && _effectCombo->currentIndex() == EffectDlss;
    const bool blend = enabled && !dlss;

    if (_screenWrapper)
        _screenWrapper->setTemporalBlendingEnabled(blend);

    emit blendingChanged(blend);

    // The core switches the features the algorithm needs (zxdlss, screenhq) on and off with it
    Screen* screen = ScreenOf(_emulator ? _emulator() : nullptr);
    if (screen)
        screen->SetTemporalAlgorithm(dlss ? currentAlgorithm() : std::string());
    updateGroups();
    refreshDlssStatus();
}

void TemporalEffectsDialog::onEnabledChanged(bool)
{
    applyEffect();
}

void TemporalEffectsDialog::onEffectChanged(int)
{
    applyEffect();
}

void TemporalEffectsDialog::onAlgorithmChanged(int)
{
    applyEffect();
}

void TemporalEffectsDialog::setLed(bool lit, const QString& text)
{
    _correctionLed->setStyleSheet(lit ? "background-color: #2ecc40; border-radius: 7px; border: 1px solid #1a8f2a;"
                                      : "background-color: #3a3a3a; border-radius: 7px; border: 1px solid #555;");
    _correctionLabel->setText(text);
}

void TemporalEffectsDialog::refreshDlssStatus()
{
    if (_effectCombo->currentIndex() != EffectDlss)
        return;
    Emulator* emulator = _emulator ? _emulator() : nullptr;
    Screen* screen = ScreenOf(emulator);
    if (!screen)
    {
        setLed(false, tr("No emulator"));
        _dlssStatus->clear();
        _dlssDetectors->clear();
        return;
    }
    const TemporalEffects::Stats s = screen->GetTemporalStats();
    if (s.algorithm.empty())
    {
        setLed(false, tr("Off"));
        _dlssStatus->clear();
        _dlssDetectors->clear();
        return;
    }
    if (!s.active)
    {
        setLed(false, tr("Waiting: %1").arg(QString::fromStdString(s.inactiveReason)));
        _dlssStatus->clear();
        _dlssDetectors->clear();
        return;
    }
    const double frameMs = screen->GetPresentDelayUs() / 1000.0 / std::max<int>(1, screen->GetEffectivePresentDelayFrames());
    _dlssStatus->setText(tr("Picture and sound delayed %1 frames (%2 ms).\n"
                            "Analysis %3 ms/frame (last %4 ms). Corrected %5 of %6 frames.\n"
                            "Shown raw %7 (output after the frame was on screen), late %8, restarts %9.")
                             .arg(s.videoDelayFrames)
                             .arg(s.videoDelayFrames * frameMs, 0, 'f', 0)
                             .arg(s.averageMs, 0, 'f', 1)
                             .arg(s.lastMs, 0, 'f', 1)
                             .arg(s.correctedFrames)
                             .arg(s.processed)
                             .arg(s.shownRaw)
                             .arg(s.late)
                             .arg(s.restarts));

    if (!s.showingProcessed)
    {
        // Warming up after a restart, or the output came after the frame was shown
        setLed(false, tr("Raw frames shown (warming up or late)"));
        showLastDetection();
        return;
    }
    if (!s.correcting)
    {
        // Every frame is analyzed; nothing was detected, so the output is the raw frame
        setLed(false, tr("Idle: nothing detected, frames pass unchanged"));
        showLastDetection();
        return;
    }

    // A detection in the frame on screen: the averaging mask, per detector
    const zxdlss::FrameReport& r = s.shownFrame;
    auto share = [&r](uint32_t n) { return 100.0 * n / std::max<uint32_t>(1, r.pixels); };
    QStringList mask;
    for (int k = 0; k < 4; ++k)
        if (r.periodPixels[k])
            mask << tr("period-%1 %2%").arg(k + 2).arg(share(r.periodPixels[k]), 0, 'f', 1);
    if (r.fieldPixels)
        mask << tr("two-page field %1%%2").arg(share(r.fieldPixels), 0, 'f', 1)
                    .arg(r.wholePaper ? tr(" (whole paper)") : QString());
    if (r.sceneAverage)
        mask << tr("scene average (whole frame)");
    const QString pattern = QString::fromLatin1(zxdlss::patternName(r));
    setLed(true, tr("Correcting: %1").arg(pattern));
    _lastDetection = tr("Averaging mask: %1").arg(mask.join(QStringLiteral(", ")));
    _lastDetectionAge.start();
    _dlssDetectors->setText(_lastDetection);
}

/// While nothing is detected: the last detection and how long ago it was
void TemporalEffectsDialog::showLastDetection()
{
    if (_lastDetection.isEmpty() || !_lastDetectionAge.isValid())
    {
        _dlssDetectors->setText(tr("No detection yet"));
        return;
    }
    _dlssDetectors->setText(tr("Last detection %1 s ago - %2")
                                .arg(_lastDetectionAge.elapsed() / 1000.0, 0, 'f', 1)
                                .arg(_lastDetection));
}

void TemporalEffectsDialog::onHistorySizeChanged(int value)
{
    _historySizeLabel->setText(QString::number(value));

    if (_screenWrapper)
    {
        _screenWrapper->setTemporalHistorySize(value);
    }
}

void TemporalEffectsDialog::onWeightModeChanged(int index)
{
    bool isExponential = (index == 1);
    _decaySpin->setEnabled(_enabledCheck->isChecked() && isExponential);
    _decayLabel->setEnabled(isExponential);

    if (_screenWrapper)
    {
        _screenWrapper->setTemporalWeightMode(index);
    }
}

void TemporalEffectsDialog::onDecayChanged(double value)
{
    Q_UNUSED(value);
    // TODO: Add decay setter to wrapper/screen when implementing full exponential control
}
