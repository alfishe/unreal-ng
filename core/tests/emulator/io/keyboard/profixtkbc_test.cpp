// ZX Profi PROFI-XT keyboard controller: the reconstructed firmware on the
// MCS-48 core and the table engine, alone (a rig with its own clock) and on the
// Profi behind IN #FE (research-profi-keyboard.md section 8; design.md
// section "Keyboard")

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/io/keyboard/profixtkbc.h"
#include "emulator/io/keyboard/profixtkeymap.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/rom.h"
#include "emulator/ports/models/portdecoder_profi.h"

namespace
{
using Engine = ProfiXtKbc::Engine;

/// The controller alone: its own emulated clock (3.5 MHz T-states), no machine
struct Rig
{
    uint64_t t = 0;
    ProfiXtKbc kbc{nullptr};
    bool loaded = false;

    explicit Rig(Engine engine, const std::string& rom = "")
    {
        kbc.SetTimeSource([this]() { return t; });
        std::string error;
        loaded = kbc.Load(engine, rom, error);
        Advance(5000);   // the firmware's power-on
    }

    void Advance(uint32_t micros)
    {
        t += static_cast<uint64_t>(micros) * 7 / 2;
        kbc.OnFrameEnd();
    }
    /// A key change, then 5 ms: up to four set-1 bytes on the wire (about 1 ms each) and their processing
    void Key(PcKey key, bool pressed)
    {
        kbc.OnPcKey(key, pressed);
        Advance(5000);
    }
    void Tap(PcKey key)
    {
        Key(key, true);
        Key(key, false);
    }
    uint8_t Read(uint8_t high) { return kbc.ReadPort(static_cast<uint16_t>((high << 8) | 0xFE)); }

    /// The matrix as a program sees it: every half-row read alone (twice, the second pass counts: the firmware
    /// answers some reads right after a key change with "no key"), as closed positions
    profixt::MatrixMask Closed()
    {
        static constexpr uint8_t kRows[8] = {0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F};
        for (uint8_t high : kRows)
            Read(high);
        profixt::MatrixMask closed = 0;
        for (int row = 0; row < 8; ++row)
        {
            const uint8_t bits = static_cast<uint8_t>(~Read(kRows[row]) & 0x3F);
            closed |= static_cast<profixt::MatrixMask>(bits) << (row * 6);
        }
        return closed;
    }
};

/// "A EXT" for a mask of closed positions
std::string Describe(profixt::MatrixMask closed)
{
    static const char* const kNames[48] = {
        "CS", "Z", "X", "C", "V", "-", "A", "S", "D", "F", "G", "-", "Q", "W", "E", "R",
        "T", "-", "1", "2", "3", "4", "5", "-", "0", "9", "8", "7", "6", "-", "P", "O",
        "I", "U", "Y", "-", "ENT", "L", "K", "J", "H", "EXT", "SP", "SS", "M", "N", "B", "X15"};
    std::string out;
    for (int n = 0; n < 48; ++n)
    {
        if (closed & (profixt::MatrixMask{1} << n))
            out += std::string(out.empty() ? "" : " ") + kNames[n];
    }
    return out.empty() ? "(none)" : out;
}

profixt::MatrixMask Names(const char* spec)
{
    profixt::MatrixMask mask = 0;
    std::istringstream in(spec);
    std::string token;
    while (in >> token)
        mask |= profixt::PositionByName(token.c_str());
    return mask;
}

/// The keys a test walks through: every PC key but the lock keys, Pause and Print Screen (their own tests)
bool Walkable(PcKey key)
{
    return key != PcKey::NumLock && key != PcKey::ScrollLock && key != PcKey::Pause && key != PcKey::PrintScreen;
}

enum class Context
{
    Plain,
    Shift,
    NumLock,
    Mode2
};

/// The positions `key` closes on a fresh controller in `context` (a held Shift's own SS included)
profixt::MatrixMask KeyIn(Engine engine, PcKey key, Context context)
{
    Rig rig(engine);
    if (context == Context::Shift)
        rig.Key(PcKey::LeftShift, true);
    else if (context == Context::NumLock)
        rig.Tap(PcKey::NumLock);
    else if (context == Context::Mode2)
        rig.Tap(PcKey::ScrollLock);
    rig.Key(key, true);
    return rig.Closed();
}
}  // namespace

// --- the firmware alone ---

