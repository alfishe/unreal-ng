#include "stdafx.h"

#include "emulator/cpu/cpu.h"
#include "emulator/cpu/cpulogic.h"
#include "emulator/cpu/cputables.h"
#include "emulator/cpu/z80.h"
#include "op_noprefix.h"
#include "op_fd.h"

// FD prefix opcodes

Z80OPCODE opy_09(Z80 *cpu) { // add iy,bc
   cpu->memptr = cpu->iy + 1;

   int iy = cpu->iy & 0xFFFF;
   int bc = cpu->bc & 0xFFFF;

   int halfIY = iy & 0x0FFF;
   int halfBC = bc & 0x0FFF;

   // Clear flags
   cpu->f = (cpu->f & ~(NF | CF | F5 | F3 | HF));

    // Set half-carry flag (HF)
   cpu->f |= ((halfIY + halfBC) >> 8) & 0x10; // HF

   // Do add
   iy = iy + bc;

   if (iy & 0x10000)
	   cpu->f |= CF;

   // Store result to registers
   cpu->iy = iy & 0xFFFF;
   cpu->f |= (cpu->yh & (F5 | F3));

   cputact(7);
}

Z80OPCODE opy_19(Z80 *cpu) { // add iy,de
    cpu->memptr = cpu->iy + 1;

    int iy = cpu->iy & 0xFFFF;
    int de = cpu->de & 0xFFFF;

    int halfIY = iy & 0x0FFF;
    int halfDE = de & 0x0FFF;

    // Clear flags
    cpu->f = (cpu->f & ~(NF | CF | F5 | F3 | HF));

    // Set half-carry flag (HF)
    cpu->f |= ((halfIY + halfDE) >> 8) & 0x10; // HF

    // Do add
    iy = iy + de;

    if (iy & 0x10000)
        cpu->f |= CF;

    // Store result to registers
    cpu->iy = iy & 0xFFFFF;
    cpu->f |= (cpu->yh & (F5 | F3));

    cputact(7);
}

Z80OPCODE opy_21(Z80 *cpu) { // ld iy,nnnn
   cpu->yl = cpu->rd(cpu->pc++);
   cpu->yh = cpu->rd(cpu->pc++);
}

Z80OPCODE opy_22(Z80 *cpu) { // ld (nnnn),iy
    uint16_t addr = cpu->rd(cpu->pc++);
    addr += cpu->rd(cpu->pc++) * 0x100;

    cpu->memptr = addr + 1;

    cpu->wd(addr, cpu->yl);
    cpu->wd(addr + 1, cpu->yh);
}

Z80OPCODE opy_23(Z80 *cpu) { // inc iy
   cpu->iy++;
   cputact(2);
}

Z80OPCODE opy_24(Z80 *cpu) { // inc yh
   inc8(cpu, cpu->yh);
}

Z80OPCODE opy_25(Z80 *cpu) { // dec yh
   dec8(cpu, cpu->yh);
}

Z80OPCODE opy_26(Z80 *cpu) { // ld yh,nn
   cpu->yh = cpu->rd(cpu->pc++);
}

Z80OPCODE opy_29(Z80 *cpu) { // add iy,iy
    cpu->memptr = cpu->iy + 1;

    int iy = cpu->iy & 0xFFFF;

    // Clear flags
    cpu->f = (cpu->f & ~(NF | CF | F5 | F3 | HF));

    // Set half-carry flag (HF)
    cpu->f |= ((iy >> 7) & 0x10); // HF

    // Do add
    iy = iy+ iy;

    if (iy & 0x10000)
        cpu->f |= CF;

    // Store result to registers
    cpu->iy = iy & 0xFFFF;
    cpu->f |= (cpu->yh & (F5 | F3));

    cputact(7);
}

Z80OPCODE opy_2A(Z80 *cpu) { // ld iy,(nnnn)
    uint16_t addr = cpu->rd(cpu->pc++);
    addr += cpu->rd(cpu->pc++) * 0x100;

    cpu->memptr = addr + 1;

    cpu->yl = cpu->rd(addr);
    cpu->yh = cpu->rd(addr + 1);
}

