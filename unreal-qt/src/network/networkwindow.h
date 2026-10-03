/**
 * @file networkwindow.h
 * @brief NetworkWindow - the machine's network devices: ZX-Bus cards
 *        (ZXNETUSB, ZX-WiFi), the machine's own serial port (ZX-Evo: the AVR's)
 *        and what is plugged into each, the emulated ESP, the ZX-Evo AVR
 *        firmware, the virtual network; and the live state of all of them.
 *
 * A thin adapter like the WebAPI, CLI, MCP, Lua and Python: it reads
 * DeviceState::Network and sends NetworkManager::ParseChange settings, so
 * every setting and every refusal is the one automation sees. Which controls
 * are offered follows the machine (PortDecoder::DescribeNetwork), through the
 * Qt-free model in network/core/networkpanelmodel.h.
 * Design: docs/inprogress/2026-09-30-nedoos-integration/tdd-network.md §8
 */

#pragma once

#include <QSet>
#include <QStringList>
#include <QWidget>

#include "network/core/networkpanelmodel.h"

class EmulatorBinding;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

/// What is plugged into one serial port: nothing, a loopback plug, a host TCP
/// endpoint, a host serial device, or an emulated ESP module
class SerialPeerEditor : public QWidget
{
    Q_OBJECT

public:
    explicit SerialPeerEditor(QWidget* parent = nullptr);

    void setSpec(const ComPortSpec& spec);
    ComPortSpec spec() const;
    /// The devices a SERIAL: peer can open (the typed text is kept)
    void setDevices(const QStringList& devices);

signals:
    void edited();

private:
    void updateFields();

    QComboBox* _kind = nullptr;
    QLineEdit* _host = nullptr;
    QSpinBox* _port = nullptr;
    QComboBox* _device = nullptr;
    QComboBox* _baud = nullptr;
    QWidget* _tcpRow = nullptr;
    QWidget* _serialRow = nullptr;
    QComboBox* _espBaud = nullptr;
    QWidget* _espRow = nullptr;
    QStringList _devices;
};

class NetworkWindow : public QWidget
{
    Q_OBJECT

public:
    explicit NetworkWindow(QWidget* parent = nullptr);

    void setBinding(EmulatorBinding* binding);

signals:
    /// Visibility changed via the window's own close box (keeps the menu in sync)
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void refresh();
    void onEdited();
    void onApply();
    void onRevert();

private:
    void buildUi();
    void loadForm(const NetworkForm& form);
    NetworkForm readForm() const;
    void updateAvailability();
    void updateStatusTree(const StateNode& network);
    void fillTree(QTreeWidgetItem* parent, const QString& path, const StateNode& node);

    EmulatorBinding* _binding = nullptr;
    QTimer* _timer = nullptr;

    NetworkForm _applied;      ///< the settings in force when the form was loaded
    bool _dirty = false;       ///< the user edited the form: refresh leaves it alone
    bool _loading = false;

    QLabel* _machine = nullptr;
    QCheckBox* _zxNetUsb = nullptr;
    QCheckBox* _zxWifi = nullptr;
    QLabel* _cardsWhy = nullptr;
    QLabel* _zxWifiWhy = nullptr;
    SerialPeerEditor* _zxWifiPeer = nullptr;
    QLabel* _comPortWhy = nullptr;
    SerialPeerEditor* _comPort = nullptr;
    QComboBox* _avrFirmware = nullptr;
    QLabel* _avrWhy = nullptr;
    QComboBox* _kbcFirmware = nullptr;
    QCheckBox* _atm2IoEsp = nullptr;
    QLabel* _atm2IoEspWhy = nullptr;
    SerialPeerEditor* _atm2IoEspPeer = nullptr;
    QComboBox* _atm2IoEspAddress = nullptr;
    QLabel* _zifiWhy = nullptr;
    SerialPeerEditor* _zifiPeer = nullptr;
    QLabel* _kbcWhy = nullptr;
    QComboBox* _espChip = nullptr;
    QCheckBox* _modemLines = nullptr;
    QCheckBox* _hostAccess = nullptr;
    QComboBox* _dnsMode = nullptr;
    QLineEdit* _hosts = nullptr;
    QLineEdit* _forwards = nullptr;
    QSpinBox* _timeout = nullptr;
    QPushButton* _apply = nullptr;
    QPushButton* _revert = nullptr;
    QLabel* _message = nullptr;
    QLabel* _notFitted = nullptr;
    QGroupBox* _slotsBox = nullptr;     ///< expansion slots (the Sprinter's ISA slots): what is plugged, what it uses
    QLabel* _slots = nullptr;
    QTreeWidget* _tree = nullptr;
    QSet<QString> _expanded;
    QWidget* _settingsPage = nullptr;
};
