#pragma once

#include <cstdint>
#include <memory>

#include <QWidget>

class Emulator;
class QLabel;
class QTreeWidget;

/// The PC history pane of the debugger window (debugger additions tdd §7): the newest instructions the CPU started,
/// newest first, each with the page its 16K window showed and the instruction read from that page. It records
/// while it is shown (setRecording; PcHistory arms the CPU hook) and stops when hidden, so the emulation pays
/// nothing then. The window refreshes it on every stop; picking a row shows that address in the disassembly.
class PcHistoryPane : public QWidget
{
    Q_OBJECT

public:
    PcHistoryPane(std::shared_ptr<Emulator> emulator, QWidget* parent = nullptr);

    /// The machine the pane reads (the debugger's bound one, null when none); the list is emptied
    void setEmulator(std::shared_ptr<Emulator> emulator);
    /// Start (empty) or stop the recording
    void setRecording(bool on);
    bool isRecording() const { return _recording; }

public slots:
    /// The newest entries again (the machine stopped); while it runs the list waits for the next stop
    void refresh();

signals:
    void addressPicked(uint16_t address);

private:
    std::shared_ptr<Emulator> _emulator;
    bool _recording = false;
    QLabel* _status = nullptr;
    QTreeWidget* _list = nullptr;
};
