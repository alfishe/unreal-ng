#include "diskfastload.h"

#include <cstring>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/spectrumconstants.h"

DiskFastLoad::DiskFastLoad(EmulatorContext* context)
    : _context(context)
{
    if (_context)
    {
        _logger = _context->pModuleLogger;
    }
}

DiskFastLoad::~DiskFastLoad()
{
    _context = nullptr;
}

bool DiskFastLoad::IsArmed() const
{
    if (!_context)
        return false;

    // FeatureManager gate (fastdisk feature toggle).
    // Note: FeatureManager::isEnabled(kFastDisk) automatically returns false
    // whenever TTD recording is active.
    FeatureManager* fm = _context->pFeatureManager;
    if (fm == nullptr || !fm->isEnabled(Features::kFastDisk))
        return false;

    // Must be in TR-DOS mode with Beta 128 interface present
    if (!(_context->emulatorState.flags & CF_TRDOS) || _context->pBetaDisk == nullptr)
        return false;

    // Bank 0 must be ROM. If it is mapped to RAM (e.g. CP/M TPA), decline.
    return _context->pMemory && _context->pMemory->GetMemoryBankMode(0) == BANK_ROM;
}

bool DiskFastLoad::IsIniAtReadLoop() const
{
    // Same gate as Unreal Speccy: only the INI opcode bytes (ED A2) at $3FEC identify the
    // sector read loop. No ROM version signature is used.
    uint8_t* rom3fec = _context->pMemory->MapZ80AddressToPhysicalAddress(0x3FEC);
    return rom3fec && rom3fec[0] == 0xED && rom3fec[1] == 0xA2;
}

bool DiskFastLoad::HandleSectorDrainTrap(Z80& cpu)
{
    if (!IsArmed() || !IsIniAtReadLoop())
        return false;

    // The FDC owns all state changes; it hands over the bytes and finishes the command
    // through its normal CRC/INTRQ path.
    bool drained = _context->pBetaDisk->drainSectorRead([&cpu](uint8_t byte) {
        cpu.wd(cpu.hl, byte);
        cpu.hl++;
        cpu.b--;
    });

    if (!drained)
        return false;

    // Flags as left by the last INI: Z = (B == 0), N = 1
    cpu.f = static_cast<uint8_t>((cpu.f & ~FLAG_ZF) | FLAG_NF | (cpu.b == 0 ? FLAG_ZF : 0));
    cpu.pc += 2;  // Skip INI opcode (0xED 0xA2)
    cpu.m1_pc = 0x3FEC;

    return true;
}
