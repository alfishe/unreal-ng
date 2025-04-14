#include "stdafx.h"

#include "common/modulelogger.h"

#include "z80disasm.h"
#include "common/dumphelper.h"
#include "common/stringhelper.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include <cstring>

/// region <Information>

// See: https://clrhome.org/table/
// See: http://www.z80.info/zip/z80-documented.pdf
// See: http://www.z80.info/z80undoc.htm
// See: http://www.z80.info/z80undoc3.txt

/// endregion </Information>

/// region <Static>

// Operands
// :1 - 1 byte
// :2 - 2 bytes
// :1 and OF_RELJUMP - relative jump offset 1 byte
// :1 and OF_DISP - index register (IX or IY) displacement, 1 byte

/// region <No prefix opcodes>

OpCode Z80Disassembler::noprefixOpcodes[256] =
{
    { OF_NONE,   4, 0, 0, "nop" },                                  // 0x00
    { OF_MWORD, 10, 0, 0, "ld bc,:2" },                             // 0x01
    { OF_NONE,   7, 0, 0, "ld bc,(a)" },                            // 0x02
    { OF_NONE,   6, 0, 0, "inc bc" },                               // 0x03
    { OF_NONE,   4, 0, 0, "inc b" },                                // 0x04
    { OF_NONE,   4, 0, 0, "dec b" },                                // 0x05
    { OF_MBYTE,  7, 0, 0, "ld b,:1" },                              // 0x06
    { OF_NONE,   4, 0, 0, "rlca" },                                 // 0x07
    { OF_NONE,   4, 0, 0, "ex af,af'" },                            // 0x08
    { OF_NONE,  11, 0, 0, "add hl,bc" },                            // 0x09
    { OF_NONE,   7, 0, 0, "ld a,(bc)" },                            // 0x0A
    { OF_NONE,   6, 0, 0, "dec bc" },                               // 0x0B
    { OF_NONE,   4, 0, 0, "inc c" },                                // 0x0C
    { OF_NONE,   4, 0, 0, "dec c" },                                // 0x0D
    { OF_MBYTE,  7, 0, 0, "ld c,:1" },                              // 0x0E
    { OF_NONE,   4, 0, 0, "rrca" },                                 // 0x0F
    
    { OF_CONDITION | OF_RELJUMP | OF_MBYTE, 0, 13, 8, "djnz :1" },  // 0x10
    { OF_MWORD, 10, 0, 0, "ld de,:2" },                             // 0x11
    { OF_MWORD,  7, 0, 0, "ld (de),:2" },                           // 0x12
    { OF_NONE,   6, 0, 0, "inc de" },                               // 0x13
    { OF_NONE,   4, 0, 0, "inc d" },                                // 0x14
    { OF_NONE,   4, 0, 0, "dec d" },                                // 0x15
    { OF_MBYTE,  7, 0, 0, "ld d,:1" },                              // 0x16
    { OF_NONE,   4, 0, 0, "rla" },                                  // 0x17
    { OF_RELJUMP | OF_MBYTE, 12, 0, 0, "jr :1" },                   // 0x18
    { OF_NONE,  11, 0, 0, "add hl,de" },                            // 0x19
    { OF_NONE,   7, 0, 0, "ld a,(de)" },                            // 0x1A
    { OF_NONE,   6, 0, 0, "dec de" },                               // 0x1B
    { OF_NONE,   4, 0, 0, "inc e" },                                // 0x1C
    { OF_NONE,   4, 0, 0, "dec e" },                                // 0x1D
    { OF_MBYTE,  7, 0, 0, "ld e,:1" },                              // 0x1E
    { OF_NONE,   4, 0, 0, "rra" },                                  // 0x1F

    { OF_CONDITION | OF_RELJUMP | OF_MBYTE, 0, 12, 7, "jr nz,:1" }, // 0x20
    { OF_MWORD, 10, 0, 0, "ld hl,:2" },                             // 0x21
    { OF_MWORD, 16, 0, 0, "ld (:2),hl" },                           // 0x22
    { OF_NONE,   6, 0, 0, "inc hl" },                               // 0x23
    { OF_NONE,   4, 0, 0, "inc h" },                                // 0x24
    { OF_NONE,   4, 0, 0, "dec h" },                                // 0x25
    { OF_MBYTE,  7, 0, 0, "ld h,:1" },                              // 0x26
    { OF_NONE,   4, 0, 0, "daa" },                                  // 0x27
    { OF_CONDITION | OF_RELJUMP | OF_MBYTE, 0, 12, 7, "jr z,:1" },  // 0x28
    { OF_NONE,  11, 0, 0, "add hl,hl" },                            // 0x29
    { OF_MWORD, 16, 0, 0, "ld hl,(:2)" },                           // 0x2A
    { OF_NONE,   6, 0, 0, "dec hl" },                               // 0x2B
    { OF_NONE,   4, 0, 0, "inc l" },                                // 0x2C
    { OF_NONE,   4, 0, 0, "dec l" },                                // 0x2D
    { OF_MBYTE,  7, 0, 0, "ld l,:1" },                              // 0x2E
    { OF_NONE,   4, 0, 0, "cpl" },                                  // 0x2F

    { OF_CONDITION | OF_RELJUMP | OF_MBYTE, 0, 12, 7, "jr nc,:1" }, // 0x30
    { OF_MWORD, 10, 0, 0, "ld sp,:2" },                             // 0x31
    { OF_MWORD, 13, 0, 0, "ld (:2),a" },                            // 0x32
    { OF_NONE,   6, 0, 0, "inc sp" },                               // 0x33
    { OF_NONE,  11, 0, 0, "inc (hl)" },                             // 0x34
    { OF_NONE,  11, 0, 0, "dec (hl)" },                             // 0x35
    { OF_MBYTE,  7, 0, 0, "ld (hl),:1" },                           // 0x36
    { OF_NONE,   4, 0, 0, "scf" },                                  // 0x37
    { OF_CONDITION | OF_RELJUMP | OF_MBYTE, 0, 12, 7, "jr c,:1" },  // 0x38
    { OF_NONE,  11, 0, 0, "add hl,sp" },                            // 0x39
    { OF_MWORD, 13, 0, 0, "ld a,(:2)" },                            // 0x3A
    { OF_NONE,   6, 0, 0, "dec sp" },                               // 0x3B
    { OF_NONE,   4, 0, 0, "inc a" },                                // 0x3C
    { OF_NONE,   4, 0, 0, "dec a" },                                // 0x3D
    { OF_MBYTE,  7, 0, 0, "ld a,:1" },                              // 0x3E
    { OF_NONE,   4, 0, 0, "ccf" },                                  // 0x3F

    { OF_NONE,   4, 0, 0, "ld b,b" },                               // 0x40
    { OF_NONE,   4, 0, 0, "ld b,c" },                               // 0x41
    { OF_NONE,   4, 0, 0, "ld b,d" },                               // 0x42
    { OF_NONE,   4, 0, 0, "ld b,e" },                               // 0x43
    { OF_NONE,   4, 0, 0, "ld b,h" },                               // 0x44
    { OF_NONE,   4, 0, 0, "ld b,l" },                               // 0x45
    { OF_NONE,   7, 0, 0, "ld b,(hl)" },                            // 0x46
    { OF_NONE,   4, 0, 0, "ld b,a" },                               // 0x47
    { OF_NONE,   4, 0, 0, "ld c,b" },                               // 0x48
    { OF_NONE,   4, 0, 0, "ld c,c" },                               // 0x49
    { OF_NONE,   4, 0, 0, "ld c,d" },                               // 0x4A
    { OF_NONE,   4, 0, 0, "ld c,e" },                               // 0x4B
    { OF_NONE,   4, 0, 0, "ld c,h" },                               // 0x4C
    { OF_NONE,   4, 0, 0, "ld c,l" },                               // 0x4D
    { OF_NONE,   7, 0, 0, "ld c,(hl)" },                            // 0x4E
    { OF_NONE,   4, 0, 0, "ld c,a" },                               // 0x4F

    { OF_NONE,  4, 0, 0, "ld d,b" },                                // 0x50
    { OF_NONE,  4, 0, 0, "ld d,c" },                                // 0x51
    { OF_NONE,  4, 0, 0, "ld d,d" },                                // 0x52
    { OF_NONE,  4, 0, 0, "ld d,e" },                                // 0x53
    { OF_NONE,  4, 0, 0, "ld d,h" },                                // 0x54
    { OF_NONE,  4, 0, 0, "ld d,l" },                                // 0x55
    { OF_NONE,  7, 0, 0, "ld d,(hl)" },                             // 0x56
    { OF_NONE,  4, 0, 0, "ld d,a" },                                // 0x57
    { OF_NONE,  4, 0, 0, "ld e,b" },                                // 0x58
    { OF_NONE,  4, 0, 0, "ld e,c" },                                // 0x59
    { OF_NONE,  4, 0, 0, "ld e,d" },                                // 0x5A
    { OF_NONE,  4, 0, 0, "ld e,e" },                                // 0x5B
    { OF_NONE,  4, 0, 0, "ld e,h" },                                // 0x5C
    { OF_NONE,  4, 0, 0, "ld e,l" },                                // 0x5D
    { OF_NONE,  7, 0, 0, "ld e,(hl)" },                             // 0x5E
    { OF_NONE,  4, 0, 0, "ld e,a" },                                // 0x5F

    { OF_NONE, 4, 0, 0, "ld h,b" },                                 // 0x60
    { OF_NONE, 4, 0, 0, "ld h,c" },                                 // 0x61
    { OF_NONE, 4, 0, 0, "ld h,d" },                                 // 0x62
    { OF_NONE, 4, 0, 0, "ld h,e" },                                 // 0x63
    { OF_NONE, 4, 0, 0, "ld h,h" },                                 // 0x64
    { OF_NONE, 4, 0, 0, "ld h,l" },                                 // 0x65
    { OF_NONE, 7, 0, 0, "ld h,(hl)" },                              // 0x66
    { OF_NONE, 4, 0, 0, "ld h,a" },                                 // 0x67
    { OF_NONE, 4, 0, 0, "ld l,b" },                                 // 0x68
    { OF_NONE, 4, 0, 0, "ld l,c" },                                 // 0x69
    { OF_NONE, 4, 0, 0, "ld l,d" },                                 // 0x6A
    { OF_NONE, 4, 0, 0, "ld l,e" },                                 // 0x6B
    { OF_NONE, 4, 0, 0, "ld l,h" },                                 // 0x6C
    { OF_NONE, 4, 0, 0, "ld l,l" },                                 // 0x6D
    { OF_NONE, 7, 0, 0, "ld l,(hl)" },                              // 0x6E
    { OF_NONE, 4, 0, 0, "ld l,a" },                                 // 0x6F

    { OF_NONE, 7, 0, 0, "ld (hl),b" },                              // 0x70
    { OF_NONE, 7, 0, 0, "ld (hl),c" },                              // 0x71
    { OF_NONE, 7, 0, 0, "ld (hl),d" },                              // 0x72
    { OF_NONE, 7, 0, 0, "ld (hl),e" },                              // 0x73
    { OF_NONE, 7, 0, 0, "ld (hl),h" },                              // 0x74
    { OF_NONE, 7, 0, 0, "ld (hl),l" },                              // 0x75
    { OF_NONE, 4, 0, 0, "halt" },                                   // 0x76
    { OF_NONE, 7, 0, 0, "ld (hl),a" },                              // 0x77
    { OF_NONE, 4, 0, 0, "ld a,b" },                                 // 0x78
    { OF_NONE, 4, 0, 0, "ld a,c" },                                 // 0x79
    { OF_NONE, 4, 0, 0, "ld a,d" },                                 // 0x7A
    { OF_NONE, 4, 0, 0, "ld a,e" },                                 // 0x7B
    { OF_NONE, 4, 0, 0, "ld a,h" },                                 // 0x7C
    { OF_NONE, 4, 0, 0, "ld a,l" },                                 // 0x7D
    { OF_NONE, 7, 0, 0, "ld a,(hl)" },                              // 0x7E
    { OF_NONE, 4, 0, 0, "ld a,a" },                                 // 0x7F

    { OF_NONE, 4, 0, 0, "add a,b" },                                // 0x80
    { OF_NONE, 4, 0, 0, "add a,c" },                                // 0x81
    { OF_NONE, 4, 0, 0, "add a,d" },                                // 0x82
    { OF_NONE, 4, 0, 0, "add a,e" },                                // 0x83
    { OF_NONE, 4, 0, 0, "add a,h" },                                // 0x84
    { OF_NONE, 4, 0, 0, "add a,l" },                                // 0x85
    { OF_NONE, 7, 0, 0, "add a,(hl)" },                             // 0x86
    { OF_NONE, 4, 0, 0, "add a,a" },                                // 0x87
    { OF_NONE, 4, 0, 0, "adc a,b" },                                // 0x88
    { OF_NONE, 4, 0, 0, "adc a,c" },                                // 0x89
    { OF_NONE, 4, 0, 0, "adc a,d" },                                // 0x8A
    { OF_NONE, 4, 0, 0, "adc a,e" },                                // 0x8B
    { OF_NONE, 4, 0, 0, "adc a,h" },                                // 0x8C
    { OF_NONE, 4, 0, 0, "adc a,l" },                                // 0x8D
    { OF_NONE, 7, 0, 0, "adc a,(hl)" },                             // 0x8E
    { OF_NONE, 4, 0, 0, "adc a,a" },                                // 0x8F

    { OF_NONE, 4, 0, 0, "sub b" },                                  // 0x90
    { OF_NONE, 4, 0, 0, "sub c" },                                  // 0x91
    { OF_NONE, 4, 0, 0, "sub d" },                                  // 0x92
    { OF_NONE, 4, 0, 0, "sub e" },                                  // 0x93
    { OF_NONE, 4, 0, 0, "sub h" },                                  // 0x94
    { OF_NONE, 4, 0, 0, "sub l" },                                  // 0x95
    { OF_NONE, 7, 0, 0, "sub (hl)" },                               // 0x96
    { OF_NONE, 4, 0, 0, "sub a" },                                  // 0x97
    { OF_NONE, 4, 0, 0, "sbc a,b" },                                // 0x98
    { OF_NONE, 4, 0, 0, "sbc a,c" },                                // 0x99
    { OF_NONE, 4, 0, 0, "sbc a,d" },                                // 0x9A
    { OF_NONE, 4, 0, 0, "sbc a,e" },                                // 0x9B
    { OF_NONE, 4, 0, 0, "sbc a,h" },                                // 0x9C
    { OF_NONE, 4, 0, 0, "sbc a,l" },                                // 0x9D
    { OF_NONE, 7, 0, 0, "sbc (hl)" },                               // 0x9E
    { OF_NONE, 4, 0, 0, "sbc a,a" },                                // 0x9F

    { OF_NONE, 4, 0, 0, "and b" },                                  // 0xA0
    { OF_NONE, 4, 0, 0, "and c" },                                  // 0xA1
    { OF_NONE, 4, 0, 0, "and d" },                                  // 0xA2
    { OF_NONE, 4, 0, 0, "and e" },                                  // 0xA3
    { OF_NONE, 4, 0, 0, "and h" },                                  // 0xA4
    { OF_NONE, 4, 0, 0, "and l" },                                  // 0xA5
    { OF_NONE, 7, 0, 0, "and (hl)" },                               // 0xA6
    { OF_NONE, 4, 0, 0, "and a" },                                  // 0xA7
    { OF_NONE, 4, 0, 0, "xor b" },                                  // 0xA8
    { OF_NONE, 4, 0, 0, "xor c" },                                  // 0xA9
    { OF_NONE, 4, 0, 0, "xor d" },                                  // 0xAA
    { OF_NONE, 4, 0, 0, "xor e" },                                  // 0xAB
    { OF_NONE, 4, 0, 0, "xor h" },                                  // 0xAC
    { OF_NONE, 4, 0, 0, "xor l" },                                  // 0xAD
    { OF_NONE, 7, 0, 0, "xor (hl)" },                               // 0xAE
    { OF_NONE, 4, 0, 0, "xor a" },                                  // 0xAF

    { OF_NONE, 4, 0, 0, "or b" },                                   // 0xB0
    { OF_NONE, 4, 0, 0, "or c" },                                   // 0xB1
    { OF_NONE, 4, 0, 0, "or d" },                                   // 0xB2
    { OF_NONE, 4, 0, 0, "or e" },                                   // 0xB3
    { OF_NONE, 4, 0, 0, "or h" },                                   // 0xB4
    { OF_NONE, 4, 0, 0, "or l" },                                   // 0xB5
    { OF_NONE, 7, 0, 0, "or (hl)" },                                // 0xB6
    { OF_NONE, 4, 0, 0, "or a" },                                   // 0xB7
    { OF_NONE, 4, 0, 0, "cp b" },                                   // 0xB8
    { OF_NONE, 4, 0, 0, "cp c" },                                   // 0xB9
    { OF_NONE, 4, 0, 0, "cp d" },                                   // 0xBA
    { OF_NONE, 4, 0, 0, "cp e" },                                   // 0xBB
    { OF_NONE, 4, 0, 0, "cp h" },                                   // 0xBC
    { OF_NONE, 4, 0, 0, "cp l" },                                   // 0xBD
    { OF_NONE, 7, 0, 0, "cp (hl)" },                                // 0xBE
    { OF_NONE, 4, 0, 0, "cp a" },                                   // 0xBF

    { OF_CONDITION | OF_RET,  0, 11, 5, "ret nz" },                 // 0xC0
    { OF_NONE, 10, 0, 0, "pop bc" },                                // 0xC1
    { OF_CONDITION | OF_MWORD | OF_JUMP, 0, 10, 10, "jp nz,:2" },   // 0xC2
    { OF_MWORD | OF_JUMP, 10, 0, 0, "jp.:2" },                      // 0xC3
    { OF_CONDITION | OF_MWORD | OF_JUMP, 0, 17, 10, "call nz,:2" }, // 0xC4
    { OF_NONE, 11, 0, 0, "push bc" },                               // 0xC5
    { OF_MBYTE,  7, 0, 0, "add a,:1" },                             // 0xC6
    { OF_RST | OF_JUMP, 11, 0, 0, "rst #00" },                      // 0xC7
    { OF_CONDITION | OF_RET, 0, 11, 5, "ret z" },                   // 0xC8
    { OF_NONE | OF_RET, 10, 0, 0, "ret" },                          // 0xC9
    { OF_CONDITION | OF_MWORD | OF_JUMP, 0, 10, 10, "jp z,:2" },    // 0xCA
    { OF_PREFIX,  4, 0, 0, "#CB" },                                 // 0xCB - Prefix
    { OF_CONDITION | OF_MWORD | OF_JUMP, 0, 17, 10, "call z,:2" },  // 0xCC
    { OF_MWORD | OF_JUMP, 17, 0, 0, "call :2" },                    // 0xCD
    { OF_MBYTE,  7, 0, 0, "adc a,:1" },                             // 0xCE
    { OF_RST | OF_JUMP, 11, 0, 0, "rst #08" },                      // 0xCF
    { OF_CONDITION | OF_RET,  0, 11, 5, "ret nc" },                 // 0xD0
    { OF_NONE, 10, 0, 0, "pop de" },                                // 0xD1
    { OF_CONDITION | OF_MWORD | OF_JUMP,  0, 10, 10, "jp nc,:2" },  // 0xD2
    { OF_MBYTE, 11, 0, 0, "out (:1),a" },                           // 0xD3
    { OF_CONDITION | OF_MWORD | OF_JUMP, 0, 17, 10, "call nc,:2" }, // 0xD4
    { OF_NONE, 11, 0, 0, "push de" },                               // 0xD5
    { OF_MBYTE,  7, 0, 0, "sub :1" },                               // 0xD6
    { OF_RST | OF_JUMP, 11, 0, 0, "rst #10" },                      // 0xD7
    { OF_CONDITION | OF_RET,  0, 11, 5, "ret c" },                  // 0xD8
    { OF_NONE,  4, 0, 0, "exx" },                                   // 0xD9
    { OF_CONDITION | OF_MWORD | OF_JUMP,  0, 10, 10, "jp c,:2" },   // 0xDA
    { OF_MBYTE, 11, 0, 0, "in a,(:1)" },                            // 0xDB
    { OF_CONDITION | OF_MWORD | OF_JUMP, 0, 17, 10, "call c,:2" },  // 0xDC
    { OF_PREFIX, 4, 0, 0, "#DD" },                                  // 0xDD - Prefix
    { OF_MBYTE,  7, 0, 0, "sbc a,:1" },                             // 0xDE
    { OF_RST | OF_JUMP, 11, 0, 0, "rst #18" },                      // 0xDF

    { OF_CONDITION | OF_RET,  0, 11, 5, "ret po" },                 // 0xE0
    { OF_NONE, 10, 0, 0, "pop hl" },                                // 0xE1
    { OF_CONDITION | OF_MWORD | OF_JUMP, 0, 10, 10, "jp po,:2" },   // 0xE2
    { OF_NONE, 19, 0, 0, "ex (sp),hl" },                            // 0xE3
    { OF_CONDITION | OF_MWORD | OF_JUMP, 0, 17, 10, "call po,:2" }, // 0xE4
    { OF_NONE, 11, 0, 0, "push hl" },                               // 0xE5
    { OF_MBYTE,  7, 0, 0, "and :1" },                               // 0xE6
    { OF_RST | OF_JUMP, 11, 0, 0, "rst #20" },                      // 0xE7
    { OF_CONDITION | OF_RET,  0, 11, 5, "ret pe" },                 // 0xE8
    { OF_JUMP, 4, 0, 0, "jp (hl)" },                                // 0xE9
    { OF_CONDITION | OF_MWORD | OF_JUMP,  0, 10, 10, "jp pe,:2" },  // 0xEA
    { OF_NONE, 4, 0, 0, "ex de,hl" },                               // 0xEB
    { OF_CONDITION | OF_MWORD | OF_JUMP, 0, 17, 10, "call pe,:2" }, // 0xEC
    { OF_PREFIX, 4, 0, 0, "#ED" },                                  // 0xED - Prefix
    { OF_MBYTE,  7, 0, 0, "xor :1" },                               // 0xEE
    { OF_RST | OF_JUMP, 11, 0, 0, "rst #28" },                      // 0xEF

    { OF_CONDITION | OF_RET, 0, 11, 5, "ret p" },                   // 0xF0
    { OF_NONE, 10, 0, 0, "pop af" },                                // 0xF1
    { OF_CONDITION | OF_MWORD | OF_JUMP, 0, 10, 10, "jp p,:2" },    // 0xF2
    { OF_NONE,  4, 0, 0, "di" },                                    // 0xF3
    { OF_CONDITION | OF_MWORD | OF_JUMP, 0, 17, 10, "call p,:2" },  // 0xF4
    { OF_NONE, 11, 0, 0, "push af" },                               // 0xF5
    { OF_MBYTE,  7, 0, 0, "or :1" },                                // 0xF6
    { OF_RST | OF_JUMP, 11, 0, 0, "rst #30" },                      // 0xF7
    { OF_CONDITION | OF_RET,  0, 11, 5, "ret m" },                  // 0xF8
    { OF_NONE,  6, 0, 0, "ld sp,hl" },                              // 0xF9
    { OF_CONDITION | OF_MWORD | OF_JUMP, 0, 10, 10, "jp m,:2" },    // 0xFA
    { OF_NONE,  4, 0, 0, "ei" },                                    // 0xFB
    { OF_CONDITION | OF_MWORD | OF_JUMP, 0, 17, 10, "call m,:2" },  // 0xFC
    { OF_PREFIX,  4, 0, 0, "#FD" },                                 // 0xFD - Prefix
    { OF_MBYTE,  7, 0, 0, "cp :1" },                                // 0xFE
    { OF_RST | OF_JUMP, 11, 0, 0, "rst #38" },                      // 0xFF
};

