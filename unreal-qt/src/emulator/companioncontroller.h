#pragma once

#include <QCheckBox>
#include <QDialog>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>
#include <cstdint>

class QLabel;
class QPushButton;
class QTableWidget;

/// @brief The GUI side of companion clients (docs/inprogress/2026-10-16-companion-client): devices that act as the
/// user's own hands. The GUI is the only place a pairing is approved.
///
/// - At start it keeps the paired companions in the user's configuration (companions.json) and tells the registry an
///   approver is present; at exit it writes the store and withdraws.
/// - A pairing request opens a non-modal prompt (CompanionPairingPrompt): the device's name and description, its
///   address and the code the device shows, the scopes to grant. Allow pairs it; Deny or closing refuses it; the
///   prompt closes by itself when the device cancels or the request expires.
/// - Tools > Companions... lists the companions with Revoke and Revoke all (CompanionsDialog), and has "Accept pairing
///   requests": off, a request is refused (pairing_disabled) and the waiting ones are denied; kept in the settings.
/// - It asks for HUD toasts: paired, revoked.
/// A companion holds no session, so the remote control guard has nothing to show or lock for it.
class CompanionController : public QObject
{
    Q_OBJECT

public:
    /// `dialogParent`: the window prompts and the dialog belong to. `attachStore`: keep companions in the user's
    /// configuration and act as the approver (false in tests, which set up the registry themselves)
    explicit CompanionController(QWidget* dialogParent, bool attachStore = true);
    ~CompanionController() override;

    /// Tools > Companions...
    void showCompanionsDialog();

    /// The open prompt of a pairing (tests)
    class CompanionPairingPrompt* prompt(const QString& pairingId) const;
    class CompanionsDialog* dialog() const { return _dialog; }

public slots:
    /// An NC_COMPANION_CHANGED event, on the GUI thread
    void handleEvent(const QString& event, const QString& pairingId, const QString& companionId, const QString& name);

signals:
    void toastRequested(const QString& title, const QString& body);

private:
    QWidget* _dialogParent = nullptr;
    bool _attached = false;
    uint64_t _observer = 0;
    QHash<QString, QPointer<CompanionPairingPrompt>> _prompts;
    QPointer<CompanionsDialog> _dialog;
};

/// @brief "Pair companion 'Desk panel'?": the request, the code to compare, the scopes to grant, Allow / Deny
class CompanionPairingPrompt : public QDialog
{
    Q_OBJECT

public:
    CompanionPairingPrompt(const QString& pairingId, QWidget* parent = nullptr);

    QString pairingId() const { return _pairingId; }
    QString code() const { return _code; }
    /// The scopes ticked now
    uint32_t scopes() const;
    QCheckBox* scopeBox(const QString& name) const;
    QPushButton* allowButton() const { return _allow; }
    QPushButton* denyButton() const { return _deny; }

    /// The request is gone (cancelled, expired, decided elsewhere): close without a decision
    void dismiss();

private:
    QString _pairingId;
    QString _code;
    QList<QCheckBox*> _boxes;
    QPushButton* _allow = nullptr;
    QPushButton* _deny = nullptr;
    bool _decided = false;
};

/// @brief Tools > Companions...: the paired companions, Revoke, Revoke all, Accept pairing requests
class CompanionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CompanionsDialog(QWidget* parent = nullptr);

    QTableWidget* table() const { return _table; }
    QCheckBox* acceptPairingBox() const { return _acceptPairing; }

public slots:
    void refresh();

signals:
    /// The user ticked or unticked "Accept pairing requests" (the registry has it already)
    void pairingAllowedChanged(bool allowed);

private:
    QTableWidget* _table = nullptr;
    QCheckBox* _acceptPairing = nullptr;
    QLabel* _note = nullptr;
    QPushButton* _revoke = nullptr;
    QPushButton* _revokeAll = nullptr;
};
