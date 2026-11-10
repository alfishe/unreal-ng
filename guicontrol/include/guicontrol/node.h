#pragma once

/// @file node.h
/// @brief One node of a host's tree (design §4.2): the host itself (the root), a window, a view. A node has
/// interfaces, commands, state and, for a view, the emulator instance it shows. Nodes live on the host's thread.

#include "guicontrol/protocol.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>
#include <vector>

namespace guicontrol
{

class Host;

class Node
{
public:
    /// A handler that answers at once
    using SyncHandler = std::function<Result(const Call& call)>;
    /// A handler that answers through `done`, now or later (on the host's thread)
    using AsyncHandler = std::function<void(const Call& call, Done done)>;

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    ~Node();

    const QString& Id() const { return _id; }
    /// "debugger/memory"; "" for the root
    QString Path() const;
    Node* Parent() const { return _parent; }
    /// 0 for the root, 1 for a window, 2 for a view
    int Depth() const;

    /// region <Tree>
    /// The child `id`, created if absent (the `nodes` topic tells the subscribers)
    Node& AddChild(const QString& id);
    /// The child and everything under it go (the `nodes` topic; focus records on them are dropped)
    void RemoveChild(const QString& id);
    Node* Child(const QString& id) const;
    const std::vector<std::unique_ptr<Node>>& Children() const { return _children; }
    /// The node at `path` under this one ("debugger/memory"); this node for ""; null when absent
    Node* Find(const QString& path);
    /// endregion </Tree>

    /// region <Interfaces and commands>
    void AddInterface(const QString& name);
    const QStringList& Interfaces() const { return _interfaces; }
    bool Implements(const QString& name) const { return _interfaces.contains(name); }
    void SetCommand(const QString& method, SyncHandler handler);
    void SetAsyncCommand(const QString& method, AsyncHandler handler);
    bool HasCommand(const QString& method) const { return _commands.count(method) != 0; }
    QStringList Commands() const;
    /// Runs `method` here; `done` gets not_found when the node has no such command
    void Invoke(const Call& call, Done done) const;
    /// endregion </Interfaces and commands>

    /// region <State>
    /// One field; a change is pushed (`state` topic, coalesced)
    void SetState(const QString& key, const QJsonValue& value);
    /// Several fields at once (one push)
    void SetState(const QJsonObject& fields);
    const QJsonObject& State() const { return _state; }
    /// The emulator instance the node shows (design R6): its endpoint ("http://127.0.0.1:8090") and id; "" clears it
    void SetInstance(const QString& endpoint, const QString& id);
    QString InstanceId() const { return _instance.value("id").toString(); }
    const QJsonObject& Instance() const { return _instance; }
    /// endregion </State>

    /// The node and its children as `list` shows them (id, interfaces, commands, state, instance, children)
    QJsonObject Describe() const;

private:
    friend class Host;
    Node(Host* host, Node* parent, QString id);
    void Changed();

    Host* _host = nullptr;
    Node* _parent = nullptr;
    QString _id;
    QStringList _interfaces;
    std::map<QString, AsyncHandler> _commands;
    QJsonObject _state;
    QJsonObject _instance;
    std::vector<std::unique_ptr<Node>> _children;
};

}  // namespace guicontrol