/// endregion </No prefix opcodes>

/// region <#CB prefix opcodes>

OpCode Z80Disassembler::cbOpcodes[256]
{
    { OF_NONE,  8, 0, 0, "rlc b" },                         // 0x00
    { OF_NONE,  8, 0, 0, "rlc c" },                         // 0x01
    { OF_NONE,  8, 0, 0, "rlc d" },                         // 0x02
    { OF_NONE,  8, 0, 0, "rlc e" },                         // 0x03
    { OF_NONE,  8, 0, 0, "rlc h" },                         // 0x04
    { OF_NONE,  8, 0, 0, "rlc l" },                         // 0x05
    { OF_NONE, 15, 0, 0, "rlc (hl)" },                      // 0x06
    { OF_NONE,  8, 0, 0, "rlc a" },                         // 0x07
    { OF_NONE,  8, 0, 0, "rrc b" },                         // 0x08
    { OF_NONE,  8, 0, 0, "rrc c" },                         // 0x09
    { OF_NONE,  8, 0, 0, "rrc d" },                         // 0x0A
    { OF_NONE,  8, 0, 0, "rrc e" },                         // 0x0B
    { OF_NONE,  8, 0, 0, "rrc h" },                         // 0x0C
    { OF_NONE,  8, 0, 0, "rrc l" },                         // 0x0D
    { OF_NONE, 15, 0, 0, "rrc (hl)" },                      // 0x0E
    { OF_NONE,  8, 0, 0, "rrc a" },                         // 0x0F

    { OF_NONE,  8, 0, 0, "rl b" },                          // 0x10
    { OF_NONE,  8, 0, 0, "rl c" },                          // 0x11
    { OF_NONE,  8, 0, 0, "rl d" },                          // 0x12
    { OF_NONE,  8, 0, 0, "rl e" },                          // 0x13
    { OF_NONE,  8, 0, 0, "rl h" },                          // 0x14
    { OF_NONE,  8, 0, 0, "rl l" },                          // 0x15
    { OF_NONE, 15, 0, 0, "rl (hl)" },                       // 0x16
    { OF_NONE,  8, 0, 0, "rl a" },                          // 0x17
    { OF_NONE,  8, 0, 0, "rr b" },                          // 0x18
    { OF_NONE,  8, 0, 0, "rr c" },                          // 0x19
    { OF_NONE,  8, 0, 0, "rr d" },                          // 0x1A
    { OF_NONE,  8, 0, 0, "rr e" },                          // 0x1B
    { OF_NONE,  8, 0, 0, "rr h" },                          // 0x1C
    { OF_NONE,  8, 0, 0, "rr l" },                          // 0x1D
    { OF_NONE, 15, 0, 0, "rr (hl)" },                       // 0x1E
    { OF_NONE,  8, 0, 0, "rr a" },                          // 0x1F

    { OF_NONE,  8, 0, 0, "sla b" },                         // 0x20
    { OF_NONE,  8, 0, 0, "sla c" },                         // 0x21
    { OF_NONE,  8, 0, 0, "sla d" },                         // 0x22
    { OF_NONE,  8, 0, 0, "sla e" },                         // 0x23
    { OF_NONE,  8, 0, 0, "sla h" },                         // 0x24
    { OF_NONE,  8, 0, 0, "sla l" },                         // 0x25
    { OF_NONE, 15, 0, 0, "sla (hl)" },                      // 0x26
    { OF_NONE,  8, 0, 0, "sla a" },                         // 0x27
    { OF_NONE,  8, 0, 0, "sra b" },                         // 0x28
    { OF_NONE,  8, 0, 0, "sra c" },                         // 0x29
    { OF_NONE,  8, 0, 0, "sra d" },                         // 0x2A
    { OF_NONE,  8, 0, 0, "sra e" },                         // 0x2B
    { OF_NONE,  8, 0, 0, "sra h" },                         // 0x2C
    { OF_NONE,  8, 0, 0, "sra l" },                         // 0x2D
    { OF_NONE, 15, 0, 0, "sra (hl)" },                      // 0x2E
    { OF_NONE,  8, 0, 0, "sra a" },                         // 0x2F

    { OF_NONE,  8, 0, 0, "sll b" },                         // 0x30
    { OF_NONE,  8, 0, 0, "sll c" },                         // 0x31
    { OF_NONE,  8, 0, 0, "sll d" },                         // 0x32
    { OF_NONE,  8, 0, 0, "sll e" },                         // 0x33
    { OF_NONE,  8, 0, 0, "sll h" },                         // 0x34
    { OF_NONE,  8, 0, 0, "sll l" },                         // 0x35
    { OF_NONE, 15, 0, 0, "sll (hl)" },                      // 0x36
    { OF_NONE,  8, 0, 0, "sll a" },                         // 0x37
    { OF_NONE,  8, 0, 0, "srl b" },                         // 0x38
    { OF_NONE,  8, 0, 0, "srl c" },                         // 0x39
    { OF_NONE,  8, 0, 0, "srl d" },                         // 0x3A
    { OF_NONE,  8, 0, 0, "srl e" },                         // 0x3B
    { OF_NONE,  8, 0, 0, "srl h" },                         // 0x3C
    { OF_NONE,  8, 0, 0, "srl l" },                         // 0x3D
    { OF_NONE, 15, 0, 0, "srl (hl)" },                      // 0x3E
    { OF_NONE,  8, 0, 0, "srl a" },                         // 0x3F

    { OF_NONE,  8, 0, 0, "bit 0,b" },                       // 0x40
    { OF_NONE,  8, 0, 0, "bit 0,c" },                       // 0x41
    { OF_NONE,  8, 0, 0, "bit 0,d" },                       // 0x42
    { OF_NONE,  8, 0, 0, "bit 0,e" },                       // 0x43
    { OF_NONE,  8, 0, 0, "bit 0,h" },                       // 0x44
    { OF_NONE,  8, 0, 0, "bit 0,l" },                       // 0x45
    { OF_NONE, 12, 0, 0, "bit 0,(hl)" },                    // 0x46
    { OF_NONE,  8, 0, 0, "bit 0,a" },                       // 0x47
    { OF_NONE,  8, 0, 0, "bit 1,b" },                       // 0x48
    { OF_NONE,  8, 0, 0, "bit 1,c" },                       // 0x49
    { OF_NONE,  8, 0, 0, "bit 1,d" },                       // 0x4A
    { OF_NONE,  8, 0, 0, "bit 1,e" },                       // 0x4B
    { OF_NONE,  8, 0, 0, "bit 1,h" },                       // 0x4C
    { OF_NONE,  8, 0, 0, "bit 1,l" },                       // 0x4D
    { OF_NONE, 12, 0, 0, "bit 1,(hl)" },                    // 0x4E
    { OF_NONE,  8, 0, 0, "bit 1,a" },                       // 0x4F

    { OF_NONE,  8, 0, 0, "bit 2,b" },                       // 0x50
    { OF_NONE,  8, 0, 0, "bit 2,c" },                       // 0x51
    { OF_NONE,  8, 0, 0, "bit 2,d" },                       // 0x52
    { OF_NONE,  8, 0, 0, "bit 2,e" },                       // 0x53
    { OF_NONE,  8, 0, 0, "bit 2,h" },                       // 0x54
    { OF_NONE,  8, 0, 0, "bit 2,l" },                       // 0x55
    { OF_NONE, 12, 0, 0, "bit 2,(hl)" },                    // 0x56
    { OF_NONE,  8, 0, 0, "bit 2,a" },                       // 0x57
    { OF_NONE,  8, 0, 0, "bit 3,b" },                       // 0x58
    { OF_NONE,  8, 0, 0, "bit 3,c" },                       // 0x59
    { OF_NONE,  8, 0, 0, "bit 3,d" },                       // 0x5A
    { OF_NONE,  8, 0, 0, "bit 3,e" },                       // 0x5B
    { OF_NONE,  8, 0, 0, "bit 3,h" },                       // 0x5C
    { OF_NONE,  8, 0, 0, "bit 3,l" },                       // 0x5D
    { OF_NONE, 12, 0, 0, "bit 3,(hl)" },                    // 0x5E
    { OF_NONE,  8, 0, 0, "bit 3,a" },                       // 0x5F

    { OF_NONE,  8, 0, 0, "bit 4,b" },                       // 0x60
    { OF_NONE,  8, 0, 0, "bit 4,c" },                       // 0x61
    { OF_NONE,  8, 0, 0, "bit 4,d" },                       // 0x62
    { OF_NONE,  8, 0, 0, "bit 4,e" },                       // 0x63
    { OF_NONE,  8, 0, 0, "bit 4,h" },                       // 0x64
    { OF_NONE,  8, 0, 0, "bit 4,l" },                       // 0x65
    { OF_NONE, 12, 0, 0, "bit 4,(hl)" },                    // 0x66
    { OF_NONE,  8, 0, 0, "bit 4,a" },                       // 0x67
    { OF_NONE,  8, 0, 0, "bit 5,b" },                       // 0x68
    { OF_NONE,  8, 0, 0, "bit 5,c" },                       // 0x69
    { OF_NONE,  8, 0, 0, "bit 5,d" },                       // 0x6A
    { OF_NONE,  8, 0, 0, "bit 5,e" },                       // 0x6B
    { OF_NONE,  8, 0, 0, "bit 5,h" },                       // 0x6C
    { OF_NONE,  8, 0, 0, "bit 5,l" },                       // 0x6D
    { OF_NONE, 12, 0, 0, "bit 5,(hl)" },                    // 0x6E
    { OF_NONE,  8, 0, 0, "bit 5,a" },                       // 0x6F

    { OF_NONE,  8, 0, 0, "bit 6,b" },                       // 0x70
    { OF_NONE,  8, 0, 0, "bit 6,c" },                       // 0x71
    { OF_NONE,  8, 0, 0, "bit 6,d" },                       // 0x72
    { OF_NONE,  8, 0, 0, "bit 6,e" },                       // 0x73
    { OF_NONE,  8, 0, 0, "bit 6,h" },                       // 0x74
    { OF_NONE,  8, 0, 0, "bit 6,l" },                       // 0x75
    { OF_NONE, 12, 0, 0, "bit 6,(hl)" },                    // 0x76
    { OF_NONE,  8, 0, 0, "bit 6,a" },                       // 0x77
    { OF_NONE,  8, 0, 0, "bit 7,b" },                       // 0x78
    { OF_NONE,  8, 0, 0, "bit 7,c" },                       // 0x79
    { OF_NONE,  8, 0, 0, "bit 7,d" },                       // 0x7A
    { OF_NONE,  8, 0, 0, "bit 7,e" },                       // 0x7B
    { OF_NONE,  8, 0, 0, "bit 7,h" },                       // 0x7C
    { OF_NONE,  8, 0, 0, "bit 7,l" },                       // 0x7D
    { OF_NONE, 12, 0, 0, "bit 7,(hl)" },                    // 0x7E
    { OF_NONE,  8, 0, 0, "bit 7,a" },                       // 0x7F

    { OF_NONE,  8, 0, 0, "res 0,b" },                       // 0x80
    { OF_NONE,  8, 0, 0, "res 0,c" },                       // 0x81
    { OF_NONE,  8, 0, 0, "res 0,d" },                       // 0x82
    { OF_NONE,  8, 0, 0, "res 0,e" },                       // 0x83
    { OF_NONE,  8, 0, 0, "res 0,h" },                       // 0x84
    { OF_NONE,  8, 0, 0, "res 0,l" },                       // 0x85
    { OF_NONE, 15, 0, 0, "res 0,(hl)" },                    // 0x86
    { OF_NONE,  8, 0, 0, "res 0,a" },                       // 0x87
    { OF_NONE,  8, 0, 0, "res 1,b" },                       // 0x88
    { OF_NONE,  8, 0, 0, "res 1,c" },                       // 0x89
    { OF_NONE,  8, 0, 0, "res 1,d" },                       // 0x8A
    { OF_NONE,  8, 0, 0, "res 1,e" },                       // 0x8B
    { OF_NONE,  8, 0, 0, "res 1,h" },                       // 0x8C
    { OF_NONE,  8, 0, 0, "res 1,l" },                       // 0x8D
    { OF_NONE, 15, 0, 0, "res 1,(hl)" },                    // 0x8E
    { OF_NONE,  8, 0, 0, "res 1,a" },                       // 0x8F

    { OF_NONE,  8, 0, 0, "res 2,b" },                       // 0x90
    { OF_NONE,  8, 0, 0, "res 2,c" },                       // 0x91
    { OF_NONE,  8, 0, 0, "res 2,d" },                       // 0x92
    { OF_NONE,  8, 0, 0, "res 2,e" },                       // 0x93
    { OF_NONE,  8, 0, 0, "res 2,h" },                       // 0x94
    { OF_NONE,  8, 0, 0, "res 2,l" },                       // 0x95
    { OF_NONE, 15, 0, 0, "res 2,(hl)" },                    // 0x96
    { OF_NONE,  8, 0, 0, "res 2,a" },                       // 0x97
    { OF_NONE,  8, 0, 0, "res 3,b" },                       // 0x98
    { OF_NONE,  8, 0, 0, "res 3,c" },                       // 0x99
    { OF_NONE,  8, 0, 0, "res 3,d" },                       // 0x9A
    { OF_NONE,  8, 0, 0, "res 3,e" },                       // 0x9B
    { OF_NONE,  8, 0, 0, "res 3,h" },                       // 0x9C
    { OF_NONE,  8, 0, 0, "res 3,l" },                       // 0x9D
    { OF_NONE, 15, 0, 0, "res 3,(hl)" },                    // 0x9E
    { OF_NONE,  8, 0, 0, "res 3,a" },                       // 0x9F

    { OF_NONE,  8, 0, 0, "res 4,b" },                       // 0xA0
    { OF_NONE,  8, 0, 0, "res 4,c" },                       // 0xA1
    { OF_NONE,  8, 0, 0, "res 4,d" },                       // 0xA2
    { OF_NONE,  8, 0, 0, "res 4,e" },                       // 0xA3
    { OF_NONE,  8, 0, 0, "res 4,h" },                       // 0xA4
    { OF_NONE,  8, 0, 0, "res 4,l" },                       // 0xA5
    { OF_NONE, 15, 0, 0, "res 4,(hl)" },                    // 0xA6
    { OF_NONE,  8, 0, 0, "res 4,a" },                       // 0xA7
    { OF_NONE,  8, 0, 0, "res 5,b" },                       // 0xA8
    { OF_NONE,  8, 0, 0, "res 5,c" },                       // 0xA9
    { OF_NONE,  8, 0, 0, "res 5,d" },                       // 0xAA
    { OF_NONE,  8, 0, 0, "res 5,e" },                       // 0xAB
    { OF_NONE,  8, 0, 0, "res 5,h" },                       // 0xAC
    { OF_NONE,  8, 0, 0, "res 5,l" },                       // 0xAD
    { OF_NONE, 15, 0, 0, "res 5,(hl)" },                    // 0xAE
    { OF_NONE,  8, 0, 0, "res 5,a" },                       // 0xAF

    { OF_NONE,  8, 0, 0, "res 6,b" },                       // 0xB0
    { OF_NONE,  8, 0, 0, "res 6,c" },                       // 0xB1
    { OF_NONE,  8, 0, 0, "res 6,d" },                       // 0xB2
    { OF_NONE,  8, 0, 0, "res 6,e" },                       // 0xB3
    { OF_NONE,  8, 0, 0, "res 6,h" },                       // 0xB4
    { OF_NONE,  8, 0, 0, "res 6,l" },                       // 0xB5
    { OF_NONE, 15, 0, 0, "res 6,(hl)" },                    // 0xB6
    { OF_NONE,  8, 0, 0, "res 6,a" },                       // 0xB7
    { OF_NONE,  8, 0, 0, "res 7,b" },                       // 0xB8
    { OF_NONE,  8, 0, 0, "res 7,c" },                       // 0xB9
    { OF_NONE,  8, 0, 0, "res 7,d" },                       // 0xBA
    { OF_NONE,  8, 0, 0, "res 7,e" },                       // 0xBB
    { OF_NONE,  8, 0, 0, "res 7,h" },                       // 0xBC
    { OF_NONE,  8, 0, 0, "res 7,l" },                       // 0xBD
    { OF_NONE, 15, 0, 0, "res 7,(hl)" },                    // 0xBE
    { OF_NONE,  8, 0, 0, "res 7,a" },                       // 0xBF

    { OF_NONE,  8, 0, 0, "set 0,b" },                       // 0xC0
    { OF_NONE,  8, 0, 0, "set 0,c" },                       // 0xC1
    { OF_NONE,  8, 0, 0, "set 0,d" },                       // 0xC2
    { OF_NONE,  8, 0, 0, "set 0,e" },                       // 0xC3
    { OF_NONE,  8, 0, 0, "set 0,h" },                       // 0xC4
    { OF_NONE,  8, 0, 0, "set 0,l" },                       // 0xC5
    { OF_NONE, 15, 0, 0, "set 0,(hl)" },                    // 0xC6
    { OF_NONE,  8, 0, 0, "set 0,a" },                       // 0xC7
    { OF_NONE,  8, 0, 0, "set 1,b" },                       // 0xC8
    { OF_NONE,  8, 0, 0, "set 1,c" },                       // 0xC9
    { OF_NONE,  8, 0, 0, "set 1,d" },                       // 0xCA
    { OF_NONE,  8, 0, 0, "set 1,e" },                       // 0xCB
    { OF_NONE,  8, 0, 0, "set 1,h" },                       // 0xCC
    { OF_NONE,  8, 0, 0, "set 1,l" },                       // 0xCD
    { OF_NONE, 15, 0, 0, "set 1,(hl)" },                    // 0xCE
    { OF_NONE,  8, 0, 0, "set 1,a" },                       // 0xCF

    { OF_NONE,  8, 0, 0, "set 2,b" },                       // 0xD0
    { OF_NONE,  8, 0, 0, "set 2,c" },                       // 0xD1
    { OF_NONE,  8, 0, 0, "set 2,d" },                       // 0xD2
    { OF_NONE,  8, 0, 0, "set 2,e" },                       // 0xD3
    { OF_NONE,  8, 0, 0, "set 2,h" },                       // 0xD4
    { OF_NONE,  8, 0, 0, "set 2,l" },                       // 0xD5
    { OF_NONE, 15, 0, 0, "set 2,(hl)" },                    // 0xD6
    { OF_NONE,  8, 0, 0, "set 2,a" },                       // 0xD7
    { OF_NONE,  8, 0, 0, "set 3,b" },                       // 0xD8
    { OF_NONE,  8, 0, 0, "set 3,c" },                       // 0xD9
    { OF_NONE,  8, 0, 0, "set 3,d" },                       // 0xDA
    { OF_NONE,  8, 0, 0, "set 3,e" },                       // 0xDB
    { OF_NONE,  8, 0, 0, "set 3,h" },                       // 0xDC
    { OF_NONE,  8, 0, 0, "set 3,l" },                       // 0xDD
    { OF_NONE, 15, 0, 0, "set 3,(hl)" },                    // 0xDE
    { OF_NONE,  8, 0, 0, "set 3,a" },                       // 0xDF

    { OF_NONE,  8, 0, 0, "set 4,b" },                       // 0xE0
    { OF_NONE,  8, 0, 0, "set 4,c" },                       // 0xE1
    { OF_NONE,  8, 0, 0, "set 4,d" },                       // 0xE2
    { OF_NONE,  8, 0, 0, "set 4,e" },                       // 0xE3
    { OF_NONE,  8, 0, 0, "set 4,h" },                       // 0xE4
    { OF_NONE,  8, 0, 0, "set 4,l" },                       // 0xE5
    { OF_NONE, 15, 0, 0, "set 4,(hl)" },                    // 0xE6
    { OF_NONE,  8, 0, 0, "set 4,a" },                       // 0xE7
    { OF_NONE,  8, 0, 0, "set 5,b" },                       // 0xE8
    { OF_NONE,  8, 0, 0, "set 5,c" },                       // 0xE9
    { OF_NONE,  8, 0, 0, "set 5,d" },                       // 0xEA
    { OF_NONE,  8, 0, 0, "set 5,e" },                       // 0xEB
    { OF_NONE,  8, 0, 0, "set 5,h" },                       // 0xEC
    { OF_NONE,  8, 0, 0, "set 5,l" },                       // 0xED
    { OF_NONE, 15, 0, 0, "set 5,(hl)" },                    // 0xEE
    { OF_NONE,  8, 0, 0, "set 5,a" },                       // 0xEF

    { OF_NONE,  8, 0, 0, "set 6,b" },                       // 0xF0
    { OF_NONE,  8, 0, 0, "set 6,c" },                       // 0xF1
    { OF_NONE,  8, 0, 0, "set 6,d" },                       // 0xF2
    { OF_NONE,  8, 0, 0, "set 6,e" },                       // 0xF3
    { OF_NONE,  8, 0, 0, "set 6,h" },                       // 0xF4
    { OF_NONE,  8, 0, 0, "set 6,l" },                       // 0xF5
    { OF_NONE, 15, 0, 0, "set 6,(hl)" },                    // 0xF6
    { OF_NONE,  8, 0, 0, "set 6,a" },                       // 0xF7
    { OF_NONE,  8, 0, 0, "set 7,b" },                       // 0xF8
    { OF_NONE,  8, 0, 0, "set 7,c" },                       // 0xF9
    { OF_NONE,  8, 0, 0, "set 7,d" },                       // 0xFA
    { OF_NONE,  8, 0, 0, "set 7,e" },                       // 0xFB
    { OF_NONE,  8, 0, 0, "set 7,h" },                       // 0xFC
    { OF_NONE,  8, 0, 0, "set 7,l" },                       // 0xFD
    { OF_NONE, 15, 0, 0, "set 7,(hl)" },                    // 0xFE
    { OF_NONE,  8, 0, 0, "set 7,a" },                       // 0xFF
};

