// KeyboardManager: a host key posts the ZX matrix key (MC_KEY_*) and the physical key (MC_PCKEY_*). When the
// window loses the key events (focus out, full screen toggle) postHeldKeyReleases must let both go up: a physical
// key left down keeps a PS/2 keyboard's typematic repeating (the Sprinter / ZX-Evo / ATM: cursor Down "stuck"),
// a matrix key left down keeps its press counter above zero, so even a later press and release of the same key
// leaves it down in a Spectrum-mode program.
//
// The macOS modifier layout (docs/features/keyboard.md "Host keys on macOS"): Qt reports Command as Key_Control /
// ControlModifier and the physical Control key as Key_Meta / MetaModifier. The tests inject the platform choice
// (KeyboardManager::setMacKeyboardForTests), so the macOS rules run on every OS: the physical Control key is the
// machine's Ctrl, the Command key and keys pressed with it held stay with the host (2026-10-03: a Cmd press during
// a Sprinter demo's loading sent PS/2 Left Ctrl, which latched the PLD keyboard INT and crashed the demo).

#include <QKeyEvent>
#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/testwaithelper.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/keyboardmanager.h"
#include "emulator/ports/models/portdecoder_sprinter.h"

namespace
{
const std::string kTarget = "keyboardmanager-test";

/// A host key event with the platform's native code of the physical key (KeyboardManager reads it first)
QKeyEvent HostKey(QEvent::Type type, int qtKey, quint32 macVirtualKey, quint32 windowsScanCode, quint32 linuxEvdev,
                  Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
#if defined(Q_OS_MACOS)
    return QKeyEvent(type, qtKey, modifiers, 0, macVirtualKey, 0);
#elif defined(Q_OS_WIN)
    return QKeyEvent(type, qtKey, modifiers, windowsScanCode, 0, 0);
#else
    return QKeyEvent(type, qtKey, modifiers, linuxEvdev + 8, 0, 0);
#endif
}

/// The physical keys of a Mac keyboard as Qt on macOS reports them (native code: the same physical key on the
/// build's own OS - kVK_Command #37 = Windows LWin E0 5B = evdev KEY_LEFTMETA 125; kVK_Control #3B = scan code 1D
/// = evdev 29; kVK_Tab #30 = scan code 0F = evdev 15). `held` are the physical modifiers down with the key
Qt::KeyboardModifiers MacModifiers(bool command, bool control)
{
    Qt::KeyboardModifiers modifiers;
    modifiers.setFlag(Qt::ControlModifier, command);  // Qt on macOS: Command -> ControlModifier
    modifiers.setFlag(Qt::MetaModifier, control);     // Control -> MetaModifier
    return modifiers;
}
QKeyEvent MacCommand(QEvent::Type type)
{
    // Pressed, Command is among its own modifiers; released, it is not
    return HostKey(type, Qt::Key_Control, 0x37, 0x15B, 125, MacModifiers(type == QEvent::KeyPress, false));
}
QKeyEvent MacControl(QEvent::Type type)
{
    return HostKey(type, Qt::Key_Meta, 0x3B, 0x1D, 29, MacModifiers(false, type == QEvent::KeyPress));
}
QKeyEvent MacTab(QEvent::Type type, bool commandHeld)
{
    return HostKey(type, Qt::Key_Tab, 0x30, 0x0F, 15, MacModifiers(commandHeld, false));
}

/// One posted key message: matrix (MC_KEY_*) or physical (MC_PCKEY_*), press or release
struct Posted
{
    bool pc = false;
    bool pressed = false;
    uint8_t code = 0;
    bool operator==(const Posted& other) const
    {
        return pc == other.pc && pressed == other.pressed && code == other.code;
    }
};
std::ostream& operator<<(std::ostream& os, const Posted& p)
{
    return os << (p.pc ? "pc " : "zx ") << (p.pressed ? "down 0x" : "up 0x") << std::hex << int(p.code) << std::dec;
}
Posted Pc(PcKey key, bool pressed) { return {true, pressed, static_cast<uint8_t>(key)}; }
Posted Zx(ZXKeysEnum key, bool pressed) { return {false, pressed, static_cast<uint8_t>(key)}; }

/// Records every key message posted for the target, in order. Flush() posts a sentinel (PcKey::None up: a keyboard
/// ignores it, postHostKey never posts it) and waits for it: the message center delivers in order, so every
/// earlier message has arrived
class KeyRecorder
{
public:
    explicit KeyRecorder(std::string target = kTarget) : _target(std::move(target))
    {
        MessageCenter& mc = MessageCenter::DefaultMessageCenter();
        for (const char* topic : {MC_KEY_PRESSED, MC_KEY_RELEASED, MC_PCKEY_PRESSED, MC_PCKEY_RELEASED})
        {
            const std::string name = topic;
            _observers.emplace_back(name, mc.AddObserver(name, [this](int, Message* msg) { Record(msg); }));
        }
        // Messages an earlier test posted may still be on their way: let them pass
        Flush();
    }
    ~KeyRecorder()
    {
        MessageCenter& mc = MessageCenter::DefaultMessageCenter();
        for (const auto& [topic, id] : _observers)
            mc.RemoveObserverById(topic, id);
    }

