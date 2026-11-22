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
#include <QSpinBox>
#include <QValidator>
#include <vector>
#include "emulator/emulator.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/targets/debugcpuid.h"
#include "emulator/sound/chips/gs/gsdebughooks.h"

class BreakpointEditor : public QDialog
{
    Q_OBJECT
public:
    enum Mode { Add, Edit };
    
    explicit BreakpointEditor(std::shared_ptr<Emulator> emulator, Mode mode, QWidget* parent = nullptr);
    /// Edits breakpoint `breakpointId` of `cpu`'s breakpoints (a card CPU's are its own manager's)
    explicit BreakpointEditor(std::shared_ptr<Emulator> emulator, Mode mode, uint16_t breakpointId, QWidget* parent = nullptr,
                              DebugCpuId cpu = DebugCpuId::Main);
    ~BreakpointEditor();
    
    BreakpointDescriptor getBreakpointDescriptor() const;

    /// A condition compile refusal split for the editor: the message without the "condition: " prefix and the
    /// " at column N" suffix, and the 1-based column (0: none)
    static QString SplitConditionError(const QString& error, int& column);
    
private slots:
    void onTypeChanged(int index);
    void validateInput();
    void onAddressChanged(const QString& text);
    void onAccept();
    
private:
    void setupUI();
    void addNextTypes();  // the ZX Next kinds into the type list (once)
    /// The event kinds this machine tells, grouped: the card's (a card fitted), the protocol's card and host sides,
    /// the machine's (by model)
    void addEventKinds();
    /// An event kind into the list (an edited breakpoint's kind the list does not offer: at the end)
    void addEventKind(GSCardEvent kind);
    /// The kind in the type list's item data
    BreakpointTypeEnum selectedType() const;
    void selectType(BreakpointTypeEnum type);
    /// The selected event kind; Count when a group's title or nothing is selected
    GSCardEvent selectedEvent() const;
    /// The CPU whose breakpoints get it: an event's by its side, a memory or port breakpoint's by the CPU choice
    DebugCpuId selectedCpu() const;
    BreakpointManager* managerOf(DebugCpuId cpu) const;
    /// The CPU choice follows the kind: memory and ports on any CPU fitted, a card event on a card's, the rest main's
    void updateCpuRow();
    void populateGroupComboBox();
    void loadBreakpointData(uint16_t breakpointId);
    bool keyAddress(const QString& text, uint16_t& address);
    bool validateAddress(const QString& text, uint16_t& address);
    /// The access bits of the selected kind (BRK_MEM_*, BRK_IO_*, BRK_KEY_*; NextREG: BRK_IO_IN read, OUT write)
    uint8_t selectedAccess(BreakpointTypeEnum type) const;
    /// Compiles the condition for the selected kind and access (the manager's check: trigger, labels, this machine)
    /// and shows what it reads as or why it is refused, the place marked. False when refused
    bool checkCondition();
    /// The record's fields follow the Count only box: editable only for a breakpoint that only counts
    void updateHitLogRow();
    
    std::shared_ptr<Emulator> _emulator;
    Mode _mode;
    uint16_t _breakpointId;
    BreakpointDescriptor _descriptor;
    DebugCpuId _editCpu = DebugCpuId::Main;  ///< Edit: whose breakpoint it is
    std::vector<DebugCpuId> _cpus;           ///< the CPUs fitted (main first)
    bool _ngsFitted = false;                 ///< a NeoGS among them: its own events are offered
    
    QComboBox* _typeCombo;
    QWidget* _cpuRow;             // a card fitted: whose breakpoint (memory, ports, card events)
    QComboBox* _cpuCombo;         // item data: the DebugCpuId
    QGroupBox* _eventBox;
    QComboBox* _eventCombo;       // item data: the GSCardEvent, -1 a group's title
    QLineEdit* _eventValueEdit;   // optional: stop only on this byte
    QLineEdit* _eventMaskEdit;    // optional: compare only these bits
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
    QWidget* _conditionRow;       // the kinds that take a condition (not keyboard)
    QLineEdit* _conditionEdit;    // optional: stop only when it holds
    QLabel* _conditionStatus;     // what it reads as, or why it is refused
    QString _loadedCondition;      // the edited breakpoint's condition as loaded
    QString _loadedConditionError; // and why it failed while running ("" while it works)
    QComboBox* _groupCombo;
    QLineEdit* _noteEdit;
    QCheckBox* _activeCheck;
    QCheckBox* _countOnlyCheck = nullptr;  // BreakpointSpec::stop = false: only counts the hits
    QWidget* _hitLogRow;         // a breakpoint that only counts: its per-hit record (BreakpointSpec::hitLog)
    QSpinBox* _hitLogSpin;       // the hits kept, 0: none
    QComboBox* _hitLogKeepCombo; // the first ones or the newest
    
    QPushButton* _okButton;
    QPushButton* _cancelButton;
    
    QLabel* _validationLabel;
};

#endif // BREAKPOINTEDITOR_H
