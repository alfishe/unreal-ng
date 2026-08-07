#include "AudioSettingsDialog.h"
#include <QDialogButtonBox>
#include <QPushButton>

#ifdef HAS_EMULATOR_CORE
#include "base/featuremanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#endif

AudioSettingsDialog::AudioSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Audio Settings");
    setMinimumWidth(400);

    _meterTimer = new QTimer(this);
#ifdef HAS_EMULATOR_CORE
    connect(_meterTimer, &QTimer::timeout, this, &AudioSettingsDialog::onUpdateMeters);
#endif

    createUI();

#ifdef HAS_EMULATOR_CORE
    connectSignals();
    refreshFromContext();
#endif
}

AudioSettingsDialog::~AudioSettingsDialog()
{
    if (_meterTimer)
        _meterTimer->stop();
}

void AudioSettingsDialog::createUI()
{
    auto* outerLayout = new QVBoxLayout(this);

    _statusLabel = new QLabel(
        "No active emulator instance.\n"
        "Start an emulator to adjust audio settings.", this);
    _statusLabel->setWordWrap(true);
    _statusLabel->setStyleSheet(
        "padding: 8px; background-color: #fff3cd; color: #856404; "
        "border: 1px solid #ffeeba; border-radius: 4px; font-weight: bold;");
    _statusLabel->setVisible(true);
    outerLayout->addWidget(_statusLabel);

    _controlsContainer = new QWidget(this);
    _controlsContainer->setEnabled(false);
    outerLayout->addWidget(_controlsContainer);

    auto* layout = new QVBoxLayout(_controlsContainer);
    layout->setContentsMargins(0, 0, 0, 0);

    // Sources section
    _sourcesGroup = new QGroupBox("Sources", this);
    _sourcesContainer = new QWidget(_sourcesGroup);
    auto* sourcesGroupLayout = new QVBoxLayout(_sourcesGroup);
    sourcesGroupLayout->addWidget(_sourcesContainer);
    _soloIndicator = new QLabel("", _sourcesGroup);
    _soloIndicator->setStyleSheet("color: orange; font-weight: bold;");
    _soloIndicator->setVisible(false);
    sourcesGroupLayout->addWidget(_soloIndicator);
    layout->addWidget(_sourcesGroup);

    // AY/TurboSound section
    _ayGroup = new QGroupBox("AY / TurboSound", this);
    auto* ayLayout = new QVBoxLayout(_ayGroup);

    auto* stereoRow = new QHBoxLayout();
    stereoRow->addWidget(new QLabel("Stereo:", _ayGroup));
    _stereoModeCombo = new QComboBox(_ayGroup);
    _stereoModeCombo->addItem("ABC", 0);
    _stereoModeCombo->addItem("ACB", 1);
    _stereoModeCombo->addItem("Mono", 2);
    stereoRow->addWidget(_stereoModeCombo);

    stereoRow->addWidget(new QLabel("Model:", _ayGroup));
    _chipModelCombo = new QComboBox(_ayGroup);
    _chipModelCombo->addItem("AY-3-8910", 0);
    _chipModelCombo->addItem("YM2149", 1);
    stereoRow->addWidget(_chipModelCombo);
    ayLayout->addLayout(stereoRow);

    auto* dspRow = new QHBoxLayout();
    _firCheckbox = new QCheckBox("FIR Filter", _ayGroup);
    dspRow->addWidget(_firCheckbox);
    _ayPunchCheckbox = new QCheckBox("Punch", _ayGroup);
    dspRow->addWidget(_ayPunchCheckbox);
    dspRow->addWidget(new QLabel("Room:", _ayGroup));
    _ayRoomCombo = new QComboBox(_ayGroup);
    _ayRoomCombo->addItem("Off", 0);
    _ayRoomCombo->addItem("-15dB", 1);
    _ayRoomCombo->addItem("-12dB", 4);
    _ayRoomCombo->addItem("-9dB", 5);
    _ayRoomCombo->addItem("-6dB", 6);
    _ayRoomCombo->addItem("-3dB", 7);
    dspRow->addWidget(_ayRoomCombo);
    ayLayout->addLayout(dspRow);

    // Channel Mixer
    _channelMixerGroup = new QGroupBox("Channels", _ayGroup);
    auto* chLayout = new QGridLayout(_channelMixerGroup);
    const char* chNames[] = {"A", "B", "C"};
    int row = 0;

    chLayout->addWidget(new QLabel("AY1", _channelMixerGroup), row++, 0, 1, 3);
    for (int ch = 0; ch < 3; ch++)
    {
        _channelMuteChecks[0][ch] = new QCheckBox(QString("Mute %1").arg(chNames[ch]), _channelMixerGroup);
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

    chLayout->addWidget(new QLabel("AY2", _channelMixerGroup), row++, 0, 1, 3);
    for (int ch = 0; ch < 3; ch++)
    {
        _channelMuteChecks[1][ch] = new QCheckBox(QString("Mute %1").arg(chNames[ch]), _channelMixerGroup);
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

    // Beeper section
    _beeperGroup = new QGroupBox("Beeper (1-bit)", this);
    auto* beeperLayout = new QHBoxLayout(_beeperGroup);
    _beeperFilterCheckbox = new QCheckBox("Lowpass", _beeperGroup);
    beeperLayout->addWidget(_beeperFilterCheckbox);
    _beeperPunchCheckbox = new QCheckBox("Punch", _beeperGroup);
    beeperLayout->addWidget(_beeperPunchCheckbox);
    beeperLayout->addStretch();
    layout->addWidget(_beeperGroup);

    // SOUNDRIVE/COVOX section
    _covoxGroup = new QGroupBox("SOUNDRIVE / COVOX", this);
    auto* covoxLayout = new QVBoxLayout(_covoxGroup);

    auto* channelsRow = new QHBoxLayout();
    const char* covoxChNames[] = {"Left A", "Left B", "Right A", "Right B"};
    for (int i = 0; i < 4; i++)
    {
        _covoxChannelMute[i] = new QCheckBox(QString("Mute %1").arg(covoxChNames[i]), _covoxGroup);
        _covoxChannelMute[i]->setProperty("channel", i);
        channelsRow->addWidget(_covoxChannelMute[i]);
    }
    covoxLayout->addLayout(channelsRow);

    auto* dcRow = new QHBoxLayout();
    _covoxDCRemovalCheckbox = new QCheckBox("DC Offset Removal", _covoxGroup);
    dcRow->addWidget(_covoxDCRemovalCheckbox);
    dcRow->addStretch();
    covoxLayout->addLayout(dcRow);

    _covoxGroup->setVisible(false);
    layout->addWidget(_covoxGroup);

    layout->addStretch();

    // Button box
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::accept);
    outerLayout->addWidget(buttonBox);
}

#ifdef HAS_EMULATOR_CORE

void AudioSettingsDialog::setContext(EmulatorContext* context)
{
    _context = context;
    refreshFromContext();
}

void AudioSettingsDialog::rebuildSourcesSection()
{
    _sourceRows.clear();

    // Delete all children of _sourcesContainer (including header labels and row widgets)
    QLayoutItem* item;
    if (auto* oldLayout = _sourcesContainer->layout()) {
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (item->widget())
                delete item->widget();
            delete item;
        }
        delete oldLayout;
    }

    auto* grid = new QGridLayout(_sourcesContainer);
    grid->setContentsMargins(0, 0, 0, 0);

    grid->addWidget(new QLabel("", _sourcesContainer), 0, 0);
    grid->addWidget(new QLabel("M", _sourcesContainer), 0, 1);
    grid->addWidget(new QLabel("S", _sourcesContainer), 0, 2);
    grid->addWidget(new QLabel("Volume", _sourcesContainer), 0, 3);
    grid->addWidget(new QLabel("", _sourcesContainer), 0, 4);
    grid->addWidget(new QLabel("Level", _sourcesContainer), 0, 5);

    if (!_context || !_context->pSoundManager)
        return;

    const auto& devices = _context->pSoundManager->devices();
    int row = 1;

    for (const auto& dev : devices)
    {
        SourceRow sr;
        sr.type = dev.type;

        sr.activityDot = new QLabel("●", _sourcesContainer);
        sr.activityDot->setStyleSheet("color: gray;");

        sr.nameLabel = new QLabel(QString::fromStdString(dev.name), _sourcesContainer);
        auto* nameRow = new QHBoxLayout();
        nameRow->setContentsMargins(0, 0, 0, 0);
        nameRow->addWidget(sr.activityDot);
        nameRow->addWidget(sr.nameLabel);
        nameRow->addStretch();
        auto* nameWidget = new QWidget(_sourcesContainer);
        nameWidget->setLayout(nameRow);
        grid->addWidget(nameWidget, row, 0);

        sr.muteCheck = new QCheckBox(_sourcesContainer);
        sr.muteCheck->setProperty("sourceType", static_cast<int>(dev.type));
        sr.muteCheck->setChecked(dev.mute);
        grid->addWidget(sr.muteCheck, row, 1);

        sr.soloCheck = new QCheckBox(_sourcesContainer);
        sr.soloCheck->setProperty("sourceType", static_cast<int>(dev.type));
        sr.soloCheck->setChecked(dev.solo);
        grid->addWidget(sr.soloCheck, row, 2);

        sr.volumeSlider = new QSlider(Qt::Horizontal, _sourcesContainer);
        sr.volumeSlider->setRange(0, 100);
        sr.volumeSlider->setValue(static_cast<int>(dev.volume * 100));
        sr.volumeSlider->setProperty("sourceType", static_cast<int>(dev.type));
        grid->addWidget(sr.volumeSlider, row, 3);

        sr.volumeLabel = new QLabel(QString("%1%").arg(static_cast<int>(dev.volume * 100)), _sourcesContainer);
        sr.volumeLabel->setMinimumWidth(40);
        grid->addWidget(sr.volumeLabel, row, 4);

        sr.meter = new QProgressBar(_sourcesContainer);
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

void AudioSettingsDialog::connectSignals()
{
    if (_signalsConnected)
        return;

    for (auto& row : _sourceRows)
    {
        connect(row.muteCheck, &QCheckBox::checkStateChanged, this, &AudioSettingsDialog::onSourceMuteChanged);
        connect(row.soloCheck, &QCheckBox::checkStateChanged, this, &AudioSettingsDialog::onSourceSoloChanged);
        connect(row.volumeSlider, &QSlider::valueChanged, this, &AudioSettingsDialog::onSourceVolumeChanged);
    }

    connect(_stereoModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioSettingsDialog::onStereoModeChanged);
    connect(_chipModelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioSettingsDialog::onChipModelChanged);
    connect(_firCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsDialog::onFirChanged);
    connect(_ayPunchCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsDialog::onAYPunchChanged);
    connect(_ayRoomCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioSettingsDialog::onAYRoomModeChanged);

    for (int chip = 0; chip < 2; chip++)
    {
        for (int ch = 0; ch < 3; ch++)
        {
            _channelMuteChecks[chip][ch]->setProperty("chip", chip);
            _channelMuteChecks[chip][ch]->setProperty("channel", ch);
            _channelVolumeSliders[chip][ch]->setProperty("chip", chip);
            _channelVolumeSliders[chip][ch]->setProperty("channel", ch);
            connect(_channelMuteChecks[chip][ch], &QCheckBox::checkStateChanged, this, &AudioSettingsDialog::onChannelMuteChanged);
            connect(_channelVolumeSliders[chip][ch], &QSlider::valueChanged, this, &AudioSettingsDialog::onChannelVolumeChanged);
        }
    }

    connect(_beeperFilterCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsDialog::onBeeperFilterChanged);
    connect(_beeperPunchCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsDialog::onBeeperPunchChanged);

    connect(_covoxDCRemovalCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsDialog::onCovoxDCRemovalChanged);
    for (int i = 0; i < 4; i++)
    {
        connect(_covoxChannelMute[i], &QCheckBox::checkStateChanged, this, &AudioSettingsDialog::onCovoxChannelMuteChanged);
    }

    _signalsConnected = true;
}

void AudioSettingsDialog::disconnectSignals()
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

void AudioSettingsDialog::refreshFromContext()
{
    disconnectSignals();

    bool hasContext = _context && _context->pSoundManager;

    _controlsContainer->setEnabled(hasContext);
    _statusLabel->setVisible(!hasContext);

    rebuildSourcesSection();

    if (hasContext)
    {
        SoundManager* sm = _context->pSoundManager;

        SoundChip_AY8910* ay0 = sm->getAYChip(0);
        if (ay0)
        {
            _stereoModeCombo->setCurrentIndex(static_cast<int>(ay0->getStereoMode()));
            _chipModelCombo->setCurrentIndex(static_cast<int>(ay0->getChipModel()));
        }

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

        _ayPunchCheckbox->setChecked(sm->getAYChain().isPunchEnabled());
        _ayRoomCombo->setCurrentIndex(static_cast<int>(sm->getAYChain().getRoomMode()));

        _beeperFilterCheckbox->setChecked(sm->isBeeperFilterEnabled());
        _beeperPunchCheckbox->setChecked(sm->getBeeperChain().isPunchEnabled());

        // FIR filter
        if (_context->pFeatureManager)
        {
            _firCheckbox->setEnabled(true);
            _firCheckbox->setChecked(_context->pFeatureManager->isEnabled(Features::kSoundHQ));
        }
        else
        {
            _firCheckbox->setEnabled(false);
            _firCheckbox->setChecked(false);
        }

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

        updateSoloIndicator();
        _meterTimer->start(100);
    }
    else
    {
        _meterTimer->stop();
    }

    connectSignals();
}

void AudioSettingsDialog::updateSoloIndicator()
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

void AudioSettingsDialog::onSourceMuteChanged(Qt::CheckState state)
{
    auto* check = qobject_cast<QCheckBox*>(sender());
    if (!check || !_context || !_context->pSoundManager)
        return;

    auto type = static_cast<AudioSourceType>(check->property("sourceType").toInt());
    _context->pSoundManager->setDeviceMute(type, state == Qt::Checked);
}

void AudioSettingsDialog::onSourceSoloChanged(Qt::CheckState state)
{
    auto* check = qobject_cast<QCheckBox*>(sender());
    if (!check || !_context || !_context->pSoundManager)
        return;

    auto type = static_cast<AudioSourceType>(check->property("sourceType").toInt());
    _context->pSoundManager->setDeviceSolo(type, state == Qt::Checked);
    updateSoloIndicator();
}

void AudioSettingsDialog::onSourceVolumeChanged(int value)
{
    auto* slider = qobject_cast<QSlider*>(sender());
    if (!slider || !_context || !_context->pSoundManager)
        return;

    auto type = static_cast<AudioSourceType>(slider->property("sourceType").toInt());
    _context->pSoundManager->setDeviceVolume(type, value / 100.0f);

    for (auto& row : _sourceRows)
    {
        if (row.type == type)
        {
            row.volumeLabel->setText(QString("%1%").arg(value));
            break;
        }
    }
}

void AudioSettingsDialog::onStereoModeChanged(int index)
{
    if (!_context || !_context->pSoundManager)
        return;

    for (int i = 0; i < _context->pSoundManager->getAYChipCount(); i++)
    {
        SoundChip_AY8910* ay = _context->pSoundManager->getAYChip(i);
        if (ay)
            ay->setStereoMode(static_cast<AYStereoMode>(index));
    }
}

void AudioSettingsDialog::onChipModelChanged(int index)
{
    if (!_context || !_context->pSoundManager)
        return;

    for (int i = 0; i < _context->pSoundManager->getAYChipCount(); i++)
    {
        SoundChip_AY8910* ay = _context->pSoundManager->getAYChip(i);
        if (ay)
            ay->setChipModel(static_cast<AYChipModel>(index));
    }
}

void AudioSettingsDialog::onAYPunchChanged(Qt::CheckState state)
{
    if (_context && _context->pSoundManager)
    {
        _context->pSoundManager->getAYChain().setPunchEnabled(state == Qt::Checked);
        _context->pSoundManager->syncAYChainSettings();
    }
}

void AudioSettingsDialog::onAYRoomModeChanged(int index)
{
    if (_context && _context->pSoundManager)
    {
        auto mode = static_cast<AudioCharacterChain::RoomMode>(_ayRoomCombo->currentData().toInt());
        _context->pSoundManager->getAYChain().setRoomMode(mode);
        _context->pSoundManager->syncAYChainSettings();
    }
}

void AudioSettingsDialog::onFirChanged(Qt::CheckState state)
{
    if (_context && _context->pFeatureManager)
        _context->pFeatureManager->setFeature(Features::kSoundHQ, state == Qt::Checked);
}

void AudioSettingsDialog::onChannelMuteChanged(Qt::CheckState state)
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

void AudioSettingsDialog::onChannelVolumeChanged(int value)
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

void AudioSettingsDialog::onBeeperFilterChanged(Qt::CheckState state)
{
    if (_context && _context->pSoundManager)
        _context->pSoundManager->setBeeperFilterEnabled(state == Qt::Checked);
}

void AudioSettingsDialog::onBeeperPunchChanged(Qt::CheckState state)
{
    if (_context && _context->pSoundManager)
        _context->pSoundManager->getBeeperChain().setPunchEnabled(state == Qt::Checked);
}

void AudioSettingsDialog::onCovoxDCRemovalChanged(Qt::CheckState state)
{
    if (_context && _context->pSoundManager && _context->pSoundManager->hasCovox())
        _context->pSoundManager->getCovox()->setDCRemovalEnabled(state == Qt::Checked);
}

void AudioSettingsDialog::onCovoxChannelMuteChanged(Qt::CheckState state)
{
    auto* check = qobject_cast<QCheckBox*>(sender());
    if (!check || !_context || !_context->pSoundManager || !_context->pSoundManager->hasCovox())
        return;

    int channel = check->property("channel").toInt();
    _context->pSoundManager->getCovox()->setChannelMute(static_cast<Covox::Channel>(channel), state == Qt::Checked);
}

void AudioSettingsDialog::onUpdateMeters()
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
                row.activityDot->setStyleSheet(dev.activeRecently ? "color: lime;" : "color: gray;");
                int peakPct = static_cast<int>(dev.peak * 100);
                row.meter->setValue(peakPct);
                break;
            }
        }
    }
}

#endif // HAS_EMULATOR_CORE
