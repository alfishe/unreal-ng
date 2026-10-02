// eve-emu - register table and side effects (spec §3).
#include "eve-internal.h"

namespace EveLib
{

namespace
{

constexpr uint8_t kRo = RegReadOnly;
constexpr uint8_t kDr = RegDrawing;

// FT812 register table: [DS Table 5-2], reset values per [PG §3] unless noted in the
// spec or measured on the BT8XX reference (golden case reset-registers: REG_SNAPFORMAT,
// REG_GPIO_DIR, REG_GPIOX_DIR, REG_GPIOX, REG_TOUCH_CONFIG, REG_ADAPTIVE_FRAMERATE).
// Masks are the register widths of the DS table. Order = enum Reg.
constexpr RegInfo kFt812Registers[kRegCount] = {
    {Reg::Id,               0x302000, 0x7C,       0xFF,       kRo | RegComputed, RegEffect::None, "REG_ID"},
    {Reg::Frames,           0x302004, 0,          0xFFFFFFFF, kRo, RegEffect::None, "REG_FRAMES"},
    {Reg::Clock,            0x302008, 0,          0xFFFFFFFF, kRo | RegComputed, RegEffect::None, "REG_CLOCK"},
    {Reg::Frequency,        0x30200C, 60000000,   kFrequencyMask, 0, RegEffect::None, "REG_FREQUENCY"},
    {Reg::RenderMode,       0x302010, 0,          0x1,        0, RegEffect::None, "REG_RENDERMODE"},
    {Reg::SnapY,            0x302014, 0,          0x7FF,      0, RegEffect::None, "REG_SNAPY"},
    {Reg::Snapshot,         0x302018, 0,          0x1,        0, RegEffect::None, "REG_SNAPSHOT"},
    {Reg::SnapFormat,       0x30201C, 0,          0x3F,       0, RegEffect::None, "REG_SNAPFORMAT"},
    {Reg::CpuReset,         0x302020, kCpuResetResetValue, 0x7, 0, RegEffect::CpuReset, "REG_CPURESET"},
    {Reg::TapCrc,           0x302024, 0,          0xFFFFFFFF, kRo, RegEffect::None, "REG_TAP_CRC"},
    {Reg::TapMask,          0x302028, 0xFFFFFFFF, 0xFFFFFFFF, 0, RegEffect::None, "REG_TAP_MASK"},
    {Reg::Hcycle,           0x30202C, 548,        0xFFF,      kDr, RegEffect::Timing, "REG_HCYCLE"},
    {Reg::Hoffset,          0x302030, 43,         0xFFF,      kDr, RegEffect::Timing, "REG_HOFFSET"},
    {Reg::Hsize,            0x302034, 480,        0xFFF,      kDr, RegEffect::Timing, "REG_HSIZE"},
    {Reg::Hsync0,           0x302038, 0,          0xFFF,      0, RegEffect::None, "REG_HSYNC0"},
    {Reg::Hsync1,           0x30203C, 41,         0xFFF,      0, RegEffect::None, "REG_HSYNC1"},
    {Reg::Vcycle,           0x302040, 292,        0xFFF,      kDr, RegEffect::Timing, "REG_VCYCLE"},
    {Reg::Voffset,          0x302044, 12,         0xFFF,      kDr, RegEffect::Timing, "REG_VOFFSET"},
    {Reg::Vsize,            0x302048, 272,        0xFFF,      kDr, RegEffect::Timing, "REG_VSIZE"},
    {Reg::Vsync0,           0x30204C, 0,          0x3FF,      0, RegEffect::None, "REG_VSYNC0"},
    {Reg::Vsync1,           0x302050, 10,         0x3FF,      0, RegEffect::None, "REG_VSYNC1"},
    {Reg::Dlswap,           0x302054, 0,          0x3,        0, RegEffect::Dlswap, "REG_DLSWAP"},
    {Reg::Rotate,           0x302058, 0,          0x7,        kDr, RegEffect::None, "REG_ROTATE"},
    {Reg::Outbits,          0x30205C, 0,          0x1FF,      kDr, RegEffect::None, "REG_OUTBITS"},
    {Reg::Dither,           0x302060, 1,          0x1,        kDr, RegEffect::None, "REG_DITHER"},
    {Reg::Swizzle,          0x302064, 0,          0xF,        kDr, RegEffect::None, "REG_SWIZZLE"},
    {Reg::Cspread,          0x302068, 1,          0x1,        kDr, RegEffect::None, "REG_CSPREAD"},
    {Reg::PclkPol,          0x30206C, 0,          0x1,        0, RegEffect::None, "REG_PCLK_POL"},
    {Reg::Pclk,             0x302070, 0,          0xFF,       kDr, RegEffect::Timing, "REG_PCLK"},
    {Reg::TagX,             0x302074, 0,          0x7FF,      0, RegEffect::None, "REG_TAG_X"},
    {Reg::TagY,             0x302078, 0,          0x7FF,      0, RegEffect::None, "REG_TAG_Y"},
    {Reg::Tag,              0x30207C, 0,          0xFF,       kRo, RegEffect::None, "REG_TAG"},
    {Reg::VolPb,            0x302080, 0xFF,       0xFF,       0, RegEffect::None, "REG_VOL_PB"},
    {Reg::VolSound,         0x302084, 0xFF,       0xFF,       0, RegEffect::None, "REG_VOL_SOUND"},
    {Reg::Sound,            0x302088, 0,          0xFFFF,     0, RegEffect::None, "REG_SOUND"},
    {Reg::Play,             0x30208C, 0,          0x1,        0, RegEffect::Play, "REG_PLAY"},
    {Reg::GpioDir,          0x302090, 0,          0xFF,       0, RegEffect::None, "REG_GPIO_DIR"},
    {Reg::Gpio,             0x302094, 0,          0xFF,       0, RegEffect::None, "REG_GPIO"},
    {Reg::GpioxDir,         0x302098, 0,          0xFFFF,     0, RegEffect::None, "REG_GPIOX_DIR"},
    {Reg::Gpiox,            0x30209C, kGpioxResetValue, 0xFFFF, 0, RegEffect::None, "REG_GPIOX"},
    {Reg::IntFlags,         0x3020A8, 0,          0xFF,       kRo | RegClearOnRead, RegEffect::None, "REG_INT_FLAGS"},
    {Reg::IntEn,            0x3020AC, 0,          0x1,        0, RegEffect::None, "REG_INT_EN"},
    {Reg::IntMask,          0x3020B0, 0xFF,       0xFF,       0, RegEffect::None, "REG_INT_MASK"},
    {Reg::PlaybackStart,    0x3020B4, 0,          0xFFFFF,    0, RegEffect::None, "REG_PLAYBACK_START"},
    {Reg::PlaybackLength,   0x3020B8, 0,          0xFFFFF,    0, RegEffect::None, "REG_PLAYBACK_LENGTH"},
    {Reg::PlaybackReadptr,  0x3020BC, 0,          0xFFFFF,    kRo | RegComputed, RegEffect::None, "REG_PLAYBACK_READPTR"},
    {Reg::PlaybackFreq,     0x3020C0, 8000,       0xFFFF,     0, RegEffect::None, "REG_PLAYBACK_FREQ"},
    {Reg::PlaybackFormat,   0x3020C4, 0,          0x3,        0, RegEffect::None, "REG_PLAYBACK_FORMAT"},
    {Reg::PlaybackLoop,     0x3020C8, 0,          0x1,        0, RegEffect::None, "REG_PLAYBACK_LOOP"},
    {Reg::PlaybackPlay,     0x3020CC, 0,          0x1,        0, RegEffect::PlaybackPlay, "REG_PLAYBACK_PLAY"},
    {Reg::PwmHz,            0x3020D0, 250,        0x3FFF,     0, RegEffect::None, "REG_PWM_HZ"},
    {Reg::PwmDuty,          0x3020D4, 128,        0xFF,       0, RegEffect::None, "REG_PWM_DUTY"},
    {Reg::Macro0,           0x3020D8, 0,          0xFFFFFFFF, kDr, RegEffect::None, "REG_MACRO_0"},
    {Reg::Macro1,           0x3020DC, 0,          0xFFFFFFFF, kDr, RegEffect::None, "REG_MACRO_1"},
    {Reg::CmdRead,          0x3020F8, 0,          0xFFF,      0, RegEffect::CmdRead, "REG_CMD_READ"},
    {Reg::CmdWrite,         0x3020FC, 0,          0xFFF,      0, RegEffect::CmdWrite, "REG_CMD_WRITE"},
    {Reg::CmdDl,            0x302100, 0,          0x1FFF,     0, RegEffect::CmdDl, "REG_CMD_DL"},
    {Reg::TouchMode,        0x302104, 3,          0x3,        0, RegEffect::None, "REG_TOUCH_MODE"},
    {Reg::TouchAdcMode,     0x302108, 1,          0x1,        0, RegEffect::None, "REG_TOUCH_ADC_MODE"},
    {Reg::TouchCharge,      0x30210C, 0x1770,     0xFFFF,     0, RegEffect::None, "REG_TOUCH_CHARGE"},
    {Reg::TouchSettle,      0x302110, 3,          0xF,        0, RegEffect::None, "REG_TOUCH_SETTLE"},
    {Reg::TouchOversample,  0x302114, 7,          0xF,        0, RegEffect::None, "REG_TOUCH_OVERSAMPLE"},
    {Reg::TouchRzthresh,    0x302118, 0xFFFF,     0xFFFF,     0, RegEffect::None, "REG_TOUCH_RZTHRESH"},
    {Reg::TouchRawXy,       0x30211C, 0xFFFFFFFF, 0xFFFFFFFF, kRo, RegEffect::None, "REG_TOUCH_RAW_XY"},
    {Reg::TouchRz,          0x302120, 0x7FFF,     0xFFFF,     kRo, RegEffect::None, "REG_TOUCH_RZ"},
    {Reg::TouchScreenXy,    0x302124, 0x80008000, 0xFFFFFFFF, kRo, RegEffect::None, "REG_TOUCH_SCREEN_XY"},
    {Reg::TouchTagXy,       0x302128, 0,          0xFFFFFFFF, kRo, RegEffect::None, "REG_TOUCH_TAG_XY"},
    {Reg::TouchTag,         0x30212C, 0,          0xFF,       kRo, RegEffect::None, "REG_TOUCH_TAG"},
    {Reg::TouchTag1Xy,      0x302130, 0,          0xFFFFFFFF, kRo, RegEffect::None, "REG_TOUCH_TAG1_XY"},
    {Reg::TouchTag1,        0x302134, 0,          0xFF,       kRo, RegEffect::None, "REG_TOUCH_TAG1"},
    {Reg::TouchTag2Xy,      0x302138, 0,          0xFFFFFFFF, kRo, RegEffect::None, "REG_TOUCH_TAG2_XY"},
    {Reg::TouchTag2,        0x30213C, 0,          0xFF,       kRo, RegEffect::None, "REG_TOUCH_TAG2"},
    {Reg::TouchTag3Xy,      0x302140, 0,          0xFFFFFFFF, kRo, RegEffect::None, "REG_TOUCH_TAG3_XY"},
    {Reg::TouchTag3,        0x302144, 0,          0xFF,       kRo, RegEffect::None, "REG_TOUCH_TAG3"},
    {Reg::TouchTag4Xy,      0x302148, 0,          0xFFFFFFFF, kRo, RegEffect::None, "REG_TOUCH_TAG4_XY"},
    {Reg::TouchTag4,        0x30214C, 0,          0xFF,       kRo, RegEffect::None, "REG_TOUCH_TAG4"},
    {Reg::TouchTransformA,  0x302150, 0x10000,    0xFFFFFFFF, 0, RegEffect::None, "REG_TOUCH_TRANSFORM_A"},
    {Reg::TouchTransformB,  0x302154, 0,          0xFFFFFFFF, 0, RegEffect::None, "REG_TOUCH_TRANSFORM_B"},
    {Reg::TouchTransformC,  0x302158, 0,          0xFFFFFFFF, 0, RegEffect::None, "REG_TOUCH_TRANSFORM_C"},
    {Reg::TouchTransformD,  0x30215C, 0,          0xFFFFFFFF, 0, RegEffect::None, "REG_TOUCH_TRANSFORM_D"},
    {Reg::TouchTransformE,  0x302160, 0x10000,    0xFFFFFFFF, 0, RegEffect::None, "REG_TOUCH_TRANSFORM_E"},
    {Reg::TouchTransformF,  0x302164, 0,          0xFFFFFFFF, 0, RegEffect::None, "REG_TOUCH_TRANSFORM_F"},
    {Reg::TouchConfig,      0x302168, 0x8000,     0xFFFF,     0, RegEffect::None, "REG_TOUCH_CONFIG"},
    {Reg::CtouchTouch4X,    0x30216C, 0,          0xFFFF,     kRo, RegEffect::None, "REG_CTOUCH_TOUCH4_X"},
    {Reg::BistEn,           0x302174, 0,          0x1,        0, RegEffect::None, "REG_BIST_EN"},
    {Reg::Trim,             0x302180, 0,          0xFF,       0, RegEffect::None, "REG_TRIM"},
    {Reg::AnaComp,          0x302184, 0,          0xFF,       0, RegEffect::None, "REG_ANA_COMP"},
    {Reg::SpiWidth,         0x302188, 0,          0x7,        0, RegEffect::None, "REG_SPI_WIDTH"},
    {Reg::TouchDirectXy,    0x30218C, 0,          0xFFFFFFFF, kRo, RegEffect::None, "REG_TOUCH_DIRECT_XY"},
    {Reg::TouchDirectZ1Z2,  0x302190, 0,          0xFFFFFFFF, kRo, RegEffect::None, "REG_TOUCH_DIRECT_Z1Z2"},
    {Reg::Datestamp0,       0x302564, kDatestampByte * 0x01010101u, 0xFFFFFFFF, kRo, RegEffect::None, "REG_DATESTAMP"},
    {Reg::Datestamp1,       0x302568, kDatestampByte * 0x01010101u, 0xFFFFFFFF, kRo, RegEffect::None, "REG_DATESTAMP+4"},
    {Reg::Datestamp2,       0x30256C, kDatestampByte * 0x01010101u, 0xFFFFFFFF, kRo, RegEffect::None, "REG_DATESTAMP+8"},
    {Reg::Datestamp3,       0x302570, kDatestampByte * 0x01010101u, 0xFFFFFFFF, kRo, RegEffect::None, "REG_DATESTAMP+12"},
    {Reg::CmdbSpace,        0x302574, 0xFFC,      0xFFF,      kRo | RegComputed, RegEffect::None, "REG_CMDB_SPACE"},
    {Reg::CmdbWrite,        0x302578, 0,          0xFFFFFFFF, RegWriteOnly, RegEffect::None, "REG_CMDB_WRITE"},
    {Reg::AdaptiveFramerate,0x30257C, kAdaptiveFramerateResetValue, 0x1, 0, RegEffect::None, "REG_ADAPTIVE_FRAMERATE"},
    {Reg::Tracker,          0x309000, 0,          0xFFFFFFFF, kRo, RegEffect::None, "REG_TRACKER"},
    {Reg::Tracker1,         0x309004, 0,          0xFFFFFFFF, kRo, RegEffect::None, "REG_TRACKER_1"},
    {Reg::Tracker2,         0x309008, 0,          0xFFFFFFFF, kRo, RegEffect::None, "REG_TRACKER_2"},
    {Reg::Tracker3,         0x30900C, 0,          0xFFFFFFFF, kRo, RegEffect::None, "REG_TRACKER_3"},
    {Reg::Tracker4,         0x309010, 0,          0xFFFFFFFF, kRo, RegEffect::None, "REG_TRACKER_4"},
    {Reg::MediafifoRead,    0x309014, 0,          0xFFFFFFFF, 0, RegEffect::None, "REG_MEDIAFIFO_READ"},
    {Reg::MediafifoWrite,   0x309018, 0,          0xFFFFFFFF, 0, RegEffect::MediafifoWrite, "REG_MEDIAFIFO_WRITE"},
    // Not in the datasheet; reset values of BT8XX (golden case reset-registers), read-only
    // here: their function and writability are TO VERIFY (spec V10).
    {Reg::Undocumented0A4,  0x3020A4, 0x1,        0xFFFFFFFF, kRo, RegEffect::None, "REG_UNDOCUMENTED_0A4"},
    {Reg::Undocumented0F0,  0x3020F0, 0x3,        0xFFFFFFFF, kRo, RegEffect::None, "REG_UNDOCUMENTED_0F0"},
    {Reg::Undocumented194,  0x302194, 0x1,        0xFFFFFFFF, kRo, RegEffect::None, "REG_UNDOCUMENTED_194"},
    {Reg::Undocumented558,  0x302558, 0x100,      0xFFFFFFFF, kRo, RegEffect::None, "REG_UNDOCUMENTED_558"},
    {Reg::Undocumented560,  0x302560, 0x800,      0xFFFFFFFF, kRo, RegEffect::None, "REG_UNDOCUMENTED_560"},
};

constexpr bool TableInOrder()
{
    for (size_t i = 0; i < kRegCount; ++i)
        if (static_cast<size_t>(kFt812Registers[i].reg) != i)
            return false;
    return true;
}

const ChipTable kFt812Table = {
    EVE_MODEL_FT812, "FT812", kRamGSize, kRamDlBase, kRamRegBase, kRamCmdBase, kSpecialBase,
    {kChipId[0], kChipId[1], kChipId[2], kChipId[3]}, kFt812Registers};

// Word index (address / 4) inside REG / SPECIAL -> register + 1, 0 = reserved.
struct RegisterLookup
{
    uint8_t reg[kRamRegSize / 4];
    uint8_t special[kSpecialSize / 4];
};

constexpr RegisterLookup BuildLookup(const RegInfo* table)
{
    RegisterLookup lookup{};
    for (size_t i = 0; i < kRegCount; ++i)
    {
        const uint32_t address = table[i].address;
        if (address >= kRamRegBase && address < kRamRegBase + kRamRegSize)
            lookup.reg[(address - kRamRegBase) / 4] = static_cast<uint8_t>(i + 1);
        else if (address >= kSpecialBase && address < kSpecialBase + kSpecialSize)
            lookup.special[(address - kSpecialBase) / 4] = static_cast<uint8_t>(i + 1);
    }
    return lookup;
}

constexpr RegisterLookup kFt812Lookup = BuildLookup(kFt812Registers);

uint8_t* RegisterBytes(EveChip& chip, const RegInfo& info, Region*& region)
{
    if (info.address >= kSpecialBase)
    {
        region = &chip.regions[RegionSpecial];
        return region->base + (info.address - kSpecialBase);
    }
    region = &chip.regions[RegionReg];
    return region->base + (info.address - kRamRegBase);
}

const uint8_t* RegisterBytes(const EveChip& chip, const RegInfo& info)
{
    if (info.address >= kSpecialBase)
        return chip.regions[RegionSpecial].base + (info.address - kSpecialBase);
    return chip.regions[RegionReg].base + (info.address - kRamRegBase);
}

} // namespace

static_assert(TableInOrder(), "register table must follow enum Reg");

const ChipTable& GetChipTable(EveModel model)
{
    (void)model; // the only model (arch §1, §12)
    return kFt812Table;
}

const RegInfo& RegisterInfo(const EveChip& chip, Reg reg)
{
    return chip.table->registers[static_cast<size_t>(reg)];
}

const RegInfo* FindRegister(const EveChip& chip, uint32_t address)
{
    const RegisterLookup& lookup = kFt812Lookup;
    uint8_t index = 0;
    if (address >= kRamRegBase && address < kRamRegBase + kRamRegSize)
        index = lookup.reg[(address - kRamRegBase) / 4];
    else if (address >= kSpecialBase && address < kSpecialBase + kSpecialSize)
        index = lookup.special[(address - kSpecialBase) / 4];
    if (index == 0)
        return nullptr;
    return &chip.table->registers[index - 1];
}

uint32_t RegGet(const EveChip& chip, Reg reg)
{
    // The chip works with the register's own bits; the storage may hold more.
    const RegInfo& info = RegisterInfo(chip, reg);
    return LoadLe32(RegisterBytes(chip, info)) & info.mask;
}

void RegSet(EveChip& chip, Reg reg, uint32_t value)
{
    const RegInfo& info = RegisterInfo(chip, reg);
    Region* region = nullptr;
    uint8_t* bytes = RegisterBytes(chip, info, region);
    const uint32_t masked = value & info.mask;
    if (LoadLe32(bytes) == masked)
        return;
    StoreLe32(bytes, masked);
    region->MarkDirty(static_cast<uint32_t>(bytes - region->base));
}

void ResetRegisters(EveChip& chip)
{
    for (size_t i = 0; i < kRegCount; ++i)
    {
        const RegInfo& info = chip.table->registers[i];
        Region* region = nullptr;
        uint8_t* bytes = RegisterBytes(chip, info, region);
        StoreLe32(bytes, info.resetValue & info.mask);
        region->MarkDirty(static_cast<uint32_t>(bytes - region->base));
    }
}

uint32_t RegReadValue(const EveChip& chip, const RegInfo& info)
{
    if (info.flags & RegWriteOnly)
        return 0;
    switch (info.reg)
    {
    case Reg::Id:
        return chip.state.scan.clocksSinceReset >= chip.state.scan.idReadyAt ? info.resetValue : 0;
    case Reg::Clock:
        return static_cast<uint32_t>(chip.state.scan.clocksSinceReset);
    case Reg::CmdbSpace:
        return CmdbSpace(chip);
    case Reg::PlaybackReadptr:
        return AudioReadPointer(chip);
    default:
        return LoadLe32(RegisterBytes(chip, info));
    }
}

void CommitRegister(EveChip& chip, const RegInfo& info, uint32_t oldValue)
{
    constexpr uint32_t kDlswapBoth = 3; // DLSWAP_LINE | DLSWAP_FRAME
    const uint32_t value = LoadLe32(RegisterBytes(chip, info)) & info.mask;
    switch (info.effect)
    {
    case RegEffect::None:
        break;
    case RegEffect::Timing:
        TimingChanged(chip);
        break;
    case RegEffect::Dlswap:
        // 3 stays in the register without a swap (BT8XX, golden case dlswap-3); 0 leaves a
        // pending swap as it is.
        if (kDlswapIgnoreZeroAndThree && value == kDlswapBoth)
            break;
        if (kDlswapIgnoreZeroAndThree && value == 0)
        {
            RegSet(chip, Reg::Dlswap, oldValue);
            break;
        }
        RequestSwap(chip, value);
        break;
    case RegEffect::CpuReset:
        if ((value ^ oldValue) & 1)
            CoproHold(chip, (value & 1) != 0);
        if ((value & 4) && !(oldValue & 4))
            AudioReset(chip);
        break;
    case RegEffect::CmdWrite:
    case RegEffect::MediafifoWrite:
        CoproKick(chip);
        break;
    case RegEffect::CmdRead:
        break;
    case RegEffect::CmdDl:
        chip.state.copro.displayListFull = 0;
        break;
    case RegEffect::Play:
        if (value & 1)
            AudioStartEffect(chip);
        break;
    case RegEffect::PlaybackPlay:
        AudioStartPlayback(chip);
        break;
    }
}

} // namespace EveLib
