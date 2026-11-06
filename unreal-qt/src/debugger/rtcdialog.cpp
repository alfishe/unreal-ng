#include "rtcdialog.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <string>
#include <vector>

#include "emulator/emulator.h"
#include "emulator/io/rtc/rtcaccess.h"
#include "emulator/remotecontrolguard.h"
#include "emulator/state/devicestate.h"

namespace
{
QString Hex2(int64_t value)
{
    return QStringLiteral("%1").arg(static_cast<unsigned>(value) & 0xFFu, 2, 16, QLatin1Char('0')).toUpper();
}

int64_t IntOf(const StateNode& node, const char* key)
{
    const StateNode* found = node.find(key);
    return found ? found->i : 0;
}

bool BoolOf(const StateNode& node, const char* key)
{
    const StateNode* found = node.find(key);
    return found && found->b;
}

QString TextOf(const StateNode& node, const char* key)
{
    const StateNode* found = node.find(key);
    return found ? QString::fromStdString(found->s) : QString();
}

/// "name" when the bit is set, "-" otherwise, as a compact flag list
QString Flags(const StateNode* reg, std::initializer_list<std::pair<const char*, const char*>> bits)
{
    QStringList parts;
    for (const auto& [key, name] : bits)
        if (reg && BoolOf(*reg, key))
            parts << QString::fromLatin1(name);
    return parts.isEmpty() ? QStringLiteral("-") : parts.join(QLatin1Char(' '));
}
}  // namespace

