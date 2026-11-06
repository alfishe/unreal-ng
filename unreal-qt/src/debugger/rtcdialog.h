#pragma once

#include <memory>

#include <QDialog>

class Emulator;
class EmulatorContext;
class QLabel;
class QTableWidget;
class QTableWidgetItem;
class QTimer;

/// The machine's CMOS clock in the debugger (debugger enhancements TODO #6): the chip, its ports and time base, the
/// time as the guest reads it, registers A-D and the alarm decoded, and every cell in a hex grid edited in place.
/// It renders DeviceState::Rtc and writes through RtcAccess::Write, the calls CLI `rtc`, WebAPI /state/rtc +
/// /rtc/cells, MCP `rtc` and Lua / Python `rtc_*` make: reads peek (register C keeps its flags), a write acts as a
/// guest write (the time registers set the clock) and is a debugger edit while time travel records
class RtcDialog : public QDialog
{
    Q_OBJECT

public:
    RtcDialog(std::shared_ptr<Emulator> emulator, QWidget* parent = nullptr);

public slots:
    /// Reads the clock again (twice a second while the dialog shows)
    void refresh();

private slots:
    void cellEdited(QTableWidgetItem* item);

private:
    EmulatorContext* context() const;
    std::shared_ptr<Emulator> _emulator;
    QLabel* _summary = nullptr;
    QLabel* _status = nullptr;
    QTableWidget* _cells = nullptr;
    QTimer* _timer = nullptr;
    bool _filling = false;  ///< refresh fills the grid: not the user's edit
};
