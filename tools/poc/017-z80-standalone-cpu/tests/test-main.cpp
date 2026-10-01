// test-main.cpp - verification suite for the standalone Z80 core (PoC 017).
//
// Unit tests pin reset state, per-opcode timing, flag semantics, the
// undocumented behaviors (MEMPTR/WZ, Q register, SLL, IN (C), OUT (C),0,
// IXH/IXL/IYH/IYL) and the interrupt model against hand-derived expectations
// (cross-checked against the ported core sources, which are FUSE-verified).
//
// The definitive verification is the ZEXALL-family tapes (zexall/zexdoc/
// zexbit/zexfix from data/testsoft/ZEXALL) run through the soft-ROM host;
// they execute after the unit tests unless --quick is passed.

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

#include "hostmachine.h"
#include "minitest.h"

namespace
{
// Tiny harness: flat-RAM host plus load/step/getter conveniences.
class CpuTest
{
public:
    HostMachine host;

    CpuTest() { host.Reset(); }

    void Load(uint16_t addr, std::initializer_list<uint8_t> code)
    {
        host.LoadCode(addr, code.begin(), code.size());
        Z80CpuSetReg(host.cpu, Z80CpuRegPc, addr);
    }

    int Step() { return host.StepCapture(); }

    uint16_t A() const { return Z80CpuGetReg(host.cpu, Z80CpuRegAf) >> 8; }
    uint16_t F() const { return Z80CpuGetReg(host.cpu, Z80CpuRegAf) & 0xFF; }
    uint16_t Reg(Z80CpuReg r) const { return Z80CpuGetReg(host.cpu, r); }
};

// Port-bus shims shared by the IN/OUT tests.
uint8_t gPortValue = 0x80;    // value returned by every port read
uint16_t gLastPort = 0xFFFF;  // last port number seen (either direction)
uint8_t gLastWritten = 0xAA;  // last value written by an OUT

uint8_t ShimPortIn(Z80CPU*, uint16_t port, void*)
{
    gLastPort = port;
    return gPortValue;
}

void ShimPortOut(Z80CPU*, uint16_t port, uint8_t value, void*)
{
    gLastPort = port;
    gLastWritten = value;
}

// RETI callback counter.
int gRetiCount = 0;
void ShimReti(Z80CPU*, void*) { ++gRetiCount; }

// IM2 vector callback.
uint8_t ShimIntVector(Z80CPU*, void*) { return 0xFE; }
}  // namespace

TEST(ResetState)
{
    CpuTest t;  // constructor resets
    CHECK_EQ(t.Reg(Z80CpuRegPc), 0x0000);
    CHECK_EQ(t.Reg(Z80CpuRegSp), 0xFFFF);
    CHECK_EQ(t.Reg(Z80CpuRegAf), 0xFFFF);
    CHECK_EQ(t.Reg(Z80CpuRegIm), 0);
    CHECK_EQ(t.Reg(Z80CpuRegIff1), 0);
    CHECK_EQ(t.Reg(Z80CpuRegIff2), 0);
    CHECK_EQ(t.Reg(Z80CpuRegI), 0);
    CHECK_EQ(t.Reg(Z80CpuRegR), 0);
    CHECK_EQ(t.Reg(Z80CpuRegQ), 0);
    CHECK_EQ(t.Reg(Z80CpuRegMemptr), 0);
    CHECK_EQ(Z80CpuTstates(t.host.cpu), 3u);  // reset consumes 3 T
}