TEST(ProfiXtKbc_Test, TheImageIsTheReconstruction)
{
    Rig rig(Engine::Firmware);
    ASSERT_TRUE(rig.loaded) << "rom/profixt/profi-xt-v1.27.rom";
    EXPECT_EQ(rig.kbc.ImageCrc(), ProfiXtKbc::kReconstructedCrc);
    EXPECT_TRUE(rig.kbc.ImageNote().empty());
    const mcs48::Mcs48* cpu = rig.kbc.Cpu();
    ASSERT_NE(cpu, nullptr);
    // The 5 replaced bytes: EN I; CALL 05Fh; NOP; NOP
    EXPECT_EQ(cpu->Code(0x02E), 0x05);
    EXPECT_EQ(cpu->Code(0x02F), 0x14);
    EXPECT_EQ(cpu->Code(0x030), 0x5F);
    EXPECT_EQ(cpu->Code(0x031), 0x00);
    EXPECT_EQ(cpu->Code(0x032), 0x00);
    EXPECT_TRUE(cpu->InterruptsEnabled()) << "the firmware is past its EN I";
    EXPECT_EQ(rig.Read(0xFE), 0xFF) << "idle: no wait, the pull-ups";
    EXPECT_EQ(rig.kbc.LastWaitClocks(), 0u);
}

TEST(ProfiXtKbc_Test, TheOriginalDumpReceivesNoKey)
{
    // The dump as found (bytes 02Eh..032h = C8 11 20 17 37) never enables interrupts: a key never arrives
    Rig good(Engine::Firmware);
    ASSERT_TRUE(good.loaded);
    std::vector<uint8_t> image(2048);
    for (uint16_t a = 0; a < 2048; ++a)
        image[a] = good.kbc.Cpu()->Code(a);
    const uint8_t dump[5] = {0xC8, 0x11, 0x20, 0x17, 0x37};
    std::memcpy(&image[0x2E], dump, sizeof(dump));
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("profixt-dump.rom");
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));

    Rig rig(Engine::Firmware, path);
    ASSERT_TRUE(rig.loaded);
    EXPECT_EQ(rig.kbc.ImageCrc(), ProfiXtKbc::kOriginalDumpCrc);
    EXPECT_FALSE(rig.kbc.ImageNote().empty()) << "a warning for the unpatched dump";
    rig.Key(PcKey::A, true);
    EXPECT_FALSE(rig.kbc.Cpu()->InterruptsEnabled());
    EXPECT_EQ(rig.Read(0xFD), 0xFF) << "A is not seen";
    std::filesystem::remove(path);
}

TEST(ProfiXtKbc_Test, TheKeyMapOfTheResearch)
{
    // research-profi-keyboard.md section 2 "Key map from the firmware" (and the materials' simulator output)
    struct Row
    {
        PcKey key;
        const char* closes;
    };
    const Row rows[] = {
        {PcKey::A, "A"},           {PcKey::Digit1, "1"},       {PcKey::Space, "SP"},          {PcKey::Enter, "ENT"},
        {PcKey::Escape, "CS 1"},   {PcKey::Backspace, "CS 0"}, {PcKey::Tab, "CS I"},          {PcKey::LeftCtrl, "CS"},
        {PcKey::RightCtrl, "CS"},  {PcKey::LeftShift, "SS"},   {PcKey::RightShift, "SS"},     {PcKey::LeftAlt, "SS ENT"},
        {PcKey::RightAlt, "SS SP"}, {PcKey::CapsLock, "CS SS"}, {PcKey::Function1, "A EXT"},  {PcKey::Function10, "J EXT"},
        {PcKey::Function11, "SS Q"}, {PcKey::Function12, "SS W"}, {PcKey::Home, "K EXT"},     {PcKey::End, "L EXT"},
        {PcKey::PageUp, "M EXT"},  {PcKey::PageDown, "N EXT"}, {PcKey::Insert, "O EXT"},      {PcKey::Delete, "P EXT"},
        {PcKey::Up, "CS 7"},       {PcKey::Down, "CS 6"},      {PcKey::Left, "CS 5"},         {PcKey::Right, "CS 8"},
        {PcKey::KeypadEnter, "ENT"}, {PcKey::Minus, "SS J"},   {PcKey::Equal, "SS L"},        {PcKey::LeftBracket, "SS Y"},
        {PcKey::Semicolon, "SS O"}, {PcKey::Quote, "SS 7"},    {PcKey::Backquote, "SS W"},    {PcKey::Backslash, "SS D"},
        {PcKey::Comma, "SS N"},    {PcKey::Period, "SS M"},    {PcKey::Slash, "SS V"},        {PcKey::KeypadMultiply, "SS B"},
    };
    for (const Row& row : rows)
    {
        const profixt::MatrixMask closed = KeyIn(Engine::Firmware, row.key, Context::Plain);
        EXPECT_EQ(Describe(closed), Describe(Names(row.closes))) << pckey::Name(row.key);
    }
    // Shifted punctuation: Shift + - is SS + 0 (_), Shift + = is SS + K (+)
    EXPECT_EQ(Describe(KeyIn(Engine::Firmware, PcKey::Minus, Context::Shift)), Describe(Names("SS 0")));
    EXPECT_EQ(Describe(KeyIn(Engine::Firmware, PcKey::Equal, Context::Shift)), Describe(Names("SS K")));
    // Num Lock: the keypad's 7 is Home
    EXPECT_EQ(Describe(KeyIn(Engine::Firmware, PcKey::Keypad7, Context::NumLock)), Describe(Names("K EXT")));
    // Scroll Lock (the second mode): Left Shift is the key at A15 / KD5
    EXPECT_EQ(Describe(KeyIn(Engine::Firmware, PcKey::LeftShift, Context::Mode2)), Describe(Names("X15")));
}

