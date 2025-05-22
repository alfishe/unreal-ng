#pragma once
#ifndef BREAKPOINTEDITOR_H
#define BREAKPOINTEDITOR_H

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QValidator>
#include "emulator/emulator.h"
#include "debugger/breakpoints/breakpointmanager.h"

class BreakpointEditor : public QDialog
{
    Q_OBJECT
public:
    enum Mode { Add, Edit };
    
    explicit BreakpointEditor(Emulator* emulator, Mode mode, QWidget* parent = nullptr);
    explicit BreakpointEditor(Emulator* emulator, Mode mode, uint16_t breakpointId, QWidget* parent = nullptr);
    ~BreakpointEditor();
    
    BreakpointDescriptor getBreakpointDescriptor() const;
    
private slots:
    void onTypeChanged(int index);
    void validateInput();
    void onAddressChanged(const QString& text);
    void onAccept();
    
private:
    void setupUI();
    void populateGroupComboBox();
    void loadBreakpointData(uint16_t breakpointId);
    bool validateAddress(const QString& text, uint16_t& address);
    
    Emulator* _emulator;
    Mode _mode;
    uint16_t _breakpointId;
    BreakpointDescriptor _descriptor;
    
    QComboBox* _typeCombo;
    QLineEdit* _addressEdit;
    QGroupBox* _memoryAccessBox;
    QGroupBox* _portAccessBox;
    QCheckBox* _readCheck;
    QCheckBox* _writeCheck;
    QCheckBox* _executeCheck;
    QCheckBox* _inCheck;
    QCheckBox* _outCheck;
    QComboBox* _groupCombo;
    QLineEdit* _noteEdit;
    QCheckBox* _activeCheck;
    
    QPushButton* _okButton;
    QPushButton* _cancelButton;
    
    QLabel* _validationLabel;
};

#endif // BREAKPOINTEDITOR_H