Z80OPCODE opy_2B(Z80 *cpu) { // dec iy
   cpu->iy--;

   cputact(2);
}

Z80OPCODE opy_2C(Z80 *cpu) { // inc yl
   inc8(cpu, cpu->yl);
}

Z80OPCODE opy_2D(Z80 *cpu) { // dec yl
   dec8(cpu, cpu->yl);
}

Z80OPCODE opy_2E(Z80 *cpu) { // ld yl,nn
   cpu->yl = cpu->rd(cpu->pc++);
}

Z80OPCODE opy_34(Z80 *cpu) { // inc (iy+nn)
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   uint8_t value = cpu->rd(cpu->iy + offset);
   inc8(cpu, value);

   cputact(1);

   cpu->wd(cpu->iy + offset, value);
}

Z80OPCODE opy_35(Z80 *cpu) { // dec (iy+nn)
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   uint8_t value = cpu->rd(cpu->iy + offset);
   dec8(cpu, value);

   cputact(1);

   cpu->wd(cpu->iy + offset, value);
}

Z80OPCODE opy_36(Z80 *cpu) { // ld (iy+nn),nn
   int8_t offset = cpu->rd(cpu->pc++);

   uint8_t value = cpu->rd(cpu->pc++);

   cputact(2);

   cpu->wd(cpu->iy + offset, value);
}

Z80OPCODE opy_39(Z80 *cpu) { // add iy,sp
   cpu->memptr = cpu->iy + 1;

   int iy = cpu->iy & 0xFFFF;
   int sp = cpu->sp & 0xFFFF;

   int halfIY = iy & 0x0FFF;
   int halfSP = sp & 0x0FFF;

   // Clear flags
   cpu->f = (cpu->f & ~(NF | CF | F5 | F3 | HF));

    // Set half-carry flag (HF)
   cpu->f |= ((halfIY + halfSP) >> 8) & 0x10; // HF

   // Do add
   iy = iy + sp;

   if (iy & 0x10000)
	   cpu->f |= CF;

   // Store result to registers
   cpu->iy = iy & 0xFFFF;
   cpu->f |= (cpu->yh & (F5 | F3));

   cputact(7);
}

Z80OPCODE opy_44(Z80 *cpu) { // ld b,yh
   cpu->b = cpu->yh;
}

Z80OPCODE opy_45(Z80 *cpu) { // ld b,yl
   cpu->b = cpu->yl;
}

Z80OPCODE opy_46(Z80 *cpu) { // ld b,(iy+nn)
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   cpu->b = cpu->rd(cpu->iy + offset);
}

Z80OPCODE opy_4C(Z80 *cpu) { // ld c,yh
   cpu->c = cpu->yh;
}

Z80OPCODE opy_4D(Z80 *cpu) { // ld c,yl
   cpu->c = cpu->yl;
}

Z80OPCODE opy_4E(Z80 *cpu) { // ld c,(iy+nn)
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   cpu->c = cpu->rd(cpu->iy + offset);
}

Z80OPCODE opy_54(Z80 *cpu) { // ld d,yh
   cpu->d = cpu->yh;
}

Z80OPCODE opy_55(Z80 *cpu) { // ld d,yl
   cpu->d = cpu->yl;
}

Z80OPCODE opy_56(Z80 *cpu) { // ld d,(iy+nn)
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   cpu->d = cpu->rd(cpu->iy + offset);
}

Z80OPCODE opy_5C(Z80 *cpu) { // ld e,yh
   cpu->e = cpu->yh;
}
Z80OPCODE opy_5D(Z80 *cpu) { // ld e,yl
   cpu->e = cpu->yl;
}

Z80OPCODE opy_5E(Z80 *cpu) { // ld e,(iy+nn)
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   cpu->e = cpu->rd(cpu->iy + offset);
}

