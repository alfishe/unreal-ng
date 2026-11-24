// Tab bars that scroll (widgets/tabscrollarrows.h): a bar with more tabs than fit scrolls instead of growing, the back
// arrow sits at its left end and the forward one at its right end, each 1.5 times the style's width, and the first tab
// starts right of the back arrow. The wheel over the bar switches to the next or previous visible enabled tab, one per
// notch, a touchpad's small steps adding up to a notch.

#include <gtest/gtest.h>

#include <QApplication>
#include <QImage>
#include <QPixmap>
#include <QLabel>
#include <QStyle>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <vector>

#include "widgets/tabscrollarrows.h"

namespace
{
std::vector<QToolButton*> VisibleArrows(QTabBar* bar)
{
    std::vector<QToolButton*> arrows;
    for (QToolButton* button : bar->findChildren<QToolButton*>(Qt::FindDirectChildrenOnly))
        if (button->isVisible())
            arrows.push_back(button);
    std::sort(arrows.begin(), arrows.end(), [](QToolButton* a, QToolButton* b) { return a->x() < b->x(); });
    return arrows;
}
/// The box of the arrow's ink on the rendered button: pixels darker than the button's own face and frame
QRect ArrowInk(QToolButton* button)
{
    const QImage image = button->grab().toImage();
    const int scale = qRound(button->grab().devicePixelRatio());
    QRect box;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qGray(image.pixel(x, y)) < 110)
                box |= QRect(x / scale, y / scale, 1, 1);
    return box;
}
}  // namespace

TEST(TabScrollArrows, ArrowsSitAtBothEndsAndTheBarDoesNotGrow)
{
    QTabWidget tabs;
    tabs.setDocumentMode(true);
    UseEndScrollArrows(tabs.tabBar());
    for (int i = 0; i < 20; ++i)
        tabs.addTab(new QLabel(QStringLiteral("pane")), QStringLiteral("Long tab name %1").arg(i));
    tabs.resize(320, 200);
    tabs.show();
    QApplication::processEvents();

    QTabBar* bar = tabs.tabBar();
    EXPECT_LT(tabs.minimumSizeHint().width(), 320) << "the bar's minimum width is not every tab together";

    const std::vector<QToolButton*> arrows = VisibleArrows(bar);
    ASSERT_EQ(arrows.size(), 2u);
    QStyleOption option;
    option.initFrom(bar);
    const int expected = QApplication::style()->pixelMetric(QStyle::PM_TabBarScrollButtonWidth, &option, bar) * 3 / 2;
    EXPECT_EQ(arrows[0]->x(), 0);
    EXPECT_EQ(arrows[1]->geometry().right(), bar->width() - 1);
    EXPECT_EQ(arrows[0]->width(), expected);
    EXPECT_EQ(arrows[1]->width(), expected);
    EXPECT_GE(bar->tabRect(0).left(), arrows[0]->geometry().right()) << "the first tab starts right of the back arrow";
    EXPECT_FALSE(arrows[0]->isEnabled()) << "at the start nothing is left to scroll back to";

    // The arrow keeps two thirds of the style's button width, centered on the wider button (both buttons enabled: a
    // disabled arrow is drawn faint)
    bar->setCurrentIndex(10);
    QApplication::processEvents();
    const int side = expected * 2 / 3 * 2 / 3;
    for (QToolButton* button : {arrows[0], arrows[1]})
    {
        const QRect ink = ArrowInk(button);
        ASSERT_FALSE(ink.isEmpty());
        EXPECT_LE(ink.width(), side + 1);
        EXPECT_LE(ink.height(), side + 1);
        EXPECT_NEAR(ink.center().x(), button->rect().center().x(), 2);
        EXPECT_NEAR(ink.center().y(), button->rect().center().y(), 2);
    }

    bar->setCurrentIndex(19);
    QApplication::processEvents();
    EXPECT_LE(bar->tabRect(19).right(), arrows[1]->x()) << "the last tab ends left of the forward arrow";
    EXPECT_TRUE(arrows[0]->isEnabled());

    for (int i = 0; i < 20; ++i)
        EXPECT_EQ(bar->tabText(i), QStringLiteral("Long tab name %1").arg(i));
    EXPECT_EQ(bar->elideMode(), Qt::ElideNone);
}

namespace
{
void Wheel(QTabBar* bar, QPoint angle)
{
    const QPointF at(bar->width() / 2.0, bar->height() / 2.0);
    QWheelEvent event(at, bar->mapToGlobal(at), QPoint(), angle, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                      false);
    QApplication::sendEvent(bar, &event);
}
}  // namespace

TEST(TabScrollArrows, WheelSwitchesToTheNextAndPreviousVisibleTab)
{
    QTabWidget tabs;
    UseWheelToSwitchTabs(tabs.tabBar());
    for (int i = 0; i < 5; ++i)
        tabs.addTab(new QLabel(QStringLiteral("pane")), QStringLiteral("Tab %1").arg(i));
    tabs.setTabVisible(1, false);
    tabs.setTabEnabled(3, false);
    tabs.resize(400, 200);
    tabs.show();
    QApplication::processEvents();
    QTabBar* bar = tabs.tabBar();
    ASSERT_EQ(bar->currentIndex(), 0);

    Wheel(bar, QPoint(0, -120));
    EXPECT_EQ(bar->currentIndex(), 2) << "down: the next tab, the hidden one skipped";
    Wheel(bar, QPoint(0, -120));
    EXPECT_EQ(bar->currentIndex(), 4) << "the disabled one skipped";
    Wheel(bar, QPoint(0, -120));
    EXPECT_EQ(bar->currentIndex(), 4) << "the last tab stays";
    Wheel(bar, QPoint(0, 120));
    EXPECT_EQ(bar->currentIndex(), 2) << "up: the previous tab";

    for (int i = 0; i < 3; ++i)
        Wheel(bar, QPoint(0, 30));
    EXPECT_EQ(bar->currentIndex(), 2) << "a touchpad's small steps short of a notch";
    Wheel(bar, QPoint(0, 30));
    EXPECT_EQ(bar->currentIndex(), 0) << "add up to one";

    Wheel(bar, QPoint(-120, 0));
    EXPECT_EQ(bar->currentIndex(), 2) << "a horizontal swipe right: the next tab";
}

TEST(TabScrollArrows, NamedBaseStyleKeepsTheArrowsAtBothEnds)
{
    QTabWidget tabs;
    UseEndScrollArrows(tabs.tabBar(), "Fusion");
    for (int i = 0; i < 20; ++i)
        tabs.addTab(new QLabel(QStringLiteral("pane")), QStringLiteral("Long tab name %1").arg(i));
    tabs.resize(320, 200);
    tabs.show();
    QApplication::processEvents();

    const std::vector<QToolButton*> arrows = VisibleArrows(tabs.tabBar());
    ASSERT_EQ(arrows.size(), 2u);
    EXPECT_EQ(arrows[0]->x(), 0);
    EXPECT_EQ(arrows[1]->geometry().right(), tabs.tabBar()->width() - 1);
    EXPECT_EQ(arrows[0]->width(), arrows[1]->width());
}
