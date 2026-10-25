#pragma once

/// @file learnwizard.h
/// @brief The learn wizard (docs/inprogress/2026-10-17-controller-manager/ui-ux.md §3.5): for a device the database
/// does not know, "Press Up…", "Press South (bottom face button)…" one canonical control after the other, with Back,
/// Skip, Finish and Cancel. A view over the `learn` host verb: Start begins the session, the device's own presses
/// answer the prompts (the service routes them to the session, nothing else gets them), a 30 Hz `status` read follows
/// it, Finish saves the map as the user family `user-<vid><pid>` and shows its gamecontrollerdb line for Export. A
/// session another plane ends (REST, CLI, Python `cancel` / `finish`) closes the wizard; closing the wizard cancels
/// a session still running. The same session drives the wizard whether its steps come from the pad, from this window
/// or from a script (`learn sample`).

#include <QDialog>

#include <functional>
#include <map>
#include <string>

#ifdef ENABLE_CONTROLLERS
#include "hostinputcontrol.h"
#endif

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTimer;

class LearnWizard : public QDialog
{
    Q_OBJECT

public:
#ifdef ENABLE_CONTROLLERS
    /// The host verb the wizard runs (HostControllers::Host in the client, a HostInputControl in the tests)
    using Call = std::function<controllers::HostReply(const controllers::HostRequest&)>;
    LearnWizard(const std::string& device, const QString& name, Call call, QWidget* parent = nullptr);
#endif
    ~LearnWizard() override;

    /// Begin the session (`learn start`, the default controls); false with the reason in the status line. A session
    /// already running for the device (started by a script) is followed, not restarted
    bool start();
    /// One `learn status` read: the prompt, the progress, the map so far (the timer calls it)
    void refresh();
    /// Finish: maps and saves; the answer in the status line and the gamecontrollerdb line in the export box
    bool finish();
    void skip();
    void back();

    const std::string& device() const
    {
        return _device;
    }
    bool learning() const
    {
        return _learning;
    }
    QString promptText() const;
    QString progressText() const;
    QString statusText() const;
    /// "dpad.up = lstick.y-" per learned control, in the order asked
    QStringList mapLines() const;
    QString exportLine() const;

signals:
    /// Finish saved or applied the map: the answer, for the status line and the toast
    void learned(const QString& message);

protected:
    void reject() override;

private:
#ifdef ENABLE_CONTROLLERS
    controllers::HostReply step(const std::string& name, std::map<std::string, std::string> options = {});
    void display(const controllers::HostReply& reply);
#endif
    void setStatus(const QString& text, bool error);
    void ended(const QString& text);

    std::string _device;
#ifdef ENABLE_CONTROLLERS
    Call _call;
#endif
    bool _learning = false;
    std::string _shown;  ///< the last status read's dump: the widgets change only when it does
    QLabel* _progress = nullptr;
    QLabel* _prompt = nullptr;
    QLabel* _release = nullptr;
    QListWidget* _map = nullptr;
    QLineEdit* _name = nullptr;
    QCheckBox* _save = nullptr;
    QLineEdit* _export = nullptr;
    QLabel* _status = nullptr;
    QPushButton* _back = nullptr;
    QPushButton* _skip = nullptr;
    QPushButton* _finish = nullptr;
    QPushButton* _cancel = nullptr;
    QTimer* _poll = nullptr;
};
