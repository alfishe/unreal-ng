#pragma once

/// @file gscpuregisters.h
/// @brief GSCpuRegister -> unreal-z80 register read, shared by the cards that
/// run a real Z80 (classic GS, NeoGS)

#include <cstdint>

#include "3rdparty/unreal-z80/z80cpu.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"

inline uint16_t gsReadCpuRegister(const Z80CPU* cpu, GSCpuRegister reg)
{
    if (!cpu)
        return 0;
    switch (reg)
    {
        case GSCpuRegister::AF: return Z80CpuGetReg(cpu, Z80CpuRegAf);
        case GSCpuRegister::BC: return Z80CpuGetReg(cpu, Z80CpuRegBc);
        case GSCpuRegister::DE: return Z80CpuGetReg(cpu, Z80CpuRegDe);
        case GSCpuRegister::HL: return Z80CpuGetReg(cpu, Z80CpuRegHl);
        case GSCpuRegister::AFAlt: return Z80CpuGetReg(cpu, Z80CpuRegAfAlt);
        case GSCpuRegister::BCAlt: return Z80CpuGetReg(cpu, Z80CpuRegBcAlt);
        case GSCpuRegister::DEAlt: return Z80CpuGetReg(cpu, Z80CpuRegDeAlt);
        case GSCpuRegister::HLAlt: return Z80CpuGetReg(cpu, Z80CpuRegHlAlt);
        case GSCpuRegister::IX: return Z80CpuGetReg(cpu, Z80CpuRegIx);
        case GSCpuRegister::IY: return Z80CpuGetReg(cpu, Z80CpuRegIy);
        case GSCpuRegister::SP: return Z80CpuGetReg(cpu, Z80CpuRegSp);
        case GSCpuRegister::PC: return Z80CpuGetReg(cpu, Z80CpuRegPc);
        case GSCpuRegister::I: return Z80CpuGetReg(cpu, Z80CpuRegI);
        case GSCpuRegister::R: return Z80CpuGetReg(cpu, Z80CpuRegR);
        case GSCpuRegister::IM: return Z80CpuGetReg(cpu, Z80CpuRegIm);
        case GSCpuRegister::IFF1: return Z80CpuGetReg(cpu, Z80CpuRegIff1);
        case GSCpuRegister::IFF2: return Z80CpuGetReg(cpu, Z80CpuRegIff2);
        case GSCpuRegister::MEMPTR: return Z80CpuGetReg(cpu, Z80CpuRegMemptr);
    }
    return 0;
}
