#include "chipwritehistory.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/slots/portowners.h"
#include "emulator/state/statenode.h"

std::string ChipHistoryDevice(const EmulatorContext* context, uint16_t port, const char* fallback)
{
    const std::string owner = slots::PortOwners::Of(context).Find(port, true, false);  // a copy: the owners are a temporary
    return owner.empty() ? std::string(fallback) : owner;
}

history::Registry* ChipHistoryRegistry(EmulatorContext* context)
{
    return context != nullptr ? &context->histories : nullptr;
}

void RecordChipWrite(const EmulatorContext* context, ChipWriteHistory& history, SoundChipKind kind, uint8_t unit,
                     uint16_t reg, uint8_t value)
{
    if (context != nullptr && context->ttdReplayActive)
        return;
    ChipWriteRecord record;
    record.kind = kind;
    record.unit = unit;
    record.reg = reg;
    record.value = value;
    if (context != nullptr && context->pCore != nullptr)
    {
        if (const Z80* z80 = context->pCore->GetZ80())
        {
            record.pc = z80->m1_pc;  // PC of the OUT instruction, not the next one
            record.tacts = static_cast<uint16_t>(z80->t);
        }
        record.frame = context->emulatorState.frame_counter;
    }
    history->Push(record);
}

void ChipWriteRecord::ToState(StateNode& node) const
{
    node["frame"] = int64_t(frame);
    node["tacts"] = int(tacts);
    node["pc"] = int(pc);
    node["chip"] = SoundChipKindName(kind);
    node["unit"] = int(unit);
    node["reg"] = int(reg);
    node["value"] = int(value);
}
