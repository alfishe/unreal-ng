#include "audiosettingswidget.h"
#include "base/featuremanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "emulator/sound/chips/soundchip_ay8910.h"

AudioSettingsWidget::AudioSettingsWidget(EmulatorContext* context, QWidget* parent)
    : QWidget(parent), _context(context)
{
    setWindowTitle("Audio Settings");
    setMinimumWidth(300);

    createUI();
    connectSignals();
    refreshFromContext();
}

void AudioSettingsWidget::setContext(EmulatorContext* context)
{
    _context = context;
    refreshFromContext();
}

void AudioSettingsWidget::createUI()
{
    auto* layout = new QVBoxLayout(this);

    // ============ AY/TurboSound group ============
    auto* ayGroup = new QGroupBox("AY / TurboSound", this);
    auto* ayLayout = new QVBoxLayout(ayGroup);

    // Stereo Mode
    auto* stereoLabel = new QLabel("Stereo Mode:", this);
    ayLayout->addWidget(stereoLabel);

    _stereoModeCombo = new QComboBox(this);
    _stereoModeCombo->addItem("ABC (A-Left, B-Center, C-Right)", 0);
    _stereoModeCombo->addItem("ACB (A-Left, C-Center, B-Right)", 1);
    _stereoModeCombo->addItem("Mono", 2);
    _stereoModeCombo->setToolTip("Stereo panning layout for AY channels.");
    ayLayout->addWidget(_stereoModeCombo);

    // Chip Model
    auto* chipLabel = new QLabel("Chip Model:", this);
    ayLayout->addWidget(chipLabel);

    _chipModelCombo = new QComboBox(this);
    _chipModelCombo->addItem("AY-3-8910 (Original)", 0);
    _chipModelCombo->addItem("YM2149 (Yamaha)", 1);
    _chipModelCombo->setToolTip("AY-3-8910: stepped DAC (brighter)\nYM2149: smoother DAC (warmer)");
    ayLayout->addWidget(_chipModelCombo);

    // FIR Filter
    _firCheckbox = new QCheckBox("FIR Filter (HQ mode)", this);
    _firCheckbox->setChecked(true);
    _firCheckbox->setToolTip("192-tap FIR decimation at native clock rate.\n"
                             "Gives bright 'ayumi' character.\n"
                             "Disable for simple averaging (faster, duller).");
    ayLayout->addWidget(_firCheckbox);

    // Punch
    _ayPunchCheckbox = new QCheckBox("Punch Enhancement", this);
    _ayPunchCheckbox->setChecked(true);
    _ayPunchCheckbox->setToolTip("Transient designer + exciter.\n"
                                  "Gentler settings for AY (square waves already have harmonics).");
    ayLayout->addWidget(_ayPunchCheckbox);

    // Room
    auto* ayRoomLabel = new QLabel("Room Simulation:", this);
    ayLayout->addWidget(ayRoomLabel);

    _ayRoomCombo = new QComboBox(this);
    _ayRoomCombo->addItem("Off", 0);
    _ayRoomCombo->addItem("Room -15dB (subtle)", 1);
    _ayRoomCombo->addItem("Room -14dB", 2);
    _ayRoomCombo->addItem("Room -13dB", 3);
    _ayRoomCombo->addItem("Room -12dB", 4);
    _ayRoomCombo->addItem("Room -9dB", 5);
    _ayRoomCombo->addItem("Room -6dB", 6);
    _ayRoomCombo->addItem("Room -3dB", 7);
    _ayRoomCombo->addItem("Room -2dB", 8);
    _ayRoomCombo->addItem("Room -1dB (near mono)", 9);
    _ayRoomCombo->setCurrentIndex(0);
    _ayRoomCombo->setToolTip("Crossfeed for headphone listening.\n"
                              "AY mode: 2ms delay, no lowpass (preserves harmonics).");
    ayLayout->addWidget(_ayRoomCombo);

    layout->addWidget(ayGroup);

    // ============ Channel Mixer group ============
    auto* mixerGroup = new QGroupBox("Channel Mixer", this);
    auto* mixerLayout = new QGridLayout(mixerGroup);

    // Channel A
    _channelAMuteCheckbox = new QCheckBox("Mute A", this);
    mixerLayout->addWidget(_channelAMuteCheckbox, 0, 0);
    _channelAVolumeSlider = new QSlider(Qt::Horizontal, this);
    _channelAVolumeSlider->setRange(0, 100);
    _channelAVolumeSlider->setValue(100);
    mixerLayout->addWidget(_channelAVolumeSlider, 0, 1);
    _channelAVolumeLabel = new QLabel("100%", this);
    _channelAVolumeLabel->setMinimumWidth(40);
    mixerLayout->addWidget(_channelAVolumeLabel, 0, 2);

    // Channel B
    _channelBMuteCheckbox = new QCheckBox("Mute B", this);
    mixerLayout->addWidget(_channelBMuteCheckbox, 1, 0);
    _channelBVolumeSlider = new QSlider(Qt::Horizontal, this);
    _channelBVolumeSlider->setRange(0, 100);
    _channelBVolumeSlider->setValue(100);
    mixerLayout->addWidget(_channelBVolumeSlider, 1, 1);
    _channelBVolumeLabel = new QLabel("100%", this);
    _channelBVolumeLabel->setMinimumWidth(40);
    mixerLayout->addWidget(_channelBVolumeLabel, 1, 2);

    // Channel C
    _channelCMuteCheckbox = new QCheckBox("Mute C", this);
    mixerLayout->addWidget(_channelCMuteCheckbox, 2, 0);
    _channelCVolumeSlider = new QSlider(Qt::Horizontal, this);
    _channelCVolumeSlider->setRange(0, 100);
    _channelCVolumeSlider->setValue(100);
    mixerLayout->addWidget(_channelCVolumeSlider, 2, 1);
    _channelCVolumeLabel = new QLabel("100%", this);
    _channelCVolumeLabel->setMinimumWidth(40);
    mixerLayout->addWidget(_channelCVolumeLabel, 2, 2);

    layout->addWidget(mixerGroup);

    // ============ Volume group ============
    auto* volumeGroup = new QGroupBox("Master Volume", this);
    auto* volumeLayout = new QGridLayout(volumeGroup);

    volumeLayout->addWidget(new QLabel("AY:", this), 0, 0);
    _ayVolumeSlider = new QSlider(Qt::Horizontal, this);
    _ayVolumeSlider->setRange(0, 100);
    _ayVolumeSlider->setValue(100);
    volumeLayout->addWidget(_ayVolumeSlider, 0, 1);
    _ayVolumeLabel = new QLabel("100%", this);
    _ayVolumeLabel->setMinimumWidth(40);
    volumeLayout->addWidget(_ayVolumeLabel, 0, 2);

    volumeLayout->addWidget(new QLabel("Beeper:", this), 1, 0);
    _beeperVolumeSlider = new QSlider(Qt::Horizontal, this);
    _beeperVolumeSlider->setRange(0, 100);
    _beeperVolumeSlider->setValue(100);
    volumeLayout->addWidget(_beeperVolumeSlider, 1, 1);
    _beeperVolumeLabel = new QLabel("100%", this);
    _beeperVolumeLabel->setMinimumWidth(40);
    volumeLayout->addWidget(_beeperVolumeLabel, 1, 2);

    layout->addWidget(volumeGroup);

    // ============ Beeper group ============
    auto* beeperGroup = new QGroupBox("Beeper (1-bit)", this);
    auto* beeperLayout = new QVBoxLayout(beeperGroup);

    _beeperFilterCheckbox = new QCheckBox("Lowpass Filter", this);
    _beeperFilterCheckbox->setChecked(false);
    _beeperFilterCheckbox->setToolTip("Smooths harsh 1-bit square wave edges.\n"
                                       "Gives warmer sound for digidrums and PWM synths.");
    beeperLayout->addWidget(_beeperFilterCheckbox);

    _beeperPunchCheckbox = new QCheckBox("Punch Enhancement", this);
    _beeperPunchCheckbox->setChecked(true);
    _beeperPunchCheckbox->setToolTip("Stronger punch for 1-bit audio.\n"
                                      "Adds attack definition to digidrums and PWM synths.");
    beeperLayout->addWidget(_beeperPunchCheckbox);

    layout->addWidget(beeperGroup);

    layout->addStretch();
}

