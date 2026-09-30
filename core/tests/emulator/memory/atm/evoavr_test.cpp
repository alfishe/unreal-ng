// ZX-Evo BaseConf AVR behind the Gluk clock ports (EvoAvr): the register and
// extension-window semantics of pentevo avr/baseconf/trunk/src/rtc.c and
// version.c, and the battery-backed NVRAM file.

#include <cstdio>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/atm/evoavr.h"
#include "emulator/ports/models/portdecoder_atm3.h"

namespace
{
    constexpr time_t kFrozenTime = 1767268830;  // 2026-01-01 12:00:30 UTC

    uint8_t Read(EvoAvr& avr, uint8_t index)
    {
        avr.WriteAddress(index);
        return avr.ReadData();
    }

    void Write(EvoAvr& avr, uint8_t index, uint8_t value)
    {
        avr.WriteAddress(index);
        avr.WriteData(value);
    }

    std::string ReadWindow(EvoAvr& avr)
    {
        std::string bytes;
        for (unsigned index = 0xF0; index <= 0xFF; index++)
            bytes.push_back(static_cast<char>(Read(avr, static_cast<uint8_t>(index))));
        return bytes;
    }

    std::string Tag(const std::array<uint8_t, 16>& tag)
    {
        return std::string(reinterpret_cast<const char*>(tag.data()), tag.size());
    }
}  // namespace

/// The ERS "Baseconf:" / "AVR Boot:" lines: write the type to cell 0xF0, then
/// read 16 bytes (rom/mainmenu/src/call_cmos.a80 GET_VERS_EVO)
TEST(EvoAvr_Test, VersionWindowServesTheReleasedTags)
{
    EvoAvr avr;
    avr.SetFixedTime(kFrozenTime);

    Write(avr, 0xF0, EvoAvr::kExtFirmwareVersion);
    EXPECT_EQ(ReadWindow(avr), Tag(EvoAvr::kFirmwareVersion));
    EXPECT_EQ(ReadWindow(avr).substr(0, 8), "ZXEvo 4M");

    Write(avr, 0xF7, EvoAvr::kExtBootloaderVersion);  // any cell of the window selects
    EXPECT_EQ(ReadWindow(avr), Tag(EvoAvr::kBootloaderVersion));
    EXPECT_EQ(ReadWindow(avr).substr(0, 12), "ZXEvoAVRBoot");
}

/// Date words decode to what the ERS prints: "07.01.2026" (release) and
/// "25.05.2019 beta" (version.h: bits 4..0 day, 8..5 month, 14..9 year-2000, 15 release)
TEST(EvoAvr_Test, VersionDatesDecodeLikeTheErs)
{
    auto date = [](const std::array<uint8_t, 16>& tag) {
        const unsigned word = tag[12] | (tag[13] << 8);
        char text[32];
        std::snprintf(text, sizeof(text), "%02u.%02u.20%02u%s", word & 31, (word >> 5) & 15, (word >> 9) & 63,
                      (word & 0x8000) ? "" : " beta");
        return std::string(text);
    };
    EXPECT_EQ(date(EvoAvr::kFirmwareVersion), "07.01.2026");
    EXPECT_EQ(date(EvoAvr::kBootloaderVersion), "25.05.2019 beta");
}

/// The ERS decides "no Evo AVR" when cell 0xF0 reads back the type it just
/// wrote; a plain CMOS RAM cell does exactly that. The selector must never be stored
TEST(EvoAvr_Test, ExtensionSelectNeverEchoes)
{
    EvoAvr avr;
    for (uint8_t type : {EvoAvr::kExtFirmwareVersion, EvoAvr::kExtBootloaderVersion})
    {
        Write(avr, 0xF0, type);
        EXPECT_NE(Read(avr, 0xF0), type) << "type " << int(type);
    }

    Write(avr, 0xF0, EvoAvr::kExtPs2Log);
    EXPECT_EQ(Read(avr, 0xF0), 0x00) << "empty PS/2 log";

    Write(avr, 0xF0, EvoAvr::kExtModes);
    EXPECT_EQ(Read(avr, 0xF0), EvoAvr::kModesRaster48K) << "modes register: 48K raster, TV, no tape-out";
    EXPECT_EQ(Read(avr, 0xF1), 0xFF) << "only cell 0xF0 carries the modes register";

    Write(avr, 0xF0, 0x0E);  // TS-Conf AVR config extension: not in the BaseConf firmware
    EXPECT_EQ(Read(avr, 0xF0), 0xFF);
}

