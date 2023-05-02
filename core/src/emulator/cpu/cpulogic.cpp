#include "stdafx.h"

#include "cpulogic.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"

// Aliases to access Core tables
uint8_t (&log_f)[0x100]			= Core::_cpuTables.logic_flags;
uint8_t const (&inc_f)[0x100]	= Core::_cpuTables.increment_flags;
uint8_t const (&dec_f)[0x100]	= Core::_cpuTables.decrement_flags;
uint8_t (&adc_f)[0x20000]		= Core::_cpuTables.add_flags;
uint8_t (&sbc_f)[0x20000]		= Core::_cpuTables.sub_flags;
uint8_t (&cp_f)[0x10000]		= Core::_cpuTables.cp_flags;
uint8_t const (&rlc_f)[0x100]	= Core::_cpuTables.rlc_flags;
uint8_t const (&rrc_f)[0x100]	= Core::_cpuTables.rrc_flags;
uint8_t (&rlca_f)[0x100]		= Core::_cpuTables.rlca_flags;
uint8_t (&rrca_f)[0x100]		= Core::_cpuTables.rrca_flags;
uint8_t const (&sra_f)[0x100]	= Core::_cpuTables.sra_flags;

uint8_t (&rol)[0x100]			= Core::_cpuTables.rol_table;
uint8_t (&ror)[0x100]			= Core::_cpuTables.ror_table;
uint8_t const (&rl0)[0x100]		= Core::_cpuTables.rl0_table;
uint8_t const (&rl1)[0x100]		= Core::_cpuTables.rl1_table;
uint8_t const (&rr0)[0x100]		= Core::_cpuTables.rr0_table;
uint8_t const (&rr1)[0x100]		= Core::_cpuTables.rr1_table;
uint8_t (&cpf8b)[0x10000]		= Core::_cpuTables.cpf8b;

// Aliases from static class methods to globally available functions
// For compatibility with obsolete Z80 opcode execution logic
// Requires C++ 11 compiler
void (&and8)(Z80* cpu, uint8_t src) = CPULogic::and8;
void (&or8)(Z80* cpu, uint8_t src) = CPULogic::or8;
void (&xor8)(Z80* cpu, uint8_t src) = CPULogic::xor8;
void (&bitmem)(Z80* cpu, uint8_t src, uint8_t bit) = CPULogic::bitmem;
void (&op_set)(uint8_t& src, uint8_t bit) = CPULogic::set;
void (&res)(uint8_t& src, uint8_t bit) = CPULogic::res;
void (&bit)(Z80* cpu, uint8_t src, uint8_t bit) = CPULogic::bit;
uint8_t (&resbyte)(uint8_t src, uint8_t bit) = CPULogic::resbyte;
uint8_t (&setbyte)(uint8_t src, uint8_t bit) = CPULogic::setbyte;
void (&inc8)(Z80 *cpu, uint8_t &x) = CPULogic::inc8;
void (&dec8)(Z80 *cpu, uint8_t &x) = CPULogic::dec8;
void (&add8)(Z80* cpu, uint8_t src) = CPULogic::add8;
void (&sub8)(Z80* cpu, uint8_t src) = CPULogic::sub8;
void (&adc8)(Z80* cpu, uint8_t src) = CPULogic::adc8;
void (&sbc8)(Z80* cpu, uint8_t src) = CPULogic::sbc8;
void (&cp8)(Z80* cpu, uint8_t src) = CPULogic::cp8;



void CPULogic::and8(Z80 *cpu, uint8_t src)
{
	cpu->a &= src;
	cpu->f = log_f[cpu->a] | HF;
}

void CPULogic::or8(Z80 *cpu, uint8_t src)
{
	cpu->a |= src;
	cpu->f = log_f[cpu->a];
}

void CPULogic::xor8(Z80 *cpu, uint8_t src)
{
	cpu->a ^= src;
	cpu->f = log_f[cpu->a];
}

void CPULogic::bitmem(Z80 *cpu, uint8_t src, uint8_t bit)
{
	cpu->f = log_f[src & (1 << bit)] | HF | (cpu->f & CF);
	cpu->f = (cpu->f & ~(F3 | F5)) | (cpu->memh & (F3 | F5));
}

void CPULogic::set(uint8_t &src, uint8_t bit)
{
	src |= (1 << bit);
}

void CPULogic::res(uint8_t &src, uint8_t bit)
{
	src &= ~(1 << bit);
}

void CPULogic::bit(Z80 *cpu, uint8_t src, uint8_t bit)
{
	cpu->f = log_f[src & (1 << bit)] | HF | (cpu->f & CF) | (src & (F3 | F5));
}

uint8_t CPULogic::resbyte(uint8_t src, uint8_t bit)
{
	return src & ~(1 << bit);
}

uint8_t CPULogic::setbyte(uint8_t src, uint8_t bit)
{
	return src | (1 << bit);
}

void CPULogic::inc8(Z80 *cpu, uint8_t &x)
{
	cpu->f = inc_f[x] | (cpu->f & CF);
	x++;
}

void CPULogic::dec8(Z80 *cpu, uint8_t &x)
{
	cpu->f = dec_f[x] | (cpu->f & CF);
	x--;
}

void CPULogic::add8(Z80 *cpu, uint8_t src)
{
	cpu->f = adc_f[cpu->a + src * 0x100];
	cpu->a += src;
}

void CPULogic::sub8(Z80 *cpu, uint8_t src)
{
	cpu->f = sbc_f[cpu->a * 0x100 + src];
	cpu->a -= src;
}

void CPULogic::adc8(Z80 *cpu, uint8_t src)
{
	uint8_t carry = ((cpu->f) & CF);
	cpu->f = adc_f[cpu->a + src * 0x100 + 0x10000 * carry];
	cpu->a += src + carry;
}

void CPULogic::sbc8(Z80 *cpu, uint8_t src)
{
	uint8_t carry = ((cpu->f) & CF);
	cpu->f = sbc_f[cpu->a * 0x100 + src + 0x10000 * carry];
	cpu->a -= src + carry;
}

void CPULogic::cp8(Z80 *cpu, uint8_t src)
{
	cpu->f = cp_f[cpu->a * 0x100 + src];
}