/// endregion </#CB prefix opcodes>

/// region <#DD prefix opcodes>

OpCode Z80Disassembler::ddOpcodes[256]
{
    { OF_NONE,  8, 0, 0, "nop" },                           // 0x00
    { OF_MWORD, 14, 0, 0, "ld bc,:2" },                     // 0x01
    { OF_NONE, 11, 0, 0, "ld bc,(a)" },                     // 0x02
    { OF_NONE, 10, 0, 0, "inc bc" },                        // 0x03
    { OF_NONE,  8, 0, 0, "inc b" },                         // 0x04
    { OF_NONE,  8, 0, 0, "dec b" },                         // 0x05
    { OF_MBYTE, 11, 0, 0, "ld b,:1" },                      // 0x06
    { OF_NONE,  8, 0, 0, "rlca" },                          // 0x07
    { OF_NONE,  8, 0, 0, "ex af,af'" },                     // 0x08
    { OF_NONE, 15, 0, 0, "add ix,bc" },                     // 0x09
    { OF_NONE, 11, 0, 0, "ld a,(bc)" },                     // 0x0A
    { OF_NONE, 10, 0, 0, "dec bc" },                        // 0x0B
    { OF_NONE,  8, 0, 0, "inc c" },                         // 0x0C
    { OF_NONE,  8, 0, 0, "dec c" },                         // 0x0D
    { OF_MBYTE, 11, 0, 0, "ld c,:1" },                      // 0x0E
    { OF_NONE,  8, 0, 0, "rrca" },                          // 0x0F

    { OF_CONDITION | OF_RELJUMP | OF_MBYTE, 0, 17, 12, "djnz :1" },    // 0x10
    { OF_MWORD, 14, 0, 0, "ld de,:2" },                     // 0x11
    { OF_MWORD, 11, 0, 0, "ld (de),:2" },                   // 0x12
    { OF_NONE, 10, 0, 0, "inc de" },                        // 0x13
    { OF_NONE,  8, 0, 0, "inc d" },                         // 0x14
    { OF_NONE,  8, 0, 0, "dec d" },                         // 0x15
    { OF_MBYTE, 11, 0, 0, "ld d,:1" },                      // 0x16
    { OF_NONE,  8, 0, 0, "rla" },                           // 0x17
    { OF_RELJUMP | OF_MBYTE,  16, 0, 0, "jr :1" },          // 0x18
    { OF_NONE,  15, 0, 0, "add ix,de" },                    // 0x19
    { OF_NONE,  11, 0, 0, "ld a,(de)" },                    // 0x1A
    { OF_NONE,  10, 0, 0, "dec de" },                       // 0x1B
    { OF_NONE,   8, 0, 0, "inc e" },                        // 0x1C
    { OF_NONE,   8, 0, 0, "dec e" },                        // 0x1D
    { OF_MBYTE,  11, 0, 0, "ld e,:1" },                     // 0x1E
    { OF_NONE,   8, 0, 0, "rra" },                          // 0x1F

    { OF_CONDITION | OF_RELJUMP | OF_MBYTE, 0, 16, 11, "jr nz,:1" },   // 0x20
    { OF_MWORD, 18, 0, 0, "ld ix,:2" },                     // 0x21
    { OF_MWORD, 24, 0, 0, "ld (:2),ix" },                   // 0x22
    { OF_NONE, 14, 0, 0, "inc ix" },                        // 0x23
    { OF_NONE,  8, 0, 0, "inc hx" },                        // 0x24
    { OF_NONE,  8, 0, 0, "dec hx" },                        // 0x25
    { OF_MBYTE, 11, 0, 0, "ld hx,:1" },                     // 0x26
    { OF_NONE,  8, 0, 0, "daa" },                           // 0x27
    { OF_CONDITION | OF_RELJUMP | OF_MBYTE, 0, 16, 11, "jr z,:1" },    // 0x28
    { OF_NONE, 15, 0, 0, "add ix,ix" },                     // 0x29
    { OF_MWORD, 20, 0, 0, "ld ix,(:2)" },                   // 0x2A
    { OF_NONE, 14, 0, 0, "dec ix" },                        // 0x2B
    { OF_NONE,  8, 0, 0, "inc lx" },                        // 0x2C
    { OF_NONE,  8, 0, 0, "dec lx" },                        // 0x2D
    { OF_MBYTE, 11, 0, 0, "ld lx,:1" },                     // 0x2E
    { OF_NONE,  8, 0, 0, "cpl" },                           // 0x2F

    { OF_CONDITION | OF_RELJUMP | OF_MBYTE, 0, 16, 11, "jr nc,:1" },   // 0x30
    { OF_MWORD, 11, 0, 0, "ld sp,:2" },                     // 0x31
    { OF_MWORD, 17, 0, 0, "ld (:2),a" },                    // 0x32
    { OF_NONE, 10, 0, 0, "inc sp" },                        // 0x33
    { OF_DISP, 19, 0, 0, "inc (ix+:1)" },                   // 0x34
    { OF_DISP, 19, 0, 0, "dec (ix+:1)" },                   // 0x35
    { OF_DISP | OF_MBYTE, 15, 0, 0, "ld (ix+:1),:1" },      // 0x36
    { OF_NONE,  8, 0, 0, "scf" },                           // 0x37
    { OF_CONDITION | OF_RELJUMP | OF_MBYTE, 0, 16, 11, "jr c,:1" },    // 0x38
    { OF_NONE, 15, 0, 0, "add ix,sp" },                     // 0x39
    { OF_MWORD, 17, 0, 0, "ld a,(:2)" },                    // 0x3A
    { OF_NONE, 10, 0, 0, "dec sp" },                        // 0x3B
    { OF_NONE,  8, 0, 0, "inc a" },                         // 0x3C
    { OF_NONE,  8, 0, 0, "dec a" },                         // 0x3D
    { OF_MBYTE, 11, 0, 0, "ld a,:1" },                      // 0x3E
    { OF_NONE,  8, 0, 0, "ccf" },                           // 0x3F

    { OF_NONE,  8, 0, 0, "ld b,b" },                        // 0x40
    { OF_NONE,  8, 0, 0, "ld b,c" },                        // 0x41
    { OF_NONE,  8, 0, 0, "ld b,d" },                        // 0x42
    { OF_NONE,  8, 0, 0, "ld b,e" },                        // 0x43
    { OF_NONE,  8, 0, 0, "ld b,hx" },                       // 0x44
    { OF_NONE,  8, 0, 0, "ld b,lx" },                       // 0x45
    { OF_DISP, 19, 0, 0, "ld b,(ix+:1)" },                  // 0x46
    { OF_NONE,  8, 0, 0, "ld b,a" },                        // 0x47
    { OF_NONE,  8, 0, 0, "ld c,b" },                        // 0x48
    { OF_NONE,  8, 0, 0, "ld c,c" },                        // 0x49
    { OF_NONE,  8, 0, 0, "ld c,d" },                        // 0x4A
    { OF_NONE,  8, 0, 0, "ld c,e" },                        // 0x4B
    { OF_NONE,  8, 0, 0, "ld c,hx" },                       // 0x4C
    { OF_NONE,  8, 0, 0, "ld c,lx" },                       // 0x4D
    { OF_DISP, 19, 0, 0, "ld c,(ix+:1)" },                  // 0x4E
    { OF_NONE,  8, 0, 0, "ld c,a" },                        // 0x4F

    { OF_NONE,  8, 0, 0, "ld d,b" },                        // 0x50
    { OF_NONE,  8, 0, 0, "ld d,c" },                        // 0x51
    { OF_NONE,  8, 0, 0, "ld d,d" },                        // 0x52
    { OF_NONE,  8, 0, 0, "ld d,e" },                        // 0x53
    { OF_NONE,  8, 0, 0, "ld d,hx" },                       // 0x54
    { OF_NONE,  8, 0, 0, "ld d,lx" },                       // 0x55
    { OF_DISP, 19, 0, 0, "ld d,(ix+:1)" },                  // 0x56
    { OF_NONE,  8, 0, 0, "ld d,a" },                        // 0x57
    { OF_NONE,  8, 0, 0, "ld e,b" },                        // 0x58
    { OF_NONE,  8, 0, 0, "ld e,c" },                        // 0x59
    { OF_NONE,  8, 0, 0, "ld e,d" },                        // 0x5A
    { OF_NONE,  8, 0, 0, "ld e,e" },                        // 0x5B
    { OF_NONE,  8, 0, 0, "ld e,hx" },                       // 0x5C
    { OF_NONE,  8, 0, 0, "ld e,lx" },                       // 0x5D
    { OF_DISP, 19, 0, 0, "ld e,(ix+:1)" },                  // 0x5E
    { OF_NONE,  8, 0, 0, "ld e,a" },                        // 0x5F

    { OF_NONE,  8, 0, 0, "ld hx,b" },                       // 0x60
    { OF_NONE,  8, 0, 0, "ld hx,c" },                       // 0x61
    { OF_NONE,  8, 0, 0, "ld hx,d" },                       // 0x62
    { OF_NONE,  8, 0, 0, "ld hx,e" },                       // 0x63
    { OF_NONE,  8, 0, 0, "ld hx,hx" },                      // 0x64
    { OF_NONE,  8, 0, 0, "ld hx,lx" },                      // 0x65
    { OF_DISP, 19, 0, 0, "ld h,(ix+:1)" },                  // 0x66
    { OF_NONE,  8, 0, 0, "ld hx,a" },                       // 0x67
    { OF_NONE,  8, 0, 0, "ld lx,b" },                       // 0x68
    { OF_NONE,  8, 0, 0, "ld lx,c" },                       // 0x69
    { OF_NONE,  8, 0, 0, "ld lx,d" },                       // 0x6A
    { OF_NONE,  8, 0, 0, "ld lx,e" },                       // 0x6B
    { OF_NONE,  8, 0, 0, "ld lx,hx" },                      // 0x6C
    { OF_NONE,  8, 0, 0, "ld lx,lx" },                      // 0x6D
    { OF_DISP, 19, 0, 0, "ld l,(ix+:1)" },                  // 0x6E
    { OF_NONE,  8, 0, 0, "ld lx,a" },                       // 0x6F

    { OF_DISP, 19, 0, 0, "ld (ix+:1),b" },                  // 0x70
    { OF_DISP, 19, 0, 0, "ld (ix+:1),c" },                  // 0x71
    { OF_DISP, 19, 0, 0, "ld (ix+:1),d" },                  // 0x72
    { OF_DISP, 19, 0, 0, "ld (ix+:1),e" },                  // 0x73
    { OF_DISP, 19, 0, 0, "ld (ix+:1),h" },                  // 0x74
    { OF_DISP, 19, 0, 0, "ld (ix+:1),l" },                  // 0x75
    { OF_NONE,  8, 0, 0, "halt" },                          // 0x76
    { OF_DISP, 19, 0, 0, "ld (ix+:1),a" },                  // 0x77
    { OF_NONE,  8, 0, 0, "ld a,b" },                        // 0x78
    { OF_NONE,  8, 0, 0, "ld a,c" },                        // 0x79
    { OF_NONE,  8, 0, 0, "ld a,d" },                        // 0x7A
    { OF_NONE,  8, 0, 0, "ld a,e" },                        // 0x7B
    { OF_NONE,  8, 0, 0, "ld a,hx" },                       // 0x7C
    { OF_NONE,  8, 0, 0, "ld a,lx" },                       // 0x7D
    { OF_DISP, 19, 0, 0, "ld a,(ix+:1)" },                  // 0x7E
    { OF_NONE,  8, 0, 0, "ld a,a" },                        // 0x7F

    { OF_NONE,  8, 0, 0, "add a,b" },                       // 0x80
    { OF_NONE,  8, 0, 0, "add a,c" },                       // 0x81
    { OF_NONE,  8, 0, 0, "add a,d" },                       // 0x82
    { OF_NONE,  8, 0, 0, "add a,e" },                       // 0x83
    { OF_NONE,  8, 0, 0, "add a,hx" },                      // 0x84
    { OF_NONE,  8, 0, 0, "add a,lx" },                      // 0x85
    { OF_DISP, 19, 0, 0, "add a,(ix+:1)" },                 // 0x86
    { OF_NONE,  8, 0, 0, "add a,a" },                       // 0x87
    { OF_NONE,  8, 0, 0, "adc a,b" },                       // 0x88
    { OF_NONE,  8, 0, 0, "adc a,c" },                       // 0x89
    { OF_NONE,  8, 0, 0, "adc a,d" },                       // 0x8A
    { OF_NONE,  8, 0, 0, "adc a,e" },                       // 0x8B
    { OF_NONE,  8, 0, 0, "adc a,hx" },                      // 0x8C
    { OF_NONE,  8, 0, 0, "adc a,lx" },                      // 0x8D
    { OF_DISP, 19, 0, 0, "adc a,(ix+:1)" },                 // 0x8E
    { OF_NONE,  8, 0, 0, "adc a,a" },                       // 0x8F

    { OF_NONE,  8, 0, 0, "sub b" },                         // 0x90
    { OF_NONE,  8, 0, 0, "sub c" },                         // 0x91
    { OF_NONE,  8, 0, 0, "sub d" },                         // 0x92
    { OF_NONE,  8, 0, 0, "sub e" },                         // 0x93
    { OF_NONE,  8, 0, 0, "sub hx" },                        // 0x94
    { OF_NONE,  8, 0, 0, "sub lx" },                        // 0x95
    { OF_DISP, 19, 0, 0, "sub (ix+:1)" },                   // 0x96
    { OF_NONE,  8, 0, 0, "sub a" },                         // 0x97
    { OF_NONE,  8, 0, 0, "sbc a,b" },                       // 0x98
    { OF_NONE,  8, 0, 0, "sbc a,c" },                       // 0x99
    { OF_NONE,  8, 0, 0, "sbc a,d" },                       // 0x9A
    { OF_NONE,  8, 0, 0, "sbc a,e" },                       // 0x9B
    { OF_NONE,  8, 0, 0, "sbc a,hx" },                      // 0x9C
    { OF_NONE,  8, 0, 0, "sbc a,lx" },                      // 0x9D
    { OF_DISP, 19, 0, 0, "sbc (ix+:1)" },                   // 0x9E
    { OF_NONE,  8, 0, 0, "sbc a,a" },                       // 0x9F

    { OF_NONE,  8, 0, 0, "and b" },                         // 0xA0
    { OF_NONE,  8, 0, 0, "and c" },                         // 0xA1
    { OF_NONE,  8, 0, 0, "and d" },                         // 0xA2
    { OF_NONE,  8, 0, 0, "and e" },                         // 0xA3
    { OF_NONE,  8, 0, 0, "and hx" },                        // 0xA4
    { OF_NONE,  8, 0, 0, "and lx" },                        // 0xA5
    { OF_DISP, 19, 0, 0, "and (ix+:1)" },                   // 0xA6
    { OF_NONE,  8, 0, 0, "and a" },                         // 0xA7
    { OF_NONE,  8, 0, 0, "xor b" },                         // 0xA8
    { OF_NONE,  8, 0, 0, "xor c" },                         // 0xA9
    { OF_NONE,  8, 0, 0, "xor d" },                         // 0xAA
    { OF_NONE,  8, 0, 0, "xor e" },                         // 0xAB
    { OF_NONE,  8, 0, 0, "xor hx" },                        // 0xAC
    { OF_NONE,  8, 0, 0, "xor lx" },                        // 0xAD
    { OF_DISP, 19, 0, 0, "xor (ix+:1)" },                   // 0xAE
    { OF_NONE,  8, 0, 0, "xor a" },                         // 0xAF

    { OF_NONE,  8, 0, 0, "or b" },                          // 0xB0
    { OF_NONE,  8, 0, 0, "or c" },                          // 0xB1
    { OF_NONE,  8, 0, 0, "or d" },                          // 0xB2
    { OF_NONE,  8, 0, 0, "or e" },                          // 0xB3
    { OF_NONE,  8, 0, 0, "or hx" },                         // 0xB4
    { OF_NONE,  8, 0, 0, "or lx" },                         // 0xB5
    { OF_DISP, 19, 0, 0, "or (ix+:1)" },                    // 0xB6
    { OF_NONE,  8, 0, 0, "or a" },                          // 0xB7
    { OF_NONE,  8, 0, 0, "cp b" },                          // 0xB8
    { OF_NONE,  8, 0, 0, "cp c" },                          // 0xB9
    { OF_NONE,  8, 0, 0, "cp d" },                          // 0xBA
    { OF_NONE,  8, 0, 0, "cp e" },                          // 0xBB
    { OF_NONE,  8, 0, 0, "cp hx" },                         // 0xBC
    { OF_NONE,  8, 0, 0, "cp lx" },                         // 0xBD
    { OF_DISP, 19, 0, 0, "cp (ix+:1)" },                    // 0xBE
    { OF_NONE,  8, 0, 0, "cp a" },                          // 0xBF

    { OF_CONDITION,  0, 15, 9, "ret nz" },                  // 0xC0
    { OF_NONE, 14, 0, 0, "pop bc" },                        // 0xC1
    { OF_CONDITION | OF_MWORD,  0, 14, 14, "jp nz,:2" },    // 0xC2
    { OF_MWORD, 14, 0, 0, "jp.:2" },                        // 0xC3
    { OF_CONDITION |  OF_MWORD, 0, 21, 14, "call nz,:2" },  // 0xC4
    { OF_NONE, 15, 0, 0, "push bc" },                       // 0xC5
    { OF_MBYTE,  11, 0, 0, "add a,:1" },                    // 0xC6
    { OF_NONE, 15, 0, 0, "rst #00" },                       // 0xC7
    { OF_CONDITION, 0, 15, 9, "ret z" },                    // 0xC8
    { OF_NONE, 14, 0, 0, "ret" },                           // 0xC9
    { OF_CONDITION |  OF_MWORD, 0, 14, 14, "jp z,:2" },     // 0xCA
    { OF_PREFIX,  8, 0, 0, "#CB" },                         // 0xCB - Prefix
    { OF_CONDITION | OF_MWORD | OF_JUMP, 0, 21, 14, "call z,:2" },    // 0xCC
    { OF_MWORD, 21, 0, 0, "call :2" },                      // 0xCD
    { OF_MBYTE, 11, 0, 0, "adc a,:1" },                     // 0xCE
    { OF_NONE, 15, 0, 0, "rst #08" },                       // 0xCF

    { OF_CONDITION,  0, 15, 9, "ret nc" },                  // 0xD0
    { OF_NONE, 14, 0, 0, "pop de" },                        // 0xD1
    { OF_CONDITION | OF_MWORD,  0, 14, 14, "jp nc,:2" },    // 0xD2
    { OF_MBYTE, 15, 0, 0, "out (:1),a" },                   // 0xD3
    { OF_CONDITION | OF_MWORD, 0, 21, 14, "call nc,:2" },   // 0xD4
    { OF_NONE, 15, 0, 0, "push de" },                       // 0xD5
    { OF_MBYTE, 11, 0, 0, "sub :1" },                       // 0xD6
    { OF_NONE, 15, 0, 0, "rst #10" },                       // 0xD7
    { OF_CONDITION,  0, 15, 9, "ret c" },                   // 0xD8
    { OF_NONE,  8, 0, 0, "exx" },                           // 0xD9
    { OF_CONDITION | OF_MWORD,  0, 14, 14, "jp c,:2" },     // 0xDA
    { OF_MBYTE, 15, 0, 0, "in a,(:1)" },                    // 0xDB
    { OF_CONDITION |  OF_MWORD, 0, 21, 14, "call c,:2" },   // 0xDC
    { OF_PREFIX, 8, 0, 0, "#DD" },                          // 0xDD - Prefix
    { OF_MBYTE, 11, 0, 0, "sbc a,:1" },                     // 0xDE
    { OF_NONE, 15, 0, 0, "rst #18" },                       // 0xDF

    { OF_CONDITION,  0, 15, 9, "ret po" },                  // 0xE0
    { OF_NONE, 14, 0, 0, "pop ix" },                        // 0xE1
    { OF_CONDITION | OF_MWORD,  0, 14, 14, "jp po,:2" },    // 0xE2
    { OF_NONE, 23, 0, 0, "ex (sp),ix" },                    // 0xE3
    { OF_CONDITION | OF_MWORD,  0, 21, 14, "call po,:2" },  // 0xE4
    { OF_NONE, 15, 0, 0, "push ix" },                       // 0xE5
    { OF_MBYTE,  8, 0, 0, "and :1" },                       // 0xE6
    { OF_NONE, 15, 0, 0, "rst #20" },                       // 0xE7
    { OF_CONDITION,  0, 15, 9, "ret pe" },                  // 0xE8
    { OF_NONE,  8, 0, 0, "jp (ix)" },                       // 0xE9
    { OF_CONDITION | OF_MWORD,  0, 14, 14, "jp pe,:2" },    // 0xEA
    { OF_NONE,  4, 0, 0, "ex de,hl" },                      // 0xEB
    { OF_CONDITION | OF_MWORD, 0, 17, 10, "call pe,:2" },   // 0xEC
    { OF_PREFIX,  4, 0, 0, "#ED" },                         // 0xED - Prefix
    { OF_MBYTE,  7, 0, 0, "xor :1" },                       // 0xEE
    { OF_NONE, 11, 0, 0, "rst #28" },                       // 0xEF

    { OF_CONDITION,  0, 15, 9, "ret p" },                   // 0xF0
    { OF_NONE, 14, 0, 0, "pop af" },                        // 0xF1
    { OF_CONDITION | OF_MWORD,  0, 14, 14, "jp p,:2" },     // 0xF2
    { OF_NONE,  8, 0, 0, "di" },                            // 0xF3
    { OF_CONDITION | OF_MWORD,  0, 21, 14, "call p,:2" },   // 0xF4
    { OF_NONE, 15, 0, 0, "push af" },                       // 0xF5
    { OF_MBYTE, 11, 0, 0, "or :1" },                        // 0xF6
    { OF_NONE, 15, 0, 0, "rst #30" },                       // 0xF7
    { OF_CONDITION,  0, 15, 9, "ret m" },                   // 0xF8
    { OF_NONE, 10, 0, 0, "ld sp,ix" },                      // 0xF9
    { OF_CONDITION | OF_MWORD, 0, 14, 14, "jp m,:2"},       // 0xFA
    { OF_NONE,  8, 0, 0, "ei" },                            // 0xFB
    { OF_MWORD,  0, 21, 15, "call m,:2" },                  // 0xFC
    { OF_PREFIX,  8, 0, 0, "#FD" },                         // 0xFD - Prefix
    { OF_MBYTE, 11, 0, 0, "cp :1" },                        // 0xFE
    { OF_NONE, 15, 0, 0, "rst #38" },                       // 0xFF
};