Z80OPCODE opy_60(Z80 *cpu) { // ld yh,b
   cpu->yh = cpu->b;
}

Z80OPCODE opy_61(Z80 *cpu) { // ld yh,c
   cpu->yh = cpu->c;
}

Z80OPCODE opy_62(Z80 *cpu) { // ld yh,d
   cpu->yh = cpu->d;
}

Z80OPCODE opy_63(Z80 *cpu) { // ld yh,e
   cpu->yh = cpu->e;
}

Z80OPCODE opy_65(Z80 *cpu) { // ld yh,yl
   cpu->yh = cpu->yl;
}

Z80OPCODE opy_66(Z80 *cpu) { // ld h,(iy+nn)
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   cpu->h = cpu->rd(cpu->iy + offset);
}

Z80OPCODE opy_67(Z80 *cpu) { // ld yh,a
   cpu->yh = cpu->a;
}

Z80OPCODE opy_68(Z80 *cpu) { // ld yl,b
   cpu->yl = cpu->b;
}

Z80OPCODE opy_69(Z80 *cpu) { // ld yl,c
   cpu->yl = cpu->c;
}

Z80OPCODE opy_6A(Z80 *cpu) { // ld yl,d
   cpu->yl = cpu->d;
}

Z80OPCODE opy_6B(Z80 *cpu) { // ld yl,e
   cpu->yl = cpu->e;
}

Z80OPCODE opy_6C(Z80 *cpu) { // ld yl,yh
   cpu->yl = cpu->yh;
}

Z80OPCODE opy_6E(Z80 *cpu) { // ld l,(iy+nn)
   int8_t ofset = cpu->rd(cpu->pc++);

   cputact(5);

   cpu->l = cpu->rd(cpu->iy + ofset);
}

Z80OPCODE opy_6F(Z80 *cpu) { // ld yl,a
   cpu->yl = cpu->a;
}

Z80OPCODE opy_70(Z80 *cpu) { // ld (iy+nn),b
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   cpu->wd(cpu->iy + offset, cpu->b);
}

Z80OPCODE opy_71(Z80 *cpu) { // ld (iy+nn),c
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   cpu->wd(cpu->iy + offset, cpu->c);
}

Z80OPCODE opy_72(Z80 *cpu) { // ld (iy+nn),d
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   cpu->wd(cpu->iy + offset, cpu->d);
}

Z80OPCODE opy_73(Z80 *cpu) { // ld (iy+nn),e
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   cpu->wd(cpu->iy + offset, cpu->e);
}

Z80OPCODE opy_74(Z80 *cpu) { // ld (iy+nn),h
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   cpu->wd(cpu->iy + offset, cpu->h);
}

Z80OPCODE opy_75(Z80 *cpu) { // ld (iy+nn),l
    int8_t offset = cpu->rd(cpu->pc++);

    cputact(5);

    cpu->wd(cpu->iy + offset, cpu->l);
}

Z80OPCODE opy_77(Z80 *cpu) { // ld (iy+nn),a
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   cpu->wd(cpu->iy + offset, cpu->a);
}

Z80OPCODE opy_7C(Z80 *cpu) { // ld a,yh
   cpu->a = cpu->yh;
}

Z80OPCODE opy_7D(Z80 *cpu) { // ld a,yl
   cpu->a = cpu->yl;
}

Z80OPCODE opy_7E(Z80 *cpu) { // ld a,(iy+nn)
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   cpu->a = cpu->rd(cpu->iy + offset);
}

Z80OPCODE opy_84(Z80 *cpu) { // add a,yh
   add8(cpu, cpu->yh);
}

Z80OPCODE opy_85(Z80 *cpu) { // add a,yl
   add8(cpu, cpu->yl);
}

Z80OPCODE opy_86(Z80 *cpu) { // add a,(iy+nn)
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   add8(cpu, cpu->rd(cpu->iy + offset));
}

