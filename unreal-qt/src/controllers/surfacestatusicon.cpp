#include "surfacestatusicon.h"

#include <QCursor>
#include <QMenu>

#include "controllers/surfacestable.h"
#include "controllers/surfacetargets.h"

SurfaceStatusIcon::SurfaceStatusIcon(SurfaceTargets* targets, QWidget* parent)
    : StatusIndicator(QStringLiteral("surface"), tr("Control surfaces"), parent), _targets(targets)
{
    setBlinking(false);  // a state LED: lit while a surface drives this window
    setCursor(Qt::PointingHandCursor);
    connect(this, &StatusIndicator::clicked, this, &SurfaceStatusIcon::showMenu);
    if (_targets)
        connect(_targets, &SurfaceTargets::changed, this, &SurfaceStatusIcon::refresh);
    refresh();
}

void SurfaceStatusIcon::refresh()
{
    if (!_targets)
    {
        hide();
        return;
    }
    const std::vector<actions::SurfaceSessionInfo> all = _targets->sessions();
    const std::vector<actions::SurfaceSessionInfo> here = _targets->sessionsHere();
    setVisible(!all.empty());
    setActive(!here.empty());
    QStringList lines;
    for (const actions::SurfaceSessionInfo& session : here)
        lines << tr("%1 drives this window (%2)")
                     .arg(QString::fromStdString(session.spec.name),
                          session.pin.kind == actions::Pin::Kind::Follow ? tr("following the focus") : tr("pinned"));
    if (here.empty())
        lines << tr("No surface drives this window (click to pin one here)");
    setLiveToolTip(lines.join(QLatin1Char('\n')));
}

QMenu* SurfaceStatusIcon::buildMenu(QWidget* parent)
{
    auto* menu = new QMenu(parent);
    if (!_targets)
        return menu;
    const std::string view = _targets->viewId().toStdString();
    for (const actions::SurfaceSessionInfo& session : _targets->sessions())
    {
        const QString id = QString::fromStdString(session.id);
        const bool here = session.target.view == view;
        const bool pinnedHere = session.pin.kind == actions::Pin::Kind::View && session.pin.id == view;
        QMenu* sub = menu->addMenu(QStringLiteral("%1 %2 - %3")
                                       .arg(here ? QStringLiteral("*") : QStringLiteral(" "),
                                            QString::fromStdString(session.spec.name),
                                            SurfaceInstanceLabel(session.target.instance)));
        QAction* pin = sub->addAction(tr("Pin here"));
        pin->setEnabled(!pinnedHere);
        connect(pin, &QAction::triggered, this, [this, id] {
            if (_targets)
                _targets->pinHere(id);
        });
        QAction* unpin = sub->addAction(tr("Unpin (follow the focus)"));
        unpin->setEnabled(session.pin.kind != actions::Pin::Kind::Follow);
        connect(unpin, &QAction::triggered, this, [this, id] {
            if (_targets)
                _targets->unpin(id);
        });
    }
    return menu;
}

void SurfaceStatusIcon::showMenu()
{
    QMenu* menu = buildMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(QCursor::pos());
}
