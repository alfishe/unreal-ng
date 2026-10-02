// eve-emu - SPI front end and host commands (spec §2).
#include "eve-internal.h"

namespace EveLib
{

namespace
{

constexpr uint8_t kHostActive = 0x00;
constexpr uint8_t kHostStandby = 0x41;
constexpr uint8_t kHostSleep = 0x42;
constexpr uint8_t kHostPowerDown = 0x43;
constexpr uint8_t kHostPowerDownAlt = 0x50;
constexpr uint8_t kHostClkExt = 0x44;
constexpr uint8_t kHostClkInt = 0x48;
constexpr uint8_t kHostPdRoms = 0x49;
constexpr uint8_t kHostClkSel = 0x61;
constexpr uint8_t kHostClkSelAlt = 0x62;
constexpr uint8_t kHostRstPulse = 0x68;

bool IsActive(const EveChip& chip)
{
    return chip.state.power.mode == PowerMode::Active;
}

void RecomputeClock(EveChip& chip)
{
    PowerState& power = chip.state.power;
    const uint64_t input = power.externalClock ? chip.externalClockHz : kInternalOscillatorHz;
    const uint64_t multiplier = power.multiplier != 0 ? power.multiplier : kDefaultClockMultiplier;
    const uint64_t hz = input * multiplier;
    power.systemClockHz = hz > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(hz);
}

void ExecuteHostCommand(EveChip& chip, uint8_t command, uint8_t parameter)
{
    PowerState& power = chip.state.power;
    switch (command)
    {
    case kHostActive:
        if (power.mode == PowerMode::PowerDown)
            CoreReset(chip, true);
        power.mode = PowerMode::Active;
        break;
    case kHostStandby:
        if (power.mode == PowerMode::Active)
            power.mode = PowerMode::Standby;
        break;
    case kHostSleep:
        if (power.mode != PowerMode::PowerDown)
            power.mode = PowerMode::Sleep;
        break;
    case kHostPowerDown:
    case kHostPowerDownAlt:
        // Core power off: all state except the host command configuration is lost.
        power.mode = PowerMode::PowerDown;
        CoreReset(chip, true);
        break;
    case kHostClkExt:
        if (!power.externalClock)
        {
            power.externalClock = 1;
            RecomputeClock(chip);
            CoreReset(chip, kRstPulseClearsRamG);
        }
        break;
    case kHostClkInt:
        if (power.externalClock)
        {
            power.externalClock = 0;
            RecomputeClock(chip);
            CoreReset(chip, kRstPulseClearsRamG);
        }
        break;
    case kHostPdRoms:
        power.romsPowerDown = parameter;
        break;
    case kHostClkSel:
    case kHostClkSelAlt:
        // Effective only in SLEEP [DS].
        if (power.mode == PowerMode::Sleep)
        {
            const uint8_t multiplier = parameter & 0x3F;
            if (multiplier == 0 || (multiplier >= kMinClockMultiplier && multiplier <= kMaxClockMultiplier))
            {
                power.multiplier = multiplier;
                power.pllRange = static_cast<uint8_t>(parameter >> 6);
                RecomputeClock(chip);
            }
        }
        break;
    case kHostRstPulse:
        CoreReset(chip, kRstPulseClearsRamG);
        break;
    default:
        break; // unknown host commands are ignored
    }
}

void EndTransaction(EveChip& chip)
{
    SpiState& spi = chip.state.spi;
    switch (spi.phase)
    {
    case SpiPhase::HostCommand:
        ExecuteHostCommand(chip, spi.header[0], spi.headerCount > 1 ? spi.header[1] : 0);
        break;
    case SpiPhase::Header:
    case SpiPhase::Ignore:
        // A memory read from address 0 while the chip sleeps is ACTIVE ("read twice")
        // [DS]: ACTIVE itself is the three bytes 00 00 00.
        if (!IsActive(chip) && spi.headerCount >= 3 && spi.header[0] == 0 && spi.header[1] == 0 &&
            spi.header[2] == 0)
            ExecuteHostCommand(chip, kHostActive, 0);
        break;
    case SpiPhase::WriteData:
        FlushPendingRegister(chip);
        if (kCmdbDropPartialWord)
            chip.state.bus.cmdbFill = 0;
        break;
    default:
        break;
    }
    // Nothing of a finished transaction matters: keep the idle state canonical, so two
    // chips that differ only in past reads save identical states.
    spi = SpiState{};
    spi.phase = SpiPhase::Idle;
}

void StartAccess(EveChip& chip)
{
    SpiState& spi = chip.state.spi;
    const uint8_t prefix = spi.header[0] & 0xC0;
    spi.address = (static_cast<uint32_t>(spi.header[0] & 0x3F) << 16) |
                  (static_cast<uint32_t>(spi.header[1]) << 8) | spi.header[2];
    if (kMemoryAccessOnlyWhenActive && !IsActive(chip))
    {
        spi.phase = SpiPhase::Ignore;
        return;
    }
    if (prefix == 0x00)
    {
        spi.phase = SpiPhase::ReadDummy;
        spi.dummyLeft = 1;
        if (kSpiWidthExtraDummy && (RegGet(chip, Reg::SpiWidth) & 4))
            spi.dummyLeft = 2;
    }
    else
    {
        spi.phase = SpiPhase::WriteData;
        spi.writeWrapsInCmd = spi.address >= kRamCmdBase && spi.address < kRamCmdBase + kRamCmdSize;
    }
}

uint32_t NextWriteAddress(const EveChip& chip, uint32_t address)
{
    const SpiState& spi = chip.state.spi;
    // REG_CMDB_WRITE is a FIFO port: the address does not advance (spec §2.1).
    if ((address & ~3u) == RegisterInfo(chip, Reg::CmdbWrite).address)
        return address;
    // A write that starts inside RAM_CMD wraps inside it [PG §5.1.1].
    if (spi.writeWrapsInCmd && address >= kRamCmdBase && address < kRamCmdBase + kRamCmdSize)
        return kRamCmdBase + ((address + 1) & kRamCmdMask);
    return (address + 1) & kAddressMask;
}

} // namespace

void SpiSelect(EveChip& chip, bool selected)
{
    SpiState& spi = chip.state.spi;
    if (selected == (spi.selected != 0))
        return;
    spi.selected = selected ? 1 : 0;
    if (selected)
    {
        spi.phase = SpiPhase::Header;
        spi.headerCount = 0;
        spi.bytes = 0;
        spi.writeWrapsInCmd = 0;
    }
    else
    {
        EndTransaction(chip);
    }
}

uint8_t SpiExchange(EveChip& chip, uint8_t mosi)
{
    SpiState& spi = chip.state.spi;
    if (!spi.selected)
        return kMisoUndefinedValue;
    switch (spi.phase)
    {
    case SpiPhase::Header:
        spi.header[spi.headerCount++] = mosi;
        if (spi.headerCount == 1)
        {
            const uint8_t prefix = mosi & 0xC0;
            if (prefix == 0x40)
                spi.phase = SpiPhase::HostCommand;
            else if (prefix == 0xC0)
                spi.phase = SpiPhase::Ignore; // undefined prefix (spec §2.1)
        }
        else if (spi.headerCount == 3)
        {
            StartAccess(chip);
        }
        return kMisoUndefinedValue;
    case SpiPhase::HostCommand:
        if (spi.headerCount < 3)
            spi.header[spi.headerCount++] = mosi;
        return kMisoUndefinedValue;
    case SpiPhase::ReadDummy:
        if (--spi.dummyLeft == 0)
            spi.phase = SpiPhase::ReadData;
        return kMisoUndefinedValue;
    case SpiPhase::ReadData:
    {
        const uint8_t value = BusRead(chip, spi.address);
        spi.address = (spi.address + 1) & kAddressMask;
        ++spi.bytes;
        return value;
    }
    case SpiPhase::WriteData:
        BusWrite(chip, spi.address, mosi);
        spi.address = NextWriteAddress(chip, spi.address);
        ++spi.bytes;
        return kMisoUndefinedValue;
    case SpiPhase::Idle:
    case SpiPhase::Ignore:
        break;
    }
    if (spi.phase == SpiPhase::Ignore && spi.headerCount < 3)
        spi.header[spi.headerCount++] = mosi;
    return kMisoUndefinedValue;
}

void InitClock(EveChip& chip)
{
    RecomputeClock(chip);
}

} // namespace EveLib