TEST(ProfiXtKbc_Test, TheTableAgreesWithTheFirmware)
{
    // Every PC key in every context, on the firmware and on the table: the same positions
    for (Context context : {Context::Plain, Context::Shift, Context::NumLock, Context::Mode2})
    {
        for (int n = 1; n < static_cast<int>(PcKey::Count); ++n)
        {
            const PcKey key = static_cast<PcKey>(n);
            if (!Walkable(key))
                continue;
            const profixt::MatrixMask firmware = KeyIn(Engine::Firmware, key, context);
            const profixt::MatrixMask table = KeyIn(Engine::Table, key, context);
            EXPECT_EQ(Describe(table), Describe(firmware))
                << pckey::Name(key) << " context " << static_cast<int>(context);
        }
    }
}

TEST(ProfiXtKbc_Test, ExtIsOnlyOnHalfRowA14)
{
    for (Engine engine : {Engine::Firmware, Engine::Table})
    {
        Rig rig(engine);
        rig.Key(PcKey::Function1, true);
        rig.Closed();
        EXPECT_EQ(rig.Read(0xBF) & 0x20, 0x00) << "#BFFE bit 5: EXT";
        EXPECT_EQ(rig.Read(0xFD) & 0x3F, 0x3E) << "#FDFE: A";
        for (uint8_t high : {0xFE, 0xFB, 0xF7, 0xEF, 0xDF, 0x7F})
            EXPECT_EQ(rig.Read(high) & 0x20, 0x20) << "bit 5 on #" << std::hex << int(high) << "FE";
        EXPECT_EQ(rig.Read(0x00) & 0x3F, 0x1E) << "all half-rows: A and EXT";
        rig.Key(PcKey::Function1, false);
        EXPECT_EQ(rig.Read(0xBF), 0xFF);
    }
}

TEST(ProfiXtKbc_Test, AMultiRowReadIsNoKey)
{
    // A9 + A10 low (#F9FE) while A (A9) and Q (A10) are held: the firmware answers "no key" (3Fh). A8..A11 are
    // decoded first: with exactly one of them low A12..A15 are not looked at (#7EFE answers half-row A8)
    for (Engine engine : {Engine::Firmware, Engine::Table})
    {
        Rig rig(engine);
        rig.Key(PcKey::A, true);
        rig.Key(PcKey::Q, true);
        rig.Key(PcKey::Z, true);
        rig.Closed();
        EXPECT_EQ(rig.Read(0xF9) & 0x3F, 0x3F) << "#F9FE";
        EXPECT_EQ(rig.Read(0xFC) & 0x3F, 0x3F) << "#FCFE";
        EXPECT_EQ(rig.Read(0x3F) & 0x3F, 0x3F) << "#3FFE (A14 + A15)";
        EXPECT_EQ(rig.Read(0x7E) & 0x3F, 0x3D) << "#7EFE: half-row A8 (Z)";
        EXPECT_EQ(rig.Read(0x00) & 0x3F, 0x3C) << "#00FE: all half-rows AND (A, Q, Z on lines 0, 0, 1)";
    }
}