/// endregion </#DD prefix opcodes>

/// region <#ED prefix opcodes>

OpCode Z80Disassembler::edOpcodes[256] =
{
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x00
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x01
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x02
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x03
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x04
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x05
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x06
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x07
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x08
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x09
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x0A
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x0B
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x0C
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x0D
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x0E
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x0F

    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x10
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x11
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x12
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x13
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x14
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x15
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x16
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x17
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x18
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x19
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x1A
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x1B
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x1C
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x1D
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x1E
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x1F

    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x20
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x21
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x22
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x23
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x24
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x25
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x26
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x27
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x28
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x29
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x2A
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x2B
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x2C
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x2D
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x2E
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x2F

    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x30
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x31
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x32
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x33
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x34
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x35
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x36
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x37
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x38
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x39
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x3A
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x3B
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x3C
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x3D
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x3E
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x3F

    { OF_NONE, 12, 0, 0, "in b,(c)" },                      // 0x40
    { OF_NONE, 12, 0, 0, "out (c),b" },                     // 0x41
    { OF_NONE, 15, 0, 0, "sbc hl,bc" },                     // 0x42
    { OF_MWORD, 20, 0, 0, "ld (:2),bc" },                   // 0x43
    { OF_NONE,  8, 0, 0, "neg" },                           // 0x44
    { OF_NONE, 14, 0, 0, "retn" },                          // 0x45
    { OF_NONE,  8, 0, 0, "im 0" },                          // 0x46
    { OF_NONE,  9, 0, 0, "ld i,a" },                        // 0x47
    { OF_NONE, 12, 0, 0, "in c,(c)" },                      // 0x48
    { OF_NONE, 12, 0, 0, "out (c),c" },                     // 0x49
    { OF_NONE, 15, 0, 0, "adc hl,bc" },                     // 0x4A
    { OF_MWORD, 20, 0, 0, "ld bc,(:2)" },                   // 0x4B
    { OF_NONE,  8, 0, 0, "neg *" },                         // 0x4C
    { OF_NONE, 14, 0, 0, "reti" },                          // 0x4D
    { OF_NONE,  8, 0, 0, "im 0 *" },                        // 0x4E
    { OF_NONE,  9, 0, 0, "ld r,a" },                        // 0x4F

    { OF_NONE, 12, 0, 0, "in d,(c)" },                      // 0x50
    { OF_NONE, 12, 0, 0, "out (c),d" },                     // 0x51
    { OF_NONE, 15, 0, 0, "sbc hl,de" },                     // 0x52
    { OF_MWORD, 20, 0, 0, "ld (:2),de" },                   // 0x53
    { OF_NONE,  8, 0, 0, "neg *" },                         // 0x54
    { OF_NONE, 14, 0, 0, "retn *" },                        // 0x55
    { OF_NONE,  8, 0, 0, "im 1" },                          // 0x56
    { OF_NONE,  9, 0, 0, "ld a,i" },                        // 0x57
    { OF_NONE, 12, 0, 0, "in e,(c)" },                      // 0x58
    { OF_NONE, 12, 0, 0, "out (c),e" },                     // 0x59
    { OF_NONE, 12, 0, 0, "adc hl,de" },                     // 0x5A
    { OF_MWORD, 20, 0, 0, "ld de,(:2)" },                   // 0x5B
    { OF_NONE,  8, 0, 0, "neg *" },                         // 0x5C
    { OF_NONE, 14, 0, 0, "reti *" },                        // 0x5D
    { OF_NONE,  8, 0, 0, "im 2" },                          // 0x5E
    { OF_NONE,  9, 0, 0, "ld a,r" },                        // 0x5F

    { OF_NONE, 12, 0, 0, "in h,(c)" },                      // 0x60
    { OF_NONE, 12, 0, 0, "out (c),h" },                     // 0x61
    { OF_NONE, 15, 0, 0, "sbc hl,hl" },                     // 0x62
    { OF_MWORD, 20, 0, 0, "ld (:2),hl" },                   // 0x63
    { OF_NONE,  8, 0, 0, "neg *" },                         // 0x64
    { OF_NONE, 14, 0, 0, "retn *" },                        // 0x65
    { OF_NONE,  8, 0, 0, "im 0 *" },                        // 0x66
    { OF_NONE, 18, 0, 0, "rrd" },                           // 0x67
    { OF_NONE, 12, 0, 0, "in l,(c)" },                      // 0x68
    { OF_NONE, 12, 0, 0, "out (c),l" },                     // 0x69
    { OF_NONE, 15, 0, 0, "adc hl,hl" },                     // 0x6A
    { OF_MWORD, 20, 0, 0, "ld hl,(:2)" },                   // 0x6B
    { OF_NONE,  8, 0, 0, "neg *" },                         // 0x6C
    { OF_NONE, 14, 0, 0, "reti *" },                        // 0x6D
    { OF_NONE,  8, 0, 0, "im 0 *" },                        // 0x6E
    { OF_NONE, 18, 0, 0, "rld" },                           // 0x6F

    { OF_NONE, 12, 0, 0, "in (c) *" },                      // 0x70
    { OF_NONE, 12, 0, 0, "out (c),0" },                     // 0x71
    { OF_NONE, 15, 0, 0, "sbc hl,sp" },                     // 0x72
    { OF_MWORD, 20, 0, 0, "ld (:2),sp" },                   // 0x73
    { OF_NONE,  8, 0, 0, "neg *" },                         // 0x74
    { OF_NONE, 14, 0, 0, "retn *" },                        // 0x75
    { OF_NONE,  8, 0, 0, "im 1 *" },                        // 0x76
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x77
    { OF_NONE, 12, 0, 0, "in a,(c)" },                      // 0x78
    { OF_NONE, 12, 0, 0, "out (c),a" },                     // 0x79
    { OF_NONE, 15, 0, 0, "adc hl,sp" },                     // 0x7A
    { OF_MWORD, 20, 0, 0, "ld sp,(:2)" },                   // 0x7B
    { OF_NONE,  8, 0, 0, "neg *" },                         // 0x7C
    { OF_NONE, 14, 0, 0, "reti *" },                        // 0x7D
    { OF_NONE,  8, 0, 0, "im 2 *" },                        // 0x7E
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x7F

    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x80
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x81
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x82
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x83
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x84
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x85
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x86
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x87
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x88
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x89
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x8A
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x8B
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x8C
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x8D
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x8E
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x8F

    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x90
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x91
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x92
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x93
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x94
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x95
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x96
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x97
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x98
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x99
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x9A
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x9B
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x9C
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x9D
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x9E
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0x9F

    { OF_NONE, 16, 0, 0, "ldi" },                           // 0xA0
    { OF_NONE, 16, 0, 0, "cpi" },                           // 0xA1
    { OF_NONE, 16, 0, 0, "ini" },                           // 0xA2
    { OF_NONE, 16, 0, 0, "outi" },                          // 0xA3
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xA4
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xA5
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xA6
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xA7
    { OF_NONE, 16, 0, 0, "ldd" },                           // 0xA8
    { OF_NONE, 16, 0, 0, "cpd" },                           // 0xA9
    { OF_NONE, 16, 0, 0, "ind" },                           // 0xAA
    { OF_NONE, 16, 0, 0, "outd" },                          // 0xAB
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xAC
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xAD
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xAE
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xAF

    { OF_VAR_T,  0, 16, 21, "ldir" },                       // 0xB0
    { OF_VAR_T,  0, 16, 21, "cpir" },                       // 0xB1
    { OF_VAR_T,  0, 16, 21, "inir" },                       // 0xB2
    { OF_VAR_T,  0, 16, 21, "otir" },                       // 0xB3
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xB4
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xB5
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xB6
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xB7
    { OF_VAR_T,  0, 16, 21, "lddr" },                       // 0xB8
    { OF_VAR_T,  0, 16, 21, "cpdr" },                       // 0xB9
    { OF_VAR_T,  0, 16, 21, "indr" },                       // 0xBA
    { OF_VAR_T,  0, 16, 21, "otdr" },                       // 0xBB
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xBC
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xBD
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xBE
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xBF

    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xC0
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xC1
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xC2
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xC3
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xC4
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xC5
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xC6
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xC7
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xC8
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xC9
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xCA
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xCB
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xCC
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xCD
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xCE
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xCF

    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xD0
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xD1
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xD2
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xD3
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xD4
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xD5
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xD6
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xD7
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xD8
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xD9
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xDA
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xDB
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xDC
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xDD
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xDE
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xDF

    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xE0
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xE1
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xE2
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xE3
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xE4
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xE5
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xE6
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xE7
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xE8
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xE9
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xEA
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xEB
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xEC
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xED
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xEE
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xEF

    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xF0
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xF1
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xF2
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xF3
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xF4
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xF5
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xF6
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xF7
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xF8
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xF9
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xFA
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xFB
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xFC
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xFD
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xFE
    { OF_NONE,  8, 0, 0, "nop *" },                         // 0xFF
};

/// endregion </#ED prefix opcodes>

/// region <#FD prefix opcodes>

