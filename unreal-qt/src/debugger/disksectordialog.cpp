#include "disksectordialog.h"
#include "emulator/remotecontrolguard.h"

#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <string>
#include <vector>

#include "QHexView/model/buffer/qmemorybuffer.h"
#include "QHexView/model/qhexcursor.h"
#include "QHexView/model/qhexdocument.h"
#include "QHexView/qhexview.h"
#include "debugger/hexviewstyle.h"
#include "debugger/media/sectorwrite.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"

namespace
{
constexpr int kDrives = 4;

DiskImage* ImageIn(FDD* fdd)
{
    return fdd && fdd->isDiskInserted() ? fdd->getDiskImage() : nullptr;
}

/// The sectors of a track by ID (each ID once, the first in the stream), with what is odd about them
std::vector<DiskImage::Sector*> SectorsById(DiskImage::Track* track)
{
    std::vector<DiskImage::Sector*> sectors;
    if (!track)
        return sectors;
    for (DiskImage::Sector& sector : track->sectors())
        if (std::none_of(sectors.begin(), sectors.end(),
                         [&](DiskImage::Sector* seen) { return seen->number() == sector.number(); }))
            sectors.push_back(&sector);
    std::sort(sectors.begin(), sectors.end(),
              [](DiskImage::Sector* a, DiskImage::Sector* b) { return a->number() < b->number(); });
    return sectors;
}
}  // namespace

bool DiskSectorDialog::HasDrives(Emulator* emulator)
{
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;  // lifetime-ok: the caller holds it
    return context && std::any_of(std::begin(context->coreState.diskDrives), std::end(context->coreState.diskDrives),
                                  [](FDD* fdd) { return fdd != nullptr; });
}

EmulatorContext* DiskSectorDialog::context() const
{
    return _emulator ? _emulator->GetContext() : nullptr;  // lifetime-ok: the dialog holds the shared_ptr
}

FDD* DiskSectorDialog::drive() const
{
    EmulatorContext* machine = context();
    const int index = _drive->currentData().toInt();
    return machine && _drive->currentIndex() >= 0 && index < kDrives ? machine->coreState.diskDrives[index] : nullptr;
}