TEST(ProfiXtKbc_Test, WaitOnlyWhileAKeyIsHeld)
{
    for (Engine engine : {Engine::Firmware, Engine::Table})
    {
        Rig rig(engine);
        rig.Read(0xFE);
        EXPECT_EQ(rig.kbc.LastWaitClocks(), 0u) << "no key: no wait";
        rig.Key(PcKey::S, true);
        rig.Closed();
        rig.Read(0xFD);
        // From the firmware's idle loop: the 49-cycle read path plus up to 8 cycles of its poll (1.875 us each)
        const uint64_t cycles = rig.kbc.LastWaitClocks() / mcs48::Mcs48::kClocksPerCycle;
        EXPECT_GE(cycles, 49u);
        EXPECT_LE(cycles, 60u);
        rig.Key(PcKey::S, false);
        rig.Read(0xFD);
        EXPECT_EQ(rig.kbc.LastWaitClocks(), 0u) << "released: no wait again";
    }
}

TEST(ProfiXtKbc_Test, CtrlAltDelDrivesTheResetLine)
{
    Rig rig(Engine::Firmware);
    rig.Key(PcKey::LeftCtrl, true);
    rig.Key(PcKey::LeftAlt, true);
    EXPECT_FALSE(rig.kbc.ResetAsserted());
    rig.Key(PcKey::Delete, true);
    EXPECT_TRUE(rig.kbc.ResetAsserted()) << "latch bit 7 low: /HRESET";
    EXPECT_EQ(rig.kbc.Latch() & 0x80, 0x00);
    rig.Key(PcKey::Delete, false);
    rig.Key(PcKey::LeftAlt, false);
    rig.Key(PcKey::LeftCtrl, false);
    // The keypad's Del with the right-hand modifiers does it too
    Rig other(Engine::Firmware);
    other.Key(PcKey::RightCtrl, true);
    other.Key(PcKey::RightAlt, true);
    other.Key(PcKey::KeypadDecimal, true);
    EXPECT_TRUE(other.kbc.ResetAsserted());
}

TEST(ProfiXtKbc_Test, ScrollLockAndAafeSwitchTheMode)
{
    for (Engine engine : {Engine::Firmware, Engine::Table})
    {
        Rig rig(engine);
        EXPECT_FALSE(rig.kbc.Mode2());
        rig.Tap(PcKey::ScrollLock);
        EXPECT_TRUE(rig.kbc.Mode2());
        rig.Tap(PcKey::ScrollLock);
        EXPECT_FALSE(rig.kbc.Mode2());
        // #AAFE / #55FE switch it while a key is held (no key: no wait, the controller does not see the read)
        rig.Read(0xAA);
        EXPECT_FALSE(rig.kbc.Mode2()) << "idle: unseen";
        rig.Key(PcKey::A, true);
        rig.Closed();
        EXPECT_EQ(rig.Read(0xAA) & 0x3F, 0x00) << "the answer is 80h on the latch";
        EXPECT_TRUE(rig.kbc.Mode2());
        rig.Read(0x55);
        EXPECT_FALSE(rig.kbc.Mode2());
    }
}

TEST(ProfiXtKbc_Test, NumLockTogglesTheKeypad)
{
    for (Engine engine : {Engine::Firmware, Engine::Table})
    {
        Rig rig(engine);
        EXPECT_FALSE(rig.kbc.NumLock());
        rig.Tap(PcKey::NumLock);
        EXPECT_TRUE(rig.kbc.NumLock());
        rig.Key(PcKey::Keypad8, true);
        EXPECT_EQ(Describe(rig.Closed()), Describe(Names("CS 7")));
    }
}

TEST(ProfiXtKbc_Test, SeveralKeysAtOnce)
{
    // Rollover: four letters and two F keys together
    Rig rig(Engine::Firmware);
    for (PcKey key : {PcKey::Q, PcKey::W, PcKey::E, PcKey::R, PcKey::Function1, PcKey::Function2})
        rig.Key(key, true);
    EXPECT_EQ(Describe(rig.Closed()), Describe(Names("Q W E R A B EXT")));
    rig.kbc.ReleaseAllPcKeys();
    rig.Advance(20000);
    EXPECT_EQ(rig.Closed(), 0u);
}

