#include "audiosettingswidget.h"

#include <algorithm>
#include <cmath>

#include <QCursor>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QSignalBlocker>
#include <QToolTip>
#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/chips/neogs/neogsmedia.h"
#include "emulator/soundcharacterpreferences.h"

AudioSettingsWidget::AudioSettingsWidget(std::shared_ptr<Emulator> emulator, QWidget* parent)
    : QWidget(parent), _emulator(std::move(emulator))
{
    setWindowTitle("Audio Settings");
    setMinimumWidth(380);

    // Create meter timer BEFORE refreshFromContext() which may start it
    _meterTimer = new QTimer(this);
    connect(_meterTimer, &QTimer::timeout, this, &AudioSettingsWidget::onUpdateMeters);

    createUI();
    connectSignals();
    refreshFromContext();
}

AudioSettingsWidget::~AudioSettingsWidget()
{
    if (_meterTimer)
        _meterTimer->stop();
}

void AudioSettingsWidget::setEmulator(std::shared_ptr<Emulator> emulator)
{
    // Stop timer FIRST to prevent race with stale context access
    if (_meterTimer)
        _meterTimer->stop();

    _emulator = std::move(emulator);
    refreshFromContext();
}

EmulatorContext* AudioSettingsWidget::context() const
{
    return _emulator ? _emulator->GetContext() : nullptr;  // lifetime-ok: the widget holds the shared_ptr
}

