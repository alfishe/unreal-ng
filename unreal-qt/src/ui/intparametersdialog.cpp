#include "intparametersdialog.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include <QMessageBox>

IntParametersDialog::IntParametersDialog(EmulatorBinding* binding, QWidget* parent)
    : QDialog(parent), _binding(binding)
{
    setupUI();

    if (_binding)
    {
        connect(_binding, &EmulatorBinding::unbound, this, &IntParametersDialog::onBindingUnbound);
        loadValues();
    }
    else
    {
        setControlsEnabled(false);
        _statusLabel->setText(tr("No emulator binding available"));
        _statusLabel->setVisible(true);
    }
}

IntParametersDialog::~IntParametersDialog()
{
}

void IntParametersDialog::setupUI()
{
    setWindowTitle(tr("INT Parameters"));
    setModal(false);
    resize(400, 280);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Create group box for parameters
    QGroupBox* paramsGroup = new QGroupBox(tr("Interrupt Timing Parameters"), this);
    QVBoxLayout* paramsLayout = new QVBoxLayout(paramsGroup);

    // intpos parameter (maps to config.intstart)
    _intPosLabel = new QLabel(tr("INT Position (intpos): 0"), this);
    paramsLayout->addWidget(_intPosLabel);

    QHBoxLayout* intPosLayout = new QHBoxLayout();
    _intPosSlider = new QSlider(Qt::Horizontal, this);
    _intPosSlider->setMinimum(0);
    _intPosSlider->setMaximum(80000);
    _intPosSlider->setSingleStep(1);
    _intPosSlider->setPageStep(224);

    _intPosSpinBox = new QSpinBox(this);
    _intPosSpinBox->setMinimum(0);
    _intPosSpinBox->setMaximum(80000);
    _intPosSpinBox->setSingleStep(1);

    intPosLayout->addWidget(_intPosSlider, 1);
    intPosLayout->addWidget(_intPosSpinBox);
    paramsLayout->addLayout(intPosLayout);

    paramsLayout->addSpacing(20);

    // intlen parameter
    _intLenLabel = new QLabel(tr("INT Length (intlen): 32"), this);
    paramsLayout->addWidget(_intLenLabel);

    QHBoxLayout* intLenLayout = new QHBoxLayout();
    _intLenSlider = new QSlider(Qt::Horizontal, this);
    _intLenSlider->setMinimum(1);
    _intLenSlider->setMaximum(512);
    _intLenSlider->setSingleStep(1);
    _intLenSlider->setPageStep(10);

    _intLenSpinBox = new QSpinBox(this);
    _intLenSpinBox->setMinimum(1);
    _intLenSpinBox->setMaximum(512);
    _intLenSpinBox->setSingleStep(1);

    intLenLayout->addWidget(_intLenSlider, 1);
    intLenLayout->addWidget(_intLenSpinBox);
    paramsLayout->addLayout(intLenLayout);

    mainLayout->addWidget(paramsGroup);

    // Add info label
    QLabel* infoLabel = new QLabel(
        tr("Changes apply in real-time.\n"
           "Cancel restores original values."),
        this);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("QLabel { color: gray; font-size: 9pt; }");
    mainLayout->addWidget(infoLabel);

    // Status label
    _statusLabel = new QLabel(this);
    _statusLabel->setWordWrap(true);
    _statusLabel->setStyleSheet("QLabel { color: red; font-weight: bold; }");
    _statusLabel->setVisible(false);
    mainLayout->addWidget(_statusLabel);

    mainLayout->addStretch();

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    _cancelButton = new QPushButton(tr("Cancel"), this);
    _okButton = new QPushButton(tr("OK"), this);

    buttonLayout->addWidget(_cancelButton);
    buttonLayout->addWidget(_okButton);

    mainLayout->addLayout(buttonLayout);

    // Connect signals - real-time apply
    connect(_intPosSlider, &QSlider::valueChanged, this, &IntParametersDialog::onIntPosChanged);
    connect(_intPosSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), _intPosSlider, &QSlider::setValue);

    connect(_intLenSlider, &QSlider::valueChanged, this, &IntParametersDialog::onIntLenChanged);
    connect(_intLenSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), _intLenSlider, &QSlider::setValue);

    connect(_okButton, &QPushButton::clicked, this, &IntParametersDialog::onOkClicked);
    connect(_cancelButton, &QPushButton::clicked, this, &IntParametersDialog::onCancelClicked);
}

