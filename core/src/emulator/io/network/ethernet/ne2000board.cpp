#include "emulator/io/network/ethernet/ne2000board.h"

#include <cstdio>
#include <cstring>

namespace
{
std::string Hex(unsigned value, int digits)
{
    char text[16];
    std::snprintf(text, sizeof(text), "#%0*X", digits, value);
    return text;
}

/// RTL8019AS CONFIG1 IOS3-0 for an I/O base (datasheet §5 CONFIG1 table)
uint8_t IosOf(uint16_t base)
{
    static const uint16_t kBases[16] = {0x300, 0x320, 0x340, 0x360, 0x200, 0x220, 0x240, 0x260,
                                        0x380, 0x3A0, 0x3C0, 0x3E0, 0x280, 0x2A0, 0x2C0, 0x2E0};
    for (uint8_t i = 0; i < 16; ++i)
    {
        if (kBases[i] == base)
            return i;
    }
    return 0;
}

/// RTL8019AS CONFIG1 IRQS2-0 for an ISA IRQ
uint8_t IrqsOf(uint8_t irq)
{
    switch (irq)
    {
        case 2: case 9: return 0;
        case 3: return 1;
        case 4: return 2;
        case 5: return 3;
        case 10: return 4;
        case 11: return 5;
        case 12: return 6;
        case 15: return 7;
        default: return 1;
    }
}

/// The ISA Plug and Play serial identifier checksum (the LFSR of the PnP specification, 64 bits LSB first)
uint8_t PnpChecksum(const uint8_t id[8])
{
    uint8_t lfsr = 0x6A;
    for (int i = 0; i < 8; ++i)
    {
        for (int bit = 0; bit < 8; ++bit)
        {
            const uint8_t b = static_cast<uint8_t>((id[i] >> bit) & 1);
            const uint8_t x = static_cast<uint8_t>(((lfsr >> 1) ^ lfsr ^ b) & 1);
            lfsr = static_cast<uint8_t>((lfsr >> 1) | (x << 7));
        }
    }
    return lfsr;
}
}  // namespace

const char* Ne2000Board::VariantName(Variant v)
{
    switch (v)
    {
        case Variant::Rtl8019as: return "RTL8019AS";
        case Variant::Um9003: return "UM9003";
        case Variant::Ne1000: return "NE1000";
    }
    return "RTL8019AS";
}

Ne2000Board::Ne2000Board(const Settings& settings, std::function<uint64_t()> clock)
    : _settings(settings), _clock(std::move(clock)), _chip(*this), _ram(kRamSize, 0)
{
    // The PROM: each MAC byte twice (NE2000 in byte mode), then the 'W' 'W' signature at 28-31; the NE1000's is
    // the plain 16 bytes, mirrored
    const bool doubled = _settings.variant != Variant::Ne1000;
    uint8_t prom16[16] = {};
    std::memcpy(prom16, _settings.mac.data(), 6);
    prom16[14] = prom16[15] = 0x57;
    for (int i = 0; i < 32; ++i)
        _prom[i] = doubled ? prom16[i >> 1] : prom16[i & 15];

    _eeprom.Load(BuildEeprom(_settings));
    Reset();
}

