#include "disksectordialog.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <string>

#include "QHexView/model/buffer/qmemorybuffer.h"
#include "QHexView/model/qhexcursor.h"
#include "QHexView/model/qhexdocument.h"
#include "QHexView/qhexview.h"
#include "debugger/media/sectorwrite.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"

DiskSectorDialog::DiskSectorDialog(Emulator* emulator, QWidget* parent) : QDialog(parent), _emulator(emulator)
{
    setWindowTitle("Disk sector");
    resize(640, 420);

    _drive = new QComboBox(this);
    _drive->addItems({"A", "B", "C", "D"});
    _cylinder = new QSpinBox(this);
    _cylinder->setRange(0, 255);
    _side = new QSpinBox(this);
    _side->setRange(0, 1);
    _sector = new QSpinBox(this);
    _sector->setRange(1, 255);
    _sector->setToolTip("Sector ID (the R byte of its address mark; TR-DOS 1-16)");
    QPushButton* loadButton = new QPushButton("Load", this);

    QHBoxLayout* top = new QHBoxLayout();
    top->addWidget(new QLabel("Drive", this));
    top->addWidget(_drive);
    top->addWidget(new QLabel("Cylinder", this));
    top->addWidget(_cylinder);
    top->addWidget(new QLabel("Side", this));
    top->addWidget(_side);
    top->addWidget(new QLabel("Sector ID", this));
    top->addWidget(_sector);
    top->addWidget(loadButton);
    top->addStretch(1);

    _status = new QLabel(this);
    _status->setWordWrap(true);
    _hexView = new QHexView(this);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(_status);
    layout->addWidget(_hexView, 1);

    connect(loadButton, &QPushButton::clicked, this, &DiskSectorDialog::load);
    // A sector has a fixed size: typing overwrites, never inserts
    connect(_hexView->hexCursor(), &QHexCursor::modeChanged, this, [this]() {
        if (_hexView->hexCursor()->mode() != QHexCursor::Mode::Overwrite)
            _hexView->hexCursor()->setMode(QHexCursor::Mode::Overwrite);
    });
    load();
}

void DiskSectorDialog::load()
{
    _shown.clear();
    _loading = true;
    _hexView->setDocument(QHexDocument::fromMemory<QMemoryBuffer>(QByteArray(), _hexView));
    _loading = false;
    _hexView->setEnabled(false);
    if (!_emulator || !_emulator->GetContext())
        return;

    FDD* fdd = _emulator->GetContext()->coreState.diskDrives[_drive->currentIndex()];
    DiskImage* image = fdd && fdd->isDiskInserted() ? fdd->getDiskImage() : nullptr;
    if (!image)
    {
        _status->setText(QString("No disk in drive %1").arg(_drive->currentText()));
        return;
    }
    DiskImage::Track* track = image->getTrackForCylinderAndSide(static_cast<uint8_t>(_cylinder->value()),
                                                                static_cast<uint8_t>(_side->value()));
    DiskImage::Sector* sector = track ? track->findSector(static_cast<uint8_t>(_sector->value())) : nullptr;
    if (!sector || !sector->hasData || !sector->data)
    {
        _status->setText(track ? "No sector with that ID (or it has no data field) on this track"
                               : "No track at that cylinder / side");
        return;
    }
    _shown = QByteArray(reinterpret_cast<const char*>(sector->data), sector->dataSize);
    _status->setText(QString("%1 bytes, data CRC %2%3. Typed bytes are written to the image (CRC recalculated).")
                         .arg(sector->dataSize)
                         .arg(sector->isDataCRCValid() ? "valid" : "BAD")
                         .arg(fdd->isWriteProtect() ? "; the disk is write-protected: edits are refused" : ""));
    _loading = true;
    QHexDocument* document = QHexDocument::fromMemory<QMemoryBuffer>(_shown, _hexView);
    _hexView->setDocument(document);
    connect(document, &QHexDocument::changed, this, &DiskSectorDialog::documentChanged);
    _loading = false;
    _hexView->setEnabled(true);
}

void DiskSectorDialog::documentChanged()
{
    QHexDocument* document = _hexView->getDocument();
    if (_loading || !document || !_emulator)
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
            SectorWrite::Write(_emulator, static_cast<uint8_t>(_drive->currentIndex()), _cylinder->value(),
                               _side->value(), _sector->value(), static_cast<uint32_t>(i),
                               {static_cast<uint8_t>(now[i])}, "qt");
        if (!result.ok)
        {
            _status->setText(QString::fromStdString("Not written: " + result.error));
            load();
            return;
        }
        _shown[i] = now[i];
    }
}
