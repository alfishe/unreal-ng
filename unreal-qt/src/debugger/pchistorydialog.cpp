#include "pchistorydialog.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "debugger/pchistory/pchistory.h"
#include "emulator/emulator.h"

PcHistoryDialog::PcHistoryDialog(Emulator* emulator, QWidget* parent) : QDialog(parent), _emulator(emulator)
{
    setWindowTitle("PC history");
    resize(260, 520);

    _status = new QLabel(this);
    _status->setWordWrap(true);
    _list = new QListWidget(this);
    _list->setFont(QFont("Menlo"));
    QPushButton* refreshButton = new QPushButton("Refresh", this);
    QPushButton* stopButton = new QPushButton("Stop", this);
    stopButton->setToolTip("Stop recording (the emulation pays nothing again); Refresh starts it anew");

    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->addWidget(refreshButton);
    buttons->addWidget(stopButton);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(_status);
    layout->addWidget(_list, 1);
    layout->addLayout(buttons);

    connect(refreshButton, &QPushButton::clicked, this, &PcHistoryDialog::refresh);
    connect(stopButton, &QPushButton::clicked, this, &PcHistoryDialog::stop);
    refresh();
}

void PcHistoryDialog::refresh()
{
    _list->clear();
    if (!_emulator)
        return;
    const PcHistory::Result result = PcHistory::Report(_emulator, 256);
    if (!result.error.empty())
    {
        _status->setText(QString::fromStdString(result.error));
        return;
    }
    const StateNode& report = result.report;
    const StateNode* entries = report.find("entries");
    _status->setText(report.find("started_now")->b
                         ? QString("Recording started: step or run, then Refresh")
                         : QString("%1 of %2 instructions, newest first")
                               .arg(entries ? entries->items.size() : 0)
                               .arg(static_cast<qulonglong>(report.find("total")->i)));
    if (!entries)
        return;
    for (const StateNode& entry : entries->items)
        _list->addItem(QString("%1  %2%3")
                           .arg(static_cast<int>(entry.find("address")->i), 4, 16, QChar('0'))
                           .toUpper()
                           .arg(QString::fromStdString(entry.find("kind")->s))
                           .arg(static_cast<int>(entry.find("page")->i)));
}

void PcHistoryDialog::stop()
{
    if (!_emulator)
        return;
    const std::string error = PcHistory::SetArmed(_emulator, false);
    _status->setText(error.empty() ? QString("Stopped (the entries above stay until the next start)")
                                   : QString::fromStdString(error));
}
