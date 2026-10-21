#pragma once

/// @file streamdeckpage.h
/// @brief The Stream Deck companion in unreal-qt (docs/inprogress/2026-10-19-streamdeck-integration/daemon.md §7):
///
/// - StreamDeckSupervision attaches the core's supervisor (companion::streamdeck::Supervisor) for the session: the
///   saved settings (QSettings group StreamDeck), the daemon beside this executable (UnrealNG.app/Contents/MacOS,
///   beside unreal-qt.exe, the AppImage's usr/bin; a build tree's bin/), its log in the settings folder, the USB watch
///   (core/controllers CountUsbDevices: Elgato's vendor, matched, never opened) and our pid as its lease. At quit the
///   lease goes back: the daemon ends unless Stream Deck Studio still holds it.
/// - StreamDeckDialog is Tools > Stream Deck...: the daemon's status (state, pid, version, decks, viewers, the link),
///   Start / Stop / Restart, when to start it, how activity shows (Q-3), the pairing (Pair, Approve here, Revoke),
///   each deck's brightness, the surfaces table (Tools > Controllers), the Linux udev rule. It reads and changes the
///   same verbs every automation plane uses (companion::streamdeck::Call); calls that reach the daemon run off the
///   GUI thread.

#include <QObject>
#include <QPointer>
#include <QWidget>

class CompanionController;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTimer;

class StreamDeckSupervision : public QObject
{
    Q_OBJECT

public:
    /// `attach` false in tests (they attach the supervisor themselves)
    explicit StreamDeckSupervision(QObject* parent = nullptr, bool attach = true);
    ~StreamDeckSupervision() override;

    /// unreal-streamdeck beside this executable, or "" when it is not there
    static QString DefaultBinary();
    /// The daemon's log: <settings folder>/streamdeck/daemon.log
    static QString LogFile();

private:
    bool _attached = false;
};

class StreamDeckDialog : public QWidget
{
    Q_OBJECT

public:
    explicit StreamDeckDialog(CompanionController* companions, QWidget* parent = nullptr);

    /// Shown again from the supervisor's status (the timer does it while visible; tests call it)
    void refresh();

    QComboBox* modeBox() const { return _mode; }
    QComboBox* activityBox() const { return _activity; }
    QPushButton* approveButton() const { return _approve; }
    QPushButton* pairButton() const { return _pair; }
    QPushButton* revokeButton() const { return _revoke; }
    QLabel* stateLabel() const { return _state; }
    QLabel* pairingLabel() const { return _pairing; }
    QTableWidget* decksTable() const { return _decks; }

signals:
    /// "Surfaces...": Tools > Controllers, where the surfaces table is
    void surfacesRequested();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    /// A verb on a worker thread (it may wait for the daemon); the answer on the status line
    void callAsync(const QString& verb, const QVariantMap& args);
    void approve();

    QPointer<CompanionController> _companions;
    QLabel* _state = nullptr;
    QLabel* _details = nullptr;
    QLabel* _pairing = nullptr;
    QLabel* _message = nullptr;
    QComboBox* _mode = nullptr;
    QComboBox* _activity = nullptr;
    QPushButton* _start = nullptr;
    QPushButton* _stop = nullptr;
    QPushButton* _restart = nullptr;
    QPushButton* _pair = nullptr;
    QPushButton* _approve = nullptr;
    QPushButton* _revoke = nullptr;
    QTableWidget* _decks = nullptr;
    QPlainTextEdit* _udev = nullptr;
    QTimer* _timer = nullptr;
    QString _pendingPairing;  ///< the daemon's pairing waiting for the user (its id at the registry)
    QString _decksShown;      ///< the deck list the table shows (rebuilt when it changes)
};
