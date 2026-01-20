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
    
    // Connect to binding signals
    if (_binding)
    {
        connect(_binding, &EmulatorBinding::unbound, this, &IntParametersDialog::onBindingUnbound);
        loadValues();
    }
    else
    {
        setControlsEnabled(false);
        _statusLabel->setText(tr("No emulator binding available"));
    }
}

IntParametersDialog::~IntParametersDialog()
{
}

void IntParametersDialog::setupUI()
{
    setWindowTitle(tr("INT Parameters"));
    setModal(false);  // Non-blocking dialog
    resize(400, 280);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Create group box for parameters
    QGroupBox* paramsGroup = new QGroupBox(tr("Interrupt Timing Parameters"), this);
    QVBoxLayout* paramsLayout = new QVBoxLayout(paramsGroup);
    
    // intpos parameter (maps to config.intstart)
    // INT pulse start position (delay in clock cycles): 0 to 2000
    _intPosLabel = new QLabel(tr("INT Position (intpos): 0"), this);
    paramsLayout->addWidget(_intPosLabel);
    
    QHBoxLayout* intPosLayout = new QHBoxLayout();
    _intPosSlider = new QSlider(Qt::Horizontal, this);
    _intPosSlider->setMinimum(0);
    _intPosSlider->setMaximum(2000);
    _intPosSlider->setSingleStep(1);
    _intPosSlider->setPageStep(10);
    
    _intPosSpinBox = new QSpinBox(this);
    _intPosSpinBox->setMinimum(0);
    _intPosSpinBox->setMaximum(2000);
    _intPosSpinBox->setSingleStep(1);
    
    intPosLayout->addWidget(_intPosSlider, 1);
    intPosLayout->addWidget(_intPosSpinBox);
    paramsLayout->addLayout(intPosLayout);
    
    paramsLayout->addSpacing(20);
    
    // intlen parameter
    // Duration of INT signal in clock cycles: 1 to 512
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
        tr("These parameters control Z80 interrupt signal timing.\n"
           "Changes take effect on the next frame."),
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
    
    _applyButton = new QPushButton(tr("Apply"), this);
    _closeButton = new QPushButton(tr("Close"), this);
    
    buttonLayout->addWidget(_applyButton);
    buttonLayout->addWidget(_closeButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Connect signals
    connect(_intPosSlider, &QSlider::valueChanged, this, &IntParametersDialog::onIntPosChanged);
    connect(_intPosSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), _intPosSlider, &QSlider::setValue);
    
    connect(_intLenSlider, &QSlider::valueChanged, this, &IntParametersDialog::onIntLenChanged);
    connect(_intLenSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), _intLenSlider, &QSlider::setValue);
    
    connect(_applyButton, &QPushButton::clicked, this, &IntParametersDialog::onApplyClicked);
    connect(_closeButton, &QPushButton::clicked, this, &QDialog::close);
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
    
    // Load intstart (displayed as intpos)
    _intPosSlider->setValue(config.intstart);
    _intPosSpinBox->setValue(config.intstart);
    
    // Load intlen
    _intLenSlider->setValue(config.intlen);
    _intLenSpinBox->setValue(config.intlen);
    
    setControlsEnabled(true);
}

void IntParametersDialog::applyValues()
{
    if (!_binding || !_binding->emulator())
    {
        QMessageBox::warning(this, tr("Error"), tr("No active emulator instance."));
        return;
    }
    
    EmulatorContext* context = _binding->emulator()->GetContext();
    if (!context)
    {
        QMessageBox::warning(this, tr("Error"), tr("Cannot access emulator context."));
        return;
    }
    
    CONFIG& config = context->config;
    
    // Apply intpos value to config.intstart
    unsigned newIntStart = static_cast<unsigned>(_intPosSpinBox->value());
    config.intstart = newIntStart;
    
    // Apply intlen value
    unsigned newIntLen = static_cast<unsigned>(_intLenSpinBox->value());
    config.intlen = newIntLen;
}

void IntParametersDialog::onApplyClicked()
{
    applyValues();
}

void IntParametersDialog::onIntPosChanged(int value)
{
    _intPosLabel->setText(tr("INT Position (intpos): %1").arg(value));
    _intPosSpinBox->setValue(value);
}

void IntParametersDialog::onIntLenChanged(int value)
{
    _intLenLabel->setText(tr("INT Length (intlen): %1").arg(value));
    _intLenSpinBox->setValue(value);
}

void IntParametersDialog::onBindingUnbound()
{
    // Emulator was closed - disable all controls
    setControlsEnabled(false);
    _statusLabel->setText(tr("Emulator closed. Please reopen this dialog to access a new emulator instance."));
    _statusLabel->setVisible(true);
}

void IntParametersDialog::setControlsEnabled(bool enabled)
{
    _intPosSlider->setEnabled(enabled);
    _intPosSpinBox->setEnabled(enabled);
    _intLenSlider->setEnabled(enabled);
    _intLenSpinBox->setEnabled(enabled);
    _applyButton->setEnabled(enabled);
}