void AudioSettingsWidget::createUI()
{
    auto* outerLayout = new QVBoxLayout(this);

    // Status banner
    _statusLabel = new QLabel(
        "No active emulator instance.\n"
        "Controls are disabled — start an emulator to adjust audio settings.", this);
    _statusLabel->setWordWrap(true);
    _statusLabel->setStyleSheet(
        "padding: 8px; background-color: #fff3cd; color: #856404; "
        "border: 1px solid #ffeeba; border-radius: 4px; font-weight: bold;");
    _statusLabel->setVisible(false);
    outerLayout->addWidget(_statusLabel);

    // Device readout: a compact line that rarely changes, plus an (i) button
    // opening the detailed latency/A-V breakdown on demand (the fast-moving
    // numbers live in the popup, not in a constantly repainting label)
    auto* deviceInfoRow = new QHBoxLayout();
    _deviceInfoLabel = new QLabel("Audio device: —", this);
    _deviceInfoLabel->setStyleSheet("color: gray; font-style: italic; padding: 2px;");
    // Ignored horizontal policy: take exactly the stretch-allocated width
    // (all of the row except the (i) button) - never force the window wider,
    // never collapse below it
    _deviceInfoLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    deviceInfoRow->addWidget(_deviceInfoLabel, 1);

    _deviceInfoButton = new QToolButton(this);
    _deviceInfoButton->setText("ⓘ");
    _deviceInfoButton->setAutoRaise(true);
    _deviceInfoButton->setToolTip("Show audio pipeline latency breakdown and realtime A/V offset");

    // Live-refreshing detail popup: the fast-moving values (ring occupancy,
    // A/V offset) belong here, refreshed while open; the static label above
    // keeps only the device identity and rates
    _detailPopup = new QFrame(this, Qt::Popup);
    _detailPopup->setFrameShape(QFrame::StyledPanel);
    auto* popupLayout = new QVBoxLayout(_detailPopup);
    _detailLabel = new QLabel(_detailPopup);
    _detailLabel->setTextFormat(Qt::PlainText);
    popupLayout->addWidget(_detailLabel);

    _detailTimer = new QTimer(this);
    _detailTimer->setInterval(500);  // >= 1 Hz refresh while the popup is open
    connect(_detailTimer, &QTimer::timeout, this, [this]() {
        if (_detailPopup->isVisible())
        {
            _detailLabel->setText(buildDeviceDetailText());
            _detailPopup->adjustSize();
        }
        else
        {
            _detailTimer->stop();
        }
    });

    connect(_deviceInfoButton, &QToolButton::clicked, this, [this]() {
        if (_detailPopup->isVisible())
        {
            _detailPopup->hide();
            _detailTimer->stop();
            return;
        }
        _detailLabel->setText(buildDeviceDetailText());
        _detailPopup->adjustSize();
        _detailPopup->move(_deviceInfoButton->mapToGlobal(QPoint(0, _deviceInfoButton->height())));
        _detailPopup->show();
        _detailTimer->start();
    });
    deviceInfoRow->addWidget(_deviceInfoButton, 0);
    outerLayout->addLayout(deviceInfoRow);

    _controlsContainer = new QWidget(this);
    outerLayout->addWidget(_controlsContainer);

    auto* layout = new QVBoxLayout(_controlsContainer);
    layout->setContentsMargins(0, 0, 0, 0);

    // ============ Sources section (registry-driven mixer rows) ============
    _sourcesGroup = new QGroupBox("Sources", this);
    _sourcesContainer = new QWidget(_sourcesGroup);
    auto* sourcesGroupLayout = new QVBoxLayout(_sourcesGroup);
    sourcesGroupLayout->addWidget(_sourcesContainer);
    _soloIndicator = new QLabel("", _sourcesGroup);
    _soloIndicator->setStyleSheet("color: orange; font-weight: bold;");
    _soloIndicator->setVisible(false);
    sourcesGroupLayout->addWidget(_soloIndicator);
    layout->addWidget(_sourcesGroup);

    // ============ Channels section (any source with channels) ============
    // One source at a time (its selector lists the fitted sources that have channels): mute, solo within the source,
    // volume, and a meter where the device measures the channel. The same channel layer the automation planes use
    // (<source>.<channel>: ay1.a, gs.3, moonsound_fm.7)
    _channelsGroup = new QGroupBox("Channels", this);
    auto* channelsLayout = new QVBoxLayout(_channelsGroup);
    auto* channelSourceRow = new QHBoxLayout();
    channelSourceRow->addWidget(new QLabel("Source:", _channelsGroup));
    _channelSourceCombo = new QComboBox(_channelsGroup);
    _channelSourceCombo->setToolTip("The source whose channels are shown");
    channelSourceRow->addWidget(_channelSourceCombo, 1);
    channelsLayout->addLayout(channelSourceRow);
    _channelsContainer = new QWidget(_channelsGroup);
    channelsLayout->addWidget(_channelsContainer);
    _channelsGroup->setVisible(false);
    layout->addWidget(_channelsGroup);

    // ============ AY/TurboSound section ============
    // Titled "TurboSound FM" when the slot device is TSFM, "TurboSound" otherwise
    // (refreshFromContext); the FM trim row appears only then (§8.3)
    _ayGroup = new QGroupBox("TurboSound", this);
    auto* ayLayout = new QVBoxLayout(_ayGroup);

    // Stereo Mode
    auto* stereoRow = new QHBoxLayout();
    stereoRow->addWidget(new QLabel("Stereo:", this));
    _stereoModeCombo = new QComboBox(this);
    _stereoModeCombo->addItem("ABC", 0);
    _stereoModeCombo->addItem("ACB", 1);
    _stereoModeCombo->addItem("Mono", 2);
    _stereoModeCombo->setToolTip("Stereo panning layout");
    stereoRow->addWidget(_stereoModeCombo);

    stereoRow->addWidget(new QLabel("Model:", this));
    _chipModelCombo = new QComboBox(this);
    _chipModelCombo->addItem("AY-3-8910", 0);
    _chipModelCombo->addItem("YM2149", 1);
    _chipModelCombo->setToolTip("AY-3-8910: brighter\nYM2149: warmer");
    stereoRow->addWidget(_chipModelCombo);
    ayLayout->addLayout(stereoRow);

    // EQ profile (voicing): the tonal balance of the AY / SSG output. Not an HQ
    // effect - it runs with the FIR filter off too, so it stays enabled
    auto* voicingRow = new QHBoxLayout();
    voicingRow->addWidget(new QLabel("EQ profile:", this));
    _ayVoicingCombo = new QComboBox(this);
    FilterVoicing::forEachVisible([this](const FilterVoicing::Profile& profile) {
        _ayVoicingCombo->addItem(QString::fromUtf8(profile.label), QString::fromUtf8(profile.id));
    });
    _ayVoicingCombo->setToolTip(
        "Real AY boards send very low bass and the thump of volume changes\n"
        "straight to the output. Classic (default) trims them; Flat is the hardware line out;\n"
        "TV speaker also drops the deep bass and softens the highs, like a TV set;\n"
        "Headphones keeps Classic bass and softens the harsh square-wave highs;\n"
        "Warm sits between Headphones and TV speaker: softer bass and softer highs;\n"
        "Small speaker sounds like a clone's built-in speaker or a cheap amplifier.\n"
        "Recordings use the same setting.");
    voicingRow->addWidget(_ayVoicingCombo, 1);
    ayLayout->addLayout(voicingRow);

    // DSP options row
    auto* dspRow = new QHBoxLayout();
    _firCheckbox = new QCheckBox("FIR Filter", this);
    _firCheckbox->setToolTip("High-quality FIR decimation (brighter, more CPU)");
    dspRow->addWidget(_firCheckbox);

    _ayPunchCheckbox = new QCheckBox("Punch", this);
    _ayPunchCheckbox->setToolTip("Transient enhancement");
    dspRow->addWidget(_ayPunchCheckbox);

    dspRow->addWidget(new QLabel("Room:", this));
    _ayRoomCombo = new QComboBox(this);
    _ayRoomCombo->addItem("Off", 0);
    _ayRoomCombo->addItem("-15dB", 1);
    _ayRoomCombo->addItem("-12dB", 4);
    _ayRoomCombo->addItem("-9dB", 5);
    _ayRoomCombo->addItem("-6dB", 6);
    _ayRoomCombo->addItem("-3dB", 7);
    _ayRoomCombo->setToolTip("Crossfeed for headphones");
    dspRow->addWidget(_ayRoomCombo);
    ayLayout->addLayout(dspRow);

    _punchVoicingHint = new QLabel("Punch was tuned for Classic voicing", this);
    _punchVoicingHint->setStyleSheet("color: gray; font-size: 11px;");
    _punchVoicingHint->setVisible(false);
    ayLayout->addWidget(_punchVoicingHint);

    // TSFM FM trim row (hidden until the device has FM channels): ±12 dB in
    // 0.5 dB steps around the measured hardware level (§7.1, ISSUES.md #1)
    _tsfmControls = new QWidget(_ayGroup);
    auto* fmTrimRow = new QHBoxLayout(_tsfmControls);
    fmTrimRow->setContentsMargins(0, 0, 0, 0);
    fmTrimRow->addWidget(new QLabel("FM trim:", _tsfmControls));
    _fmTrimSlider = new QSlider(Qt::Horizontal, _tsfmControls);
    _fmTrimSlider->setRange(-24, 24);  // half-dB units: -12.0 .. +12.0 dB
    _fmTrimSlider->setValue(0);
    _fmTrimSlider->setToolTip("FM loudness relative to the hardware-derived default (0 dB)\n"
                              "Applies live to every YM2203 FM: the TurboSound FM and the ZX-MultiSound");
    fmTrimRow->addWidget(_fmTrimSlider, 1);
    _fmTrimLabel = new QLabel("+0.0 dB", _tsfmControls);
    _fmTrimLabel->setMinimumWidth(56);
    fmTrimRow->addWidget(_fmTrimLabel);
    _tsfmControls->setVisible(false);  // Hidden until the slot device is TSFM
    ayLayout->addWidget(_tsfmControls);

    layout->addWidget(_ayGroup);

    // ============ Beeper section ============
    _beeperGroup = new QGroupBox("Beeper (1-bit)", this);
    auto* beeperLayout = new QHBoxLayout(_beeperGroup);
    _beeperPunchCheckbox = new QCheckBox("Punch", this);
    _beeperPunchCheckbox->setToolTip("Attack enhancement for digidrums");
    beeperLayout->addWidget(_beeperPunchCheckbox);
    beeperLayout->addStretch();
    layout->addWidget(_beeperGroup);

    // ============ General Sound slot section (shown only when a card is fitted) ============
    _gsGroup = new QGroupBox("General Sound slot", this);
    auto* gsLayout = new QVBoxLayout(_gsGroup);

    auto* cardRow = new QHBoxLayout();
    cardRow->addWidget(new QLabel("Card:", _gsGroup));
    _gsCardCombo = new QComboBox(_gsGroup);
    _gsCardCombo->addItem("General Sound (classic)", static_cast<int>(GSTypeKind::Z80));
    _gsCardCombo->addItem("General Sound (lightweight player)", static_cast<int>(GSTypeKind::LW));
    _gsCardCombo->addItem("NeoGS", static_cast<int>(GSTypeKind::NGS));
    _gsCardCombo->setToolTip("The card in the GS slot. Another card is a slot change: the machine restarts with it "
                             "(its state is lost; disks and tapes stay in) - see Machine > Slots");
    cardRow->addWidget(_gsCardCombo, 1);
    gsLayout->addLayout(cardRow);

    _neoGSControls = new QWidget(_gsGroup);
    auto* sdRow = new QHBoxLayout(_neoGSControls);
    sdRow->setContentsMargins(0, 0, 0, 0);
    sdRow->addWidget(new QLabel("SD card:", _neoGSControls));
    _neoGSSdLabel = new QLabel(_neoGSControls);
    _neoGSSdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sdRow->addWidget(_neoGSSdLabel, 1);
    _neoGSInsertButton = new QPushButton("Insert...", _neoGSControls);
    _neoGSInsertButton->setToolTip("Insert a raw SD card image (FAT16 / FAT32). Refused while TTD records");
    sdRow->addWidget(_neoGSInsertButton);
    _neoGSEjectButton = new QPushButton("Eject", _neoGSControls);
    _neoGSEjectButton->setToolTip("Remove the SD card. Refused while TTD records");
    sdRow->addWidget(_neoGSEjectButton);
    gsLayout->addWidget(_neoGSControls);

    _neoGSStereoRow = new QWidget(_gsGroup);
    auto* neoGSStereoLayout = new QHBoxLayout(_neoGSStereoRow);
    neoGSStereoLayout->setContentsMargins(0, 0, 0, 0);
    neoGSStereoLayout->addWidget(new QLabel("Stereo:", _neoGSStereoRow));
    _neoGSStereoCombo = new QComboBox(_neoGSStereoRow);
    _neoGSStereoCombo->addItem("Separated (as on the board)", static_cast<int>(NeoGSConfig::StereoMode::Separated));
    _neoGSStereoCombo->addItem("Legacy GS (50% cross-feed)", static_cast<int>(NeoGSConfig::StereoMode::GS));
    _neoGSStereoCombo->addItem("Mono", static_cast<int>(NeoGSConfig::StereoMode::Mono));
    _neoGSStereoCombo->setToolTip("How the NeoGS DAC channels reach the two sides. The board keeps them hard left/right; "
                                  "the classic GS board cross-feeds the other side at half level. The MP3 decoder keeps its own stereo");
    neoGSStereoLayout->addWidget(_neoGSStereoCombo, 1);
    gsLayout->addWidget(_neoGSStereoRow);

    _gsStatusLabel = new QLabel(_gsGroup);
    _gsStatusLabel->setWordWrap(true);
    _gsStatusLabel->setStyleSheet("color: gray;");
    _gsStatusLabel->setVisible(false);
    gsLayout->addWidget(_gsStatusLabel);

    _gsGroup->setVisible(false);  // Hidden until we know a GS card is fitted
    layout->addWidget(_gsGroup);

    // ============ SOUNDRIVE/COVOX section (shown only when present) ============
    _covoxGroup = new QGroupBox("SOUNDRIVE / COVOX (4x 8-bit DAC)", this);
    auto* covoxLayout = new QVBoxLayout(_covoxGroup);

    // DC removal row
    auto* dcRow = new QHBoxLayout();
    _covoxDCRemovalCheckbox = new QCheckBox("DC Offset Removal", this);
    _covoxDCRemovalCheckbox->setToolTip("Removes DC bias from digitized audio");
    dcRow->addWidget(_covoxDCRemovalCheckbox);
    dcRow->addStretch();
    covoxLayout->addLayout(dcRow);

    _covoxGroup->setVisible(false);  // Hidden until we know Covox is present
    layout->addWidget(_covoxGroup);

    layout->addStretch();
}