void AudioSettingsWidget::connectSignals()
{
    if (_signalsConnected)
        return;

    connect(_stereoModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioSettingsWidget::onStereoModeChanged);
    connect(_chipModelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioSettingsWidget::onChipModelChanged);
    connect(_firCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onFirChanged);
    connect(_ayPunchCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onAYPunchChanged);
    connect(_ayRoomCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioSettingsWidget::onAYRoomModeChanged);

    connect(_channelAMuteCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onChannelAMuteChanged);
    connect(_channelBMuteCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onChannelBMuteChanged);
    connect(_channelCMuteCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onChannelCMuteChanged);
    connect(_channelAVolumeSlider, &QSlider::valueChanged, this, &AudioSettingsWidget::onChannelAVolumeChanged);
    connect(_channelBVolumeSlider, &QSlider::valueChanged, this, &AudioSettingsWidget::onChannelBVolumeChanged);
    connect(_channelCVolumeSlider, &QSlider::valueChanged, this, &AudioSettingsWidget::onChannelCVolumeChanged);

    connect(_ayVolumeSlider, &QSlider::valueChanged, this, &AudioSettingsWidget::onAYVolumeChanged);
    connect(_beeperVolumeSlider, &QSlider::valueChanged, this, &AudioSettingsWidget::onBeeperVolumeChanged);

    connect(_beeperFilterCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onBeeperFilterChanged);
    connect(_beeperPunchCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onBeeperPunchChanged);

    _signalsConnected = true;
}