    /// Everything posted since the last call (the sentinel left out)
    std::vector<Posted> Flush()
    {
        MessageCenter::DefaultMessageCenter().Post(
            MC_PCKEY_RELEASED, new PcKeyEvent(static_cast<uint8_t>(kSentinel), KEY_RELEASED, _target));
        EXPECT_TRUE(TestWait::For([&] { return _sentinels.load() > 0; }));
        _sentinels = 0;
        std::lock_guard<std::mutex> guard(_lock);
        std::vector<Posted> out;
        out.swap(_posted);
        return out;
    }

private:
    static constexpr PcKey kSentinel = PcKey::None;

    void Record(Message* msg)
    {
        if (!msg || !msg->obj)
            return;
        Posted p;
        if (auto* pc = dynamic_cast<PcKeyEvent*>(msg->obj))
        {
            if (pc->targetEmulatorId != _target)
                return;
            p = {true, pc->eventType == KEY_PRESSED, pc->pcKeyCode};
            if (!p.pressed && p.code == static_cast<uint8_t>(kSentinel))
            {
                _sentinels++;
                return;
            }
        }
        else if (auto* zx = dynamic_cast<KeyboardEvent*>(msg->obj))
        {
            if (zx->targetEmulatorId != _target)
                return;
            p = {false, zx->eventType == KEY_PRESSED, zx->zxKeyCode};
        }
        else
            return;
        std::lock_guard<std::mutex> guard(_lock);
        _posted.push_back(p);
    }