std::array<uint16_t, Eeprom93c46::kWords> Ne2000Board::BuildEeprom(const Settings& settings)
{
    // RTL8019AS 9346 contents (datasheet §6.3): CONFIG1-4, the NE2000 ID PROM (MAC + product ID), the PnP serial
    // identifier and its resource data (the datasheet's example: an I/O range #220-#380 step #20, IRQ 3-5, 9-12, 15)
    uint8_t bytes[128];
    std::memset(bytes, 0xFF, sizeof(bytes));
    bytes[0x00] = static_cast<uint8_t>((IrqsOf(settings.irq) << 4) | IosOf(settings.base));   // CONFIG1 (IRQEN at power-up)
    bytes[0x01] = 0x00;   // CONFIG2: PL = auto-detect (10BASE-T link test), no boot ROM
    bytes[0x02] = 0x00;   // CONFIG3: jumperless, PnP off, half duplex, LEDs mode 0
    bytes[0x03] = 0x00;   // CONFIG4: IOMS off
    std::memcpy(bytes + 0x04, settings.mac.data(), 6);
    static const char kProduct[8] = {'R', 'T', 'L', '8', '0', '1', '9', 'A'};
    std::memcpy(bytes + 0x0A, kProduct, 8);
    uint8_t serial[8] = {0x4A, 0x8C, 0x80, 0x19, settings.mac[2], settings.mac[3], settings.mac[4], settings.mac[5]};
    std::memcpy(bytes + 0x12, serial, 8);
    bytes[0x1A] = PnpChecksum(serial);
    static const uint8_t kResources[] = {
        0x0A, 0x10, 0x10,                                                     // PnP version 1.0
        0x82, 0x22, 0x00, 'R', 'E', 'A', 'L', 'T', 'E', 'K', ' ', 'P', 'L', 'U', 'G', ' ', '&', ' ', 'P', 'L', 'A',
        'Y', ' ', 'E', 'T', 'H', 'E', 'R', 'N', 'E', 'T', ' ', 'C', 'A', 'R', 'D', 0x00,  // ANSI identifier
        0x16, 0x4A, 0x8C, 0x80, 0x19, 0x02, 0x00,                             // logical device ID
        0x1C, 0x41, 0xD0, 0x80, 0xD6,                                         // compatible: PNP80D6 (NE2000)
        0x47, 0x00, 0x20, 0x02, 0x80, 0x03, 0x20, 0x20,                       // I/O #220-#380, step #20, 32 ports
        0x23, 0x38, 0x9E, 0x01,                                               // IRQ 3, 4, 5, 9, 10, 11, 12, 15
        0x79};                                                                 // END
    std::memcpy(bytes + 0x1B, kResources, sizeof(kResources));
    uint8_t sum = 0;
    for (uint8_t b : kResources)
        sum = static_cast<uint8_t>(sum + b);
    bytes[0x1B + sizeof(kResources)] = static_cast<uint8_t>(-sum);

    std::array<uint16_t, Eeprom93c46::kWords> words{};
    for (int w = 0; w < Eeprom93c46::kWords; ++w)
        words[w] = static_cast<uint16_t>(bytes[2 * w] | (bytes[2 * w + 1] << 8));
    return words;
}

// ---------------------------------------------------------------------------
// The board's buffer memory (the DP8390's local bus)
// ---------------------------------------------------------------------------

bool Ne2000Board::RamAddress(uint16_t address, size_t& index) const
{
    address &= 0x7FFF;   // the board does not decode A15
    switch (_settings.variant)
    {
        case Variant::Ne1000:
            if (address >= 0x2000 && address < 0x4000)
            {
                index = address - 0x2000;
                return true;
            }
            return false;
        case Variant::Rtl8019as:
        {
            // Byte mode exposes #4000-#5FFF only (the kit keeps PSTOP at #60 for that reason)
            const uint16_t end = (_chip.GetState().dcr & Dp8390::kDcrWts) ? 0x8000 : 0x6000;
            if (address >= 0x4000 && address < end)
            {
                index = address - 0x4000;
                return true;
            }
            return false;
        }
        default:
            if (address >= 0x4000)
            {
                index = address - 0x4000;
                return true;
            }
            return false;
    }
}

uint8_t Ne2000Board::BufferRead(uint16_t address) const
{
    size_t index = 0;
    if (RamAddress(address, index))
        return _ram[index];
    const uint16_t a = address & 0x7FFF;
    const uint16_t promEnd = _settings.variant == Variant::Ne1000 ? 0x2000 : 0x4000;
    if (a < promEnd)
        return _prom[a & 0x1F];
    return 0xFF;
}

void Ne2000Board::BufferWrite(uint16_t address, uint8_t value)
{
    size_t index = 0;
    if (RamAddress(address, index))
        _ram[index] = value;
}

void Ne2000Board::Transmit(const uint8_t* frame, size_t length)
{
    if (_link)
        _link->Transmit(*this, frame, length);
    else
        ++_counters.txNoLink;
}

// ---------------------------------------------------------------------------
// The bus side
// ---------------------------------------------------------------------------

bool Ne2000Board::Decodes(uint32_t address, uint16_t& offset) const
{
    if ((address & 0x3E0) != _settings.base)
        return false;
    offset = static_cast<uint16_t>(address & 0x1F);
    return true;
}

uint8_t Ne2000Board::ReadPage3(uint8_t offset) const
{
    switch (offset)
    {
        case 0x01: return static_cast<uint8_t>((_cr9346 & 0xCE) | (_eeprom.DataOut() ? 1 : 0));
        case 0x02: return _bpage;
        case 0x03: return 0x00;   // CONFIG0: RTL8019AS (VERID 00), jumperless, UTP with link
        case 0x04: return _config1;
        case 0x05: return _config2;
        case 0x06: return _config3;
        case 0x08: return 0x00;   // CSNSAV: PnP inactive
        case 0x0B:
        {
            // INTR: the selected INTn line follows the interrupt request
            const bool level = (_config1 & 0x80) && _chip.InterruptActive();
            return level ? static_cast<uint8_t>(1u << ((_config1 >> 4) & 7)) : 0;
        }
        case 0x0D: return static_cast<uint8_t>(_config4 & 0x01);
        default: return 0xFF;
    }
}