void AudioSettingsWidget::disconnectSignals()
{
    if (!_signalsConnected)
        return;

    disconnect(_stereoModeCombo, nullptr, this, nullptr);
    disconnect(_chipModelCombo, nullptr, this, nullptr);
    disconnect(_firCheckbox, nullptr, this, nullptr);
    disconnect(_ayPunchCheckbox, nullptr, this, nullptr);
    disconnect(_ayRoomCombo, nullptr, this, nullptr);
    disconnect(_channelAMuteCheckbox, nullptr, this, nullptr);
    disconnect(_channelBMuteCheckbox, nullptr, this, nullptr);
    disconnect(_channelCMuteCheckbox, nullptr, this, nullptr);
    disconnect(_channelAVolumeSlider, nullptr, this, nullptr);
    disconnect(_channelBVolumeSlider, nullptr, this, nullptr);
    disconnect(_channelCVolumeSlider, nullptr, this, nullptr);
    disconnect(_ayVolumeSlider, nullptr, this, nullptr);
    disconnect(_beeperVolumeSlider, nullptr, this, nullptr);
    disconnect(_beeperFilterCheckbox, nullptr, this, nullptr);
    disconnect(_beeperPunchCheckbox, nullptr, this, nullptr);

    _signalsConnected = false;
}

