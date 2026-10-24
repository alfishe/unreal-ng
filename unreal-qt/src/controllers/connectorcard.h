#pragma once

/// @file connectorcard.h
/// @brief One connector of the machine in the controllers dialog (docs/inprogress/2026-10-17-controller-manager/
/// ui-ux.md §2.3, §4): its colour and tag, name and port, the retro device combo, the retro device's picture lit
/// live in the connector's colour (a Sinclair or Cursor joystick adds the strip of the five ZX keys it presses), the
/// host devices plugged into it, and what is held. Host devices are dropped on it to plug them (DeviceDragList's
/// rows); while one hovers where the connector cannot take it the card shows the reason and refuses the drop. A
/// plugged device's chip is dragged off the card to unplug it, or onto another card to move it.

#include <QFrame>
#include <QListWidget>
#include <QString>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "emulator/io/controllers/controllermanager.h"

class ControllerPicture;
class QComboBox;
class QLabel;
class QVBoxLayout;
class KeyStripView;

/// The mime type of a dragged host device: its id as UTF-8
constexpr const char* kHostDeviceMime = "application/x-unreal-host-device";

/// The dialog's host device list: a row drags its device (rows without a player device do not drag)
class DeviceDragList : public QListWidget
{
public:
    explicit DeviceDragList(QWidget* parent = nullptr);

protected:
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
};

class ConnectorCard : public QFrame
{
    Q_OBJECT

public:
    explicit ConnectorCard(QWidget* parent = nullptr);

    /// The connector as the machine has it now
    void setConnector(const controllers::Connector& connector);
    const controllers::Connector& connector() const
    {
        return _connector;
    }
    /// The held controls (ControllerManager::State); repaints only on a change
    void setHeld(uint16_t mask);
    /// The host devices plugged in: id and name, in plug order
    void setDrivers(const std::vector<std::pair<std::string, QString>>& drivers);
    const std::vector<std::pair<std::string, QString>>& drivers() const
    {
        return _drivers;
    }
    /// Why the connector cannot take a host device ("" when it can); asked while a device hovers
    std::function<QString(const std::string& device)> refusalFor;
    /// The refusal shown now ("" while nothing refused hovers)
    QString hoverRefusal() const
    {
        return _refusal;
    }
    /// Where a patch cord ends: the left edge, at the header's height (card coordinates)
    QPoint cordAnchor() const;
    const ControllerPicture* picture() const
    {
        return _picture;
    }

    /// For the tests: what a drop of @p device does, as the drag and drop does it (refused: nothing)
    bool dropDevice(const std::string& device);

signals:
    /// The combo chose another retro device ("" = empty the connector)
    void retroDeviceChosen(const QString& connector, const QString& retroDevice);
    /// A host device was dropped here
    void deviceDropped(const QString& device, const QString& connector);
    /// A plugged device's chip was dropped where nothing takes it: unplug it
    void deviceDraggedOff(const QString& device, const QString& connector);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void rebuildDrivers();
    void showRefusal(const QString& refusal);

    controllers::Connector _connector;
    uint16_t _held = 0;
    std::vector<std::pair<std::string, QString>> _drivers;
    QString _refusal;
    bool _hover = false;  ///< a device that can be plugged hovers
    QLabel* _title = nullptr;
    QComboBox* _device = nullptr;
    ControllerPicture* _picture = nullptr;
    QString _pictureSet;
    KeyStripView* _strip = nullptr;
    QWidget* _chips = nullptr;
    QVBoxLayout* _chipsLayout = nullptr;
    QLabel* _heldLabel = nullptr;
    QLabel* _refusalLabel = nullptr;
    QLabel* _noteLabel = nullptr;  ///< the Next's "X Y Z MODE: NR #B2 only"
    bool _updating = false;
};
