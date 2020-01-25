#pragma once
#include "stdafx.h"

#include "emulator/cpu/cpu.h"

class Z80;

// Aliases for Z80 logic functions
#if defined (_MSC_VER)
	#define Z80FAST __fastcall		// Try to keep function parameters in x86/x64 CPU registers. Not guaranteed for all compilers
#else
	#define Z80FAST
#endif
#define Z80INLINE __forceinline // Time-critical inlines. Uses cross-compiler __forceinline macro from stdafx.h
#define Z80LOGIC uint8_t Z80FAST
#define Z80OPCODE void Z80FAST

// Z80 opcode callback types
typedef void (Z80FAST * STEPFUNC)(Z80*);
typedef uint8_t(Z80FAST * LOGICFUNC)(Z80*, uint8_t byte);

// Operations decoder / micrologic helpers
#define cputact(a)	cpu->tt += ((a) * cpu->rate); cpu->cycle_count += (a)
#define turbo(a)	cpu.rate = (256/(a))


// Aliases to access CPU tables
// Definitions done in cpulogic.cpp
extern uint8_t (&log_f)[0x100];
extern uint8_t const (&inc_f)[0x100];
extern uint8_t const (&dec_f)[0x100];
extern uint8_t (&adc_f)[0x20000];
extern uint8_t (&sbc_f)[0x20000];
extern uint8_t (&cp_f)[65536];
extern uint8_t const (&rlc_f)[0x100];
extern uint8_t const (&rrc_f)[0x100];
extern uint8_t (&rlca_f)[0x100];
extern uint8_t (&rrca_f)[0x100];
extern uint8_t const (&sra_f)[0x100];

extern uint8_t (&rol)[0x100];
extern uint8_t (&ror)[0x100];
extern uint8_t const (&rl0)[0x100];
extern uint8_t const (&rl1)[0x100];
extern uint8_t const (&rr0)[0x100];
extern uint8_t const (&rr1)[0x100];
extern uint8_t (&cpf8b)[0x10000];

//auto& inc_f = CPU::_cpuTables.increment_flags;
//auto& dec_f = CPU::_cpuTables.decrement_flags;
//auto& adc_f = CPU::_cpuTables.add_flags;
//auto& sbc_f = CPU::_cpuTables.sub_flags;
//auto& cp_f = CPU::_cpuTables.cp_flags;
//auto& rlc_f = CPU::_cpuTables.rlc_flags;
//auto& rrc_f = CPU::_cpuTables.rrc_flags;
//auto& rlca_f = CPU::_cpuTables.rlca_flags;
//auto& rrca_f = CPU::_cpuTables.rrca_flags;
//auto& sra_f = CPU::_cpuTables.sra_flags;
//
//auto& rol = CPU::_cpuTables.rol_table;
//auto& ror = CPU::_cpuTables.ror_table;
//auto& rl0 = CPU::_cpuTables.rl0_table;
//auto& rl1 = CPU::_cpuTables.rl1_table;
//auto& rr0 = CPU::_cpuTables.rr0_table;
//auto& rr1 = CPU::_cpuTables.rr1_table;
//auto& cpf8b = CPU::_cpuTables.cpf8b;

//#define log_f CPU::_cpuTables.logic_flags
//#define inc_f CPU::_cpuTables.increment_flags
//#define dec_f CPU::_cpuTables.decrement_flags
//#define adc_f CPU::_cpuTables.add_flags
//#define sbc_f CPU::_cpuTables.sub_flags
//#define cp_f CPU::_cpuTables.cp_flags
//#define rlc_f CPU::_cpuTables.rlc_flags
//#define rrc_f CPU::_cpuTables.rrc_flags
//#define rlca_f CPU::_cpuTables.rlca_flags
//#define rrca_f CPU::_cpuTables.rrca_flags
//
//#define rol CPU::_cpuTables.rol_table
//#define ror CPU::_cpuTables.ror_table
//#define rl0 CPU::_cpuTables.rl0_table
//#define rl1 CPU::_cpuTables.rl1_table
//#define rr0 CPU::_cpuTables.rr0_table
//#define rr1 CPU::_cpuTables.rr1_table

// Z80 CPU flag register bits
#define CF 0x01			// Bit 0 - Carry Flag
#define NF 0x02			// Bit 1 - Add/Subtract
#define PV 0x04			// Bit 2 - Parity/Overflow
#define F3 0x08			// Bit 3 - Not used
#define HF 0x10			// Bit 4 - Half Carry flag
#define F5 0x20			// Bit 5 - Not used
#define ZF 0x40			// Bit 6 - Zero flag
#define SF 0x80			// Bit 7 - Sign flag


//
// Inlined microcode operations to be used from opcode handlers
//
class CPULogic
{
public:
	static Z80INLINE void and8(Z80 *cpu, uint8_t src);
	static Z80INLINE void or8(Z80* cpu, uint8_t src);
	static Z80INLINE void xor8(Z80* cpu, uint8_t src);
	static Z80INLINE void bitmem(Z80 *cpu, uint8_t src, uint8_t bit);
	static Z80INLINE void set(uint8_t& src, uint8_t bit);
	static Z80INLINE void res(uint8_t& src, uint8_t bit);
	static Z80INLINE void bit(Z80 *cpu, uint8_t src, uint8_t bit);
	static Z80INLINE uint8_t resbyte(uint8_t src, uint8_t bit);
	static Z80INLINE uint8_t setbyte(uint8_t src, uint8_t bit);
	static Z80INLINE void inc8(Z80 *cpu, uint8_t &x);
	static Z80INLINE void dec8(Z80 *cpu, uint8_t &x);
	static Z80INLINE void add8(Z80 *cpu, uint8_t src);
	static Z80INLINE void sub8(Z80 *cpu, uint8_t src);
	static Z80INLINE void adc8(Z80 *cpu, uint8_t src);
	static Z80INLINE void sbc8(Z80 *cpu, uint8_t src);
	static Z80INLINE void  cp8(Z80 *cpu, uint8_t src);
};

// Aliases from static class methods to globally available functions
// For compatibility with obsolete Z80 opcode execution logic
// Requires C++ 11 compiler
// Definitions done in cpulogic.cpp
extern void (&and8)(Z80* cpu, uint8_t src);
extern void (&or8)(Z80* cpu, uint8_t src);
extern void (&xor8)(Z80* cpu, uint8_t src);
extern void (&bitmem)(Z80* cpu, uint8_t src, uint8_t bit);
extern void (&set)(uint8_t& src, uint8_t bit);
extern void (&res)(uint8_t& src, uint8_t bit);
extern void (&bit)(Z80* cpu, uint8_t src, uint8_t bit);
extern uint8_t (&resbyte)(uint8_t src, uint8_t bit);
extern uint8_t (&setbyte)(uint8_t src, uint8_t bit);
extern void (&inc8)(Z80 *cpu, uint8_t &x);
extern void (&dec8)(Z80 *cpu, uint8_t &x);
extern void (&add8)(Z80* cpu, uint8_t src);
extern void (&sub8)(Z80* cpu, uint8_t src);
extern void (&adc8)(Z80* cpu, uint8_t src);
extern void (&sbc8)(Z80* cpu, uint8_t src);
extern void (&cp8)(Z80* cpu, uint8_t src);
