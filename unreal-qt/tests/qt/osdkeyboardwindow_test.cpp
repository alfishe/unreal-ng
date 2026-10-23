// The on-screen keyboard (unreal-qt/src/osdkeyboard/osdkeyboardwindow.h, POC 031): every picture of
// data/controllers/keyboards loads and names only keys the core knows; a model gets its pictures, the default first;
// a ZX key posts its matrix key, a combination key its keys in order (up in reverse), a latched modifier stays down
// until the next key goes up, a PC picture's key posts the PC key alone (the controller, not the matrix).

#include <QFile>
#include <QKeyEvent>
#include <QSettings>
#include <gtest/gtest.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/keyboardmanager.h"
#include "osdkeyboard/keyboardpicture.h"
#include "osdkeyboard/osdkeyboardwindow.h"

namespace
{
QString Root()
{
    return QString::fromStdString((TestPathHelper::FindProjectRoot() / "data" / "controllers" / "keyboards").string());
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

/// Records the key messages posted for one machine, in order; Flush() waits for a sentinel posted behind them
class KeyRecorder
{
public:
    explicit KeyRecorder(std::string target) : _target(std::move(target))
    {
        MessageCenter& mc = MessageCenter::DefaultMessageCenter();
        for (const char* topic : {MC_KEY_PRESSED, MC_KEY_RELEASED, MC_PCKEY_PRESSED, MC_PCKEY_RELEASED})
            _observers.emplace_back(topic, mc.AddObserver(topic, [this](int, Message* msg) { Record(msg); }));
        Flush();
    }
    ~KeyRecorder()
    {
        MessageCenter& mc = MessageCenter::DefaultMessageCenter();
        for (const auto& [topic, id] : _observers)
            mc.RemoveObserverById(topic, id);
    }
    std::vector<Posted> Flush()
    {
        MessageCenter::DefaultMessageCenter().Post(
            MC_PCKEY_RELEASED, new PcKeyEvent(static_cast<uint8_t>(PcKey::None), KEY_RELEASED, _target));
        EXPECT_TRUE(TestWait::For([&] { return _sentinels.load() > 0; }));
        _sentinels = 0;
        std::lock_guard<std::mutex> guard(_lock);
        std::vector<Posted> out;
        out.swap(_posted);
        return out;
    }

private:
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
            if (!p.pressed && p.code == static_cast<uint8_t>(PcKey::None))
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

class OsdKeyboardWindow_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        QSettings().remove("OsdKeyboard");
    }
    void TearDown() override
    {
        QSettings().remove("OsdKeyboard");
        _binding.unbind();
        for (const auto& id : EmulatorManager::GetInstance()->GetEmulatorIds())
            EmulatorManager::GetInstance()->RemoveEmulator(id);
    }
    std::shared_ptr<Emulator> Create(const std::string& model)
    {
        std::string error;
        auto emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("", model, LoggerLevel::LogError, &error);
        EXPECT_NE(emulator, nullptr) << error;
        if (emulator)
            _binding.bind(emulator);
        return emulator;
    }
    EmulatorBinding _binding;
};
}  // namespace

TEST_F(OsdKeyboardWindow_Test, EveryPictureLoadsWithKnownKeys)
{
    KeyboardPictures pictures;
    ASSERT_TRUE(pictures.load(Root())) << Root().toStdString();
    size_t total = 0;
    for (const char* model : {"48K", "128k", "PLUS2", "PLUS2A", "PLUS3", "PENTAGON", "SCORPION", "PROFSCORP", "GMX",
                              "QUORUM", "LSY256", "PROFI", "PROFI3", "ATM450", "ATM710", "ATM3", "TSL", "SPRINTER",
                              "KAY", "PHOENIX", "NEXT"})
    {
        const std::vector<std::string> ids = pictures.forModel(model);
        ASSERT_FALSE(ids.empty()) << model << ": no keyboard";
        for (const std::string& id : ids)
        {
            KeyboardPicture picture;
            QString error;
            ASSERT_TRUE(pictures.picture(id, picture, error)) << error.toStdString();
            EXPECT_TRUE(QFile::exists(picture.svgPath)) << picture.svgPath.toStdString();
            for (const std::string& style : pictures.styles())  // every style of every picture is there
            {
                KeyboardPicture drawn;
                ASSERT_TRUE(pictures.picture(id, drawn, error, style)) << error.toStdString();
                EXPECT_EQ(drawn.style, style);
                EXPECT_TRUE(QFile::exists(drawn.svgPath)) << drawn.svgPath.toStdString();
                EXPECT_EQ(drawn.keys.size(), picture.keys.size()) << id << " " << style << ": one key map for all";
            }
            EXPECT_GE(picture.keys.size(), 40u) << id;
            for (const PictureKey& key : picture.keys)
            {
                const QPointF inside = key.shape.isEmpty() ? key.rect.center() : key.shape.at(1) + QPointF(-1, 1);
                EXPECT_EQ(picture.keyAt(inside), &key) << id << " " << key.id << ": a click on it finds it";
            }
            total++;
        }
    }
    EXPECT_GE(total, 21u);
    EXPECT_EQ(pictures.styles(), (std::vector<std::string>{"photo", "schematic", "hybrid"}));
    EXPECT_EQ(pictures.forModel("ATM710").front(), "pcat") << "the ATM controller's firmwares are all AT";
    EXPECT_EQ(pictures.forModel("ATM450"), std::vector<std::string>{"zxmatrix"}) << "no PC keyboard before 7.00";
    EXPECT_TRUE(pictures.forModel("NOSUCH").empty());
}