    std::string _target;
    std::vector<std::pair<std::string, uint64_t>> _observers;
    std::mutex _lock;
    std::vector<Posted> _posted;
    std::atomic<int> _sentinels{0};
};

/// The platform and the Command option for one test, restored afterwards (the state is static)
class HostLayout
{
public:
    HostLayout(bool mac, bool commandToGuest)
    {
        KeyboardManager::setMacKeyboardForTests(mac);
        KeyboardManager::setCommandKeyToGuest(commandToGuest);
    }
    ~HostLayout()
    {
        KeyboardManager::postHeldKeyReleases(kTarget);
        KeyboardManager::setMacKeyboardForTests(std::nullopt);
        KeyboardManager::setCommandKeyToGuest(false);
    }
};
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

// macOS: the Command key is a host key - nothing reaches the machine, neither the matrix nor the PS/2 keyboard
TEST(KeyboardManager_Test, MacCommandSendsNothingByDefault)
{
    HostLayout layout(/*mac=*/true, /*commandToGuest=*/false);
    KeyRecorder recorder;

    const QKeyEvent press = MacCommand(QEvent::KeyPress);
    const QKeyEvent release = MacCommand(QEvent::KeyRelease);
    KeyboardManager::postHostKey(&press, KEY_PRESSED, kTarget);
    EXPECT_EQ(KeyboardManager::heldPcKeyCount(), 0u);
    EXPECT_EQ(KeyboardManager::heldMatrixKeyCount(), 0u);
    KeyboardManager::postHostKey(&release, KEY_RELEASED, kTarget);
    EXPECT_EQ(recorder.Flush(), std::vector<Posted>{});
}

// The option passes Command on as the PC Win / GUI key, on the PS/2 side only (no Symbol Shift on the matrix)
TEST(KeyboardManager_Test, MacCommandAsGuiWithTheOption)
{
    HostLayout layout(/*mac=*/true, /*commandToGuest=*/true);
    KeyRecorder recorder;

    const QKeyEvent press = MacCommand(QEvent::KeyPress);
    const QKeyEvent release = MacCommand(QEvent::KeyRelease);
    KeyboardManager::postHostKey(&press, KEY_PRESSED, kTarget);
    EXPECT_EQ(KeyboardManager::heldPcKeyCount(), 1u);
    KeyboardManager::postHostKey(&release, KEY_RELEASED, kTarget);
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Pc(PcKey::LeftGui, true), Pc(PcKey::LeftGui, false)}));

    // Held as the GUI key, then the option goes off: its release still lets it go
    KeyboardManager::postHostKey(&press, KEY_PRESSED, kTarget);
    KeyboardManager::setCommandKeyToGuest(false);
    KeyboardManager::postHostKey(&release, KEY_RELEASED, kTarget);
    EXPECT_EQ(KeyboardManager::heldPcKeyCount(), 0u);
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Pc(PcKey::LeftGui, true), Pc(PcKey::LeftGui, false)}));
}

// macOS: the physical Control key (Qt's Key_Meta there) is the machine's Ctrl: PS/2 Left Ctrl, matrix Symbol Shift
TEST(KeyboardManager_Test, MacPhysicalControlIsCtrl)
{
    HostLayout layout(/*mac=*/true, /*commandToGuest=*/false);
    KeyRecorder recorder;

    const QKeyEvent press = MacControl(QEvent::KeyPress);
    const QKeyEvent release = MacControl(QEvent::KeyRelease);
    KeyboardManager::postHostKey(&press, KEY_PRESSED, kTarget);
    KeyboardManager::postHostKey(&release, KEY_RELEASED, kTarget);
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Pc(PcKey::LeftCtrl, true), Zx(ZXKEY_SYM_SHIFT, true),
                                                     Pc(PcKey::LeftCtrl, false), Zx(ZXKEY_SYM_SHIFT, false)}));
}