/// Register C bit 7 turns cells 0xF0-0xFF into a window on the 4 KiB EEPROM at
/// page A << 4 (rtc.c:164-184); an erased EEPROM reads #FF
TEST(EvoAvr_Test, EepromWindowPagedByRegisterA)
{
    EvoAvr avr;
    Write(avr, 0x0C, 0x80);
    Write(avr, 0x0A, 0x12);
    EXPECT_EQ(Read(avr, 0xF3), 0xFF) << "erased EEPROM";

    Write(avr, 0xF3, 0xAB);
    EXPECT_EQ(Read(avr, 0xF3), 0xAB);
    EXPECT_EQ(Read(avr, 0x0A), 0x12) << "register A reads the page";

    Write(avr, 0x0A, 0x13);
    EXPECT_EQ(Read(avr, 0xF3), 0xFF) << "another page";
    Write(avr, 0x0A, 0x12);
    EXPECT_EQ(Read(avr, 0xF3), 0xAB);

    Write(avr, 0x0C, 0x00);  // back to the extension window
    Write(avr, 0xF0, EvoAvr::kExtFirmwareVersion);
    EXPECT_EQ(Read(avr, 0xF0), EvoAvr::kFirmwareVersion[0]);
}

/// Registers B, C, D (rtc.c:352-378, 486-510)
TEST(EvoAvr_Test, RegistersBCDFollowTheAvrFirmware)
{
    EvoAvr avr;
    avr.SetFixedTime(kFrozenTime);  // no update-ended flag while time is frozen

    Write(avr, 0x0B, 0xFF);
    EXPECT_EQ(Read(avr, 0x0B), 0x06) << "B keeps only the binary-mode bit, bit 1 reads 1";

    EXPECT_EQ(Read(avr, 0x0C), 0x00);
    avr.SetSdStatus(/*present*/ true, /*writeProtected*/ true);
    EXPECT_EQ(Read(avr, 0x0C), 0x0C) << "b3 SD present, b2 write-protected";
    Write(avr, 0x0C, 0x02);
    EXPECT_EQ(Read(avr, 0x0C), 0x0E) << "b1 Caps LED";
    Write(avr, 0x0C, 0x80);
    EXPECT_EQ(Read(avr, 0x0C) & 0x80, 0x80) << "b7 EEPROM mode";

    EXPECT_EQ(Read(avr, 0x0D), 0x80);
    avr.OnPcKey(PcKey::LeftCtrl, true);
    avr.OnPcKey(PcKey::LeftShift, true);
    avr.OnPcKey(PcKey::Function12, true);
    EXPECT_EQ(Read(avr, 0x0D), 0xD1) << "L Ctrl, L Shift, F12";
    Write(avr, 0x0D, 0x00);
    EXPECT_EQ(Read(avr, 0x0D), 0xD1) << "D is read-only";
}

/// NVRAM cells 0x0E-0xEF are ordinary battery-backed RAM (the ERS settings)
TEST(EvoAvr_Test, NvramCellsAreRam)
{
    EvoAvr avr;
    Write(avr, 0x20, 0x5A);
    Write(avr, 0xED, 0x02);  // ERS reset target = ProfROM
    EXPECT_EQ(Read(avr, 0x20), 0x5A);
    EXPECT_EQ(Read(avr, 0xED), 0x02);
}

/// NvramFile round trip: the NVRAM cells and the EEPROM survive; a missing or
/// truncated file leaves the power-on contents
TEST(EvoAvr_Test, NvramFileRoundTrip)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("zxevo-nvram.bin");

    {
        EvoAvr avr;
        Write(avr, 0x20, 0x5A);
        Write(avr, 0xEB, 0x41);
        Write(avr, 0x0C, 0x80);
        Write(avr, 0x0A, 0x00);
        Write(avr, 0xF0, 'K');
        ASSERT_TRUE(avr.SaveNvram(path));
    }

    EvoAvr restored;
    ASSERT_TRUE(restored.LoadNvram(path));
    EXPECT_EQ(Read(restored, 0x20), 0x5A);
    EXPECT_EQ(Read(restored, 0xEB), 0x41);
    Write(restored, 0x0C, 0x80);
    Write(restored, 0x0A, 0x00);
    EXPECT_EQ(Read(restored, 0xF0), 'K') << "EEPROM restored";

    {
        std::ofstream truncated(path, std::ios::binary | std::ios::trunc);
        truncated << "short";
    }
    EvoAvr rejected;
    EXPECT_FALSE(rejected.LoadNvram(path));
    EXPECT_EQ(Read(rejected, 0x20), 0x00) << "power-on contents kept";
    EXPECT_FALSE(rejected.LoadNvram(path + ".missing"));

    std::remove(path.c_str());
}

