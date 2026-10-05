#pragma once

#include <QByteArray>
#include <QDialog>

class Emulator;
class QComboBox;
class QHexView;
class QLabel;

/// Device memory regions in the debugger (emulator/memory/devicememory.h; debugger additions tdd §2, TODO A2q):
/// memory a device owns outside the CPU's RAM / ROM pages, by name - the Sprinter's video RAM "vram", the TS-Conf
/// palette "cram" and sprite table "sfile". The region is shown as hex; a typed byte goes through the device's own
/// write path (DeviceMemory::Write), a tool edit for TTD, as on every automation surface.
class DeviceMemoryDialog : public QDialog
{
    Q_OBJECT

public:
    DeviceMemoryDialog(Emulator* emulator, QWidget* parent = nullptr);

private slots:
    void regionSelected(int index);
    void reload();
    void documentChanged();

private:
    Emulator* _emulator;
    QComboBox* _regions = nullptr;
    QLabel* _description = nullptr;
    QHexView* _hexView = nullptr;
    QByteArray _shown;      // the bytes as last read: a typed byte is the difference
    bool _loading = false;  // setDocument emits changes too
};
