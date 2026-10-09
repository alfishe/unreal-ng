// DeviceState::Next / NextRegs / NextMmu - the ZX Spectrum Next reports every automation interface renders
// (declared in emulator/state/devicestate.h; built here, beside the Next code, so the shared state code names no Next
// type). Design: docs/inprogress/2026-10-07-zx-next/design-automation.md

#include "stdafx.h"

#include "emulator/state/devicestate.h"

#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextregtable.h"
#include "emulator/ports/models/portdecoder_next.h"

namespace
{
StateNode Unavailable()
{
    StateNode n = StateNode::Object();
    n["available"] = false;
    n["description"] = "not a ZX Spectrum Next";
    return n;
}

PortDecoder_Next* NextDecoder(EmulatorContext* context)
{
    return context ? dynamic_cast<PortDecoder_Next*>(context->pPortDecoder) : nullptr;
}

std::string Hex8(unsigned value) { return StringHelper::Format("0x%02X", value & 0xFFu); }
std::string Hex16(unsigned value) { return StringHelper::Format("0x%04X", value & 0xFFFFu); }

const char* TimingName(uint8_t timing)
{
    switch (timing)
    {
        case 1: return "48K";
        case 2: return "128K";
        case 3: return "+3";
        case 4: return "Pentagon";
        default: return "unknown";
    }
}

StateNode NextSlotNodes(EmulatorContext* context, PortDecoder_Next& decoder)
{
    (void)decoder;
    NextMemory& memory = *static_cast<NextMemory*>(context->pMemory);
    StateNode slots = StateNode::Array();
    for (unsigned s = 0; s < NextMemory::kSlots; s++)
    {
        StateNode slot = StateNode::Object();
        slot["slot"] = int(s);
        slot["address_range"] = StringHelper::Format("0x%04X-0x%04X", s * 0x2000, s * 0x2000 + 0x1FFF);
        slot["mmu"] = Hex8(memory.GetMmu(s));
        slot["kind"] = memory.SlotKind(s);
        slot["buffer_offset"] = StringHelper::Format("0x%08X", memory.SlotReadOffset(s));
        slots.push(std::move(slot));
    }
    return slots;
}
}  // namespace

namespace DeviceState
{
StateNode NextMmu(EmulatorContext* context)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextMemory& memory = *static_cast<NextMemory*>(context->pMemory);
    StateNode n = StateNode::Object();
    n["available"] = true;
    n["slots"] = NextSlotNodes(context, *decoder);
    n["rom_select"] = int(memory.GetRomSelect());
    n["config_mode"] = memory.InConfigMode();
    n["boot_rom"] = memory.BootRomEnabled();
    n["config_bank"] = int(memory.GetConfigBank());
    n["extended_bank"] = Hex8(memory.GetExtendedBank());
    n["p7ffd"] = Hex8(context->emulatorState.p7FFD);
    n["p1ffd"] = Hex8(context->emulatorState.p1FFD);
    const NextMemory::DivMmcView& div = memory.GetDivMmcView();
    n["divmmc_mapped"] = div.mapped;
    return n;
}

StateNode NextRegs(EmulatorContext* context)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    StateNode n = StateNode::Object();
    n["available"] = true;
    n["selected"] = Hex8(decoder->Board().SelectedRegister());
    StateNode regs = StateNode::Array();
    size_t count = 0;
    const NextRegInfo* table = NextRegTable(count);
    for (size_t i = 0; i < count; i++)
    {
        const NextRegInfo& r = table[i];
        StateNode reg = StateNode::Object();
        reg["nr"] = Hex8(r.number);
        reg["name"] = r.name;
        reg["access"] = std::string(r.readable ? "R" : "") + (r.writable ? "W" : "");
        reg["value"] = Hex8(r.readable ? decoder->Board().Read(r.number) : decoder->Board().Stored(r.number));
        if (r.hasReset)
            reg["reset"] = Hex8(r.reset);
        regs.push(std::move(reg));
    }
    n["registers"] = std::move(regs);
    return n;
}