void Ne2000Board::WritePage3(uint8_t offset, uint8_t value)
{
    const bool configWrite = (_cr9346 & 0xC0) == 0xC0;
    switch (offset)
    {
        case 0x01:
        {
            _cr9346 = static_cast<uint8_t>(value & 0xCE);
            const uint8_t mode = static_cast<uint8_t>(value >> 6);
            if (mode == 2)
                _eeprom.SetPins((value & 0x08) != 0, (value & 0x04) != 0, (value & 0x02) != 0);
            else
                _eeprom.SetPins(false, false, false);
            if (mode == 1)
            {
                // Auto-load (about 2 ms on the chip): the configuration from the EEPROM, CR back to #21, normal mode
                LoadConfigFromEeprom();
                _chip.Reset();
                _cr9346 = 0;
            }
            break;
        }
        case 0x02: _bpage = value; break;
        case 0x04:
            if (configWrite)
                _config1 = static_cast<uint8_t>((_config1 & 0x7F) | (value & 0x80));
            break;
        case 0x05:
            if (configWrite)
                _config2 = static_cast<uint8_t>((_config2 & 0x1F) | (value & 0xE0));
            break;
        case 0x06:
            if (configWrite)
                _config3 = static_cast<uint8_t>((_config3 & ~0x06) | (value & 0x06));
            break;
        default:
            break;   // TEST, HLTCLK, FMWP: no effect here
    }
}

void Ne2000Board::LoadConfigFromEeprom()
{
    _config1 = static_cast<uint8_t>(0x80 | (_eeprom.Byte(0) & 0x7F));   // IRQEN powers up set
    _config2 = static_cast<uint8_t>(_eeprom.Byte(1) & 0xDF);            // BSELB powers up clear
    _config3 = static_cast<uint8_t>(_eeprom.Byte(2) & 0xF9);            // SLEEP, PWRDN clear
    _config4 = static_cast<uint8_t>(_eeprom.Byte(3) & 0x01);
}

uint8_t Ne2000Board::PeekChip(uint8_t offset) const
{
    const uint8_t page = _chip.Page();
    if (offset == 0)
        return _chip.Command();
    if (page == 0 && (offset == 0x0A || offset == 0x0B))
    {
        switch (_settings.variant)
        {
            case Variant::Rtl8019as: return offset == 0x0A ? 0x50 : 0x70;   // 'P' 'p'
            case Variant::Um9003: return offset == 0x0A ? 0x20 : 0x01;
            default: return 0xFF;
        }
    }
    if (page == 3)
    {
        switch (_settings.variant)
        {
            case Variant::Rtl8019as: return ReadPage3(offset);
            case Variant::Um9003: return _chip.PeekRegister(1, offset);   // page 3 mirrors page 1
            default: return 0xFF;
        }
    }
    return _chip.PeekRegister(page, offset);
}

uint8_t Ne2000Board::ReadChip(uint8_t offset, bool peek)
{
    if (peek)
        return PeekChip(offset);
    const uint8_t page = _chip.Page();
    if (offset == 0 || (page == 0 && (offset == 0x0A || offset == 0x0B)) ||
        (page == 3 && _settings.variant == Variant::Rtl8019as))
    {
        _chip.Advance(Now());
        return PeekChip(offset);
    }
    if (page == 3)
        return _settings.variant == Variant::Um9003 ? _chip.ReadRegisterAs(1, offset, Now()) : 0xFF;
    return _chip.ReadRegister(offset, Now());
}

uint8_t Ne2000Board::Read(uint16_t offset)
{
    offset &= 0x1F;
    if (offset < 0x10)
        return ReadChip(static_cast<uint8_t>(offset), false);
    if (offset < 0x18)
        return _chip.DataRead(Now());
    // The reset port
    ++_counters.resetPortReads;
    if (_settings.variant == Variant::Um9003)
    {
        // The UMC clone does not finish this cycle: the ISA bus hangs until RESET (network open question Q5 A)
        _stalled = true;
        return 0xFF;
    }
    _chip.Reset();
    return 0xFF;
}

