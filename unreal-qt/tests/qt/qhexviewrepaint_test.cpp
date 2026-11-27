// The debugger's memory view (QHexView, vendored) at a stop (gs-debugger design §7.3): the changes made between
// beginChanges() and endChanges() (a new document, the cursor, the metadata) repaint only the rows they change; a
// scroll outside a batch still repaints the whole view. Runtime: a few ms each

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QPaintEvent>
#include <QRegion>
#include <QScrollBar>
#include <gtest/gtest.h>

#include <functional>

#include <QHexView/model/buffer/qmemorybuffer.h>
#include <QHexView/qhexview.h>

namespace
{
/// The regions the view's viewport painted
class PaintSpy : public QObject
{
public:
    QRegion painted;
    int paints = 0;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Paint)
        {
            painted += static_cast<QPaintEvent*>(event)->region();
            paints++;
        }
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

QByteArray Bytes()
{
    QByteArray data(4096, '\0');
    for (int i = 0; i < data.size(); i++)
        data[i] = static_cast<char>(i * 7);
    return data;
}

class QHexViewRepaint_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _view.resize(640, 480);
        _view.setDocument(QHexDocument::fromMemory<QMemoryBuffer>(Bytes(), &_view));
        _view.gotoOffset(0x800);
        _view.viewport()->installEventFilter(&_spy);
        _view.show();
        ASSERT_TRUE(PumpUntil([&] { return _spy.paints > 0; })) << "the view was never painted";
        PumpIdle();
        _spy.painted = QRegion();
        _spy.paints = 0;
    }

    /// A debugger stop: the page again as a new document (one byte changed), the cursor on the next byte
    void Stop(int changedOffset, quint64 cursor)
    {
        QByteArray data = Bytes();
        data[changedOffset] = static_cast<char>(data[changedOffset] ^ 0xFF);
        _view.beginChanges();
        QHexDocument* previous = _view.getDocument();
        _view.setDocument(QHexDocument::fromMemory<QMemoryBuffer>(data, &_view));
        delete previous;
        _view.gotoOffset(cursor);
        _view.endChanges();
    }

    bool WholeViewPainted() const { return (QRegion(_view.viewport()->rect()) - _spy.painted).isEmpty(); }

    int PaintedHeight() const
    {
        int height = 0;
        for (const QRect& rect : _spy.painted)
            height += rect.height();
        return height;
    }

    QHexView _view;
    PaintSpy _spy;
};
}  // namespace

TEST_F(QHexViewRepaint_Test, AStopRepaintsOnlyTheRowsItChanged)
{
    const int scroll = _view.verticalScrollBar()->value();
    Stop(0x800 + 16 * 3, 0x801);  // a byte three rows below the cursor's, the cursor a byte on
    ASSERT_TRUE(PumpUntil([&] { return _spy.paints > 0; })) << "the stop's changes were never painted";
    PumpIdle();
    EXPECT_EQ(_view.verticalScrollBar()->value(), scroll) << "the cursor's row stays in the middle";
    EXPECT_FALSE(WholeViewPainted());
    EXPECT_GT(PaintedHeight(), 0);
    // The header (the cursor's column), the cursor's row and the changed byte's row: not the 20 rows of the view
    EXPECT_LT(PaintedHeight(), _view.viewport()->height() / 3) << "painted " << PaintedHeight() << " px rows";
}

TEST_F(QHexViewRepaint_Test, AStopWithNothingChangedPaintsNothing)
{
    // The same bytes, the cursor where it was
    _view.beginChanges();
    QHexDocument* previous = _view.getDocument();
    QByteArray data = Bytes();
    _view.setDocument(QHexDocument::fromMemory<QMemoryBuffer>(data, &_view));
    delete previous;
    _view.gotoOffset(0x800);
    _view.endChanges();
    PumpIdle();
    EXPECT_EQ(_spy.paints, 0) << "painted " << PaintedHeight() << " px rows";
}

TEST_F(QHexViewRepaint_Test, AStopThatScrollsARowMovesThePixelsAndPaintsTheNewRow)
{
    const int scroll = _view.verticalScrollBar()->value();
    Stop(0, 0x810);  // the cursor a row down: the view scrolls a row to keep it in the middle
    ASSERT_TRUE(PumpUntil([&] { return _spy.paints > 0; }));
    PumpIdle();
    EXPECT_EQ(_view.verticalScrollBar()->value(), scroll + 1);
    EXPECT_FALSE(WholeViewPainted());
    EXPECT_LT(PaintedHeight(), _view.viewport()->height() / 3) << "painted " << PaintedHeight() << " px rows";
}

TEST_F(QHexViewRepaint_Test, AScrollOutsideABatchRepaintsTheWholeView)
{
    _view.verticalScrollBar()->setValue(_view.verticalScrollBar()->value() + 5);
    ASSERT_TRUE(PumpUntil([&] { return _spy.paints > 0; }));
    PumpIdle();
    EXPECT_TRUE(WholeViewPainted());
}

TEST_F(QHexViewRepaint_Test, AFarJumpInABatchRepaintsTheWholeView)
{
    Stop(0, 0x100);  // the cursor 112 rows up: nothing on screen stays
    ASSERT_TRUE(PumpUntil([&] { return _spy.paints > 0; }));
    PumpIdle();
    EXPECT_TRUE(WholeViewPainted());
}
