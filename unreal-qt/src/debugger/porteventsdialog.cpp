#include "porteventsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdportsearch.h"
#include "emulator/emulator.h"
#include "emulator/remotecontrolguard.h"

namespace
{
enum Column
{
    ColFrame,
    ColT,
    ColPc,
    ColPort,
    ColValue,
    ColAy,
    ColKeys,
    ColumnCount
};

QString Hex(int64_t value, int digits)
{
    return QStringLiteral("%1").arg(static_cast<qulonglong>(value), digits, 16, QLatin1Char('0')).toUpper();
}

int64_t IntOf(const StateNode& node, const char* key, int64_t fallback = -1)
{
    const StateNode* found = node.find(key);
    return found ? found->i : fallback;
}
}  // namespace

PortEventsDialog::PortEventsDialog(std::shared_ptr<Emulator> emulator, QWidget* parent)
    : QDialog(parent), _emulator(emulator)
{
    setWindowTitle("Port events");
    resize(760, 600);

    _event = new QComboBox(this);
    for (const std::string& name : ttd::PortEventNames())
        _event->addItem(QString::fromStdString(name));
    _arg = new QLineEdit(this);
    _arg->setPlaceholderText(tr("key: a, enter, space ...; ay-*: a register 0-15"));
    _from = new QLineEdit(this);
    _from->setPlaceholderText(tr("frame[:t]"));
    _to = new QLineEdit(this);
    _to->setPlaceholderText(tr("frame[:t]"));
    _limit = new QSpinBox(this);
    _limit->setRange(1, 100000);
    _limit->setValue(100);
    _newest = new QCheckBox(tr("Newest first"), this);
    _trigger = new QComboBox(this);
    _trigger->addItem(tr("(the event's)"), QString());
    _trigger->addItem(QStringLiteral("every"), QStringLiteral("every"));
    _trigger->addItem(QStringLiteral("rising"), QStringLiteral("rising"));
    _trigger->addItem(QStringLiteral("change"), QStringLiteral("change"));
    _port = new QLineEdit(this);
    _port->setPlaceholderText(tr("#FE"));
    _portMask = new QLineEdit(this);
    _portMask->setPlaceholderText(tr("#FFFF"));
    _value = new QLineEdit(this);
    _value->setPlaceholderText(tr("#10"));
    _valueMask = new QLineEdit(this);
    _valueMask->setPlaceholderText(tr("#FF"));
    _file = new QLineEdit(this);
    _file->setPlaceholderText(tr("empty: this session; a .ttd file: searched without loading it"));
    QPushButton* browse = new QPushButton(tr("..."), this);
    QPushButton* searchButton = new QPushButton(tr("Search"), this);
    searchButton->setDefault(true);

    QGridLayout* form = new QGridLayout();
    int row = 0;
    form->addWidget(new QLabel(tr("Event"), this), row, 0);
    form->addWidget(_event, row, 1);
    form->addWidget(new QLabel(tr("Argument"), this), row, 2);
    form->addWidget(_arg, row, 3);
    row++;
    form->addWidget(new QLabel(tr("From"), this), row, 0);
    form->addWidget(_from, row, 1);
    form->addWidget(new QLabel(tr("To"), this), row, 2);
    form->addWidget(_to, row, 3);
    row++;
    form->addWidget(new QLabel(tr("Limit"), this), row, 0);
    form->addWidget(_limit, row, 1);
    form->addWidget(new QLabel(tr("Trigger"), this), row, 2);
    form->addWidget(_trigger, row, 3);
    row++;
    form->addWidget(new QLabel(tr("Port"), this), row, 0);
    form->addWidget(_port, row, 1);
    form->addWidget(new QLabel(tr("Port mask"), this), row, 2);
    form->addWidget(_portMask, row, 3);
    row++;
    form->addWidget(new QLabel(tr("Value"), this), row, 0);
    form->addWidget(_value, row, 1);
    form->addWidget(new QLabel(tr("Value mask"), this), row, 2);
    form->addWidget(_valueMask, row, 3);
    row++;
    QHBoxLayout* fileRow = new QHBoxLayout();
    fileRow->addWidget(_file, 1);
    fileRow->addWidget(browse);
    form->addWidget(new QLabel(tr("File"), this), row, 0);
    form->addLayout(fileRow, row, 1, 1, 3);
    row++;
    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->addWidget(_newest);
    buttons->addStretch(1);
    buttons->addWidget(searchButton);
    form->addLayout(buttons, row, 0, 1, 4);

    _status = new QLabel(this);
    _status->setWordWrap(true);
    _hits = new QTableWidget(this);
    _hits->setColumnCount(ColumnCount);
    _hits->setHorizontalHeaderLabels({tr("Frame"), tr("T"), tr("PC"), tr("Port"), tr("Value"), tr("AY reg"), tr("Keys")});
    _hits->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    _hits->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _hits->setSelectionBehavior(QAbstractItemView::SelectRows);
    _hits->verticalHeader()->hide();
    _hits->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    _hits->horizontalHeader()->setStretchLastSection(true);
    _hits->setToolTip(tr("Double click: seek there and show the PC in the disassembly"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(_status);
    layout->addWidget(_hits, 1);

    connect(searchButton, &QPushButton::clicked, this, &PortEventsDialog::search);
    connect(browse, &QPushButton::clicked, this, &PortEventsDialog::browseFile);
    connect(_hits, &QTableWidget::cellDoubleClicked, this, [this](int r, int) { seekToRow(r); });
}

EmulatorContext* PortEventsDialog::context() const
{
    return _emulator ? _emulator->GetContext() : nullptr;  // lifetime-ok: the dialog holds the shared_ptr
}

void PortEventsDialog::browseFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Time travel file"), _file->text(),
                                                      tr("Time travel sessions (*.ttd);;All files (*)"));
    if (!path.isEmpty())
        _file->setText(path);
}