OpCode Z80Disassembler::fdOpcodes[256]
{
    { OF_NONE,  8, 0, 0, "nop" },                           // 0x00
    { OF_MWORD, 14, 0, 0, "ld bc,:2" },                     // 0x01
    { OF_NONE, 11, 0, 0, "ld bc,(a)" },                     // 0x02
    { OF_NONE, 10, 0, 0, "inc bc" },                        // 0x03
    { OF_NONE,  8, 0, 0, "inc b" },                         // 0x04
    { OF_NONE,  8, 0, 0, "dec b" },                         // 0x05
    { OF_MBYTE, 11, 0, 0, "ld b,:1" },                      // 0x06
    { OF_NONE,  8, 0, 0, "rlca" },                          // 0x07
    { OF_NONE,  8, 0, 0, "ex af,af'" },                     // 0x08
    { OF_NONE, 15, 0, 0, "add iy,bc" },                     // 0x09
    { OF_NONE, 11, 0, 0, "ld a,(bc)" },                     // 0x0A
    { OF_NONE, 10, 0, 0, "dec bc" },                        // 0x0B
    { OF_NONE,  8, 0, 0, "inc c" },                         // 0x0C
    { OF_NONE,  8, 0, 0, "dec c" },                         // 0x0D
    { OF_MBYTE, 11, 0, 0, "ld c,:1" },                      // 0x0E
    { OF_NONE,  8, 0, 0, "rrca" },                          // 0x0F

    { OF_CONDITION | OF_RELJUMP, 0, 17, 12, "djnz :1" },    // 0x10
    { OF_NONE, 14, 0, 0, "ld de,:2" },                      // 0x11
    { OF_NONE, 11, 0, 0, "ld (de),:2" },                    // 0x12
    { OF_NONE, 10, 0, 0, "inc de" },                        // 0x13
    { OF_NONE,  8, 0, 0, "inc d" },                         // 0x14
    { OF_NONE,  8, 0, 0, "dec d" },                         // 0x15
    { OF_MBYTE, 11, 0, 0, "ld d,:1" },                      // 0x16
    { OF_NONE,  8, 0, 0, "rla" },                           // 0x17
    { OF_RELJUMP | OF_MBYTE,  16, 0, 0, "jr :1" },          // 0x18
    { OF_NONE,  15, 0, 0, "add iy,de" },                    // 0x19
    { OF_NONE,  11, 0, 0, "ld a,(de)" },                    // 0x1A
    { OF_NONE,  10, 0, 0, "dec de" },                       // 0x1B
    { OF_NONE,   8, 0, 0, "inc e" },                        // 0x1C
    { OF_NONE,   8, 0, 0, "dec e" },                        // 0x1D
    { OF_MBYTE,  11, 0, 0, "ld e,:1" },                     // 0x1E
    { OF_NONE,   8, 0, 0, "rra" },                          // 0x1F

    { OF_CONDITION | OF_RELJUMP | OF_MBYTE, 0, 16, 11, "jr nz,:1" },   // 0x20
    { OF_NONE, 18, 0, 0, "ld iy,:2" },                      // 0x21
    { OF_NONE, 24, 0, 0, "ld (:2),iy" },                    // 0x22
    { OF_NONE, 14, 0, 0, "inc ix" },                        // 0x23
    { OF_NONE,  8, 0, 0, "inc hy" },                        // 0x24
    { OF_NONE,  8, 0, 0, "dec hy" },                        // 0x25
    { OF_MBYTE, 11, 0, 0, "ld hy,:1" },                     // 0x26
    { OF_NONE,  8, 0, 0, "daa" },                           // 0x27
    { OF_CONDITION | OF_RELJUMP | OF_MBYTE, 0, 16, 11, "jr z,:1" },    // 0x28
    { OF_NONE, 15, 0, 0, "add iy,iy" },                     // 0x29
    { OF_MWORD, 20, 0, 0, "ld iy,(:2)" },                   // 0x2A
    { OF_NONE, 14, 0, 0, "dec iy" },                        // 0x2B
    { OF_NONE,  8, 0, 0, "inc ly" },                        // 0x2C
    { OF_NONE,  8, 0, 0, "dec ly" },                        // 0x2D
    { OF_MBYTE, 11, 0, 0, "ld ly,:1" },                     // 0x2E
    { OF_NONE,  8, 0, 0, "cpl" },                           // 0x2F

    { OF_CONDITION | OF_RELJUMP | OF_MBYTE, 0, 16, 11, "jr nc,:1" },   // 0x30
    { OF_MWORD, 11, 0, 0, "ld sp,:2" },                     // 0x31
    { OF_MWORD, 17, 0, 0, "ld (:2),a" },                    // 0x32
    { OF_NONE, 10, 0, 0, "inc sp" },                        // 0x33
    { OF_DISP, 19, 0, 0, "inc (iy+:1)" },                   // 0x34
    { OF_DISP, 19, 0, 0, "dec (iy+:1)" },                   // 0x35
    { OF_DISP | OF_MBYTE, 15, 0, 0, "ld (iy+:1),:1" },      // 0x36
    { OF_NONE,  8, 0, 0, "scf" },                           // 0x37
    { OF_CONDITION | OF_RELJUMP | OF_MBYTE, 0, 16, 11, "jr c,:1" },    // 0x38
    { OF_NONE, 15, 0, 0, "add iy,sp" },                     // 0x39
    { OF_MWORD, 17, 0, 0, "ld a,(:2)" },                    // 0x3A
    { OF_NONE, 10, 0, 0, "dec sp" },                        // 0x3B
    { OF_NONE,  8, 0, 0, "inc a" },                         // 0x3C
    { OF_NONE,  8, 0, 0, "dec a" },                         // 0x3D
    { OF_MBYTE, 11, 0, 0, "ld a,:1" },                      // 0x3E
    { OF_NONE,  8, 0, 0, "ccf" },                           // 0x3F

    { OF_NONE,  8, 0, 0, "ld b,b" },                        // 0x40
    { OF_NONE,  8, 0, 0, "ld b,c" },                        // 0x41
    { OF_NONE,  8, 0, 0, "ld b,d" },                        // 0x42
    { OF_NONE,  8, 0, 0, "ld b,e" },                        // 0x43
    { OF_NONE,  8, 0, 0, "ld b,hy" },                       // 0x44
    { OF_NONE,  8, 0, 0, "ld b,ly" },                       // 0x45
    { OF_DISP, 19, 0, 0, "ld b,(iy+:1)" },                  // 0x46
    { OF_NONE,  8, 0, 0, "ld b,a" },                        // 0x47
    { OF_NONE,  8, 0, 0, "ld c,b" },                        // 0x48
    { OF_NONE,  8, 0, 0, "ld c,c" },                        // 0x49
    { OF_NONE,  8, 0, 0, "ld c,d" },                        // 0x4A
    { OF_NONE,  8, 0, 0, "ld c,e" },                        // 0x4B
    { OF_NONE,  8, 0, 0, "ld c,hy" },                       // 0x4C
    { OF_NONE,  8, 0, 0, "lc c,ly" },                       // 0x4D
    { OF_DISP, 19, 0, 0, "lc c,(iy+:1)" },                  // 0x4E
    { OF_NONE,  8, 0, 0, "ld c,a" },                        // 0x4F

    { OF_NONE,  8, 0, 0, "ld d,b" },                        // 0x50
    { OF_NONE,  8, 0, 0, "ld d,c" },                        // 0x51
    { OF_NONE,  8, 0, 0, "ld d,d" },                        // 0x52
    { OF_NONE,  8, 0, 0, "ld d,e" },                        // 0x53
    { OF_NONE,  8, 0, 0, "ld d,hy" },                       // 0x54
    { OF_NONE,  8, 0, 0, "ld d,ly" },                       // 0x55
    { OF_DISP, 19, 0, 0, "ld d,(iy+:1)" },                  // 0x56
    { OF_NONE,  8, 0, 0, "ld d,a" },                        // 0x57
    { OF_NONE,  8, 0, 0, "ld e,b" },                        // 0x58
    { OF_NONE,  8, 0, 0, "ld e,c" },                        // 0x59
    { OF_NONE,  8, 0, 0, "ld e,d" },                        // 0x5A
    { OF_NONE,  8, 0, 0, "ld e,e" },                        // 0x5B
    { OF_NONE,  8, 0, 0, "ld e,hy" },                       // 0x5C
    { OF_NONE,  8, 0, 0, "ld e,ly" },                       // 0x5D
    { OF_DISP, 19, 0, 0, "ld e,(iy+:1)" },                  // 0x5E
    { OF_NONE,  8, 0, 0, "ld e,a" },                        // 0x5F

    { OF_NONE,  8, 0, 0, "ld hy,b" },                       // 0x60
    { OF_NONE,  8, 0, 0, "ld hy,c" },                       // 0x61
    { OF_NONE,  8, 0, 0, "ld hy,d" },                       // 0x62
    { OF_NONE,  8, 0, 0, "ld hy,e" },                       // 0x63
    { OF_NONE,  8, 0, 0, "ld hy,hy" },                      // 0x64
    { OF_NONE,  8, 0, 0, "ld hy,ly" },                      // 0x65
    { OF_DISP, 19, 0, 0, "ld h,(iy+:1)" },                  // 0x66
    { OF_NONE,  8, 0, 0, "ld hy,a" },                       // 0x67
    { OF_NONE,  8, 0, 0, "ld ly,b" },                       // 0x68
    { OF_NONE,  8, 0, 0, "ld ly,c" },                       // 0x69
    { OF_NONE,  8, 0, 0, "ld ly,d" },                       // 0x6A
    { OF_NONE,  8, 0, 0, "ld ly,e" },                       // 0x6B
    { OF_NONE,  8, 0, 0, "ld ly,hy" },                      // 0x6C
    { OF_NONE,  8, 0, 0, "ld ly,ly" },                      // 0x6D
    { OF_DISP, 19, 0, 0, "ld l,(iy+:1)" },                  // 0x6E
    { OF_NONE,  8, 0, 0, "ld ly,a" },                       // 0x6F

    { OF_DISP, 19, 0, 0, "ld (iy+:1),b" },                  // 0x70
    { OF_DISP, 19, 0, 0, "ld (iy+:1),c" },                  // 0x71
    { OF_DISP, 19, 0, 0, "ld (iy+:1),d" },                  // 0x72
    { OF_DISP, 19, 0, 0, "ld (iy+:1),e" },                  // 0x73
    { OF_DISP, 19, 0, 0, "ld (iy+:1),h" },                  // 0x74
    { OF_DISP, 19, 0, 0, "ld (iy+:1),l" },                  // 0x75
    { OF_NONE,  8, 0, 0, "halt" },                          // 0x76
    { OF_DISP, 19, 0, 0, "ld (iy+:1),a" },                  // 0x77
    { OF_NONE,  8, 0, 0, "ld a,b" },                        // 0x78
    { OF_NONE,  8, 0, 0, "ld a,c" },                        // 0x79
    { OF_NONE,  8, 0, 0, "ld a,d" },                        // 0x7A
    { OF_NONE,  8, 0, 0, "ld a,e" },                        // 0x7B
    { OF_NONE,  8, 0, 0, "ld a,hy" },                       // 0x7C
    { OF_NONE,  8, 0, 0, "ld a,ly" },                       // 0x7D
    { OF_DISP, 19, 0, 0, "ld a,(iy+:1)" },                  // 0x7E
    { OF_NONE,  8, 0, 0, "ld a,a" },                        // 0x7F

    { OF_NONE,  8, 0, 0, "add a,b" },                       // 0x80
    { OF_NONE,  8, 0, 0, "add a,c" },                       // 0x81
    { OF_NONE,  8, 0, 0, "add a,d" },                       // 0x82
    { OF_NONE,  8, 0, 0, "add a,e" },                       // 0x83
    { OF_NONE,  8, 0, 0, "add a,hy" },                      // 0x84
    { OF_NONE,  8, 0, 0, "add a,ly" },                      // 0x85
    { OF_DISP, 19, 0, 0, "add a,(iy+:1)" },                 // 0x86
    { OF_NONE,  8, 0, 0, "add a,a" },                       // 0x87
    { OF_NONE,  8, 0, 0, "adc a,b" },                       // 0x88
    { OF_NONE,  8, 0, 0, "adc a,c" },                       // 0x89
    { OF_NONE,  8, 0, 0, "adc a,d" },                       // 0x8A
    { OF_NONE,  8, 0, 0, "adc a,e" },                       // 0x8B
    { OF_NONE,  8, 0, 0, "adc a,hy" },                      // 0x8C
    { OF_NONE,  8, 0, 0, "adc a,ly" },                      // 0x8D
    { OF_DISP, 19, 0, 0, "adc a,(iy+:1)" },                 // 0x8E
    { OF_NONE,  8, 0, 0, "adc a,a" },                       // 0x8F

    { OF_NONE,  8, 0, 0, "sub b" },                         // 0x90
    { OF_NONE,  8, 0, 0, "sub c" },                         // 0x91
    { OF_NONE,  8, 0, 0, "sub d" },                         // 0x92
    { OF_NONE,  8, 0, 0, "sub e" },                         // 0x93
    { OF_NONE,  8, 0, 0, "sub hy" },                        // 0x94
    { OF_NONE,  8, 0, 0, "sub ly" },                        // 0x95
    { OF_DISP, 19, 0, 0, "sub (iy+:1)" },                   // 0x96
    { OF_NONE,  8, 0, 0, "sub a" },                         // 0x97
    { OF_NONE,  8, 0, 0, "sbc a,b" },                       // 0x98
    { OF_NONE,  8, 0, 0, "sbc a,c" },                       // 0x99
    { OF_NONE,  8, 0, 0, "sbc a,d" },                       // 0x9A
    { OF_NONE,  8, 0, 0, "sbc a,e" },                       // 0x9B
    { OF_NONE,  8, 0, 0, "sbc a,hy" },                      // 0x9C
    { OF_NONE,  8, 0, 0, "sbc a,ly" },                      // 0x9D
    { OF_DISP, 19, 0, 0, "sbc (iy+:1)" },                   // 0x9E
    { OF_NONE,  8, 0, 0, "sbc a,a" },                       // 0x9F

    { OF_NONE,  8, 0, 0, "and b" },                         // 0xA0
    { OF_NONE,  8, 0, 0, "and c" },                         // 0xA1
    { OF_NONE,  8, 0, 0, "and d" },                         // 0xA2
    { OF_NONE,  8, 0, 0, "and e" },                         // 0xA3
    { OF_NONE,  8, 0, 0, "and hy" },                        // 0xA4
    { OF_NONE,  8, 0, 0, "and ly" },                        // 0xA5
    { OF_DISP, 19, 0, 0, "and (iy+:1)" },                   // 0xA6
    { OF_NONE,  8, 0, 0, "and a" },                         // 0xA7
    { OF_NONE,  8, 0, 0, "xor b" },                         // 0xA8
    { OF_NONE,  8, 0, 0, "xor c" },                         // 0xA9
    { OF_NONE,  8, 0, 0, "xor d" },                         // 0xAA
    { OF_NONE,  8, 0, 0, "xor e" },                         // 0xAB
    { OF_NONE,  8, 0, 0, "xor hy" },                        // 0xAC
    { OF_NONE,  8, 0, 0, "xor ly" },                        // 0xAD
    { OF_DISP, 19, 0, 0, "xor (iy+:1)" },                   // 0xAE
    { OF_NONE,  8, 0, 0, "xor a" },                         // 0xAF

    { OF_NONE,  8, 0, 0, "or b" },                          // 0xB0
    { OF_NONE,  8, 0, 0, "or c" },                          // 0xB1
    { OF_NONE,  8, 0, 0, "or d" },                          // 0xB2
    { OF_NONE,  8, 0, 0, "or e" },                          // 0xB3
    { OF_NONE,  8, 0, 0, "or hy" },                         // 0xB4
    { OF_NONE,  8, 0, 0, "or ly" },                         // 0xB5
    { OF_DISP, 19, 0, 0, "or (iy+:1)" },                    // 0xB6
    { OF_NONE,  8, 0, 0, "or a" },                          // 0xB7
    { OF_NONE,  8, 0, 0, "cp b" },                          // 0xB8
    { OF_NONE,  8, 0, 0, "cp c" },                          // 0xB9
    { OF_NONE,  8, 0, 0, "cp d" },                          // 0xBA
    { OF_NONE,  8, 0, 0, "cp e" },                          // 0xBB
    { OF_NONE,  8, 0, 0, "cp hy" },                         // 0xBC
    { OF_NONE,  8, 0, 0, "cp ly" },                         // 0xBD
    { OF_DISP, 19, 0, 0, "cp (iy+:1)" },                    // 0xBE
    { OF_NONE,  8, 0, 0, "cp a" },                          // 0xBF

    { OF_CONDITION,  0, 15, 9, "ret nz" },                  // 0xC0
    { OF_NONE, 14, 0, 0, "pop bc" },                        // 0xC1
    { OF_CONDITION | OF_MWORD,  0, 14, 14, "jp nz,:2" },    // 0xC2
    { OF_MWORD, 14, 0, 0, "jp.:2" },                        // 0xC3
    { OF_CONDITION |  OF_MWORD, 0, 21, 14, "call nz,:2" },  // 0xC4
    { OF_NONE, 15, 0, 0, "push bc" },                       // 0xC5
    { OF_MBYTE,  11, 0, 0, "add a,:1" },                    // 0xC6
    { OF_NONE, 15, 0, 0, "rst #00" },                       // 0xC7
    { OF_CONDITION, 0, 15, 9, "ret z" },                    // 0xC8
    { OF_NONE, 14, 0, 0, "ret" },                           // 0xC9
    { OF_CONDITION |  OF_MWORD, 0, 14, 14, "jp z,:2" },     // 0xCA
    { OF_PREFIX,  8, 0, 0, "#CB" },                         // 0xCB - Prefix
    { OF_CONDITION |  OF_MWORD, 0, 21, 14, "call z,:2" },   // 0xCC
    { OF_MWORD, 21, 0, 0, "call :2" },                      // 0xCD
    { OF_MBYTE, 11, 0, 0, "adc a,:1" },                     // 0xCE
    { OF_NONE, 15, 0, 0, "rst #08" },                       // 0xCF

    { OF_CONDITION,  0, 15, 9, "ret nc" },                  // 0xD0
    { OF_NONE, 14, 0, 0, "pop de" },                        // 0xD1
    { OF_CONDITION | OF_MWORD,  0, 14, 14, "jp nc,:2" },    // 0xD2
    { OF_MBYTE, 15, 0, 0, "out (:1),a" },                   // 0xD3
    { OF_CONDITION | OF_MWORD, 0, 21, 14, "call nc,:2" },   // 0xD4
    { OF_NONE, 15, 0, 0, "push de" },                       // 0xD5
    { OF_MBYTE, 11, 0, 0, "sub :1" },                       // 0xD6
    { OF_NONE, 15, 0, 0, "rst #10" },                       // 0xD7
    { OF_CONDITION,  0, 15, 9, "ret c" },                   // 0xD8
    { OF_NONE,  8, 0, 0, "exx" },                           // 0xD9
    { OF_CONDITION |  OF_MWORD,  0, 14, 14, "jp c,:2" },    // 0xDA
    { OF_NONE, 15, 0, 0, "in a,(:1)" },                     // 0xDB
    { OF_CONDITION | OF_MWORD, 0, 21, 14, "call c,:2" },    // 0xDC
    { OF_PREFIX, 8, 0, 0, "#DD" },                          // 0xDD - Prefix
    { OF_MBYTE, 11, 0, 0, "sbc a,:1" },                     // 0xDE
    { OF_NONE, 15, 0, 0, "rst #18" },                       // 0xDF

    { OF_CONDITION,  0, 15, 9, "ret po" },                  // 0xE0
    { OF_NONE, 14, 0, 0, "pop iy" },                        // 0xE1
    { OF_CONDITION | OF_MWORD,  0, 14, 14, "jp po,:2" },    // 0xE2
    { OF_NONE, 23, 0, 0, "ex (sp),iy" },                    // 0xE3
    { OF_CONDITION | OF_MWORD,  0, 21, 14, "call po,:2" },  // 0xE4
    { OF_NONE, 15, 0, 0, "push iy" },                       // 0xE5
    { OF_MBYTE,  8, 0, 0, "and :1" },                       // 0xE6
    { OF_NONE, 15, 0, 0, "rst #20" },                       // 0xE7
    { OF_CONDITION,  0, 15, 9, "ret pe" },                  // 0xE8
    { OF_NONE,  8, 0, 0, "jp (iy)" },                       // 0xE9
    { OF_CONDITION | OF_MWORD,  0, 14, 14, "jp pe,:2" },    // 0xEA
    { OF_NONE,  4, 0, 0, "ex de,hl" },                      // 0xEB
    { OF_CONDITION | OF_MWORD, 0, 17, 10, "call pe,:2" },   // 0xEC
    { OF_PREFIX,  4, 0, 0, "#ED" },                         // 0xED - Prefix
    { OF_MBYTE,  7, 0, 0, "xor :1" },                       // 0xEE
    { OF_NONE, 11, 0, 0, "rst #28" },                       // 0xEF

    { OF_CONDITION,  0, 15, 9, "ret p" },                   // 0xF0
    { OF_NONE, 14, 0, 0, "pop af" },                        // 0xF1
    { OF_CONDITION | OF_MWORD,  0, 14, 14, "jp p,:2" },     // 0xF2
    { OF_NONE,  8, 0, 0, "di" },                            // 0xF3
    { OF_CONDITION | OF_MWORD,  0, 21, 14, "call p,:2" },   // 0xF4
    { OF_NONE, 15, 0, 0, "push af" },                       // 0xF5
    { OF_MBYTE, 11, 0, 0, "or :1" },                        // 0xF6
    { OF_NONE, 15, 0, 0, "rst #30" },                       // 0xF7
    { OF_CONDITION,  0, 15, 9, "ret m" },                   // 0xF8
    { OF_NONE, 10, 0, 0, "ld sp,iy" },                      // 0xF9
    { OF_CONDITION | OF_MWORD, 0, 14, 14, "jp m,:2" },      // 0xFA
    { OF_NONE,  8, 0, 0, "ei" },                            // 0xFB
    { OF_MWORD,  0, 21, 15, "call m,:2" },                  // 0xFC
    { OF_PREFIX,  8, 0, 0, "#FD" },                         // 0xFD - Prefix
    { OF_MBYTE, 11, 0, 0, "cp :1" },                        // 0xFE
    { OF_NONE, 15, 0, 0, "rst #38" },                       // 0xFF
};
/// endregion </#FD prefix opcodes>

/// region <#DDCB prefix opcodes>

