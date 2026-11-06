#include "portwatchdialog.h"

#include <QCheckBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <vector>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"

namespace
{
enum Column
{
    ColPort,
    ColMask,
    ColDevice,
    ColGate,
    ColValue,
    ColDecoded,
    ColTags,
    ColumnCount
};

QString Hex(uint32_t value, int digits)
{
    return QStringLiteral("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}
}  // namespace

PortWatchDialog::PortWatchDialog(std::shared_ptr<Emulator> emulator, QWidget* parent)
    : QDialog(parent), _emulator(emulator)
{
    setWindowTitle("Ports");
    resize(900, 520);

    _live = new QLabel(this);
    _live->setWordWrap(true);
    _latchedOnly = new QCheckBox(tr("Latched ports only"), this);
    _latchedOnly->setToolTip(tr("Only the ports whose last written value the machine keeps (paging, border ...)"));

    _table = new QTableWidget(this);
    _table->setColumnCount(ColumnCount);
    _table->setHorizontalHeaderLabels({tr("Port"), tr("Mask"), tr("Device"), tr("Gate"), tr("Value"), tr("Decoded"),
                                       tr("Tags")});
    _table->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->verticalHeader()->hide();
    _table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setStretchLastSection(true);

    QHBoxLayout* top = new QHBoxLayout();
    top->addWidget(_live, 1);
    top->addWidget(_latchedOnly);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(_table, 1);

    connect(_latchedOnly, &QCheckBox::toggled, this, &PortWatchDialog::refresh);
    _timer = new QTimer(this);
    _timer->setInterval(500);
    connect(_timer, &QTimer::timeout, this, &PortWatchDialog::refresh);
    _timer->start();
    refresh();
}

EmulatorContext* PortWatchDialog::context() const
{
    return _emulator ? _emulator->GetContext() : nullptr;  // lifetime-ok: the dialog holds the shared_ptr
}

void PortWatchDialog::refresh()
{
    EmulatorContext* machine = context();
    if (!machine || !machine->pPortDecoder)
    {
        _live->setText(tr("No machine"));
        _table->setRowCount(0);
        return;
    }
    PortDecoder* decoder = machine->pPortDecoder;
    const EmulatorState& state = machine->emulatorState;

    bool mouseDecoded = false;
    std::string mouseNote;
    decoder->GetMouseRoutingState(mouseDecoded, mouseNote);
    QString live = tr("TR-DOS ports %1, mouse ports %2")
                       .arg((state.flags & (CF_TRDOS | CF_DOSPORTS)) ? tr("on") : tr("off"))
                       .arg(mouseDecoded ? tr("decoded") : tr("not decoded"));
    if (!mouseNote.empty())
        live += QStringLiteral(" (%1)").arg(QString::fromStdString(mouseNote));
    _live->setText(live);

    const QBrush changed(QColor(255, 220, 90));
    std::vector<PortMapEntry> entries = decoder->getPortMapEntries();
    int row = 0;
    _table->setRowCount(static_cast<int>(entries.size()));
    for (const PortMapEntry& entry : entries)
    {
        const char* latchName = PagingLatchToString(entry.latch);
        if (_latchedOnly->isChecked() && !latchName)
            continue;
        QString value;
        QString decoded;
        bool highlight = false;
        if (latchName)
        {
            const uint32_t now = PortDecoder::ReadPagingLatch(entry.latch, state);
            value = Hex(now, now > 0xFF ? 4 : 2);
            QStringList fields;
            for (const DecodedLatchField& field :
                 DecodePagingLatch(entry.latch, now, machine->config.mem_model, machine->config.ramsize))
                fields << (field.isBool ? (field.boolValue ? QString::fromStdString(field.key) : QString())
                                        : QStringLiteral("%1=%2").arg(QString::fromStdString(field.key)).arg(field.intValue));
            fields.removeAll(QString());
            decoded = fields.join(QLatin1Char(' '));
            const std::string key = Hex(entry.port, 4).toStdString() + "/" + Hex(entry.mask, 4).toStdString() + "/" +
                                    (entry.device ? entry.device : "");
            auto [seen, inserted] = _seen.try_emplace(key, Seen{now, 0});
            if (!inserted && seen->second.value != now)
            {
                seen->second.value = now;
                seen->second.highlight = kHighlightReads;
            }
            else if (seen->second.highlight > 0)
                seen->second.highlight--;
            highlight = seen->second.highlight > 0;
        }
        QStringList tags;
        for (const std::string& tag : PortTagSetToStrings(entry.tags))
            tags << QString::fromStdString(tag);
        const QString cells[ColumnCount] = {Hex(entry.port, 4),
                                            Hex(entry.mask, 4),
                                            QString::fromLatin1(entry.device ? entry.device : ""),
                                            QString::fromLatin1(entry.gate ? entry.gate : ""),
                                            value,
                                            decoded,
                                            tags.join(QLatin1Char(' '))};
        for (int column = 0; column < ColumnCount; column++)
        {
            QTableWidgetItem* item = _table->item(row, column);
            if (!item)
            {
                item = new QTableWidgetItem();
                _table->setItem(row, column, item);
            }
            if (item->text() != cells[column])
                item->setText(cells[column]);
            item->setBackground(highlight && (column == ColValue || column == ColDecoded) ? changed : QBrush());
        }
        if (latchName)
            _table->item(row, ColValue)->setToolTip(tr("Latch %1").arg(QString::fromLatin1(latchName)));
        row++;
    }
    _table->setRowCount(row);
}
