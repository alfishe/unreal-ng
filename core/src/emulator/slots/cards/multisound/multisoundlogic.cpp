#include "multisoundlogic.h"

// Every rule cites the top.v (commit d7f3ac2) term it reproduces; the L numbers are tdd-card-logic.md §3.

void MultiSoundLogic::Reset()
{
    // L17: the reset branches of every always block with 'negedge rst_n'
    _latches = MultiSoundLatches{};
    _dac = {};
}

bool MultiSoundLogic::Iorqge(uint16_t port) const
{
    // L2, L16: zxiorqge_n = ~(zxm1_n && (port_fffd_full || port_bffd || port_b3 || port_bb)); no direction term
    const bool ymRegisterFull = _options.ym && (port & 0xE00Fu) == 0xE00Du;
    return ymRegisterFull || IsYmDataPort(port) || IsGsDataPort(port) || IsGsCommandPort(port);
}

MultiSoundBusActions MultiSoundLogic::Write(uint16_t port, uint8_t value)
{
    MultiSoundBusActions actions{};
    size_t count = 0;
    auto push = [&](MultiSoundBusAction::Kind kind, uint8_t chip)
    {
        actions[count++] = MultiSoundBusAction{ kind, chip, value };
    };

    // The control byte: the YM latches (port_fffd, needs ym) and the SAA clock (port_fffd_saa, needs saa only) are
    // separate always blocks with separate enables (L3, L4, L7)
    const bool registerPort = (port & 0xC00Fu) == 0xC00Du;
    const bool top4 = (value & 0xF0u) == 0xF0u;
    const bool top5 = (value & 0xF8u) == 0xF8u;
    const bool ymControl = _options.ym && registerPort &&
                           ((_options.ctrlMask == MultiSoundCtrlMask::Classic && !_options.saa) ? top5 : top4);
    const bool saaControl = _options.saa && registerPort && top4;

    if (ymControl)
    {
        _latches.ymChip = value & 0x01u;
        _latches.ymReadStatus = (value & 0x02u) == 0;
        _latches.fmMuted = (value & 0x04u) != 0;
    }
    if (saaControl)
        _latches.saaClock = (value & 0x08u) == 0;
    if (ymControl || saaControl)
        push(MultiSoundBusAction::Kind::Control, _latches.ymChip);

    // L1, L5: ym*_cs_n decode the address only, so the control byte is also an address write. The chip select
    // changes within the cycle; the chip that holds CS when /WR rises (the new one) latches it
    if (IsYmRegisterPort(port))
        push(MultiSoundBusAction::Kind::YmAddress, _latches.ymChip);
    else if (IsYmDataPort(port))
        push(MultiSoundBusAction::Kind::YmData, _latches.ymChip);
    else if (IsSaaPort(port))
    {
        // L8: aa0 = zxa[1] ? zxa[8] : ym_a0; A0 = 1 is the SAA address register
        push((port & 0x0100u) ? MultiSoundBusAction::Kind::SaaAddress : MultiSoundBusAction::Kind::SaaData, 0);
    }
    else if (IsGsDataPort(port))
    {
        // L12: gs_regdata <= zxd; gs_flag_data <= 1 on the cycle's first edge
        _latches.gsData = value;
        _latches.dataFlag = true;
        push(MultiSoundBusAction::Kind::GsData, 0);
    }
    else if (IsGsCommandPort(port))
    {
        _latches.gsCommand = value;
        _latches.commandFlag = true;
        push(MultiSoundBusAction::Kind::GsCommand, 0);
    }
    else if (IsSoundrivePort(port))
    {
        // L9, L14: sd_dac*_wr sets vol* = 63 and dac* = converted sample
        const uint8_t channel = SoundriveChannel(port);
        _dac[channel].sample = ConvertSample(value);
        _dac[channel].volume = 0x3F;
        push(MultiSoundBusAction::Kind::SoundriveSample, channel);
    }

    return actions;
}

MultiSoundReadResult MultiSoundLogic::Peek(uint16_t port) const
{
    // L11: zxd = ad for port_fffd (any A13); ym_a0 = ~ym_get_stat for a read with A14 = 1
    MultiSoundReadResult result;
    if (IsYmRegisterPort(port))
    {
        result.source = _latches.ymReadStatus ? MultiSoundReadResult::Source::YmStatus
                                              : MultiSoundReadResult::Source::YmRegister;
        result.chip = _latches.ymChip;
    }
    else if (IsGsDataPort(port))
    {
        result.source = MultiSoundReadResult::Source::GsOutput;
        result.value = _latches.gsOutput;
    }
    else if (IsGsCommandPort(port))
    {
        result.source = MultiSoundReadResult::Source::GsStatus;
        result.value = GsStatus();
    }
    return result;
}