TEST(OpcodeTiming)
{
    CpuTest t;
    t.Load(0x8000, {0x01, 0x34, 0x12});       // LD BC,1234h
    CHECK_EQ(t.Step(), 10);
    CHECK_EQ(t.Reg(Z80CpuRegBc), 0x1234);

    t.Load(0x8003, {0x3E, 0x2A});             // LD A,2Ah
    CHECK_EQ(t.Step(), 7);

    t.Load(0x8005, {0x80});                   // ADD A,B
    CHECK_EQ(t.Step(), 4);

    t.Load(0x8006, {0x32, 0x00, 0x81});       // LD (8100h),A
    CHECK_EQ(t.Step(), 13);
    CHECK_EQ(t.host.mem[0x8100], 0x2A + 0x12);  // B is the high byte of BC

    t.Load(0x8009, {0x00});                   // NOP
    CHECK_EQ(t.Step(), 4);

    t.Load(0x800A, {0xF7});                   // RST 30h
    CHECK_EQ(t.Step(), 11);
    CHECK_EQ(t.Reg(Z80CpuRegPc), 0x0030);
    CHECK_EQ(t.Reg(Z80CpuRegSp), 0xFFFD);     // return address pushed
}

TEST(ArithmeticFlags)
{
    // 7Fh + 01h = 80h: SF|HF|PV, no CF; F3/F5 from result (80h -> both clear)
    CpuTest t;
    t.Load(0x8000, {0x3E, 0x7F, 0xC6, 0x01});  // LD A,7Fh; ADD A,01h
    t.Step();
    t.Step();
    CHECK_EQ(t.A(), 0x80);
    CHECK_EQ(t.F(), 0x94);

    // XOR A: A=0, F = ZF|PV = 44h
    t.Load(0x8004, {0xAF});
    t.Step();
    CHECK_EQ(t.A(), 0x00);
    CHECK_EQ(t.F(), 0x44);
}

TEST(DaaBehavior)
{
    // 0Fh + 01h = 10h with HF set; DAA adds 6 -> 16h
    CpuTest t;
    t.Load(0x8000, {0x3E, 0x0F, 0xC6, 0x01, 0x27});  // LD A,0Fh; ADD A,1; DAA
    t.Step();
    t.Step();
    CHECK_EQ(t.Step(), 4);
    CHECK_EQ(t.A(), 0x16);
    CHECK_EQ(t.F() & 0x01, 0u);  // no carry out

    // Already-BCD value is left alone
    t.Load(0x8005, {0x3E, 0x15, 0x27});  // LD A,15h; DAA
    t.Step();
    t.Step();
    CHECK_EQ(t.A(), 0x15);
}

TEST(UndocumentedSll)
{
    // SLL A (CB 37) shifts left and feeds 1 into bit 0; flags from rl1 table
    CpuTest t;
    t.Load(0x8000, {0x3E, 0x7F, 0xCB, 0x37});  // LD A,7Fh; SLL A
    t.Step();
    t.Step();
    CHECK_EQ(t.A(), 0xFF);
    CHECK_EQ(t.F(), 0xAC);  // SF|F5|F3|PV(8 ones = even), CF=0

    t.Load(0x8004, {0x3E, 0xFF, 0xCB, 0x37});  // LD A,FFh; SLL A
    t.Step();
    t.Step();
    CHECK_EQ(t.A(), 0xFF);
    CHECK_EQ(t.F(), 0xAD);  // same plus CF=1 (bit 7 shifted out)
}

TEST(UndocumentedInOutC)
{
    CpuTest t;
    Z80CpuSetPortBus(t.host.cpu, ShimPortIn, nullptr, ShimPortOut, nullptr);

    t.Load(0x8000, {0xAF, 0x01, 0x34, 0x12});  // XOR A; LD BC,1234h
    t.Step();  // F=44h, CF=0
    t.Step();

    // IN (C) (ED 70): flags from the port value, CF preserved
    t.Load(0x8004, {0xED, 0x70});
    gPortValue = 0x80;
    t.Step();
    CHECK_EQ(t.F(), 0x80);  // SF only (odd parity, X/Y clear in 80h)
    CHECK_EQ(gLastPort, 0x1234);
    CHECK_EQ(t.Reg(Z80CpuRegMemptr), 0x1235);  // WZ = BC+1

    gPortValue = 0x00;
    t.Load(0x8006, {0xED, 0x70});
    t.Step();
    CHECK_EQ(t.F(), 0x44);  // ZF|PV

    // OUT (C),0 (ED 71): writes the configurable OUT-C0 value
    t.Load(0x8008, {0xED, 0x71});
    t.Step();
    CHECK_EQ(gLastWritten, 0x00);  // NMOS default: 0

    Z80CpuSetOutC0Value(t.host.cpu, 0xFF);  // some CMOS clones write FFh
    t.Load(0x800A, {0xED, 0x71});
    t.Step();
    CHECK_EQ(gLastWritten, 0xFF);
}

