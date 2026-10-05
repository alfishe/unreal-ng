#include "stdafx.h"

#include "sprintermemory.h"

#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/ports/models/sprinter/sprinterpldconfiguration.h"
#include "emulator/ports/models/sprinter/sprinterpldstate.h"
#include "emulator/video/sprinter/sprintervideoram.h"

SprinterMemory::SprinterMemory(EmulatorContext* context) : Memory(context)
{
}

void SprinterMemory::AttachDecoder(PortDecoder_Sprinter* decoder)
{
    _decoder = decoder;
    _pld = decoder ? &decoder->GetPldState() : nullptr;
    _vram = decoder ? &decoder->GetVideoRam() : nullptr;
}

/// region <Latch-to-bank translation>

bool SprinterMemory::UpdateModelBanks()
{
    EmulatorState& state = _context->emulatorState;

    // The Sprinter's TR-DOS signal and its ports come from the PLD (the port
    // table has /DOS in its index): the generic #3Dxx session machinery stays off
    state.flags &= ~(CF_TRDOS | CF_DOSPORTS | CF_Z80FBUS | CF_LEAVEDOSRAM | CF_LEAVEDOSADR | CF_SETDOSROM);

    ClearSpecialBanks();

    if (!_pld || _pld->configState != SprinterConfigState::Configured)
    {
        LoaderLayout();
    }
    else
    {
        if (!_decoder->ActiveModule().UpdateBanks(*this, *_pld))
            _decoder->GetRegistry().Standard().UpdateBanks(*this, *_pld);

        if (!_pld->dos)
            state.flags |= CF_TRDOS;  // shown by the debugger; nothing else reads it on this machine
    }

    FinishBanks();
    return true;
}

void SprinterMemory::ClearSpecialBanks()
{
    for (uint8_t bank = 0; bank < 4; bank++)
    {
        _action[bank] = BankAction::Plain;
        _redirect[bank] = ReadRedirect::None;
    }
    _loadingFastRamFrom = 0x10000;
}

/// Before the PLD is configured the small CPLD shows ROM pages #C-#F at
/// #0000-#FFFF (loader README); the Z84C15 gives the addresses above its CS0
/// boundary to the fast RAM (MAME z84c015 translate_memory_address +
/// sprinter bootstrap_r / bootstrap_w). Every write is a configuration bit
void SprinterMemory::LoaderLayout()
{
    for (uint8_t bank = 0; bank < 4; bank++)
        MapRomToBank(bank, static_cast<uint8_t>(kLoaderRomPage + bank));

    if (_decoder)
        _loadingFastRamFrom = _decoder->GetZ84().system.Cs0End();
    for (uint8_t bank = 0; bank < 4; bank++)
    {
        if ((static_cast<uint32_t>(bank) + 1) * PAGE_SIZE > _loadingFastRamFrom)
            _redirect[bank] = ReadRedirect::LoadingCs;
    }
}

void SprinterMemory::StandardUpdateBanks(const SprinterPldState& pld)
{
    const bool sc0 = (pld.sc & 0x01) != 0;
    const bool ramSys = pld.ramSys != 0;

    // Window 0 (MAME update_memory: pre_rom = rom_sys || cash_on, pre_cash = !cash_on)
    if (!pld.romOff && !pld.cacheOn)
    {
        // System ROM: ROM_RG[3:0] with bit 3 inverted unless SYS_PG
        MapRomToBank(0, static_cast<uint8_t>((pld.romRg & 0x0F) ^ (pld.sysPg ? 0x00 : 0x08)));
    }
    else if (pld.cacheOn)
    {
        MapFastRamToBank(0, static_cast<uint8_t>(pld.romRg & 0x03));
    }
    else
    {
        // Spectrum mode: a "vROM" RAM page from the cells #E0-#EF
        const bool scLc = !(sc0 && ramSys);
        const uint8_t spr = (pld.sc & 0x02) ? 0 : static_cast<uint8_t>((pld.dos << 1) | (((pld.pn & 0x10) || !pld.dos) ? 1 : 0));
        const uint8_t cell = static_cast<uint8_t>(0x20 | ((sc0 || !ramSys) ? 0x08 : 0) |
                                                  ((pld.arom16 && !(sc0 && ramSys)) ? 0x04 : 0) |
                                                  ((((spr & 0x02) && scLc) || !ramSys) ? 0x02 : 0) |
                                                  ((((spr & 0x01) && scLc) || !ramSys) ? 0x01 : 0));
        MapRamToBank(0, pld.cells[cell], sc0 && ramSys);
    }

    MapRamToBank(1, pld.Cell(SprinterCode::Page1), true);
    MapRamToBank(2, pld.Cell(SprinterCode::Page2), true);

    // Window 3: the cell of the current Spectrum page; page #40 until the first port read
    const uint8_t page3 = pld.starting ? kPortTablePage : pld.cells[pld.pg3 & 0x3F];
    SprinterIsaBus::Space isaSpace = SprinterIsaBus::Space::Memory;
    int isaSlot = 0;
    if ((pld.sc & 0x10) && SprinterIsaBus::PageToSlot(page3, isaSpace, isaSlot))
    {
        // The ISA view: the RAM page under it is neither read nor written (the trash page takes the plain store)
        MapRamToBank(3, page3, false);
        _action[3] = BankAction::Isa;
        _redirect[3] = ReadRedirect::Isa;
        _isaSpace = static_cast<uint8_t>(isaSpace);
        _isaSlot = static_cast<uint8_t>(isaSlot);
    }
    else
    {
        MapRamToBank(3, page3, true);
        if (pld.sc == 0x10 && page3 == kResetPage)
            _action[3] = BankAction::ResetPage;
    }
}

