#include "audiosettingswidget.h"
#include "base/featuremanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "emulator/sound/chips/soundchip_ay8910.h"

AudioSettingsWidget::AudioSettingsWidget(EmulatorContext* context, QWidget* parent)
    : QWidget(parent), _context(context)
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

void AudioSettingsWidget::setContext(EmulatorContext* context)
{
    _context = context;
    refreshFromContext();
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

    // ============ AY/TurboSound section ============
    _ayGroup = new QGroupBox("AY / TurboSound", this);
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

    // Channel Mixer (AY1 and AY2 channels, vertical layout)
    _channelMixerGroup = new QGroupBox("Channels", _ayGroup);
    auto* chLayout = new QGridLayout(_channelMixerGroup);
    const char* chNames[] = {"A", "B", "C"};
    int row = 0;

    // AY1 channels
    chLayout->addWidget(new QLabel("AY1", _channelMixerGroup), row++, 0, 1, 3);
    for (int ch = 0; ch < 3; ch++)
    {
        _channelMuteChecks[0][ch] = new QCheckBox(QString("Mute %1").arg(chNames[ch]), _channelMixerGroup);
        _channelMuteChecks[0][ch]->setToolTip(QString("Mute AY1 channel %1").arg(chNames[ch]));
        chLayout->addWidget(_channelMuteChecks[0][ch], row, 0);
        _channelVolumeSliders[0][ch] = new QSlider(Qt::Horizontal, _channelMixerGroup);
        _channelVolumeSliders[0][ch]->setRange(0, 100);
        _channelVolumeSliders[0][ch]->setValue(100);
        chLayout->addWidget(_channelVolumeSliders[0][ch], row, 1);
        _channelVolumeLabels[0][ch] = new QLabel("100%", _channelMixerGroup);
        _channelVolumeLabels[0][ch]->setMinimumWidth(40);
        chLayout->addWidget(_channelVolumeLabels[0][ch], row, 2);
        row++;
    }

    // AY2 channels
    chLayout->addWidget(new QLabel("AY2", _channelMixerGroup), row++, 0, 1, 3);
    for (int ch = 0; ch < 3; ch++)
    {
        _channelMuteChecks[1][ch] = new QCheckBox(QString("Mute %1").arg(chNames[ch]), _channelMixerGroup);
        _channelMuteChecks[1][ch]->setToolTip(QString("Mute AY2 channel %1").arg(chNames[ch]));
        chLayout->addWidget(_channelMuteChecks[1][ch], row, 0);
        _channelVolumeSliders[1][ch] = new QSlider(Qt::Horizontal, _channelMixerGroup);
        _channelVolumeSliders[1][ch]->setRange(0, 100);
        _channelVolumeSliders[1][ch]->setValue(100);
        chLayout->addWidget(_channelVolumeSliders[1][ch], row, 1);
        _channelVolumeLabels[1][ch] = new QLabel("100%", _channelMixerGroup);
        _channelVolumeLabels[1][ch]->setMinimumWidth(40);
        chLayout->addWidget(_channelVolumeLabels[1][ch], row, 2);
        row++;
    }
    ayLayout->addWidget(_channelMixerGroup);

    layout->addWidget(_ayGroup);

    // ============ Beeper section ============
    _beeperGroup = new QGroupBox("Beeper (1-bit)", this);
    auto* beeperLayout = new QHBoxLayout(_beeperGroup);
    _beeperFilterCheckbox = new QCheckBox("Lowpass", this);
    _beeperFilterCheckbox->setToolTip("Smooths harsh edges");
    beeperLayout->addWidget(_beeperFilterCheckbox);
    _beeperPunchCheckbox = new QCheckBox("Punch", this);
    _beeperPunchCheckbox->setToolTip("Attack enhancement for digidrums");
    beeperLayout->addWidget(_beeperPunchCheckbox);
    beeperLayout->addStretch();
    layout->addWidget(_beeperGroup);

    // ============ SOUNDRIVE/COVOX section (shown only when present) ============
    _covoxGroup = new QGroupBox("SOUNDRIVE / COVOX (4x 8-bit DAC)", this);
    auto* covoxLayout = new QVBoxLayout(_covoxGroup);

    // Channel mutes row
    auto* channelsRow = new QHBoxLayout();
    const char* covoxChNames[] = {"Left A", "Left B", "Right A", "Right B"};
    for (int i = 0; i < 4; i++)
    {
        _covoxChannelMute[i] = new QCheckBox(QString("Mute %1").arg(covoxChNames[i]), _covoxGroup);
        _covoxChannelMute[i]->setProperty("channel", i);
        channelsRow->addWidget(_covoxChannelMute[i]);
    }
    covoxLayout->addLayout(channelsRow);

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

    if (!_context || !_context->pSoundManager)
        return;

    const auto& devices = _context->pSoundManager->devices();
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
    connect(_ayPunchCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onAYPunchChanged);
    connect(_ayRoomCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioSettingsWidget::onAYRoomModeChanged);

    // Channel mixer (2 chips x 3 channels)
    for (int chip = 0; chip < 2; chip++)
    {
        for (int ch = 0; ch < 3; ch++)
        {
            _channelMuteChecks[chip][ch]->setProperty("chip", chip);
            _channelMuteChecks[chip][ch]->setProperty("channel", ch);
            _channelVolumeSliders[chip][ch]->setProperty("chip", chip);
            _channelVolumeSliders[chip][ch]->setProperty("channel", ch);
            connect(_channelMuteChecks[chip][ch], &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onChannelMuteChanged);
            connect(_channelVolumeSliders[chip][ch], &QSlider::valueChanged, this, &AudioSettingsWidget::onChannelVolumeChanged);
        }
    }

    // Beeper
    connect(_beeperFilterCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onBeeperFilterChanged);
    connect(_beeperPunchCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onBeeperPunchChanged);

    // Covox
    connect(_covoxDCRemovalCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onCovoxDCRemovalChanged);
    for (int i = 0; i < 4; i++)
    {
        connect(_covoxChannelMute[i], &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onCovoxChannelMuteChanged);
    }

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
    disconnect(_ayPunchCheckbox, nullptr, this, nullptr);
    disconnect(_ayRoomCombo, nullptr, this, nullptr);

    for (int chip = 0; chip < 2; chip++)
    {
        for (int ch = 0; ch < 3; ch++)
        {
            disconnect(_channelMuteChecks[chip][ch], nullptr, this, nullptr);
            disconnect(_channelVolumeSliders[chip][ch], nullptr, this, nullptr);
        }
    }

    disconnect(_beeperFilterCheckbox, nullptr, this, nullptr);
    disconnect(_beeperPunchCheckbox, nullptr, this, nullptr);
    disconnect(_covoxDCRemovalCheckbox, nullptr, this, nullptr);
    for (int i = 0; i < 4; i++)
    {
        disconnect(_covoxChannelMute[i], nullptr, this, nullptr);
    }

    _signalsConnected = false;
}