/// On the machine: the decoder saves the NVRAM file when the machine goes away
/// ([EVO] NvramFile), and the Gluk ports reach the AVR
TEST(EvoAvr_Test, MachineSavesNvramOnShutdown)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("zxevo-nvram-machine.bin");
    std::remove(path.c_str());

    auto emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("evoavr-nvram", "ATM3", LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    EmulatorContext* context = emulator->GetContext();
    std::snprintf(context->config.atm.evo_nvram_path, sizeof(context->config.atm.evo_nvram_path), "%s", path.c_str());

    // Shadow on (#BF bit 0): the clock answers on #DEF7 / #BEF7 without #EFF7 bit 7
    auto* decoder = dynamic_cast<PortDecoder_ATM3*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    decoder->DecodePortOut(0x00BF, 0x01, 0x0000);
    decoder->DecodePortOut(0xDEF7, 0x30, 0x0000);
    decoder->DecodePortOut(0xBEF7, 0xC3, 0x0000);
    EXPECT_EQ(decoder->DecodePortIn(0xBEF7, 0x0000), 0xC3);

    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
    emulator.reset();

    EvoAvr reloaded;
    ASSERT_TRUE(reloaded.LoadNvram(path)) << "the NVRAM file must be written on shutdown";
    EXPECT_EQ(Read(reloaded, 0x30), 0xC3);

    std::remove(path.c_str());
}

/// The AVR ignores SET, so the ERS / BaseConf set the date one field at a time
/// and each field must read back as written (was: day 31 of a 30-day month
/// folded into the 1st of the next month before the month was written)
TEST(EvoAvr_Test, DateSetFieldByFieldWithoutSet)
{
    EvoAvr avr;
    avr.SetFixedTime(1759140000);  // 2025-09-29, a 30-day month on every host zone
    Write(avr, 0x0B, 0x80);        // SET: ignored by the AVR
    Write(avr, 0x07, 0x31);
    Write(avr, 0x08, 0x12);
    Write(avr, 0x09, 0x99);
    EXPECT_EQ(Read(avr, 0x07), 0x31);
    EXPECT_EQ(Read(avr, 0x08), 0x12);
    EXPECT_EQ(Read(avr, 0x09), 0x99);
}

// --- PS/2 keyboard (ps2.c ps2keyboard_parse / to_log / from_log, zx.c to_zx) ---

namespace
{
    /// Every byte the Z80 pops from the log until it reads 0 (at most 20)
    std::vector<uint8_t> DrainLog(EvoAvr& avr)
    {
        Write(avr, 0xF0, EvoAvr::kExtPs2Log);
        std::vector<uint8_t> bytes;
        for (int i = 0; i < 20; i++)
        {
            const uint8_t byte = Read(avr, 0xF0);
            if (byte == 0)
                break;
            bytes.push_back(byte);
        }
        return bytes;
    }

    void Tap(EvoAvr& avr, PcKey key)
    {
        avr.OnPcKey(key, true);
        avr.OnPcKey(key, false);
    }
}

/// AVR-2 / PS2-2: a key's make and break bytes come out in order; an empty log reads 0
TEST(EvoAvr_Test, Ps2LogReturnsMakeAndBreakBytes)
{
    EvoAvr avr;
    EXPECT_TRUE(DrainLog(avr).empty()) << "power-on: the log is in its reset state";

    Tap(avr, PcKey::A);
    EXPECT_EQ(DrainLog(avr), (std::vector<uint8_t>{0x1C, 0xF0, 0x1C}));

    Tap(avr, PcKey::Up);
    EXPECT_EQ(DrainLog(avr), (std::vector<uint8_t>{0xE0, 0x75, 0xE0, 0xF0, 0x75})) << "extended key: E0 prefix";
}

/// A read of the log with another extension type selected does not pop it
TEST(EvoAvr_Test, Ps2LogPopsOnlyThroughExtensionType2)
{
    EvoAvr avr;
    Tap(avr, PcKey::B);
    Write(avr, 0xF0, EvoAvr::kExtFirmwareVersion);
    EXPECT_EQ(Read(avr, 0xF0), EvoAvr::kFirmwareVersion[0]);
    EXPECT_EQ(avr.PeekRegister(0xF0), EvoAvr::kFirmwareVersion[0]);

    Write(avr, 0xF0, EvoAvr::kExtPs2Log);
    EXPECT_EQ(avr.PeekRegister(0xF5), 0x32) << "a peek shows the oldest byte";
    EXPECT_EQ(avr.PeekRegister(0xF5), 0x32) << "and does not pop it";
    EXPECT_EQ(Read(avr, 0xF5), 0x32) << "any cell of the window pops";
    EXPECT_EQ(Read(avr, 0xF0), 0xF0);
    EXPECT_EQ(Read(avr, 0xF0), 0x32);
    EXPECT_EQ(Read(avr, 0xF0), 0x00);
}