// A layout without Latin letters (Russian: the K position types Cyrillic el): the physical key stands in, so the
// matrix gets K (and the comma position the ZX comma); a Latin layout's unmapped punctuation stays silent
TEST(KeyboardManager_Test, NonLatinLayoutUsesThePhysicalKey)
{
    // Qt has no enum for these: a letter without one is reported as its (upper case) Unicode code point
    constexpr int kCyrillicEl = 0x041B;  // Л
    constexpr int kCyrillicBe = 0x0411;  // Б
    HostLayout layout(/*mac=*/false, /*commandToGuest=*/false);
    KeyRecorder recorder;

    const QKeyEvent kDown = HostKey(QEvent::KeyPress, kCyrillicEl, 0x28, 0x25, 37);
    const QKeyEvent kUp = HostKey(QEvent::KeyRelease, kCyrillicEl, 0x28, 0x25, 37);
    KeyboardManager::postHostKey(&kDown, KEY_PRESSED, kTarget);
    KeyboardManager::postHostKey(&kUp, KEY_RELEASED, kTarget);
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Pc(PcKey::K, true), Zx(ZXKEY_K, true), Pc(PcKey::K, false),
                                                     Zx(ZXKEY_K, false)}));

    const QKeyEvent commaDown = HostKey(QEvent::KeyPress, kCyrillicBe, 0x2B, 0x33, 51);
    const QKeyEvent commaUp = HostKey(QEvent::KeyRelease, kCyrillicBe, 0x2B, 0x33, 51);
    KeyboardManager::postHostKey(&commaDown, KEY_PRESSED, kTarget);
    KeyboardManager::postHostKey(&commaUp, KEY_RELEASED, kTarget);
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Pc(PcKey::Comma, true), Zx(ZXKEY_EXT_COMMA, true),
                                                     Pc(PcKey::Comma, false), Zx(ZXKEY_EXT_COMMA, false)}));

    // Latin layout, an unmapped punctuation key (';'): the physical key only, as before
    const QKeyEvent semiDown = HostKey(QEvent::KeyPress, Qt::Key_Semicolon, 0x29, 0x27, 39);
    const QKeyEvent semiUp = HostKey(QEvent::KeyRelease, Qt::Key_Semicolon, 0x29, 0x27, 39);
    KeyboardManager::postHostKey(&semiDown, KEY_PRESSED, kTarget);
    KeyboardManager::postHostKey(&semiUp, KEY_RELEASED, kTarget);
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Pc(PcKey::Semicolon, true), Pc(PcKey::Semicolon, false)}));
}

// Windows / Linux layout: Ctrl is Ctrl, the Win / Super key is the GUI key (and Symbol Shift, as before)
TEST(KeyboardManager_Test, PcLayoutUnchanged)
{
    HostLayout layout(/*mac=*/false, /*commandToGuest=*/false);
    KeyRecorder recorder;

    const QKeyEvent ctrlDown = HostKey(QEvent::KeyPress, Qt::Key_Control, 0x3B, 0x1D, 29, Qt::ControlModifier);
    const QKeyEvent ctrlUp = HostKey(QEvent::KeyRelease, Qt::Key_Control, 0x3B, 0x1D, 29);
    const QKeyEvent winDown = HostKey(QEvent::KeyPress, Qt::Key_Meta, 0x37, 0x15B, 125, Qt::MetaModifier);
    const QKeyEvent winUp = HostKey(QEvent::KeyRelease, Qt::Key_Meta, 0x37, 0x15B, 125);
    KeyboardManager::postHostKey(&ctrlDown, KEY_PRESSED, kTarget);
    KeyboardManager::postHostKey(&ctrlUp, KEY_RELEASED, kTarget);
    KeyboardManager::postHostKey(&winDown, KEY_PRESSED, kTarget);
    KeyboardManager::postHostKey(&winUp, KEY_RELEASED, kTarget);
    EXPECT_EQ(recorder.Flush(),
              (std::vector<Posted>{Pc(PcKey::LeftCtrl, true), Zx(ZXKEY_SYM_SHIFT, true), Pc(PcKey::LeftCtrl, false),
                                   Zx(ZXKEY_SYM_SHIFT, false), Pc(PcKey::LeftGui, true), Zx(ZXKEY_SYM_SHIFT, true),
                                   Pc(PcKey::LeftGui, false), Zx(ZXKEY_SYM_SHIFT, false)}));
}