Z80OPCODE opy_8C(Z80 *cpu) { // adc a,yh
   adc8(cpu, cpu->yh);
}

Z80OPCODE opy_8D(Z80 *cpu) { // adc a,yl
   adc8(cpu, cpu->yl);
}

Z80OPCODE opy_8E(Z80 *cpu) { // adc a,(iy+nn)
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   adc8(cpu, cpu->rd(cpu->iy + offset));
}

Z80OPCODE opy_94(Z80 *cpu) { // sub yh
   sub8(cpu, cpu->yh);
}

Z80OPCODE opy_95(Z80 *cpu) { // sub yl
   sub8(cpu, cpu->yl);
}

Z80OPCODE opy_96(Z80 *cpu) { // sub (iy+nn)
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   sub8(cpu, cpu->rd(cpu->iy + offset));
}

Z80OPCODE opy_9C(Z80 *cpu) { // sbc a,yh
   sbc8(cpu, cpu->yh);
}

Z80OPCODE opy_9D(Z80 *cpu) { // sbc a,yl
   sbc8(cpu, cpu->yl);
}

Z80OPCODE opy_9E(Z80 *cpu) { // sbc a,(iy+nn)
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   sbc8(cpu, cpu->rd(cpu->iy + offset));
}

Z80OPCODE opy_A4(Z80 *cpu) { // and yh
   and8(cpu, cpu->yh);
}

Z80OPCODE opy_A5(Z80 *cpu) { // and yl
   and8(cpu, cpu->yl);
}

Z80OPCODE opy_A6(Z80 *cpu) { // and (iy+nn)
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   and8(cpu, cpu->rd(cpu->iy + offset));
}

Z80OPCODE opy_AC(Z80 *cpu) { // xor yh
   xor8(cpu, cpu->yh);
}

Z80OPCODE opy_AD(Z80 *cpu) { // xor yl
   xor8(cpu, cpu->yl);
}

Z80OPCODE opy_AE(Z80 *cpu) { // xor (iy+nn)
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   xor8(cpu, cpu->rd(cpu->iy + offset));
}

Z80OPCODE opy_B4(Z80 *cpu) { // or yh
   or8(cpu, cpu->yh);
}

Z80OPCODE opy_B5(Z80 *cpu) { // or yl
   or8(cpu, cpu->yl);
}

Z80OPCODE opy_B6(Z80 *cpu) { // or (iy+nn)
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   or8(cpu, cpu->rd(cpu->iy + offset));
}

Z80OPCODE opy_BC(Z80 *cpu) { // cp yh
   cp8(cpu, cpu->yh);
}

Z80OPCODE opy_BD(Z80 *cpu) { // cp yl
   cp8(cpu, cpu->yl);
}

Z80OPCODE opy_BE(Z80 *cpu) { // cp (iy+nn)
   int8_t offset = cpu->rd(cpu->pc++);

   cputact(5);

   cp8(cpu, cpu->rd(cpu->iy + offset));
}

Z80OPCODE opy_E1(Z80 *cpu) { // pop iy
   cpu->yl = cpu->rd(cpu->sp++);
   cpu->yh = cpu->rd(cpu->sp++);
}

Z80OPCODE opy_E3(Z80 *cpu) { // ex (sp),iy
   uint16_t value = cpu->rd(cpu->sp) + 0x100 * cpu->rd(cpu->sp + 1);
   cputact(1);

   cpu->wd(cpu->sp, cpu->yl);
   cpu->wd(cpu->sp + 1, cpu->yh);

   cpu->memptr = value;

   cpu->iy = value;

   cputact(2);
}

Z80OPCODE opy_E5(Z80 *cpu) { // push iy
   cputact(1);

   cpu->wd(--cpu->sp, cpu->yh);
   cpu->wd(--cpu->sp, cpu->yl);
}

Z80OPCODE opy_E9(Z80 *cpu) { // jp (iy)
   cpu->last_branch = cpu->pc - 2;
   cpu->pc = cpu->iy;
}