TEST(ProfiXtKbc_Test, AHeldKeyRepeats)
{
    Rig rig(Engine::Firmware);
    rig.Key(PcKey::B, true);
    const uint64_t instructions = rig.kbc.Cpu()->Instructions();
    rig.Advance(800000);   // past the 500 ms delay: three repeats at 10.9 per second
    EXPECT_GT(rig.kbc.Cpu()->Instructions(), instructions);
    EXPECT_EQ(Describe(rig.Closed()), Describe(Names("B")));
    rig.Key(PcKey::B, false);
    EXPECT_EQ(rig.Closed(), 0u);
}

TEST(ProfiXtKbc_Test, AutomationMapsTheShiftsAcross)
{
    // Caps Shift is Ctrl and Symbol Shift is Shift on this controller; combination keys press their parts
    ProfiXtKbc kbc(nullptr);
    using Keys = std::vector<PcKey>;
    EXPECT_EQ(kbc.PcKeysForZxKey(ZXKEY_CAPS_SHIFT), (Keys{PcKey::LeftCtrl}));
    EXPECT_EQ(kbc.PcKeysForZxKey(ZXKEY_SYM_SHIFT), (Keys{PcKey::LeftShift}));
    EXPECT_EQ(kbc.PcKeysForZxKey(ZXKEY_A), (Keys{PcKey::A}));
    EXPECT_EQ(kbc.PcKeysForZxKey(ZXKEY_ENTER), (Keys{PcKey::Enter}));
    EXPECT_EQ(kbc.PcKeysForZxKey(ZXKEY_EXT_UP), (Keys{PcKey::LeftCtrl, PcKey::Digit7}));
    EXPECT_EQ(kbc.PcKeysForZxKey(ZXKEY_EXT_DBLQUOTE), (Keys{PcKey::LeftShift, PcKey::P}));
    // '&' is Symbol Shift + 6: Shift + 6 here (the controller does not translate Shift + digit)
    EXPECT_EQ(kbc.PcKeysForCharacter('&', {ZXKEY_SYM_SHIFT, ZXKEY_6}), (Keys{PcKey::LeftShift, PcKey::Digit6}));
    EXPECT_TRUE(kbc.ReplacesMatrix());
}

TEST(ProfiXtKbc_Test, TypedCombinationsReachTheMatrix)
{
    // What the automation's reverse map sends gives the ZX combination back, for every combination TypeText uses
    for (Engine engine : {Engine::Firmware, Engine::Table})
    {
        for (ZXKeysEnum base : {ZXKEY_A, ZXKEY_Z, ZXKEY_P, ZXKEY_0, ZXKEY_6, ZXKEY_7, ZXKEY_SPACE, ZXKEY_ENTER, ZXKEY_M})
        {
            for (ZXKeysEnum modifier : {ZXKEY_NONE, ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT})
            {
                Rig rig(engine);
                std::vector<ZXKeysEnum> zx;
                if (modifier != ZXKEY_NONE)
                    zx.push_back(modifier);
                zx.push_back(base);
                for (PcKey pc : rig.kbc.PcKeysForCharacter('?', zx))
                    rig.Key(pc, true);
                profixt::MatrixMask expected = 0;
                auto name = [](ZXKeysEnum k) -> std::string {
                    if (k == ZXKEY_CAPS_SHIFT) return "CS";
                    if (k == ZXKEY_SYM_SHIFT) return "SS";
                    if (k == ZXKEY_SPACE) return "SP";
                    if (k == ZXKEY_ENTER) return "ENT";
                    return std::string(1, static_cast<char>(k));
                };
                for (ZXKeysEnum k : zx)
                    expected |= profixt::PositionByName(name(k).c_str());
                EXPECT_EQ(Describe(rig.Closed()), Describe(expected)) << name(modifier) << "+" << name(base);
            }
        }
    }
}

TEST(ProfiXtKbc_Test, StateRoundTrip)
{
    Rig rig(Engine::Firmware);
    rig.Key(PcKey::Q, true);
    auto saved = std::make_unique<ProfiXtKbc::State>();
    rig.kbc.SaveState(*saved);
    const uint16_t pc = rig.kbc.Cpu()->Pc();
    rig.Key(PcKey::W, true);
    rig.Read(0xFB);
    ASSERT_NE(rig.kbc.Cpu()->Clock(), saved->cpu.clock);
    ASSERT_TRUE(rig.kbc.LoadState(*saved));
    EXPECT_EQ(rig.kbc.Cpu()->Pc(), pc);
    auto again = std::make_unique<ProfiXtKbc::State>();
    rig.kbc.SaveState(*again);
    EXPECT_EQ(std::memcmp(saved.get(), again.get(), sizeof(ProfiXtKbc::State)), 0) << "the blob comes back byte for byte";
    saved->engine = static_cast<uint8_t>(Engine::Table);
    EXPECT_FALSE(rig.kbc.LoadState(*saved)) << "a blob of the other engine is refused";
}