TEST(UndocumentedIxHighLow)
{
    CpuTest t;
    t.Load(0x8000, {0xDD, 0x26, 0x55});  // LD IXH,55h
    t.Step();
    CHECK_EQ(t.Reg(Z80CpuRegIx), 0x5500);

    t.Load(0x8003, {0xFD, 0x26, 0xAA});  // LD IYH,AAh
    t.Step();
    CHECK_EQ(t.Reg(Z80CpuRegIy), 0xAA00);

    t.Load(0x8006, {0xDD, 0x24});  // INC IXH
    t.Step();
    CHECK_EQ(t.Reg(Z80CpuRegIx), 0x5600);
}

TEST(MemptrAndQ)
{
    // LD (nn),A leaves WZ.H = A (op_32); observable via GetReg and via
    // BIT 0,(HL) X/Y flags (which read WZ.H on real silicon)
    CpuTest t;
    t.Load(0x8000, {0x3E, 0x28, 0x32, 0x00, 0x81});  // LD A,28h; LD (8100h),A
    t.Step();
    t.Step();
    CHECK_EQ(t.Reg(Z80CpuRegMemptr) >> 8, 0x28);

    t.host.mem[0x2B99] = 0x00;
    t.Load(0x8005, {0x21, 0x99, 0x2B, 0xCB, 0x46});  // LD HL,2B99h; BIT 0,(HL)
    t.Step();
    t.Step();
    // log_f[0] | HF | CF(kept from the post-reset F=FFh) with X/Y <- WZ.H:
    // (0x44|0x10|0x01) with bits 3/5 replaced by 28h -> 7Dh. Pins both the
    // WZ-derived X/Y flags and BIT n,(HL)'s CF preservation.
    CHECK_EQ(t.F(), 0x7D);

    // SCF/CCF X/Y come from (A | (F & ~Q)) - Zilog flavor; Q is exposed
    CpuTest t2;
    t2.Load(0x8000, {0xAF, 0x3E, 0x28, 0x37});  // XOR A; LD A,28h; SCF
    t2.Step();
    t2.Step();
    t2.Step();
    CHECK_EQ(t2.F(), 0x6D);  // 44h | A-derived X/Y(28h) | CF

    t2.Load(0x8004, {0x3F});  // CCF: X/Y preserved (Q=F), CF toggled off, HF=old CF
    t2.Step();
    CHECK_EQ(t2.F(), 0x7C);
    CHECK_EQ(t2.Reg(Z80CpuRegQ), 0x28);
}

TEST(BlockOpsTiming)
{
    // LDIR copies 2 bytes: 21 T (continue) + 16 T (last) = 37 T, BC ends 0
    CpuTest t;
    t.host.mem[0x8000] = 0xAA;
    t.host.mem[0x8001] = 0xBB;
    t.Load(0x8100, {0x21, 0x00, 0x80, 0x11, 0x00, 0x82, 0x01, 0x02, 0x00, 0xED, 0xB0});
    // LD HL,8000h; LD DE,8200h; LD BC,0002h; LDIR
    for (int i = 0; i < 3; ++i)
        t.Step();

    CHECK_EQ(t.Step(), 21);  // first LDIR iteration
    CHECK_EQ(t.Reg(Z80CpuRegBc), 0x0001);
    CHECK_EQ(t.F() & 0x04, 0x04u);  // PV set: BC != 0

    CHECK_EQ(t.Step(), 16);  // final LDIR iteration
    CHECK_EQ(t.Reg(Z80CpuRegBc), 0x0000);
    CHECK_EQ(t.F() & 0x04, 0x00u);  // PV clear
    CHECK_EQ(t.host.mem[0x8200], 0xAA);
    CHECK_EQ(t.host.mem[0x8201], 0xBB);
    CHECK_EQ(t.Reg(Z80CpuRegPc), 0x810B);  // PC past the LDIR
}