void AudioSettingsWidget::rebuildSourcesSection()
{
    // Clear existing rows
    for (auto& row : _sourceRows)
    {
        delete row.nameLabel;
        delete row.activityDot;
        delete row.muteCheck;
        delete row.soloCheck;
        delete row.volumeSlider;
        delete row.volumeLabel;
        delete row.meter;
    }
    _sourceRows.clear();

    // Delete old layout
    if (_sourcesContainer->layout())
        delete _sourcesContainer->layout();

    auto* grid = new QGridLayout(_sourcesContainer);
    grid->setContentsMargins(0, 0, 0, 0);

    // Header row
    grid->addWidget(new QLabel("", this), 0, 0);  // activity dot column
    grid->addWidget(new QLabel("M", this), 0, 1);
    grid->addWidget(new QLabel("S", this), 0, 2);
    grid->addWidget(new QLabel("Volume", this), 0, 3);
    grid->addWidget(new QLabel("", this), 0, 4);  // volume label
    grid->addWidget(new QLabel("Level", this), 0, 5);

    if (!context() || !context()->pSoundManager)
        return;

    const auto& devices = context()->pSoundManager->devices();
    int row = 1;

    for (const auto& dev : devices)
    {
        SourceRow sr;
        sr.type = dev.type;

        sr.activityDot = new QLabel("●", this);
        sr.activityDot->setStyleSheet("color: gray;");
        grid->addWidget(sr.activityDot, row, 0);

        sr.nameLabel = new QLabel(QString::fromStdString(dev.name), this);
        grid->addWidget(sr.nameLabel, row, 0);
        sr.activityDot->setParent(nullptr);  // Move activity dot before name
        auto* nameRow = new QHBoxLayout();
        nameRow->setContentsMargins(0, 0, 0, 0);
        nameRow->addWidget(sr.activityDot);
        nameRow->addWidget(sr.nameLabel);
        nameRow->addStretch();
        auto* nameWidget = new QWidget(this);
        nameWidget->setLayout(nameRow);
        grid->addWidget(nameWidget, row, 0);

        sr.muteCheck = new QCheckBox(this);
        sr.muteCheck->setProperty("sourceType", static_cast<int>(dev.type));
        sr.muteCheck->setChecked(dev.mute);
        sr.muteCheck->setToolTip("Mute — remove this device from the mix");
        grid->addWidget(sr.muteCheck, row, 1);

        sr.soloCheck = new QCheckBox(this);
        sr.soloCheck->setProperty("sourceType", static_cast<int>(dev.type));
        sr.soloCheck->setChecked(dev.solo);
        sr.soloCheck->setToolTip("Solo — hear only soloed devices (multiple allowed)");
        grid->addWidget(sr.soloCheck, row, 2);

        sr.volumeSlider = new QSlider(Qt::Horizontal, this);
        sr.volumeSlider->setRange(0, 100);
        sr.volumeSlider->setValue(static_cast<int>(dev.volume * 100));
        sr.volumeSlider->setProperty("sourceType", static_cast<int>(dev.type));
        grid->addWidget(sr.volumeSlider, row, 3);

        sr.volumeLabel = new QLabel(QString("%1%").arg(static_cast<int>(dev.volume * 100)), this);
        sr.volumeLabel->setMinimumWidth(40);
        grid->addWidget(sr.volumeLabel, row, 4);

        sr.meter = new QProgressBar(this);
        sr.meter->setRange(0, 100);
        sr.meter->setValue(0);
        sr.meter->setTextVisible(false);
        sr.meter->setMaximumHeight(12);
        sr.meter->setMinimumWidth(60);
        grid->addWidget(sr.meter, row, 5);

        _sourceRows.push_back(sr);
        row++;
    }
}

