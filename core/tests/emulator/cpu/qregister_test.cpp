/// @file The Z80's hidden Q latch (qregister.h): F & 0x28 after an instruction that writes F, 0 after any other.
/// SCF / CCF read it: bits 5 and 3 = (A | (F & ~Q)) & 0x28.
///
/// Two layers:
///   - the cases z80test's z80ccf caught (DEC r leaving F as it was, POP AF, EX AF,AF'), spelled out;
///   - every instruction of every map (base, CB, ED, DD, FD, DDCB, FDCB), each from several register states,
///     against the unreal-z80 library (core/src/3rdparty/unreal-z80), an independent implementation whose
///     handlers carry their own flag-writer class (Z80_RETURN_Q). F, Q and then SCF after it must agree.
/// The whole z80test suite on the machine, z80ccf included, is Z80TestProgram_Test (long tier).

#include "stdafx.h"
#include "pch.h"

#include "3rdparty/unreal-z80/z80cpu.h"
#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/qregister.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portinterceptor.h"

#include <array>
#include <cstdio>
#include <string>
#include <unordered_map>

namespace
{

constexpr uint16_t kCodeAt = 0x8000;

/// Every IN reads one fixed value, every OUT is swallowed: no port decoder state changes between steps
class FixedPorts : public IPortInterceptor
{
public:
    uint8_t value = 0x00;
    bool InterceptIn(uint16_t, uint8_t& v) override
    {
        v = value;
        return true;
    }
    bool InterceptOut(uint16_t, uint8_t) override { return true; }
};

struct State
{
    uint16_t af, bc, de, hl, ix, iy, sp;
    uint16_t afAlt, bcAlt, deAlt, hlAlt;
    uint8_t i, r, q, iff;
    uint8_t port;
};

}  // namespace

class QRegister_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    FixedPorts _ports;
    Z80CPU* _lib = nullptr;
    std::unordered_map<uint16_t, uint8_t> _libWrites;  // the library's writes, over the machine's memory

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _z80 = _emulator->GetContext()->pCore->GetZ80();
        _memory = _emulator->GetContext()->pMemory;
        _z80->SetPortInterceptor(&_ports);

        _lib = Z80CpuCreate();
        Z80CpuReset(_lib);
        Z80CpuSetMemoryBus(
            _lib,
            [](Z80CPU*, uint16_t addr, int, void* self) -> uint8_t
            {
                auto* t = static_cast<QRegister_Test*>(self);
                auto it = t->_libWrites.find(addr);
                return it != t->_libWrites.end() ? it->second : t->_memory->DirectReadFromZ80Memory(addr);
            },
            this,
            [](Z80CPU*, uint16_t addr, uint8_t value, void* self) { static_cast<QRegister_Test*>(self)->_libWrites[addr] = value; },
            this);
        Z80CpuSetPortBus(
            _lib, [](Z80CPU*, uint16_t, void* self) -> uint8_t { return static_cast<QRegister_Test*>(self)->_ports.value; },
            this, nullptr, nullptr);
    }

    void TearDown() override
    {
        if (_lib)
            Z80CpuDestroy(_lib);
        if (_emulator)
        {
            _z80->SetPortInterceptor(nullptr);
            EmulatorTestHelper::CleanupEmulator(_emulator);
        }
    }

    void Load(uint16_t addr, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            _memory->DirectWriteToZ80Memory(addr++, b);
    }

    /// Native core: registers from s, PC at the code
    void SetNative(const State& s)
    {
        _z80->af = s.af;
        _z80->bc = s.bc;
        _z80->de = s.de;
        _z80->hl = s.hl;
        _z80->ix = s.ix;
        _z80->iy = s.iy;
        _z80->sp = s.sp;
        _z80->alt.af = s.afAlt;
        _z80->alt.bc = s.bcAlt;
        _z80->alt.de = s.deAlt;
        _z80->alt.hl = s.hlAlt;
        _z80->i = s.i;
        _z80->r_low = s.r & 0x7F;
        _z80->r_hi = s.r & 0x80;
        _z80->iff1 = _z80->iff2 = s.iff;
        _z80->im = 1;
        _z80->q = s.q;
        _z80->memptr = 0;
        _z80->halted = 0;
        _z80->boundary = Z80_BOUNDARY_NONE;
        _z80->pc = kCodeAt;
        _ports.value = s.port;
    }

    void SetLibrary(const State& s)
    {
        Z80CpuReset(_lib);
        Z80CpuRegisters r{};
        r.af = s.af;
        r.bc = s.bc;
        r.de = s.de;
        r.hl = s.hl;
        r.ix = s.ix;
        r.iy = s.iy;
        r.sp = s.sp;
        r.afAlt = s.afAlt;
        r.bcAlt = s.bcAlt;
        r.deAlt = s.deAlt;
        r.hlAlt = s.hlAlt;
        r.i = s.i;
        r.r = s.r;
        r.iff1 = r.iff2 = s.iff;
        r.im = 1;
        r.q = s.q;
        r.pc = kCodeAt;
        Z80CpuSetRegisters(_lib, &r);
        _libWrites.clear();
        _ports.value = s.port;
    }

    /// SCF / CCF bits 5 and 3 after the code at kCodeAt ran one step from s (native core)
    uint8_t ScfAfter(const State& s, std::initializer_list<uint8_t> code)
    {
        Load(kCodeAt, code);
        SetNative(s);
        _z80->Z80Step();
        Load(_z80->pc, {0x37});
        _z80->Z80Step();
        return _z80->f & 0x28;
    }
};