OpCode Z80Disassembler::ddcbOpcodes[256]
{
    { OF_DISP, 23, 0, 0, "rlc (ix+:1),b" },                 // 0x00
    { OF_DISP, 23, 0, 0, "rlc (ix+:1),c" },                 // 0x01
    { OF_DISP, 23, 0, 0, "rlc (ix+:1),d" },                 // 0x02
    { OF_DISP, 23, 0, 0, "rlc (ix+:1),e" },                 // 0x03
    { OF_DISP, 23, 0, 0, "rlc (ix+:1),h" },                 // 0x04
    { OF_DISP, 23, 0, 0, "rlc (ix+:1),l" },                 // 0x05
    { OF_DISP, 23, 0, 0, "rlc (ix+:1)" },                   // 0x06
    { OF_DISP, 23, 0, 0, "rlc (ix+:1),a" },                 // 0x07
    { OF_DISP, 23, 0, 0, "rrc (ix+:1),b" },                 // 0x08
    { OF_DISP, 23, 0, 0, "rrc (ix+:1),c" },                 // 0x09
    { OF_DISP, 23, 0, 0, "rrc (ix+:1),d" },                 // 0x0A
    { OF_DISP, 23, 0, 0, "rrc (ix+:1),e" },                 // 0x0B
    { OF_DISP, 23, 0, 0, "rrc (ix+:1),h" },                 // 0x0C
    { OF_DISP, 23, 0, 0, "rrc (ix+:1),l" },                 // 0x0D
    { OF_DISP, 23, 0, 0, "rrc (ix+:1)" },                   // 0x0E
    { OF_DISP, 23, 0, 0, "rrc (ix+:1),a" },                 // 0x0F

    { OF_DISP, 23, 0, 0, "rl (ix+:1),b" },                  // 0x10
    { OF_DISP, 23, 0, 0, "rl (ix+:1),c" },                  // 0x11
    { OF_DISP, 23, 0, 0, "rl (ix+:1),d" },                  // 0x12
    { OF_DISP, 23, 0, 0, "rl (ix+:1),e" },                  // 0x13
    { OF_DISP, 23, 0, 0, "rl (ix+:1),h" },                  // 0x14
    { OF_DISP, 23, 0, 0, "rl (ix+:1),l" },                  // 0x15
    { OF_DISP, 23, 0, 0, "rl (ix+:1)" },                    // 0x16
    { OF_DISP, 23, 0, 0, "rl (ix+:1),a" },                  // 0x17
    { OF_DISP, 23, 0, 0, "rr (ix+:1),b" },                  // 0x18
    { OF_DISP, 23, 0, 0, "rr (ix+:1),c" },                  // 0x19
    { OF_DISP, 23, 0, 0, "rr (ix+:1),d" },                  // 0x1A
    { OF_DISP, 23, 0, 0, "rr (ix+:1),e" },                  // 0x1B
    { OF_DISP, 23, 0, 0, "rr (ix+:1),h" },                  // 0x1C
    { OF_DISP, 23, 0, 0, "rr (ix+:1),l" },                  // 0x1D
    { OF_DISP, 23, 0, 0, "rr (ix+:1)" },                    // 0x1E
    { OF_DISP, 23, 0, 0, "rr (ix+:1),a" },                  // 0x1F

    { OF_DISP, 23, 0, 0, "sla (ix+:1),b" },                 // 0x20
    { OF_DISP, 23, 0, 0, "sla (ix+:1),c" },                 // 0x21
    { OF_DISP, 23, 0, 0, "sla (ix+:1),d" },                 // 0x22
    { OF_DISP, 23, 0, 0, "sla (ix+:1),e" },                 // 0x23
    { OF_DISP, 23, 0, 0, "sla (ix+:1),h" },                 // 0x24
    { OF_DISP, 23, 0, 0, "sla (ix+:1),l" },                 // 0x25
    { OF_DISP, 23, 0, 0, "sla (ix+:1)" },                   // 0x26
    { OF_DISP, 23, 0, 0, "sla (ix+:1),a" },                 // 0x27
    { OF_DISP, 23, 0, 0, "sra (ix+:1),b" },                 // 0x28
    { OF_DISP, 23, 0, 0, "sra (ix+:1),c" },                 // 0x29
    { OF_DISP, 23, 0, 0, "sra (ix+:1),d" },                 // 0x2A
    { OF_DISP, 23, 0, 0, "sra (ix+:1),e" },                 // 0x2B
    { OF_DISP, 23, 0, 0, "sra (ix+:1),h" },                 // 0x2C
    { OF_DISP, 23, 0, 0, "sra (ix+:1),l" },                 // 0x2D
    { OF_DISP, 23, 0, 0, "sra (ix+:1)" },                   // 0x2E
    { OF_DISP, 23, 0, 0, "sra (ix+:1),a" },                 // 0x2F

    { OF_DISP, 23, 0, 0, "sll (ix+:1),b" },                 // 0x30
    { OF_DISP, 23, 0, 0, "sll (ix+:1),c" },                 // 0x31
    { OF_DISP, 23, 0, 0, "sll (ix+:1),d" },                 // 0x32
    { OF_DISP, 23, 0, 0, "sll (ix+:1),e" },                 // 0x33
    { OF_DISP, 23, 0, 0, "sll (ix+:1),h" },                 // 0x34
    { OF_DISP, 23, 0, 0, "sll (ix+:1),l" },                 // 0x35
    { OF_DISP, 23, 0, 0, "sll (ix+:1)" },                   // 0x36
    { OF_DISP, 23, 0, 0, "sll (ix+:1),a" },                 // 0x37
    { OF_DISP, 23, 0, 0, "srl (ix+:1),b" },                 // 0x38
    { OF_DISP, 23, 0, 0, "srl (ix+:1),c" },                 // 0x39
    { OF_DISP, 23, 0, 0, "srl (ix+:1),d" },                 // 0x3A
    { OF_DISP, 23, 0, 0, "srl (ix+:1),e" },                 // 0x3B
    { OF_DISP, 23, 0, 0, "srl (ix+:1),h" },                 // 0x3C
    { OF_DISP, 23, 0, 0, "srl (ix+:1),l" },                 // 0x3D
    { OF_DISP, 23, 0, 0, "srl (ix+:1)" },                   // 0x3E
    { OF_DISP, 23, 0, 0, "srl (ix+:1),a" },                 // 0x3F

    { OF_DISP, 20, 0, 0, "bit 0,(ix+:1)" },                 // 0x40
    { OF_DISP, 20, 0, 0, "bit 0,(ix+:1)" },                 // 0x41
    { OF_DISP, 20, 0, 0, "bit 0,(ix+:1)" },                 // 0x42
    { OF_DISP, 20, 0, 0, "bit 0,(ix+:1)" },                 // 0x43
    { OF_DISP, 20, 0, 0, "bit 0,(ix+:1)" },                 // 0x44
    { OF_DISP, 20, 0, 0, "bit 0,(ix+:1)" },                 // 0x45
    { OF_DISP, 20, 0, 0, "bit 0,(ix+:1)" },                 // 0x46
    { OF_DISP, 20, 0, 0, "bit 0,(ix+:1)" },                 // 0x47
    { OF_DISP, 20, 0, 0, "bit 1,(ix+:1)" },                 // 0x48
    { OF_DISP, 20, 0, 0, "bit 1,(ix+:1)" },                 // 0x49
    { OF_DISP, 20, 0, 0, "bit 1,(ix+:1)" },                 // 0x4A
    { OF_DISP, 20, 0, 0, "bit 1,(ix+:1)" },                 // 0x4B
    { OF_DISP, 20, 0, 0, "bit 1,(ix+:1)" },                 // 0x4C
    { OF_DISP, 20, 0, 0, "bit 1,(ix+:1)" },                 // 0x4D
    { OF_DISP, 20, 0, 0, "bit 1,(ix+:1)" },                 // 0x4E
    { OF_DISP, 20, 0, 0, "bit 1,(ix+:1)" },                 // 0x4F

    { OF_DISP, 20, 0, 0, "bit 2,(ix+:1)" },                 // 0x50
    { OF_DISP, 20, 0, 0, "bit 2,(ix+:1)" },                 // 0x51
    { OF_DISP, 20, 0, 0, "bit 2,(ix+:1)" },                 // 0x52
    { OF_DISP, 20, 0, 0, "bit 2,(ix+:1)" },                 // 0x53
    { OF_DISP, 20, 0, 0, "bit 2,(ix+:1)" },                 // 0x54
    { OF_DISP, 20, 0, 0, "bit 2,(ix+:1)" },                 // 0x55
    { OF_DISP, 20, 0, 0, "bit 2,(ix+:1)" },                 // 0x56
    { OF_DISP, 20, 0, 0, "bit 2,(ix+:1)" },                 // 0x57
    { OF_DISP, 20, 0, 0, "bit 3,(ix+:1)" },                 // 0x58
    { OF_DISP, 20, 0, 0, "bit 3,(ix+:1)" },                 // 0x59
    { OF_DISP, 20, 0, 0, "bit 3,(ix+:1)" },                 // 0x5A
    { OF_DISP, 20, 0, 0, "bit 3,(ix+:1)" },                 // 0x5B
    { OF_DISP, 20, 0, 0, "bit 3,(ix+:1)" },                 // 0x5C
    { OF_DISP, 20, 0, 0, "bit 3,(ix+:1)" },                 // 0x5D
    { OF_DISP, 20, 0, 0, "bit 3,(ix+:1)" },                 // 0x5E
    { OF_DISP, 20, 0, 0, "bit 3,(ix+:1)" },                 // 0x5F

    { OF_DISP, 20, 0, 0, "bit 4,(ix+:1)" },                 // 0x60
    { OF_DISP, 20, 0, 0, "bit 4,(ix+:1)" },                 // 0x61
    { OF_DISP, 20, 0, 0, "bit 4,(ix+:1)" },                 // 0x62
    { OF_DISP, 20, 0, 0, "bit 4,(ix+:1)" },                 // 0x63
    { OF_DISP, 20, 0, 0, "bit 4,(ix+:1)" },                 // 0x64
    { OF_DISP, 20, 0, 0, "bit 4,(ix+:1)" },                 // 0x65
    { OF_DISP, 20, 0, 0, "bit 4,(ix+:1)" },                 // 0x66
    { OF_DISP, 20, 0, 0, "bit 4,(ix+:1)" },                 // 0x67
    { OF_DISP, 20, 0, 0, "bit 5,(ix+:1)" },                 // 0x68
    { OF_DISP, 20, 0, 0, "bit 5,(ix+:1)" },                 // 0x69
    { OF_DISP, 20, 0, 0, "bit 5,(ix+:1)" },                 // 0x6A
    { OF_DISP, 20, 0, 0, "bit 5,(ix+:1)" },                 // 0x6B
    { OF_DISP, 20, 0, 0, "bit 5,(ix+:1)" },                 // 0x6C
    { OF_DISP, 20, 0, 0, "bit 5,(ix+:1)" },                 // 0x6D
    { OF_DISP, 20, 0, 0, "bit 5,(ix+:1)" },                 // 0x6E
    { OF_DISP, 20, 0, 0, "bit 5,(ix+:1)" },                 // 0x6F

    { OF_DISP, 20, 0, 0, "bit 6,(ix+:1)" },                 // 0x70
    { OF_DISP, 20, 0, 0, "bit 6,(ix+:1)" },                 // 0x71
    { OF_DISP, 20, 0, 0, "bit 6,(ix+:1)" },                 // 0x72
    { OF_DISP, 20, 0, 0, "bit 6,(ix+:1)" },                 // 0x73
    { OF_DISP, 20, 0, 0, "bit 6,(ix+:1)" },                 // 0x74
    { OF_DISP, 20, 0, 0, "bit 6,(ix+:1)" },                 // 0x75
    { OF_DISP, 20, 0, 0, "bit 6,(ix+:1)" },                 // 0x76
    { OF_DISP, 20, 0, 0, "bit 6,(ix+:1)" },                 // 0x77
    { OF_DISP, 20, 0, 0, "bit 7,(ix+:1)" },                 // 0x78
    { OF_DISP, 20, 0, 0, "bit 7,(ix+:1)" },                 // 0x79
    { OF_DISP, 20, 0, 0, "bit 7,(ix+:1)" },                 // 0x7A
    { OF_DISP, 20, 0, 0, "bit 7,(ix+:1)" },                 // 0x7B
    { OF_DISP, 20, 0, 0, "bit 7,(ix+:1)" },                 // 0x7C
    { OF_DISP, 20, 0, 0, "bit 7,(ix+:1)" },                 // 0x7D
    { OF_DISP, 20, 0, 0, "bit 7,(ix+:1)" },                 // 0x7E
    { OF_DISP, 20, 0, 0, "bit 7,(ix+:1)" },                 // 0x7F

    { OF_DISP, 23, 0, 0, "res 0,(ix+:1),b" },               // 0x80
    { OF_DISP, 23, 0, 0, "res 0,(ix+:1),c" },               // 0x81
    { OF_DISP, 23, 0, 0, "res 0,(ix+:1),d" },               // 0x82
    { OF_DISP, 23, 0, 0, "res 0,(ix+:1),e" },               // 0x83
    { OF_DISP, 23, 0, 0, "res 0,(ix+:1),h" },               // 0x84
    { OF_DISP, 23, 0, 0, "res 0,(ix+:1),l" },               // 0x85
    { OF_DISP, 23, 0, 0, "res 0,(ix+:1)" },                 // 0x86
    { OF_DISP, 23, 0, 0, "res 0,(ix+:1),a" },               // 0x87
    { OF_DISP, 23, 0, 0, "res 1,(ix+:1),b" },               // 0x88
    { OF_DISP, 23, 0, 0, "res 1,(ix+:1),c" },               // 0x89
    { OF_DISP, 23, 0, 0, "res 1,(ix+:1),d" },               // 0x8A
    { OF_DISP, 23, 0, 0, "res 1,(ix+:1),e" },               // 0x8B
    { OF_DISP, 23, 0, 0, "res 1,(ix+:1),h" },               // 0x8C
    { OF_DISP, 23, 0, 0, "res 1,(ix+:1),l" },               // 0x8D
    { OF_DISP, 23, 0, 0, "res 1,(ix+:1)" },                 // 0x8E
    { OF_DISP, 23, 0, 0, "res 1,(ix+:1),a" },               // 0x8F

    { OF_DISP, 23, 0, 0, "res 2,(ix+:1),b" },               // 0x90
    { OF_DISP, 23, 0, 0, "res 2,(ix+:1),c" },               // 0x91
    { OF_DISP, 23, 0, 0, "res 2,(ix+:1),d" },               // 0x92
    { OF_DISP, 23, 0, 0, "res 2,(ix+:1),e" },               // 0x93
    { OF_DISP, 23, 0, 0, "res 2,(ix+:1),h" },               // 0x94
    { OF_DISP, 23, 0, 0, "res 2,(ix+:1),l" },               // 0x95
    { OF_DISP, 23, 0, 0, "res 2,(ix+:1)" },                 // 0x96
    { OF_DISP, 23, 0, 0, "res 2,(ix+:1),a" },               // 0x97
    { OF_DISP, 23, 0, 0, "res 3,(ix+:1),b" },               // 0x98
    { OF_DISP, 23, 0, 0, "res 3,(ix+:1),c" },               // 0x99
    { OF_DISP, 23, 0, 0, "res 3,(ix+:1),d" },               // 0x9A
    { OF_DISP, 23, 0, 0, "res 3,(ix+:1),e" },               // 0x9B
    { OF_DISP, 23, 0, 0, "res 3,(ix+:1),h" },               // 0x9C
    { OF_DISP, 23, 0, 0, "res 3,(ix+:1),l" },               // 0x9D
    { OF_DISP, 23, 0, 0, "res 3,(ix+:1)" },                 // 0x9E
    { OF_DISP, 23, 0, 0, "res 3,(ix+:1),a" },               // 0x9F

    { OF_DISP, 23, 0, 0, "res 4,(ix+:1),b" },               // 0xA0
    { OF_DISP, 23, 0, 0, "res 4,(ix+:1),c" },               // 0xA1
    { OF_DISP, 23, 0, 0, "res 4,(ix+:1),d" },               // 0xA2
    { OF_DISP, 23, 0, 0, "res 4,(ix+:1),e" },               // 0xA3
    { OF_DISP, 23, 0, 0, "res 4,(ix+:1),h" },               // 0xA4
    { OF_DISP, 23, 0, 0, "res 4,(ix+:1),l" },               // 0xA5
    { OF_DISP, 23, 0, 0, "res 4,(ix+:1)" },                 // 0xA6
    { OF_DISP, 23, 0, 0, "res 4,(ix+:1),a" },               // 0xA7
    { OF_DISP, 23, 0, 0, "res 5,(ix+:1),b" },               // 0xA8
    { OF_DISP, 23, 0, 0, "res 5,(ix+:1),c" },               // 0xA9
    { OF_DISP, 23, 0, 0, "res 5,(ix+:1),d" },               // 0xAA
    { OF_DISP, 23, 0, 0, "res 5,(ix+:1),e" },               // 0xAB
    { OF_DISP, 23, 0, 0, "res 5,(ix+:1),h" },               // 0xAC
    { OF_DISP, 23, 0, 0, "res 5,(ix+:1),l" },               // 0xAD
    { OF_DISP, 23, 0, 0, "res 5,(ix+:1)" },                 // 0xAE
    { OF_DISP, 23, 0, 0, "res 5,(ix+:1),a" },               // 0xAF

    { OF_DISP, 23, 0, 0, "res 6,(ix+:1),b" },               // 0xB0
    { OF_DISP, 23, 0, 0, "res 6,(ix+:1),c" },               // 0xB1
    { OF_DISP, 23, 0, 0, "res 6,(ix+:1),d" },               // 0xB2
    { OF_DISP, 23, 0, 0, "res 6,(ix+:1),e" },               // 0xB3
    { OF_DISP, 23, 0, 0, "res 6,(ix+:1),h" },               // 0xB4
    { OF_DISP, 23, 0, 0, "res 6,(ix+:1),l" },               // 0xB5
    { OF_DISP, 23, 0, 0, "res 6,(ix+:1)" },                 // 0xB6
    { OF_DISP, 23, 0, 0, "res 6,(ix+:1),a" },               // 0xB7
    { OF_DISP, 23, 0, 0, "res 7,(ix+:1),b" },               // 0xB8
    { OF_DISP, 23, 0, 0, "res 7,(ix+:1),c" },               // 0xB9
    { OF_DISP, 23, 0, 0, "res 7,(ix+:1),d" },               // 0xBA
    { OF_DISP, 23, 0, 0, "res 7,(ix+:1),e" },               // 0xBB
    { OF_DISP, 23, 0, 0, "res 7,(ix+:1),h" },               // 0xBC
    { OF_DISP, 23, 0, 0, "res 7,(ix+:1),l" },               // 0xBD
    { OF_DISP, 23, 0, 0, "res 7,(ix+:1)" },                 // 0xBE
    { OF_DISP, 23, 0, 0, "res 7,(ix+:1),a" },               // 0xBF

    { OF_DISP, 23, 0, 0, "set 0,(ix+:1),b" },               // 0xC0
    { OF_DISP, 23, 0, 0, "set 0,(ix+:1),c" },               // 0xC1
    { OF_DISP, 23, 0, 0, "set 0,(ix+:1),d" },               // 0xC2
    { OF_DISP, 23, 0, 0, "set 0,(ix+:1),e" },               // 0xC3
    { OF_DISP, 23, 0, 0, "set 0,(ix+:1),h" },               // 0xC4
    { OF_DISP, 23, 0, 0, "set 0,(ix+:1),l" },               // 0xC5
    { OF_DISP, 23, 0, 0, "set 0,(ix+:1)" },                 // 0xC6
    { OF_DISP, 23, 0, 0, "set 0,(ix+:1),a" },               // 0xC7
    { OF_DISP, 23, 0, 0, "set 1,(ix+:1),b" },               // 0xC8
    { OF_DISP, 23, 0, 0, "set 1,(ix+:1),c" },               // 0xC9
    { OF_DISP, 23, 0, 0, "set 1,(ix+:1),d" },               // 0xCA
    { OF_DISP, 23, 0, 0, "set 1,(ix+:1),e" },               // 0xCB
    { OF_DISP, 23, 0, 0, "set 1,(ix+:1),h" },               // 0xCC
    { OF_DISP, 23, 0, 0, "set 1,(ix+:1),l" },               // 0xCD
    { OF_DISP, 23, 0, 0, "set 1,(ix+:1)" },                 // 0xCE
    { OF_DISP, 23, 0, 0, "set 1,(ix+:1),a" },               // 0xCF

    { OF_DISP, 23, 0, 0, "set 2,(ix+:1),b" },               // 0xD0
    { OF_DISP, 23, 0, 0, "set 2,(ix+:1),c" },               // 0xD1
    { OF_DISP, 23, 0, 0, "set 2,(ix+:1),d" },               // 0xD2
    { OF_DISP, 23, 0, 0, "set 2,(ix+:1),e" },               // 0xD3
    { OF_DISP, 23, 0, 0, "set 2,(ix+:1),h" },               // 0xD4
    { OF_DISP, 23, 0, 0, "set 2,(ix+:1),l" },               // 0xD5
    { OF_DISP, 23, 0, 0, "set 2,(ix+:1)" },                 // 0xD6
    { OF_DISP, 23, 0, 0, "set 2,(ix+:1),a" },               // 0xD7
    { OF_DISP, 23, 0, 0, "set 3,(ix+:1),b" },               // 0xD8
    { OF_DISP, 23, 0, 0, "set 3,(ix+:1),c" },               // 0xD9
    { OF_DISP, 23, 0, 0, "set 3,(ix+:1),d" },               // 0xDA
    { OF_DISP, 23, 0, 0, "set 3,(ix+:1),e" },               // 0xDB
    { OF_DISP, 23, 0, 0, "set 3,(ix+:1),h" },               // 0xDC
    { OF_DISP, 23, 0, 0, "set 3,(ix+:1),l" },               // 0xDD
    { OF_DISP, 23, 0, 0, "set 3,(ix+:1)" },                 // 0xDE
    { OF_DISP, 23, 0, 0, "set 3,(ix+:1),a" },               // 0xDF

    { OF_DISP, 23, 0, 0, "set 4,(ix+:1),b" },               // 0xE0
    { OF_DISP, 23, 0, 0, "set 4,(ix+:1),c" },               // 0xE1
    { OF_DISP, 23, 0, 0, "set 4,(ix+:1),d" },               // 0xE2
    { OF_DISP, 23, 0, 0, "set 4,(ix+:1),e" },               // 0xE3
    { OF_DISP, 23, 0, 0, "set 4,(ix+:1),h" },               // 0xE4
    { OF_DISP, 23, 0, 0, "set 4,(ix+:1),l" },               // 0xE5
    { OF_DISP, 23, 0, 0, "set 4,(ix+:1)" },                 // 0xE6
    { OF_DISP, 23, 0, 0, "set 4,(ix+:1),a" },               // 0xE7
    { OF_DISP, 23, 0, 0, "set 5,(ix+:1),b" },               // 0xE8
    { OF_DISP, 23, 0, 0, "set 5,(ix+:1),c" },               // 0xE9
    { OF_DISP, 23, 0, 0, "set 5,(ix+:1),d" },               // 0xEA
    { OF_DISP, 23, 0, 0, "set 5,(ix+:1),e" },               // 0xEB
    { OF_DISP, 23, 0, 0, "set 5,(ix+:1),h" },               // 0xEC
    { OF_DISP, 23, 0, 0, "set 5,(ix+:1),l" },               // 0xED
    { OF_DISP, 23, 0, 0, "set 5,(ix+:1)" },                 // 0xEE
    { OF_DISP, 23, 0, 0, "set 5,(ix+:1),a" },               // 0xEF

    { OF_DISP, 23, 0, 0, "set 6,(ix+:1),b" },               // 0xF0
    { OF_DISP, 23, 0, 0, "set 6,(ix+:1),c" },               // 0xF1
    { OF_DISP, 23, 0, 0, "set 6,(ix+:1),d" },               // 0xF2
    { OF_DISP, 23, 0, 0, "set 6,(ix+:1),e" },               // 0xF3
    { OF_DISP, 23, 0, 0, "set 6,(ix+:1),h" },               // 0xF4
    { OF_DISP, 23, 0, 0, "set 6,(ix+:1),l" },               // 0xF5
    { OF_DISP, 23, 0, 0, "set 6,(ix+:1)" },                 // 0xF6
    { OF_DISP, 23, 0, 0, "set 6,(ix+:1),a" },               // 0xF7
    { OF_DISP, 23, 0, 0, "set 7,(ix+:1),b" },               // 0xF8
    { OF_DISP, 23, 0, 0, "set 7,(ix+:1),c" },               // 0xF9
    { OF_DISP, 23, 0, 0, "set 7,(ix+:1),d" },               // 0xFA
    { OF_DISP, 23, 0, 0, "set 7,(ix+:1),e" },               // 0xFB
    { OF_DISP, 23, 0, 0, "set 7,(ix+:1),h" },               // 0xFC
    { OF_DISP, 23, 0, 0, "set 7,(ix+:1),l" },               // 0xFD
    { OF_DISP, 23, 0, 0, "set 7,(ix+:1)" },                 // 0xFE
    { OF_DISP, 23, 0, 0, "set 7,(ix+:1),a" },               // 0xFF
};

/// endregion </#DDCB prefix opcodes>

/// region <#FDCB prefix opcodes>