// Cmd+Tab while a key is held: the switch sends nothing, the held key still goes up (focus out), nothing stays held
TEST(KeyboardManager_Test, MacCommandTabSendsNothingAndLeavesNothingHeld)
{
    HostLayout layout(/*mac=*/true, /*commandToGuest=*/false);
    KeyRecorder recorder;

    const QKeyEvent downPress = HostKey(QEvent::KeyPress, Qt::Key_Down, 0x7D, 0x150, 108);
    KeyboardManager::postHostKey(&downPress, KEY_PRESSED, kTarget);
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Pc(PcKey::Down, true), Zx(ZXKEY_EXT_DOWN, true)}));

    const QKeyEvent cmdPress = MacCommand(QEvent::KeyPress);
    const QKeyEvent tabPress = MacTab(QEvent::KeyPress, true);
    const QKeyEvent tabRelease = MacTab(QEvent::KeyRelease, true);
    KeyboardManager::postHostKey(&cmdPress, KEY_PRESSED, kTarget);
    KeyboardManager::postHostKey(&tabPress, KEY_PRESSED, kTarget);
    KeyboardManager::postHostKey(&tabRelease, KEY_RELEASED, kTarget);
    EXPECT_EQ(recorder.Flush(), std::vector<Posted>{}) << "Cmd+Tab belongs to the host";

    // The app switch takes the focus: the screen releases what is held - the Down key only
    KeyboardManager::postHeldKeyReleases(kTarget);
    EXPECT_EQ(KeyboardManager::heldPcKeyCount(), 0u);
    EXPECT_EQ(KeyboardManager::heldMatrixKeyCount(), 0u);
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Pc(PcKey::Down, false), Zx(ZXKEY_EXT_DOWN, false)}));

    // Back in the window, the Command release (Qt delivers it late) sends nothing either
    const QKeyEvent cmdRelease = MacCommand(QEvent::KeyRelease);
    KeyboardManager::postHostKey(&cmdRelease, KEY_RELEASED, kTarget);
    EXPECT_EQ(recorder.Flush(), std::vector<Posted>{});
}

// A key pressed before Command went down is released while Command is held: the release reaches the machine
TEST(KeyboardManager_Test, MacKeyHeldBeforeCommandStillReleases)
{
    HostLayout layout(/*mac=*/true, /*commandToGuest=*/false);
    KeyRecorder recorder;

    const QKeyEvent aPress = HostKey(QEvent::KeyPress, Qt::Key_A, 0x00, 0x1E, 30);
    const QKeyEvent aRelease = HostKey(QEvent::KeyRelease, Qt::Key_A, 0x00, 0x1E, 30, MacModifiers(true, false));
    const QKeyEvent cmdPress = MacCommand(QEvent::KeyPress);
    const QKeyEvent cmdRelease = MacCommand(QEvent::KeyRelease);
    KeyboardManager::postHostKey(&aPress, KEY_PRESSED, kTarget);
    KeyboardManager::postHostKey(&cmdPress, KEY_PRESSED, kTarget);
    KeyboardManager::postHostKey(&aRelease, KEY_RELEASED, kTarget);
    KeyboardManager::postHostKey(&cmdRelease, KEY_RELEASED, kTarget);
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Pc(PcKey::A, true), Zx(ZXKEY_A, true), Pc(PcKey::A, false),
                                                     Zx(ZXKEY_A, false)}));
    EXPECT_EQ(KeyboardManager::heldPcKeyCount(), 0u);
    EXPECT_EQ(KeyboardManager::heldMatrixKeyCount(), 0u);
}

#if !defined(Q_OS_MACOS)
// Without a native code (synthetic events) the Qt key decides, with the same macOS swap
TEST(KeyboardManager_Test, MacLayoutWithoutNativeCodes)
{
    HostLayout layout(/*mac=*/true, /*commandToGuest=*/true);
    EXPECT_EQ(KeyboardManager::mapQtKeyToPcKey(Qt::Key_Meta), PcKey::LeftCtrl);
    EXPECT_EQ(KeyboardManager::mapQtKeyToPcKey(Qt::Key_Control), PcKey::LeftGui);

    KeyRecorder recorder;
    const QKeyEvent cmdPress(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
    const QKeyEvent cmdRelease(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier);
    KeyboardManager::postHostKey(&cmdPress, KEY_PRESSED, kTarget);
    KeyboardManager::postHostKey(&cmdRelease, KEY_RELEASED, kTarget);
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Pc(PcKey::LeftGui, true), Pc(PcKey::LeftGui, false)}));
}
#endif