TEST_F(OsdKeyboardWindow_Test, OrelKeysFollowTheSchematic)
{
    // Orel BK-08 keyboard schematic БУ5.103.474: АЛТ = CS, УПР = SS, the rest of the grey keys and the punctuation on
    // the extra columns KD5 / KD7 (ZXKEY_BK08_*); РЕГ and Р/А are shifts to the ROM, so they latch
    KeyboardPictures pictures;
    ASSERT_TRUE(pictures.load(Root()));
    KeyboardPicture picture;
    QString error;
    ASSERT_TRUE(pictures.picture("orel", picture, error)) << error.toStdString();
    std::map<std::string, const PictureKey*> byId;
    for (const PictureKey& key : picture.keys)
    {
        EXPECT_FALSE(key.inert()) << key.id << ": every Orel key is wired";
        byId[key.id] = &key;
    }
    const auto presses = [&](const char* id) {
        const auto it = byId.find(id);
        return it == byId.end() ? std::vector<ZXKeysEnum>{} : it->second->zx;
    };
    EXPECT_EQ(presses("alt"), std::vector<ZXKeysEnum>{ZXKEY_CAPS_SHIFT});
    EXPECT_EQ(presses("ctrl"), std::vector<ZXKeysEnum>{ZXKEY_SYM_SHIFT});
    EXPECT_EQ(presses("shift"), std::vector<ZXKeysEnum>{ZXKEY_BK08_SHIFT});
    EXPECT_EQ(presses("shift-right"), std::vector<ZXKeysEnum>{ZXKEY_BK08_SHIFT});
    EXPECT_EQ(presses("caps"), std::vector<ZXKeysEnum>{ZXKEY_BK08_CAPS});
    EXPECT_EQ(presses("rus"), std::vector<ZXKeysEnum>{ZXKEY_BK08_RUS});
    EXPECT_EQ(presses("ra"), std::vector<ZXKeysEnum>{ZXKEY_BK08_RA});
    EXPECT_EQ(presses("tab"), std::vector<ZXKeysEnum>{ZXKEY_BK08_TAB});
    EXPECT_EQ(presses("slash"), std::vector<ZXKeysEnum>{ZXKEY_BK08_SLASH});
    EXPECT_EQ(presses("bs"), (std::vector<ZXKeysEnum>{ZXKEY_CAPS_SHIFT, ZXKEY_0}));
    ASSERT_TRUE(byId.count("shift") && byId.count("ra") && byId.count("caps"));
    EXPECT_TRUE(byId["shift"]->modifier());
    EXPECT_TRUE(byId["ra"]->modifier());
    EXPECT_FALSE(byId["caps"]->modifier()) << "ЗГЛ is a toggle key, not a shift";
}