// DEC B from B = 0x29 gives B = 0x28, F = 0x2A (bits 5/3, N): with F already 0x2A the value does not change, the
// instruction still wrote it, so Q = 0x28 and SCF (A = 0) sees bits 5/3 = (0 | (0x2A & ~0x28)) = 0. Q left at 0 would
// give 0x28. z80ccf "DEC r" (tests 056 / 058)
TEST_F(QRegister_Test, FlagWriterWithUnchangedFLoadsQ)
{
    State s{};
    s.af = 0x002A;  // A = 0, F = what DEC B produces from B = 0x29
    s.bc = 0x2900;
    s.sp = 0xA000;
    s.q = 0x00;     // Q from an older instruction that wrote no flags
    Load(kCodeAt, {0x05});
    SetNative(s);
    _z80->Z80Step();
    ASSERT_EQ(_z80->b, 0x28);
    ASSERT_EQ(_z80->f, 0x2A) << "DEC B from B = 0x29";
    EXPECT_EQ(_z80->q, 0x28) << "DEC B wrote F (unchanged value): Q = F & 0x28";
    EXPECT_EQ(ScfAfter(s, {0x05}), 0x00);

    // INC (HL) with F already equal to its result: same rule through the (HL) form
    s.af = 0x0000;
    s.hl = 0x9000;
    Load(0x9000, {0x2F});  // INC (HL) -> 0x30: F = bit 5 of the result | H = 0x30
    Load(kCodeAt, {0x34});
    SetNative(s);
    _z80->f = 0x30;
    _z80->q = 0x00;
    _z80->Z80Step();
    ASSERT_EQ(_z80->f, 0x30);
    EXPECT_EQ(_z80->q, 0x20);
}

// POP AF and EX AF,AF' move F but do not write it through the flag logic: Q = 0. z80ccf tests 125 / 129
TEST_F(QRegister_Test, PopAfAndExAfClearQ)
{
    State s{};
    s.af = 0x0000;
    s.sp = 0xA000;
    s.q = 0x28;
    Load(0xA000, {0x28, 0x00});  // POP AF: F = 0x28, A = 0
    Load(kCodeAt, {0xF1});
    SetNative(s);
    _z80->Z80Step();
    ASSERT_EQ(_z80->f, 0x28);
    EXPECT_EQ(_z80->q, 0x00) << "POP AF";
    // SCF: (A | (F & ~Q)) = 0 | 0x28 = 0x28
    EXPECT_EQ(ScfAfter(s, {0xF1}), 0x28);

    s.afAlt = 0x0028;
    Load(kCodeAt, {0x08});
    SetNative(s);
    _z80->Z80Step();
    ASSERT_EQ(_z80->f, 0x28);
    EXPECT_EQ(_z80->q, 0x00) << "EX AF,AF'";
    EXPECT_EQ(ScfAfter(s, {0x08}), 0x28);
}

