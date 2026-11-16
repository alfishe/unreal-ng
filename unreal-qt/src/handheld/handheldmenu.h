#pragma once

/// @file handheldmenu.h
/// @brief The handheld menu (docs/inprogress/2026-11-12-handheld-menu): a SteamOS-styled emulator menu driven by a
/// controller, a child widget of the main window drawn over the paused picture (gamescope shows one window of the
/// app, so never a top-level one). Every entry calls a catalogue action or reads a readout (design C-5). The pads
/// reach it as UI input (HandheldPad, before the emulator's router), the automation through the `handheld.*`
/// commands of the GUI plane.

#include <QImage>
#include <QJsonObject>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QWidget>

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "emulator/state/statenode.h"
#include "handheld/handheldbrowser.h"

class Emulator;
class EmulatorContext;
class QTimer;

class HandheldMenu : public QWidget
{
    Q_OBJECT

public:
    struct Hooks
    {
        std::function<Emulator*()> emulator;
        /// The bound machine's context (EmulatorBinding::context), for the current handler only, never stored
        std::function<EmulatorContext*()> context;
        /// The picture the screen shows now (letterbox excluded), for the background while the menu is open
        std::function<QImage()> grabPicture;
        /// Hide the screen while the menu covers it (a native GL window stays above any widget otherwise)
        std::function<void(bool)> showScreen;
        std::function<void()> quit;
        /// The on-screen keyboard's choices (id, name: "" the machine's own, "minimal", "steam", the pictures that
        /// fit the machine), the forced one and a new choice (OsdKeyboardWindow::setForced)
        std::function<std::vector<std::pair<std::string, QString>>()> keyboards;
        std::function<std::string()> keyboard;
        std::function<void(const std::string&)> setKeyboard;
        /// Show the on-screen keyboard over the game (the menu closes first)
        std::function<void()> showKeyboard;
        /// A slot change (SlotControl verbs plug / remove) on the shown machine, applied by a restart the window
        /// follows; "" when applied, else why not
        std::function<QString(const std::string& verb, const std::string& slot, const std::string& card)> changeSlot;
        /// A short note for the user (the status bar or the HUD toast)
        std::function<void(const QString&)> notice;
        /// The floppy drive under the hand (HostControllers::DriveHaptics): the head's steps and the turning disk on the
        /// pads; empty: no controllers in this build
        std::function<bool()> driveHaptics;
        std::function<void(bool)> setDriveHaptics;
    };

    HandheldMenu(Hooks hooks, QWidget* parent);

    bool IsOpen() const { return _open; }
    void Open();
    void Close();

    /// Controller verbs (`handheld.*`): move the focus (left / right also adjust a value row), activate, go back, a
    /// page's X / Y, the previous / next section
    void Nav(int dx, int dy);
    void Accept();
    void Back();
    void Option(char button);
    void Section(int step);

    /// Run a catalogue action with one `command` parameter on the bound machine (the pad's back grips on the
    /// on-screen keyboard: basic.run LOAD "")
    void Run(const std::string& id, const QString& command);

