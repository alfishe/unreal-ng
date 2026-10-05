#include "devicememorydialog.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <string>
#include <vector>

#include "QHexView/model/buffer/qmemorybuffer.h"
#include "QHexView/model/qhexcursor.h"
#include "QHexView/model/qhexdocument.h"
#include "QHexView/qhexview.h"
#include "emulator/emulator.h"
#include "emulator/memory/devicememory.h"

DeviceMemoryDialog::DeviceMemoryDialog(Emulator* emulator, QWidget* parent) : QDialog(parent), _emulator(emulator)
{
    setWindowTitle("Device memory");
    resize(720, 480);

    _regions = new QComboBox(this);
    QPushButton* refresh = new QPushButton("Refresh", this);
    QHBoxLayout* top = new QHBoxLayout();
    top->addWidget(new QLabel("Region:", this));
    top->addWidget(_regions, 1);
    top->addWidget(refresh);

    _description = new QLabel(this);
    _description->setWordWrap(true);
    _hexView = new QHexView(this);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(_description);
    layout->addWidget(_hexView, 1);

    if (_emulator)
        for (IDeviceMemoryRegion* region : DeviceMemory::Regions(_emulator->GetContext()))
            _regions->addItem(QString("%1 (%2 bytes)").arg(region->Name()).arg(region->Size()), QString(region->Name()));
    if (_regions->count() == 0)
    {
        _description->setText("This machine has no device memory regions.");
        _hexView->setEnabled(false);
        refresh->setEnabled(false);
    }

    connect(_regions, qOverload<int>(&QComboBox::currentIndexChanged), this, &DeviceMemoryDialog::regionSelected);
    connect(refresh, &QPushButton::clicked, this, &DeviceMemoryDialog::reload);
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
    if (index < 0 || !_emulator)
        return;
    const std::string name = _regions->itemData(index).toString().toStdString();
    if (IDeviceMemoryRegion* region = DeviceMemory::Find(_emulator->GetContext(), name))
        _description->setText(QString("%1\nWrites: %2").arg(region->Description(), region->WritePath()));
    reload();
}

void DeviceMemoryDialog::reload()
{
    if (!_emulator || _regions->currentIndex() < 0)
        return;
    const std::string name = _regions->currentData().toString().toStdString();
    IDeviceMemoryRegion* region = DeviceMemory::Find(_emulator->GetContext(), name);
    if (!region)
        return;
    std::vector<uint8_t> bytes;
    std::string error;
    if (!DeviceMemory::Read(_emulator->GetContext(), name, 0, region->Size(), bytes, error))
    {
        _description->setText(QString::fromStdString(error));
        return;
    }
    _shown = QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()));
    _loading = true;
    QHexDocument* document = QHexDocument::fromMemory<QMemoryBuffer>(_shown, _hexView);
    _hexView->setDocument(document);
    connect(document, &QHexDocument::changed, this, &DeviceMemoryDialog::documentChanged);
    _loading = false;
}

void DeviceMemoryDialog::documentChanged()
{
    QHexDocument* document = _hexView->getDocument();
    if (_loading || !document || !_emulator)
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
        if (!DeviceMemory::Write(_emulator->GetContext(), name, static_cast<uint32_t>(i),
                                 {static_cast<uint8_t>(now[i])}, "qt", error))
        {
            QMessageBox::warning(this, "Device memory", QString::fromStdString(error));
            reload();
            return;
        }
        _shown[i] = now[i];
    }
}