void SprinterMemory::MapRomToBank(uint8_t bank, uint8_t romPage)
{
    SetROMPageToBank(bank, romPage);
}

void SprinterMemory::MapRamToBank(uint8_t bank, uint8_t ramPage, bool writable)
{
    switch (bank & 3)
    {
        case 0: SetRAMPageToBank0(ramPage); break;
        case 1: SetRAMPageToBank1(ramPage); break;
        case 2: SetRAMPageToBank2(ramPage); break;
        default: SetRAMPageToBank3(ramPage); break;
    }

    // Graphics pages: the plain store must not land (the intercept writes the
    // video address), reads come from the video address
    const bool graphics = (ramPage & 0xF0) == kGraphicsFirstPage;
    if (graphics)
    {
        _redirect[bank & 3] = ReadRedirect::Graphics;
        if (writable)
            _action[bank & 3] = BankAction::Graphics;
    }
    if (!writable || graphics)
        SetBankWriteProtected(bank);
    else if (ramPage == kCblPage)
        _action[bank & 3] = BankAction::CblPage;
    else if (ramPage == kPortTablePage && _decoder && _decoder->PldJournal().Enabled())
        _action[bank & 3] = BankAction::PortTable;  // the PLD journal notes table writes (nothing when it is off)
}

void SprinterMemory::MapFastRamToBank(uint8_t bank, uint8_t fastRamPage)
{
    bank &= 3;
    _bank_mode[bank] = BANK_CACHE;
    _bank_read[bank] = _bank_write[bank] = CacheBase() + static_cast<size_t>(fastRamPage & (MAX_CACHE_PAGES - 1)) * PAGE_SIZE;
    // Not a RAM page: TTD journals RAM pages only (fast RAM joins the TTD state in phase S7)
    _bank_ram_page_cache[bank] = ttd::kPhysPageNone;
    UpdateSlotContention(bank);
    if (bank == 0)
        SetROMPageFlags();
}

void SprinterMemory::FinishBanks()
{
    _anyRedirect = false;
    for (uint8_t bank = 0; bank < 4; bank++)
        _anyRedirect |= _redirect[bank] != ReadRedirect::None;
    _toolReadRedirect = _anyRedirect;

    if (_decoder)
        _decoder->OnBanksChanged();
}

/// endregion </Latch-to-bank translation>

/// region <Read redirect>

uint8_t SprinterMemory::Redirect(uint16_t addr, uint8_t normal) const
{
    switch (_redirect[addr >> 14])
    {
        case ReadRedirect::Graphics:
            return _ramBase[static_cast<size_t>(kGraphicsFirstPage) * PAGE_SIZE + _pld->portY * 1024u + (addr & 0x3FF)];
        case ReadRedirect::Isa:
            // Tools (debugger, memory viewer, automation) peek the card: no side effect
            return _decoder ? _decoder->GetIsaBus().Peek(static_cast<SprinterIsaBus::Space>(_isaSpace), _isaSlot,
                                                         static_cast<uint16_t>(addr & 0x3FFF))
                            : 0xFF;
        case ReadRedirect::LoadingCs:
            return addr >= _loadingFastRamFrom ? _cacheBase[addr] : normal;
        default:
            return normal;
    }
}

uint8_t SprinterMemory::IsaRead(uint16_t addr)
{
    return _decoder ? _decoder->GetIsaBus().Read(static_cast<SprinterIsaBus::Space>(_isaSpace), _isaSlot,
                                                 static_cast<uint16_t>(addr & 0x3FFF))
                    : 0xFF;
}

uint8_t SprinterMemory::MemoryReadFast(uint16_t addr, bool isExecution)
{
    const uint8_t value = Memory::MemoryReadFast(addr, isExecution);
    if (_anyRedirect) [[unlikely]]
    {
        // A CPU read or opcode fetch in the ISA view is a real ISA cycle (code can run from ISA memory)
        if (_redirect[addr >> 14] == ReadRedirect::Isa)
            return IsaRead(addr);
        return Redirect(addr, value);
    }
    return value;
}