StateNode Next(EmulatorContext* context)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextMemory& memory = *static_cast<NextMemory*>(context->pMemory);
    NextBoard& board = decoder->Board();
    const EmulatorState& state = context->emulatorState;

    StateNode n = StateNode::Object();
    n["available"] = true;

    StateNode machine = StateNode::Object();
    machine["machine_type"] = int(board.MachineType());
    machine["timing"] = TimingName(board.Timing());
    machine["config_mode"] = memory.InConfigMode();
    machine["boot_rom"] = memory.BootRomEnabled();
    machine["cpu_clock_hz"] = static_cast<double>(state.current_z80_frequency);
    machine["speed_ratio"] = int(state.hw_turbo_ratio);
    machine["contention_disabled"] = state.hw_contention_disabled != 0;
    machine["frame_tstates"] = int(context->config.frame);
    n["machine"] = std::move(machine);

    n["mmu"] = NextMmu(context);

    StateNode div = StateNode::Object();
    NextDivMmc& divmmc = decoder->DivMmc();
    div["control"] = Hex8(divmmc.ReadPort());
    div["mapped"] = divmmc.Mapped();
    div["automapped"] = divmmc.Automapped();
    div["mapram"] = divmmc.Mapram();
    div["bank"] = int(divmmc.Bank());
    div["automap_enabled"] = (board.Stored(NextBoard::kRegPeripheral3) & 0x10) != 0;
    div["entry_points"] = Hex8(board.Stored(0xB8));
    div["entry_valid"] = Hex8(board.Stored(0xB9));
    div["entry_timing"] = Hex8(board.Stored(0xBA));
    div["entry_extra"] = Hex8(board.Stored(0xBB));
    n["divmmc"] = std::move(div);

    NextInterruptSource& irq = decoder->Interrupts();
    StateNode ints = StateNode::Object();
    ints["mode"] = irq.HardwareMode() ? "hardware im2" : "pulse";
    ints["pending"] = Hex16(irq.PendingMask());
    ints["in_service"] = Hex16(irq.InServiceMask());
    uint8_t value = 0;
    for (uint8_t reg : {NextInterruptSource::kRegControl, NextInterruptSource::kRegEnable0, NextInterruptSource::kRegEnableCtc,
                        NextInterruptSource::kRegEnableUart, NextInterruptSource::kRegLineControl, NextInterruptSource::kRegLineValue})
        if (irq.ReadNr(reg, value))
            ints[StringHelper::Format("nr_%02x", reg)] = Hex8(value);
    n["interrupts"] = std::move(ints);

    StateNode ctc = StateNode::Array();
    for (unsigned c = 0; c < NextCtc::kChannels; c++)
    {
        StateNode ch = StateNode::Object();
        ch["channel"] = int(c);
        ch["control"] = Hex8(decoder->Ctc().Control(c));
        ch["time_constant"] = int(decoder->Ctc().TimeConstant(c));
        ch["running"] = decoder->Ctc().Running(c);
        ch["zero_counts"] = int(decoder->Ctc().Zeros(c));
        ctc.push(std::move(ch));
    }
    n["ctc"] = std::move(ctc);

    StateNode spi = StateNode::Object();
    for (unsigned c = 0; c < 2; c++)
    {
        StateNode card = StateNode::Object();
        card["present"] = decoder->SdCard(c).present();
        card["blocks_read"] = static_cast<double>(decoder->SdCard(c).blocksRead());
        card["blocks_written"] = static_cast<double>(decoder->SdCard(c).blocksWritten());
        spi[c == 0 ? "card0" : "card1"] = std::move(card);
    }
    spi["too_fast_accesses"] = int(decoder->SpiTooFastCount());
    n["spi"] = std::move(spi);

    Ds1307& rtc = decoder->I2c().Rtc();
    StateNode r = StateNode::Object();
    r["time_bcd"] = StringHelper::Format("%02X:%02X:%02X", rtc.Reg(2), rtc.Reg(1), rtc.Reg(0) & 0x7F);
    r["date_bcd"] = StringHelper::Format("20%02X-%02X-%02X", rtc.Reg(6), rtc.Reg(5), rtc.Reg(4));
    n["rtc"] = std::move(r);
    return n;
}
}  // namespace DeviceState
