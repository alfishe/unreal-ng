#pragma once

#include <memory>
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
    
    explicit BreakpointEditor(std::shared_ptr<Emulator> emulator, Mode mode, QWidget* parent = nullptr);
    explicit BreakpointEditor(std::shared_ptr<Emulator> emulator, Mode mode, uint16_t breakpointId, QWidget* parent = nullptr);
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
    bool keyAddress(const QString& text, uint16_t& address);
    bool validateAddress(const QString& text, uint16_t& address);
    /// The access bits of the selected kind (BRK_MEM_*, BRK_IO_*, BRK_KEY_*; NextREG: BRK_IO_IN read, OUT write)
    uint8_t selectedAccess(BreakpointTypeEnum type) const;
    
    std::shared_ptr<Emulator> _emulator;
    Mode _mode;
    uint16_t _breakpointId;
    BreakpointDescriptor _descriptor;
    
    QComboBox* _typeCombo;
    QLabel* _addressLabel;      // "Address:", "Port:", "Register:", "Line:", "List index:", "Key:"
    QLineEdit* _addressEdit;
    QLabel* _endLabel;
    QLineEdit* _endEdit;        // optional range end
    QWidget* _pageRow;          // memory only
    QLineEdit* _pageEdit;       // optional page "ram5": a physical breakpoint
    QCheckBox* _slotOnlyCheck;  // with a page: only through the slot of the address
    QLineEdit* _maskEdit;       // ports: optional mask
    QLineEdit* _hitsEdit;       // optional hit policy "5", ">=5", "%5"
    QGroupBox* _memoryAccessBox;
    QGroupBox* _portAccessBox;
    QCheckBox* _readCheck;
    QCheckBox* _writeCheck;
    QCheckBox* _executeCheck;
    QCheckBox* _inCheck;
    QCheckBox* _outCheck;
    // ZX Next: NextREG (read / write, the doors a write comes through, a value), beam (the pixel clock), magic opcode
    QGroupBox* _nextRegBox;
    QCheckBox* _nrReadCheck;
    QCheckBox* _nrWriteCheck;
    QCheckBox* _doorNextRegCheck;
    QCheckBox* _doorPortCheck;
    QCheckBox* _doorCopperCheck;
    QLineEdit* _nrValueEdit;      // optional: stop only on this value
    QLineEdit* _nrValueMaskEdit;  // optional: compare only these bits (#FF with a value)
    QGroupBox* _beamBox;
    QLineEdit* _hcEdit;           // the pixel clock in the row, 0..511
    QGroupBox* _magicBox;
    QComboBox* _magicCombo;       // ED FF, DD 01
    QGroupBox* _keyboardBox;
    QCheckBox* _keyPressCheck;
    QCheckBox* _keyReleaseCheck;
    QComboBox* _groupCombo;
    QLineEdit* _noteEdit;
    QCheckBox* _activeCheck;
    
    QPushButton* _okButton;
    QPushButton* _cancelButton;
    
    QLabel* _validationLabel;
};

#endif // BREAKPOINTEDITOR_H