void AudioSettingsWidget::refreshFromContext()
{
    disconnectSignals();

    bool hasContext = _context && _context->pSoundManager;
    setEnabled(hasContext);

    if (hasContext)
    {
        SoundChip_AY8910* ay = _context->pSoundManager->getAYChip(0);
        if (ay)
        {
            _stereoModeCombo->setCurrentIndex(static_cast<int>(ay->getStereoMode()));
            _chipModelCombo->setCurrentIndex(static_cast<int>(ay->getChipModel()));

            _channelAMuteCheckbox->setChecked(ay->isChannelMuted(AY_CHANNEL_A));
            _channelBMuteCheckbox->setChecked(ay->isChannelMuted(AY_CHANNEL_B));
            _channelCMuteCheckbox->setChecked(ay->isChannelMuted(AY_CHANNEL_C));

            _channelAVolumeSlider->setValue(static_cast<int>(ay->getChannelVolume(AY_CHANNEL_A) * 100));
            _channelBVolumeSlider->setValue(static_cast<int>(ay->getChannelVolume(AY_CHANNEL_B) * 100));
            _channelCVolumeSlider->setValue(static_cast<int>(ay->getChannelVolume(AY_CHANNEL_C) * 100));

            updateChannelVolumeLabel(AY_CHANNEL_A, _channelAVolumeSlider->value());
            updateChannelVolumeLabel(AY_CHANNEL_B, _channelBVolumeSlider->value());
            updateChannelVolumeLabel(AY_CHANNEL_C, _channelCVolumeSlider->value());
        }

        if (_context->pFeatureManager)
        {
            _firCheckbox->setChecked(_context->pFeatureManager->isEnabled(Features::kSoundHQ));
        }
    }

    connectSignals();
}

void AudioSettingsWidget::updateChannelVolumeLabel(int channel, int value)
{
    QString text = QString("%1%").arg(value);
    switch (channel)
    {
        case AY_CHANNEL_A:
            _channelAVolumeLabel->setText(text);
            break;
        case AY_CHANNEL_B:
            _channelBVolumeLabel->setText(text);
            break;
        case AY_CHANNEL_C:
            _channelCVolumeLabel->setText(text);
            break;
    }
}

// ============ AY slots ============

void AudioSettingsWidget::onStereoModeChanged(int index)
{
    if (_context && _context->pSoundManager)
    {
        SoundChip_AY8910* ay = _context->pSoundManager->getAYChip(0);
        if (ay)
        {
            ay->setStereoMode(static_cast<AYStereoMode>(index));
        }
        // Also apply to second chip if TurboSound
        SoundChip_AY8910* ay2 = _context->pSoundManager->getAYChip(1);
        if (ay2)
        {
            ay2->setStereoMode(static_cast<AYStereoMode>(index));
        }
    }
}

void AudioSettingsWidget::onChipModelChanged(int index)
{
    if (_context && _context->pSoundManager)
    {
        SoundChip_AY8910* ay = _context->pSoundManager->getAYChip(0);
        if (ay)
        {
            ay->setChipModel(static_cast<AYChipModel>(index));
        }
        SoundChip_AY8910* ay2 = _context->pSoundManager->getAYChip(1);
        if (ay2)
        {
            ay2->setChipModel(static_cast<AYChipModel>(index));
        }
    }
}

void AudioSettingsWidget::onAYPunchChanged(Qt::CheckState state)
{
    if (_context && _context->pSoundManager)
    {
        _context->pSoundManager->getAYChain().setPunchEnabled(state == Qt::Checked);
    }
}

void AudioSettingsWidget::onAYRoomModeChanged(int index)
{
    if (_context && _context->pSoundManager)
    {
        auto mode = static_cast<AudioCharacterChain::RoomMode>(index);
        _context->pSoundManager->getAYChain().setRoomMode(mode);
    }
}

void AudioSettingsWidget::onFirChanged(Qt::CheckState state)
{
    if (_context && _context->pSoundManager && _context->pSoundManager->hasTurboSound())
    {
        if (_context->pFeatureManager)
        {
            _context->pFeatureManager->setFeature("soundhq", state == Qt::Checked);
        }
    }
}

// ============ Channel slots ============

void AudioSettingsWidget::onChannelAMuteChanged(Qt::CheckState state)
{
    if (_context && _context->pSoundManager)
    {
        SoundChip_AY8910* ay = _context->pSoundManager->getAYChip(0);
        if (ay) ay->setChannelMuted(AY_CHANNEL_A, state == Qt::Checked);
        SoundChip_AY8910* ay2 = _context->pSoundManager->getAYChip(1);
        if (ay2) ay2->setChannelMuted(AY_CHANNEL_A, state == Qt::Checked);
    }
}