void IntParametersDialog::loadValues()
{
    if (!_binding || !_binding->emulator())
    {
        setControlsEnabled(false);
        return;
    }

    EmulatorContext* context = _binding->emulator()->GetContext();
    if (!context)
    {
        setControlsEnabled(false);
        return;
    }

    CONFIG& config = context->config;

    // Store original values for Cancel
    _originalIntStart = config.intstart;
    _originalIntLen = config.intlen;

    // Load values into UI (block signals to avoid applying during load)
    _intPosSlider->blockSignals(true);
    _intPosSpinBox->blockSignals(true);
    _intLenSlider->blockSignals(true);
    _intLenSpinBox->blockSignals(true);

    _intPosSlider->setValue(config.intstart);
    _intPosSpinBox->setValue(config.intstart);
    _intPosLabel->setText(tr("INT Position (intpos): %1").arg(config.intstart));

    _intLenSlider->setValue(config.intlen);
    _intLenSpinBox->setValue(config.intlen);
    _intLenLabel->setText(tr("INT Length (intlen): %1").arg(config.intlen));

    _intPosSlider->blockSignals(false);
    _intPosSpinBox->blockSignals(false);
    _intLenSlider->blockSignals(false);
    _intLenSpinBox->blockSignals(false);

    setControlsEnabled(true);
}

void IntParametersDialog::applyIntPos(int value)
{
    if (!_binding || !_binding->emulator())
        return;

    EmulatorContext* context = _binding->emulator()->GetContext();
    if (!context)
        return;

    context->config.intstart = static_cast<unsigned>(value);
}

void IntParametersDialog::applyIntLen(int value)
{
    if (!_binding || !_binding->emulator())
        return;

    EmulatorContext* context = _binding->emulator()->GetContext();
    if (!context)
        return;

    context->config.intlen = static_cast<unsigned>(value);
}

void IntParametersDialog::restoreOriginalValues()
{
    if (!_binding || !_binding->emulator())
        return;

    EmulatorContext* context = _binding->emulator()->GetContext();
    if (!context)
        return;

    context->config.intstart = _originalIntStart;
    context->config.intlen = _originalIntLen;
}

void IntParametersDialog::onIntPosChanged(int value)
{
    _intPosLabel->setText(tr("INT Position (intpos): %1").arg(value));
    _intPosSpinBox->setValue(value);
    applyIntPos(value);
}

void IntParametersDialog::onIntLenChanged(int value)
{
    _intLenLabel->setText(tr("INT Length (intlen): %1").arg(value));
    _intLenSpinBox->setValue(value);
    applyIntLen(value);
}

void IntParametersDialog::onOkClicked()
{
    accept();
}

void IntParametersDialog::onCancelClicked()
{
    restoreOriginalValues();
    reject();
}

void IntParametersDialog::onBindingUnbound()
{
    setControlsEnabled(false);
    _statusLabel->setText(tr("Emulator closed. Please reopen this dialog."));
    _statusLabel->setVisible(true);
}

void IntParametersDialog::setControlsEnabled(bool enabled)
{
    _intPosSlider->setEnabled(enabled);
    _intPosSpinBox->setEnabled(enabled);
    _intLenSlider->setEnabled(enabled);
    _intLenSpinBox->setEnabled(enabled);
    _okButton->setEnabled(enabled);
    _cancelButton->setEnabled(enabled);
}