Z80OPCODE opy_F9(Z80 *cpu) { // ld sp,iy
   cpu->sp = cpu->iy;

   cputact(2);
}

STEPFUNC const iy_opcode[0x100] =
{
    op_00,  op_01,  op_02,  op_03,  op_04,  op_05,  op_06,  op_07,
    op_08, opy_09,  op_0A,  op_0B,  op_0C,  op_0D,  op_0E,  op_0F,
    op_10,  op_11,  op_12,  op_13,  op_14,  op_15,  op_16,  op_17,
    op_18, opy_19,  op_1A,  op_1B,  op_1C,  op_1D,  op_1E,  op_1F,
    op_20, opy_21, opy_22, opy_23, opy_24, opy_25, opy_26,  op_27,
    op_28, opy_29, opy_2A, opy_2B, opy_2C, opy_2D, opy_2E,  op_2F,
    op_30,  op_31,  op_32,  op_33, opy_34, opy_35, opy_36,  op_37,
    op_38, opy_39,  op_3A,  op_3B,  op_3C,  op_3D,  op_3E,  op_3F,

    op_40,  op_41,  op_42,  op_43, opy_44, opy_45, opy_46,  op_47,
    op_48,  op_49,  op_4A,  op_4B, opy_4C, opy_4D, opy_4E,  op_4F,
    op_50,  op_51,  op_52,  op_53, opy_54, opy_55, opy_56,  op_57,
    op_58,  op_59,  op_5A,  op_5B, opy_5C, opy_5D, opy_5E,  op_5F,
   opy_60, opy_61, opy_62, opy_63,  op_64, opy_65, opy_66, opy_67,
   opy_68, opy_69, opy_6A, opy_6B, opy_6C,  op_6D, opy_6E, opy_6F,
   opy_70, opy_71, opy_72, opy_73, opy_74, opy_75,  op_76, opy_77,
    op_78,  op_79,  op_7A,  op_7B, opy_7C, opy_7D, opy_7E,  op_7F,

    op_80,  op_81,  op_82,  op_83, opy_84, opy_85, opy_86,  op_87,
    op_88,  op_89,  op_8A,  op_8B, opy_8C, opy_8D, opy_8E,  op_8F,
    op_90,  op_91,  op_92,  op_93, opy_94, opy_95, opy_96,  op_97,
    op_98,  op_99,  op_9A,  op_9B, opy_9C, opy_9D, opy_9E,  op_9F,
    op_A0,  op_A1,  op_A2,  op_A3, opy_A4, opy_A5, opy_A6,  op_A7,
    op_A8,  op_A9,  op_AA,  op_AB, opy_AC, opy_AD, opy_AE,  op_AF,
    op_B0,  op_B1,  op_B2,  op_B3, opy_B4, opy_B5, opy_B6,  op_B7,
    op_B8,  op_B9,  op_BA,  op_BB, opy_BC, opy_BD, opy_BE,  op_BF,

    op_C0,  op_C1,  op_C2,  op_C3,  op_C4,  op_C5,  op_C6,  op_C7,
    op_C8,  op_C9,  op_CA,  op_CB,  op_CC,  op_CD,  op_CE,  op_CF,
    op_D0,  op_D1,  op_D2,  op_D3,  op_D4,  op_D5,  op_D6,  op_D7,
    op_D8,  op_D9,  op_DA,  op_DB,  op_DC,  op_DD,  op_DE,  op_DF,
    op_E0, opy_E1,  op_E2, opy_E3,  op_E4, opy_E5,  op_E6,  op_E7,
    op_E8, opy_E9,  op_EA,  op_EB,  op_EC,  op_ED,  op_EE,  op_EF,
    op_F0,  op_F1,  op_F2,  op_F3,  op_F4,  op_F5,  op_F6,  op_F7,
    op_F8, opy_F9,  op_FA,  op_FB,  op_FC,  op_FD,  op_FE,  op_FF,
};
