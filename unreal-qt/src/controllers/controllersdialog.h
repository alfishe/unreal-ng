#pragma once

/// @file controllersdialog.h
/// @brief Tools > Controllers (docs/inprogress/2026-10-17-controller-manager/ui-ux.md §2): host devices on the left,
/// the selected device's picture (ControllerPicture, C5) and its bindings from the machine's side (BindingTable: what
/// the game sees, each with the pad's controls bound to it) in the centre, the machine's connectors with their retro
/// device as cards (ConnectorCard) on the right, the applied profiles on top. Profiles… opens the profile manager.
/// Patch cords (PatchCordOverlay) join each device to the connectors it drives; a device row dropped on a card plugs
/// it there, a plugged device's chip dragged off its card unplugs it, both with undo (Ctrl+Z). Test shows the
/// device's test panel (DeviceTestPanel); Picture… opens the hotspot editor for a photo of the user's own pad;
/// Import mapping… takes a gamecontrollerdb line or a RetroArch autoconfig for a pad the database does not know.
/// Under the title the selected device's role (player, debugger remote, disabled), the instances it drives (follow
/// focus, all, one), Identify (a rumble and a blink; its row pulses) and Learn… (LearnWizard, ui-ux.md §3.5), each
/// through its host verb, as every plane runs it.

#include <QPointer>
#include <QWidget>

#include <functional>
#include <string>
#include <vector>

#include "controllers/learnwizard.h"

namespace actions
{
class ActionRegistry;
}
class BindingTable;
class ConnectorCard;
class DeviceDragList;
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
    const std::vector<ConnectorCard*>& connectorCards() const
    {
        return _cards;
    }
    /// A host device dropped on a connector's card (the drag and drop, and the tests): plugs the connector's
    /// retro device first when it is empty; false (and the reason in the status line) when refused
    bool plugDevice(const std::string& device, const std::string& connector);
    /// A plugged device's chip dragged off its card: the device drives no connector until undone
    void unplugDevice(const std::string& device);
    /// Undo the last drop or unplug; false when there is nothing to undo
    bool undo();
    /// Why a drop of @p device on @p connector is refused ("" = it is taken)
    QString refusal(const std::string& device, const std::string& connector) const;
    QString statusText() const;
    /// Import mapping…'s work for @p text (a gamecontrollerdb.txt, many lines: the one with the selected device's
    /// VID:PID, else the first; or a RetroArch .cfg); the answer for the status line
    QString importMapping(const QString& text);
    /// The learn wizard for @p device (Learn…, the learn toast, the ctl.learn action): selects the device, opens the
    /// wizard and starts its session; an empty answer when it runs, else the reason (also in the status line)
    QString openLearnWizard(const std::string& device);
    /// The wizard open now (nullptr when none); for the tests
    LearnWizard* learnWizard() const
    {
        return _wizard;
    }
    /// Identify on the selected device: the identify verb's answer (its row pulses while the pad rumbles)
    QString identifySelected();

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
    bool plug(const std::string& connector, const QString& device);
    void setStatus(const QString& text, bool error);
    /// The test panel's thresholds and endpoints (@p full) and its raw reports: only while it is shown
    void refreshTestPanel(bool full);
    std::string selectedDevice() const;
    std::string selectedFamily() const;
    std::string selectedLayout() const;
    /// The role and pin combos from the router (another plane may have changed them); not while one is open
    void refreshRolePin();

    HostControllers* _host = nullptr;
    QLabel* _profileLabel = nullptr;
    QComboBox* _gameProfile = nullptr;  ///< ProfileContext::manual: by the loaded file, or one chosen here
    QCheckBox* _advanced = nullptr;
    QCheckBox* _soundHaptics = nullptr;
    QCheckBox* _soundHapticsDsp = nullptr;  ///< the Haptics panel: lowband / onset of the mix in the sound haptics
    QCheckBox* _globalShortcuts = nullptr;
    DeviceDragList* _devices = nullptr;
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
    QWidget* _cardsBox = nullptr;
    QVBoxLayout* _cardsLayout = nullptr;
    std::vector<ConnectorCard*> _cards;
    QLabel* _status = nullptr;  ///< the last drop's or import's answer
    QPushButton* _undoButton = nullptr;
    QPushButton* _importButton = nullptr;
    /// One drop or unplug: the device's former choice ("" automatic, "none", a connector id) and the connector
    /// whose retro device the drop plugged ("" when it plugged none)
    struct UndoStep
    {
        std::string device;
        std::string choice;
        std::string pluggedConnector;
    };
    std::vector<UndoStep> _undo;
    PatchCordOverlay* _cords = nullptr;
    DeviceTestPanel* _test = nullptr;
    QPushButton* _testButton = nullptr;
    QPushButton* _pictureButton = nullptr;
    QComboBox* _role = nullptr;  ///< player, debugger-remote, disabled (the role verb)
    QComboBox* _pin = nullptr;   ///< follow-focus, all, pinned:<instance> (the pin verb)
    QPushButton* _identify = nullptr;
    QPushButton* _learnButton = nullptr;
    QPointer<LearnWizard> _wizard;
    QComboBox* _view = nullptr;  ///< Front | Back | Top: shown when the set has more than one map
    QCheckBox* _pauseOnDisconnect = nullptr;
    QLabel* _machineLabel = nullptr;
    QLabel* _hapticsLabel = nullptr;  ///< haptics.md §7: the live meters of the feed
    QTimer* _live = nullptr;
    QVBoxLayout* _root = nullptr;
    SurfacesTable* _surfaces = nullptr;
    uint64_t _machineRevision = ~0ull;
    unsigned _ticks = 0;
    unsigned _rolePinTicks = 0;
    bool _rebuildingConnectors = false;
};
