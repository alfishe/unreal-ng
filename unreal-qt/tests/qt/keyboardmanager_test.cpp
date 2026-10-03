// KeyboardManager: a host key posts the ZX matrix key (MC_KEY_*) and the physical key (MC_PCKEY_*). When the
// window loses the key events (focus out, full screen toggle) postHeldKeyReleases must let both go up: a physical
// key left down keeps a PS/2 keyboard's typematic repeating (the Sprinter / ZX-Evo / ATM: cursor Down "stuck"),
// a matrix key left down keeps its press counter above zero, so even a later press and release of the same key
// leaves it down in a Spectrum-mode program.

#include <QKeyEvent>
#include <gtest/gtest.h>

#include <atomic>
#include <mutex>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/testwaithelper.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/keyboardmanager.h"

namespace
{
const std::string kTarget = "keyboardmanager-test";

/// A host key event with the platform's native code of the physical key (KeyboardManager reads it first)
QKeyEvent HostKey(QEvent::Type type, int qtKey, quint32 macVirtualKey, quint32 windowsScanCode, quint32 linuxEvdev)
{
#if defined(Q_OS_MACOS)
    return QKeyEvent(type, qtKey, Qt::NoModifier, 0, macVirtualKey, 0);
#elif defined(Q_OS_WIN)
    return QKeyEvent(type, qtKey, Qt::NoModifier, windowsScanCode, 0, 0);
#else
    return QKeyEvent(type, qtKey, Qt::NoModifier, linuxEvdev + 8, 0, 0);
#endif
}
}  // namespace

TEST(KeyboardManager_Test, FocusOutReleasesThePhysicalAndTheMatrixKeys)
{
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    std::mutex lock;
    std::vector<uint8_t> matrixReleases;
    std::vector<uint8_t> pcReleases;
    std::atomic<int> releases{0};

    const uint64_t matrixObserver = mc.AddObserver(MC_KEY_RELEASED, [&](int, Message* msg) {
        auto* ev = msg ? dynamic_cast<KeyboardEvent*>(msg->obj) : nullptr;
        if (!ev || ev->targetEmulatorId != kTarget)
            return;
        std::lock_guard<std::mutex> guard(lock);
        matrixReleases.push_back(ev->zxKeyCode);
        releases++;
    });
    const uint64_t pcObserver = mc.AddObserver(MC_PCKEY_RELEASED, [&](int, Message* msg) {
        auto* ev = msg ? dynamic_cast<PcKeyEvent*>(msg->obj) : nullptr;
        if (!ev || ev->targetEmulatorId != kTarget)
            return;
        std::lock_guard<std::mutex> guard(lock);
        pcReleases.push_back(ev->pcKeyCode);
        releases++;
    });

    // Cursor Down held (macOS kVK_DownArrow #7D, Windows scan code E0 50, evdev 108), then the window loses focus
    const QKeyEvent down = HostKey(QEvent::KeyPress, Qt::Key_Down, 0x7D, 0x150, 108);
    KeyboardManager::postHostKey(&down, KEY_PRESSED, kTarget);
    EXPECT_EQ(KeyboardManager::heldPcKeyCount(), 1u);
    EXPECT_EQ(KeyboardManager::heldMatrixKeyCount(), 1u);

    KeyboardManager::postHeldKeyReleases(kTarget);
    EXPECT_EQ(KeyboardManager::heldPcKeyCount(), 0u);
    EXPECT_EQ(KeyboardManager::heldMatrixKeyCount(), 0u);
    ASSERT_TRUE(TestWait::For([&] { return releases.load() >= 2; }));
    {
        std::lock_guard<std::mutex> guard(lock);
        EXPECT_EQ(pcReleases, (std::vector<uint8_t>{static_cast<uint8_t>(PcKey::Down)}));
        EXPECT_EQ(matrixReleases, (std::vector<uint8_t>{static_cast<uint8_t>(ZXKEY_EXT_DOWN)}));
    }

    // A key released normally is not released again
    const QKeyEvent pressA = HostKey(QEvent::KeyPress, Qt::Key_A, 0x00, 0x1E, 30);
    const QKeyEvent releaseA = HostKey(QEvent::KeyRelease, Qt::Key_A, 0x00, 0x1E, 30);
    KeyboardManager::postHostKey(&pressA, KEY_PRESSED, kTarget);
    KeyboardManager::postHostKey(&releaseA, KEY_RELEASED, kTarget);
    EXPECT_EQ(KeyboardManager::heldPcKeyCount(), 0u);
    EXPECT_EQ(KeyboardManager::heldMatrixKeyCount(), 0u);

    mc.RemoveObserverById(MC_KEY_RELEASED, matrixObserver);
    mc.RemoveObserverById(MC_PCKEY_RELEASED, pcObserver);
}
