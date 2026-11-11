// MouseCaptureController with real Qt key events (offscreen platform): the
// release key must release whatever window the event goes to, and only the
// release key (mouse-manager design §3.3). The native capture and the cursor
// warp are off, so the test never touches the real pointer.

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWidget>
#include <gtest/gtest.h>

#include "emulator/mousecapturecontroller.h"

namespace
{
/// The physical Control key as Qt reports it: Meta on macOS, Control elsewhere
Qt::KeyboardModifier PhysicalControl()
{
#ifdef Q_OS_MACOS
    return Qt::MetaModifier;
#else
    return Qt::ControlModifier;
#endif
}

/// The Command key on macOS (Qt's Control there); elsewhere the Meta (Windows / Super) key
Qt::KeyboardModifier PhysicalCommand()
{
#ifdef Q_OS_MACOS
    return Qt::ControlModifier;
#else
    return Qt::MetaModifier;
#endif
}

class MouseCaptureController_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _screen.resize(320, 240);
        _other.resize(100, 100);
        _controller = std::make_unique<MouseCaptureController>();

        _controller->setSurface(MakeSurface());
        _controller->setSourceSizeProvider([] { return QSizeF(320, 240); });
        _controller->setHostSettingsProvider([this] {
            MouseCaptureController::HostSettings settings;
            settings.mouseFitted = _fitted;
            settings.releaseKey = _releaseKey;
            return settings;
        });
        _controller->setTargetEmulatorId("test-emulator");
    }

    void TearDown() override { _controller.reset(); }

    MouseCaptureController::Surface MakeSurface()
    {
        MouseCaptureController::Surface surface;
        surface.centerGlobal = [this] { return _screen.mapToGlobal(_screen.rect().center()); };
        surface.logicalSize = [this] { return _screen.size(); };
        surface.devicePixelRatio = [] { return 1.0; };
        surface.setCursorHidden = [](bool) {};
        surface.takeFocus = [] {};
        surface.allowNativeCapture = false;
        // A simulated pointer: the warp moves it, the backend reads it
        surface.warpCursor = [this](QPoint global) { _cursor = global; };
        surface.cursorPosition = [this] { return _cursor; };
        return surface;
    }

    /// A mouse event as Qt delivers it to the screen, positions in the screen's logical pixels
    static QMouseEvent MouseEvent(QEvent::Type type, QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons)
    {
        return QMouseEvent(type, position, position, button, buttons, Qt::NoModifier);
    }

    /// Send a key press + release to `target`, as Qt delivers it; true when the press was consumed
    bool Tap(QObject* target, int key, Qt::KeyboardModifiers modifiers)
    {
        QKeyEvent press(QEvent::KeyPress, key, modifiers);
        QApplication::sendEvent(target, &press);
        QKeyEvent release(QEvent::KeyRelease, key, modifiers);
        QApplication::sendEvent(target, &release);
        return press.isAccepted();
    }

    QWidget _screen;
    QWidget _other;
    std::unique_ptr<MouseCaptureController> _controller;
    bool _fitted = true;
    std::string _releaseKey;
    QPoint _cursor;
};
}  // namespace

/// The default Ctrl+Esc is the physical Control key on every platform, and it
/// releases whichever object gets the key (the filter is application-wide)
TEST_F(MouseCaptureController_Test, PhysicalCtrlEscReleasesFromAnyWindow)
{
    _controller->capture();
    ASSERT_TRUE(_controller->isCaptured());
    EXPECT_EQ(_controller->state(), MouseCaptureController::State::Captured);

    Tap(&_other, Qt::Key_Escape, PhysicalControl());  // not even the screen widget
    EXPECT_FALSE(_controller->isCaptured()) << "Ctrl+Esc releases";
    EXPECT_EQ(_controller->state(), MouseCaptureController::State::Ready);
}

