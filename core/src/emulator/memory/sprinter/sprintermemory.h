#pragma once

#include "emulator/memory/hostbusoverlay.h"
#include "emulator/memory/memory.h"

class PortDecoder_Sprinter;
class SprinterVideoRam;
struct SprinterPldState;

/// Sprinter Sp2000 memory (Sprinter tdd-ports-memory §5).
///
/// Owns the latch-to-bank translation (UpdateModelBanks): the PLD state lives in
/// PortDecoder_Sprinter, which attaches itself here (AttachDecoder). The active
/// PLD configuration module maps the windows; the Standard module calls
/// StandardUpdateBanks, MAME's update_memory (sprinter.cpp:320-382):
///   window 0: system ROM (ROM_RG, SYS_PG), fast RAM (IN #FB) or a RAM page from
///             the cells #E0-#EF ("vROM", read-only unless #1FFD bit 0 and the
///             system RAM mode say "RAM at 0");
///   windows 1, 2: cells #E9, #EA;
///   window 3: the cell of the current Spectrum page (#D0-#FF by #7FFD / #1FFD),
///             page #40 while the PLD is starting, the ISA view for #D0-#D6.
/// While the PLD loads, the four windows show ROM pages #C-#F and every write is
/// a configuration bit (the decoder's sink); the addresses the Z84C15 chip
/// selects do not give to the ROM (CS0) read and write fast RAM.
///
/// Special banks (the write intercept and the read redirect):
///   - graphics pages #50-#5F: CPU address -> video address PORT_Y x 1024 +
///     A[9:0]; page bit 3 = transparent #FF, bit 2 = video RAM only; reads come
///     from main RAM at the video address (MAME :1175-1205);
///   - the Spectrum screen shadow (ALL_MODE bit 0 = 0): writes to #4000-#5FFF
///     (and window 3 holding Spectrum page 5 / 7) also go to video RAM;
///   - page #A0 in window 3 with #1FFD = #10: a write resets the CPU;
///   - the ISA view: reads #FF, writes ignored (no cards in v1).
class SprinterMemory : public Memory
{
public:
    /// What a CPU write to a window does beyond the plain store
    enum class BankAction : uint8_t
    {
        Plain = 0,   ///< nothing (the Spectrum shadow is checked per write)
        Graphics,    ///< graphics page: the store goes to the video address (plain store trashed)
        ResetPage,   ///< page #A0 with #1FFD = #10: the write resets the CPU
        Isa,         ///< ISA view: ignored
        CblPage,     ///< page #FD: the plain store, and the Covox-Blaster sees it (accelerator copies, INT on)
    };

    /// How a CPU read from a window differs from the mapped page
    enum class ReadRedirect : uint8_t
    {
        None = 0,
        Graphics,    ///< main RAM at the video address
        Isa,         ///< #FF (no ISA card)
        LoadingCs,   ///< configuration loading: addresses outside the Z84C15 CS0 read fast RAM
    };

    /// region <Constructors / Destructors>
public:
    SprinterMemory() = delete;
    explicit SprinterMemory(EmulatorContext* context);
    ~SprinterMemory() override = default;
    /// endregion </Constructors / Destructors>

    /// region <Model overrides>
public:
    uint8_t MemoryReadFast(uint16_t addr, bool isExecution) override;
    uint8_t MemoryReadDebug(uint16_t addr, bool isExecution) override;

    /// The decoder that owns the PLD state (null before it exists: the windows
    /// then show the loader layout)
    void AttachDecoder(PortDecoder_Sprinter* decoder);
    /// endregion </Model overrides>

    /// region <Bank mapping (used by the configuration modules)>
public:
    /// MAME's update_memory over the PLD state (the Standard module's rule)
    void StandardUpdateBanks(const SprinterPldState& pld);

    /// Window 0 helpers for a module's own rule
    void MapRomToBank(uint8_t bank, uint8_t romPage);
    void MapRamToBank(uint8_t bank, uint8_t ramPage, bool writable);
    void MapFastRamToBank(uint8_t bank, uint8_t fastRamPage);