// Behind a DD / FD the prefixed instruction sets Q from its own class; a prefix alone (redundant DD) leaves Q = 0
TEST_F(QRegister_Test, PrefixedInstructionsSetQFromTheirMap)
{
    State s{};
    s.sp = 0xA000;
    s.af = 0x0100;
    s.ix = 0x9000;
    s.q = 0x28;

    Load(kCodeAt, {0xDD, 0x3D});  // DD DEC A: base class
    SetNative(s);
    _z80->f = 0x42;
    _z80->Z80Step();
    EXPECT_EQ(_z80->q, 0x00);

    Load(kCodeAt, {0xDD, 0xCB, 0x00, 0xC6});  // SET 0,(IX+0): no flags written
    SetNative(s);
    _z80->f = 0x28;
    _z80->Z80Step();
    EXPECT_EQ(_z80->q, 0x00);

    Load(kCodeAt, {0xFD, 0xCB, 0x00, 0x46});  // BIT 0,(IY+0): writes F
    SetNative(s);
    _z80->iy = 0x9000;
    _z80->Z80Step();
    EXPECT_EQ(_z80->q, _z80->f & 0x28);

    Load(kCodeAt, {0xDD, 0xFD});  // redundant DD: a step of its own, Q = 0
    SetNative(s);
    _z80->Z80Step();
    EXPECT_EQ(_z80->q, 0x00);
}

// Every instruction of every map against the unreal-z80 library: F, Q, and SCF after it
TEST_F(QRegister_Test, EveryInstructionMatchesTheLibrary)
{
    // Register states: F with bits 5/3 set and clear, A with bits 5/3 set and clear, a stale Q
    static const State kStates[] = {
        {0x0000, 0x0001, 0x0000, 0x9000, 0x9010, 0x9020, 0xA000, 0x28FF, 0x1234, 0x5678, 0x9ABC, 0x00, 0x00, 0x00, 0, 0x00},
        {0x28FF, 0xFF00, 0x8000, 0x9100, 0x9110, 0x9120, 0xA100, 0x0000, 0x0102, 0x0304, 0x0506, 0x80, 0x7F, 0x28, 1, 0xFF},
        {0xD728, 0x0100, 0x9200, 0x9200, 0x9210, 0x91F0, 0xA200, 0xFF28, 0x0001, 0x8000, 0x9300, 0x3F, 0x80, 0x08, 1, 0x28},
        {0x0142, 0x0007, 0x9300, 0x9300, 0x92F8, 0x9308, 0xA300, 0x0101, 0xFFFF, 0x0000, 0x9400, 0x10, 0x55, 0x20, 0, 0x81},
    };

    struct Map
    {
        const char* name;
        std::array<uint8_t, 3> prefix;  // up to DD CB d
        uint8_t prefixLength;
    };
    static const Map kMaps[] = {
        {"", {}, 0},
        {"CB ", {0xCB}, 1},
        {"ED ", {0xED}, 1},
        {"DD ", {0xDD}, 1},
        {"FD ", {0xFD}, 1},
        {"DD CB 05 ", {0xDD, 0xCB, 0x05}, 3},
        {"FD CB FB ", {0xFD, 0xCB, 0xFB}, 3},
    };

    // Memory the instructions read: a pattern with bits 5/3 in both states
    for (uint32_t a = 0x9000; a < 0xA400; ++a)
        _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(a), static_cast<uint8_t>(a * 37 + (a >> 8)));

    int mismatches = 0;
    int checked = 0;
    for (const Map& map : kMaps)
    {
        for (int op = 0; op < 256; ++op)
        {
            for (const State& s : kStates)
            {
                // The code: prefix bytes, the opcode, two operand bytes, then SCF where the step ends
                uint16_t at = kCodeAt;
                for (int k = 0; k < map.prefixLength; ++k)
                    _memory->DirectWriteToZ80Memory(at++, map.prefix[k]);
                _memory->DirectWriteToZ80Memory(at++, static_cast<uint8_t>(op));
                _memory->DirectWriteToZ80Memory(at++, 0x34);
                _memory->DirectWriteToZ80Memory(at++, 0x92);

                // The library first: its writes stay in its overlay, the machine's memory is untouched for the
                // native step
                SetLibrary(s);
                Z80CpuStep(_lib);
                Z80CpuRegisters lib{};
                Z80CpuGetRegisters(_lib, &lib);

                // Native: snapshot the bytes it may write (stack, data area) so every state starts the same
                std::array<uint8_t, 0x1400> saved;
                for (size_t k = 0; k < saved.size(); ++k)
                    saved[k] = _memory->DirectReadFromZ80Memory(static_cast<uint16_t>(0x9000 + k));
                SetNative(s);
                _z80->Z80Step();
                const uint8_t nativeF = _z80->f;
                const uint8_t nativeQ = _z80->q;
                const uint16_t nativePc = _z80->pc;

                ++checked;
                const bool sameStep = nativePc == lib.pc;
                const bool sameF = nativeF == static_cast<uint8_t>(lib.af);
                const bool sameQ = nativeQ == lib.q;

                // SCF after it, on both: what a program sees of Q
                bool sameScf = true;
                if (sameStep && sameF && sameQ && !_z80->halted)
                {
                    _memory->DirectWriteToZ80Memory(nativePc, 0x37);
                    _libWrites.erase(nativePc);
                    _z80->Z80Step();
                    Z80CpuStep(_lib);
                    Z80CpuGetRegisters(_lib, &lib);
                    sameScf = (_z80->f & 0x28) == (static_cast<uint8_t>(lib.af) & 0x28);
                }

                for (size_t k = 0; k < saved.size(); ++k)
                    _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x9000 + k), saved[k]);

                if (!(sameStep && sameF && sameQ && sameScf))
                {
                    if (++mismatches <= 40)
                    {
                        char line[160];
                        std::snprintf(line, sizeof(line),
                                      "%s%02X from AF=%04X Q=%02X: native F=%02X Q=%02X PC=%04X, library F=%02X Q=%02X "
                                      "PC=%04X%s",
                                      map.name, op, s.af, s.q, nativeF, nativeQ, nativePc,
                                      static_cast<uint8_t>(lib.af), lib.q, lib.pc, sameScf ? "" : ", SCF differs");
                        ADD_FAILURE() << line;
                    }
                }
            }
        }
    }
    EXPECT_EQ(mismatches, 0) << "of " << checked << " instructions";
    EXPECT_EQ(checked, 7 * 256 * 4);
}

