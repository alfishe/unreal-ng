// The drop zones (media/droptargetoverlay.h) open after a 1.5 s hold, under a cursor held still: the tile that lands
// there (drive D in the middle of four) used to take the drop, and a disk meant to autostart went to D. Held still, a
// drop goes where a quick drop would (the rest tile, drive A); a tile is picked once the cursor moves to it

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QWidget>

#include "media/droptargetoverlay.h"

namespace
{
MediaPlan FourDrives()
{
    MediaPlan plan;
    for (const char* slot : {"fdd.a", "fdd.b", "fdd.c", "fdd.d"})
    {
        MediaTarget target;
        target.as = FileKind::Floppy;
        target.slotId = slot;
        target.autostart = std::string(slot) == "fdd.a";
        plan.targets.push_back(target);
    }
    return plan;
}

class DropTargetOverlay_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        overlay = new DropTargetOverlay(&owner);
        mime.setUrls({QUrl::fromLocalFile("/disk/game.trd")});
        QObject::connect(overlay, &DropTargetOverlay::targetChosen, [this](int index) { chosen = index; });
        QObject::connect(overlay, &DropTargetOverlay::droppedOutside, [this]() { outside++; });
    }

    void Enter(const QPoint& at)
    {
        QDragEnterEvent event(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(overlay, &event);
    }
    void Move(const QPoint& at)
    {
        QDragMoveEvent event(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(overlay, &event);
    }
    void Drop(const QPoint& at)
    {
        QDropEvent event(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(overlay, &event);
    }
    QPoint Centre(int index) const { return overlay->tileRect(index).center(); }

    QWidget owner;
    DropTargetOverlay* overlay = nullptr;
    QMimeData mime;
    int chosen = -1;
    int outside = 0;
};
}  // namespace

TEST_F(DropTargetOverlay_Test, HeldStillOverATileTheDropGoesToTheRestTile)
{
    overlay->showZones(QRect(100, 100, 800, 600), "game.trd", FourDrives(), 0);
    ASSERT_FALSE(overlay->tileRect(3).isEmpty());
    const QPoint d = Centre(3);
    Enter(d);  // the zones came up under the resting cursor, over drive D
    EXPECT_EQ(overlay->hovered(), 0) << "drive A glows: that is where the drop goes";
    Move(d + QPoint(3, -2));  // the hand's tremble
    Move(d + QPoint(-4, 5));
    EXPECT_EQ(overlay->hovered(), 0);
    Drop(d + QPoint(2, 2));
    EXPECT_EQ(chosen, 0) << "not drive D, the tile that happened to open under the cursor";
}

TEST_F(DropTargetOverlay_Test, MovedToATileTheDropGoesThere)
{
    overlay->showZones(QRect(100, 100, 800, 600), "game.trd", FourDrives(), 0);
    Enter(Centre(3));
    Move(Centre(1));
    EXPECT_EQ(overlay->hovered(), 1);
    Drop(Centre(1));
    EXPECT_EQ(chosen, 1);
}

TEST_F(DropTargetOverlay_Test, OnceMovedTheTileUnderTheRestingPlaceCanBePickedToo)
{
    overlay->showZones(QRect(100, 100, 800, 600), "game.trd", FourDrives(), 0);
    Enter(Centre(3));
    Move(Centre(2));
    Move(Centre(3));  // back: now a pick
    EXPECT_EQ(overlay->hovered(), 3);
    Drop(Centre(3));
    EXPECT_EQ(chosen, 3);
}

TEST_F(DropTargetOverlay_Test, MovedAwayFromTheTilesTheChooserOpens)
{
    overlay->showZones(QRect(100, 100, 800, 600), "game.trd", FourDrives(), 0);
    Enter(Centre(3));
    Move(QPoint(2, 2));  // a corner, no tile there
    EXPECT_EQ(overlay->hovered(), -1);
    Drop(QPoint(2, 2));
    EXPECT_EQ(chosen, -1);
    EXPECT_EQ(outside, 1);
}

TEST_F(DropTargetOverlay_Test, WithoutARestTileTheTileUnderTheCursorTakesTheDrop)
{
    overlay->showZones(QRect(100, 100, 800, 600), "game.trd", FourDrives());
    Enter(Centre(3));
    Drop(Centre(3));
    EXPECT_EQ(chosen, 3);
}