void Ne2000Board::Write(uint16_t offset, uint8_t value)
{
    offset &= 0x1F;
    if (offset < 0x10)
    {
        const uint8_t page = _chip.Page();
        if (offset != 0 && page == 3)
        {
            if (_settings.variant == Variant::Rtl8019as)
                WritePage3(static_cast<uint8_t>(offset), value);
            else if (_settings.variant == Variant::Um9003)
                _chip.WriteRegisterAs(1, static_cast<uint8_t>(offset), value, Now());
            return;
        }
        _chip.WriteRegister(static_cast<uint8_t>(offset), value, Now());
        return;
    }
    if (offset < 0x18)
        _chip.DataWrite(value, Now());
    // A write to the reset port does nothing (drivers write back what they read)
}

uint8_t Ne2000Board::Peek(uint16_t offset) const
{
    offset &= 0x1F;
    if (offset < 0x10)
        return const_cast<Ne2000Board*>(this)->ReadChip(static_cast<uint8_t>(offset), true);
    if (offset < 0x18)
        return _chip.DataPeek();
    return 0xFF;
}

void Ne2000Board::Reset()
{
    // RESET DRV: the chip resets, the RTL8019AS loads its EEPROM again, a stalled cycle ends
    _chip.Reset();
    _stalled = false;
    _cr9346 = 0;
    _bpage = 0;
    _eeprom.SetPins(false, false, false);
    if (_settings.variant == Variant::Rtl8019as)
        LoadConfigFromEeprom();
}

const char* Ne2000Board::RegisterName(uint16_t offset, bool write) const
{
    offset &= 0x1F;
    if (offset >= 0x18)
        return "reset port";
    if (offset >= 0x10)
        return "data port";
    if (offset == 0)
        return "CR";
    static const char* const kPage0Read[16] = {"CR", "CLDA0", "CLDA1", "BNRY", "TSR", "NCR", "FIFO", "ISR",
                                               "CRDA0", "CRDA1", "ID0", "ID1", "RSR", "CNTR0", "CNTR1", "CNTR2"};
    static const char* const kPage0Write[16] = {"CR", "PSTART", "PSTOP", "BNRY", "TPSR", "TBCR0", "TBCR1", "ISR",
                                                "RSAR0", "RSAR1", "RBCR0", "RBCR1", "RCR", "TCR", "DCR", "IMR"};
    static const char* const kPage1[16] = {"CR", "PAR0", "PAR1", "PAR2", "PAR3", "PAR4", "PAR5", "CURR",
                                           "MAR0", "MAR1", "MAR2", "MAR3", "MAR4", "MAR5", "MAR6", "MAR7"};
    static const char* const kPage2[16] = {"CR", "PSTART", "PSTOP", "RNPP", "TPSR", "LNPP", "ACU", "ACL",
                                           "-", "-", "-", "-", "RCR", "TCR", "DCR", "IMR"};
    static const char* const kPage3[16] = {"CR", "9346CR", "BPAGE", "CONFIG0", "CONFIG1", "CONFIG2", "CONFIG3", "TEST",
                                           "CSNSAV", "HLTCLK", "-", "INTR", "FMWP", "CONFIG4", "-", "-"};
    switch (_chip.Page())
    {
        case 0: return write ? kPage0Write[offset] : kPage0Read[offset];
        case 1: return kPage1[offset];
        case 2: return kPage2[offset];
        default: return kPage3[offset];
    }
}

bool Ne2000Board::Irq() const
{
    if (_settings.variant == Variant::Rtl8019as && !(_config1 & 0x80))
        return false;   // IRQEN clear: the line is tri-stated
    return _chip.InterruptActive();
}

void Ne2000Board::SetIrqListener(std::function<void()> changed)
{
    _chip.SetInterruptListener(std::move(changed));
}

void Ne2000Board::OnFrame()
{
    _chip.Advance(Now());
}

void Ne2000Board::StationMac(uint8_t out[6]) const
{
    std::memcpy(out, _settings.mac.data(), 6);
}

bool Ne2000Board::Offer(const uint8_t* frame, size_t length)
{
    return _chip.Receive(frame, length, Now());
}

