#pragma once
#include "emulator/memory/memory.h"

#include <memory>

/// ZX Spectrum Next memory: a flat 2 MB array behind eight 8K slots (design D6). Every slot holds a read and a
/// write offset into Memory's buffer - offsets, not pointers, so a shared-memory migration (which moves the
/// buffer) needs no rebuild. MMU value 255 is ROM (read-only: writes land in the trash page); 0..223 is an 8K
/// RAM page; anything else reads the trash page.
///
/// The slot table is what the CPU sees (the plain Fast / Debug interface of ModelMemoryInterface). The four
/// 16K windows of the base class are kept in step with the even slot of each pair - enough for the tools that
/// look at the base arrays; the table is the truth when a pair is split.
///
/// The classic paging ports rewrite the table (ApplyClassicPaging); NEXTREG #50-#57 write single slots
/// (SetMmu) and stay until the next paging port write, as on the board.
class NextMemory : public Memory
{
public:
    static constexpr unsigned kSlots = 8;
    static constexpr uint8_t kMmuRom = 0xFF;
    static constexpr uint32_t kSlotSize = 0x2000;
    static constexpr unsigned kRamPages8K = 224;  // 1.75 MB behind the MMU values (the rest of 2 MB is the ROM / config area)
    /// Reset values of the slot table: ROM, ROM, then the 48K layout (RAM 5, 2, 0 as 8K pages)
    static constexpr uint8_t kResetMmu[kSlots] = {kMmuRom, kMmuRom, 10, 11, 4, 5, 0, 1};

    NextMemory() = delete;
    explicit NextMemory(EmulatorContext* context);
    ~NextMemory() override = default;

    /// region <Memory overrides>
    uint8_t MemoryReadFast(uint16_t addr, bool isExecution) override;
    uint8_t MemoryReadDebug(uint16_t addr, bool isExecution) override;
    void SlotWriteFast(uint16_t addr, uint8_t value);
    void SlotWriteDebug(uint16_t addr, uint8_t value);
    MemoryInterface* ModelMemoryInterface(bool debug) override;
    uint8_t ToolReadRedirect(uint16_t addr, uint8_t normal) const override;
    /// endregion

    /// region <Slot table>
    /// Reset values of the table; the ROM select returns to ROM 0
    void ResetMmu();
    /// One slot to an MMU value (NEXTREG #50 + slot)
    void SetMmu(unsigned slot, uint8_t value);
    uint8_t GetMmu(unsigned slot) const { return _mmu[slot & 7]; }
    /// The 16K ROM (0..3) behind MMU value 255
    void SetRomSelect(uint8_t rom);
    uint8_t GetRomSelect() const { return _rom; }
    /// #DFFD bits 0-3: the bank bits above #7FFD's three
    void SetExtendedBank(uint8_t value);
    uint8_t GetExtendedBank() const { return _dffd; }

    /// The slot table from the paging latches (#7FFD, #1FFD, #DFFD): 128K layout, the +3 all-RAM modes
    void ApplyClassicPaging(uint8_t p7ffd, uint8_t p1ffd);
    /// Debugger / test pokes into the slot an address falls in (the base DirectWrite sees the 16K windows)
    void PokeSlot(uint16_t addr, uint8_t value);
    uint8_t PeekSlot(uint16_t addr) const;
    /// Physical 8K address (offset in the buffer) behind a CPU address, for reports
    uint32_t SlotReadOffset(unsigned slot) const { return _readOff[slot & 7]; }
    /// endregion

protected:
    bool UpdateModelBanks() override;

private:
    void MapSlot(unsigned slot);
    void SyncWindows();

    uint8_t _mmu[kSlots] = {};
    uint8_t _rom = 0;
    uint8_t _dffd = 0;
    uint32_t _readOff[kSlots] = {};
    uint32_t _writeOff[kSlots] = {};
    ttd::PhysPage _physPage[kSlots] = {};
    std::unique_ptr<MemoryInterface> _fastIf;
    std::unique_ptr<MemoryInterface> _debugIf;
};
