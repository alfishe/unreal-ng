#pragma once

/// @file bindingtable.h
/// @brief The bindings of one host device, from the machine's side (docs/inprogress/2026-10-17-controller-manager/
/// research-binding-ux.md): a row per thing the game sees, in plain words (Joystick: Move, Up, Down, Left, Right,
/// Fire; ZX keys; Mouse; Emulator: Pause, On-screen keyboard …), each holding the pad's controls bound to it as chips
/// named the way the pad names them ("Cross", "D-pad", "Left stick"). Click + and press a control to add one; click a
/// chip to change it, right-click it for autofire, tap, remove, restore, edit as text. "Works as" picks the scheme
/// (Kempston, Sinclair 1, QAOP + Space), "Save for" the profile written (this game, every pad of the family, this pad,
/// every pad), "Set all…" walks the joystick rows. Everything goes through the host verbs (HostInputControl).

#include <QString>
#include <QWidget>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#ifdef ENABLE_CONTROLLERS
#include "profiles/json.h"
#endif

class ActionRegistry;
class HostControllers;
class QComboBox;
class QLabel;
class QPushButton;
class QTabBar;
class QTableWidget;

class BindingTable : public QWidget
{
    Q_OBJECT

public:
    struct Device
    {
        std::string id;
        std::string family;
        std::string glyphs;  ///< the database's glyph set (xbox, playstation, nintendo)
        QString name;
    };

    explicit BindingTable(HostControllers* host, QWidget* parent = nullptr);

    void setActions(const ActionRegistry* actions)
    {
        _actions = actions;
    }
    /// The device whose bindings are shown (an empty id: none); reads the bindings again
    void setDevice(const Device& device);
    /// Read the bindings again (after a change in the profiles); a press being awaited stays awaited
    void refresh();
    /// The live values of the device's controls: lights the chips, and takes the press while one is awaited
    void setLive(const std::map<std::string, float>& live);
    /// A control clicked on the picture: its chip is scrolled to, or the hint says it is free
    void showControl(const std::string& control);
    /// Advanced > Debug layer: the Debugging tab is shown
    void setDebugTab(bool on);
    /// The picture's callout per control: what it does ("Fire", "Q", "Pause / continue")
    const std::map<std::string, QString>& captions() const
    {
        return _captions;
    }
    /// The pad's name for a control ("Cross", "D-pad ↑", "Left stick")
    static QString ControlName(const std::string& control, const std::string& glyphs);

signals:
    /// The pointer is on a chip of this control (the picture highlights it); empty when it leaves
    void controlHovered(const std::string& control);
    /// The bindings were read again: captions() changed
    void captionsChanged();

protected:
    void keyPressEvent(class QKeyEvent* event) override;
    bool eventFilter(QObject* watched, class QEvent* event) override;

private:
    enum class Capture
    {
        Button,      ///< one control
        Directions,  ///< the D-pad or a stick as a whole
        MouseMove,   ///< a stick: its x and y axes
    };
#ifdef ENABLE_CONTROLLERS
    using Binds = std::vector<std::pair<std::string, controllers::JsonValue>>;
    /// An effective binding of the shown layer (bindings verb), none included: it hides a lower one
    struct Bound
    {
        std::string control;
        controllers::JsonValue spec;
        std::string profile;
    };
    struct Chip
    {
        std::string control;             ///< what the chip shows: south, dpad.*, rstick (a stick's two axes)
        std::vector<std::string> parts;  ///< the bound controls behind it (rstick.x, rstick.y)
        controllers::JsonValue spec;
        std::string profile;
        bool user = false;  ///< from a user profile (shown bold): the user's own choice
    };
    struct Row
    {
        std::string key;  ///< retro:fire, retro:dpad, key:Q, mouse:move, mouse:left, action:run.pause_toggle
        QString section;
        QString title;
        QString tooltip;
        controllers::JsonValue target;  ///< the spec a new chip gets ({retro: "auto:fire"})
        Capture capture = Capture::Button;
        std::vector<Chip> chips;
    };

    void buildRows();
    Row& addRow(const std::string& key, const QString& section, const QString& title, const QString& tooltip,
                controllers::JsonValue target, Capture capture = Capture::Button);
    Row* findRow(const std::string& key);
    const Bound* findBound(const std::string& control) const;
    void populate();
    QWidget* chipButton(const Row& row, const Chip& chip);
    void refreshScope();
    void updateCaptions();
    void light();
    /// Scroll the table to the line of a chip or + button
    void reveal(QWidget* widget);

    void startCapture(const std::string& rowKey, const std::string& replace = {});
    void captured(const std::string& control, float value);
    void cancelCapture(const QString& why = {});
    /// The press for the awaited row: "already", or the question when another row has it, else apply
    void commit(const Binds& binds, const std::string& replace);
    /// Bind it: what it overlaps elsewhere is hidden (a direction taken from a whole D-pad splits it), the chip it
    /// replaces removed
    void apply(const Binds& binds, const std::string& replace);
    void startWizard();
    void wizardStep();
    void endWizard();
    void showPrompt(const QString& text, bool yes, bool wizard);
    void hidePrompt();
    void addKey();

    void chipMenu(const std::string& rowKey, const Chip& chip, const QPoint& where);
    bool bind(const std::string& control, const controllers::JsonValue& spec);
    bool unbind(const std::string& control);
    void restoreDefaults();
    void schemeChosen();

    std::map<std::string, std::string> scopeOptions() const;
    std::string scopeProfile() const;
    std::string layer() const;
    /// "Joystick fire", "Mouse move", "ZX key Q", "Pause / continue"
    QString fullTitle(const std::string& rowKey);
    void say(const QString& text, bool error = false);

    std::vector<Row> _rows;
    std::vector<Bound> _effective;
    std::vector<std::pair<QPushButton*, Chip>> _chipButtons;  ///< to light them while their control is pressed
    Binds _pendingBinds;                                      ///< the press waiting for "Use it here"
    std::string _pendingReplace;
#endif
    HostControllers* _host = nullptr;
    const ActionRegistry* _actions = nullptr;
    Device _device;

    QTabBar* _layers = nullptr;
    QComboBox* _scope = nullptr;
    QComboBox* _scheme = nullptr;
    QPushButton* _setAll = nullptr;
    QPushButton* _restore = nullptr;
    QLabel* _prompt = nullptr;
    QPushButton* _promptYes = nullptr;
    QPushButton* _promptBack = nullptr;
    QPushButton* _promptSkip = nullptr;
    QPushButton* _promptCancel = nullptr;
    QTableWidget* _grid = nullptr;  ///< borderless: the input column sized to its text, the controls column the rest
    QLabel* _status = nullptr;
    QPushButton* _captureButton = nullptr;  ///< the + or chip that shows the press is awaited

    // The press being awaited
    std::string _captureRow;
    std::string _captureReplace;
    std::map<std::string, float> _baseline;
    int _captureTicks = 0;
    std::string _pendingKey;           ///< a ZX key row added by "Add a key…", shown until bound or cancelled
    std::vector<std::string> _wizard;  ///< the rows "Set all…" walks
    size_t _wizardStep = 0;

    std::map<std::string, float> _live;
    std::map<std::string, QString> _captions;
    std::set<QPushButton*> _lit;
    bool _filling = false;
    bool _debugTab = false;  ///< the debug layer is on: every device shows its Debugging tab
};
