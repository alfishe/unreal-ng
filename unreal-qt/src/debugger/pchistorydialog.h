#pragma once

#include <memory>

#include <QDialog>

class Emulator;
class QLabel;
class QListWidget;

/// The PC history in the debugger (debugger additions tdd §7): the newest instructions the CPU started, each with
/// the page its 16K window showed, newest first. Opening it starts the recording (PcHistory::Report arms it);
/// "Stop" ends it, so the emulation pays nothing again.
class PcHistoryDialog : public QDialog
{
    Q_OBJECT

public:
    PcHistoryDialog(std::shared_ptr<Emulator> emulator, QWidget* parent = nullptr);

public slots:
    void refresh();

private slots:
    void stop();

private:
    std::shared_ptr<Emulator> _emulator;
    QLabel* _status = nullptr;
    QListWidget* _list = nullptr;
};