void AudioSettingsWidget::connectSignals()
{
    if (_signalsConnected)
        return;

    // Source row signals
    for (auto& row : _sourceRows)
    {
        connect(row.muteCheck, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onSourceMuteChanged);
        connect(row.soloCheck, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onSourceSoloChanged);
        connect(row.volumeSlider, &QSlider::valueChanged, this, &AudioSettingsWidget::onSourceVolumeChanged);
    }

    // AY section
    connect(_stereoModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioSettingsWidget::onStereoModeChanged);
    connect(_chipModelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioSettingsWidget::onChipModelChanged);
    connect(_firCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onFirChanged);
    connect(_ayVoicingCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioSettingsWidget::onAYVoicingChanged);
    connect(_ayPunchCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onAYPunchChanged);
    connect(_ayRoomCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioSettingsWidget::onAYRoomModeChanged);

    // TSFM FM trim
    connect(_fmTrimSlider, &QSlider::valueChanged, this, &AudioSettingsWidget::onFmTrimChanged);

    // Channels section: its source selector (the rows connect as they are built)
    connect(_channelSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &AudioSettingsWidget::onChannelSourceChanged);

    // Beeper
    connect(_beeperPunchCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onBeeperPunchChanged);

    // General Sound slot
    connect(_gsCardCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AudioSettingsWidget::onGSCardChanged);
    connect(_neoGSInsertButton, &QPushButton::clicked, this, &AudioSettingsWidget::onNeoGSInsertSd);
    connect(_neoGSEjectButton, &QPushButton::clicked, this, &AudioSettingsWidget::onNeoGSEjectSd);
    connect(_neoGSStereoCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AudioSettingsWidget::onNeoGSStereoModeChanged);

    // Covox
    connect(_covoxDCRemovalCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onCovoxDCRemovalChanged);

    _signalsConnected = true;
}

void AudioSettingsWidget::disconnectSignals()
{
    if (!_signalsConnected)
        return;

    for (auto& row : _sourceRows)
    {
        disconnect(row.muteCheck, nullptr, this, nullptr);
        disconnect(row.soloCheck, nullptr, this, nullptr);
        disconnect(row.volumeSlider, nullptr, this, nullptr);
    }

    disconnect(_stereoModeCombo, nullptr, this, nullptr);
    disconnect(_chipModelCombo, nullptr, this, nullptr);
    disconnect(_firCheckbox, nullptr, this, nullptr);
    disconnect(_ayVoicingCombo, nullptr, this, nullptr);
    disconnect(_ayPunchCheckbox, nullptr, this, nullptr);
    disconnect(_ayRoomCombo, nullptr, this, nullptr);
    disconnect(_fmTrimSlider, nullptr, this, nullptr);

    disconnect(_channelSourceCombo, nullptr, this, nullptr);

    disconnect(_beeperPunchCheckbox, nullptr, this, nullptr);
    disconnect(_gsCardCombo, nullptr, this, nullptr);
    disconnect(_neoGSInsertButton, nullptr, this, nullptr);
    disconnect(_neoGSEjectButton, nullptr, this, nullptr);
    disconnect(_neoGSStereoCombo, nullptr, this, nullptr);
    disconnect(_covoxDCRemovalCheckbox, nullptr, this, nullptr);

    _signalsConnected = false;
}

