#include "temporaleffectsdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QPushButton>

#include "widgets/devicescreenwrapper.h"

TemporalEffectsDialog::TemporalEffectsDialog(DeviceScreenWrapper* screenWrapper, QWidget* parent)
    : QDialog(parent)
    , _screenWrapper(screenWrapper)
{
    setWindowTitle(tr("Temporal Effects Settings"));
    setMinimumWidth(350);

    auto* mainLayout = new QVBoxLayout(this);

    // Enable checkbox
    _enabledCheck = new QCheckBox(tr("Enable Temporal Blending"));
    _enabledCheck->setToolTip(tr("Blend multiple frames to reduce flickering (gigascreen smoothing)"));
    mainLayout->addWidget(_enabledCheck);

    // Settings group
    auto* settingsGroup = new QGroupBox(tr("Blending Settings"));
    auto* settingsLayout = new QVBoxLayout(settingsGroup);

    // History depth
    auto* depthLayout = new QHBoxLayout();
    depthLayout->addWidget(new QLabel(tr("Frame History:")));
    _historySizeSlider = new QSlider(Qt::Horizontal);
    _historySizeSlider->setRange(2, 5);
    _historySizeSlider->setTickPosition(QSlider::TicksBelow);
    _historySizeSlider->setTickInterval(1);
    _historySizeSlider->setToolTip(tr("Number of frames to blend (2-5). More frames = smoother but more ghosting."));
    depthLayout->addWidget(_historySizeSlider);
    _historySizeLabel = new QLabel("2");
    _historySizeLabel->setMinimumWidth(20);
    depthLayout->addWidget(_historySizeLabel);
    settingsLayout->addLayout(depthLayout);

    // Weight mode
    auto* modeLayout = new QHBoxLayout();
    modeLayout->addWidget(new QLabel(tr("Blend Mode:")));
    _weightModeCombo = new QComboBox();
    _weightModeCombo->addItem(tr("Equal (all frames same weight)"), 0);
    _weightModeCombo->addItem(tr("Exponential (newer frames stronger)"), 1);
    _weightModeCombo->setToolTip(tr("How frame weights are distributed. Exponential gives less ghosting."));
    modeLayout->addWidget(_weightModeCombo);
    settingsLayout->addLayout(modeLayout);

    // Decay factor (for exponential mode)
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

    mainLayout->addWidget(settingsGroup);

    // Info label
    auto* infoLabel = new QLabel(tr(
        "<b>Temporal blending</b> smooths flickering in gigascreen and interlaced effects "
        "by averaging multiple frames. This creates a subtle phosphor persistence / afterglow effect.\n\n"
        "<b>Tip:</b> Start with 2 frames and exponential mode for minimal ghosting."));
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: gray; font-size: 11px;");
    mainLayout->addWidget(infoLabel);

    mainLayout->addStretch();

    // Buttons
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    // Connect signals
    connect(_enabledCheck, &QCheckBox::toggled, this, &TemporalEffectsDialog::onEnabledChanged);
    connect(_historySizeSlider, &QSlider::valueChanged, this, &TemporalEffectsDialog::onHistorySizeChanged);
    connect(_weightModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TemporalEffectsDialog::onWeightModeChanged);
    connect(_decaySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TemporalEffectsDialog::onDecayChanged);

    // Initialize from current screen state
    updateFromScreen();

    // Initial UI state
    onWeightModeChanged(_weightModeCombo->currentIndex());
}

void TemporalEffectsDialog::updateFromScreen()
{
    if (!_screenWrapper)
        return;

    bool enabled = _screenWrapper->temporalBlendingEnabled();
    int historySize = _screenWrapper->temporalHistorySize();
    int weightMode = _screenWrapper->temporalWeightMode();

    _enabledCheck->setChecked(enabled);
    _historySizeSlider->setValue(historySize);
    _historySizeLabel->setText(QString::number(historySize));
    _weightModeCombo->setCurrentIndex(weightMode);

    // Enable/disable settings based on enabled state
    _historySizeSlider->setEnabled(enabled);
    _weightModeCombo->setEnabled(enabled);
    _decaySpin->setEnabled(enabled && weightMode == 1);
}

void TemporalEffectsDialog::onEnabledChanged(bool enabled)
{
    _historySizeSlider->setEnabled(enabled);
    _weightModeCombo->setEnabled(enabled);
    _decaySpin->setEnabled(enabled && _weightModeCombo->currentIndex() == 1);

    if (_screenWrapper)
    {
        _screenWrapper->setTemporalBlendingEnabled(enabled);
    }
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
