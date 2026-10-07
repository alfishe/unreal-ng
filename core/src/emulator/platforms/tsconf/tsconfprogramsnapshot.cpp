#include "tsconfprogramsnapshot.h"

#include <cstring>

#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_tsconf.h"
#include "loaders/snapshot/loaderspg.h"

namespace
{
using snapshot::Outcome;
using snapshot::Verdict;

PortDecoder_TSConf* DecoderOf(EmulatorContext& context) { return dynamic_cast<PortDecoder_TSConf*>(context.pPortDecoder); }

const snapshot::Extension* HeaderOf(const snapshot::Image& image)
{
    for (const snapshot::Extension& extension : image.extensions)
    {
        if (extension.origin == LoaderSPG::kHeaderOrigin && extension.payload.size() >= 2)
            return &extension;
    }
    return nullptr;
}

constexpr size_t kRamSize = 4u * 1024 * 1024;
}  // namespace

TsConfProgramSnapshot& TsConfProgramSnapshot::Instance()
{
    static TsConfProgramSnapshot instance;
    return instance;
}

Verdict TsConfProgramSnapshot::Examine(const snapshot::Image& image, EmulatorContext& context) const
{
    if (image.memoryModel != snapshot::MemoryModel::Physical)
        return Verdict::Decline();   // a Spectrum snapshot: the shared fit check and the legacy commit decide
    if (!DecoderOf(context) || !context.pCore || !context.pMemory)
        return Verdict::Refuse("an SPG program runs on the TS-Conf machine (model TSL) only", "model:TSL");
    if (!HeaderOf(image))
        return Verdict::Refuse("the program image has no TS-Conf header (page at #C000, clock)", "format:incomplete");
    for (const snapshot::PhysicalRun& run : image.physical)
    {
        if (run.address >= kRamSize || run.data.size() > kRamSize - run.address)
            return Verdict::Refuse("a block of the program lies outside the 4 MB RAM", "format:unsupported");
    }
    return Verdict::Take("a physically addressed program on the TS-Conf machine");
}

bool TsConfProgramSnapshot::Commit(const snapshot::Image& image, EmulatorContext& context, snapshot::Report& report)
{
    PortDecoder_TSConf* decoder = DecoderOf(context);
    if (!decoder || !context.pCore || !context.pMemory)
    {
        report.Refuse("an SPG program runs on the TS-Conf machine (model TSL) only", "model:TSL");
        return false;
    }
    const snapshot::Extension* header = HeaderOf(image);
    if (!header)
    {
        report.Refuse("the program image has no TS-Conf header (page at #C000, clock)", "format:incomplete");
        return false;
    }
    const uint8_t page3 = header->payload[0];
    const uint8_t clock = header->payload[1] & 0x03;

    context.pCore->Reset();
    // A program is started by the shell (Wild Commander), which leaves the SD card
    // initialized and idle. The reset above keeps the card's state (its power is
    // not cut), so a load that lands while the TS-BIOS is initializing or streaming
    // from the card (it boots from SD by default) would hand the program a card
    // mid-transfer or not initialized, which its driver does not expect
    decoder->GetSdCard().LeaveForProgram();

    // The memory map a TS-Conf SPG expects: BASIC-48 at #0000, RAM 5 / 2 / page 3
    decoder->WriteRegister(TsConfReg::MemConfig, 0x01);
    decoder->WriteRegister(TsConfReg::Page1, 0x05);
    decoder->WriteRegister(TsConfReg::Page2, 0x02);
    decoder->WriteRegister(TsConfReg::Page3, page3);
    decoder->WriteRegister(TsConfReg::SysConfig, clock);
    context.emulatorState.p7FFD = image.paging.p7FFD.value_or(0x10);
    report.Add("registers", Outcome::Applied,
               "RAM 5 / 2 / " + std::to_string(page3) + ", SYS_CONFIG[1:0] = " + std::to_string(clock) + ", BASIC-48 ROM at #0000");

    uint8_t* ram = context.pMemory->RAMBase();
    size_t bytes = 0;
    for (const snapshot::PhysicalRun& run : image.physical)
    {
        std::memcpy(ram + run.address, run.data.data(), run.data.size());
        bytes += run.data.size();
    }
    report.Add("blocks", Outcome::Applied, std::to_string(image.physical.size()) + " blocks, " + std::to_string(bytes) + " bytes");

    const snapshot::Cpu& cpu = image.cpu;
    Z80& z80 = *context.pCore->GetZ80();
    z80.iy = cpu.iy;
    z80.alt.hl = cpu.hl2;
    z80.i = cpu.i;
    z80.im = cpu.im;
    z80.sp = cpu.sp;
    z80.pc = cpu.pc;
    z80.iff1 = cpu.iff1 ? 1 : 0;
    z80.iff2 = cpu.iff2 ? 1 : 0;
    report.Add("CPU", Outcome::Applied,
               "PC " + StringHelper::Format("#%04X", cpu.pc) + ", SP " + StringHelper::Format("#%04X", cpu.sp));
    return true;
}