TEST(InterruptAcceptance)
{
    // EI-delay: the instruction boundary right after EI suppresses INT
    CpuTest t;
    t.Load(0x8000, {0xFB, 0x00});  // EI; NOP
    t.Step();                      // EI: IFF1 set, acceptance suppressed
    CHECK_EQ(Z80CpuIntPossible(t.host.cpu), 0);
    CHECK_EQ(Z80CpuInt(t.host.cpu), 0);  // rejected

    t.Step();  // NOP: suppression lifted
    CHECK_EQ(Z80CpuIntPossible(t.host.cpu), 1);

    const uint16_t pcBeforeInt = t.Reg(Z80CpuRegPc);
    CHECK_EQ(Z80CpuInt(t.host.cpu), 13);  // IM0/IM1: RST 38h
    CHECK_EQ(t.Reg(Z80CpuRegPc), 0x0038);
    CHECK_EQ(t.Reg(Z80CpuRegIff1), 0);
    CHECK_EQ(t.Reg(Z80CpuRegIff2), 0);
    CHECK_EQ(t.Reg(Z80CpuRegSp), 0xFFFD);                 // PC pushed
    CHECK_EQ(t.host.mem[0xFFFD], pcBeforeInt & 0xFF);     // little-endian push
    CHECK_EQ(t.host.mem[0xFFFE], pcBeforeInt >> 8);

    // With IFF1 now clear the next request is rejected
    CHECK_EQ(Z80CpuInt(t.host.cpu), 0);

    // DI keeps the line rejected even after another instruction
    CpuTest t2;
    t2.Load(0x8000, {0xFB, 0xF3, 0x00});  // EI; DI; NOP
    t2.Step();
    t2.Step();  // DI: IFF1=0
    t2.Step();
    CHECK_EQ(Z80CpuInt(t2.host.cpu), 0);
}

TEST(InterruptMode2)
{
    CpuTest t;
    Z80CpuSetIntVectorFn(t.host.cpu, ShimIntVector, nullptr);
    Z80CpuSetReg(t.host.cpu, Z80CpuRegIm, 2);
    Z80CpuSetReg(t.host.cpu, Z80CpuRegI, 0x10);
    Z80CpuSetReg(t.host.cpu, Z80CpuRegIff1, 1);
    // vector table entry at 10FEh -> 1234h
    t.host.mem[0x10FE] = 0x34;
    t.host.mem[0x10FF] = 0x12;

    CHECK_EQ(Z80CpuInt(t.host.cpu), 19);
    CHECK_EQ(t.Reg(Z80CpuRegPc), 0x1234);
    CHECK_EQ(t.Reg(Z80CpuRegMemptr), 0x1234);  // WZ = handler address
}

TEST(NmiBehavior)
{
    CpuTest t;
    Z80CpuSetReg(t.host.cpu, Z80CpuRegIff1, 1);
    Z80CpuSetReg(t.host.cpu, Z80CpuRegPc, 0x8000);
    Z80CpuSetReg(t.host.cpu, Z80CpuRegSp, 0xFFF0);

    CHECK_EQ(Z80CpuNmi(t.host.cpu), 11);
    CHECK_EQ(t.Reg(Z80CpuRegPc), 0x0066);
    CHECK_EQ(t.Reg(Z80CpuRegMemptr), 0x0066);
    CHECK_EQ(t.Reg(Z80CpuRegIff1), 0);  // disabled inside handler...
    CHECK_EQ(t.Reg(Z80CpuRegIff2), 1);  // ...snapshot kept for RETN
    CHECK_EQ(t.Reg(Z80CpuRegSp), 0xFFEE);
    CHECK_EQ(t.host.mem[0xFFEE], 0x00);
    CHECK_EQ(t.host.mem[0xFFEF], 0x80);
}