/// A plain Esc and Cmd+Esc (on macOS) belong to the machine and the system: no release
TEST_F(MouseCaptureController_Test, OnlyTheReleaseKeyReleases)
{
    _controller->capture();
    ASSERT_TRUE(_controller->isCaptured());

    Tap(&_screen, Qt::Key_Escape, Qt::NoModifier);
    EXPECT_TRUE(_controller->isCaptured()) << "plain Esc reaches the machine";
    Tap(&_screen, Qt::Key_Escape, PhysicalCommand());
    EXPECT_TRUE(_controller->isCaptured()) << "Cmd+Esc is not the release key";
    Tap(&_screen, Qt::Key_Escape, PhysicalControl() | Qt::ShiftModifier);
    EXPECT_TRUE(_controller->isCaptured()) << "exactly the release key's modifiers";

    Tap(&_screen, Qt::Key_Escape, PhysicalControl());
    EXPECT_FALSE(_controller->isCaptured());
}

/// The release press and its release are swallowed: the machine sees neither
TEST_F(MouseCaptureController_Test, ReleaseKeyIsSwallowed)
{
    _controller->capture();
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, PhysicalControl());
    press.setAccepted(false);
    const bool filtered = _controller->eventFilter(&_screen, &press);
    EXPECT_TRUE(filtered);
    EXPECT_FALSE(_controller->isCaptured());

    QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Escape, PhysicalControl(), QString(), true);
    EXPECT_TRUE(_controller->eventFilter(&_screen, &repeat)) << "auto-repeat swallowed too";
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Escape, PhysicalControl());
    EXPECT_TRUE(_controller->eventFilter(&_screen, &release));

    QKeyEvent later(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    EXPECT_FALSE(_controller->eventFilter(&_screen, &later)) << "a later Esc reaches the machine again";
}

/// The config names physical keys ("Ctrl" = Control on macOS too)
TEST_F(MouseCaptureController_Test, ConfiguredReleaseKey)
{
    _releaseKey = "Ctrl+F10";
    _controller->capture();
    Tap(&_screen, Qt::Key_Escape, PhysicalControl());
    EXPECT_TRUE(_controller->isCaptured()) << "Ctrl+Esc is not the configured key";
    Tap(&_screen, Qt::Key_F10, PhysicalControl());
    EXPECT_FALSE(_controller->isCaptured());

    EXPECT_EQ(_controller->releaseKeyText(), QStringLiteral("Ctrl+F10")) << "named as the physical keys, every platform";
    _releaseKey.clear();
    EXPECT_EQ(_controller->releaseKeyText(), QStringLiteral("Ctrl+Esc")) << "the default, as people read it";

    _releaseKey = "not a key sequence !!";
    EXPECT_EQ(_controller->releaseKey(), QKeySequence(QKeyCombination(PhysicalControl(), Qt::Key_Escape)))
        << "an unparsable value falls back to Ctrl+Esc";
}

/// The gate and the device: closed gate or no mouse device = no capture
TEST_F(MouseCaptureController_Test, GateAndDevice)
{
    _controller->setGateOpen(false);
    EXPECT_EQ(_controller->state(), MouseCaptureController::State::Gated);
    _controller->capture();
    EXPECT_FALSE(_controller->isCaptured()) << "gate closed";

    _controller->setGateOpen(true);
    _controller->capture();
    ASSERT_TRUE(_controller->isCaptured());
    _controller->setGateOpen(false);
    EXPECT_FALSE(_controller->isCaptured()) << "closing the gate releases";

    _controller->setGateOpen(true);
    _fitted = false;
    EXPECT_EQ(_controller->state(), MouseCaptureController::State::NoDevice);
    _controller->capture();
    EXPECT_FALSE(_controller->isCaptured()) << "no mouse device";

    _fitted = true;
    _controller->capture();
    ASSERT_TRUE(_controller->isCaptured());
    _controller->handleFocusOut();
    EXPECT_FALSE(_controller->isCaptured()) << "focus loss releases";
}