OpCode Z80Disassembler::fdcbOpcodes[256]
{
    { OF_DISP, 23, 0, 0, "rlc (iy+:1),b" },                 // 0x00
    { OF_DISP, 23, 0, 0, "rlc (iy+:1),c" },                 // 0x01
    { OF_DISP, 23, 0, 0, "rlc (iy+:1),d" },                 // 0x02
    { OF_DISP, 23, 0, 0, "rlc (iy+:1),e" },                 // 0x03
    { OF_DISP, 23, 0, 0, "rlc (iy+:1),h" },                 // 0x04
    { OF_DISP, 23, 0, 0, "rlc (iy+:1),l" },                 // 0x05
    { OF_DISP, 23, 0, 0, "rlc (iy+:1)" },                   // 0x06
    { OF_DISP, 23, 0, 0, "rlc (iy+:1),a" },                 // 0x07
    { OF_DISP, 23, 0, 0, "rrc (iy+:1),b" },                 // 0x08
    { OF_DISP, 23, 0, 0, "rrc (iy+:1),c" },                 // 0x09
    { OF_DISP, 23, 0, 0, "rrc (iy+:1),d" },                 // 0x0A
    { OF_DISP, 23, 0, 0, "rrc (iy+:1),e" },                 // 0x0B
    { OF_DISP, 23, 0, 0, "rrc (iy+:1),h" },                 // 0x0C
    { OF_DISP, 23, 0, 0, "rrc (iy+:1),l" },                 // 0x0D
    { OF_DISP, 23, 0, 0, "rrc (iy+:1)" },                   // 0x0E
    { OF_DISP, 23, 0, 0, "rrc (iy+:1),a" },                 // 0x0F

    { OF_DISP, 23, 0, 0, "rl (iy+:1),b" },                  // 0x10
    { OF_DISP, 23, 0, 0, "rl (iy+:1),c" },                  // 0x11
    { OF_DISP, 23, 0, 0, "rl (iy+:1),d" },                  // 0x12
    { OF_DISP, 23, 0, 0, "rl (iy+:1),e" },                  // 0x13
    { OF_DISP, 23, 0, 0, "rl (iy+:1),h" },                  // 0x14
    { OF_DISP, 23, 0, 0, "rl (iy+:1),l" },                  // 0x15
    { OF_DISP, 23, 0, 0, "rl (iy+:1)" },                    // 0x16
    { OF_DISP, 23, 0, 0, "rl (iy+:1),a" },                  // 0x17
    { OF_DISP, 23, 0, 0, "rr (iy+:1),b" },                  // 0x18
    { OF_DISP, 23, 0, 0, "rr (iy+:1),c" },                  // 0x19
    { OF_DISP, 23, 0, 0, "rr (iy+:1),d" },                  // 0x1A
    { OF_DISP, 23, 0, 0, "rr (iy+:1),e" },                  // 0x1B
    { OF_DISP, 23, 0, 0, "rr (iy+:1),h" },                  // 0x1C
    { OF_DISP, 23, 0, 0, "rr (iy+:1),l" },                  // 0x1D
    { OF_DISP, 23, 0, 0, "rr (iy+:1)" },                    // 0x1E
    { OF_DISP, 23, 0, 0, "rr (iy+:1),a" },                  // 0x1F

    { OF_DISP, 23, 0, 0, "sla (iy+:1),b" },                 // 0x20
    { OF_DISP, 23, 0, 0, "sla (iy+:1),c" },                 // 0x21
    { OF_DISP, 23, 0, 0, "sla (iy+:1),d" },                 // 0x22
    { OF_DISP, 23, 0, 0, "sla (iy+:1),e" },                 // 0x23
    { OF_DISP, 23, 0, 0, "sla (iy+:1),h" },                 // 0x24
    { OF_DISP, 23, 0, 0, "sla (iy+:1),l" },                 // 0x25
    { OF_DISP, 23, 0, 0, "sla (iy+:1)" },                   // 0x26
    { OF_DISP, 23, 0, 0, "sla (iy+:1),a" },                 // 0x27
    { OF_DISP, 23, 0, 0, "sra (iy+:1),b" },                 // 0x28
    { OF_DISP, 23, 0, 0, "sra (iy+:1),c" },                 // 0x29
    { OF_DISP, 23, 0, 0, "sra (iy+:1),d" },                 // 0x2A
    { OF_DISP, 23, 0, 0, "sra (iy+:1),e" },                 // 0x2B
    { OF_DISP, 23, 0, 0, "sra (iy+:1),h" },                 // 0x2C
    { OF_DISP, 23, 0, 0, "sra (iy+:1),l" },                 // 0x2D
    { OF_DISP, 23, 0, 0, "sra (iy+:1)" },                   // 0x2E
    { OF_DISP, 23, 0, 0, "sra (iy+:1),a" },                 // 0x2F

    { OF_DISP, 23, 0, 0, "sll (iy+:1),b" },                 // 0x30
    { OF_DISP, 23, 0, 0, "sll (iy+:1),c" },                 // 0x31
    { OF_DISP, 23, 0, 0, "sll (iy+:1),d" },                 // 0x32
    { OF_DISP, 23, 0, 0, "sll (iy+:1),e" },                 // 0x33
    { OF_DISP, 23, 0, 0, "sll (iy+:1),h" },                 // 0x34
    { OF_DISP, 23, 0, 0, "sll (iy+:1),l" },                 // 0x35
    { OF_DISP, 23, 0, 0, "sll (iy+:1)" },                   // 0x36
    { OF_DISP, 23, 0, 0, "sll (iy+:1),a" },                 // 0x37
    { OF_DISP, 23, 0, 0, "srl (iy+:1),b" },                 // 0x38
    { OF_DISP, 23, 0, 0, "srl (iy+:1),c" },                 // 0x39
    { OF_DISP, 23, 0, 0, "srl (iy+:1),d" },                 // 0x3A
    { OF_DISP, 23, 0, 0, "srl (iy+:1),e" },                 // 0x3B
    { OF_DISP, 23, 0, 0, "srl (iy+:1),h" },                 // 0x3C
    { OF_DISP, 23, 0, 0, "srl (iy+:1),l" },                 // 0x3D
    { OF_DISP, 23, 0, 0, "srl (iy+:1)" },                   // 0x3E
    { OF_DISP, 23, 0, 0, "srl (iy+:1),a" },                 // 0x3F

    { OF_DISP, 20, 0, 0, "bit 0,(iy+:1)" },                 // 0x40
    { OF_DISP, 20, 0, 0, "bit 0,(iy+:1)" },                 // 0x41
    { OF_DISP, 20, 0, 0, "bit 0,(iy+:1)" },                 // 0x42
    { OF_DISP, 20, 0, 0, "bit 0,(iy+:1)" },                 // 0x43
    { OF_DISP, 20, 0, 0, "bit 0,(iy+:1)" },                 // 0x44
    { OF_DISP, 20, 0, 0, "bit 0,(iy+:1)" },                 // 0x45
    { OF_DISP, 20, 0, 0, "bit 0,(iy+:1)" },                 // 0x46
    { OF_DISP, 20, 0, 0, "bit 0,(iy+:1)" },                 // 0x47
    { OF_DISP, 20, 0, 0, "bit 1,(iy+:1)" },                 // 0x48
    { OF_DISP, 20, 0, 0, "bit 1,(iy+:1)" },                 // 0x49
    { OF_DISP, 20, 0, 0, "bit 1,(iy+:1)" },                 // 0x4A
    { OF_DISP, 20, 0, 0, "bit 1,(iy+:1)" },                 // 0x4B
    { OF_DISP, 20, 0, 0, "bit 1,(iy+:1)" },                 // 0x4C
    { OF_DISP, 20, 0, 0, "bit 1,(iy+:1)" },                 // 0x4D
    { OF_DISP, 20, 0, 0, "bit 1,(iy+:1)" },                 // 0x4E
    { OF_DISP, 20, 0, 0, "bit 1,(iy+:1)" },                 // 0x4F

    { OF_DISP, 20, 0, 0, "bit 2,(iy+:1)" },                 // 0x50
    { OF_DISP, 20, 0, 0, "bit 2,(iy+:1)" },                 // 0x51
    { OF_DISP, 20, 0, 0, "bit 2,(iy+:1)" },                 // 0x52
    { OF_DISP, 20, 0, 0, "bit 2,(iy+:1)" },                 // 0x53
    { OF_DISP, 20, 0, 0, "bit 2,(iy+:1)" },                 // 0x54
    { OF_DISP, 20, 0, 0, "bit 2,(iy+:1)" },                 // 0x55
    { OF_DISP, 20, 0, 0, "bit 2,(iy+:1)" },                 // 0x56
    { OF_DISP, 20, 0, 0, "bit 2,(iy+:1)" },                 // 0x57
    { OF_DISP, 20, 0, 0, "bit 3,(iy+:1)" },                 // 0x58
    { OF_DISP, 20, 0, 0, "bit 3,(iy+:1)" },                 // 0x59
    { OF_DISP, 20, 0, 0, "bit 3,(iy+:1)" },                 // 0x5A
    { OF_DISP, 20, 0, 0, "bit 3,(iy+:1)" },                 // 0x5B
    { OF_DISP, 20, 0, 0, "bit 3,(iy+:1)" },                 // 0x5C
    { OF_DISP, 20, 0, 0, "bit 3,(iy+:1)" },                 // 0x5D
    { OF_DISP, 20, 0, 0, "bit 3,(iy+:1)" },                 // 0x5E
    { OF_DISP, 20, 0, 0, "bit 3,(iy+:1)" },                 // 0x5F

    { OF_DISP, 20, 0, 0, "bit 4,(iy+:1)" },                 // 0x60
    { OF_DISP, 20, 0, 0, "bit 4,(iy+:1)" },                 // 0x61
    { OF_DISP, 20, 0, 0, "bit 4,(iy+:1)" },                 // 0x62
    { OF_DISP, 20, 0, 0, "bit 4,(iy+:1)" },                 // 0x63
    { OF_DISP, 20, 0, 0, "bit 4,(iy+:1)" },                 // 0x64
    { OF_DISP, 20, 0, 0, "bit 4,(iy+:1)" },                 // 0x65
    { OF_DISP, 20, 0, 0, "bit 4,(iy+:1)" },                 // 0x66
    { OF_DISP, 20, 0, 0, "bit 4,(iy+:1)" },                 // 0x67
    { OF_DISP, 20, 0, 0, "bit 5,(iy+:1)" },                 // 0x68
    { OF_DISP, 20, 0, 0, "bit 5,(iy+:1)" },                 // 0x69
    { OF_DISP, 20, 0, 0, "bit 5,(iy+:1)" },                 // 0x6A
    { OF_DISP, 20, 0, 0, "bit 5,(iy+:1)" },                 // 0x6B
    { OF_DISP, 20, 0, 0, "bit 5,(iy+:1)" },                 // 0x6C
    { OF_DISP, 20, 0, 0, "bit 5,(iy+:1)" },                 // 0x6D
    { OF_DISP, 20, 0, 0, "bit 5,(iy+:1)" },                 // 0x6E
    { OF_DISP, 20, 0, 0, "bit 5,(iy+:1)" },                 // 0x6F

    { OF_DISP, 20, 0, 0, "bit 6,(iy+:1)" },                 // 0x70
    { OF_DISP, 20, 0, 0, "bit 6,(iy+:1)" },                 // 0x71
    { OF_DISP, 20, 0, 0, "bit 6,(iy+:1)" },                 // 0x72
    { OF_DISP, 20, 0, 0, "bit 6,(iy+:1)" },                 // 0x73
    { OF_DISP, 20, 0, 0, "bit 6,(iy+:1)" },                 // 0x74
    { OF_DISP, 20, 0, 0, "bit 6,(iy+:1)" },                 // 0x75
    { OF_DISP, 20, 0, 0, "bit 6,(iy+:1)" },                 // 0x76
    { OF_DISP, 20, 0, 0, "bit 6,(iy+:1)" },                 // 0x77
    { OF_DISP, 20, 0, 0, "bit 7,(iy+:1)" },                 // 0x78
    { OF_DISP, 20, 0, 0, "bit 7,(iy+:1)" },                 // 0x79
    { OF_DISP, 20, 0, 0, "bit 7,(iy+:1)" },                 // 0x7A
    { OF_DISP, 20, 0, 0, "bit 7,(iy+:1)" },                 // 0x7B
    { OF_DISP, 20, 0, 0, "bit 7,(iy+:1)" },                 // 0x7C
    { OF_DISP, 20, 0, 0, "bit 7,(iy+:1)" },                 // 0x7D
    { OF_DISP, 20, 0, 0, "bit 7,(iy+:1)" },                 // 0x7E
    { OF_DISP, 20, 0, 0, "bit 7,(iy+:1)" },                 // 0x7F

    { OF_DISP, 23, 0, 0, "res 0,(iy+:1),b" },               // 0x80
    { OF_DISP, 23, 0, 0, "res 0,(iy+:1),c" },               // 0x81
    { OF_DISP, 23, 0, 0, "res 0,(iy+:1),d" },               // 0x82
    { OF_DISP, 23, 0, 0, "res 0,(iy+:1),e" },               // 0x83
    { OF_DISP, 23, 0, 0, "res 0,(iy+:1),h" },               // 0x84
    { OF_DISP, 23, 0, 0, "res 0,(iy+:1),l" },               // 0x85
    { OF_DISP, 23, 0, 0, "res 0,(iy+:1)" },                 // 0x86
    { OF_DISP, 23, 0, 0, "res 0,(iy+:1),a" },               // 0x87
    { OF_DISP, 23, 0, 0, "res 1,(iy+:1),b" },               // 0x88
    { OF_DISP, 23, 0, 0, "res 1,(iy+:1),c" },               // 0x89
    { OF_DISP, 23, 0, 0, "res 1,(iy+:1),d" },               // 0x8A
    { OF_DISP, 23, 0, 0, "res 1,(iy+:1),e" },               // 0x8B
    { OF_DISP, 23, 0, 0, "res 1,(iy+:1),h" },               // 0x8C
    { OF_DISP, 23, 0, 0, "res 1,(iy+:1),l" },               // 0x8D
    { OF_DISP, 23, 0, 0, "res 1,(iy+:1)" },                 // 0x8E
    { OF_DISP, 23, 0, 0, "res 1,(iy+:1),a" },               // 0x8F

    { OF_DISP, 23, 0, 0, "res 2,(iy+:1),b" },               // 0x90
    { OF_DISP, 23, 0, 0, "res 2,(iy+:1),c" },               // 0x91
    { OF_DISP, 23, 0, 0, "res 2,(iy+:1),d" },               // 0x92
    { OF_DISP, 23, 0, 0, "res 2,(iy+:1),e" },               // 0x93
    { OF_DISP, 23, 0, 0, "res 2,(iy+:1),h" },               // 0x94
    { OF_DISP, 23, 0, 0, "res 2,(iy+:1),l" },               // 0x95
    { OF_DISP, 23, 0, 0, "res 2,(iy+:1)" },                 // 0x96
    { OF_DISP, 23, 0, 0, "res 2,(iy+:1),a" },               // 0x97
    { OF_DISP, 23, 0, 0, "res 3,(iy+:1),b" },               // 0x98
    { OF_DISP, 23, 0, 0, "res 3,(iy+:1),c" },               // 0x99
    { OF_DISP, 23, 0, 0, "res 3,(iy+:1),d" },               // 0x9A
    { OF_DISP, 23, 0, 0, "res 3,(iy+:1),e" },               // 0x9B
    { OF_DISP, 23, 0, 0, "res 3,(iy+:1),h" },               // 0x9C
    { OF_DISP, 23, 0, 0, "res 3,(iy+:1),l" },               // 0x9D
    { OF_DISP, 23, 0, 0, "res 3,(iy+:1)" },                 // 0x9E
    { OF_DISP, 23, 0, 0, "res 3,(iy+:1),a" },               // 0x9F

    { OF_DISP, 23, 0, 0, "res 4,(iy+:1),b" },               // 0xA0
    { OF_DISP, 23, 0, 0, "res 4,(iy+:1),c" },               // 0xA1
    { OF_DISP, 23, 0, 0, "res 4,(iy+:1),d" },               // 0xA2
    { OF_DISP, 23, 0, 0, "res 4,(iy+:1),e" },               // 0xA3
    { OF_DISP, 23, 0, 0, "res 4,(iy+:1),h" },               // 0xA4
    { OF_DISP, 23, 0, 0, "res 4,(iy+:1),l" },               // 0xA5
    { OF_DISP, 23, 0, 0, "res 4,(iy+:1)" },                 // 0xA6
    { OF_DISP, 23, 0, 0, "res 4,(iy+:1),a" },               // 0xA7
    { OF_DISP, 23, 0, 0, "res 5,(iy+:1),b" },               // 0xA8
    { OF_DISP, 23, 0, 0, "res 5,(iy+:1),c" },               // 0xA9
    { OF_DISP, 23, 0, 0, "res 5,(iy+:1),d" },               // 0xAA
    { OF_DISP, 23, 0, 0, "res 5,(iy+:1),e" },               // 0xAB
    { OF_DISP, 23, 0, 0, "res 5,(iy+:1),h" },               // 0xAC
    { OF_DISP, 23, 0, 0, "res 5,(iy+:1),l" },               // 0xAD
    { OF_DISP, 23, 0, 0, "res 5,(iy+:1)" },                 // 0xAE
    { OF_DISP, 23, 0, 0, "res 5,(iy+:1),a" },               // 0xAF

    { OF_DISP, 23, 0, 0, "res 6,(iy+:1),b" },               // 0xB0
    { OF_DISP, 23, 0, 0, "res 6,(iy+:1),c" },               // 0xB1
    { OF_DISP, 23, 0, 0, "res 6,(iy+:1),d" },               // 0xB2
    { OF_DISP, 23, 0, 0, "res 6,(iy+:1),e" },               // 0xB3
    { OF_DISP, 23, 0, 0, "res 6,(iy+:1),h" },               // 0xB4
    { OF_DISP, 23, 0, 0, "res 6,(iy+:1),l" },               // 0xB5
    { OF_DISP, 23, 0, 0, "res 6,(iy+:1)" },                 // 0xB6
    { OF_DISP, 23, 0, 0, "res 6,(iy+:1),a" },               // 0xB7
    { OF_DISP, 23, 0, 0, "res 7,(iy+:1),b" },               // 0xB8
    { OF_DISP, 23, 0, 0, "res 7,(iy+:1),c" },               // 0xB9
    { OF_DISP, 23, 0, 0, "res 7,(iy+:1),d" },               // 0xBA
    { OF_DISP, 23, 0, 0, "res 7,(iy+:1),e" },               // 0xBB
    { OF_DISP, 23, 0, 0, "res 7,(iy+:1),h" },               // 0xBC
    { OF_DISP, 23, 0, 0, "res 7,(iy+:1),l" },               // 0xBD
    { OF_DISP, 23, 0, 0, "res 7,(iy+:1)" },                 // 0xBE
    { OF_DISP, 23, 0, 0, "res 7,(iy+:1),a" },               // 0xBF

    { OF_DISP, 23, 0, 0, "set 0,(iy+:1),b" },               // 0xC0
    { OF_DISP, 23, 0, 0, "set 0,(iy+:1),c" },               // 0xC1
    { OF_DISP, 23, 0, 0, "set 0,(iy+:1),d" },               // 0xC2
    { OF_DISP, 23, 0, 0, "set 0,(iy+:1),e" },               // 0xC3
    { OF_DISP, 23, 0, 0, "set 0,(iy+:1),h" },               // 0xC4
    { OF_DISP, 23, 0, 0, "set 0,(iy+:1),l" },               // 0xC5
    { OF_DISP, 23, 0, 0, "set 0,(iy+:1)" },                 // 0xC6
    { OF_DISP, 23, 0, 0, "set 0,(iy+:1),a" },               // 0xC7
    { OF_DISP, 23, 0, 0, "set 1,(iy+:1),b" },               // 0xC8
    { OF_DISP, 23, 0, 0, "set 1,(iy+:1),c" },               // 0xC9
    { OF_DISP, 23, 0, 0, "set 1,(iy+:1),d" },               // 0xCA
    { OF_DISP, 23, 0, 0, "set 1,(iy+:1),e" },               // 0xCB
    { OF_DISP, 23, 0, 0, "set 1,(iy+:1),h" },               // 0xCC
    { OF_DISP, 23, 0, 0, "set 1,(iy+:1),l" },               // 0xCD
    { OF_DISP, 23, 0, 0, "set 1,(iy+:1)" },                 // 0xCE
    { OF_DISP, 23, 0, 0, "set 1,(iy+:1),a" },               // 0xCF

    { OF_DISP, 23, 0, 0, "set 2,(iy+:1),b" },               // 0xD0
    { OF_DISP, 23, 0, 0, "set 2,(iy+:1),c" },               // 0xD1
    { OF_DISP, 23, 0, 0, "set 2,(iy+:1),d" },               // 0xD2
    { OF_DISP, 23, 0, 0, "set 2,(iy+:1),e" },               // 0xD3
    { OF_DISP, 23, 0, 0, "set 2,(iy+:1),h" },               // 0xD4
    { OF_DISP, 23, 0, 0, "set 2,(iy+:1),l" },               // 0xD5
    { OF_DISP, 23, 0, 0, "set 2,(iy+:1)" },                 // 0xD6
    { OF_DISP, 23, 0, 0, "set 2,(iy+:1),a" },               // 0xD7
    { OF_DISP, 23, 0, 0, "set 3,(iy+:1),b" },               // 0xD8
    { OF_DISP, 23, 0, 0, "set 3,(iy+:1),c" },               // 0xD9
    { OF_DISP, 23, 0, 0, "set 3,(iy+:1),d" },               // 0xDA
    { OF_DISP, 23, 0, 0, "set 3,(iy+:1),e" },               // 0xDB
    { OF_DISP, 23, 0, 0, "set 3,(iy+:1),h" },               // 0xDC
    { OF_DISP, 23, 0, 0, "set 3,(iy+:1),l" },               // 0xDD
    { OF_DISP, 23, 0, 0, "set 3,(iy+:1)" },                 // 0xDE
    { OF_DISP, 23, 0, 0, "set 3,(iy+:1),a" },               // 0xDF

    { OF_DISP, 23, 0, 0, "set 4,(iy+:1),b" },               // 0xE0
    { OF_DISP, 23, 0, 0, "set 4,(iy+:1),c" },               // 0xE1
    { OF_DISP, 23, 0, 0, "set 4,(iy+:1),d" },               // 0xE2
    { OF_DISP, 23, 0, 0, "set 4,(iy+:1),e" },               // 0xE3
    { OF_DISP, 23, 0, 0, "set 4,(iy+:1),h" },               // 0xE4
    { OF_DISP, 23, 0, 0, "set 4,(iy+:1),l" },               // 0xE5
    { OF_DISP, 23, 0, 0, "set 4,(iy+:1)" },                 // 0xE6
    { OF_DISP, 23, 0, 0, "set 4,(iy+:1),a" },               // 0xE7
    { OF_DISP, 23, 0, 0, "set 5,(iy+:1),b" },               // 0xE8
    { OF_DISP, 23, 0, 0, "set 5,(iy+:1),c" },               // 0xE9
    { OF_DISP, 23, 0, 0, "set 5,(iy+:1),d" },               // 0xEA
    { OF_DISP, 23, 0, 0, "set 5,(iy+:1),e" },               // 0xEB
    { OF_DISP, 23, 0, 0, "set 5,(iy+:1),h" },               // 0xEC
    { OF_DISP, 23, 0, 0, "set 5,(iy+:1),l" },               // 0xED
    { OF_DISP, 23, 0, 0, "set 5,(iy+:1)" },                 // 0xEE
    { OF_DISP, 23, 0, 0, "set 5,(iy+:1),a" },               // 0xEF

    { OF_DISP, 23, 0, 0, "set 6,(iy+:1),b" },               // 0xF0
    { OF_DISP, 23, 0, 0, "set 6,(iy+:1),c" },               // 0xF1
    { OF_DISP, 23, 0, 0, "set 6,(iy+:1),d" },               // 0xF2
    { OF_DISP, 23, 0, 0, "set 6,(iy+:1),e" },               // 0xF3
    { OF_DISP, 23, 0, 0, "set 6,(iy+:1),h" },               // 0xF4
    { OF_DISP, 23, 0, 0, "set 6,(iy+:1),l" },               // 0xF5
    { OF_DISP, 23, 0, 0, "set 6,(iy+:1)" },                 // 0xF6
    { OF_DISP, 23, 0, 0, "set 6,(iy+:1),a" },               // 0xF7
    { OF_DISP, 23, 0, 0, "set 7,(iy+:1),b" },               // 0xF8
    { OF_DISP, 23, 0, 0, "set 7,(iy+:1),c" },               // 0xF9
    { OF_DISP, 23, 0, 0, "set 7,(iy+:1),d" },               // 0xFA
    { OF_DISP, 23, 0, 0, "set 7,(iy+:1),e" },               // 0xFB
    { OF_DISP, 23, 0, 0, "set 7,(iy+:1),h" },               // 0xFC
    { OF_DISP, 23, 0, 0, "set 7,(iy+:1),l" },               // 0xFD
    { OF_DISP, 23, 0, 0, "set 7,(iy+:1)" },                 // 0xFE
    { OF_DISP, 23, 0, 0, "set 7,(iy+:1),a" },               // 0xFF
};

