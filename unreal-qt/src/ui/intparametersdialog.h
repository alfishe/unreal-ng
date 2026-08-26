#pragma once

#include <QDialog>
#include <QSlider>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>

class EmulatorBinding;

/// @brief Dialog for configuring INT timing parameters
/// This dialog allows configuration of interrupt signal timing parameters:
/// - intpos (intstart in config): INT pulse start position in clock cycles (0-80000)
/// - intlen: Duration of INT signal in clock cycles (1-512)
///
/// Architecture: Follows EmulatorBinding pattern
/// - Dialog stores EmulatorBinding pointer (not raw Emulator)
/// - Changes apply in real-time as sliders/spinboxes are adjusted
/// - Cancel restores original values, OK keeps current values
/// - Disables controls when binding becomes unready/unbound
/// - User must reopen dialog to access new emulator instance
class IntParametersDialog : public QDialog
{
    Q_OBJECT

public:
    explicit IntParametersDialog(EmulatorBinding* binding, QWidget* parent = nullptr);
    ~IntParametersDialog();

private slots:
    void onIntPosChanged(int value);
    void onIntLenChanged(int value);
    void onOkClicked();
    void onCancelClicked();
    void onBindingUnbound();

private:
    void setupUI();
    void loadValues();
    void applyIntPos(int value);
    void applyIntLen(int value);
    void restoreOriginalValues();
    void setControlsEnabled(bool enabled);

    EmulatorBinding* _binding = nullptr;

    // Original values (for Cancel)
    unsigned _originalIntStart = 0;
    unsigned _originalIntLen = 32;

    // UI elements for intpos (mapped to config.intstart)
    QLabel* _intPosLabel = nullptr;
    QSlider* _intPosSlider = nullptr;
    QSpinBox* _intPosSpinBox = nullptr;

    // UI elements for intlen
    QLabel* _intLenLabel = nullptr;
    QSlider* _intLenSlider = nullptr;
    QSpinBox* _intLenSpinBox = nullptr;

    // Buttons
    QPushButton* _okButton = nullptr;
    QPushButton* _cancelButton = nullptr;

    // Status
    QLabel* _statusLabel = nullptr;
};