MultiSoundReadResult MultiSoundLogic::Read(uint16_t port)
{
    MultiSoundReadResult result = Peek(port);
    if (result.source == MultiSoundReadResult::Source::GsOutput)
        _latches.dataFlag = false;      // L12: ioreq_rd && !ioreq_prev && port_b3
    return result;
}

void MultiSoundLogic::GsPortWrite(uint8_t gsPort, uint8_t value)
{
    // L13: gs_reg00 / gs_reg_out on ~giorq_n && ~gwr_n; volumes 6-9 (gd[5:0]); then the flag edges, which fire
    // on any access (read or write) to the port
    switch (gsPort & 0x0Fu)
    {
        case 0x0: _latches.gsPage = value; break;
        case 0x3: _latches.gsOutput = value; break;
        case 0x6: case 0x7: case 0x8: case 0x9:
            GsDacVolume((gsPort & 0x0F) - 6, value);
            break;
        default: break;
    }
    GsPortRead(gsPort);     // the flag rules are the same for both directions; the read value is dropped
}

uint8_t MultiSoundLogic::GsPortRead(uint8_t gsPort)
{
    const uint8_t port = gsPort & 0x0Fu;

    // gd is combinational: the value is the one before this access's flag edge
    uint8_t value = 0xFF;
    if (port == 0x4)
        value = GsStatus();
    else if (port == 0x2)
        value = _latches.gsData;
    else if (port == 0x1)
        value = _latches.gsCommand;

    switch (port)
    {
        case 0x2: _latches.dataFlag = false; break;
        case 0x3: _latches.dataFlag = true; break;
        case 0xA: _latches.dataFlag = (_latches.gsPage & 0x01u) == 0; break;
        case 0x5: _latches.commandFlag = false; break;
        case 0xB: _latches.commandFlag = (_dac[3].volume & 0x20u) != 0; break;
        default: break;
    }
    return value;
}

void MultiSoundLogic::GsMemoryRead(uint16_t address, uint8_t value)
{
    // L14: gs_dac*_cs = ~gmreq_n && ga[15:13] == 3'b011 && ga[9:8] == ch, written while ~grd_n
    if ((address & 0xE000u) == 0x6000u)
        GsDacSample((address >> 8) & 0x03, value);
}

uint8_t MultiSoundLogic::GsStatus() const
{
    return static_cast<uint8_t>((_latches.dataFlag ? 0x80u : 0x00u) | 0x7Eu | (_latches.commandFlag ? 0x01u : 0x00u));
}

MultiSoundGsMapping MultiSoundLogic::GsMemoryMapFor(uint8_t gsPage, MultiSoundGsRam gsRam, uint16_t address)
{
    // GS bus controller: grom_n, gram*_n, gma
    const uint8_t page = gsPage & 0x7Fu;
    const bool upper = (address & 0x8000u) != 0;

    MultiSoundGsMapping mapping;
    mapping.gma = upper ? static_cast<uint8_t>(page & 0x0Fu) : uint8_t{ 1 };

    if ((address & 0xC000u) == 0 || (upper && page == 0))
    {
        mapping.chip = MultiSoundGsMapping::Chip::Rom;
        return mapping;
    }

    if (gsRam == MultiSoundGsRam::TwoMb)
    {
        const uint8_t bank = upper ? static_cast<uint8_t>((page >> 4) & 0x03u) : uint8_t{ 0 };
        mapping.chip = static_cast<MultiSoundGsMapping::Chip>(static_cast<uint8_t>(MultiSoundGsMapping::Chip::Ram1) + bank);
    }
    else
    {
        mapping.chip = (upper && (page & 0x10u)) ? MultiSoundGsMapping::Chip::Ram2 : MultiSoundGsMapping::Chip::Ram1;
    }
    return mapping;
}

void MultiSoundLogic::GsDacSample(int channel, uint8_t sample)
{
    _dac[static_cast<size_t>(channel & 3)].sample = ConvertSample(sample);
}

void MultiSoundLogic::GsDacVolume(int channel, uint8_t volume)
{
    _dac[static_cast<size_t>(channel & 3)].volume = volume & 0x3Fu;
}
