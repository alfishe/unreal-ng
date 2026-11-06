#pragma once

#include <map>
#include <memory>
#include <string>

#include <QDialog>

class Emulator;
class EmulatorContext;
class QCheckBox;
class QLabel;
class QTableWidget;
class QTimer;

/// The machine's ports in the debugger (debugger enhancements TODO #4, port watch): every row of the port decoder's
/// registry (PortDecoder::getPortMapEntries, the source of WebAPI /ports, MCP, CLI) with its device, gate and tags;
/// a latched port shows its value now and decoded (ReadPagingLatch / DecodePagingLatch), a value that changed since
/// the last read is highlighted for a few reads. The routing state that turns rows on and off (TR-DOS, the mouse)
/// is shown above
class PortWatchDialog : public QDialog
{
    Q_OBJECT

public:
    PortWatchDialog(std::shared_ptr<Emulator> emulator, QWidget* parent = nullptr);

    static constexpr int kHighlightReads = 6;  ///< a change stays highlighted this many reads (3 s)

public slots:
    /// Reads the registry and the latches again (twice a second while the dialog shows)
    void refresh();

private:
    EmulatorContext* context() const;
    struct Seen
    {
        uint32_t value = 0;
        int highlight = 0;  ///< reads left to highlight a change
    };

    std::shared_ptr<Emulator> _emulator;
    QLabel* _live = nullptr;
    QCheckBox* _latchedOnly = nullptr;
    QTableWidget* _table = nullptr;
    QTimer* _timer = nullptr;
    std::map<std::string, Seen> _seen;  ///< per row (port / mask / device): the last value
};
