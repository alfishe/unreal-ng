#pragma once

/// @file hostcontrollers.h
/// @brief The Qt client's host controllers (docs/inprogress/2026-10-17-controller-manager/ui-ux.md §8): the process's
/// HostInputService and ProfileStore, the bound emulator's IHostInputSource, the player connector each pad drives
/// (retro-devices.md §4, the machine default) and the profiles re-applied whenever a device or the machine changes.

#include <QObject>
#include <QString>
#include <QTimer>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef ENABLE_CONTROLLERS
#include "hostinputcontrol.h"
#include "hostinputservice.h"
#include "profiles/autodecision.h"
#include "profiles/profilestore.h"
#include "wheelclaim.h"
#endif
#include "controllers/connectorview.h"

class Emulator;
class EmulatorContext;

/// A device's host action (debug-and-ttd.md §3, control surfaces architecture.md §5), for the owner that hands it to
/// the core ActionRegistry
struct HostActionEvent
{
    QString action;
    bool pressed = false;
    float value = 0;  ///< 1 for a press, a scrub level, an encoder's steps
    QString device;   ///< the host device's id
    bool turn = false;  ///< an encoder's turn: `value` steps at `rate` steps/s
    double rate = 0;
    QString params;     ///< the binding's `curve` object as JSON (the action's parameters); empty when none
};
class EmulatorBinding;
class QKeyEvent;
class QWidget;

class HostControllers : public QObject
{
    Q_OBJECT

public:
    explicit HostControllers(EmulatorBinding* binding, QObject* parent = nullptr);
    ~HostControllers() override;

#ifdef ENABLE_CONTROLLERS
    controllers::HostInputService& Service()
    {
        return *_service;
    }
    controllers::ProfileStore& Profiles()
    {
        return _profiles;
    }
    const controllers::ResolvedProfiles& Resolved() const
    {
        return _resolved;
    }
    /// What the last Reapply resolved for
    const controllers::ProfileContext& Context() const
    {
        return _context;
    }
    /// The user profiles' directory (`<user config>/unreal-ng/controllers/profiles`, architecture.md §12)
    static QString UserProfileDir();
    /// The user's controllers folder (`<user config>/unreal-ng/controllers`): devices.user.json over the compiled-in
    /// device database and the hotspot editor's image sets in images/<set> (device-database.md §7)
    static QString UserControllersDir();
    /// The status-bar connector LED (ui-ux.md §8): shown while a connector holds a device, lit while one is held,
    /// the tooltip per connector what is held and which host devices drive it
    connectorview::Led ConnectorLed() const;
    /// A player device holds its debug layer key now (not the debugger window's focus): the status bar's DEBUG tag
    bool DebugLayerHeld() const;
    /// A host verb (surfaces.md §2: profiles, bindings, bind / unbind, save, delete, apply) run here as the automation
    /// surfaces run it: the dialog's editors take the same path as WebAPI, MCP, CLI and Python
    controllers::HostReply Host(const controllers::HostRequest& request);
    /// "Keep" on the auto toast: a user game profile keyed by the loaded file's SHA-256 (mapping-and-profiles.md
    /// §6.1) holding the connector and the first pad's pin; false with the reason
    bool KeepAutoDecision(QString& error);
    /// The connector a player device drives, chosen by the user (the dialog's drag and drop, the `player` verb); an
    /// empty @p connector: back to the automatic choice; "none": no connector (a cord dragged off). Kept for the
    /// session by device id (a replug keeps it). Empty when done, else the error code with @p message
    std::string ChoosePlayerConnector(const std::string& device, const std::string& connector, std::string& message);
    /// "" automatic, "none", or the chosen connector's id
    std::string PlayerChoice(const std::string& device) const;
    /// Machine > Controllers > Reset bindings to default: every choice made by hand goes, the profiles and the
    /// machine's order decide again
    void ResetPlayerChoices();
#endif
    /// Advanced > Debug layer (debug-and-ttd.md §1.6): the debug profiles take part; kept in the settings
    bool DebugLayer() const
    {
        return _debugLayer;
    }
    void SetDebugLayer(bool on);
    /// A game profile chosen by hand (empty: by the loaded file), for a program the emulator did not load itself
    /// (a NEX started from NextZXOS's browser)
    const std::string& ManualProfile() const
    {
        return _manualProfile;
    }
    void SetManualProfile(const std::string& id);
    /// Advanced > Haptics from the sound (haptics.md §4, H-6: off by default): the generic AY / beeper filters
    bool SoundHaptics() const
    {
        return _soundHaptics;
    }
    void SetSoundHaptics(bool on);
    /// The window keyboard (`kbd:system`, mapping-and-profiles.md §4) is a debugger remote: its debug bindings are the
    /// debugger's shortcuts (debug-and-ttd.md §4.3). They take the keys of the debugger window, and of the main window
    /// with "global shortcuts" on (kept in the settings, off by default); a key they take does not reach the machine.
    /// While the debugger window has the focus the pads are debugger remotes too, with no layer key and without the
    /// Advanced switch (InputRouter::SetDebugFocus); another app taking the focus changes nothing
    void SetShortcutWindows(QWidget* main, QWidget* debugger);
    bool GlobalShortcuts() const
    {
        return _globalShortcuts;
    }
    void SetGlobalShortcuts(bool on);
    /// A player device that goes while the machine runs pauses it (ui-ux.md §8; kept in the settings, off by default)
    bool PauseOnDisconnect() const
    {
        return _pauseOnDisconnect;
    }
    void SetPauseOnDisconnect(bool on);
    /// The keyboard control of a key event (`key.ctrl.f10`); empty for a modifier alone or an unknown key
    static std::string KeyControlOf(const QKeyEvent* event);