DiskSectorDialog::DiskSectorDialog(std::shared_ptr<Emulator> emulator, QWidget* parent) : QDialog(parent), _emulator(emulator)
{
    setWindowTitle("Disk sector");
    resize(720, 520);

    _drive = new QComboBox(this);
    _drive->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    _cylinder = new QSpinBox(this);
    _side = new QSpinBox(this);
    _sector = new QComboBox(this);
    _sector->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    _sector->setToolTip("The sector IDs on this track (the R byte of their address marks; TR-DOS 1-16)");
    QPushButton* previous = new QPushButton("◀", this);
    previous->setToolTip("The previous sector (across tracks)");
    QPushButton* next = new QPushButton("▶", this);
    next->setToolTip("The next sector (across tracks)");
    QPushButton* reloadButton = new QPushButton("Reload", this);
    reloadButton->setToolTip("Read the sector again (the machine may have written it)");

    _controls = new QWidget(this);
    QHBoxLayout* top = new QHBoxLayout(_controls);
    top->setContentsMargins(0, 0, 0, 0);
    top->addWidget(new QLabel("Drive", this));
    top->addWidget(_drive);
    top->addSpacing(8);
    top->addWidget(new QLabel("Cylinder", this));
    top->addWidget(_cylinder);
    top->addWidget(new QLabel("Side", this));
    top->addWidget(_side);
    top->addWidget(new QLabel("Sector", this));
    top->addWidget(_sector);
    top->addWidget(previous);
    top->addWidget(next);
    top->addStretch(1);
    top->addWidget(reloadButton);

    _status = new QLabel(this);
    _status->setWordWrap(true);
    _hexView = new QHexView(this);
    _empty = MakeEmptyNote(this);
    _pages = new QStackedWidget(this);
    _pages->addWidget(_empty);
    _pages->addWidget(_hexView);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(_controls);
    layout->addWidget(_status);
    layout->addWidget(_pages, 1);

    // The machine's drives, each with its image; the first with a disk is shown
    int first = -1;
    if (EmulatorContext* machine = context())
        for (int i = 0; i < kDrives; ++i)
            if (FDD* fdd = machine->coreState.diskDrives[i])
            {
                DiskImage* image = ImageIn(fdd);
                const QString name = image ? QFileInfo(QString::fromStdString(image->getFilePath())).fileName() : "";
                _drive->addItem(QString("%1: %2").arg(QChar('A' + i)).arg(
                                    !image ? QString("empty") : name.isEmpty() ? QString("disk") : name),
                                i);
                if (image && first < 0)
                    first = _drive->count() - 1;
            }
    if (_drive->count() == 0)
    {
        _controls->setEnabled(false);
        showNote("This machine has no floppy drives.");
    }
    else
    {
        _drive->setCurrentIndex(std::max(first, 0));
    }

    connect(_drive, qOverload<int>(&QComboBox::currentIndexChanged), this, &DiskSectorDialog::driveSelected);
    connect(_cylinder, qOverload<int>(&QSpinBox::valueChanged), this, &DiskSectorDialog::trackSelected);
    connect(_side, qOverload<int>(&QSpinBox::valueChanged), this, &DiskSectorDialog::trackSelected);
    connect(_sector, qOverload<int>(&QComboBox::currentIndexChanged), this, &DiskSectorDialog::load);
    connect(previous, &QPushButton::clicked, this, [this]() { step(-1); });
    connect(next, &QPushButton::clicked, this, [this]() { step(1); });
    connect(reloadButton, &QPushButton::clicked, this, &DiskSectorDialog::load);
    // A sector has a fixed size: typing overwrites, never inserts
    connect(_hexView->hexCursor(), &QHexCursor::modeChanged, this, [this]() {
        if (_hexView->hexCursor()->mode() != QHexCursor::Mode::Overwrite)
            _hexView->hexCursor()->setMode(QHexCursor::Mode::Overwrite);
    });
    if (_drive->count() > 0)
        driveSelected();
}

void DiskSectorDialog::showNote(const QString& text)
{
    _status->clear();
    _empty->setText(text);
    _pages->setCurrentWidget(_empty);
}

void DiskSectorDialog::driveSelected()
{
    // The image's geometry bounds the choices
    DiskImage* image = ImageIn(drive());
    {
        QSignalBlocker blockCylinder(_cylinder);
        QSignalBlocker blockSide(_side);
        _cylinder->setRange(0, image ? std::max(1, static_cast<int>(image->getCylinders())) - 1 : 0);
        _side->setRange(0, image ? std::max(1, static_cast<int>(image->getSides())) - 1 : 0);
        _cylinder->setValue(0);
        _side->setValue(0);
    }
    _cylinder->setEnabled(image != nullptr);
    _side->setEnabled(image && image->getSides() > 1);
    trackSelected();
}

void DiskSectorDialog::trackSelected()
{
    // The IDs this track holds; the same ID stays chosen when the new track has it
    DiskImage* image = ImageIn(drive());
    DiskImage::Track* track = image ? image->getTrackForCylinderAndSide(static_cast<uint8_t>(_cylinder->value()),
                                                                        static_cast<uint8_t>(_side->value()))
                                    : nullptr;
    const QVariant was = _sector->currentData();
    {
        QSignalBlocker block(_sector);
        _sector->clear();
        for (DiskImage::Sector* sector : SectorsById(track))
        {
            QString text = QString::number(sector->number());
            if (!sector->hasData)
                text += " (no data)";
            else if (sector->deleted)
                text += " (deleted)";
            else if (!sector->isDataCRCValid())
                text += " (bad CRC)";
            _sector->addItem(text, sector->number());
        }
        const int keep = _sector->findData(was);
        _sector->setCurrentIndex(keep >= 0 ? keep : 0);
    }
    _sector->setEnabled(_sector->count() > 0);
    load();
}

