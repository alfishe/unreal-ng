#pragma once
#include "stdafx.h"

void set_mode(ROM_MODE mode);
void set_banks();
void set_scorp_profrom(unsigned read_address);
void cmos_write(uint8_t val);
uint8_t cmos_read();
void rand_ram();

/*
Z80INLINE uint8_t *am_r(unsigned addr)
{
#ifdef MOD_VID_VD
   if (comp.vdbase && (unsigned)((addr & 0xFFFF) - 0x4000) < 0x1800) return comp.vdbase + (addr & 0x1FFF);
#endif
   // return bankr[(addr >> 14) & 3] + (addr & (PAGE-1));
}
//u8 rmdbg(u32 addr);
//void wmdbg(u32 addr, u8 val);
//u8 *MemDbg(u32 addr);
*/

enum BANKM
{
    BANKM_ROM = 0,
    BANKM_RAM
};
