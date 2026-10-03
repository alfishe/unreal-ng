// eve-emu - regions, the single write path, dirty marking (spec §1, arch §8.2).
#include "eve-internal.h"

#include <new>

namespace EveLib
{

namespace
{

struct RegionLayout
{
    const char* name;
    uint32_t size;
};

constexpr RegionLayout kLayout[RegionCount] = {
    {"RAM_G", kRamGSize},
    {"DL0", kRamDlSize},
    {"DL1", kRamDlSize},
    {"REG", kRamRegSize},
    {"CMD", kRamCmdSize},
    {"SPECIAL", kSpecialSize},
    {"INFLIGHT", kInflightCapacity},
};

uint32_t PageCount(uint32_t size)
{
    return (size + kPageSize - 1) / kPageSize;
}

uint32_t DirtyWords(uint32_t size)
{
    return (PageCount(size) + kPagesPerWord - 1) / kPagesPerWord;
}

void WriteRegisterByte(EveChip& chip, const RegInfo& info, uint32_t address, uint8_t value)
{
    constexpr uint8_t kAllBits = 0xFF;
    BusState& bus = chip.state.bus;
    if (info.flags & RegWriteOnly)
    {
        // REG_CMDB_WRITE: each complete word goes to the ring (spec §2.1).
        bus.cmdbWord[bus.cmdbFill++] = value;
        if (bus.cmdbFill == 4)
        {
            bus.cmdbFill = 0;
            CmdbAppendWord(chip, LoadLe32(bus.cmdbWord));
        }
        return;
    }
    if (info.flags & RegReadOnly)
        return;
    if (bus.pendingRegister != 0 && bus.pendingRegister != info.address + 1)
        FlushPendingRegister(chip);
    if (info.flags & RegDrawing)
        CatchUp(chip);

    Region& region = chip.regions[info.address >= kSpecialBase ? RegionSpecial : RegionReg];
    const uint32_t base = info.address >= kSpecialBase ? kSpecialBase : kRamRegBase;
    uint8_t* bytes = region.base + (info.address - base);
    if (bus.pendingRegister == 0)
    {
        bus.pendingRegister = info.address + 1;
        bus.pendingOldValue = LoadLe32(bytes) & info.mask;
    }
    const uint32_t byteIndex = address & 3;
    const uint8_t mask = kRegistersKeepAllBits ? kAllBits : static_cast<uint8_t>(info.mask >> (8 * byteIndex));
    if ((info.flags & RegDrawing) && bytes[byteIndex] != static_cast<uint8_t>(value & mask))
        ++chip.drawRegChanges; // a register drawing reads changed: no line is kept
    bytes[byteIndex] = static_cast<uint8_t>(value & mask);
    region.MarkDirty(info.address - base);
    if (byteIndex == 3)
        FlushPendingRegister(chip);
}

} // namespace

void Region::MarkDirtyRange(uint32_t offset, uint32_t length)
{
    if (length == 0)
        return;
    const uint32_t first = offset >> kPageShift;
    const uint32_t last = (offset + length - 1) >> kPageShift;
    for (uint32_t page = first; page <= last; ++page)
        MarkPage(page);
}

bool InitRegions(EveChip& chip)
{
    size_t bytes = 0;
    size_t words = 0;
    for (const RegionLayout& layout : kLayout)
    {
        bytes += layout.size;
        words += DirtyWords(layout.size);
    }
    chip.memory.reset(new (std::nothrow) uint8_t[bytes]());
    chip.dirtyBits.reset(new (std::nothrow) uint64_t[words]());
    if (!chip.memory || !chip.dirtyBits)
        return false;
    uint8_t* base = chip.memory.get();
    uint64_t* dirty = chip.dirtyBits.get();
    for (size_t i = 0; i < RegionCount; ++i)
    {
        Region& region = chip.regions[i];
        region.name = kLayout[i].name;
        region.base = base;
        region.size = kLayout[i].size;
        region.dirty = dirty;
        region.pageCount = PageCount(kLayout[i].size);
        base += kLayout[i].size;
        dirty += DirtyWords(kLayout[i].size);
    }
    return true;
}

void ClearRegions(EveChip& chip)
{
    ++chip.ramGWrites;
    for (Region& region : chip.regions)
    {
        std::memset(region.base, 0, region.size);
        region.MarkDirtyRange(0, region.size);
    }
}

uint8_t* RamG(EveChip& chip)
{
    return chip.regions[RegionRamG].base;
}

const uint8_t* RamG(const EveChip& chip)
{
    return chip.regions[RegionRamG].base;
}

Region& PendingDlRegion(EveChip& chip)
{
    return chip.regions[RegionDl0 + (chip.state.scan.activeDl ^ 1)];
}

Region& ActiveDlRegion(EveChip& chip)
{
    return chip.regions[RegionDl0 + chip.state.scan.activeDl];
}

uint8_t* PendingDl(EveChip& chip)
{
    return PendingDlRegion(chip).base;
}

const uint8_t* ActiveDl(const EveChip& chip)
{
    return chip.regions[RegionDl0 + chip.state.scan.activeDl].base;
}

uint8_t RomByte(const EveChip& chip, uint32_t address)
{
    if (chip.romImage != nullptr && address >= chip.romBase && address < kRomEnd)
        return chip.romImage[address - chip.romBase];
    if (address >= kRomFontRootAddress && address < kRomEnd)
        return static_cast<uint8_t>(kRomFontRootValue >> (8 * (address - kRomFontRootAddress)));
    return 0;
}

void BusWrite(EveChip& chip, uint32_t address, uint8_t value)
{
    address &= kAddressMask;
    if (address < kRamGSize)
    {
        if (chip.drawing)
            CatchUp(chip);
        Region& region = chip.regions[RegionRamG];
        if (region.base[address] != value)
            ++chip.ramGWrites; // counts changes: kept lines and decoded palettes hold over equal writes
        region.base[address] = value;
        region.MarkDirty(address);
        return;
    }
    if (address >= kRamDlBase && address < kRamDlBase + kRamDlSize)
    {
        Region& region = PendingDlRegion(chip);
        const uint32_t offset = address - kRamDlBase;
        region.base[offset] = value;
        region.MarkDirty(offset);
        return;
    }
    if (address >= kRamCmdBase && address < kRamCmdBase + kRamCmdSize)
    {
        Region& region = chip.regions[RegionCmd];
        const uint32_t offset = address - kRamCmdBase;
        region.base[offset] = value;
        region.MarkDirty(offset);
        return;
    }
    if ((address >= kRamRegBase && address < kRamRegBase + kRamRegSize) ||
        (address >= kSpecialBase && address < kSpecialBase + kSpecialSize))
    {
        const RegInfo* info = FindRegister(chip, address);
        if (info != nullptr)
            WriteRegisterByte(chip, *info, address, value);
        return;
    }
    // ROM and reserved addresses: writes are ignored (spec §1).
}

uint8_t BusPeek(const EveChip& chip, uint32_t address)
{
    address &= kAddressMask;
    if (address < kRamGSize)
        return chip.regions[RegionRamG].base[address];
    if (address >= kRomFontBase && address < kRomEnd)
        return RomByte(chip, address);
    if (address >= kRamDlBase && address < kRamDlBase + kRamDlSize)
        return chip.regions[RegionDl0 + (chip.state.scan.activeDl ^ 1)].base[address - kRamDlBase];
    if (address >= kRamCmdBase && address < kRamCmdBase + kRamCmdSize)
        return chip.regions[RegionCmd].base[address - kRamCmdBase];
    if ((address >= kRamRegBase && address < kRamRegBase + kRamRegSize) ||
        (address >= kSpecialBase && address < kSpecialBase + kSpecialSize))
    {
        const RegInfo* info = FindRegister(chip, address);
        if (info == nullptr)
            return kReservedReadValue;
        return static_cast<uint8_t>(RegReadValue(chip, *info) >> (8 * (address & 3)));
    }
    return kReservedReadValue;
}

uint8_t BusRead(EveChip& chip, uint32_t address)
{
    const uint8_t value = BusPeek(chip, address);
    address &= kAddressMask;
    if (address >= kRamRegBase && address < kRamRegBase + kRamRegSize && (address & 3) == 0)
    {
        const RegInfo* info = FindRegister(chip, address);
        if (info != nullptr && (info->flags & RegClearOnRead))
            RegSet(chip, info->reg, 0);
    }
    return value;
}

void FlushPendingRegister(EveChip& chip)
{
    BusState& bus = chip.state.bus;
    if (bus.pendingRegister == 0)
        return;
    const RegInfo* info = FindRegister(chip, bus.pendingRegister - 1);
    const uint32_t oldValue = bus.pendingOldValue;
    bus.pendingRegister = 0;
    bus.pendingOldValue = 0;
    if (info != nullptr)
        CommitRegister(chip, *info, oldValue);
}

} // namespace EveLib