void AudioSettingsWidget::refreshFromContext()
{
    disconnectSignals();

    bool hasContext = context() && context()->pSoundManager;

    _controlsContainer->setEnabled(hasContext);
    _statusLabel->setVisible(!hasContext);
    updateDeviceInfo();

    // Rebuild sources section from registry
    rebuildSourcesSection();

    if (hasContext)
    {
        SoundManager* sm = context()->pSoundManager;

        // The AY socket's stereo mode and chip model as requested (automation and this panel set the same values)
        _stereoModeCombo->setCurrentIndex(static_cast<int>(sm->getAYStereoMode()));
        _chipModelCombo->setCurrentIndex(static_cast<int>(sm->getAYChipModel()));

        // TurboSound slot kind: the whole AY/TurboSound block retitles and
        // re-labels for TSFM, the FM trim row appears, and the chip-model
        // selector locks to the board's YM2203 (§8.3)
        ITurboSoundDevice* tsDevice = sm->getTurboSound();
        const bool isFm = tsDevice && tsDevice->hasFm();
        _ayGroup->setTitle(!tsDevice ? "TurboSound (not fitted)" : (isFm ? "TurboSound FM" : "TurboSound"));
        _ayGroup->setEnabled(tsDevice != nullptr);
        // FM trim: one calibration for every YM2203 FM in the machine - the TurboSound FM in the socket and a
        // ZX-MultiSound in a slot ([SOUND] TSFM_FmLevelDb on both)
        double fmTrim = 0.0;
        const bool hasFmTrim = sm->fmTrimDb(fmTrim);
        _tsfmControls->setVisible(hasFmTrim);
        if (hasFmTrim)
        {
            _fmTrimSlider->setValue(static_cast<int>(std::lround(fmTrim * 2.0)));
            _fmTrimLabel->setText(QString("%1%2 dB").arg(fmTrim >= 0 ? "+" : "").arg(fmTrim, 0, 'f', 1));
        }
        _chipModelCombo->setEnabled(!isFm);
        _chipModelCombo->setToolTip(isFm
                                        ? "Locked: the TSFM board is 2 x YM2203\n"
                                          "(each chip's SSG half is a fixed YM2149)"
                                        : "AY-3-8910: brighter\nYM2149: warmer");
        if (isFm)
        {
            // Name the chips the machine actually has; the fixed YM2149 DAC
            // curve of the SSG half is an internal detail (§8.3)
            if (_chipModelCombo->count() != 1)
            {
                _chipModelCombo->clear();
                _chipModelCombo->addItem("YM2203");
            }
            _chipModelCombo->setCurrentIndex(0);
        }
        else if (_chipModelCombo->count() != 2)
        {
            // Restore the AY-3-8910 / YM2149 choice after an FM instance
            // collapsed it (the GUI can adopt instances of either kind)
            _chipModelCombo->clear();
            _chipModelCombo->addItem("AY-3-8910", 0);
            _chipModelCombo->addItem("YM2149", 1);
            _chipModelCombo->setCurrentIndex(static_cast<int>(sm->getAYChipModel()));
        }

        // Sound character: the instance's requested values (a change made
        // through automation shows up here too)
        const int voicingIndex =
            _ayVoicingCombo->findData(QString::fromUtf8(FilterVoicing::presetId(sm->getAYVoicing())));
        if (voicingIndex >= 0)
            _ayVoicingCombo->setCurrentIndex(voicingIndex);
        _ayPunchCheckbox->setChecked(sm->getAYPunch());

        // The combo offers a subset of the room levels: select by value (the
        // item data is the RoomMode), and show a level set elsewhere as an extra item
        const int roomValue = static_cast<int>(sm->getAYRoomMode());
        int roomIndex = _ayRoomCombo->findData(roomValue);
        if (roomIndex < 0)
        {
            const QString name = QString::fromUtf8(AudioCharacterChain::roomModeId(sm->getAYRoomMode()));
            _ayRoomCombo->addItem("-" + name.left(name.size() - 2) + "dB", roomValue);
            roomIndex = _ayRoomCombo->count() - 1;
        }
        _ayRoomCombo->setCurrentIndex(roomIndex);

        // Beeper settings
        _beeperPunchCheckbox->setChecked(sm->getBeeperPunch());
        updatePunchVoicingHint();

        // FIR filter
        if (context()->pFeatureManager)
        {
            _firCheckbox->setChecked(context()->pFeatureManager->isEnabled(Features::kSoundHQ));
        }

        // General Sound slot section
        _gsStatusLabel->setVisible(false);
        updateGSSection(sm->generalSoundSlot());

        // SOUNDRIVE/Covox section
        bool hasCovox = sm->hasCovox();
        _covoxGroup->setVisible(hasCovox);
        if (hasCovox)
        {
            Covox* covox = sm->getCovox();
            _covoxDCRemovalCheckbox->setChecked(covox->isDCRemovalEnabled());
        }

        // Solo indicator
        updateSoloIndicator();

        // Channels section
        rebuildChannelsSection();

        // Start meter updates
        _meterTimer->start(100);
    }
    else
    {
        _meterTimer->stop();
    }

    connectSignals();
}

void AudioSettingsWidget::updateSoloIndicator()
{
    bool anySolo = false;
    if (context() && context()->pSoundManager)
    {
        for (const auto& dev : context()->pSoundManager->devices())
        {
            if (dev.solo)
            {
                anySolo = true;
                break;
            }
        }
    }
    _soloIndicator->setText(anySolo ? "SOLO" : "");
    _soloIndicator->setVisible(anySolo);
}

// ============ Source row slots ============

void AudioSettingsWidget::onSourceMuteChanged(int state)
{
    auto* check = qobject_cast<QCheckBox*>(sender());
    if (!check || !context() || !context()->pSoundManager)
        return;

    auto type = static_cast<AudioSourceType>(check->property("sourceType").toInt());
    context()->pSoundManager->setDeviceMute(type, state == Qt::Checked);
}

void AudioSettingsWidget::onSourceSoloChanged(int state)
{
    auto* check = qobject_cast<QCheckBox*>(sender());
    if (!check || !context() || !context()->pSoundManager)
        return;

    auto type = static_cast<AudioSourceType>(check->property("sourceType").toInt());
    context()->pSoundManager->setDeviceSolo(type, state == Qt::Checked);
    updateSoloIndicator();
}

void AudioSettingsWidget::onSourceVolumeChanged(int value)
{
    auto* slider = qobject_cast<QSlider*>(sender());
    if (!slider || !context() || !context()->pSoundManager)
        return;

    auto type = static_cast<AudioSourceType>(slider->property("sourceType").toInt());
    context()->pSoundManager->setDeviceVolume(type, value / 100.0f);

    // Update label
    for (auto& row : _sourceRows)
    {
        if (row.type == type)
        {
            row.volumeLabel->setText(QString("%1%").arg(value));
            break;
        }
    }
}

// ============ AY slots ============

void AudioSettingsWidget::onStereoModeChanged(int index)
{
    // The socket's chips at the next frame start (the same request automation makes: setting ay_stereo)
    if (context() && context()->pSoundManager && index >= 0)
        context()->pSoundManager->setAYStereoMode(static_cast<AYStereoMode>(index));
}

void AudioSettingsWidget::onChipModelChanged(int index)
{
    // TSFM: the YM2203's SSG half is a fixed YM2149 (§8.3) - the selector is locked, and the request is refused
    if (context() && context()->pSoundManager && index >= 0)
        context()->pSoundManager->setAYChipModel(static_cast<AYChipModel>(index));
}

void AudioSettingsWidget::onAYVoicingChanged(int index)
{
    if (!context() || !context()->pSoundManager || index < 0)
        return;

    const std::string id = _ayVoicingCombo->itemData(index).toString().toStdString();
    FilterVoicing::Preset preset = FilterVoicing::Preset::Classic;
    if (!FilterVoicing::parsePreset(id, preset))
        return;

    // Thread-safe request: the audio switches click-free at the next frame
    context()->pSoundManager->setAYVoicing(preset);
    SoundCharacterPreferences::Save("ay_voicing", id);
    updatePunchVoicingHint();
}

void AudioSettingsWidget::onAYPunchChanged(int state)
{
    if (context() && context()->pSoundManager)
    {
        const bool enabled = state == Qt::Checked;
        context()->pSoundManager->setAYPunch(enabled);
        SoundCharacterPreferences::Save("ay_punch", enabled ? "on" : "off");
        updatePunchVoicingHint();
    }
}

