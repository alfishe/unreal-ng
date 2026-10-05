/**
 * @file trafficwindow.h
 * @brief TrafficWindow - the network traffic tap (network #91 T4): every
 *        Ethernet frame and socket operation of the machine's network
 *        adapters, with a decode, a hex view, a jump to the moment it happened
 *        (TTD) and the pcapng outputs (save, record to a file, the live stream
 *        Wireshark reads).
 *
 * A thin adapter like the automation interfaces: it reads and drives the tap
 * through TrafficAccess (the module behind the WebAPI, CLI, Lua, Python and
 * MCP), and the Qt-free model in network/core/trafficpanelmodel.h turns the
 * report into rows, decode and hex.
 * Design: docs/inprogress/2026-10-04-network-traffic-debug/design.md
 */

#pragma once

#include <QWidget>
#include <vector>

#include "network/core/trafficpanelmodel.h"

class EmulatorBinding;
class EmulatorContext;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTimer;
class QTreeWidget;

class TrafficWindow : public QWidget
{
    Q_OBJECT

public:
    static constexpr size_t kMaxRows = 20000;   ///< the newest rows the window keeps (the tap's ring holds more)

    explicit TrafficWindow(QWidget* parent = nullptr);

    void setBinding(EmulatorBinding* binding);

signals:
    /// Visibility changed via the window's own close box (keeps the menu in sync)
    void visibilityChanged(bool visible);
    /// A seek moved the machine: the screen needs a redraw
    void seeked();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void refresh();
    void rebuildTable();
    void onSelection();
    void onSeek();
    void onClear();
    void onSave();
    void onRecord();
    void onStream();

private:
    void buildUi();
    EmulatorContext* context() const;
    void appendRow(size_t rowIndex);
    const TrafficRow* selectedRow() const;
    void updateSeek();
    void reset();
    void control(const std::string& action, const std::string& path = {}, uint64_t number = 0);

    EmulatorBinding* _binding = nullptr;
    QTimer* _timer = nullptr;

    std::vector<TrafficRow> _rows;   ///< every fetched record, oldest first
    uint64_t _next = 0;              ///< the tap's next index: the next poll asks from here
    bool _recording = false;
    bool _streaming = false;

    QLineEdit* _filter = nullptr;
    QComboBox* _kind = nullptr;
    QTableWidget* _table = nullptr;
    QTreeWidget* _decode = nullptr;
    QPlainTextEdit* _hex = nullptr;
    QPushButton* _seek = nullptr;
    QPushButton* _clear = nullptr;
    QPushButton* _save = nullptr;
    QPushButton* _record = nullptr;
    QPushButton* _stream = nullptr;
    QLabel* _status = nullptr;
};