    /// region <Contexts (control surfaces architecture.md §6)>
    /// The widgets whose focus sets the bound instance's context through actions::ContextState (last setter wins,
    /// Q-5): the debugger's panes (`debug.disasm`, `debug.registers`, `debug.memory`, `debug.stack`) and the TTD panel
    /// (`ttd`). Entering one sets its context; the TTD panel shown sets `ttd`, hidden returns a `ttd` the GUI set to
    /// the default; the debugger window hidden returns a `debug.*` the GUI set to the default
    void SetContextWidgets(std::vector<std::pair<QWidget*, std::string>> widgets, QWidget* ttdPanel);
    /// The TTD panel was shown or hidden
    void OnTtdPanelVisible(bool visible);
    /// The debugger window was shown or hidden
    void OnDebuggerVisible(bool visible);
    /// A seek's marks felt on the device that moved (its top layer's feel; architecture.md §9)
    void PlayMarks(const std::string& device, const std::vector<std::string>& marks);
    /// endregion </Contexts>

    /// The Haptics panel (haptics.md §7): the feed, the gate, the motors of @p device and the sources' levels
    QString HapticsStatus(const std::string& device) const;

    /// The emulator the host devices drive, and its context (nullptr when none)
    Emulator* BoundEmulator() const;
    EmulatorContext* BoundContext() const;
    /// Resolve the profiles and the player connectors again
    void Reapply();

signals:
    /// A device arrived or left, or the profiles changed
    void changed();
    /// A host action (debug-and-ttd.md §3) from a device, on the GUI thread; the owner hands it to the core
    /// ActionRegistry. @p value: 1 for a press, a scrub level, an encoder's steps; @p device: the host device's id
    void actionTriggered(const HostActionEvent& event);
    /// For the status bar: "Xbox Elite 2 connected → kempston"
    void notice(const QString& text);
    /// The port-read heuristic plugged a device for the loaded program (ui-ux.md §3.8): the toast's line
    void autoDecided(const QString& text);
    /// A player device arrived; @p first: never seen on this computer before (the toast with Open controllers),
    /// @p connector: the one it drives ("" none)
    void deviceConnected(const QString& name, const QString& connector, bool first);
    /// A player device the database does not know (no family, no learned or imported map) connected for the first
    /// time: the learn toast (ui-ux.md §3.5) instead of the first-connect one
    void unknownDeviceConnected(const QString& name, const QString& device);
    /// A player device left while the bound machine ran; @p paused: the pause-on-disconnect option paused it
    void deviceDisconnected(const QString& name, bool paused);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// A key of a window the shortcuts take: true when a binding takes it (the window and the machine do not see it)
    bool shortcutKey(const QKeyEvent* event, bool pressed);
    /// The debugger window took or lost the focus: the pads follow
    void updateDebugFocus();
    void onBound();
    void onUnbound();
    void poll();
    /// The bound instance's context (actions::ContextState, its default included) into the router
    void followContext();
    /// The focus moved: the pane or panel it entered sets the context
    void updateGuiContext();
    /// Set the bound instance's context as the GUI ("" = back to the default)
    void setGuiContext(const std::string& context);
    /// The GUI set the context in force and it matches @p prefix: back to the default
    void releaseGuiContext(const std::string& prefix);
#ifdef ENABLE_CONTROLLERS
    void assignPlayers(const std::vector<controllers::HostDeviceInfo>& devices);
    /// Plug what the resolved profiles' connectors name, where the machine holds something else
    void applyConnectorPlans();
    void onFileLoaded(const QString& emulatorId, const QString& path);
    void checkAuto();
    void applyAuto(const controllers::AutoDecision& decision);
    bool saveUserProfile(controllers::Profile profile, QString& error);
    /// H-5: the game's haptics play only while the bound machine runs live at 1x and the application has focus
    void updateHapticsGate();
    /// debug-and-ttd.md §7: a breakpoint hit is one short pulse on every pad with motors (debug layer on)
    void onBreakpointHit(const QString& emulatorId);
    /// The feed for the resolved profiles: a game profile's haptics, else the sound filters when on, else none
    void attachHaptics();
    /// surface-dial.md §4.1: while a wheel device (the Surface Dial) is connected, the main and debugger windows hold
    /// the system wheel (Windows: no built-in tools, no wheel menu); released when the last one goes and at the end
    void updateWheelMenu();
    void releaseWheelMenu();
    controllers::WheelClaim _wheelClaim;
    std::map<controllers::WheelClaim::Window, void*> _wheelHolds;  ///< WheelMenuWindows::Take's holds by window
    bool _wheelConnected = false;
    std::shared_ptr<controllers::IHapticFeed> _hapticFeed;
    std::string _hapticsKey;  ///< what _hapticFeed was made from: a re-resolve to the same keeps its state
    uint64_t _fileObserver = 0;
    uint64_t _contextObserver = 0;
    uint64_t _breakpointObserver = 0;
    std::string _fileSha256;  ///< of the program loaded last into the bound emulator
    QString _fileName;
    bool _autoDone = false;      ///< decided (or a kept game profile holds the connectors) for this program
    std::string _autoConnector;  ///< the connector the decision chose: the first pad drives it
    controllers::AutoDecision _decision;
    std::unique_ptr<controllers::HostInputService> _service;
    controllers::ProfileStore _profiles;
    controllers::ResolvedProfiles _resolved;
    controllers::ProfileContext _context;
    /// HostInputControl's owner (the WebAPI / MCP / CLI / Python host verbs run on the GUI thread through it)
    std::unique_ptr<controllers::IHostInputOwner> _controlOwner;
    std::vector<controllers::HostDeviceId> _known;
    std::map<controllers::HostDeviceId, std::pair<std::string, bool>> _knownNames;  ///< name, a player, by id
    std::map<controllers::HostDeviceId, std::string> _playerChoice;  ///< the user's connector per device
#endif
    bool _pauseOnDisconnect = false;
    EmulatorBinding* _binding = nullptr;
    std::shared_ptr<Emulator> _attached;  ///< the emulator whose ControllerManager samples the service
    QTimer _timer;
    uint64_t _revision = ~0ull;
    uint64_t _machineRevision = ~0ull;
    bool _debugLayer = false;
    bool _debugFocus = false;  ///< the debugger window has the focus
    std::string _manualProfile;
    bool _soundHaptics = false;
    bool _globalShortcuts = false;
    QWidget* _mainWindow = nullptr;
    QWidget* _debuggerWindow = nullptr;
    std::vector<std::pair<QWidget*, std::string>> _contextWidgets;
    QWidget* _ttdPanel = nullptr;
    std::string _guiPane;  ///< the context of the widget the focus entered last ("" none): set once per entry
    struct HeldKey
    {
        std::string control;
        bool taken = false;
    };
    std::map<int, HeldKey> _heldKeys;  ///< by native key: the control its press sent, released by the same name
};
