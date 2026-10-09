#include "stdafx.h"

#include "nextmemory.h"

#include "debugger/ttd/ttddirtytracker.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "common/filehelper.h"
#include "common/modulelogger.h"

#include <cstring>

NextMemory::NextMemory(EmulatorContext* context) : Memory(context)
{
    _fastIf = std::make_unique<MemoryInterface>(&Memory::MemoryReadFast, static_cast<MemoryWriteCallback>(&NextMemory::SlotWriteFast));
    _debugIf = std::make_unique<MemoryInterface>(&Memory::MemoryReadDebug, static_cast<MemoryWriteCallback>(&NextMemory::SlotWriteDebug));
    _toolReadRedirect = true;  // DirectReadFromZ80Memory asks the slot table
    ResetMmu();
}

MemoryInterface* NextMemory::ModelMemoryInterface(bool debug)
{
    return debug ? _debugIf.get() : _fastIf.get();
}

/// region <Access>

uint8_t NextMemory::MemoryReadFast(uint16_t addr, [[maybe_unused]] bool isExecution)
{
    return _memory[_readOff[addr >> 13] + (addr & (kSlotSize - 1))];
}

uint8_t NextMemory::MemoryReadDebug(uint16_t addr, bool isExecution)
{
    const unsigned slot = addr >> 13;
    const uint8_t result = _memory[_readOff[slot] + (addr & (kSlotSize - 1))];
    ReadDebugEffects(addr, result, isExecution, _physPage[slot]);
    return result;
}

void NextMemory::SlotWriteFast(uint16_t addr, uint8_t value)
{
    _memory[_writeOff[addr >> 13] + (addr & (kSlotSize - 1))] = value;
}

void NextMemory::SlotWriteDebug(uint16_t addr, uint8_t value)
{
    const unsigned slot = addr >> 13;
    _memory[_writeOff[slot] + (addr & (kSlotSize - 1))] = value;
    WriteDebugEffects(addr, value, _physPage[slot]);
}

uint8_t NextMemory::ToolReadRedirect(uint16_t addr, [[maybe_unused]] uint8_t normal) const
{
    return PeekSlot(addr);
}

uint8_t NextMemory::PeekSlot(uint16_t addr) const
{
    return _memory[_readOff[addr >> 13] + (addr & (kSlotSize - 1))];
}

void NextMemory::PokeSlot(uint16_t addr, uint8_t value)
{
    const unsigned slot = addr >> 13;
    // A tool's poke is not the CPU's write: it reaches ROM too (as DirectWrite does)
    _memory[_readOff[slot] + (addr & (kSlotSize - 1))] = value;
    if (_feature_ttd_enabled && _ttdDirtyTracker != nullptr && _physPage[slot] != ttd::kPhysPageNone)
        _ttdDirtyTracker->MarkDirty(_physPage[slot]);
    if (addr >= 0x4000 && addr <= 0x5B00)
        _state->video_memory_changed = true;
}

/// endregion

/// region <Config mode and the boot ROM>

void NextMemory::SetConfigMode(bool on)
{
    _configMode = on;
    Remap();
}

void NextMemory::SetBootRomEnabled(bool on)
{
    _bootRom = on;
    Remap();
}

void NextMemory::SetConfigBank(uint8_t bank)
{
    _cfgBank = bank & 0x3F;
    Remap();
}

void NextMemory::Remap()
{
    for (unsigned s = 0; s < kSlots; s++)
        MapSlot(s);
    SyncWindows();
}

void NextMemory::OnRomLoaded([[maybe_unused]] uint16_t imageBanks)
{
    const std::string path = _context->config.next_boot_rom_path;
    if (path.empty())
        return;
    std::string resolved = FileHelper::NormalizePath(path);
    if (!FileHelper::FileExists(resolved))
        resolved = FileHelper::PathCombine(FileHelper::GetExecutablePath(), resolved);
    if (!FileHelper::FileExists(resolved))
        resolved = FileHelper::PathCombine(FileHelper::GetResourcesPath(), path);
    FILE* file = FileHelper::OpenFile(resolved, "rb");
    if (!file)
    {
        MLOGERROR("NextMemory: boot ROM '%s' not found", path.c_str());
        return;
    }
    uint8_t* page = ROMPageHostAddress(kBootRomPage);
    std::memset(page, 0xFF, PAGE_SIZE);
    const size_t size = std::fread(page, 1, kBootRomSize, file);
    std::fclose(file);
    if (size != kBootRomSize)
    {
        MLOGERROR("NextMemory: boot ROM '%s' is %zu bytes, expected %u", path.c_str(), size, kBootRomSize);
        return;
    }
    // Power-on state of the real board: an empty system area (the firmware loads it), config mode, boot ROM on
    for (unsigned p = 0; p < kSystemAreaPages; p++)
        std::memset(ROMPageHostAddress(static_cast<uint8_t>(p)), 0xFF, PAGE_SIZE);
    _bootRomLoaded = true;
    _configMode = true;
    _bootRom = true;
    Remap();
}

/// endregion

/// region <Slot table>