void AudioSettingsWidget::onAYRoomModeChanged(int index)
{
    if (context() && context()->pSoundManager)
    {
        auto mode = static_cast<AudioCharacterChain::RoomMode>(_ayRoomCombo->itemData(index).toInt());
        context()->pSoundManager->setAYRoomMode(mode);
        SoundCharacterPreferences::Save("ay_room", AudioCharacterChain::roomModeId(mode));
    }
}

void AudioSettingsWidget::updatePunchVoicingHint()
{
    // Informational only: the punch presets were tuned by ear on the Classic
    // balance; with Flat the punch envelope also follows the volume-step thumps
    const bool flat = _ayVoicingCombo->currentData().toString() == QString::fromUtf8(
                                                                       FilterVoicing::presetId(FilterVoicing::Preset::Flat));
    _punchVoicingHint->setVisible(flat && _ayPunchCheckbox->isChecked());
}

void AudioSettingsWidget::onFirChanged(int state)
{
    if (context() && context()->pFeatureManager)
        context()->pFeatureManager->setFeature("soundhq", state == Qt::Checked);
}

void AudioSettingsWidget::onFmTrimChanged(int value)
{
    if (!context() || !context()->pSoundManager)
        return;

    // Half-dB slider units -> dB, applied live to every YM2203 FM: the TurboSound FM's two chips and a ZX-MultiSound's
    // (§7.1; the same [SOUND] TSFM_FmLevelDb calibration)
    const double db = value / 2.0;
    context()->pSoundManager->setFmTrimDb(db);
    _fmTrimLabel->setText(QString("%1%2 dB").arg(db >= 0 ? "+" : "").arg(db, 0, 'f', 1));
}

// ============ Channels section ============

AudioSourceType AudioSettingsWidget::selectedChannelSource() const
{
    const int index = _channelSourceCombo->currentIndex();
    return index >= 0 && static_cast<size_t>(index) < _channelSources.size() ? _channelSources[static_cast<size_t>(index)]
                                                                             : AudioSourceType::Custom;
}

void AudioSettingsWidget::rebuildChannelsSection()
{
    const AudioSourceType selected = selectedChannelSource();
    _channelSources.clear();
    const QSignalBlocker blocker(_channelSourceCombo);
    _channelSourceCombo->clear();
    if (context() && context()->pSoundManager)
    {
        SoundManager* sm = context()->pSoundManager;
        for (const AudioDeviceInfo& dev : sm->devices())
        {
            if (sm->mixerChannels(dev.type).empty())
                continue;
            _channelSources.push_back(dev.type);
            _channelSourceCombo->addItem(QString::fromStdString(dev.name));
        }
    }
    _channelsGroup->setVisible(!_channelSources.empty());
    const auto at = std::find(_channelSources.begin(), _channelSources.end(), selected);
    _channelSourceCombo->setCurrentIndex(at != _channelSources.end() ? static_cast<int>(at - _channelSources.begin()) : 0);
    rebuildChannelRows();
}

void AudioSettingsWidget::onChannelSourceChanged(int index)
{
    (void)index;
    rebuildChannelRows();
}

void AudioSettingsWidget::rebuildChannelRows()
{
    for (ChannelRow& row : _channelRows)
    {
        delete row.nameLabel;
        delete row.muteCheck;
        delete row.soloCheck;
        delete row.volumeSlider;
        delete row.volumeLabel;
        delete row.meter;
    }
    _channelRows.clear();
    delete _channelsContainer->layout();

    auto* grid = new QGridLayout(_channelsContainer);
    grid->setContentsMargins(0, 0, 0, 0);
    if (!context() || !context()->pSoundManager)
        return;
    SoundManager* sm = context()->pSoundManager;
    const AudioSourceType type = selectedChannelSource();
    const std::vector<SoundManager::MixerChannelState> channels = sm->mixerChannels(type);
    int row = 0;
    for (const SoundManager::MixerChannelState& c : channels)
    {
        ChannelRow cr;
        cr.key = c.desc.key;
        cr.nameLabel = new QLabel(QString::fromStdString(c.desc.name), _channelsContainer);
        grid->addWidget(cr.nameLabel, row, 0);

        cr.muteCheck = new QCheckBox("M", _channelsContainer);
        cr.muteCheck->setChecked(c.mute);
        cr.muteCheck->setToolTip("Mute this channel");
        grid->addWidget(cr.muteCheck, row, 1);
        cr.soloCheck = new QCheckBox("S", _channelsContainer);
        cr.soloCheck->setChecked(c.solo);
        cr.soloCheck->setToolTip("Solo: hear only the soloed channels of this source");
        grid->addWidget(cr.soloCheck, row, 2);

        const std::string key = c.desc.key;
        connect(cr.muteCheck, &QCheckBox::checkStateChanged, this, [this, type, key](Qt::CheckState state) {
            std::string error;
            if (context() && context()->pSoundManager)
                context()->pSoundManager->setMixerChannel(type, key, state == Qt::Checked ? 1 : 0, -1, -1.0f, error);
        });
        connect(cr.soloCheck, &QCheckBox::checkStateChanged, this, [this, type, key](Qt::CheckState state) {
            std::string error;
            if (context() && context()->pSoundManager)
                context()->pSoundManager->setMixerChannel(type, key, -1, state == Qt::Checked ? 1 : 0, -1.0f, error);
        });

        if (c.desc.hasVolume)
        {
            const int percent = static_cast<int>(std::lround(c.volume * 100.0f));
            cr.volumeSlider = new QSlider(Qt::Horizontal, _channelsContainer);
            cr.volumeSlider->setRange(0, 100);
            cr.volumeSlider->setValue(percent);
            grid->addWidget(cr.volumeSlider, row, 3);
            cr.volumeLabel = new QLabel(QString("%1%").arg(percent), _channelsContainer);
            cr.volumeLabel->setMinimumWidth(40);
            grid->addWidget(cr.volumeLabel, row, 4);
            QLabel* label = cr.volumeLabel;
            connect(cr.volumeSlider, &QSlider::valueChanged, this, [this, type, key, label](int value) {
                label->setText(QString("%1%").arg(value));
                std::string error;
                if (context() && context()->pSoundManager)
                    context()->pSoundManager->setMixerChannel(type, key, -1, -1, value / 100.0f, error);
            });
        }
        if (c.desc.hasMeter)
        {
            cr.meter = new QProgressBar(_channelsContainer);
            cr.meter->setRange(0, 100);
            cr.meter->setValue(0);
            cr.meter->setTextVisible(false);
            cr.meter->setMaximumHeight(12);
            cr.meter->setMinimumWidth(60);
            grid->addWidget(cr.meter, row, 5);
        }
        _channelRows.push_back(cr);
        row++;
    }
}

