#pragma once

#include <memory>

#include <QByteArray>
#include <QDialog>

class Emulator;
class EmulatorContext;
class QComboBox;
class QHexView;
class QLabel;
class QPushButton;
class QStackedWidget;

/// Device memory regions in the debugger (emulator/memory/devicememory.h; debugger additions tdd §2, TODO A2q):
/// memory a device owns outside the CPU's RAM / ROM pages, by name - the Sprinter's video RAM "vram", the TS-Conf
/// palette "cram" and sprite table "sfile". The region is shown as hex; a typed byte goes through the device's own
/// write path (DeviceMemory::Write), a tool edit for TTD, as on every automation surface. A machine without any
/// region says so instead of an empty view (the debugger's toolbar button is off for it: HasRegions).
class DeviceMemoryDialog : public QDialog
{
    Q_OBJECT

public:
    DeviceMemoryDialog(std::shared_ptr<Emulator> emulator, QWidget* parent = nullptr);

    /// The machine has at least one device memory region
    static bool HasRegions(Emulator* emulator);

private slots:
    void regionSelected(int index);
    void reload();
    void documentChanged();

private:
    EmulatorContext* context() const;

    std::shared_ptr<Emulator> _emulator;
    QComboBox* _regions = nullptr;
    QPushButton* _refresh = nullptr;
    QLabel* _description = nullptr;
    QStackedWidget* _pages = nullptr;  // the hex view, or the "nothing here" note
    QLabel* _empty = nullptr;
    QHexView* _hexView = nullptr;
    QByteArray _shown;      // the bytes as last read: a typed byte is the difference
    bool _loading = false;  // setDocument emits changes too
};
