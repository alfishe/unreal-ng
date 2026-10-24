#pragma once

/// @file controllersdialog.h
/// @brief Tools > Controllers (docs/inprogress/2026-10-17-controller-manager/ui-ux.md §2): host devices on the left,
/// the selected device's picture (ControllerPicture, C5) and its bindings from the machine's side (BindingTable: what
/// the game sees, each with the pad's controls bound to it) in the centre, the machine's connectors with their retro
/// device on the right, the applied profiles on top. Profiles… opens the profile manager. Patch cords
/// (PatchCordOverlay) join each device to the connectors it drives; Test shows the device's test panel
/// (DeviceTestPanel); Picture… opens the hotspot editor for a photo of the user's own pad.

#include <QWidget>

#include <functional>
#include <string>
#include <vector>

namespace actions
{
class ActionRegistry;
}
class BindingTable;
class ControllerPicture;
class DeviceTestPanel;
class PatchCordOverlay;
class HostControllers;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTimer;
class QVBoxLayout;
class SurfaceTargets;
class SurfacesTable;

class ControllersDialog : public QWidget
{
    Q_OBJECT

public:
    explicit ControllersDialog(HostControllers* host, QWidget* parent = nullptr);
    /// The host actions the bindings offer by name
    void setActions(const actions::ActionRegistry* actions);
    /// The control surfaces table under the devices (Stream Deck targeting.md T-2): each surface and its pin
    void setSurfaceTargets(SurfaceTargets* targets);
    /// For the tests and the offscreen grabs
    const PatchCordOverlay* patchCords() const
    {
        return _cords;
    }

signals:
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void rebuild();
    void rebuildControls();
    void rebuildConnectors();
    /// The haptics test: the effects of the selected device's actuators and the Test button, or both off with the reason
    void rebuildHapticsTest(const std::string& device);
    void tick();
    /// The cords from each device's row through the picture to its connectors' rows (ui-ux.md §2.3)
    void updateCords();
    void plug(const std::string& connector, const QString& device);
    std::string selectedDevice() const;
    std::string selectedFamily() const;
    std::string selectedLayout() const;

    HostControllers* _host = nullptr;
    QLabel* _profileLabel = nullptr;
    QComboBox* _gameProfile = nullptr;  ///< ProfileContext::manual: by the loaded file, or one chosen here
    QCheckBox* _advanced = nullptr;
    QCheckBox* _soundHaptics = nullptr;
    QCheckBox* _globalShortcuts = nullptr;
    QListWidget* _devices = nullptr;
    QLabel* _probeLabel = nullptr;
    QLabel* _deviceTitle = nullptr;
    QLabel* _battery = nullptr;  ///< next to the title: the selected device's battery
    ControllerPicture* _picture = nullptr;
    QString _pictureSet;
    BindingTable* _table = nullptr;
    QPushButton* _profilesButton = nullptr;
    QComboBox* _hapticsEffect = nullptr;  ///< the effect Test plays
    QPushButton* _testHaptics = nullptr;  ///< always there; off, with the reason, for a device it cannot drive
    std::vector<std::function<void()>> _hapticsTests;  ///< one per _hapticsEffect item
    QTableWidget* _connectors = nullptr;
    PatchCordOverlay* _cords = nullptr;
    DeviceTestPanel* _test = nullptr;
    QPushButton* _testButton = nullptr;
    QPushButton* _pictureButton = nullptr;
    QCheckBox* _pauseOnDisconnect = nullptr;
    QLabel* _machineLabel = nullptr;
    QLabel* _hapticsLabel = nullptr;  ///< haptics.md §7: the live meters of the feed
    QTimer* _live = nullptr;
    QVBoxLayout* _root = nullptr;
    SurfacesTable* _surfaces = nullptr;
    uint64_t _machineRevision = ~0ull;
    bool _rebuildingConnectors = false;
};