void PortEventsDialog::search()
{
    _hits->setRowCount(0);
    if (!_emulator)
        return;
    ttd::TTDRequest request{"port-events", {}};
    auto& options = request.options;
    options["event"] = _event->currentText().toStdString();
    options["context"] = "false";  // the hit list only; a seek shows the machine
    options["limit"] = std::to_string(_limit->value());
    if (_newest->isChecked())
        options["newest"] = "true";
    const std::pair<QLineEdit*, const char*> fields[] = {{_arg, "arg"},       {_from, "from"},
                                                         {_to, "to"},         {_port, "port"},
                                                         {_portMask, "port_mask"}, {_value, "value"},
                                                         {_valueMask, "value_mask"}, {_file, "file"}};
    for (const auto& [edit, name] : fields)
        if (!edit->text().trimmed().isEmpty())
            options[name] = edit->text().trimmed().toStdString();
    if (const QString trigger = _trigger->currentData().toString(); !trigger.isEmpty())
        options["trigger"] = trigger.toStdString();

    const ttd::TTDReply reply = ttd::TTDControl(context()).Execute(request);
    if (!reply.Ok())
    {
        _status->setText(QString::fromStdString(reply.message));
        return;
    }
    _hitsFromFile = options.count("file") != 0;
    const StateNode* hits = reply.body.find("hits");
    const int64_t count = IntOf(reply.body, "count", 0);
    _status->setText(tr("%1 hit(s) of %2 records scanned%3%4")
                         .arg(count)
                         .arg(IntOf(reply.body, "scanned", 0))
                         .arg(reply.body.find("truncated") && reply.body.find("truncated")->b
                                  ? tr(", more past the limit")
                                  : QString())
                         .arg(_hitsFromFile ? tr(" (a file: no seek)") : QString()));
    if (!hits)
        return;
    _hits->setRowCount(static_cast<int>(hits->items.size()));
    for (int r = 0; r < static_cast<int>(hits->items.size()); r++)
    {
        const StateNode& hit = hits->items[static_cast<size_t>(r)];
        QStringList keys;
        if (const StateNode* list = hit.find("keys"))
            for (const StateNode& key : list->items)
                keys << QString::fromStdString(key.s);
        const int64_t ay = IntOf(hit, "ay_register");
        const QString cells[ColumnCount] = {QString::number(IntOf(hit, "frame", 0)),
                                            QString::number(IntOf(hit, "tinframe", 0)),
                                            Hex(IntOf(hit, "pc", 0), 4),
                                            Hex(IntOf(hit, "port", 0), 4),
                                            Hex(IntOf(hit, "value", 0), 2),
                                            ay >= 0 ? QString::number(ay) : QString(),
                                            keys.join(QLatin1Char(' '))};
        for (int c = 0; c < ColumnCount; c++)
            _hits->setItem(r, c, new QTableWidgetItem(cells[c]));
    }
}

void PortEventsDialog::seekToRow(int row)
{
    if (!_emulator || _hitsFromFile || row < 0 || row >= _hits->rowCount())
        return;
    if (RemoteControlGuard::Refuse(this))  // an automation session holds the machine
        return;
    const std::string frame = _hits->item(row, ColFrame)->text().toStdString();
    const std::string t = _hits->item(row, ColT)->text().toStdString();
    const ttd::TTDReply reply =
        ttd::TTDControl(context()).Execute({"seek", {{"frame", frame}, {"tinframe", t}}});
    if (!reply.Ok())
    {
        _status->setText(QString::fromStdString(reply.message));
        return;
    }
    if (const StateNode* reached = reply.body.find("reached"); reached && !reached->b)
    {
        const StateNode* message = reply.body.find("message");
        _status->setText(message ? QString::fromStdString(message->s) : tr("The seek did not reach the hit"));
        return;
    }
    _status->setText(tr("At frame %1, T %2").arg(QString::fromStdString(frame), QString::fromStdString(t)));
    emit seekedTo(static_cast<uint16_t>(_hits->item(row, ColPc)->text().toUInt(nullptr, 16)));
}