void NextMemory::MapSlot(unsigned slot)
{
    const uint8_t value = _mmu[slot];
    const uint32_t trash = static_cast<uint32_t>(TRASH_MEMORY_OFFSET);
    if (value == kMmuRom && slot == 0 && _configMode && _bootRom && _bootRomLoaded)
    {
        // the boot ROM: read-only
        _readOff[slot] = static_cast<uint32_t>(ROMPageHostAddress(kBootRomPage) - _memory);
        _writeOff[slot] = trash;
        _physPage[slot] = ttd::kPhysPageNone;
    }
    else if (value == kMmuRom && slot < 2 && _configMode)
    {
        // config mapping: NR #04's 16K SRAM bank, writable (the firmware loads the ROMs through it)
        const uint8_t* mem = _cfgBank < kSystemAreaPages ? ROMPageHostAddress(_cfgBank) : RAMPageAddress(_cfgBank - kSystemAreaPages);
        _readOff[slot] = _writeOff[slot] = static_cast<uint32_t>(mem + slot * kSlotSize - _memory);
        _physPage[slot] = _cfgBank < kSystemAreaPages ? ttd::kPhysPageNone : static_cast<ttd::PhysPage>(_cfgBank - kSystemAreaPages);
    }
    else if (value == kMmuRom)
    {
        const uint8_t* rom = ROMPageHostAddress(_rom) + (slot & 1) * kSlotSize;
        _readOff[slot] = static_cast<uint32_t>(rom - _memory);
        _writeOff[slot] = trash;
        _physPage[slot] = ttd::kPhysPageNone;
    }
    else if (value < kRamPages8K && (value >> 1) < MAX_RAM_PAGES)
    {
        const uint8_t* ram = RAMPageAddress(value >> 1) + (value & 1) * kSlotSize;
        _readOff[slot] = _writeOff[slot] = static_cast<uint32_t>(ram - _memory);
        _physPage[slot] = static_cast<ttd::PhysPage>(value >> 1);
    }
    else
    {
        _readOff[slot] = _writeOff[slot] = trash;
        _physPage[slot] = ttd::kPhysPageNone;
    }
}

void NextMemory::SyncWindows()
{
    for (unsigned w = 0; w < 4; w++)
    {
        const unsigned lo = w * 2;
        _bank_read[w] = _memory + _readOff[lo];
        _bank_write[w] = _memory + _writeOff[lo];
        _bank_mode[w] = _mmu[lo] == kMmuRom ? BANK_ROM : BANK_RAM;
        _bank_ram_page_cache[w] = _physPage[lo];
    }
}

void NextMemory::ResetMmu()
{
    _rom = 0;
    _dffd = 0;
    for (unsigned s = 0; s < kSlots; s++)
    {
        _mmu[s] = kResetMmu[s];
        MapSlot(s);
    }
    SyncWindows();
}

void NextMemory::SetMmu(unsigned slot, uint8_t value)
{
    slot &= 7;
    _mmu[slot] = value;
    MapSlot(slot);
    SyncWindows();
}

void NextMemory::SetRomSelect(uint8_t rom)
{
    _rom = rom & 3;
    for (unsigned s = 0; s < kSlots; s++)
        if (_mmu[s] == kMmuRom)
            MapSlot(s);
    SyncWindows();
}

void NextMemory::SetExtendedBank(uint8_t value)
{
    _dffd = value & 0x0F;
}

void NextMemory::ApplyClassicPaging(uint8_t p7ffd, uint8_t p1ffd)
{
    // 16K banks per window; the 8K pages of bank b are 2b and 2b+1
    uint8_t bank[4] = {kMmuRom, 5, 2, static_cast<uint8_t>(((_dffd & 0x0F) << 3) | (p7ffd & 7))};
    bool rom0 = true;
    if (p1ffd & 1)  // +3 special paging: all RAM
    {
        static const uint8_t cfg[4][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {4, 5, 6, 3}, {4, 7, 6, 3}};
        const uint8_t* c = cfg[(p1ffd >> 1) & 3];
        for (unsigned w = 0; w < 4; w++)
            bank[w] = c[w];
        rom0 = false;
    }
    _rom = static_cast<uint8_t>((((p1ffd >> 2) & 1) << 1) | ((p7ffd >> 4) & 1));
    for (unsigned w = 0; w < 4; w++)
    {
        if (w == 0 && rom0)
            _mmu[0] = _mmu[1] = kMmuRom;
        else
        {
            _mmu[w * 2] = static_cast<uint8_t>(bank[w] * 2);
            _mmu[w * 2 + 1] = static_cast<uint8_t>(bank[w] * 2 + 1);
        }
    }
    for (unsigned s = 0; s < kSlots; s++)
        MapSlot(s);
    SyncWindows();
}

bool NextMemory::UpdateModelBanks()
{
    // Called by UpdateZ80Banks (snapshot load, TTD restore): the table follows the latches
    EmulatorState& state = _context->emulatorState;
    state.flags &= ~(CF_DOSPORTS | CF_Z80FBUS | CF_LEAVEDOSRAM | CF_LEAVEDOSADR | CF_SETDOSROM);
    ApplyClassicPaging(state.p7FFD, state.p1FFD);
    return true;
}

/// endregion
