// MouseCaptureController with real Qt key events (offscreen platform): the
// release key must release whatever window the event goes to, and only the
// release key (mouse-manager design §3.3). The native capture and the cursor
// warp are off, so the test never touches the real pointer.

#include <QApplication>
#include <QKeyEvent>
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

        MouseCaptureController::Surface surface;
        surface.centerGlobal = [this] { return _screen.mapToGlobal(_screen.rect().center()); };
        surface.logicalSize = [this] { return _screen.size(); };
        surface.devicePixelRatio = [] { return 1.0; };
        surface.setCursorHidden = [](bool) {};
        surface.takeFocus = [] {};
        surface.allowNativeCapture = false;
        surface.warpCursor = [](QPoint) {};
        _controller->setSurface(surface);
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

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
