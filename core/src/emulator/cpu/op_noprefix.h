#pragma once
#include "stdafx.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/cpulogic.h"
#include "emulator/cpu/z80.h"

#define op_40 op_00     // ld b,b
#define op_49 op_00     // ld c,c
#define op_52 op_00     // ld d,d
#define op_5B op_00     // ld e,e
#define op_64 op_00     // ld h,h
#define op_6D op_00     // ld l,l
#define op_7F op_00     // ld a,a

extern STEPFUNC const normal_opcode[];

Z80OPCODE op_00(Z80 *cpu);
Z80OPCODE op_01(Z80 *cpu);
Z80OPCODE op_02(Z80 *cpu);
Z80OPCODE op_03(Z80 *cpu);
Z80OPCODE op_04(Z80 *cpu);
Z80OPCODE op_05(Z80 *cpu);
Z80OPCODE op_06(Z80 *cpu);
Z80OPCODE op_07(Z80 *cpu);
Z80OPCODE op_08(Z80 *cpu);
Z80OPCODE op_0A(Z80 *cpu);
Z80OPCODE op_0B(Z80 *cpu);
Z80OPCODE op_0C(Z80 *cpu);
Z80OPCODE op_0D(Z80 *cpu);
Z80OPCODE op_0E(Z80 *cpu);
Z80OPCODE op_0F(Z80 *cpu);
Z80OPCODE op_10(Z80 *cpu);
Z80OPCODE op_11(Z80 *cpu);
Z80OPCODE op_12(Z80 *cpu);
Z80OPCODE op_13(Z80 *cpu);
Z80OPCODE op_14(Z80 *cpu);
Z80OPCODE op_15(Z80 *cpu);
Z80OPCODE op_16(Z80 *cpu);
Z80OPCODE op_17(Z80 *cpu);
Z80OPCODE op_18(Z80 *cpu);
Z80OPCODE op_1A(Z80 *cpu);
Z80OPCODE op_1B(Z80 *cpu);
Z80OPCODE op_1C(Z80 *cpu);
Z80OPCODE op_1D(Z80 *cpu);
Z80OPCODE op_1E(Z80 *cpu);
Z80OPCODE op_1F(Z80 *cpu);
Z80OPCODE op_20(Z80 *cpu);
Z80OPCODE op_22(Z80 *cpu);
Z80OPCODE op_27(Z80 *cpu);
Z80OPCODE op_28(Z80 *cpu);
Z80OPCODE op_2A(Z80 *cpu);
Z80OPCODE op_2F(Z80 *cpu);
Z80OPCODE op_30(Z80 *cpu);
Z80OPCODE op_31(Z80 *cpu);
Z80OPCODE op_32(Z80 *cpu);
Z80OPCODE op_33(Z80 *cpu);
Z80OPCODE op_37(Z80 *cpu);
Z80OPCODE op_38(Z80 *cpu);
Z80OPCODE op_3A(Z80 *cpu);
Z80OPCODE op_3B(Z80 *cpu);
Z80OPCODE op_3C(Z80 *cpu);
Z80OPCODE op_3D(Z80 *cpu);
Z80OPCODE op_3E(Z80 *cpu);
Z80OPCODE op_3F(Z80 *cpu);
Z80OPCODE op_41(Z80 *cpu);
Z80OPCODE op_42(Z80 *cpu);
Z80OPCODE op_43(Z80 *cpu);
Z80OPCODE op_47(Z80 *cpu);
Z80OPCODE op_48(Z80 *cpu);
Z80OPCODE op_4A(Z80 *cpu);
Z80OPCODE op_4B(Z80 *cpu);
Z80OPCODE op_4F(Z80 *cpu);
Z80OPCODE op_50(Z80 *cpu);
Z80OPCODE op_51(Z80 *cpu);
Z80OPCODE op_53(Z80 *cpu);
Z80OPCODE op_57(Z80 *cpu);
Z80OPCODE op_58(Z80 *cpu);
Z80OPCODE op_59(Z80 *cpu);
Z80OPCODE op_5A(Z80 *cpu);
Z80OPCODE op_5F(Z80 *cpu);
Z80OPCODE op_76(Z80 *cpu);
Z80OPCODE op_78(Z80 *cpu);
Z80OPCODE op_79(Z80 *cpu);
Z80OPCODE op_7A(Z80 *cpu);
Z80OPCODE op_7B(Z80 *cpu);
Z80OPCODE op_80(Z80 *cpu);
Z80OPCODE op_81(Z80 *cpu);
Z80OPCODE op_82(Z80 *cpu);
Z80OPCODE op_83(Z80 *cpu);
Z80OPCODE op_87(Z80 *cpu);
Z80OPCODE op_88(Z80 *cpu);
Z80OPCODE op_89(Z80 *cpu);
Z80OPCODE op_8A(Z80 *cpu);
Z80OPCODE op_8B(Z80 *cpu);
Z80OPCODE op_8F(Z80 *cpu);
Z80OPCODE op_90(Z80 *cpu);
Z80OPCODE op_91(Z80 *cpu);
Z80OPCODE op_92(Z80 *cpu);
Z80OPCODE op_93(Z80 *cpu);
Z80OPCODE op_97(Z80 *cpu);
Z80OPCODE op_98(Z80 *cpu);
Z80OPCODE op_99(Z80 *cpu);
Z80OPCODE op_9A(Z80 *cpu);
Z80OPCODE op_9B(Z80 *cpu);
Z80OPCODE op_9F(Z80 *cpu);
Z80OPCODE op_A0(Z80 *cpu);
Z80OPCODE op_A1(Z80 *cpu);
Z80OPCODE op_A2(Z80 *cpu);
Z80OPCODE op_A3(Z80 *cpu);
Z80OPCODE op_A7(Z80 *cpu);
Z80OPCODE op_A8(Z80 *cpu);
Z80OPCODE op_A9(Z80 *cpu);
Z80OPCODE op_AA(Z80 *cpu);
Z80OPCODE op_AB(Z80 *cpu);
Z80OPCODE op_AF(Z80 *cpu);
Z80OPCODE op_B0(Z80 *cpu);
Z80OPCODE op_B1(Z80 *cpu);
Z80OPCODE op_B2(Z80 *cpu);
Z80OPCODE op_B3(Z80 *cpu);
Z80OPCODE op_B7(Z80 *cpu);
Z80OPCODE op_B8(Z80 *cpu);
Z80OPCODE op_B9(Z80 *cpu);
Z80OPCODE op_BA(Z80 *cpu);
Z80OPCODE op_BB(Z80 *cpu);
Z80OPCODE op_BF(Z80 *cpu);
Z80OPCODE op_C0(Z80 *cpu);
Z80OPCODE op_C1(Z80 *cpu);
Z80OPCODE op_C2(Z80 *cpu);
Z80OPCODE op_C3(Z80 *cpu);
Z80OPCODE op_C4(Z80 *cpu);
Z80OPCODE op_C5(Z80 *cpu);
Z80OPCODE op_C6(Z80 *cpu);
Z80OPCODE op_C7(Z80 *cpu);
Z80OPCODE op_C8(Z80 *cpu);
Z80OPCODE op_C9(Z80 *cpu);
Z80OPCODE op_CA(Z80 *cpu);
Z80OPCODE op_CB(Z80 *cpu);
Z80OPCODE op_CC(Z80 *cpu);
Z80OPCODE op_CD(Z80 *cpu);
Z80OPCODE op_CE(Z80 *cpu);
Z80OPCODE op_CF(Z80 *cpu);
Z80OPCODE op_D0(Z80 *cpu);
Z80OPCODE op_D1(Z80 *cpu);
Z80OPCODE op_D2(Z80 *cpu);
Z80OPCODE op_D3(Z80 *cpu);
Z80OPCODE op_D4(Z80 *cpu);
Z80OPCODE op_D5(Z80 *cpu);
Z80OPCODE op_D6(Z80 *cpu);
Z80OPCODE op_D7(Z80 *cpu);
Z80OPCODE op_D8(Z80 *cpu);
Z80OPCODE op_D9(Z80 *cpu);
Z80OPCODE op_DA(Z80 *cpu);
Z80OPCODE op_DB(Z80 *cpu);
Z80OPCODE op_DC(Z80 *cpu);
Z80OPCODE op_DD(Z80 *cpu);
Z80OPCODE op_DE(Z80 *cpu);
Z80OPCODE op_DF(Z80 *cpu);
Z80OPCODE op_E0(Z80 *cpu);
Z80OPCODE op_E2(Z80 *cpu);
Z80OPCODE op_E4(Z80 *cpu);
Z80OPCODE op_E6(Z80 *cpu);
Z80OPCODE op_E7(Z80 *cpu);
Z80OPCODE op_E8(Z80 *cpu);
Z80OPCODE op_EA(Z80 *cpu);
Z80OPCODE op_EB(Z80 *cpu);
Z80OPCODE op_EC(Z80 *cpu);
Z80OPCODE op_ED(Z80 *cpu);
Z80OPCODE op_EE(Z80 *cpu);
Z80OPCODE op_EF(Z80 *cpu);
Z80OPCODE op_F0(Z80 *cpu);
Z80OPCODE op_F1(Z80 *cpu);
Z80OPCODE op_F2(Z80 *cpu);
Z80OPCODE op_F3(Z80 *cpu);
Z80OPCODE op_F4(Z80 *cpu);
Z80OPCODE op_F5(Z80 *cpu);
Z80OPCODE op_F6(Z80 *cpu);
Z80OPCODE op_F7(Z80 *cpu);
Z80OPCODE op_F8(Z80 *cpu);
Z80OPCODE op_FA(Z80 *cpu);
Z80OPCODE op_FB(Z80 *cpu);
Z80OPCODE op_FC(Z80 *cpu);
Z80OPCODE op_FD(Z80 *cpu);
Z80OPCODE op_FE(Z80 *cpu);
Z80OPCODE op_FF(Z80 *cpu);