TEST(HaltBehavior)
{
    CpuTest t;
    Z80CpuSetReg(t.host.cpu, Z80CpuRegIff1, 1);
    t.Load(0x8000, {0x76});  // HALT
    t.Step();
    CHECK_EQ(Z80CpuHalted(t.host.cpu), 1);

    const uint16_t rBefore = t.Reg(Z80CpuRegR);
    CHECK_EQ(t.Step(), 4);  // halted step burns 4 T...
    CHECK_EQ(t.Reg(Z80CpuRegR), (rBefore + 1) & 0xFF);  // ...and ticks R

    CHECK_EQ(Z80CpuInt(t.host.cpu), 13);  // INT releases the halt
    CHECK_EQ(Z80CpuHalted(t.host.cpu), 0);
    CHECK_EQ(t.Reg(Z80CpuRegPc), 0x0038);
}

TEST(RetiCallback)
{
    CpuTest t;
    Z80CpuSetRetiFn(t.host.cpu, ShimReti, nullptr);
    Z80CpuSetReg(t.host.cpu, Z80CpuRegSp, 0xFF00);
    t.host.mem[0xFF00] = 0x00;
    t.host.mem[0xFF01] = 0x90;  // RETI pops PC = 9000h

    t.Load(0x8000, {0xED, 0x4D});  // RETI
    CHECK_EQ(t.Step(), 14);
    CHECK_EQ(gRetiCount, 1);
    CHECK_EQ(t.Reg(Z80CpuRegPc), 0x9000);
}

// ---------------------------------------------------------------------------
// ZEXALL-family verification runs
// ---------------------------------------------------------------------------

namespace
{
bool FileExists(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (f)
    {
        fclose(f);
        return true;
    }
    return false;
}

// Runs one tape; returns 0 = pass, 1 = fail, 2 = skipped (tape missing).
// Budgets are generous: the <adc,sbc> hl test of zexdoc alone needs ~2.3e9 T.
int RunZexTape(const char* file, uint64_t budget)
{
    const std::string path = std::string(Z80POC_DATA_DIR) + "/" + file;
    if (!FileExists(path))
    {
        std::printf("[ SKIP ] %s (not found: %s)\n", file, path.c_str());
        return 2;
    }

    HostMachine host;
    const bool ok = host.RunZex(path, budget, /*verbose=*/true);
    std::printf("[ %s ] %s\n\n", ok ? " OK " : "FAIL", file);
    return ok ? 0 : 1;
}
}  // namespace

int main(int argc, char** argv)
{
    bool quick = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--quick") == 0)
            quick = true;

    int failures = RUN_TESTS();

    if (!quick)
    {
        std::printf("\n=== ZEXALL-family verification (full undocumented-behavior suite) ===\n");
        std::printf("(zexdoc alone is ~30+ min of real 3.5 MHz time; each pass takes ~15-30 s here)\n\n");
        struct TapeSpec
        {
            const char* file;
            uint64_t budget;
        };
        const TapeSpec tapes[] = {
            {"zexdoc.tap", 25000000000ULL},  // the full undocumented-op suite
            {"zexall.tap", 12000000000ULL},
            {"zexbit.tap", 8000000000ULL},
            {"zexfix.tap", 8000000000ULL},
        };
        for (const TapeSpec& tape : tapes)
        {
            const int r = RunZexTape(tape.file, tape.budget);
            if (r == 1)
                ++failures;
        }
    }
    else
    {
        std::printf("\n(--quick: ZEXALL-family runs skipped)\n");
    }

    return failures == 0 ? 0 : 1;
}