/// TR-DOS shadows the mouse while captured: released after 3 s of continuous unreachability,
/// not by a brief access, and never re-captured by itself
TEST_F(MouseCaptureController_Test, ReleasesWhenTheMouseStaysUnreachable)
{
    qint64 now = 10000;
    _controller->setClock([&now] { return now; });
    _controller->capture();
    ASSERT_TRUE(_controller->isCaptured());

    _fitted = false;  // TR-DOS goes active
    _controller->checkReachable();
    now += 2900;
    _controller->checkReachable();
    EXPECT_TRUE(_controller->isCaptured()) << "not yet 3 s";

    _fitted = true;  // a brief access: the mouse is back
    now += 100;
    _controller->checkReachable();
    _fitted = false;
    now += 2900;
    _controller->checkReachable();
    EXPECT_TRUE(_controller->isCaptured()) << "the count restarted when it came back";

    now += 3000;
    _controller->checkReachable();
    EXPECT_FALSE(_controller->isCaptured()) << "unreachable for 3 s";

    _fitted = true;
    now += 10000;
    _controller->checkReachable();
    EXPECT_FALSE(_controller->isCaptured()) << "no re-capture without a click";
    _controller->capture();
    EXPECT_TRUE(_controller->isCaptured());
}

/// Speed matching: host travel over a picture drawn at 2x moves the guest half as many pixels,
/// so the guest cursor keeps up with where the host pointer would be; at 1x one for one.
/// Off: one host pixel = one count whatever the zoom. The same arithmetic serves the
/// macOS native deltas and the Windows / Linux warp deltas (both are the host pointer's
/// own travel, with the system's speed and acceleration)
TEST_F(MouseCaptureController_Test, SpeedFollowsTheHostPointer)
{
    int sumX = 0, sumY = 0;
    MouseCaptureController::Poster poster;
    poster.move = [&](int dx, int dy) {
        sumX += dx;
        sumY += dy;
    };
    _controller->setPoster(poster);
    _controller->setSourceSizeProvider([] { return QSizeF(320, 240); });

    _screen.resize(640, 480);  // the picture at 2x
    _controller->capture();
    _controller->applyHostMotion(100, 40);
    EXPECT_EQ(sumX, 50) << "100 host pixels over a 2x picture = 50 emulated pixels";
    EXPECT_EQ(sumY, -20) << "screen Y down = mouse Y up";

    sumX = sumY = 0;
    _controller->applyHostMotion(1, 0);
    EXPECT_EQ(sumX, 0) << "half a pixel is carried";
    _controller->applyHostMotion(1, 0);
    EXPECT_EQ(sumX, 1);

    sumX = sumY = 0;
    _controller->setMatchHostPointer(false);  // switched while captured
    _controller->applyHostMotion(100, 40);
    EXPECT_EQ(sumX, 100) << "off: one host pixel = one count";
    EXPECT_EQ(sumY, -40);

    sumX = sumY = 0;
    _controller->setMatchHostPointer(true);
    _screen.resize(320, 240);  // 1x
    _controller->applyHostMotion(37, 0);
    EXPECT_EQ(sumX, 37);
}