TEST_F(OsdKeyboardWindow_Test, ZxKeysAndALatchedModifier)
{
    auto emulator = Create("48K");
    ASSERT_NE(emulator, nullptr);
    KeyRecorder recorder(emulator->GetUUID().toString());
    OsdKeyboardWindow window;
    window.setRoot(Root());
    window.setBinding(&_binding);
    ASSERT_EQ(window.model(), "48K");
    EXPECT_EQ(window.picture().id, "zx48k");
    EXPECT_EQ(window.choices(), (std::vector<std::string>{"zx48k", "zxplus", "forum"}))
        << "the 48K's own, then the Spectrum+ and the Forum";

    window.press("a");
    window.release();
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Zx(ZXKEY_A, true), Zx(ZXKEY_A, false)}));

    // Caps Shift latches, goes down before the next key and up after it
    window.press("caps-shift");
    EXPECT_EQ(window.held(), std::set<std::string>{"caps-shift"});
    window.press("5");
    window.release();
    EXPECT_TRUE(window.held().empty());
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Zx(ZXKEY_CAPS_SHIFT, true), Zx(ZXKEY_5, true),
                                                     Zx(ZXKEY_5, false), Zx(ZXKEY_CAPS_SHIFT, false)}));

    // A second click lets a latched modifier go; hiding the window lets go of everything
    window.press("symbol-shift");
    window.press("symbol-shift");
    window.press("caps-shift");
    window.show();
    window.hide();
    EXPECT_TRUE(window.held().empty());
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Zx(ZXKEY_SYM_SHIFT, true), Zx(ZXKEY_SYM_SHIFT, false),
                                                     Zx(ZXKEY_CAPS_SHIFT, true), Zx(ZXKEY_CAPS_SHIFT, false)}));
}

TEST_F(OsdKeyboardWindow_Test, ACombinationKeyPressesItsKeysTogether)
{
    auto emulator = Create("128k");
    ASSERT_NE(emulator, nullptr);
    KeyRecorder recorder(emulator->GetUUID().toString());
    OsdKeyboardWindow window;
    window.setRoot(Root());
    window.setBinding(&_binding);
    const PictureKey* combo = nullptr;
    for (const PictureKey& key : window.picture().keys)
        if (key.zx.size() == 2 && key.zx[0] == ZXKEY_CAPS_SHIFT)
            combo = &key;
    ASSERT_NE(combo, nullptr) << "the 128's keyboard has its Caps Shift keys (TRUE VIDEO, the arrows)";
    const ZXKeysEnum second = combo->zx[1];
    window.press(combo->id);
    window.release();
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Zx(ZXKEY_CAPS_SHIFT, true), Zx(second, true), Zx(second, false),
                                                     Zx(ZXKEY_CAPS_SHIFT, false)}));
}

TEST_F(OsdKeyboardWindow_Test, APcPictureKeyGoesToTheControllerOnly)
{
    // ~120 ms: creating the ZX-Evo (its AVR and firmware) is most of it; the PC picture needs a machine with a
    // PS/2 controller
    auto emulator = Create("ATM3");
    ASSERT_NE(emulator, nullptr);
    KeyRecorder recorder(emulator->GetUUID().toString());
    OsdKeyboardWindow window;
    window.setRoot(Root());
    window.setBinding(&_binding);
    ASSERT_EQ(window.choices(), (std::vector<std::string>{"pcps2", "zxmatrix"}));
    EXPECT_TRUE(window.picture().pc);
    EXPECT_TRUE(window.notice().isEmpty()) << window.notice().toStdString();

    window.press("leftshift");
    window.press("a");
    window.release();
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Pc(PcKey::LeftShift, true), Pc(PcKey::A, true), Pc(PcKey::A, false),
                                                     Pc(PcKey::LeftShift, false)}))
        << "no matrix key beside a PC picture's key";

    // The switch: the matrix picture, kept for the model
    window.choose("zxmatrix");
    EXPECT_FALSE(window.picture().pc);
    window.press("a");
    window.release();
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Zx(ZXKEY_A, true), Zx(ZXKEY_A, false)}));
    OsdKeyboardWindow again;
    again.setRoot(Root());
    again.setBinding(&_binding);
    EXPECT_EQ(again.picture().id, "zxmatrix") << "the choice is kept per model";
}

TEST_F(OsdKeyboardWindow_Test, NoMachineNoPicture)
{
    auto emulator = Create("48K");
    ASSERT_NE(emulator, nullptr);
    OsdKeyboardWindow window;
    window.setRoot(Root());
    window.setBinding(&_binding);
    EXPECT_TRUE(window.notice().isEmpty()) << window.notice().toStdString();
    _binding.unbind();
    window.refresh();
    EXPECT_TRUE(window.picture().keys.empty());
    EXPECT_FALSE(window.notice().isEmpty()) << "no machine";
}

