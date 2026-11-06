#include "devicememorydialog.h"
#include "emulator/remotecontrolguard.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <initializer_list>
#include <string>
#include <vector>

#include "QHexView/model/buffer/qmemorybuffer.h"
#include "QHexView/model/qhexcursor.h"
#include "QHexView/model/qhexdocument.h"
#include "QHexView/qhexview.h"
#include "debugger/hexviewstyle.h"
#include "emulator/emulator.h"
#include "emulator/memory/devicememory.h"

namespace
{
QString SizeText(uint32_t size)
{
    if (size >= 1024 && size % 1024 == 0)
        return QString("%1 KB").arg(size / 1024);
    return QString("%1 bytes").arg(size);
}
}  // namespace

bool DeviceMemoryDialog::HasRegions(Emulator* emulator)
{
    return emulator && !DeviceMemory::Regions(emulator->GetContext()).empty();  // lifetime-ok: the caller holds it
}

EmulatorContext* DeviceMemoryDialog::context() const
{
    return _emulator ? _emulator->GetContext() : nullptr;  // lifetime-ok: the dialog holds the shared_ptr
}

DeviceMemoryDialog::DeviceMemoryDialog(std::shared_ptr<Emulator> emulator, QWidget* parent) : QDialog(parent), _emulator(emulator)
{
    setWindowTitle("Device memory");
    resize(760, 520);

    _regions = new QComboBox(this);
    _regions->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    _refresh = new QPushButton("Refresh", this);
    _refresh->setToolTip("Read the region again (the machine may have changed it)");
    QHBoxLayout* top = new QHBoxLayout();
    top->addWidget(new QLabel("Region:", this));
    top->addWidget(_regions);
    top->addStretch(1);
    top->addWidget(_refresh);

    _description = new QLabel(this);
    _description->setWordWrap(true);
    _description->setTextInteractionFlags(Qt::TextSelectableByMouse);

    _hexView = new QHexView(this);
    _empty = MakeEmptyNote(this);
    _pages = new QStackedWidget(this);
    _pages->addWidget(_empty);
    _pages->addWidget(_hexView);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(_description);
    layout->addWidget(_pages, 1);

    if (EmulatorContext* machine = context())
        for (IDeviceMemoryRegion* region : DeviceMemory::Regions(machine))
            _regions->addItem(QString("%1  -  %2%3")
                                  .arg(region->Name(), SizeText(region->Size()),
                                       region->Writable() ? "" : ", read only"),
                              QString(region->Name()));
    if (_regions->count() == 0)
    {
        for (QWidget* control : std::initializer_list<QWidget*>{_regions, _refresh, _description})
            control->setEnabled(false);
        _empty->setText("This machine has no device memory.\n\nDevice memory is memory a device owns outside the "
                        "CPU's RAM and ROM pages: the Sprinter's video RAM, the TS-Conf palette and sprite table, a "
                        "General Sound or MoonSound card's RAM.");
        _pages->setCurrentWidget(_empty);
    }

    connect(_regions, qOverload<int>(&QComboBox::currentIndexChanged), this, &DeviceMemoryDialog::regionSelected);
    connect(_refresh, &QPushButton::clicked, this, &DeviceMemoryDialog::reload);
    // A region has a fixed size: typing overwrites, never inserts
    connect(_hexView->hexCursor(), &QHexCursor::modeChanged, this, [this]() {
        if (_hexView->hexCursor()->mode() != QHexCursor::Mode::Overwrite)
            _hexView->hexCursor()->setMode(QHexCursor::Mode::Overwrite);
    });
    if (_regions->count() > 0)
        regionSelected(0);
}

void DeviceMemoryDialog::regionSelected(int index)
{
    if (index < 0 || !context())
        return;
    const std::string name = _regions->itemData(index).toString().toStdString();
    if (IDeviceMemoryRegion* region = DeviceMemory::Find(context(), name))
    {
        _description->setText(QString("%1\n%2: %3")
                                  .arg(region->Description(), region->Writable() ? "Typed bytes go through" : "Read only",
                                       region->WritePath()));
        StyleHexView(_hexView, region->Size());
    }
    _hexView->gotoOffset(0);
    reload();
}

void DeviceMemoryDialog::reload()
{
    if (!context() || _regions->currentIndex() < 0)
        return;
    const std::string name = _regions->currentData().toString().toStdString();
    IDeviceMemoryRegion* region = DeviceMemory::Find(context(), name);
    if (!region)
        return;
    std::vector<uint8_t> bytes;
    std::string error;
    if (!DeviceMemory::Read(context(), name, 0, region->Size(), bytes, error))
    {
        _empty->setText(QString::fromStdString(error));
        _pages->setCurrentWidget(_empty);
        return;
    }
    const quint64 at = _hexView->getDocument() ? _hexView->offset() : 0;  // a refresh keeps the place
    _shown = QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()));
    _loading = true;
    QHexDocument* document = QHexDocument::fromMemory<QMemoryBuffer>(_shown, _hexView);
    _hexView->setDocument(document);
    _hexView->setReadOnly(!region->Writable());   // a time-travel view the device keeps itself (VDAC2)
    connect(document, &QHexDocument::changed, this, &DeviceMemoryDialog::documentChanged);
    _loading = false;
    if (at < static_cast<quint64>(_shown.size()))
        _hexView->gotoOffset(at);
    _pages->setCurrentWidget(_hexView);
}

void DeviceMemoryDialog::documentChanged()
{
    QHexDocument* document = _hexView->getDocument();
    if (_loading || !document || !context())
        return;
    if (RemoteControlGuard::Refuse(this))  // an automation session holds the machine
        return;
    const QByteArray now = document->read(0, static_cast<int>(_shown.size()));
    if (document->length() != _shown.size() || now.size() != _shown.size())
    {
        reload();  // only overwriting is allowed: put the region back
        return;
    }
    const std::string name = _regions->currentData().toString().toStdString();
    for (int i = 0; i < now.size(); ++i)
    {
        if (now[i] == _shown[i])
            continue;
        std::string error;
        if (!DeviceMemory::Write(context(), name, static_cast<uint32_t>(i), {static_cast<uint8_t>(now[i])}, "qt", error))
        {
            QMessageBox::warning(this, "Device memory", QString::fromStdString(error));
            reload();
            return;
        }
        _shown[i] = now[i];
    }
}
