#include "audiosettingswidget.h"
#include "base/featuremanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/sound/chips/soundchip_turbosound.h"

AudioSettingsWidget::AudioSettingsWidget(EmulatorContext* context, QWidget* parent)
    : QWidget(parent), _context(context)
{
    setWindowTitle("Audio Settings");
    setMinimumWidth(280);

    auto* layout = new QVBoxLayout(this);

    // ============ AY/TurboSound group ============
    auto* ayGroup = new QGroupBox("AY / TurboSound", this);
    auto* ayLayout = new QVBoxLayout(ayGroup);

    _firCheckbox = new QCheckBox("FIR Filter (HQ mode)", this);
    _firCheckbox->setChecked(true);
    _firCheckbox->setToolTip("96-tap FIR decimation at native clock rate.\n"
                             "Gives bright 'ayumi' character.\n"
                             "Disable for simple averaging (faster, duller).");
    connect(_firCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onFirChanged);
    ayLayout->addWidget(_firCheckbox);

    _ayPunchCheckbox = new QCheckBox("Punch Enhancement", this);
    _ayPunchCheckbox->setChecked(true);
    _ayPunchCheckbox->setToolTip("Transient designer + exciter.\n"
                                  "Gentler settings for AY (square waves already have harmonics).");
    connect(_ayPunchCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onAYPunchChanged);
    ayLayout->addWidget(_ayPunchCheckbox);

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
    connect(_ayRoomCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioSettingsWidget::onAYRoomModeChanged);
    ayLayout->addWidget(_ayRoomCombo);

    layout->addWidget(ayGroup);

    // ============ Beeper group ============
    auto* beeperGroup = new QGroupBox("Beeper (1-bit)", this);
    auto* beeperLayout = new QVBoxLayout(beeperGroup);

    _beeperFilterCheckbox = new QCheckBox("Lowpass Filter", this);
    _beeperFilterCheckbox->setChecked(false);
    _beeperFilterCheckbox->setToolTip("Smooths harsh 1-bit square wave edges.\n"
                                       "Gives warmer sound for digidrums and PWM synths.");
    connect(_beeperFilterCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onBeeperFilterChanged);
    beeperLayout->addWidget(_beeperFilterCheckbox);

    _beeperPunchCheckbox = new QCheckBox("Punch Enhancement", this);
    _beeperPunchCheckbox->setChecked(true);
    _beeperPunchCheckbox->setToolTip("Stronger punch for 1-bit audio.\n"
                                      "Adds attack definition to digidrums and PWM synths.");
    connect(_beeperPunchCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onBeeperPunchChanged);
    beeperLayout->addWidget(_beeperPunchCheckbox);

    layout->addWidget(beeperGroup);

    layout->addStretch();
}

// ============ AY slots ============

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

