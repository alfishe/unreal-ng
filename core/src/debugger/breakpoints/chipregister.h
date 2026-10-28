#pragma once

/// @file chipregister.h
/// @brief Sound chip register writes as breakpoints (BRK_CHIPREG, platform-debugging P3).
///
/// A chip's host port path reports a register write after it took effect, only while a chip register breakpoint
/// exists: `if (context->chipRegisterWatch) [[unlikely]] ReportChipRegisterWrite(...)`. The flag is the main CPU's
/// breakpoint manager's (its hot state), so a machine nobody debugs pays one predictable branch per OUT to a chip.
/// The stop is the main CPU's, after its OUT, like the host side of the General Sound mailbox.

#include <cstdint>
#include <cstring>

class EmulatorContext;

/// The register files a breakpoint can name. `unit` tells chips of a kind apart on one device (the TurboSound's two
/// AYs, the Next's three); a breakpoint takes one unit or all
enum class SoundChipKind : uint8_t
{
    Ay = 0,    ///< AY-3-8910 / YM2149 and the SSG part of a YM2203 (TS-FM, MultiSound): registers 0-15
    Fm,        ///< the FM part of a YM2203 (TS-FM, MultiSound): registers #10-#FF
    Saa,       ///< SAA1099 (MultiSound): registers 0-#1F
    Opl4,      ///< MoonSound OPL4 FM: bank 0 #000-#0FF, bank 1 #100-#1FF
    Opl4Wave,  ///< MoonSound OPL4 wave part: registers 0-#FF
    Dac,       ///< Covox / SounDrive: the register is the channel (0-3), the value the sample
    Cbl,       ///< the Sprinter's Covox Blaster: register 0 the data (code #88: the DAC, or the ring's next entry;
               ///< the accelerator's copies into page #FD too), 1 the control (code #89)
    Count
};

constexpr uint8_t kSoundChipAnyUnit = 0xFF;

/// The surfaces' names ("ay", "fm", "saa", "opl4", "opl4wave", "dac", "cbl")
inline const char* SoundChipKindName(SoundChipKind kind)
{
    switch (kind)
    {
        case SoundChipKind::Ay: return "ay";
        case SoundChipKind::Fm: return "fm";
        case SoundChipKind::Saa: return "saa";
        case SoundChipKind::Opl4: return "opl4";
        case SoundChipKind::Opl4Wave: return "opl4wave";
        case SoundChipKind::Dac: return "dac";
        case SoundChipKind::Cbl: return "cbl";
        case SoundChipKind::Count: break;
    }
    return "unknown";
}

/// The kind of a surface name; false when there is none
inline bool ParseSoundChipKind(const char* name, SoundChipKind& out)
{
    for (uint8_t k = 0; k < static_cast<uint8_t>(SoundChipKind::Count); k++)
    {
        if (std::strcmp(SoundChipKindName(static_cast<SoundChipKind>(k)), name) == 0)
        {
            out = static_cast<SoundChipKind>(k);
            return true;
        }
    }
    return false;
}

/// The highest register of a kind (a breakpoint's register range is checked against it)
constexpr uint16_t SoundChipLastRegister(SoundChipKind kind)
{
    switch (kind)
    {
        case SoundChipKind::Ay: return 0x0F;
        case SoundChipKind::Fm: return 0xFF;
        case SoundChipKind::Saa: return 0x1F;
        case SoundChipKind::Opl4: return 0x1FF;
        case SoundChipKind::Opl4Wave: return 0xFF;
        case SoundChipKind::Dac: return 0x03;
        case SoundChipKind::Cbl: return 0x01;
        case SoundChipKind::Count: break;
    }
    return 0;
}

/// A chip's register write by the main CPU (call it only while context->chipRegisterWatch is set): the main CPU's
/// chip register breakpoints see it; a stop ends the main CPU's run after its OUT
void ReportChipRegisterWrite(EmulatorContext* context, SoundChipKind kind, uint8_t unit, uint16_t reg, uint8_t value);