void Ne2000Board::Describe(StateNode& out) const
{
    const Dp8390::State& s = _chip.GetState();
    out["chip"] = VariantName(_settings.variant);
    out["base"] = Hex(_settings.base, 3);
    out["irq"] = static_cast<int>(_settings.irq);
    char mac[24];
    std::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", _settings.mac[0], _settings.mac[1],
                  _settings.mac[2], _settings.mac[3], _settings.mac[4], _settings.mac[5]);
    out["mac"] = std::string(mac);
    out["port_key"] = _settings.key;
    out["link"] = _link ? "ethernet-gateway" : "none";
    out["running"] = _chip.Running();
    out["irq_level"] = Irq();
    if (_stalled)
        out["stalled"] = "the ISA cycle hangs: the UM9003's reset port was read (the machine waits for RESET)";
    StateNode r = StateNode::Object();
    r["cr"] = Hex(s.cr, 2);
    r["isr"] = Hex(s.isr, 2);
    r["imr"] = Hex(s.imr, 2);
    r["rcr"] = Hex(s.rcr, 2);
    r["tcr"] = Hex(s.tcr, 2);
    r["dcr"] = Hex(s.dcr, 2);
    r["pstart"] = Hex(s.pstart, 2);
    r["pstop"] = Hex(s.pstop, 2);
    r["bnry"] = Hex(s.bnry, 2);
    r["curr"] = Hex(s.curr, 2);
    r["tpsr"] = Hex(s.tpsr, 2);
    r["tbcr"] = static_cast<int>(s.tbcr);
    r["rsar"] = Hex(s.rsar, 4);
    r["rbcr"] = static_cast<int>(s.rbcr);
    r["crda"] = Hex(s.crda, 4);
    char par[24];
    std::snprintf(par, sizeof(par), "%02X:%02X:%02X:%02X:%02X:%02X", s.par[0], s.par[1], s.par[2], s.par[3], s.par[4],
                  s.par[5]);
    r["par"] = std::string(par);
    if (_settings.variant == Variant::Rtl8019as)
    {
        r["config1"] = Hex(_config1, 2);
        r["config2"] = Hex(_config2, 2);
        r["config3"] = Hex(_config3, 2);
    }
    out["registers"] = r;
    StateNode c = StateNode::Object();
    c["tx_frames"] = s.framesOut;
    c["rx_frames"] = s.framesIn;
    c["rx_filtered"] = s.framesFiltered;
    c["rx_missed"] = s.framesMissed;
    c["rx_ring_full_waits"] = _chip.RingFullWaits();
    c["tx_no_link"] = _counters.txNoLink;
    c["reset_port_reads"] = _counters.resetPortReads;
    out["counters"] = c;
}

// ---------------------------------------------------------------------------
// TTD
// ---------------------------------------------------------------------------

size_t Ne2000Board::StateSize() const
{
    return 2 + sizeof(Dp8390::State) + sizeof(Eeprom93c46::State) + kRamSize + 8 + 6;
}

void Ne2000Board::SaveState(uint8_t* dst) const
{
    size_t at = 0;
    dst[at++] = kStateVersion;
    dst[at++] = static_cast<uint8_t>(_settings.variant);
    std::memcpy(dst + at, &_chip.GetState(), sizeof(Dp8390::State));
    at += sizeof(Dp8390::State);
    std::memcpy(dst + at, &_eeprom.GetState(), sizeof(Eeprom93c46::State));
    at += sizeof(Eeprom93c46::State);
    std::memcpy(dst + at, _ram.data(), kRamSize);
    at += kRamSize;
    const uint8_t tail[8] = {_cr9346, _bpage, _config1, _config2, _config3, _config4, static_cast<uint8_t>(_stalled ? 1 : 0), 0};
    std::memcpy(dst + at, tail, sizeof(tail));
    at += sizeof(tail);
    std::memcpy(dst + at, _settings.mac.data(), 6);   // the station address the PROM held when recorded
}

bool Ne2000Board::LoadState(const uint8_t* src, size_t size)
{
    if (size < StateSize() || src[0] != kStateVersion || src[1] != static_cast<uint8_t>(_settings.variant))
        return false;
    size_t at = 2;
    Dp8390::State chip;
    std::memcpy(&chip, src + at, sizeof(chip));
    _chip.LoadState(chip);
    at += sizeof(Dp8390::State);
    Eeprom93c46::State eeprom;
    std::memcpy(&eeprom, src + at, sizeof(eeprom));
    _eeprom.LoadState(eeprom);
    at += sizeof(Eeprom93c46::State);
    std::memcpy(_ram.data(), src + at, kRamSize);
    at += kRamSize;
    _cr9346 = src[at];
    _bpage = src[at + 1];
    _config1 = src[at + 2];
    _config2 = src[at + 3];
    _config3 = src[at + 4];
    _config4 = src[at + 5];
    _stalled = src[at + 6] != 0;
    at += 8;
    // The recorded station address: the PROM follows it (an instance with another automatic MAC replays exactly)
    std::memcpy(_settings.mac.data(), src + at, 6);
    const bool doubled = _settings.variant != Variant::Ne1000;
    uint8_t prom16[16] = {};
    std::memcpy(prom16, _settings.mac.data(), 6);
    prom16[14] = prom16[15] = 0x57;
    for (int i = 0; i < 32; ++i)
        _prom[i] = doubled ? prom16[i >> 1] : prom16[i & 15];
    return true;
}
