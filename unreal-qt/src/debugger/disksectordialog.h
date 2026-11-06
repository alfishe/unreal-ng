#pragma once

#include <memory>

#include <QByteArray>
#include <QDialog>

class Emulator;
class EmulatorContext;
class FDD;
class QComboBox;
class QHexView;
class QLabel;
class QSpinBox;
class QStackedWidget;
class QWidget;

/// A disk sector in the debugger (debugger additions tdd §3): drive, cylinder, side and sector ID choose it, its
/// data field is shown as hex, and a typed byte is written through SectorWrite (the data CRC follows, the image
/// counts as modified, a TTD tool edit) - the same path as every automation surface. The choices come from the
/// disk: the machine's drives with their images, the image's cylinders and sides, the IDs the track holds; a
/// choice shows its sector at once, Prev / Next walk the disk sector by sector.
class DiskSectorDialog : public QDialog
{
    Q_OBJECT

public:
    DiskSectorDialog(std::shared_ptr<Emulator> emulator, QWidget* parent = nullptr);

    /// The machine has a floppy drive (the debugger's toolbar button is off without one)
    static bool HasDrives(Emulator* emulator);

public slots:
    void load();

private slots:
    void driveSelected();
    void trackSelected();
    void step(int direction);
    void documentChanged();

private:
    EmulatorContext* context() const;
    FDD* drive() const;
    void showNote(const QString& text);

    std::shared_ptr<Emulator> _emulator;
    QComboBox* _drive = nullptr;
    QSpinBox* _cylinder = nullptr;
    QSpinBox* _side = nullptr;
    QComboBox* _sector = nullptr;
    QWidget* _controls = nullptr;
    QLabel* _status = nullptr;
    QStackedWidget* _pages = nullptr;  // the hex view, or the "nothing here" note
    QLabel* _empty = nullptr;
    QHexView* _hexView = nullptr;
    QByteArray _shown;      // the data field as last read: a typed byte is the difference
    bool _loading = false;  // refilling the choices or the document: no load / write meanwhile
};