RtcDialog::RtcDialog(std::shared_ptr<Emulator> emulator, QWidget* parent) : QDialog(parent), _emulator(emulator)
{
    setWindowTitle("CMOS clock");
    resize(560, 520);

    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    _summary = new QLabel(this);
    _summary->setFont(fixed);
    _summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _status = new QLabel(this);
    _status->setWordWrap(true);

    _cells = new QTableWidget(this);
    _cells->setFont(fixed);
    _cells->setColumnCount(16);
    QStringList columns;
    for (int c = 0; c < 16; c++)
        columns << QStringLiteral("%1").arg(c, 0, 16).toUpper();
    _cells->setHorizontalHeaderLabels(columns);
    _cells->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    _cells->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    _cells->setToolTip(tr("Every cell as the guest reads it (00-0D: time, alarm, registers A-D; then RAM). "
                          "Double click a cell to write it like a guest OUT: the time registers set the clock, "
                          "C and D are read only"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(_summary);
    layout->addWidget(_cells, 1);
    layout->addWidget(_status);

    connect(_cells, &QTableWidget::itemChanged, this, &RtcDialog::cellEdited);
    _timer = new QTimer(this);
    _timer->setInterval(500);
    connect(_timer, &QTimer::timeout, this, &RtcDialog::refresh);
    _timer->start();
    refresh();
}

EmulatorContext* RtcDialog::context() const
{
    return _emulator ? _emulator->GetContext() : nullptr;  // lifetime-ok: the dialog holds the shared_ptr
}

void RtcDialog::refresh()
{
    if (!_emulator)
        return;
    const StateNode report = DeviceState::Rtc(context());
    if (!BoolOf(report, "available"))
    {
        const QString reason = TextOf(report, "description");
        _summary->setText(reason.isEmpty() ? tr("This machine has no CMOS clock") : reason);
        _cells->setRowCount(0);
        _timer->stop();
        return;
    }

    const StateNode* time = report.find("time");
    const StateNode* regA = report.find("register_a");
    const StateNode* regB = report.find("register_b");
    const StateNode* regC = report.find("register_c");
    const StateNode* regD = report.find("register_d");
    const StateNode* alarm = report.find("alarm");
    QStringList lines;
    lines << tr("%1 at ports %2, %3 cells, time %4")
                 .arg(TextOf(report, "chip"), TextOf(report, "ports"))
                 .arg(IntOf(report, "cells"))
                 .arg(TextOf(report, "time_mode"));
    lines << tr("Time  %1  (day of week %2)")
                 .arg(time ? TextOf(*time, "text") : QString())
                 .arg(time ? IntOf(*time, "day_of_week") : 0);
    lines << tr("A %1  divider %2 rate %3%4")
                 .arg(Hex2(regA ? IntOf(*regA, "value") : 0))
                 .arg(regA ? IntOf(*regA, "divider") : 0)
                 .arg(regA ? IntOf(*regA, "rate") : 0)
                 .arg(regA && BoolOf(*regA, "uip") ? tr("  UIP") : QString());
    lines << tr("B %1  %2")
                 .arg(Hex2(regB ? IntOf(*regB, "value") : 0))
                 .arg(Flags(regB, {{"set", "SET"},
                                   {"periodic_irq", "PIE"},
                                   {"alarm_irq", "AIE"},
                                   {"update_irq", "UIE"},
                                   {"square_wave", "SQWE"},
                                   {"binary", "BIN"},
                                   {"hour_24", "24H"},
                                   {"daylight_saving", "DSE"}}));
    lines << tr("C %1  %2    D %3  %4")
                 .arg(Hex2(regC ? IntOf(*regC, "value") : 0))
                 .arg(Flags(regC, {{"irq", "IRQF"}, {"periodic", "PF"}, {"alarm", "AF"}, {"update_ended", "UF"}}))
                 .arg(Hex2(regD ? IntOf(*regD, "value") : 0))
                 .arg(regD && BoolOf(*regD, "battery_ok") ? tr("battery ok") : tr("battery low"));
    lines << tr("Alarm %1:%2:%3 (raw)    address latch %4")
                 .arg(Hex2(alarm ? IntOf(*alarm, "hours") : 0))
                 .arg(Hex2(alarm ? IntOf(*alarm, "minutes") : 0))
                 .arg(Hex2(alarm ? IntOf(*alarm, "seconds") : 0))
                 .arg(Hex2(IntOf(report, "address_latch")));
    const QString note = TextOf(report, "note");
    if (!note.isEmpty())
        lines << note;
    lines << tr("NVRAM file: %1").arg(TextOf(report, "nvram_file"));
    _summary->setText(lines.join(QLatin1Char('\n')));

    // The grid: the cells peeked (RtcAccess::Read never clears C); a cell being edited stays as typed
    const int count = static_cast<int>(IntOf(report, "cells"));
    std::vector<uint8_t> bytes;
    std::string error;
    if (!RtcAccess::Read(context(), 0, static_cast<unsigned>(count), bytes, error))
    {
        _status->setText(QString::fromStdString(error));
        return;
    }
    _filling = true;
    const int rows = (count + 15) / 16;
    if (_cells->rowCount() != rows)
    {
        _cells->setRowCount(rows);
        QStringList rowNames;
        for (int r = 0; r < rows; r++)
            rowNames << Hex2(r * 16);
        _cells->setVerticalHeaderLabels(rowNames);
    }
    for (int index = 0; index < static_cast<int>(bytes.size()); index++)
    {
        QTableWidgetItem* item = _cells->item(index / 16, index % 16);
        if (!item)
        {
            item = new QTableWidgetItem();
            item->setTextAlignment(Qt::AlignCenter);
            if (index == 0x0C || index == 0x0D)
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);  // C and D: read only on the chip
            _cells->setItem(index / 16, index % 16, item);
        }
        // The user is typing in this cell: an item editor lives on the viewport and holds the focus
        QWidget* focus = QApplication::focusWidget();
        if (_cells->currentItem() == item && focus && _cells->viewport()->isAncestorOf(focus))
            continue;
        const QString text = Hex2(bytes[static_cast<size_t>(index)]);
        if (item->text() != text)
            item->setText(text);
    }
    _filling = false;
}

void RtcDialog::cellEdited(QTableWidgetItem* item)
{
    if (_filling || !_emulator || !item)
        return;
    const int index = item->row() * 16 + item->column();
    bool ok = false;
    QString text = item->text().trimmed();
    if (text.startsWith(QLatin1Char('#')) || text.startsWith(QLatin1Char('$')))
        text = text.mid(1);
    else if (text.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
        text = text.mid(2);
    const uint value = text.toUInt(&ok, 16);
    if (!ok || value > 0xFF)
    {
        _status->setText(tr("Cell %1: '%2' is not a hex byte").arg(Hex2(index), item->text()));
        refresh();
        return;
    }
    if (RemoteControlGuard::Refuse(this))  // an automation session holds the machine
    {
        refresh();
        return;
    }
    std::string error;
    if (!RtcAccess::Write(context(), static_cast<unsigned>(index), {static_cast<uint8_t>(value)},
                          "qt rtc", error))
        _status->setText(tr("Cell %1 not written: %2").arg(Hex2(index), QString::fromStdString(error)));
    else
        _status->setText(tr("Cell %1 = %2").arg(Hex2(index), Hex2(value)));
    refresh();
}
