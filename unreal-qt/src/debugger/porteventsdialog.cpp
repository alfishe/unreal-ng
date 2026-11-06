#include "porteventsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
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
    ColDevice,
    ColWhat,
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
    resize(980, 640);

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
    _recording = new QLabel(this);
    _recording->setWordWrap(true);
    _recordButton = new QPushButton(this);
    QHBoxLayout* recordingRow = new QHBoxLayout();
    recordingRow->addWidget(_recording, 1);
    recordingRow->addWidget(_recordButton);

    QPushButton* browse = new QPushButton(tr("..."), this);
    QPushButton* searchButton = new QPushButton(tr("Search"), this);
    searchButton->setDefault(true);
    _exportButton = new QPushButton(tr("Export CSV..."), this);
    _exportButton->setEnabled(false);
    _exportButton->setToolTip(tr("Save the listed hits to a .csv file"));

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
    buttons->addWidget(_exportButton);
    buttons->addWidget(searchButton);
    form->addLayout(buttons, row, 0, 1, 4);

    _status = new QLabel(this);
    _status->setWordWrap(true);
    _hits = new QTableWidget(this);
    _hits->setColumnCount(ColumnCount);
    _hits->setHorizontalHeaderLabels({tr("Frame"), tr("T"), tr("PC"), tr("Port"), tr("Value"), tr("Device"), tr("What")});
    _hits->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    _hits->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _hits->setSelectionBehavior(QAbstractItemView::SelectRows);
    _hits->verticalHeader()->hide();
    _hits->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    _hits->horizontalHeader()->setStretchLastSection(true);
    _hits->horizontalHeader()->setMaximumSectionSize(320);  // a long device name stays readable, What gets the rest
    _hits->verticalHeader()->setDefaultSectionSize(QFontMetrics(_hits->font()).height() + 6);
    _hits->setWordWrap(false);
    _hits->setToolTip(tr("Double click: seek there and show the PC in the disassembly"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(recordingRow);
    layout->addLayout(form);
    layout->addWidget(_status);
    layout->addWidget(_hits, 1);

    connect(searchButton, &QPushButton::clicked, this, &PortEventsDialog::search);
    connect(browse, &QPushButton::clicked, this, &PortEventsDialog::browseFile);
    connect(_exportButton, &QPushButton::clicked, this, &PortEventsDialog::exportCsvToFile);
    connect(_hits, &QTableWidget::cellDoubleClicked, this, [this](int r, int) { seekToRow(r); });
    connect(_recordButton, &QPushButton::clicked, this, &PortEventsDialog::toggleRecording);
    QTimer* poll = new QTimer(this);
    connect(poll, &QTimer::timeout, this, [this]() {
        if (isVisible())
            updateRecording();
    });
    poll->start(1000);
    updateRecording();
}

void PortEventsDialog::updateRecording()
{
    const ttd::TTDReply reply = ttd::TTDControl(context()).Execute({"status", {}});
    const StateNode& body = reply.body;
    const StateNode* available = body.find("ttd_available");
    if (!_emulator || !reply.Ok() || !available || !available->b)
    {
        _recording->setText(reply.message.empty() ? tr("Time travel is not available on this machine")
                                                  : QString::fromStdString(reply.message));
        _recordButton->setText(tr("Start recording"));
        _recordButton->setEnabled(false);
        return;
    }
    const StateNode* state = body.find("state");
    const StateNode* paused = body.find("recording_paused");
    const bool recording = (state && state->s == "recording") || (paused && paused->b);
    const StateNode* active = body.find("port_journal_active");
    const StateNode* off = body.find("port_journal_off_reason");
    const QString counts = tr("%1 reads, %2 writes journaled")
                               .arg(IntOf(body, "port_read_count", 0))
                               .arg(IntOf(body, "port_write_count", 0));
    if (recording)
        _recording->setText(active && active->b ? tr("Recording: %1. Run the program, then search.").arg(counts)
                                                : tr("Recording without port journals: %1")
                                                      .arg(off && !off->s.empty() ? QString::fromStdString(off->s)
                                                                                  : tr("this machine has none")));
    else if (IntOf(body, "checkpoint_count", 0) > 0)
        _recording->setText(tr("A recorded session (stopped): %1").arg(counts));
    else
        _recording->setText(tr("Not recording. Port events are searched in a time-travel recording's port journals: "
                               "start one, run the program, then search."));
    _recordButton->setText(recording ? tr("Stop recording") : tr("Start recording"));
    _recordButton->setEnabled(true);
}

void PortEventsDialog::toggleRecording()
{
    if (!_emulator || RemoteControlGuard::Refuse(this))  // an automation session holds the machine
        return;
    const bool recording = _recordButton->text() == tr("Stop recording");
    const ttd::TTDReply reply = ttd::TTDControl(context()).Execute({recording ? "stop" : "start", {}});
    updateRecording();
    if (!reply.Ok())
        _status->setText(QString::fromStdString(reply.message));
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
    _lastReply = StateNode();
    _exportButton->setEnabled(false);
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
    _lastReply = reply.body;
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
    _exportButton->setEnabled(!hits->items.empty());
    for (int r = 0; r < static_cast<int>(hits->items.size()); r++)
    {
        const StateNode& hit = hits->items[static_cast<size_t>(r)];
        // The access in words (the core describes it for every surface); the device's tags in its tooltip
        const StateNode* device = hit.find("device");
        const StateNode* what = hit.find("what");
        QStringList tags;
        if (const StateNode* list = hit.find("tags"))
            for (const StateNode& tag : list->items)
                tags << QString::fromStdString(tag.s);
        const QString cells[ColumnCount] = {QString::number(IntOf(hit, "frame", 0)),
                                            QString::number(IntOf(hit, "tinframe", 0)),
                                            Hex(IntOf(hit, "pc", 0), 4),
                                            Hex(IntOf(hit, "port", 0), 4),
                                            Hex(IntOf(hit, "value", 0), 2),
                                            device && device->kind == StateNode::Kind::String
                                                ? QString::fromStdString(device->s)
                                                : QString(),
                                            what ? QString::fromStdString(what->s) : QString()};
        for (int c = 0; c < ColumnCount; c++)
        {
            auto* item = new QTableWidgetItem(cells[c]);
            if (c == ColDevice && !tags.isEmpty())
                item->setToolTip(tr("Tags: %1").arg(tags.join(QStringLiteral(", "))));
            else if (c == ColWhat || c == ColDevice)
                item->setToolTip(cells[c]);
            _hits->setItem(r, c, item);
        }
    }
}

namespace
{
/// One CSV field: quoted when it holds a separator, a quote or a line break (RFC 4180)
QString CsvField(const QString& text)
{
    if (!text.contains(QLatin1Char(',')) && !text.contains(QLatin1Char('"')) && !text.contains(QLatin1Char('\n')) &&
        !text.contains(QLatin1Char('\r')))
        return text;
    QString quoted = text;
    quoted.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

QString StringOf(const StateNode& node, const char* key)
{
    const StateNode* found = node.find(key);
    return found && found->kind == StateNode::Kind::String ? QString::fromStdString(found->s) : QString();
}
}  // namespace

QString PortEventsDialog::hitsCsv() const
{
    // Addresses and values in hex with 0x (a spreadsheet keeps them as text, not as decimal-looking numbers);
    // frame, T and the AY register in decimal
    QString csv = QStringLiteral("event,direction,index,frame,t_in_frame,pc,port,value,ay_register,device,tags,what\r\n");
    const StateNode* hits = _lastReply.find("hits");
    if (!hits)
        return csv;
    const QString event = CsvField(StringOf(_lastReply, "event"));
    const QString direction = CsvField(StringOf(_lastReply, "direction"));
    for (const StateNode& hit : hits->items)
    {
        QStringList tags;
        if (const StateNode* list = hit.find("tags"))
            for (const StateNode& tag : list->items)
                tags << QString::fromStdString(tag.s);
        const int64_t ayRegister = IntOf(hit, "ay_register");
        const QStringList fields = {event,
                                    direction,
                                    QString::number(IntOf(hit, "index", 0)),
                                    QString::number(IntOf(hit, "frame", 0)),
                                    QString::number(IntOf(hit, "tinframe", 0)),
                                    QStringLiteral("0x") + Hex(IntOf(hit, "pc", 0), 4),
                                    QStringLiteral("0x") + Hex(IntOf(hit, "port", 0), 4),
                                    QStringLiteral("0x") + Hex(IntOf(hit, "value", 0), 2),
                                    ayRegister >= 0 ? QString::number(ayRegister) : QString(),
                                    CsvField(StringOf(hit, "device")),
                                    CsvField(tags.join(QLatin1Char(';'))),
                                    CsvField(StringOf(hit, "what"))};
        csv += fields.join(QLatin1Char(',')) + QStringLiteral("\r\n");
    }
    return csv;
}

bool PortEventsDialog::exportCsv(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        _status->setText(tr("Cannot write %1: %2").arg(path, file.errorString()));
        return false;
    }
    const QByteArray bytes = hitsCsv().toUtf8();
    if (file.write(bytes) != bytes.size())
    {
        _status->setText(tr("Cannot write %1: %2").arg(path, file.errorString()));
        return false;
    }
    const StateNode* hits = _lastReply.find("hits");
    _status->setText(tr("%1 hit(s) exported to %2").arg(static_cast<qulonglong>(hits ? hits->items.size() : 0)).arg(path));
    return true;
}

void PortEventsDialog::exportCsvToFile()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Export port events"),
                                                QStringLiteral("port-events-%1.csv").arg(_event->currentText()),
                                                tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".csv");
    exportCsv(path);
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
