#include "guicontrol/node.h"

#include "guicontrol/host.h"

#include <algorithm>

namespace guicontrol
{

Node::Node(Host* host, Node* parent, QString id) : _host(host), _parent(parent), _id(std::move(id))
{
}

Node::~Node() = default;

QString Node::Path() const
{
    if (_parent == nullptr)
        return {};
    const QString above = _parent->Path();
    return above.isEmpty() ? _id : above + '/' + _id;
}

int Node::Depth() const
{
    int depth = 0;
    for (const Node* node = _parent; node != nullptr; node = node->_parent)
        ++depth;
    return depth;
}

Node& Node::AddChild(const QString& id)
{
    if (Node* existing = Child(id))
        return *existing;
    _children.push_back(std::unique_ptr<Node>(new Node(_host, this, id)));
    Node& child = *_children.back();
    if (_host != nullptr)
        _host->NodeAdded(child);
    return child;
}

void Node::RemoveChild(const QString& id)
{
    const auto it = std::find_if(_children.begin(), _children.end(),
                                 [&id](const std::unique_ptr<Node>& child) { return child->_id == id; });
    if (it == _children.end())
        return;
    const QString path = (*it)->Path();
    std::unique_ptr<Node> gone = std::move(*it);
    _children.erase(it);
    if (_host != nullptr)
        _host->NodeRemoved(path);
}

Node* Node::Child(const QString& id) const
{
    for (const std::unique_ptr<Node>& child : _children)
        if (child->_id == id)
            return child.get();
    return nullptr;
}

Node* Node::Find(const QString& path)
{
    Node* node = this;
    for (const QString& part : path.split('/', Qt::SkipEmptyParts))
    {
        node = node->Child(part);
        if (node == nullptr)
            return nullptr;
    }
    return node;
}

void Node::AddInterface(const QString& name)
{
    if (_interfaces.contains(name))
        return;
    _interfaces.append(name);
    Changed();
}

void Node::SetCommand(const QString& method, SyncHandler handler)
{
    _commands[method] = [handler = std::move(handler)](const Call& call, Done done) { done(handler(call)); };
}

void Node::SetAsyncCommand(const QString& method, AsyncHandler handler)
{
    _commands[method] = std::move(handler);
}

QStringList Node::Commands() const
{
    QStringList methods;
    for (const auto& [method, handler] : _commands)
        methods.append(method);
    return methods;
}

void Node::Invoke(const Call& call, Done done) const
{
    const auto it = _commands.find(call.method);
    if (it == _commands.end())
    {
        const QString where = Path().isEmpty() ? QStringLiteral("the host") : Path();
        done(Result::Failure(errors::kNotFound, QStringLiteral("%1 has no command %2").arg(where, call.method)));
        return;
    }
    it->second(call, std::move(done));
}

void Node::SetState(const QString& key, const QJsonValue& value)
{
    if (_state.value(key) == value && _state.contains(key))
        return;
    _state[key] = value;
    Changed();
}

void Node::SetState(const QJsonObject& fields)
{
    bool changed = false;
    for (auto it = fields.begin(); it != fields.end(); ++it)
        if (!_state.contains(it.key()) || _state.value(it.key()) != it.value())
        {
            _state[it.key()] = it.value();
            changed = true;
        }
    if (changed)
        Changed();
}

void Node::SetInstance(const QString& endpoint, const QString& id)
{
    const QJsonObject instance = id.isEmpty() ? QJsonObject() : QJsonObject{{"endpoint", endpoint}, {"id", id}};
    if (instance == _instance)
        return;
    _instance = instance;
    Changed();
}

QJsonObject Node::Describe() const
{
    QJsonObject json{{"id", _id}, {"interfaces", QJsonArray::fromStringList(_interfaces)},
                     {"commands", QJsonArray::fromStringList(Commands())}, {"state", _state}};
    if (!_instance.isEmpty())
        json["instance"] = _instance;
    if (!_children.empty())
    {
        QJsonArray children;
        for (const std::unique_ptr<Node>& child : _children)
            children.append(child->Describe());
        json[Depth() == 0 ? "windows" : "views"] = children;
    }
    return json;
}

void Node::Changed()
{
    if (_host != nullptr)
        _host->NodeChanged(*this);
}

}  // namespace guicontrol