// --- on the Profi ---

class ProfiXtKbcMachine_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;

    void SetUp() override { _manager = EmulatorManager::GetInstance(); }
    void TearDown() override
    {
        if (_emulator)
            _manager->RemoveEmulator(_emulator->GetId());
    }

    void Create(const char* model, ProfiKeyboard keyboard = ProfiKeyboard::Default)
    {
        std::function<void(CONFIG&)> override;
        if (keyboard != ProfiKeyboard::Default)
            override = [keyboard](CONFIG& config) { config.profi_keyboard = static_cast<uint8_t>(keyboard); };
        _emulator = _manager->CreateEmulatorWithModel("profixt", model, LoggerLevel::LogError, nullptr, override);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }
    PortDecoder_Profi* Decoder() { return dynamic_cast<PortDecoder_Profi*>(_context->pPortDecoder); }
    ProfiXtKbc* Kbc() { return Decoder() ? Decoder()->GetKeyboardController() : nullptr; }
    uint8_t In(uint16_t port) { return _context->pCore->GetZ80()->in(port); }
    /// IN with the T-states it cost the Z80 beyond its own 12 T (#FE is contention-free on the Profi)
    uint32_t InWait(uint16_t port, uint8_t& value)
    {
        Z80* z80 = _context->pCore->GetZ80();
        const uint32_t before = z80->t;
        value = z80->in(port);
        return z80->t - before;
    }
};

TEST_F(ProfiXtKbcMachine_Test, TheBoardsGetTheirKeyboards)
{
    Create("PROFI");
    ASSERT_NE(Kbc(), nullptr) << "v5: the PROFI-XT controller by default";
    EXPECT_EQ(Decoder()->GetKeyboardKind(), ProfiKeyboard::Xt);
    EXPECT_EQ(Kbc()->GetEngine(), Engine::Firmware);
    EXPECT_EQ(_context->pKeyboard->GetPs2Sink(), Kbc());
    EXPECT_EQ(_context->pKeyboard->EffectiveHostRoute(), HostKeyboardRoute::Ps2) << "Auto: the controller alone";
    _manager->RemoveEmulator(_emulator->GetId());

    Create("PROFI3");
    EXPECT_EQ(Kbc(), nullptr) << "v3: the matrix keyboard by default";
    EXPECT_EQ(Decoder()->GetKeyboardKind(), ProfiKeyboard::Matrix);
    EXPECT_EQ(_context->pKeyboard->GetPs2Sink(), nullptr);
    EXPECT_EQ(_context->pKeyboard->EffectiveHostRoute(), HostKeyboardRoute::Matrix);
    _manager->RemoveEmulator(_emulator->GetId());

    Create("PROFI", ProfiKeyboard::XtTable);
    ASSERT_NE(Kbc(), nullptr);
    EXPECT_EQ(Kbc()->GetEngine(), Engine::Table);
    _manager->RemoveEmulator(_emulator->GetId());

    Create("PROFI", ProfiKeyboard::Matrix);
    EXPECT_EQ(Kbc(), nullptr);
    EXPECT_EQ(In(0xBFFE) & 0x20, 0x20) << "no controller: KD5 pulled up";
}

TEST_F(ProfiXtKbcMachine_Test, MatrixMachinesAreUnchanged)
{
    // The matrix keyboard: host keys on the ZX matrix, bit 5 = 1, no wait
    Create("PROFI", ProfiKeyboard::Matrix);
    _context->pKeyboard->PressKey(ZXKEY_A);
    uint8_t value = 0;
    const uint32_t cost = InWait(0xFDFE, value);
    EXPECT_EQ(value & 0x3F, 0x3E);
    _context->pKeyboard->ReleaseKey(ZXKEY_A);
    const uint32_t idle = InWait(0xFDFE, value);
    EXPECT_EQ(cost, idle) << "no wait from a matrix keyboard";
    _manager->RemoveEmulator(_emulator->GetId());

    Create("48K");
    EXPECT_EQ(_context->pKeyboard->GetPs2Sink(), nullptr);
    EXPECT_EQ(_context->pKeyboard->EffectiveHostRoute(), HostKeyboardRoute::Matrix);
}