    BankAction GetBankAction(uint8_t bank) const { return _action[bank & 3]; }
    ReadRedirect GetReadRedirect(uint8_t bank) const { return _redirect[bank & 3]; }

    /// The 64 KB fast RAM (the four cache pages)
    uint8_t* FastRam() { return CacheBase(); }

    /// The graphics area of main RAM: pages #50-#5F seen as one 256 KB block
    uint8_t* GraphicsArea() { return RAMPageAddress(kGraphicsFirstPage); }

    static constexpr uint8_t kPortTablePage = 0x40;
    static constexpr uint8_t kGraphicsFirstPage = 0x50;
    static constexpr uint8_t kResetPage = 0xA0;
    /// The Covox-Blaster buffer page: accelerator copies into it also feed the ring (INC SP2000.inc:138)
    static constexpr uint8_t kCblPage = 0xFD;
    static constexpr uint8_t kLoaderRomPage = 0x0C;

    /// The Spectrum screen shadow address in video RAM for a CPU write at `addr`
    /// (MAME :1212-1216; tdd-ports-memory §5.3):
    ///   (A7..A0) << 10 | (PORT_Y bits 4-1) << 6 | (PORT_Y.0 ^ zxA15 ^ A13) << 5 | A12..A8
    /// Worked example: #4000 with PORT_Y = 0: row 0, column 0; #57FF: row #FF, column #17
    static uint32_t ZxShadowAddress(uint16_t addr, uint8_t portY, uint8_t pg3);
    /// endregion </Bank mapping>

    /// region <Accelerator accesses (SprinterAccelerator, tdd-accel-sound-input §1.3)>
public:
    /// Whether an accelerator access at `addr` reaches memory: main RAM windows only (not ROM, fast RAM, ISA)
    bool AcceleratorReaches(uint16_t addr) const
    {
        const uint8_t bank = static_cast<uint8_t>(addr >> 14);
        return _bank_mode[bank] == BANK_RAM && _action[bank] != BankAction::Isa;
    }
    /// A read the accelerator repeats: the window's byte with the graphics redirect, no CPU wait
    uint8_t AcceleratorRead(uint16_t addr) { return MemoryReadFast(addr, false); }
    /// A store the accelerator repeats: the plain store, TTD dirty page, then the write intercept
    /// (graphics pages, VRAM shadow, reset page) - what a CPU write does, without its wait
    void AcceleratorWrite(uint16_t addr, uint8_t value);
    /// endregion </Accelerator accesses>

protected:
    bool UpdateModelBanks() override;
    /// Tool reads (debugger, WebAPI, Lua/Python) see the read redirect as the CPU does: graphics
    /// pages from the video address, the ISA view #FF, the loader's fast RAM above CS0
    uint8_t ToolReadRedirect(uint16_t addr, uint8_t normal) const override { return Redirect(addr, normal); }

private:
    /// Write-only overlay over the whole address space (tdd-ports-memory §5.3):
    /// runs after the plain store, which the special banks send to the trash page
    class WriteIntercept : public HostBusOverlay
    {
    public:
        explicit WriteIntercept(SprinterMemory& owner) : _owner(owner) { observesReads = false; }
        uint8_t onRead(uint16_t, uint8_t normal, bool, bool) override { return normal; }
        void onWrite(uint16_t addr, uint8_t value, bool romPaged) override;

    private:
        SprinterMemory& _owner;
    };

    void OnWrite(uint16_t addr, uint8_t value);
    uint8_t Redirect(uint16_t addr, uint8_t normal) const;
    void LoaderLayout();
    void ClearSpecialBanks();
    void FinishBanks();

    PortDecoder_Sprinter* _decoder = nullptr;
    const SprinterPldState* _pld = nullptr;
    SprinterVideoRam* _vram = nullptr;

    BankAction _action[4] = {};
    ReadRedirect _redirect[4] = {};
    bool _anyRedirect = false;
    /// While loading: the first address the Z84C15 does not give to CS0 (fast RAM from there)
    uint32_t _loadingFastRamFrom = 0x10000;

    WriteIntercept _intercept{*this};

public:
    HostBusOverlay& GetWriteIntercept() { return _intercept; }
};
