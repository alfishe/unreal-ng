#include "actionregistry.h"

ActionRegistry::ActionRegistry(QObject* parent) : QObject(parent) {}

void ActionRegistry::Register(Action action)
{
    const std::string id = action.id;
    if (_actions.find(id) == _actions.end())
        _order.push_back(id);
    _actions[id] = std::move(action);
}

const ActionRegistry::Action* ActionRegistry::Find(const std::string& id) const
{
    auto it = _actions.find(id);
    return it != _actions.end() ? &it->second : nullptr;
}

std::vector<const ActionRegistry::Action*> ActionRegistry::Actions() const
{
    std::vector<const Action*> out;
    for (const std::string& id : _order)
        out.push_back(&_actions.at(id));
    return out;
}

QString ActionRegistry::Unavailable(const std::string& id) const
{
    const Action* action = Find(id);
    if (action == nullptr)
        return tr("unknown action");
    return action->unavailable ? action->unavailable() : QString();
}

bool ActionRegistry::Trigger(const std::string& id, bool pressed, float value, QString& error)
{
    const Action* action = Find(id);
    if (action == nullptr)
    {
        error = tr("%1: unknown action").arg(QString::fromStdString(id));
        return false;
    }
    if (action->amount)
    {
        // The release always arrives: a scrub stops on it
        if (pressed)
            if (const QString why = Unavailable(id); !why.isEmpty())
            {
                error = tr("%1: %2").arg(action->label, why);
                return false;
            }
        const QString why = action->amount(pressed ? value : 0.0f);
        if (!why.isEmpty())
        {
            error = tr("%1: %2").arg(action->label, why);
            return false;
        }
        return true;
    }
    if (!pressed && !action->held)
        return true;
    if (pressed)
        if (const QString why = Unavailable(id); !why.isEmpty())
        {
            error = tr("%1: %2").arg(action->label, why);
            return false;
        }
    const QString why = action->run ? action->run(pressed) : QString();
    if (!why.isEmpty())
    {
        error = tr("%1: %2").arg(action->label, why);
        return false;
    }
    return true;
}