uint8_t SprinterMemory::MemoryReadDebug(uint16_t addr, bool isExecution)
{
    const uint8_t value = Memory::MemoryReadDebug(addr, isExecution);
    if (_anyRedirect) [[unlikely]]
    {
        if (_redirect[addr >> 14] == ReadRedirect::Isa)
            return IsaRead(addr);
        return Redirect(addr, value);
    }
    return value;
}

/// endregion </Read redirect>

/// region <Write intercept>

uint32_t SprinterMemory::ZxShadowAddress(uint16_t addr, uint8_t portY, uint8_t pg3)
{
    const uint8_t zxs = portY & 0x3F;
    const uint32_t zxA15 = (addr & 0x8000) ? ((pg3 >> 1) & 1u) : 0u;
    const uint32_t a13 = (addr >> 13) & 1u;
    return (static_cast<uint32_t>(addr & 0xFF) << 10) | (static_cast<uint32_t>((zxs >> 1) & 0x0F) << 6) |
           (((zxs & 1u) ^ zxA15 ^ a13) << 5) | ((addr >> 8) & 0x1Fu);
}

void SprinterMemory::AcceleratorWrite(uint16_t addr, uint8_t value)
{
    const uint8_t bank = static_cast<uint8_t>(addr >> 14);
    MemoryWriteFast(addr, value);  // write-protected windows (graphics) store into the trash page
    if (_bank_ram_page_cache[bank] != ttd::kPhysPageNone)
        MarkRamPageEdited(static_cast<uint16_t>(_bank_ram_page_cache[bank]));
    OnWrite(addr, value);
}

void SprinterMemory::RefreshZxShadow(uint8_t window)
{
    window &= 3;
    if (_bank_mode[window] != BANK_RAM || _action[window] != BankAction::Plain || !_pld)
        return;
    const uint8_t* bytes = _bank_read[window];
    for (uint32_t i = 0; i < PAGE_SIZE; ++i)
        OnWrite(static_cast<uint16_t>((static_cast<uint32_t>(window) << 14) | i), bytes[i]);
}

void SprinterMemory::WriteIntercept::onWrite(uint16_t addr, uint8_t value, [[maybe_unused]] bool romPaged)
{
    _owner.OnWrite(addr, value);
}

void SprinterMemory::OnWrite(uint16_t addr, uint8_t value)
{
    if (!_pld)
        return;

    if (_pld->configState != SprinterConfigState::Configured)
    {
        // Configuration loading: the write is a configuration clock; the
        // addresses outside CS0 hold fast RAM (the loader's #FExx writes)
        if (addr >= _loadingFastRamFrom)
            _cacheBase[addr] = value;
        _decoder->OnConfigurationWrite(value);
        return;
    }

    const uint8_t bank = static_cast<uint8_t>(addr >> 14);
    switch (_action[bank])
    {
        case BankAction::Graphics:
        {
            const uint8_t page = static_cast<uint8_t>(GetRAMPageFromAddress(_bank_read[bank]));
            if ((page & 0x08) && value == 0xFF)
                return;  // transparent: #FF is not written
            const uint32_t videoAddr = _pld->portY * 1024u + (addr & 0x3FF);
            if (!(page & 0x04))
            {
                _ramBase[static_cast<size_t>(kGraphicsFirstPage) * PAGE_SIZE + videoAddr] = value;
                MarkRamPageEdited(static_cast<uint16_t>(kGraphicsFirstPage + (videoAddr >> 14)));
            }
            if (_vram)
                _vram->Write(videoAddr, value);
            return;
        }
        case BankAction::Isa:
            // An ISA cycle replaces the RAM cycle: no plain store landed (write-protected) and no Spectrum
            // screen shadow write happens (the PLD's ISA select wins; Sprinter ISA tdd §4.3)
            _decoder->GetIsaBus().Write(static_cast<SprinterIsaBus::Space>(_isaSpace), _isaSlot,
                                        static_cast<uint16_t>(addr & 0x3FFF), value);
            return;
        case BankAction::ResetPage:
            _decoder->OnResetPageWrite();
            break;
        case BankAction::PortTable:
            _decoder->OnPortTableWrite(addr);
            break;
        case BankAction::CblPage:
            _decoder->OnCblPageWrite(addr, value);
            break;
        default:
            break;
    }

    // Spectrum screen shadow (ALL_MODE bit 0 = 0): window 1, or window 3 with
    // Spectrum page 5 / 7; #6000-#7FFF only with PORT_Y bit 7; PORT_Y bit 6 = 1 disables
    if (!(_pld->allMode & 0x01) && _vram && _bank_mode[bank] == BANK_RAM)
    {
        const bool spectrumScreen = (addr & 0x4000) && (!(addr & 0x8000) || (_pld->pg3 & 0x3D) == 0x35);
        if (spectrumScreen && !((addr & 0x2000) && !(_pld->portY & 0x80)) && !(_pld->portY & 0x40))
            _vram->Write(ZxShadowAddress(addr, _pld->portY, _pld->pg3), value);
    }
}

/// endregion </Write intercept>
