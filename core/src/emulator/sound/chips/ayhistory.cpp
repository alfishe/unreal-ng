#include "ayhistory.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/slots/portowners.h"

std::string AYHistoryDevice(const EmulatorContext* context)
{
    const std::string owner = slots::PortOwners::Of(context).Find(0xFFFD, true, false);  // a copy: the owners are a temporary
    return owner.empty() ? std::string("ay") : owner;
}

bool AYHistoryDeclared(const EmulatorContext* context)
{
    return !slots::PortOwners::Of(context).Find(0xFFFD, true, false).empty();
}

void RecordAYWrite(const EmulatorContext* context, AYHistory& history, uint16_t port, uint8_t value, uint8_t chip,
                   uint8_t reg)
{
    if (context != nullptr && context->ttdReplayActive)
        return;
    AYLogRecord record;
    record.port = port;
    record.value = value;
    record.chip = chip;
    record.reg = reg;
    if (context != nullptr && context->pCore != nullptr)
    {
        if (const Z80* z80 = context->pCore->GetZ80())
        {
            record.pc = z80->m1_pc;  // PC of the OUT instruction, not the next one
            record.tacts = static_cast<uint16_t>(z80->t);
        }
        record.frame = context->emulatorState.frame_counter;
    }
    history.Push(record);
}

void AYLogRecord::ToState(StateNode& node) const
{
    node["frame"] = int64_t(frame);
    node["tacts"] = int(tacts);
    node["pc"] = int(pc);
    node["port"] = int(port);
    node["chip"] = int(chip);
    node["type"] = port == 0xFFFD ? (value > 0x0F ? "switch" : "select") : "write";
    node["reg"] = int(reg);
    if (reg < 16)
        node["reg_name"] = SoundChip_AY8910::AYRegisterNames[reg];
    node["value"] = int(value);
}
