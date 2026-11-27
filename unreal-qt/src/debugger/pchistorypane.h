#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <QWidget>

class Emulator;
class QLabel;
class QTimer;
class QTreeWidget;

/// The PC history pane of the debugger window (debugger additions tdd §7): the newest instructions the CPU started,
/// newest first, each with the page its 16K window showed and the instruction read from that page. It records
/// while it is shown (setRecording; PcHistory arms the CPU hook) and stops when hidden, so the emulation pays
/// nothing then. The window refreshes it on every stop; picking a row shows that address in the disassembly.
/// A refresh never waits on the emulation thread: busy (the machine stopping, another client holding it) the list
/// stays and is read again a moment later; only the new rows are made (the older ones move down), the columns have
/// fixed widths (no measuring of 1024 rows per stop).
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

    /// Reads that found the machine busy and were tried again (tests)
    int busyReads() const { return _busyReads; }
    /// Rows made by the refreshes so far (tests: a step makes only its new rows)
    int rowsMade() const { return _rowsMade; }

public slots:
    /// The newest entries again (the machine stopped); while it runs the list waits for the next stop
    void refresh();

signals:
    void addressPicked(uint16_t address);

protected:
    void changeEvent(QEvent* event) override;

private:
    /// An entry as the list shows it: a row is made again only when its entry is new
    struct Key
    {
        uint16_t address = 0;
        char kind = 0;
        uint8_t page = 0;
        bool operator==(const Key& other) const
        {
            return address == other.address && kind == other.kind && page == other.page;
        }
    };
    static constexpr int kRetryMs = 30;
    static constexpr int kRetries = 30;
    void read();
    void retryLater();
    void setStatus(const QString& text);
    void sizeColumns();
    void clearRows();

    std::vector<Key> _keys;     ///< the rows shown, newest first
    unsigned long long _total = 0;  ///< the history's total at the rows' read
    int _busyReads = 0;
    int _rowsMade = 0;
    int _retriesLeft = 0;
    QTimer* _retry = nullptr;
    std::shared_ptr<Emulator> _emulator;
    bool _recording = false;
    QLabel* _status = nullptr;
    QTreeWidget* _list = nullptr;
};
