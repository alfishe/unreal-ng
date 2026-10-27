#include "surfacetargets.h"

#include <QGuiApplication>
#include <QMetaObject>
#include <QWindow>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

SurfaceTargets::SurfaceTargets(EmulatorBinding* binding, QString name, QObject* parent)
    : QObject(parent), _binding(binding)
{
    _view = actions::TargetState::Instance().RegisterView(name.toStdString(), "unreal-qt");
    connect(_binding, &EmulatorBinding::bound, this, &SurfaceTargets::onBound);
    connect(_binding, &EmulatorBinding::unbound, this, &SurfaceTargets::onUnbound);
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        connect(app, &QGuiApplication::focusWindowChanged, this, &SurfaceTargets::onFocusWindowChanged);
    _observer = MessageCenter::DefaultMessageCenter().AddObserver(NC_SURFACE_TARGET, [this](int, Message*) {
        QMetaObject::invokeMethod(this, [this] { emit changed(); }, Qt::QueuedConnection);
    });
    if (_binding->emulator() != nullptr)
        onBound();
    // The first window of the host is in front until another one takes the focus
    actions::TargetState::Instance().Activated(_view);
}

SurfaceTargets::~SurfaceTargets()
{
    MessageCenter::DefaultMessageCenter().RemoveObserverById(NC_SURFACE_TARGET, _observer);
    _view.Reset();  // the view closes: `view:` pins on it fall back to following
}

QString SurfaceTargets::viewId() const
{
    return QString::fromStdString(_view.Id());
}

std::vector<actions::SurfaceSessionInfo> SurfaceTargets::sessions() const
{
    return actions::TargetState::Instance().Sessions();
}

std::vector<actions::SurfaceSessionInfo> SurfaceTargets::sessionsHere() const
{
    std::vector<actions::SurfaceSessionInfo> here;
    for (actions::SurfaceSessionInfo& session : sessions())
        if (session.target.view == _view.Id())
            here.push_back(std::move(session));
    return here;
}

std::vector<actions::ViewInfo> SurfaceTargets::views() const
{
    return actions::TargetState::Instance().Views();
}

QString SurfaceTargets::pin(const QString& session, const actions::Pin& pin)
{
    std::string error;
    std::string message;
    if (actions::TargetState::Instance().SetPin(session.toStdString(), pin, error, message))
        return {};
    return QString::fromStdString(message.empty() ? error : message);
}

QString SurfaceTargets::pinHere(const QString& session)
{
    return pin(session, actions::Pin::OnView(_view.Id()));
}

QString SurfaceTargets::unpin(const QString& session)
{
    return pin(session, actions::Pin::Follow());
}

void SurfaceTargets::activated()
{
    actions::TargetState::Instance().Activated(_view);
}

void SurfaceTargets::onBound()
{
    Emulator* emulator = _binding->emulator();
    actions::TargetState::Instance().SetActive(_view, emulator != nullptr ? emulator->GetId() : std::string());
}

void SurfaceTargets::onUnbound()
{
    actions::TargetState::Instance().SetActive(_view, std::string());
}

void SurfaceTargets::onFocusWindowChanged(QWindow* window)
{
    // Null: the focus went to another program - the order stays (targeting.md §3 rule 2). Any window of this process
    // belongs to this view while the process has one main window (the debugger and the tool windows show its instance)
    if (window != nullptr)
        activated();
}
