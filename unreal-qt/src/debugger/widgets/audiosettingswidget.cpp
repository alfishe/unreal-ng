#include "audiosettingswidget.h"
#include "base/featuremanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/sound/chips/soundchip_turbosound.h"

AudioSettingsWidget::AudioSettingsWidget(EmulatorContext* context, QWidget* parent)
    : QWidget(parent), _context(context)
{
    setWindowTitle("Audio Settings");
    setMinimumWidth(250);

    auto* layout = new QVBoxLayout(this);

    // DSP group
    auto* dspGroup = new QGroupBox("DSP Processing", this);
    auto* dspLayout = new QVBoxLayout(dspGroup);

    _firCheckbox = new QCheckBox("FIR Filter (HQ mode)", this);
    _firCheckbox->setChecked(true);
    _firCheckbox->setToolTip("192-tap FIR decimation filter.\nGives bright 'ayumi' character.\nDisable for simple averaging (faster, duller).");
    connect(_firCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onFirChanged);
    dspLayout->addWidget(_firCheckbox);

    _punchCheckbox = new QCheckBox("Punch Enhancement", this);
    _punchCheckbox->setChecked(true);
    _punchCheckbox->setToolTip("Transient designer + exciter.\nAdds attack definition.\nNot needed for pure AY square waves.");
    connect(_punchCheckbox, &QCheckBox::checkStateChanged, this, &AudioSettingsWidget::onPunchChanged);
    dspLayout->addWidget(_punchCheckbox);

    layout->addWidget(dspGroup);

    // Spatial group
    auto* spatialGroup = new QGroupBox("Headphone Spatial", this);
    auto* spatialLayout = new QVBoxLayout(spatialGroup);

    auto* roomLabel = new QLabel("Room Simulation:", this);
    spatialLayout->addWidget(roomLabel);

    _roomCombo = new QComboBox(this);
    _roomCombo->addItem("Off", 0);
    _roomCombo->addItem("Room -15dB (subtle)", 1);
    _roomCombo->addItem("Room -14dB (recommended)", 2);
    _roomCombo->addItem("Room -13dB (light)", 3);
    _roomCombo->addItem("Room -12dB (moderate)", 4);
    _roomCombo->addItem("Room -9dB (strong)", 5);
    _roomCombo->setCurrentIndex(0);  // Default to Off
    _roomCombo->setToolTip("Reduces headphone fatigue from hard L-R panning.\n3ms delayed crossfeed with 10kHz lowpass.\nHigher levels may color transients on fast music.");
    connect(_roomCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioSettingsWidget::onRoomModeChanged);
    spatialLayout->addWidget(_roomCombo);

    layout->addWidget(spatialGroup);

    layout->addStretch();
}

void AudioSettingsWidget::onPunchChanged(Qt::CheckState state)
{
    if (_context && _context->pSoundManager)
    {
        _context->pSoundManager->getCharacterChain().setPunchEnabled(state == Qt::Checked);
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

void AudioSettingsWidget::onRoomModeChanged(int index)
{
    if (_context && _context->pSoundManager)
    {
        auto mode = static_cast<AudioCharacterChain::RoomMode>(index);
        _context->pSoundManager->getCharacterChain().setRoomMode(mode);
    }
}
