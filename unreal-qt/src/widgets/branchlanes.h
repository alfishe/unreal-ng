#pragma once

/// @file branchlanes.h
/// @brief The TTD panel's Branches row: one lane per branch over a common frame axis (TTD branches, surfaces.md §1).

#include <QColor>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <cstdint>
#include <functional>
#include <vector>

/// One lane per branch in tree order (a branch under its parent, children by fork position): the name on the
/// left, a bar from the fork to the end, a connector up to the parent's lane at the fork. The active lineage is
/// drawn strong, the active branch's name bold, the moment the machine stands on as a line. A click selects a
/// lane, a double click activates it. Every lane but the trunk's ends in a cross that deletes the branch (the trunk
/// goes only with the whole history, Clear)
class BranchLanes : public QWidget
{
public:
    struct Lane
    {
        uint32_t id = 0;
        uint32_t parent = 0xFFFFFFFFu;   ///< none for the trunk
        QString name;
        uint64_t fork = 0;   ///< frame; the trunk's start for the trunk
        uint64_t end = 0;    ///< frame
        bool active = false;
        bool onLineage = false;
        bool pinned = false;
        QString tip;         ///< created, by, reason, length, tags, note
        int depth = 0;       ///< filled by setLanes (tree order)
    };

    explicit BranchLanes(QWidget* parent = nullptr);

    /// The lanes (any order: they are put in tree order), the frame axis and the current moment
    void setLanes(std::vector<Lane> lanes, uint64_t axisFrom, uint64_t axisTo, uint64_t current);
    const std::vector<Lane>& lanes() const { return _lanes; }

    uint32_t selected() const { return _selected; }
    void setSelected(uint32_t id);
    /// The lane under @p y (widget pixels), or -1
    int laneAt(int y) const;
    /// The cross of lane @p row (empty for the trunk's lane)
    QRect closeRect(int row) const;
    /// Whether lane @p row can be deleted from its cross (every lane but the trunk's)
    bool closable(int row) const;
    /// The color of a branch's lane (the slider tints the lineage's parts with it)
    static QColor ColorOf(uint32_t id);

    static constexpr int kRowHeight = 18;
    static constexpr uint32_t kNone = 0xFFFFFFFFu;

    std::function<void(uint32_t)> onSelected;
    std::function<void(uint32_t)> onActivated;
    std::function<void(uint32_t)> onDeleteRequested;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool event(QEvent* event) override;

private:
    int NameWidth() const;
    int XOf(uint64_t frame) const;

    std::vector<Lane> _lanes;
    uint64_t _from = 0;
    uint64_t _to = 1;
    uint64_t _current = 0;
    uint32_t _selected = kNone;
    int _hoverClose = -1;   ///< the row whose cross the mouse is over
};