// The Sprinter (PS/2 keyboard on Z84C15 SIO channel A): Cmd+Tab through the whole host keyboard path puts no byte
// in SIO A's receive FIFO; the physical Control key that follows arrives as Left Ctrl (#14), and nothing before it.
// Creating a Sprinter takes ~100 ms (4 MB RAM, firmware load) - the only machine with this keyboard path
TEST(KeyboardManager_Test, MacCommandTabSendsNoByteToSprinterSioA)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    ASSERT_NE(manager, nullptr);
    std::shared_ptr<Emulator> emulator =
        manager->CreateEmulatorWithModel("keyboardmanager-sprinter", "SPRINTER", LoggerLevel::LogError);
    if (!emulator)
        GTEST_SKIP() << "no Sprinter firmware (data/rom/sprinter)";
    const std::string id = emulator->GetUUID();
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = context ? dynamic_cast<PortDecoder_Sprinter*>(context->pPortDecoder) : nullptr;
    ASSERT_NE(decoder, nullptr);

    // SIO A drained through its ports (RR0 bit 0: a character waits; #18 data), after `frames` PS/2 frames
    auto drainA = [&](int frames) {
        std::vector<uint8_t> out;
        for (int i = 0; i < frames; i++)
        {
            context->emulatorState.t_states += 3210;  // one PS/2 frame at 3.5 MHz
            while (decoder->DecodePortIn(0x0019, 0) & 0x01)
                out.push_back(decoder->DecodePortIn(0x0018, 0));
        }
        return out;
    };
    drainA(4);

    {
        HostLayout layout(/*mac=*/true, /*commandToGuest=*/false);
        KeyRecorder recorder(id);  // observers after the keyboard's: a message seen here has reached it

        const QKeyEvent cmdPress = MacCommand(QEvent::KeyPress);
        const QKeyEvent tabPress = MacTab(QEvent::KeyPress, true);
        const QKeyEvent tabRelease = MacTab(QEvent::KeyRelease, true);
        const QKeyEvent cmdRelease = MacCommand(QEvent::KeyRelease);
        KeyboardManager::postHostKey(&cmdPress, KEY_PRESSED, id);
        KeyboardManager::postHostKey(&tabPress, KEY_PRESSED, id);
        KeyboardManager::postHostKey(&tabRelease, KEY_RELEASED, id);
        KeyboardManager::postHeldKeyReleases(id);  // the focus leaves for the other app
        KeyboardManager::postHostKey(&cmdRelease, KEY_RELEASED, id);
        EXPECT_EQ(recorder.Flush(), std::vector<Posted>{});
        EXPECT_EQ(drainA(8), std::vector<uint8_t>{}) << "Cmd+Tab must not reach the PS/2 keyboard";

        const QKeyEvent ctrlPress = MacControl(QEvent::KeyPress);
        KeyboardManager::postHostKey(&ctrlPress, KEY_PRESSED, id);
        recorder.Flush();
        std::vector<uint8_t> got;
        auto collect = [&](size_t count) {
            got.clear();
            EXPECT_TRUE(TestWait::For([&] {
                const std::vector<uint8_t> part = drainA(1);
                got.insert(got.end(), part.begin(), part.end());
                return got.size() >= count;
            }));
            const std::vector<uint8_t> rest = drainA(2);  // nothing more follows
            got.insert(got.end(), rest.begin(), rest.end());
        };
        collect(1);
        EXPECT_EQ(got, std::vector<uint8_t>{0x14}) << "the physical Control key is Left Ctrl, the first byte since Cmd+Tab";
        const QKeyEvent ctrlRelease = MacControl(QEvent::KeyRelease);
        KeyboardManager::postHostKey(&ctrlRelease, KEY_RELEASED, id);
        recorder.Flush();
        collect(2);
        EXPECT_EQ(got, (std::vector<uint8_t>{0xF0, 0x14}));
    }

    emulator.reset();
    manager->RemoveEmulator(id);
}