/// endregion </#FDCB prefix opcodes>

// Match opcode operands in mnemonic. :<N>
// Example: ld a,:1; ld hl,:2
std::regex Z80Disassembler::regexOpcodeOperands(":\\d+");

/// endregion </Static>

std::string Z80Disassembler::disassembleSingleCommand(const uint8_t* buffer, size_t len, uint8_t* commandLen, DecodedInstruction* decoded)
{
    std::string result = disassembleSingleCommandWithRuntime(buffer, len, commandLen, nullptr, nullptr, decoded);

    return result;
}

std::string Z80Disassembler::disassembleSingleCommandWithRuntime(const uint8_t* buffer, size_t len, uint8_t* commandLen, Z80Registers* registers, Memory* memory, DecodedInstruction* decoded)
{
    std::string result;

    /// region <Sanity check>
    if (buffer == nullptr || len == 0)
    {
        MLOGWARNING("Z80Disassembler::disassembleSingleCommand - invalid arguments");

        return result;
    }
    /// endregion </Sanity check>

    DecodedInstruction decodedInstruction = decodeInstruction(buffer, len, registers, memory);
    if (decodedInstruction.isValid)
    {
        result = decodedInstruction.mnemonic;

        if (commandLen)
            *commandLen = decodedInstruction.fullCommandLen;

        // Populate 'decoded' output param if available
        if (decoded != nullptr)
        {
            // Transfer all decoded information via copy-constructor
            *decoded = decodedInstruction;
        }
    }

    return result;
}

std::string Z80Disassembler::getRuntimeHints(DecodedInstruction& decoded)
{
    std::string result;

    // No runtime information available
    if (!decoded.hasRuntime)
    {
        return result;
    }

    if (decoded.hasRelativeJump)
    {
        result = StringHelper::Format("(#%04X)", decoded.relJumpAddr);
    }

    if (decoded.hasReturn)
    {
        result = StringHelper::Format("(#%04X)", decoded.returnAddr);
    }

    return result;
}

/// region <Helper methods>

DecodedInstruction Z80Disassembler::decodeInstruction(const uint8_t* buffer, size_t len, Z80Registers* registers, Memory* memory)
{
    DecodedInstruction result;

    /// region <Input parameters validation>
    if (buffer == nullptr || len == 0)
    {
        result.isValid = false;

        MLOGWARNING("Z80Disassembler::decodeInstruction - invalid arguments");

        return result;
    }
    /// endregion </Input parameters validation>

    int pos = 0;
    uint16_t prefix = 0x0000;
    uint8_t command = 0x00;
    OpCode opcode;
    uint8_t operandsLen = 0;
    uint8_t displacement = 0x00;
    [[maybe_unused]] uint8_t jumpOffset = 0x00;
    [[maybe_unused]] uint16_t wordOperand = 0x0000;
    [[maybe_unused]] uint8_t byteOperand = 0x00;
    bool hasDisplacement = false;
    bool hasJump = false;
    bool hasRelativeJump = false;
    bool hasReturn = false;
    bool hasByteArgument = false;
    bool hasWordArgument = false;

    bool commandDecoded = false;

    // Fetch longest possible prefixed command
    do
    {
        uint8_t fetchByte = *(buffer + pos++);
        result.instructionBytes.push_back(fetchByte);

        // Decode current byte
        opcode = getOpcode(prefix, fetchByte);
        bool isPrefix = opcode.flags & OF_PREFIX;
        hasDisplacement = opcode.flags & OF_DISP;
        hasJump = opcode.flags & OF_JUMP;
        hasRelativeJump = opcode.flags & OF_RELJUMP;
        hasReturn = opcode.flags & OF_RET;
        hasByteArgument = opcode.flags & OF_MBYTE;
        hasWordArgument = opcode.flags & OF_MWORD;

        // Update effective prefix (for prefix chains like DD FD FD DD DD ...)
        if (isPrefix)
        {
            if (prefix == 0x0000)   // No prefix
            {
                prefix = fetchByte;
            }
            else
            {
                prefix = (static_cast<uint16_t>(prefix) << 8) | (fetchByte & 0x00FF);
            }

            continue;
        }

        // As soon as non-prefix byte fetched - command be decoded
        if (!isPrefix)
        {
            switch (prefix)
            {
                // All those prefixes have simple pattern [<prefix>] <opcode> [<displacement>] [<operand(s)]>
                case 0x0000:
                    result.command = fetchByte;
                    operandsLen = hasOperands(opcode);
                    if (operandsLen > 0)
                    {
                        for (int i = 0; i < operandsLen; i++)
                        {
                            uint8_t curByte = *(buffer + pos++);
                            result.instructionBytes.push_back(curByte);
                            result.operandBytes.push_back(curByte);
                        }
                    }
                    break;
                case 0x00CB:
                case 0x00DD:
                case 0x00ED:
                case 0x00FD:
                    result.prefix = prefix;
                    result.command = fetchByte;
                    result.opcode = opcode;

                    operandsLen = hasOperands(opcode);
                    if (operandsLen > 0)
                    {
                        for (int i = 0; i < operandsLen; i++)
                        {
                            uint8_t curByte = *(buffer + pos++);
                            result.instructionBytes.push_back(curByte);
                            result.operandBytes.push_back(curByte);
                        }
                    }
                    break;
                case 0xDDCB:
                case 0xFDCB:
                    // DDCB and FDCB prefixes use another pattern <prefix> <displacement> <opcode>. No operands.
                    // DD CB <dd> E1 - set 4,(ix+dd),c
                    operandsLen = 1;                    // DDCB and FDCB prefixed instructions are always have just single displacement
                    displacement = fetchByte;
                    result.hasDisplacement = hasDisplacement;
                    result.displacement = displacement;
                    result.operandBytes.push_back(displacement);

                    command = *(buffer + pos++);
                    result.command = command;
                    opcode = getOpcode(prefix, command);
                    result.instructionBytes.push_back(command);

                    break;
            }

            commandDecoded = true;
            break;
        }

        pos++;
    }
    while (!commandDecoded);

    // Apply operands to mnemonic
    result.isValid = true;
    result.fullCommandLen = result.instructionBytes.size();
    result.operandsLen = operandsLen;
    result.opcode = opcode;

    result.hasJump = hasJump || (opcode.flags & OF_RST) != 0;
    result.hasRelativeJump = hasRelativeJump;
    result.hasReturn = hasReturn;
    result.hasByteOperand = hasByteArgument;
    result.hasWordOperand = hasWordArgument;
    result.hasCondition = (opcode.flags & OF_CONDITION) != 0;
    // Variable cycles are only for instructions like DJNZ where cycles depend on a counter
    result.hasVariableCycles = (opcode.flags & OF_VAR_T) != 0;
    result.hasDisplacement = (opcode.flags & OF_DISP) != 0;

    /// region <Actualize values according flags>
    if (hasByteArgument)
    {
        result.byteOperand = result.operandBytes[0];
    }

    if (hasWordArgument)
    {
        result.wordOperand = (result.operandBytes[0] << 8) | result.operandBytes[1];
    }

    if (hasRelativeJump)
    {
        result.relJumpOffset = result.operandBytes[0];
    }
    /// endregion </Actualize values according flags>

    // Populate runtime information if available
    if (registers != nullptr && memory != nullptr)
    {
        result.hasRuntime = true;

        // Populate information for jumps / calls (trivial)
        if (hasJump)
        {
            result.jumpAddr = result.wordOperand;
        }
        // Populate runtime information for relative jumps (JR e; JR z, xx; JR c, xx; JR nz, xx; JR c, xx)
        else if (hasRelativeJump)
        {
            // Offset is signed 8-bit integer [-128..+127]. Calculated from next instruction after JR ...
            result.instructionAddr = registers->pc;
            result.relJumpAddr = result.instructionAddr + result.relJumpOffset + 2;
        }
        // Populate runtime information for displacement operation
        else if (hasDisplacement)
        {
            // Displacement is signed 8-bit integer [-128..+127]
            result.displacement = static_cast<int8_t>(result.byteOperand);

            uint16_t baseAddr = 0x0000;

            // All commands with DD and DDCB prefixes are IX-indexed operations
            if (result.prefix == 0x00DD || result.prefix == 0xDDCB)
            {
                baseAddr = registers->ix;
            }

            // All commands with FD and FDCB prefixes are IX-indexed operations
            if (result.prefix == 0x00FD || result.prefix == 0xFDCB)
            {
                baseAddr = registers->iy;
            }

            result.displacementAddr = baseAddr + result.displacement;
        }
        // Populate runtime information for returns - requires access to stack memory
        else if (hasReturn)
        {
            if (memory != nullptr && registers != nullptr)
            {
                uint16_t sp = registers->sp;
                uint8_t loRet = memory->DirectReadFromZ80Memory(sp++);
                uint16_t hiRet = memory->DirectReadFromZ80Memory(sp);
                result.returnAddr = (hiRet << 8) | loRet;
            }
        }
    }

    // Format text representation of mnemonic
    std::string mnemonic = formatMnemonic(result);

    // Populate all decoded instruction fields

    result.hexDump = DumpHelper::HexDumpBuffer(result.instructionBytes.data(), result.instructionBytes.size());
    result.mnemonic = mnemonic;
    return result;
}

OpCode Z80Disassembler::getOpcode(uint16_t prefix, uint8_t fetchByte)
{
    OpCode opcode;
    switch (prefix)
    {
        case 0x0000:
            opcode = noprefixOpcodes[fetchByte];
            break;
        case 0x00CB:
            opcode = cbOpcodes[fetchByte];
            break;
        case 0x00DD:
            opcode = ddOpcodes[fetchByte];
            break;
        case 0x00ED:
            opcode = edOpcodes[fetchByte];
            break;
        case 0x00FD:
            opcode = fdOpcodes[fetchByte];
            break;
        case 0xDDCB:
            opcode = ddcbOpcodes[fetchByte];
            break;
        case 0xFDCB:
            opcode = fdcbOpcodes[fetchByte];
            break;
        default:
#ifdef _DEBUG
            {
                std::string prefixValue = StringHelper::FormatBinary(prefix);
                std::string message = StringHelper::Format("Unknown prefix: 0x%04X (%s), Instruction: 0x%02X", prefix, prefixValue.c_str(), fetchByte);
                throw std::logic_error(message);
            }
#endif
            break;
    }

    return opcode;
}

uint8_t Z80Disassembler::hasOperands(OpCode& opcode)
{
    uint8_t result = 0;

    /// region <Sanity checks>
    bool isPrefix = opcode.flags & OF_PREFIX;
    if (isPrefix)
    {
        throw std::logic_error("Prefix should not be evaluated for operands");
    }
    /// endregion </Sanity checks>

    bool isRelativeJump = opcode.flags & OF_RELJUMP;
    bool isDisplacement = opcode.flags & OF_DISP;
    bool isByteArgument = opcode.flags & OF_MBYTE;
    bool isWordArgument = opcode.flags & OF_MWORD;

    if (isRelativeJump)
    {
        result = 1;
    }
    else
    {
        if (isDisplacement)
        {
            result = 1;
        }

        if (isWordArgument)
        {
            result += 2;
        }
        else if (isByteArgument)
        {
            result += 1;
        }
    }

    return result;
}

std::string Z80Disassembler::formatMnemonic(const DecodedInstruction& decoded)
{
    std::string result;

    /// region <No operands>

    if (decoded.operandBytes.size() == 0)
    {
        result = decoded.opcode.mnem;
        return result;
    }

    /// endregion </No operands>

    /// region <Operands exist>

    std::string mnemonic = decoded.opcode.mnem;
    uint8_t operandBytesNeeded = 0;
    std::vector<uint8_t> operands = parseOperands(mnemonic, &operandBytesNeeded);
    std::vector<uint16_t> values;

    if (operands.size() > 0 && decoded.operandsLen > 0)
    {
        /// region <Sanity check>
        if (operandBytesNeeded != decoded.operandBytes.size() || operandBytesNeeded != decoded.operandsLen)
        {
            throw std::logic_error("Z80Disassembler::formatMnemonic - unmatched required and supplied operand bytes");
        }
        /// endregion </Sanity check>

        int pos = 0;

        for (uint8_t operand : operands)
        {
            switch (operand)
            {
                case 1:
                    {
                        uint8_t value = decoded.operandBytes[pos++];
                        values.push_back(value);
                    }
                    break;
                case 2:
                    {
                        uint8_t loByte = decoded.operandBytes[pos++];
                        uint16_t hiByte = decoded.operandBytes[pos++];
                        uint16_t value = hiByte << 8 | loByte;
                        values.push_back(value);
                    }
                    break;
                default:
                    throw std::logic_error("Z80Disassembler::formatMnemonic - invalid operand length");
                    break;
            }
        }

        result = formatOperandString(decoded, mnemonic, values);
    }

    /// endregion </Operands exist>

    return result;
}

std::vector<uint8_t> Z80Disassembler::parseOperands(std::string& mnemonic, uint8_t* expectedOperandsLen)
{
    std::vector<uint8_t> result;
    uint8_t bytesNeeded = 0;

    if (mnemonic.size() > 0)
    {
        try
        {
            std::sregex_iterator next(mnemonic.begin(), mnemonic.end(), regexOpcodeOperands);
            std::sregex_iterator end;
            while (next != end)
            {
                std::smatch match = *next;

                // Get match string like ':2'
                std::string value = match.str();

                /// region <Sanity checks>
                if (value.size() < 2)
                {
                    throw std::logic_error("Invalid regex to parse operands. Should produce at least 2 symbols like ':1', ':2'");
                }
                /// endregion </Sanity checks>

                // Remove leading ':' => '2'
                value = value.substr(1);

                // Convert from std::string to int
                uint8_t operandSize = std::stoi(value);
                bytesNeeded += operandSize;


                /// region <Sanity checks>
                if (operandSize > 2)
                {
                    std::string message = StringHelper::Format("Z80 cannot have operand size longer than WORD (2 bytes). In '%s' detected: %d", mnemonic.c_str(), operandSize);
                    throw std::logic_error(message);
                }

                if (operandSize == 0)
                {
                    std::string message = StringHelper::Format("Z80 cannot have operand with 0 bytes. In '%s' detected: %d", mnemonic.c_str(), operandSize);
                    throw std::logic_error(message);
                }

                /// endregion </Sanity checks>

                result.push_back(operandSize);

                next++;
            }
        }
        catch (std::regex_error& e)
        {
            // Syntax error in the regular expression
        }
    }

    if (expectedOperandsLen)
    {
        *expectedOperandsLen = bytesNeeded;
    }

    return result;
}

std::string Z80Disassembler::formatOperandString(const DecodedInstruction& decoded, const std::string& mnemonic, std::vector<uint16_t>& values)
{
    static const char* HEX_PREFIX = "#";

    std::string result;
    std::stringstream ss;

    try
    {
        int i = 0;
        int startPos = 0;

        std::sregex_iterator next(mnemonic.begin(), mnemonic.end(), regexOpcodeOperands);
        std::sregex_iterator end;
        while (next != end)
        {
            std::smatch match = *next;

            // Get match string like ':2'
            std::string value = match.str();

            /// region <Sanity checks>
            if (value.size() < 2)
            {
                throw std::logic_error("Invalid regex to parse operands. Should produce at least 2 symbols like ':1', ':2'");
            }
            /// endregion </Sanity checks>

            // Remove leading ':' => '2'
            value = value.substr(1);

            // Convert from std::string to int
            uint8_t operandSize = std::stoi(value);

            /// region <Sanity checks>
            if (operandSize > 2)
            {
                std::string message = StringHelper::Format("Z80 cannot have operand size longer than WORD (2 bytes). In '%s' detected: %d", mnemonic.c_str(), operandSize);
                throw std::logic_error(message);
            }

            if (operandSize == 0)
            {
                std::string message = StringHelper::Format("Z80 cannot have operand with 0 bytes. In '%s' detected: %d", mnemonic.c_str(), operandSize);
                throw std::logic_error(message);
            }

            /// endregion </Sanity checks>

            // Print mnemonic fragment till operand placeholder
            ss << mnemonic.substr(startPos, match.position() - startPos);

            // Print operand value
            std::string operand;
            switch (operandSize)
            {
                case 1:
                    if (decoded.hasRelativeJump)
                        operand = StringHelper::ToHexWithPrefix(static_cast<int8_t>(values[i]), HEX_PREFIX);
                    else
                        operand = StringHelper::ToHexWithPrefix(static_cast<uint8_t>(values[i]), HEX_PREFIX);
                    break;
                case 2:
                    operand = StringHelper::ToHexWithPrefix(values[i], HEX_PREFIX);
                    break;
                default:
                    throw std::logic_error("Invalid operand size");
            }

            ss << StringHelper::ToUpper(operand);

            startPos = match.position() + match.length();
            i++;
            next++;
        }

        // Print mnemonic leftover after last operand
        ss << mnemonic.substr(startPos);

        result = ss.str();
    }
    catch (std::regex_error& e)
    {
        // Syntax error in the regular expression
    }

    return result;
}


/// endregion </Helper methods>