/// The Windows / Linux backend: the pointer is warped back to the center after
/// every move and the offset from the center is the travel (macOS native is off here)
TEST_F(MouseCaptureController_Test, WarpBackendMeasuresTravelFromTheCenter)
{
    int sumX = 0, sumY = 0;
    MouseCaptureController::Poster poster;
    poster.move = [&](int dx, int dy) {
        sumX += dx;
        sumY += dy;
    };
    _controller->setPoster(poster);
    _screen.resize(640, 480);
    _controller->setSourceSizeProvider([] { return QSizeF(320, 240); });
    _controller->capture();
    ASSERT_TRUE(_controller->isCaptured());

    const QPoint center = _screen.mapToGlobal(_screen.rect().center());
    ASSERT_EQ(_cursor, center) << "capture warps the pointer to the center";
    {
        QMouseEvent warpEcho(QEvent::MouseMove, QPointF(0, 0), QPointF(center), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        _controller->handleMouseMove(&warpEcho);  // the move the capture's own warp generates
    }
    _cursor = center + QPoint(20, -10);
    QMouseEvent move(QEvent::MouseMove, QPointF(0, 0), QPointF(_cursor), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    EXPECT_TRUE(_controller->handleMouseMove(&move));
    EXPECT_EQ(sumX, 10) << "20 host pixels at 2x";
    EXPECT_EQ(sumY, 5) << "10 up on screen = 5 up for the mouse";
    EXPECT_EQ(_cursor, center) << "warped back";
}

/// Moves queued before a warp still carry pre-warp positions (Windows delivers them
/// late when its message queue is busy). Measured against the center again they
/// repeated the same travel and the guest pointer leapt; the travel is where the
/// pointer is now, so a stale move reports nothing
TEST_F(MouseCaptureController_Test, StaleMovesBeforeTheWarpAreNotCountedAgain)
{
    int sumX = 0, sumY = 0;
    MouseCaptureController::Poster poster;
    poster.move = [&](int dx, int dy) {
        sumX += dx;
        sumY += dy;
    };
    _controller->setPoster(poster);
    _controller->setSourceSizeProvider([] { return QSizeF(320, 240); });  // 1x
    _controller->capture();
    ASSERT_TRUE(_controller->isCaptured());
    const QPoint center = _cursor;

    // The user moves 30 pixels right in three steps; Windows queues all three before
    // the first is handled
    const QPoint queued[] = {center + QPoint(10, 0), center + QPoint(20, 0), center + QPoint(30, 0)};
    _cursor = queued[2];
    for (const QPoint& at : queued)
    {
        QMouseEvent move(QEvent::MouseMove, QPointF(0, 0), QPointF(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        EXPECT_TRUE(_controller->handleMouseMove(&move));
    }
    EXPECT_EQ(sumX, 30) << "30 pixels of travel, counted once (not 10 + 20 + 30)";
    EXPECT_EQ(sumY, 0);
}

/// region <Touchscreen: gamescope hands it over as an absolute pointer>

class MouseCaptureControllerTouch_Test : public MouseCaptureController_Test
{
protected:
    void SetUp() override
    {
        MouseCaptureController_Test::SetUp();
        MouseCaptureController::Surface surface = MakeSurface();
        surface.pointerIsTouch = true;
        _controller->setSurface(surface);
        MouseCaptureController::Poster poster;
        poster.move = [this](int dx, int dy) { _moves.emplace_back(dx, dy); };
        poster.buttons = [this](uint8_t mask) { _masks.push_back(mask); };
        _controller->setPoster(poster);
        _cursor = QPoint(-1, -1);  // the real pointer is somewhere else: a warp would show
    }

    bool Press(QPointF at) { auto e = MouseEvent(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton); return _controller->handleMousePress(&e); }
    bool Move(QPointF at, Qt::MouseButtons buttons = Qt::LeftButton) { auto e = MouseEvent(QEvent::MouseMove, at, Qt::NoButton, buttons); return _controller->handleMouseMove(&e); }
    bool Release(QPointF at) { auto e = MouseEvent(QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton); return _controller->handleMouseRelease(&e); }

    std::vector<std::pair<int, int>> _moves;
    std::vector<uint8_t> _masks;
};

/// The tap that captures is not a click for the guest; after it a drag moves the cursor by the finger's travel
/// (320 px of screen = 320 emulated pixels here), wherever the finger lands, and the real pointer is never warped
TEST_F(MouseCaptureControllerTouch_Test, CapturingTapIsNotForwardedAndADragIsRelative)
{
    EXPECT_TRUE(Press({250, 40})) << "the press captures";
    ASSERT_TRUE(_controller->isCaptured());
    Release({250, 40});
    EXPECT_TRUE(_moves.empty());
    EXPECT_TRUE(_masks.empty()) << "the capturing tap is no click";

    Press({50, 200});  // lands far from the centre
    Move({70, 200});
    Move({70, 190});
    Release({70, 190});
    ASSERT_EQ(_moves.size(), 2u);
    EXPECT_EQ(_moves[0], std::make_pair(20, 0));
    EXPECT_EQ(_moves[1], std::make_pair(0, 10)) << "the finger went up the screen: the mouse's Y grows upward";
    EXPECT_TRUE(_masks.empty()) << "a drag is no click";
    EXPECT_EQ(_cursor, QPoint(-1, -1)) << "no warp: the finger stays where it is";
}

TEST_F(MouseCaptureControllerTouch_Test, ATapClicksTheLeftButtonForAMoment)
{
    _controller->capture();
    ASSERT_TRUE(_controller->isCaptured());
    qint64 now = 1000;
    _controller->setClock([&now] { return now; });

    Press({100, 100});
    now += 120;
    Release({102, 101});
    ASSERT_EQ(_masks.size(), 1u);
    EXPECT_EQ(_masks[0], 0xFE) << "left pressed (active low)";
    EXPECT_TRUE(_moves.empty()) << "a tap's wobble moves nothing the guest sees... ";

    QElapsedTimer timer;
    timer.start();
    while (_masks.size() < 2 && timer.elapsed() < 1000)
        QCoreApplication::processEvents();
    ASSERT_EQ(_masks.size(), 2u);
    EXPECT_EQ(_masks[1], 0xFF) << "and released";
}

TEST_F(MouseCaptureControllerTouch_Test, AMoveWithoutAContactAndALeapOfTheReportMoveNothing)
{
    _controller->capture();
    Move({300, 220}, Qt::NoButton);  // the pointer hovering or jumping to a touch
    EXPECT_TRUE(_moves.empty());

    Press({10, 10});
    Move({310, 230});  // the pointer jumps to the finger around the press
    EXPECT_TRUE(_moves.empty()) << "a catch-up is not travel";
    Move({314, 230});
    ASSERT_EQ(_moves.size(), 1u);
    EXPECT_EQ(_moves[0], std::make_pair(4, 0));
}

TEST_F(MouseCaptureControllerTouch_Test, ReleasingTheCaptureForgetsTheContact)
{
    _controller->capture();
    Press({100, 100});
    _controller->release();
    EXPECT_FALSE(_controller->isCaptured());
    _controller->capture();
    Move({140, 100});
    EXPECT_TRUE(_moves.empty()) << "the old finger is gone";
}

/// endregion </Touchscreen>

/// region <A real mouse on the same pointer: told from the touchscreen by its behaviour>

class MouseCaptureControllerMouseOnTouchSurface_Test : public MouseCaptureControllerTouch_Test
{
protected:
    void SetUp() override
    {
        MouseCaptureControllerTouch_Test::SetUp();
        _controller->capture();
        ASSERT_TRUE(_controller->isCaptured());
        _now = 1000;
        _controller->setClock([this] { return _now; });
    }

    void Hover(QPointF at) { Move(at, Qt::NoButton); }

    /// A mouse moving over the screen: the first move only finds it, the second confirms a mouse
    void MouseMoves()
    {
        Hover({100, 100});
        Hover({110, 100});
        ASSERT_EQ(_moves.size(), 1u);
        _moves.clear();
    }

    bool WaitForMoves(size_t count)
    {
        QElapsedTimer timer;
        timer.start();
        while (_moves.size() < count && timer.elapsed() < 1000)
            QCoreApplication::processEvents();
        return _moves.size() >= count;
    }

    QPoint Centre() const { return _screen.mapToGlobal(_screen.rect().center()); }

    qint64 _now = 0;
};

/// A mouse moves the guest without a button; a finger's single landing move before the press does not
TEST_F(MouseCaptureControllerMouseOnTouchSurface_Test, HoverMovesWithoutAButtonOnceItIsAMouse)
{
    Hover({100, 100});
    EXPECT_TRUE(_moves.empty()) << "the first hover is not yet known to be a mouse";
    Hover({110, 100});
    ASSERT_EQ(_moves.size(), 1u);
    EXPECT_EQ(_moves[0], std::make_pair(10, 0));
    Hover({110, 95});
    ASSERT_EQ(_moves.size(), 2u);
    EXPECT_EQ(_moves[1], std::make_pair(0, 5)) << "the mouse went up the screen";
    EXPECT_TRUE(_masks.empty());
}

TEST_F(MouseCaptureControllerMouseOnTouchSurface_Test, ASingleHoverBeforeAPressIsAFingerLandingNotAMouse)
{
    Hover({120, 100});  // the pointer lands near the previous place: no leap to tell it by
    Press({125, 100});
    Move({135, 100});
    Release({135, 100});
    ASSERT_EQ(_moves.size(), 1u);
    EXPECT_EQ(_moves[0], std::make_pair(10, 0)) << "the drag is the pad's, the landing moved nothing";
}

/// A mouse press is a button down at once and held until the release (drawing programs drag with it)
TEST_F(MouseCaptureControllerMouseOnTouchSurface_Test, AMouseButtonIsHeldInTheGuestAndADragMovesIt)
{
    MouseMoves();
    Press({110, 100});
    ASSERT_EQ(_masks.size(), 1u);
    EXPECT_EQ(_masks[0], 0xFE) << "left pressed on the press, not on the release";
    Move({120, 100});
    ASSERT_EQ(_moves.size(), 1u);
    EXPECT_EQ(_moves[0], std::make_pair(10, 0));
    Release({120, 100});
    ASSERT_EQ(_masks.size(), 2u);
    EXPECT_EQ(_masks[1], 0xFF);
}

TEST_F(MouseCaptureControllerMouseOnTouchSurface_Test, ARightOrMiddleButtonIsAMouseEvenBeforeItMoved)
{
    auto press = MouseEvent(QEvent::MouseButtonPress, {100, 100}, Qt::RightButton, Qt::RightButton);
    _controller->handleMousePress(&press);
    ASSERT_EQ(_masks.size(), 1u);
    EXPECT_EQ(_masks[0], 0xFD) << "right pressed (active low)";
    auto release = MouseEvent(QEvent::MouseButtonRelease, {100, 100}, Qt::RightButton, Qt::NoButton);
    _controller->handleMouseRelease(&release);
    ASSERT_EQ(_masks.size(), 2u);
    EXPECT_EQ(_masks[1], 0xFF);
}

/// The two in one session: a finger after the mouse (the pointer leaps to it, then the press) is a pad contact
/// again, and the mouse that moves after it is a mouse again
TEST_F(MouseCaptureControllerMouseOnTouchSurface_Test, AFingerAfterTheMouseIsAPadAndTheMouseAfterTheFingerIsAMouse)
{
    MouseMoves();

    Hover({10, 200});  // the leap to the finger
    Press({10, 200});
    Move({30, 200});
    Release({30, 200});
    ASSERT_EQ(_moves.size(), 1u) << "the leap moved nothing, the drag is the pad's";
    EXPECT_EQ(_moves[0], std::make_pair(20, 0));
    EXPECT_TRUE(_masks.empty()) << "a drag is no click";
    _moves.clear();

    Hover({32, 200});
    Hover({34, 200});
    ASSERT_EQ(_moves.size(), 1u) << "a mouse again after two hovers";
    EXPECT_EQ(_moves[0], std::make_pair(4, 0));
}

/// The press delivered before the pointer catches up with the finger: the button is let go, the contact is a pad's
TEST_F(MouseCaptureControllerMouseOnTouchSurface_Test, APressBeforeTheLeapOfAFingerIsNotAHeldButton)
{
    MouseMoves();
    Press({110, 100});  // still where the mouse was
    ASSERT_EQ(_masks.size(), 1u);
    EXPECT_EQ(_masks[0], 0xFE);
    Move({400, 40});  // the pointer reaches the finger
    EXPECT_TRUE(_moves.empty()) << "a leap is not travel";
    ASSERT_EQ(_masks.size(), 2u);
    EXPECT_EQ(_masks[1], 0xFF) << "the mistaken press is let go";

    Move({410, 40});
    ASSERT_EQ(_moves.size(), 1u);
    EXPECT_EQ(_moves[0], std::make_pair(10, 0));
}

/// A fast flick is a leap too; with no press after it, it is delivered
TEST_F(MouseCaptureControllerMouseOnTouchSurface_Test, AFastFlickIsDeliveredWhenNoPressFollows)
{
    MouseMoves();
    Hover({260, 100});  // 150 px in one event
    EXPECT_TRUE(_moves.empty()) << "held back in case a finger landed";
    ASSERT_TRUE(WaitForMoves(1));
    EXPECT_EQ(_moves[0], std::make_pair(150, 0));
}

TEST_F(MouseCaptureControllerMouseOnTouchSurface_Test, AFastFlickIsDeliveredWithTheNextMove)
{
    MouseMoves();
    Hover({260, 100});
    Hover({262, 100});
    ASSERT_EQ(_moves.size(), 1u);
    EXPECT_EQ(_moves[0], std::make_pair(152, 0));
}

/// Moving a mouse far from the centre warps the pointer back, and the echo of the warp is no travel
TEST_F(MouseCaptureControllerMouseOnTouchSurface_Test, TheMouseIsWarpedBackToTheCentreAndTheEchoIsNoTravel)
{
    MouseMoves();
    Hover({150, 100});
    EXPECT_EQ(_cursor, QPoint(-1, -1)) << "near the centre: no warp";
    Hover({60, 100});  // beyond a quarter of the width from the centre (160)
    EXPECT_EQ(_cursor, Centre()) << "warped back";
    _moves.clear();

    Hover({100, 100});  // a move queued before the warp: no longer where the pointer is
    EXPECT_TRUE(_moves.empty());
    Hover(Centre());  // the echo
    EXPECT_TRUE(_moves.empty());
    Hover(Centre() + QPoint(5, 0));
    ASSERT_EQ(_moves.size(), 1u);
    EXPECT_EQ(_moves[0], std::make_pair(5, 0)) << "travel is measured from the centre again";
}

/// A system that ignores the warp leaves the mouse edge-limited but working: no leap, no more warps
TEST_F(MouseCaptureControllerMouseOnTouchSurface_Test, AnIgnoredWarpLeavesTheMouseWorking)
{
    MouseMoves();
    Hover({60, 100});
    EXPECT_EQ(_cursor, Centre());
    _cursor = QPoint(-1, -1);
    _moves.clear();
    _now += 200;  // no echo came
    Hover({61, 100});
    EXPECT_TRUE(_moves.empty()) << "the move that finds the warp ignored only re-anchors";
    Hover({63, 100});
    ASSERT_EQ(_moves.size(), 1u);
    EXPECT_EQ(_moves[0], std::make_pair(2, 0));
    Hover({40, 100});
    EXPECT_EQ(_cursor, QPoint(-1, -1)) << "not warped again";
}

/// A mouse event Qt synthesized from a touch is a touch whatever the hover history says
TEST_F(MouseCaptureControllerMouseOnTouchSurface_Test, ASynthesizedEventIsATouch)
{
    MouseMoves();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(100, 100), QPointF(100, 100), QPointF(100, 100),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier, Qt::MouseEventSynthesizedBySystem);
    _controller->handleMousePress(&press);
    EXPECT_TRUE(_masks.empty()) << "a touch: nothing is pressed in the guest";
}

TEST_F(MouseCaptureControllerMouseOnTouchSurface_Test, TheWheelIsAMouse)
{
    QWheelEvent wheel(QPointF(100, 100), QPointF(100, 100), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    _controller->handleWheel(&wheel);
    Press({100, 100});
    ASSERT_EQ(_masks.size(), 1u);
    EXPECT_EQ(_masks[0], 0xFE) << "after the wheel the press is a mouse button";
}

/// endregion </A real mouse on the same pointer>
