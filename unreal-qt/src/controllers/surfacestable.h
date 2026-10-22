#pragma once

/// @file surfacestable.h
/// @brief The surfaces table of the controllers window (Stream Deck integration targeting.md T-2): each control
/// surface session (a Stream Deck, a REST surface), what it is, the instance it drives now, and its pin - follow the
/// focus, a window, or one instance - changed from a combo box.

#include <QPointer>
#include <QString>
#include <string>
#include <QWidget>

class QLabel;
class QTableWidget;
class SurfaceTargets;

class SurfacesTable : public QWidget
{
    Q_OBJECT

public:
    explicit SurfacesTable(SurfaceTargets* targets, QWidget* parent = nullptr);

    /// Rebuild from TargetState (on every change; tests call it directly)
    void refresh();
    /// The table (tests)
    QTableWidget* table() const { return _table; }

private:
    QPointer<SurfaceTargets> _targets;
    QTableWidget* _table = nullptr;
    QLabel* _empty = nullptr;
};

/// "#1 PENTAGON 128K" for an instance id ("no machine" for "")
QString SurfaceInstanceLabel(const std::string& instanceId);