void AudioSettingsWidget::onBeeperPunchChanged(int state)
{
    if (context() && context()->pSoundManager)
    {
        const bool enabled = state == Qt::Checked;
        context()->pSoundManager->setBeeperPunch(enabled);
        SoundCharacterPreferences::Save("beeper_punch", enabled ? "on" : "off");
    }
}

// ============ Covox slots ============

void AudioSettingsWidget::onCovoxDCRemovalChanged(int state)
{
    if (context() && context()->pSoundManager && context()->pSoundManager->hasCovox())
        context()->pSoundManager->getCovox()->setDCRemovalEnabled(state == Qt::Checked);
}

// ============ Meter updates ============

void AudioSettingsWidget::updateDeviceInfo()
{
    if (!_deviceInfoLabel)
        return;

    QString text;
    if (!context() || !context()->pSoundManager)
    {
        text = "Audio device: — (no active emulator)";
    }
    else
    {
        const size_t coreRate = context()->pSoundManager->getCoreRate();
        if (const AudioDeviceDescriptor* dev = context()->pAudioDeviceDescriptor.load(std::memory_order_acquire))
        {
            text = QString("Device: %1 @ %2 Hz · Core: %3 Hz")
                       .arg(QString::fromUtf8(dev->deviceName))
                       .arg(dev->sampleRate.load(std::memory_order_relaxed))
                       .arg(coreRate);
        }
        else
        {
            text = QString("Device: none · Core: %1 Hz").arg(coreRate);
        }
    }

    // Re-elide against the CURRENT width every tick: the stretch layout
    // hands the label all row width except the (i) button, and the first
    // update can run before layout when the width is still tiny. Repaint
    // only when the elided result actually differs.
    _deviceInfoLabel->setToolTip(text);
    const int avail = _deviceInfoLabel->width() > 50 ? _deviceInfoLabel->width() - 4 : 600;
    const QString elided = _deviceInfoLabel->fontMetrics().elidedText(text, Qt::ElideRight, avail);
    if (elided != _deviceInfoLabel->text())
        _deviceInfoLabel->setText(elided);
}

/// Full latency/A-V breakdown, built on demand for the (i) popup. Each
/// component is listed separately so the SOURCE of a desync is identifiable:
/// ring+HW+device-output = audio presentation delay; video present latency
/// is stamped by the GUI frame source; their difference is the A/V offset.
QString AudioSettingsWidget::buildDeviceDetailText() const
{
    if (!context() || !context()->pSoundManager)
        return "No active emulator";

    const AudioDeviceDescriptor* dev = context()->pAudioDeviceDescriptor.load(std::memory_order_acquire);
    if (!dev)
        return QString("No audio device attached · Core: %1 Hz")
            .arg(context()->pSoundManager->getCoreRate());

    const uint32_t rate = dev->sampleRate.load(std::memory_order_relaxed);
    const double frameToMs = rate ? 1000.0 / rate : 0.0;
    const double ringInstantMs = dev->occupancyMs();

    // Ring occupancy is a SAWTOOTH (a whole emulated frame lands at once,
    // then drains continuously): the instantaneous value swings +-1/2 frame
    // (~10 ms) around the setpoint depending on sampling phase. The
    // DRC-filtered EMA is the stable figure - per-sample latency is
    // constant; use it for the A/V offset so the readout doesn't wander.
    const double ringFilteredMs = context()->pSoundManager->getDrcFilteredOccupancyMs();
    const double ringMs = (ringFilteredMs > 0.0) ? ringFilteredMs : ringInstantMs;

    const double hwMs = dev->deviceBufferFrames.load(std::memory_order_relaxed) * frameToMs;
    const double devOutMs = dev->deviceOutputLatencyFrames.load(std::memory_order_relaxed) * frameToMs;
    const double audioMs = ringMs + hwMs + devOutMs;
    const double videoMs = context()->pVideoPresentLatencyUs.load(std::memory_order_relaxed) / 1000.0;

    QString text;
    text += QString("Device: %1 @ %2 Hz, %3 ch\n")
                .arg(QString::fromUtf8(dev->deviceName))
                .arg(rate)
                .arg(dev->channels.load(std::memory_order_relaxed));
    text += QString("Core rate: %1 Hz\n").arg(context()->pSoundManager->getCoreRate());
    text += QString("Audio delay: ring %1 ms (%2 now) + HW %3 ms + device output %4 ms = %5 ms\n")
                .arg(ringMs, 0, 'f', 1)
                .arg(ringInstantMs, 0, 'f', 1)
                .arg(hwMs, 0, 'f', 1)
                .arg(devOutMs, 0, 'f', 1)
                .arg(audioMs, 0, 'f', 1);

    if (videoMs > 0.0 && videoMs < 200.0)
    {
        const double avMs = audioMs - videoMs;
        text += QString("Video present: %1 ms\n").arg(videoMs, 0, 'f', 1);
        text += QString("A/V offset: %1%2 ms (audio %3)\n")
                    .arg(avMs >= 0 ? "+" : "")
                    .arg(avMs, 0, 'f', 1)
                    .arg(avMs >= 0 ? "late" : "early");
    }
    else
    {
        text += "A/V offset: — (video path idle)\n";
    }

    text += QString("Ring errors: %1 enq / %2 deq · Reroutes: %3")
                .arg(dev->enqueueErrors.load(std::memory_order_relaxed))
                .arg(dev->dequeueErrors.load(std::memory_order_relaxed))
                .arg(dev->reinitCount.load(std::memory_order_relaxed));
    return text;
}

