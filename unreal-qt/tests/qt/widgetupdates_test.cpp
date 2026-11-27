// The debugger's set-when-changed helpers (gs-debugger design §7.3): QuietScrollBar repaints on a value change only
// when its handle moves (a disassembly following PC moves its 64K bar by a few bytes per step). Runtime: a few ms

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <gtest/gtest.h>

#include <functional>

#include "debugger/widgetupdates.h"

namespace
{
class PaintCount : public QObject
{
public:
    int paints = 0;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Paint)
            paints++;
        return QObject::eventFilter(watched, event);
    }
};

bool PumpUntil(const std::function<bool()>& done)
{
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < 1000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    return done();
}

void PumpIdle()
{
    for (int i = 0; i < 5; i++)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

class QuietScrollBar_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _bar.setRange(0, 0xFFFF);
        _bar.setPageStep(64);
        _bar.setValue(0x8000);
        _bar.resize(16, 400);
        _bar.installEventFilter(&_count);
        _bar.show();
        ASSERT_TRUE(PumpUntil([&] { return _count.paints > 0; })) << "the bar was never painted";
        PumpIdle();
        _count.paints = 0;
    }

    widgetupdates::QuietScrollBar _bar{Qt::Vertical};
    PaintCount _count;
};
}  // namespace

TEST_F(QuietScrollBar_Test, AStepThatLeavesTheHandleInPlacePaintsNothing)
{
    _bar.setValue(0x8003);  // three bytes of 64K: well under a pixel of a 400 px bar
    PumpIdle();
    EXPECT_EQ(_count.paints, 0);
    EXPECT_EQ(_bar.value(), 0x8003);
}

TEST_F(QuietScrollBar_Test, AMoveOfTheHandlePaints)
{
    _bar.setValue(0xC000);
    EXPECT_TRUE(PumpUntil([&] { return _count.paints > 0; })) << "the moved handle was never painted";
}

TEST_F(QuietScrollBar_Test, TheEndsPaint)
{
    _bar.setValue(0xFFFF);
    ASSERT_TRUE(PumpUntil([&] { return _count.paints > 0; }));
    PumpIdle();
    _count.paints = 0;
    _bar.setValue(0xFFFE);  // the handle stays, the down arrow comes back
    EXPECT_TRUE(PumpUntil([&] { return _count.paints > 0; })) << "leaving the end was never painted";
}