// The tables themselves: the classes z80ccf checks, written out once more by mnemonic
TEST_F(QRegister_Test, TablesNameTheFlagWriters)
{
    using namespace z80q;
    for (uint8_t op : {0x3C, 0x3D, 0x34, 0x35, 0x09, 0x39, 0x07, 0x17, 0x27, 0x2F, 0x37, 0x3F, 0x80, 0xBF, 0xC6, 0xFE})
        EXPECT_EQ(kBase[op], kWrites) << std::hex << int(op);
    for (uint8_t op : {0x00, 0x01, 0x03, 0x08, 0x0A, 0x22, 0x40, 0x76, 0x7F, 0xC1, 0xC3, 0xC9, 0xE3, 0xF1, 0xF3, 0xFB})
        EXPECT_EQ(kBase[op], kKeeps) << std::hex << int(op);
    for (uint8_t op : {0xCB, 0xED, 0xDD, 0xFD})
        EXPECT_EQ(kBase[op], kPrefixed);
    for (uint8_t op : {0x40, 0x70, 0x78, 0x42, 0x4A, 0x44, 0x7C, 0x57, 0x5F, 0x67, 0x6F, 0xA0, 0xA1, 0xA2, 0xA3, 0xB0, 0xBB})
        EXPECT_EQ(kEd[op], kWrites) << std::hex << int(op);
    for (uint8_t op : {0x41, 0x43, 0x45, 0x46, 0x47, 0x4D, 0x4F, 0x77, 0x7F, 0x00, 0xA4, 0xBF, 0xFF})
        EXPECT_EQ(kEd[op], kKeeps) << std::hex << int(op);
    EXPECT_EQ(CbMask(0x00), kWrites);
    EXPECT_EQ(CbMask(0x7F), kWrites);  // BIT 7,A
    EXPECT_EQ(CbMask(0x80), kKeeps);   // RES
    EXPECT_EQ(CbMask(0xFF), kKeeps);   // SET
}
