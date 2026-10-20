#pragma once

/// @file actionregistry.h
/// @brief The host actions every binding source calls by id (debug-and-ttd.md §1.3, §3): pad bindings, and later
/// keyboard shortcuts, menus, toolbars and the Stream Deck. An action has a label, a reason it cannot run now ("needs
/// a pause", "no TTD history") and an executor. Executors run on the GUI thread; most act on a press only, the held
/// ones (run.turbo_hold, mach.reset) see the release too.

#include <QObject>
#include <QString>

#include <functional>
#include <map>
#include <string>
#include <vector>

class ActionRegistry : public QObject
{
    Q_OBJECT

public:
    struct Action
    {
        std::string id;
        QString label;
        QString group;                         ///< "Run", "Time travel", "Debugger", "Capture and machine"
        std::function<QString(bool pressed)> run;  ///< "" on success, else the reason
        std::function<QString()> unavailable;      ///< "" when it can run now; null = always
        bool held = false;                         ///< called on the release too
        /// An analog or encoder action, instead of run: `ttd.scrub_*` get the level (0..1, 0 on the release),
        /// `ttd.jog_*` the signed steps of a turn
        std::function<QString(float value)> amount;
    };

    explicit ActionRegistry(QObject* parent = nullptr);

    void Register(Action action);
    const Action* Find(const std::string& id) const;
    std::vector<const Action*> Actions() const;
    /// Why @p id cannot run now; "" when it can, "unknown action" when there is no such id
    QString Unavailable(const std::string& id) const;
    /// Run @p id; false with the reason (unknown, unavailable, refused by the core verb). @p value: 1 for a press, a
    /// level or a turn's steps (ActionEvent::value)
    bool Trigger(const std::string& id, bool pressed, float value, QString& error);
    bool Trigger(const std::string& id, bool pressed, QString& error)
    {
        return Trigger(id, pressed, pressed ? 1.0f : 0.0f, error);
    }

private:
    std::map<std::string, Action> _actions;
    std::vector<std::string> _order;
};
