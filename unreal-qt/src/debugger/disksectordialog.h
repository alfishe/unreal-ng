#pragma once

#include <QByteArray>
#include <QDialog>

class Emulator;
class QComboBox;
class QHexView;
class QLabel;
class QSpinBox;

/// A disk sector in the debugger (debugger additions tdd §3): drive, cylinder, side and sector ID choose it, its
/// data field is shown as hex, and a typed byte is written through SectorWrite (the data CRC follows, the image
/// counts as modified, a TTD tool edit) - the same path as every automation surface.
class DiskSectorDialog : public QDialog
{
    Q_OBJECT

public:
    DiskSectorDialog(Emulator* emulator, QWidget* parent = nullptr);

public slots:
    void load();

private slots:
    void documentChanged();

private:
    Emulator* _emulator;
    QComboBox* _drive = nullptr;
    QSpinBox* _cylinder = nullptr;
    QSpinBox* _side = nullptr;
    QSpinBox* _sector = nullptr;
    QLabel* _status = nullptr;
    QHexView* _hexView = nullptr;
    QByteArray _shown;      // the data field as last read: a typed byte is the difference
    bool _loading = false;
};