void AudioSettingsWidget::refreshFromContext()
{
    disconnectSignals();

    bool hasContext = _context && _context->pSoundManager;

    _controlsContainer->setEnabled(hasContext);
    _statusLabel->setVisible(!hasContext);

    // Rebuild sources section from registry
    rebuildSourcesSection();

    if (hasContext)
    {
        SoundManager* sm = _context->pSoundManager;

        // Load AY settings from chip 0 (stereo/model shared across both chips)
        SoundChip_AY8910* ay0 = sm->getAYChip(0);
        if (ay0)
        {
            _stereoModeCombo->setCurrentIndex(static_cast<int>(ay0->getStereoMode()));
            _chipModelCombo->setCurrentIndex(static_cast<int>(ay0->getChipModel()));
        }

        // Load per-chip channel settings
        for (int chip = 0; chip < 2; chip++)
        {
            SoundChip_AY8910* ay = sm->getAYChip(chip);
            bool hasChip = (ay != nullptr);

            for (int ch = 0; ch < 3; ch++)
            {
                _channelMuteChecks[chip][ch]->setEnabled(hasChip);
                _channelVolumeSliders[chip][ch]->setEnabled(hasChip);

                if (hasChip)
                {
                    _channelMuteChecks[chip][ch]->setChecked(ay->isChannelMuted(ch));
                    int vol = static_cast<int>(ay->getChannelVolume(ch) * 100);
                    _channelVolumeSliders[chip][ch]->setValue(vol);
                    _channelVolumeLabels[chip][ch]->setText(QString("%1%").arg(vol));
                }
            }
        }

        // AY chain settings
        _ayPunchCheckbox->setChecked(sm->getAYChain().isPunchEnabled());
        _ayRoomCombo->setCurrentIndex(static_cast<int>(sm->getAYChain().getRoomMode()));

        // Beeper settings
        _beeperFilterCheckbox->setChecked(sm->isBeeperFilterEnabled());
        _beeperPunchCheckbox->setChecked(sm->getBeeperChain().isPunchEnabled());

        // FIR filter
        if (_context->pFeatureManager)
        {
            _firCheckbox->setChecked(_context->pFeatureManager->isEnabled(Features::kSoundHQ));
        }

        // SOUNDRIVE/Covox section
        bool hasCovox = sm->hasCovox();
        _covoxGroup->setVisible(hasCovox);
        if (hasCovox)
        {
            Covox* covox = sm->getCovox();
            _covoxDCRemovalCheckbox->setChecked(covox->isDCRemovalEnabled());
            for (int i = 0; i < 4; i++)
            {
                _covoxChannelMute[i]->setChecked(covox->isChannelMuted(static_cast<Covox::Channel>(i)));
            }
        }

        // Solo indicator
        updateSoloIndicator();

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
    if (_context && _context->pSoundManager)
    {
        for (const auto& dev : _context->pSoundManager->devices())
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

void AudioSettingsWidget::onSourceMuteChanged(Qt::CheckState state)
{
    auto* check = qobject_cast<QCheckBox*>(sender());
    if (!check || !_context || !_context->pSoundManager)
        return;

    auto type = static_cast<AudioSourceType>(check->property("sourceType").toInt());
    _context->pSoundManager->setDeviceMute(type, state == Qt::Checked);
}

void AudioSettingsWidget::onSourceSoloChanged(Qt::CheckState state)
{
    auto* check = qobject_cast<QCheckBox*>(sender());
    if (!check || !_context || !_context->pSoundManager)
        return;

    auto type = static_cast<AudioSourceType>(check->property("sourceType").toInt());
    _context->pSoundManager->setDeviceSolo(type, state == Qt::Checked);
    updateSoloIndicator();
}

void AudioSettingsWidget::onSourceVolumeChanged(int value)
{
    auto* slider = qobject_cast<QSlider*>(sender());
    if (!slider || !_context || !_context->pSoundManager)
        return;

    auto type = static_cast<AudioSourceType>(slider->property("sourceType").toInt());
    _context->pSoundManager->setDeviceVolume(type, value / 100.0f);

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
    if (!_context || !_context->pSoundManager)
        return;

    // Apply to all AY chips
    for (int i = 0; i < _context->pSoundManager->getAYChipCount(); i++)
    {
        SoundChip_AY8910* ay = _context->pSoundManager->getAYChip(i);
        if (ay)
            ay->setStereoMode(static_cast<AYStereoMode>(index));
    }
}

void AudioSettingsWidget::onChipModelChanged(int index)
{
    if (!_context || !_context->pSoundManager)
        return;

    // Apply to all AY chips
    for (int i = 0; i < _context->pSoundManager->getAYChipCount(); i++)
    {
        SoundChip_AY8910* ay = _context->pSoundManager->getAYChip(i);
        if (ay)
            ay->setChipModel(static_cast<AYChipModel>(index));
    }
}

void AudioSettingsWidget::onAYPunchChanged(Qt::CheckState state)
{
    if (_context && _context->pSoundManager)
    {
        _context->pSoundManager->getAYChain().setPunchEnabled(state == Qt::Checked);
        _context->pSoundManager->syncAYChainSettings();
    }
}

void AudioSettingsWidget::onAYRoomModeChanged(int index)
{
    if (_context && _context->pSoundManager)
    {
        auto mode = static_cast<AudioCharacterChain::RoomMode>(_ayRoomCombo->currentData().toInt());
        _context->pSoundManager->getAYChain().setRoomMode(mode);
        _context->pSoundManager->syncAYChainSettings();
    }
}

void AudioSettingsWidget::onFirChanged(Qt::CheckState state)
{
    if (_context && _context->pFeatureManager)
        _context->pFeatureManager->setFeature("soundhq", state == Qt::Checked);
}

// ============ Channel slots ============

void AudioSettingsWidget::onChannelMuteChanged(Qt::CheckState state)
{
    auto* check = qobject_cast<QCheckBox*>(sender());
    if (!check || !_context || !_context->pSoundManager)
        return;

    int chip = check->property("chip").toInt();
    int channel = check->property("channel").toInt();

    SoundChip_AY8910* ay = _context->pSoundManager->getAYChip(chip);
    if (ay)
        ay->setChannelMuted(channel, state == Qt::Checked);
}

void AudioSettingsWidget::onChannelVolumeChanged(int value)
{
    auto* slider = qobject_cast<QSlider*>(sender());
    if (!slider || !_context || !_context->pSoundManager)
        return;

    int chip = slider->property("chip").toInt();
    int channel = slider->property("channel").toInt();
    _channelVolumeLabels[chip][channel]->setText(QString("%1%").arg(value));

    SoundChip_AY8910* ay = _context->pSoundManager->getAYChip(chip);
    if (ay)
        ay->setChannelVolume(channel, value / 100.0);
}

// ============ Beeper slots ============

void AudioSettingsWidget::onBeeperFilterChanged(Qt::CheckState state)
{
    if (_context && _context->pSoundManager)
        _context->pSoundManager->setBeeperFilterEnabled(state == Qt::Checked);
}

void AudioSettingsWidget::onBeeperPunchChanged(Qt::CheckState state)
{
    if (_context && _context->pSoundManager)
        _context->pSoundManager->getBeeperChain().setPunchEnabled(state == Qt::Checked);
}

// ============ Covox slots ============

void AudioSettingsWidget::onCovoxDCRemovalChanged(Qt::CheckState state)
{
    if (_context && _context->pSoundManager && _context->pSoundManager->hasCovox())
        _context->pSoundManager->getCovox()->setDCRemovalEnabled(state == Qt::Checked);
}

void AudioSettingsWidget::onCovoxChannelMuteChanged(Qt::CheckState state)
{
    auto* check = qobject_cast<QCheckBox*>(sender());
    if (!check || !_context || !_context->pSoundManager || !_context->pSoundManager->hasCovox())
        return;

    int channel = check->property("channel").toInt();
    _context->pSoundManager->getCovox()->setChannelMute(static_cast<Covox::Channel>(channel), state == Qt::Checked);
}

// ============ Meter updates ============

void AudioSettingsWidget::onUpdateMeters()
{
    if (!_context || !_context->pSoundManager || !isVisible())
        return;

    const auto& devices = _context->pSoundManager->devices();

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
}