TEST_F(OsdKeyboardWindow_Test, AHostCursorKeyLightsItsKeys)
{
    // Host Left is the extended key Caps Shift + 5 on the matrix and the PC key Left
    const auto hostLeft = [](bool pressed, const std::string& target) {
        QKeyEvent event(pressed ? QEvent::KeyPress : QEvent::KeyRelease, Qt::Key_Left, Qt::NoModifier);
        KeyboardManager::postHostKey(&event, pressed ? KEY_PRESSED : KEY_RELEASED, target);
    };
    for (const auto& [model, lit] : std::vector<std::pair<std::string, std::set<std::string>>>{
             {"48K", {"host:caps-shift", "host:5"}},  // no LEFT key: its two keys
             {"128k", {"host:left"}},                 // the key printed with the combination
             {"NEXT", {"host:left"}},                 // the membrane's own key
         })
    {
        auto emulator = Create(model);
        ASSERT_NE(emulator, nullptr);
        const std::string target = emulator->GetUUID().toString();
        OsdKeyboardWindow window;
        window.setRoot(Root());
        window.setBinding(&_binding);
        hostLeft(true, target);
        window.refresh();
        EXPECT_EQ(window.lit(), lit) << model;
        hostLeft(false, target);
        window.refresh();
        EXPECT_TRUE(window.lit().empty()) << model;
        _binding.unbind();
        EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
    }
}

TEST_F(OsdKeyboardWindow_Test, ANextExtendedKeyIsOneKeyOfItsOwn)
{
    // The Next's LEFT is a membrane key: the machine presses CAPS SHIFT + 5 and shows it in NR #B0
    auto emulator = Create("NEXT");
    ASSERT_NE(emulator, nullptr);
    KeyRecorder recorder(emulator->GetUUID().toString());
    OsdKeyboardWindow window;
    window.setRoot(Root());
    window.setBinding(&_binding);
    ASSERT_EQ(window.picture().id, "zxnext");
    window.press("left");
    window.release();
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Zx(ZXKEY_EXT_LEFT, true), Zx(ZXKEY_EXT_LEFT, false)}));
    window.press("extend-mode");
    window.release();
    EXPECT_EQ(recorder.Flush(), (std::vector<Posted>{Zx(ZXKEY_EXT_EXTEND, true), Zx(ZXKEY_EXT_EXTEND, false)}));
}

/// The automation's report of a hidden window (osd_keyboard_pictures): the shown machine's pictures, the one chosen,
/// and a signal on every change of the picture
TEST_F(OsdKeyboardWindow_Test, FollowModelOffersThePicturesWhileHidden)
{
    auto emulator = Create("PENTAGON");
    ASSERT_NE(emulator, nullptr);
    OsdKeyboardWindow window;
    window.setRoot(Root());
    window.setBinding(&_binding);
    int changes = 0;
    QObject::connect(&window, &OsdKeyboardWindow::pictureChanged, [&changes] { ++changes; });
    window.followModel();
    EXPECT_FALSE(window.isVisible());
    EXPECT_EQ(window.choices(), (std::vector<std::string>{"pentagon", "pentagon2plus"}));
    EXPECT_FALSE(window.picture().id.empty());
    const int before = changes;
    window.choose("pentagon2plus");
    EXPECT_EQ(window.picture().id, "pentagon2plus");
    EXPECT_EQ(changes, before + 1);
    window.choose("zx48k");
    EXPECT_EQ(window.picture().id, "pentagon2plus") << "not offered for this machine: nothing changes";
}

/// The style switch draws the picture shown anew, keeps it for every model and for the next window, and an unknown
/// style changes nothing
TEST_F(OsdKeyboardWindow_Test, AStyleDrawsThePictureShownAndIsKept)
{
    auto emulator = Create("PENTAGON");
    ASSERT_NE(emulator, nullptr);
    {
        OsdKeyboardWindow window;
        window.setRoot(Root());
        window.setBinding(&_binding);
        window.followModel();
        EXPECT_EQ(window.style(), "photo") << "the default";
        window.choose("pentagon2plus");
        int changes = 0;
        QObject::connect(&window, &OsdKeyboardWindow::pictureChanged, [&changes] { ++changes; });
        window.chooseStyle("hybrid");
        EXPECT_EQ(window.style(), "hybrid");
        EXPECT_EQ(window.picture().id, "pentagon2plus") << "the same picture";
        EXPECT_TRUE(window.picture().svgPath.endsWith("pentagon2plus.hybrid.svg")) << window.picture().svgPath.toStdString();
        EXPECT_EQ(changes, 1);
        window.chooseStyle("sketch");
        EXPECT_EQ(window.style(), "hybrid") << "not a style";
        EXPECT_EQ(changes, 1);
    }
    OsdKeyboardWindow again;
    again.setRoot(Root());
    again.setBinding(&_binding);
    again.followModel();
    EXPECT_EQ(again.style(), "hybrid") << "kept in the settings";
    EXPECT_EQ(again.picture().style, "hybrid");
}