    /// What the automation reads (`handheld_menu` state of the GUI control plane)
    QJsonObject State() const;

signals:
    void stateChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    /// The touchscreen (and a mouse on a desktop): a tap activates what it touches, a drag moves the focus
    bool event(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Page
    {
        Slots,     ///< Save & Load: the quick-save slots
        Media,     ///< the media manager: every medium slot, open a file, save a snapshot
        Machine,
        Cards,     ///< the expansion buses, slots and cards
        Display,
        Sound,
        Network,
        Controls,
        Quit
    };

    struct Row
    {
        QString icon;
        QString label;
        QString value;        ///< shown on the right ("" none)
        bool picker = false;  ///< opens a picker (a chevron on the right)
        std::function<void()> activate;
        /// Left / right change the value (◀▶ on the row) instead of leaving the page
        std::function<void(int)> adjust;
        bool header = false;  ///< a group caption, never focused
        /// A slider row (the Sound page's mixer): the level 0..1 on a track, the live peak under it (-1: none)
        double level = -1;
        double peak = -1;
        bool muted = false;
    };

    struct Slot
    {
        int number = 0;
        bool used = false;
        qint64 time = 0;
        QImage picture;
    };

    struct Picker
    {
        QString title;
        QString warning;
        std::vector<QString> items;
        int row = 0;
        std::function<void(int)> chosen;
    };

    struct SectionInfo
    {
        Page page;
        QString icon;
        QString label;
    };

    Page page() const { return _sections[static_cast<size_t>(_section)].page; }
    std::vector<Row> rows() const;
    void slotRows(std::vector<Row>& list) const;
    void mediaRows(std::vector<Row>& list) const;
    void machineRows(std::vector<Row>& list) const;
    void cardRows(std::vector<Row>& list) const;
    void displayRows(std::vector<Row>& list) const;
    void soundRows(std::vector<Row>& list) const;
    void networkRows(std::vector<Row>& list) const;
    void controlRows(std::vector<Row>& list) const;
    void quitRows(std::vector<Row>& list) const;

    void refreshSlots();
    void clampFocus();
    /// Move the page focus by @p step rows, over the group captions
    void moveRow(int step);
    void openModelPicker();
    void openSpeedPicker();
    void openKeyboardPicker();
    void openCrtPicker();
    void openTemporalPicker();
    void openBorderPicker();
    /// A picker of actions (a medium's Insert / Eject / Save, a card's Change / Remove)
    void openActions(const QString& title, std::vector<std::pair<QString, std::function<void()>>> actions,
                     const QString& warning = {});
    void openMediumActions(const std::string& slot, int kind);
    void openTapeBlocks();
    void openCardPicker(const std::string& slot);
    void openBrowser(HandheldBrowser::Filter filter, const QString& title, std::function<void(const QString&)> chosen);
    void openFile(const QString& path);
    void saveSnapshot();
    void quickSlot(const char* op, int slot);

    /// Submit a catalogue action on the bound instance; @p done runs on the GUI thread with ok and the message
    void submit(const std::string& id, StateNode params, std::function<void(bool, const QString&, const StateNode&)> done = {});
    /// The same with a dial's steps (a Delta action: video.crt_param_by, audio.volume_by)
    void submitDelta(const std::string& id, StateNode params, double delta);
    StateNode readout(const std::string& name) const;
    EmulatorContext* machineContext() const;
    std::optional<bool> toggleOn(const std::string& id, const StateNode& params = StateNode::Object()) const;
    void emitState();

    // Painting
    qreal scale() const;
    void paintSidebar(QPainter& p, const QRectF& area);
    void paintRows(QPainter& p, const QRectF& area);
    void paintSlots(QPainter& p, const QRectF& area);
    /// A mixer row: the icon, the name and the value over a track with the level and the live peak
    void paintSliderRow(QPainter& p, const Row& row, const QRectF& box, bool focused);
    /// The Network page: a monitor, not a list (the card, the virtual network, the traffic, the ports as tiles)
    void paintNetwork(QPainter& p, const QRectF& area);
    void paintFooter(QPainter& p, const QRectF& area);
    void paintPicker(QPainter& p);
    void paintBrowser(QPainter& p);
    void paintIcon(QPainter& p, const QString& name, const QRectF& rect, const QColor& color) const;
    void paintHint(QPainter& p, qreal& x, qreal centerY, const QString& button, const QString& text);

    Hooks _hooks;
    bool _open = false;
    bool _pausedByMenu = false;
    QImage _background;
    std::vector<SectionInfo> _sections;
    int _section = 0;
    bool _inPage = false;  ///< the focus is on the page (right), else on the section list (left)
    int _row = 0;
    qreal _scroll = 0;     ///< the page's scroll (design pixels), keeps the focused row in view
    std::vector<Slot> _slots;
    bool _hasPicker = false;
    Picker _picker;
    bool _hasBrowser = false;
    HandheldBrowser _browser;
    int _browserRow = 0;
    QString _browserTitle;
    std::function<void(const QString&)> _browserChosen;
    QString _status;  ///< the last action's message, under the page
    // Touch: where each painted thing is and what a tap on it does (topmost last), rebuilt by every paint
    struct Hit
    {
        QRectF rect;
        std::function<void(QPointF)> tap;
    };
    void touchAt(QPointF at, int phase);
    void tapSection(int section);
    void tapRow(int index, const QRectF& box, bool slider, QPointF at);
    std::vector<Hit> _hits;
    qreal _hitDragStep = 0;  ///< a drag's travel per row (the shown list's row height)
    QPointF _touchStart;
    QPointF _touchLast;
    bool _touchDragged = false;
    QTimer* _live = nullptr;  ///< repaints the live values (tape position, network counters, peaks) while open
    /// The traffic counter's recent samples (time ms, bytes) and the rates between them, for the Network page's graph
    std::vector<std::pair<qint64, double>> _trafficSamples;
    std::vector<double> _trafficRates;
};