void DiskSectorDialog::step(int direction)
{
    // Within the track first, then the neighbour track (side, then cylinder) at its first / last sector
    const int index = _sector->currentIndex() + direction;
    if (index >= 0 && index < _sector->count())
    {
        _sector->setCurrentIndex(index);
        return;
    }
    const int sides = _side->maximum() + 1;
    int linear = _cylinder->value() * sides + _side->value();
    const int last = (_cylinder->maximum() + 1) * sides - 1;
    for (linear += direction; linear >= 0 && linear <= last; linear += direction)
    {
        {
            QSignalBlocker blockCylinder(_cylinder);
            QSignalBlocker blockSide(_side);
            _cylinder->setValue(linear / sides);
            _side->setValue(linear % sides);
        }
        {
            QSignalBlocker block(_sector);
            _sector->clear();
        }
        trackSelected();
        if (_sector->count() == 0)
            continue;  // an unformatted track: on to the next
        _sector->setCurrentIndex(direction > 0 ? 0 : _sector->count() - 1);
        return;
    }
}

void DiskSectorDialog::load()
{
    _shown.clear();
    FDD* fdd = drive();
    DiskImage* image = ImageIn(fdd);
    if (!image)
    {
        showNote(fdd ? QString("No disk in drive %1.").arg(QChar('A' + _drive->currentData().toInt()))
                     : QString("This machine has no floppy drives."));
        return;
    }
    DiskImage::Track* track = image->getTrackForCylinderAndSide(static_cast<uint8_t>(_cylinder->value()),
                                                                static_cast<uint8_t>(_side->value()));
    if (!track || _sector->count() == 0)
    {
        showNote(QString("Cylinder %1, side %2 has no sectors (unformatted).").arg(_cylinder->value()).arg(_side->value()));
        return;
    }
    DiskImage::Sector* sector = track->findSector(static_cast<uint8_t>(_sector->currentData().toInt()));
    if (!sector || !sector->hasData || !sector->data)
    {
        showNote("This sector has an ID field but no data field.");
        return;
    }

    _status->setText(QString("Cylinder %1, side %2: %3 sectors. Sector %4: %5 bytes, data CRC %6%7%8")
                         .arg(_cylinder->value())
                         .arg(_side->value())
                         .arg(track->sectorCount())
                         .arg(sector->number())
                         .arg(sector->dataSize)
                         .arg(sector->isDataCRCValid() ? "valid" : "BAD")
                         .arg(sector->deleted ? ", deleted data mark" : "")
                         .arg(fdd->isWriteProtect() ? ". The disk is write-protected: edits are refused"
                                                    : ". Typed bytes go to the image (CRC recalculated)"));
    _shown = QByteArray(reinterpret_cast<const char*>(sector->data), sector->dataSize);
    _loading = true;
    StyleHexView(_hexView, static_cast<quint64>(_shown.size()));
    QHexDocument* document = QHexDocument::fromMemory<QMemoryBuffer>(_shown, _hexView);
    _hexView->setDocument(document);
    connect(document, &QHexDocument::changed, this, &DiskSectorDialog::documentChanged);
    _loading = false;
    _pages->setCurrentWidget(_hexView);
}

void DiskSectorDialog::documentChanged()
{
    QHexDocument* document = _hexView->getDocument();
    if (_loading || !document || !_emulator)
        return;
    if (RemoteControlGuard::Refuse(this))  // an automation session holds the machine
        return;
    const QByteArray now = document->read(0, static_cast<int>(_shown.size()));
    if (document->length() != _shown.size() || now.size() != _shown.size())
    {
        load();  // only overwriting is allowed: put the sector back
        return;
    }
    for (int i = 0; i < now.size(); ++i)
    {
        if (now[i] == _shown[i])
            continue;
        const SectorWrite::Result result =
            SectorWrite::Write(_emulator.get(), static_cast<uint8_t>(_drive->currentData().toInt()),
                               _cylinder->value(), _side->value(), _sector->currentData().toInt(),
                               static_cast<uint32_t>(i), {static_cast<uint8_t>(now[i])}, "qt");
        if (!result.ok)
        {
            load();
            _status->setText(QString::fromStdString("Not written: " + result.error));
            return;
        }
        _shown[i] = now[i];
    }
}