/// AVR-2 / PS2-2: the ring holds 15 bytes; the 16th overflows it, the Z80 reads
/// #FF once, then the log starts over (reads 0 until a new key starts)
TEST(EvoAvr_Test, Ps2LogOverflowReadsFFThenResets)
{
    EvoAvr avr;
    for (PcKey key : {PcKey::A, PcKey::B, PcKey::C, PcKey::D, PcKey::E})
        Tap(avr, key);  // 5 keys x 3 bytes = 15: full, not overflowed
    EXPECT_EQ(avr.GetPs2LogCount(), 15u);
    EXPECT_FALSE(avr.IsPs2LogOverflow());

    avr.OnPcKey(PcKey::F, true);  // the 16th byte
    EXPECT_TRUE(avr.IsPs2LogOverflow());

    Write(avr, 0xF0, EvoAvr::kExtPs2Log);
    EXPECT_EQ(Read(avr, 0xF0), 0xFF) << "overflow";
    EXPECT_EQ(Read(avr, 0xF0), 0x00) << "then the log is reset";

    // The log takes bytes again from the next key sequence on
    avr.OnPcKey(PcKey::F, false);
    Tap(avr, PcKey::G);
    EXPECT_EQ(DrainLog(avr), (std::vector<uint8_t>{0xF0, 0x2B, 0x34, 0xF0, 0x34}));
}

/// After a log reset the first byte logged must start a key sequence: the rest
/// of a key cut in half by the reset is dropped (ps2.c:175)
TEST(EvoAvr_Test, Ps2LogAfterResetStartsAtAKey)
{
    EvoAvr avr;
    avr.ReceivePs2Byte(0xE0);  // Up: E0 ...
    Write(avr, 0x0C, 0x01);    // log reset in between
    avr.ReceivePs2Byte(0x75);  // ... 75: a continuation, not logged
    EXPECT_TRUE(DrainLog(avr).empty());

    Tap(avr, PcKey::A);
    EXPECT_EQ(DrainLog(avr), (std::vector<uint8_t>{0x1C, 0xF0, 0x1C}));
}

/// Register C bit 0 = 1 clears the log (rtc.c:491-510); the Caps LED bit is kept apart
TEST(EvoAvr_Test, Ps2LogClearedByRegisterC)
{
    EvoAvr avr;
    Tap(avr, PcKey::A);
    Write(avr, 0x0C, 0x01);
    EXPECT_TRUE(DrainLog(avr).empty());
    EXPECT_EQ(Read(avr, 0x0C) & 0x01, 0x00) << "bit 0 is a command, not the tape-out mode";
}

/// Pause (E1 14 77 E1 F0 14 F0 77) is never logged; protocol bytes neither
TEST(EvoAvr_Test, Ps2PauseAndProtocolBytesNotLogged)
{
    EvoAvr avr;
    Tap(avr, PcKey::Pause);
    for (uint8_t byte : {0xFA, 0xFE, 0xEE, 0xAA})
        avr.ReceivePs2Byte(byte);
    Tap(avr, PcKey::A);
    EXPECT_EQ(DrainLog(avr), (std::vector<uint8_t>{0x1C, 0xF0, 0x1C}));
}

/// PS2-3: register D follows the modifier keys' make and break (zx.c to_zx)
TEST(EvoAvr_Test, Ps2ModifiersInRegisterD)
{
    EvoAvr avr;
    const std::pair<PcKey, uint8_t> mods[] = {
        {PcKey::LeftCtrl, EvoAvr::kModLeftCtrl},   {PcKey::RightCtrl, EvoAvr::kModRightCtrl},
        {PcKey::LeftAlt, EvoAvr::kModLeftAlt},     {PcKey::RightAlt, EvoAvr::kModRightAlt},
        {PcKey::LeftShift, EvoAvr::kModLeftShift}, {PcKey::RightShift, EvoAvr::kModRightShift},
        {PcKey::Function12, EvoAvr::kModF12},
    };
    for (const auto& [key, mask] : mods)
    {
        avr.OnPcKey(key, true);
        EXPECT_EQ(Read(avr, 0x0D), 0x80 | mask) << pckey::Name(key);
        avr.OnPcKey(key, false);
        EXPECT_EQ(Read(avr, 0x0D), 0x80) << pckey::Name(key) << " released";
    }

    // Print Screen's fake Left Shift (E0 12) is not a Shift
    avr.OnPcKey(PcKey::PrintScreen, true);
    EXPECT_EQ(Read(avr, 0x0D), 0x80);
}

/// Release-all sends the breaks of the keys held; the modifiers drop
TEST(EvoAvr_Test, Ps2ReleaseAllSendsBreaks)
{
    EvoAvr avr;
    avr.OnPcKey(PcKey::LeftShift, true);
    avr.OnPcKey(PcKey::A, true);
    DrainLog(avr);

    avr.ReleaseAllPcKeys();
    EXPECT_EQ(Read(avr, 0x0D), 0x80);
    const std::vector<uint8_t> bytes = DrainLog(avr);
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0xF0, 0x1C, 0xF0, 0x12}));
}
