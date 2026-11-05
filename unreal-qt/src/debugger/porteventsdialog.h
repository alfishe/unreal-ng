#pragma once

#include <memory>

#include <QDialog>

#include "emulator/state/statenode.h"

class Emulator;
class EmulatorContext;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

/// "When did the program ..." in the debugger (debugger enhancements TODO #7, ttd-port-read-journal.md §10): a
/// named event (a key, the tape signal, an AY register read / write / select, the border, the beeper, raw IN / OUT)
/// with the query options (time window, limit, newest first, trigger, port / value masks), searched in the session's
/// port journals or a .ttd file without loading it. A double click on a hit seeks there and shows its PC in the
/// disassembly. Everything goes through TTDControl's "port-events" and "seek" verbs, the code CLI `ttd port-events`,
/// WebAPI /ttd/port-events, MCP port_events and Lua / Python ttd_port_events run, so a refusal (no port journals on
/// TS-Conf / ZX Next / NeoGS, a running recording) reads the same as there. The journals come from a time-travel
/// recording: the top row shows its state and starts / stops it (the "start" / "stop" verbs), and the window does not
/// block the debugger, so the program can run while it records. The listed hits export to a .csv file
class PortEventsDialog : public QDialog
{
    Q_OBJECT

public:
    PortEventsDialog(std::shared_ptr<Emulator> emulator, QWidget* parent = nullptr);
    const std::shared_ptr<Emulator>& emulator() const { return _emulator; }

public slots:
    /// Runs the query the fields describe and lists its hits
    void search();
    /// The last search's hits as CSV (RFC 4180, a header row): the raw numbers and the core's words, one row per hit
    QString hitsCsv() const;
    /// Writes hitsCsv() to the file; false and the reason in the status line when it cannot
    bool exportCsv(const QString& path);

signals:
    /// A hit was sought (the session): the debugger shows the new machine state and this PC
    void seekedTo(uint16_t pc);

private slots:
    void seekToRow(int row);
    void browseFile();
    /// The recording's state on the top row (polled while shown)
    void updateRecording();
    void toggleRecording();
    /// Asks for a file and exports the hits there
    void exportCsvToFile();

private:
    EmulatorContext* context() const;
    std::shared_ptr<Emulator> _emulator;
    QComboBox* _event = nullptr;
    QLineEdit* _arg = nullptr;
    QLineEdit* _from = nullptr;
    QLineEdit* _to = nullptr;
    QSpinBox* _limit = nullptr;
    QCheckBox* _newest = nullptr;
    QComboBox* _trigger = nullptr;
    QLineEdit* _port = nullptr;
    QLineEdit* _portMask = nullptr;
    QLineEdit* _value = nullptr;
    QLineEdit* _valueMask = nullptr;
    QLineEdit* _file = nullptr;
    QLabel* _recording = nullptr;
    QPushButton* _recordButton = nullptr;
    QLabel* _status = nullptr;
    QTableWidget* _hits = nullptr;
    QPushButton* _exportButton = nullptr;
    StateNode _lastReply;  ///< the last search's reply body: the export writes its hits, not the table's text
    bool _hitsFromFile = false;  ///< the listed hits came from a file: no seek
};