void AudioSettingsWidget::onChannelBMuteChanged(Qt::CheckState state)
{
    if (_context && _context->pSoundManager)
    {
        SoundChip_AY8910* ay = _context->pSoundManager->getAYChip(0);
        if (ay) ay->setChannelMuted(AY_CHANNEL_B, state == Qt::Checked);
        SoundChip_AY8910* ay2 = _context->pSoundManager->getAYChip(1);
        if (ay2) ay2->setChannelMuted(AY_CHANNEL_B, state == Qt::Checked);
    }
}

void AudioSettingsWidget::onChannelCMuteChanged(Qt::CheckState state)
{
    if (_context && _context->pSoundManager)
    {
        SoundChip_AY8910* ay = _context->pSoundManager->getAYChip(0);
        if (ay) ay->setChannelMuted(AY_CHANNEL_C, state == Qt::Checked);
        SoundChip_AY8910* ay2 = _context->pSoundManager->getAYChip(1);
        if (ay2) ay2->setChannelMuted(AY_CHANNEL_C, state == Qt::Checked);
    }
}

void AudioSettingsWidget::onChannelAVolumeChanged(int value)
{
    updateChannelVolumeLabel(AY_CHANNEL_A, value);
    if (_context && _context->pSoundManager)
    {
        double volume = value / 100.0;
        SoundChip_AY8910* ay = _context->pSoundManager->getAYChip(0);
        if (ay) ay->setChannelVolume(AY_CHANNEL_A, volume);
        SoundChip_AY8910* ay2 = _context->pSoundManager->getAYChip(1);
        if (ay2) ay2->setChannelVolume(AY_CHANNEL_A, volume);
    }
}

void AudioSettingsWidget::onChannelBVolumeChanged(int value)
{
    updateChannelVolumeLabel(AY_CHANNEL_B, value);
    if (_context && _context->pSoundManager)
    {
        double volume = value / 100.0;
        SoundChip_AY8910* ay = _context->pSoundManager->getAYChip(0);
        if (ay) ay->setChannelVolume(AY_CHANNEL_B, volume);
        SoundChip_AY8910* ay2 = _context->pSoundManager->getAYChip(1);
        if (ay2) ay2->setChannelVolume(AY_CHANNEL_B, volume);
    }
}

void AudioSettingsWidget::onChannelCVolumeChanged(int value)
{
    updateChannelVolumeLabel(AY_CHANNEL_C, value);
    if (_context && _context->pSoundManager)
    {
        double volume = value / 100.0;
        SoundChip_AY8910* ay = _context->pSoundManager->getAYChip(0);
        if (ay) ay->setChannelVolume(AY_CHANNEL_C, volume);
        SoundChip_AY8910* ay2 = _context->pSoundManager->getAYChip(1);
        if (ay2) ay2->setChannelVolume(AY_CHANNEL_C, volume);
    }
}

// ============ Volume slots ============

void AudioSettingsWidget::onAYVolumeChanged(int value)
{
    _ayVolumeLabel->setText(QString("%1%").arg(value));
    if (_context && _context->pSoundManager)
    {
        _context->pSoundManager->setAYVolume(value / 100.0);
    }
}

void AudioSettingsWidget::onBeeperVolumeChanged(int value)
{
    _beeperVolumeLabel->setText(QString("%1%").arg(value));
    if (_context && _context->pSoundManager)
    {
        _context->pSoundManager->setBeeperVolume(value / 100.0);
    }
}

// ============ Beeper slots ============

void AudioSettingsWidget::onBeeperFilterChanged(Qt::CheckState state)
{
    if (_context && _context->pSoundManager)
    {
        _context->pSoundManager->setBeeperFilterEnabled(state == Qt::Checked);
    }
}

void AudioSettingsWidget::onBeeperPunchChanged(Qt::CheckState state)
{
    if (_context && _context->pSoundManager)
    {
        _context->pSoundManager->getBeeperChain().setPunchEnabled(state == Qt::Checked);
    }
}