void AudioSettingsWidget::onUpdateMeters()
{
    if (!context() || !context()->pSoundManager || !isVisible())
        return;

    updateDeviceInfo();

    // The GS slot changes on the emulation thread (a switch, automation):
    // another card renames the mixer source ("GS" / "NeoGS") and adds or
    // drops "NeoGS MP3", so the panel is rebuilt whenever the card or the
    // source list itself differs from what is shown; a new SD card only
    // needs its row
    const GeneralSoundSlot gsSlot = context()->pSoundManager->generalSoundSlot();
    const auto& devices = context()->pSoundManager->devices();
    bool sourcesChanged = gsSlot.kind != _shownGSSlot.kind || devices.size() != _sourceRows.size();
    for (size_t i = 0; !sourcesChanged && i < devices.size(); i++)
    {
        sourcesChanged = devices[i].type != _sourceRows[i].type ||
                         _sourceRows[i].nameLabel->text() != QString::fromStdString(devices[i].name);
    }
    if (sourcesChanged)
    {
        refreshFromContext();
        return;
    }
    if (gsSlot.sdCardImage != _shownGSSlot.sdCardImage)
        updateGSSection(gsSlot);

    for (auto& row : _sourceRows)
    {
        for (const auto& dev : devices)
        {
            if (dev.type == row.type)
            {
                // Update activity dot
                row.activityDot->setStyleSheet(dev.activeRecently ? "color: lime;" : "color: gray;");

                // Update meter (peak as percentage)
                int peakPct = static_cast<int>(dev.peak * 100);
                row.meter->setValue(peakPct);
                break;
            }
        }
    }

    // The Channels section: its rows' meters, and a state another surface changed (automation, a deck)
    const std::vector<SoundManager::MixerChannelState> channels =
        context()->pSoundManager->mixerChannels(selectedChannelSource());
    if (channels.size() != _channelRows.size())
    {
        rebuildChannelsSection();
        return;
    }
    for (size_t i = 0; i < channels.size(); i++)
    {
        ChannelRow& row = _channelRows[i];
        const SoundManager::MixerChannelState& c = channels[i];
        if (row.meter)
            row.meter->setValue(static_cast<int>(c.peak * 100.0f));
        const QSignalBlocker muteBlocker(row.muteCheck);
        const QSignalBlocker soloBlocker(row.soloCheck);
        row.muteCheck->setChecked(c.mute);
        row.soloCheck->setChecked(c.solo);
        if (row.volumeSlider && !row.volumeSlider->isSliderDown())
        {
            const int percent = static_cast<int>(std::lround(c.volume * 100.0f));
            if (row.volumeSlider->value() != percent)
            {
                const QSignalBlocker volumeBlocker(row.volumeSlider);
                row.volumeSlider->setValue(percent);
                row.volumeLabel->setText(QString("%1%").arg(percent));
            }
        }
    }
}

// ============ General Sound slot ============

void AudioSettingsWidget::updateGSSection(const GeneralSoundSlot& slot)
{
    _shownGSSlot = slot;
    const bool fitted = slot.kind != GSTypeKind::NONE;
    _gsGroup->setVisible(fitted);
    if (!fitted)
        return;

    const bool neoGS = slot.kind == GSTypeKind::NGS;
    _gsGroup->setTitle(neoGS ? "General Sound slot: NeoGS" : "General Sound slot");

    const QSignalBlocker blocker(_gsCardCombo);
    const int index = _gsCardCombo->findData(static_cast<int>(slot.kind));
    if (index >= 0)
        _gsCardCombo->setCurrentIndex(index);

    _neoGSControls->setVisible(neoGS);
    _neoGSStereoRow->setVisible(neoGS);
    if (neoGS && context() && context()->pSoundManager)
    {
        const QSignalBlocker stereoBlocker(_neoGSStereoCombo);
        const int stereoIndex = _neoGSStereoCombo->findData(static_cast<int>(context()->pSoundManager->neoGSStereoMode()));
        if (stereoIndex >= 0)
            _neoGSStereoCombo->setCurrentIndex(stereoIndex);
    }
    if (slot.sdCardImage.empty())
    {
        _neoGSSdLabel->setText("(empty)");
        _neoGSSdLabel->setToolTip(QString());
    }
    else
    {
        const QString path = QString::fromStdString(slot.sdCardImage);
        _neoGSSdLabel->setText(QFileInfo(path).fileName());
        _neoGSSdLabel->setToolTip(path);
    }
    _neoGSEjectButton->setEnabled(!slot.sdCardImage.empty());
}

void AudioSettingsWidget::onGSCardChanged(int index)
{
    if (!context() || !context()->pSoundManager)
        return;

    const auto target = static_cast<GSTypeKind>(_gsCardCombo->itemData(index).toInt());
    if (target == _shownGSSlot.kind)
        return;
    // The combo shows the card in the slot until the change is made: a slot change (owner decision Q10) that
    // restarts the machine after a confirmation; the new machine's card shows when the panel follows it
    {
        const QSignalBlocker blocker(_gsCardCombo);
        const int shown = _gsCardCombo->findData(static_cast<int>(_shownGSSlot.kind));
        if (shown >= 0)
            _gsCardCombo->setCurrentIndex(shown);
    }
    emit generalSoundCardRequested(static_cast<int>(target));
}

void AudioSettingsWidget::onNeoGSInsertSd()
{
    if (!context())
        return;

    const QString startDir = _shownGSSlot.sdCardImage.empty()
                                 ? QString()
                                 : QFileInfo(QString::fromStdString(_shownGSSlot.sdCardImage)).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, "Insert SD card image", startDir,
                                                      "SD card images (*.img *.ima *.bin *.hdf);;All files (*)");
    if (path.isEmpty())
        return;

    // Checked here, carried out on the emulation thread; the meter poll shows the result
    const NeoGSMediaResult result = NeoGSRequestSdInsert(context(), path.toStdString());
    _gsStatusLabel->setText(NeoGSMediaAccepted(result) ? QString() : QString("SD card: %1").arg(NeoGSMediaResultText(result)));
    _gsStatusLabel->setVisible(!NeoGSMediaAccepted(result));
}

void AudioSettingsWidget::onNeoGSEjectSd()
{
    if (!context())
        return;

    const NeoGSMediaResult result = NeoGSRequestSdEject(context());
    _gsStatusLabel->setText(NeoGSMediaAccepted(result) ? QString() : QString("SD card: %1").arg(NeoGSMediaResultText(result)));
    _gsStatusLabel->setVisible(!NeoGSMediaAccepted(result));
}

void AudioSettingsWidget::onNeoGSStereoModeChanged(int index)
{
    if (!context() || !context()->pSoundManager)
        return;
    // Kept by the SoundManager (any thread), taken by the card at the next frame
    context()->pSoundManager->setNeoGSStereoMode(static_cast<NeoGSConfig::StereoMode>(_neoGSStereoCombo->itemData(index).toInt()));
}