TEST_F(ProfiXtKbcMachine_Test, F1ReadsAsAPlusExtWithAWait)
{
    // Hold F1 on the host: #FDFE bit 0 and #BFFE bit 5 read 0; the reads wait while it is held, not after
    Create("PROFI");
    _emulator->RunNFrames(2);
    uint8_t value = 0;
    const uint32_t idle = InWait(0xBFFE, value);
    EXPECT_EQ(value & 0x3F, 0x3F);
    _context->pKeyboard->ApplyPcKey(PcKey::Function1, true);
    _emulator->RunNFrames(2);
    In(0xBFFE);
    const uint32_t held = InWait(0xBFFE, value);
    EXPECT_EQ(value & 0x3F, 0x1F) << "EXT on #BFFE bit 5";
    EXPECT_EQ(In(0xFDFE) & 0x3F, 0x3E) << "A";
    EXPECT_EQ(In(0xFEFE) & 0x20, 0x20);
    // research: about 50-110 us (175-380 T at 3.5 MHz) from the firmware's idle loop, longer while it is busy
    EXPECT_GT(held, idle + 150) << "the controller holds the Z80";
    EXPECT_LT(held, idle + 600);
    _context->pKeyboard->ApplyPcKey(PcKey::Function1, false);
    _emulator->RunNFrames(2);
    EXPECT_EQ(InWait(0xBFFE, value), idle) << "released: no wait";
}

TEST_F(ProfiXtKbcMachine_Test, CtrlAltDelResetsTheMachine)
{
    Create("PROFI");
    _emulator->RunNFrames(10);
    for (PcKey key : {PcKey::LeftCtrl, PcKey::LeftAlt})
    {
        _context->pKeyboard->ApplyPcKey(key, true);
        _emulator->RunNFrames(1);
    }
    const uint64_t frames = _context->emulatorState.frame_counter;
    ASSERT_GE(frames, 12u);
    _context->pKeyboard->ApplyPcKey(PcKey::Delete, true);
    _emulator->RunNFrames(1);
    EXPECT_TRUE(Kbc()->ResetAsserted());
    EXPECT_LT(_context->emulatorState.frame_counter, frames) << "the machine was reset (frame counter from 0 again)";
    for (PcKey key : {PcKey::Delete, PcKey::LeftAlt, PcKey::LeftCtrl})
        _context->pKeyboard->ApplyPcKey(key, false);
}

TEST_F(ProfiXtKbcMachine_Test, OnV3KeyboardExtLandsOnBit7)
{
    // XT on a v3 board: KEYB has no KD5 line (bit 5 = 1) and its pin 2 is KD7: EXT reads on bit 7
    Create("PROFI3", ProfiKeyboard::Xt);
    ASSERT_NE(Kbc(), nullptr);
    _emulator->RunNFrames(2);   // the controller's power-on: a key sent before its EN I is lost, as on the board
    _context->pKeyboard->ApplyPcKey(PcKey::Function1, true);
    _emulator->RunNFrames(2);
    In(0xBFFE);
    const uint8_t value = In(0xBFFE);
    EXPECT_EQ(value & 0x20, 0x20);
    EXPECT_EQ(value & 0x80, 0x00);
}

TEST_F(ProfiXtKbcMachine_Test, Bios20SeesF1AsCode75h)
{
    // BIOS 2.0 (rom/profi/bios20.rom, SYS page): the keyboard scan at 0600h reads 6 bits per half-row and collects
    // KD5 of every half-row into 99DAh; the remap at 127Ah turns the letter of an EXT key into its code (F1 = A +
    // EXT -> 75h). Both are called in place, in the SYS ROM (a fetch from RAM would drop the DOS latch and with it
    // the SYS page), returning to a JR $ at 0E24h. Boots the real ROM: slower than 50 ms by nature
    Create("PROFI");
    const std::filesystem::path rom = TestPathHelper::FindProjectRoot() / "data/rom/profi/bios20.rom";
    ASSERT_TRUE(std::filesystem::exists(rom));
    const auto u8 = rom.u8string();
    std::strncpy(_context->config.profi_rom_path, std::string(u8.begin(), u8.end()).c_str(),
                 sizeof(_context->config.profi_rom_path) - 1);
    ASSERT_TRUE(_context->pCore->GetROM()->LoadROM());
    _emulator->Reset();
    _emulator->RunNFrames(25);   // the BIOS boots and waits for a disk, in its SYS page
    Memory& memory = *_context->pMemory;
    ASSERT_EQ(memory.DirectReadFromZ80Memory(0x0600), 0xD9) << "the BIOS page is at 0000h";
    ASSERT_EQ(memory.DirectReadFromZ80Memory(0x0E24), 0x18) << "JR $";

    _context->pKeyboard->ApplyPcKey(PcKey::Function1, true);
    _emulator->RunNFrames(3);

    Z80* z80 = _context->pCore->GetZ80();
    auto call = [&](uint16_t routine) {
        z80->iff1 = z80->iff2 = 0;
        z80->sp = 0x7E00;
        memory.DirectWriteToZ80Memory(0x7E00, 0x24);   // return address 0E24h
        memory.DirectWriteToZ80Memory(0x7E01, 0x0E);
        z80->pc = routine;
        for (int n = 0; n < 100000 && z80->pc != 0x0E24; ++n)
            z80->Z80Step(true);
        return z80->pc == 0x0E24;
    };
    ASSERT_TRUE(call(0x0600)) << "the scan returns";
    EXPECT_NE(memory.DirectReadFromZ80Memory(0x99DA) & 0x40, 0) << "99DAh bit 6: KD5 of half-row A14 (EXT)";
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0x99DA) & 0xBF, 0) << "no KD5 on the other half-rows";
    ASSERT_TRUE(call(0x127A)) << "the remap returns";
    EXPECT_EQ(z80->a, 0x75) << "F1";
    _context->pKeyboard->ApplyPcKey(PcKey::Function1, false);
}

TEST_F(ProfiXtKbcMachine_Test, TtdSeekReplaysAKeyPressExactly)
{
    // Record a PC key through the controller, seek back, replay: the controller ends in the recorded state
    Create("PROFI");
    FeatureManager* features = _emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    _context->pMemory->UpdateFeatureCache();
    ttd::TimeTravelController* ttd = _context->pTimeTravelController;
    ASSERT_NE(ttd, nullptr);
    ASSERT_TRUE(ttd->StartRecording());
    _emulator->RunNFrames(2);
    const uint64_t before = _context->emulatorState.frame_counter;
    _context->pDebugManager->GetKeyboardManager()->TapKey("pc.f1", 3);
    _emulator->RunNFrames(10);
    const uint64_t end = _context->emulatorState.frame_counter;
    auto recorded = std::make_unique<ProfiXtKbc::State>();
    Kbc()->SaveState(*recorded);
    ttd->StopRecording();

    ASSERT_TRUE(ttd->SeekTo({before, 0}));
    _emulator->RunNFrames(static_cast<int>(end - before));
    ASSERT_EQ(_context->emulatorState.frame_counter, end);
    auto replayed = std::make_unique<ProfiXtKbc::State>();
    Kbc()->SaveState(*replayed);
    EXPECT_EQ(replayed->cpu.clock, recorded->cpu.clock);
    EXPECT_EQ(replayed->cpu.pc, recorded->cpu.pc);
    EXPECT_EQ(std::memcmp(replayed->cpu.ram, recorded->cpu.ram, sizeof(recorded->cpu.ram)), 0);
    EXPECT_EQ(std::memcmp(replayed.get(), recorded.get(), sizeof(ProfiXtKbc::State)), 0) << "the whole controller as recorded";
}

TEST_F(ProfiXtKbcMachine_Test, AutomationTypesThroughTheController)
{
    // PressKey of a ZX key on a Profi with the controller: the controller hears Ctrl for Caps Shift
    Create("PROFI");
    _emulator->RunNFrames(2);
    DebugKeyboardManager* keys = _context->pDebugManager->GetKeyboardManager();
    keys->PressKey(ZXKEY_CAPS_SHIFT);
    _emulator->RunNFrames(2);
    EXPECT_TRUE(Kbc()->KeyHeld(PcKey::LeftCtrl));
    EXPECT_FALSE(Kbc()->KeyHeld(PcKey::LeftShift));
    In(0xFEFE);
    EXPECT_EQ(In(0xFEFE) & 0x01, 0x00) << "Caps Shift on the matrix through the controller";
    keys->ReleaseKey(ZXKEY_CAPS_SHIFT);
    _emulator->RunNFrames(2);
    EXPECT_FALSE(Kbc()->KeyHeld(PcKey::LeftCtrl));
}


