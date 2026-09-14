/// @file z80_predecode_benchmark.cpp
/// @brief Benchmark comparing current Z80 interpretation vs predecoded micro-ops
///
/// Tests the hypothesis: predecoding Z80 instructions into cached micro-ops,
/// keyed by physical address, should yield 1.5-3x speedup by eliminating:
/// - Repeated prefix parsing (DD/FD/CB/ED, especially DDCB with mid-opcode displacement)
/// - Multiple memory fetches per instruction
/// - Function pointer dispatch overhead
///
/// Approach: cache decoded instruction info at physical addresses, invalidate on write.

#include <benchmark/benchmark.h>

#include <array>
#include <cstring>
#include <unordered_map>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"

/// Predecoded micro-op: all information needed to execute an instruction
/// without re-parsing the opcode stream.
struct PredecodedOp
{
    uint8_t length;         // Total instruction length (1-4 bytes)
    uint8_t prefix;         // 0=none, 0xCB, 0xDD, 0xFD, 0xED
    uint8_t prefix2;        // Second prefix for DDCB/FDCB
    uint8_t opcode;         // Main opcode byte
    int8_t displacement;    // IX/IY displacement for DDCB/FDCB (-128..127)
    uint8_t cycles_base;    // Base cycle count (not counting memory contention)
    uint8_t flags;          // Flags: has_operand, reads_mem, writes_mem, etc.

    enum Flags : uint8_t
    {
        FLAG_VALID = 0x01,
        FLAG_HAS_IMM8 = 0x02,
        FLAG_HAS_IMM16 = 0x04,
        FLAG_READS_MEM = 0x08,
        FLAG_WRITES_MEM = 0x10,
        FLAG_BRANCHES = 0x20,
        FLAG_DDCB_FDCB = 0x40,
    };
};

/// Simple predecode cache for 64KB address space
/// In real implementation, would be keyed by physical page + offset with write invalidation
class PredecodeCache
{
public:
    static constexpr size_t CACHE_SIZE = 65536;

    PredecodeCache()
    {
        clear();
    }

    void clear()
    {
        std::memset(m_cache.data(), 0, sizeof(m_cache));
    }

    const PredecodedOp* lookup(uint16_t addr) const
    {
        const PredecodedOp& op = m_cache[addr];
        return (op.flags & PredecodedOp::FLAG_VALID) ? &op : nullptr;
    }

    void store(uint16_t addr, const PredecodedOp& op)
    {
        m_cache[addr] = op;
        m_cache[addr].flags |= PredecodedOp::FLAG_VALID;
    }

    void invalidate(uint16_t addr)
    {
        m_cache[addr].flags = 0;
    }

private:
    std::array<PredecodedOp, CACHE_SIZE> m_cache;
};

/// Predecode a Z80 instruction at given address
/// Returns instruction length, fills PredecodedOp structure
static int predecode_instruction(Memory& mem, uint16_t pc, PredecodedOp& out)
{
    out = {};
    out.flags = PredecodedOp::FLAG_VALID;

    uint8_t byte0 = mem.DirectReadFromZ80Memory(pc);
    int len = 1;

    // Handle prefixes
    if (byte0 == 0xDD || byte0 == 0xFD)
    {
        out.prefix = byte0;
        uint8_t byte1 = mem.DirectReadFromZ80Memory(pc + 1);
        len = 2;

        // Check for DDCB/FDCB
        if (byte1 == 0xCB)
        {
            out.prefix2 = 0xCB;
            out.flags |= PredecodedOp::FLAG_DDCB_FDCB;
            out.displacement = static_cast<int8_t>(mem.DirectReadFromZ80Memory(pc + 2));
            out.opcode = mem.DirectReadFromZ80Memory(pc + 3);
            len = 4;
            out.cycles_base = 23;  // DDCB/FDCB instructions are expensive
        }
        // Check for chained DD/FD (treated as NOP by hardware)
        else if (byte1 == 0xDD || byte1 == 0xFD)
        {
            out.opcode = byte0;  // Just the prefix, next will re-parse
            out.cycles_base = 4;
        }
        else if (byte1 == 0xED)
        {
            // DD ED / FD ED - prefix ignored, execute ED opcode
            out.prefix = 0xED;
            out.opcode = mem.DirectReadFromZ80Memory(pc + 2);
            len = 3;
            out.cycles_base = 8;
        }
        else
        {
            out.opcode = byte1;
            // IX/IY indexed instructions often have displacement
            // Simplified: assume 3 bytes for (IX+d) type, 2 otherwise
            if ((byte1 & 0xC7) == 0x46 || (byte1 & 0xC7) == 0x86 ||
                (byte1 & 0xF8) == 0x70 || byte1 == 0x36)
            {
                out.displacement = static_cast<int8_t>(mem.DirectReadFromZ80Memory(pc + 2));
                len = 3;
                if (byte1 == 0x36) len = 4;  // LD (IX+d),n
            }
            out.cycles_base = 8;
        }
    }
    else if (byte0 == 0xCB)
    {
        out.prefix = 0xCB;
        out.opcode = mem.DirectReadFromZ80Memory(pc + 1);
        len = 2;
        out.cycles_base = 8;
    }
    else if (byte0 == 0xED)
    {
        out.prefix = 0xED;
        out.opcode = mem.DirectReadFromZ80Memory(pc + 1);
        len = 2;
        // ED instructions vary: 8-23 cycles
        out.cycles_base = 8;
    }
    else
    {
        out.opcode = byte0;
        // Determine length from opcode encoding
        // 1-byte: most instructions
        // 2-byte: immediate byte (LD r,n, etc)
        // 3-byte: immediate word (LD rr,nn, JP, CALL, etc)

        switch (byte0 & 0xC7)
        {
        case 0x06: // LD r,n
            out.flags |= PredecodedOp::FLAG_HAS_IMM8;
            len = 2;
            break;
        case 0xC6: // ALU A,n
            out.flags |= PredecodedOp::FLAG_HAS_IMM8;
            len = 2;
            break;
        }

        switch (byte0 & 0xCF)
        {
        case 0x01: // LD rr,nn
            out.flags |= PredecodedOp::FLAG_HAS_IMM16;
            len = 3;
            break;
        }

        switch (byte0)
        {
        case 0xC3: case 0xC2: case 0xCA: case 0xD2: case 0xDA:
        case 0xE2: case 0xEA: case 0xF2: case 0xFA: // JP
        case 0xCD: case 0xC4: case 0xCC: case 0xD4: case 0xDC:
        case 0xE4: case 0xEC: case 0xF4: case 0xFC: // CALL
        case 0x22: case 0x2A: case 0x32: case 0x3A: // LD (nn),A etc
            len = 3;
            out.flags |= PredecodedOp::FLAG_HAS_IMM16;
            break;
        case 0x10: case 0x18: case 0x20: case 0x28: case 0x30: case 0x38: // JR/DJNZ
            len = 2;
            out.flags |= PredecodedOp::FLAG_HAS_IMM8 | PredecodedOp::FLAG_BRANCHES;
            break;
        case 0xD3: case 0xDB: // OUT (n),A / IN A,(n)
            len = 2;
            out.flags |= PredecodedOp::FLAG_HAS_IMM8;
            break;
        }

        out.cycles_base = 4;  // Most simple instructions
    }

    out.length = len;
    return len;
}

/// Count executed instructions over N frames using standard interpreter
static uint64_t count_instructions_interpreted(MainLoopCUT* mainLoop, Z80* cpu, int frames)
{
    uint64_t count = 0;
    uint16_t last_pc = cpu->pc;

    for (int f = 0; f < frames; f++)
    {
        uint32_t start_t = cpu->t;
        mainLoop->RunFramePublic();
        // Approximate instruction count from t-states (avg ~10 t-states/instr for Spectrum code)
        count += (cpu->t - start_t + cpu->tpi) / 10;
    }

    return count;
}

/// Benchmark: baseline frame execution (current interpreter)
static void BM_Z80_Baseline_FrameExecution(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModel("z80-predecode-bench", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* ctx = emulator->GetContext();
    MainLoopCUT* mainLoop = ctx ? reinterpret_cast<MainLoopCUT*>(ctx->pMainLoop) : nullptr;
    if (!mainLoop)
    {
        state.SkipWithError("main loop unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    // Warm-up
    for (int i = 0; i < 10; i++)
        mainLoop->RunFramePublic();

    for (auto _ : state)
    {
        mainLoop->RunFramePublic();
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel("baseline interpreter");

    manager->RemoveEmulator(emulator->GetUUID());
}

/// Benchmark: predecode overhead - measure cost of predecoding all ROM
static void BM_Z80_Predecode_ROMScan(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModel("z80-predecode-scan", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* ctx = emulator->GetContext();
    Memory* mem = ctx ? ctx->pMemory : nullptr;
    if (!mem)
    {
        state.SkipWithError("memory unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    PredecodeCache cache;

    for (auto _ : state)
    {
        cache.clear();

        // Predecode 16KB ROM region
        PredecodedOp op;
        for (uint16_t addr = 0; addr < 0x4000; )
        {
            int len = predecode_instruction(*mem, addr, op);
            cache.store(addr, op);
            addr += len;
        }
    }

    state.SetLabel("predecode 16KB ROM");
    manager->RemoveEmulator(emulator->GetUUID());
}

/// Benchmark: cache lookup overhead
static void BM_Z80_CacheLookup(benchmark::State& state)
{
    PredecodeCache cache;

    // Pre-populate cache
    PredecodedOp dummy{};
    dummy.flags = PredecodedOp::FLAG_VALID;
    dummy.length = 1;
    dummy.opcode = 0x00;

    for (uint16_t i = 0; i < 0x4000; i++)
    {
        cache.store(i, dummy);
    }

    uint64_t hits = 0;
    for (auto _ : state)
    {
        // Simulate hot path: lookup at sequential addresses
        for (uint16_t addr = 0; addr < 0x4000; addr++)
        {
            const PredecodedOp* op = cache.lookup(addr);
            if (op) hits++;
        }
    }

    state.SetItemsProcessed(state.iterations() * 0x4000);
    state.SetLabel("cache lookups");
    benchmark::DoNotOptimize(hits);
}

/// Benchmark: simulated predecoded execution
/// This simulates what a predecoded interpreter would do: lookup cached op,
/// skip decode, directly execute. We can't actually execute differently here,
/// but we measure the overhead difference.
static void BM_Z80_Predecoded_Simulated(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModel("z80-predecode-sim", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* ctx = emulator->GetContext();
    MainLoopCUT* mainLoop = ctx ? reinterpret_cast<MainLoopCUT*>(ctx->pMainLoop) : nullptr;
    Memory* mem = ctx ? ctx->pMemory : nullptr;
    Z80* cpu = ctx && ctx->pCore ? ctx->pCore->GetZ80() : nullptr;

    if (!mainLoop || !mem || !cpu)
    {
        state.SkipWithError("components unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    // Pre-populate cache for ROM area
    PredecodeCache cache;
    PredecodedOp op;
    for (uint16_t addr = 0; addr < 0x4000; )
    {
        int len = predecode_instruction(*mem, addr, op);
        cache.store(addr, op);
        addr += len;
    }

    // Warm-up
    for (int i = 0; i < 10; i++)
        mainLoop->RunFramePublic();

    uint64_t cache_hits = 0;
    uint64_t cache_misses = 0;

    for (auto _ : state)
    {
        // Run frame normally, but also simulate cache activity
        uint16_t pc_before = cpu->pc;
        mainLoop->RunFramePublic();

        // Simulate: check if we would have had cache hits
        // (In real implementation, the frame loop would use cached ops)
        if (pc_before < 0x4000)
        {
            const PredecodedOp* cached = cache.lookup(pc_before);
            if (cached) cache_hits++;
            else cache_misses++;
        }
    }

    state.SetItemsProcessed(state.iterations());
    state.counters["cache_hits"] = cache_hits;
    state.counters["cache_misses"] = cache_misses;
    state.SetLabel("with predecode cache");

    manager->RemoveEmulator(emulator->GetUUID());
}

/// Benchmark: measure actual decode overhead by comparing NOP execution
/// NOPs have minimal execution cost, so the difference shows pure decode overhead
static void BM_Z80_DecodeOverhead_NOP(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModel("z80-nop-bench", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* ctx = emulator->GetContext();
    Memory* mem = ctx ? ctx->pMemory : nullptr;
    Z80* cpu = ctx && ctx->pCore ? ctx->pCore->GetZ80() : nullptr;

    if (!mem || !cpu)
    {
        state.SkipWithError("components unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    // Fill a region with NOPs
    const uint16_t TEST_ADDR = 0x8000;
    const int NOP_COUNT = 1000;
    for (int i = 0; i < NOP_COUNT; i++)
    {
        mem->DirectWriteToZ80Memory(TEST_ADDR + i, 0x00);  // NOP
    }
    // Add a jump back at the end
    mem->DirectWriteToZ80Memory(TEST_ADDR + NOP_COUNT, 0xC3);     // JP
    mem->DirectWriteToZ80Memory(TEST_ADDR + NOP_COUNT + 1, TEST_ADDR & 0xFF);
    mem->DirectWriteToZ80Memory(TEST_ADDR + NOP_COUNT + 2, TEST_ADDR >> 8);

    cpu->pc = TEST_ADDR;
    cpu->t = 0;

    for (auto _ : state)
    {
        // Execute 1000 NOPs (each NOP is 4 t-states, so 4000 t-states total)
        for (int i = 0; i < NOP_COUNT; i++)
        {
            cpu->Z80Step();
        }
    }

    state.SetItemsProcessed(state.iterations() * NOP_COUNT);
    state.SetLabel("1000 NOPs");

    manager->RemoveEmulator(emulator->GetUUID());
}

/// Benchmark: measure decode overhead for prefixed instructions (DD prefix)
static void BM_Z80_DecodeOverhead_DD_Prefix(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModel("z80-dd-bench", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* ctx = emulator->GetContext();
    Memory* mem = ctx ? ctx->pMemory : nullptr;
    Z80* cpu = ctx && ctx->pCore ? ctx->pCore->GetZ80() : nullptr;

    if (!mem || !cpu)
    {
        state.SkipWithError("components unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    // Fill with DD 00 (IX prefix + NOP equivalent)
    const uint16_t TEST_ADDR = 0x8000;
    const int INSTR_COUNT = 500;
    for (int i = 0; i < INSTR_COUNT; i++)
    {
        mem->DirectWriteToZ80Memory(TEST_ADDR + i * 2, 0xDD);     // DD prefix
        mem->DirectWriteToZ80Memory(TEST_ADDR + i * 2 + 1, 0x00); // NOP
    }
    // Jump back
    mem->DirectWriteToZ80Memory(TEST_ADDR + INSTR_COUNT * 2, 0xC3);
    mem->DirectWriteToZ80Memory(TEST_ADDR + INSTR_COUNT * 2 + 1, TEST_ADDR & 0xFF);
    mem->DirectWriteToZ80Memory(TEST_ADDR + INSTR_COUNT * 2 + 2, TEST_ADDR >> 8);

    cpu->pc = TEST_ADDR;
    cpu->t = 0;

    for (auto _ : state)
    {
        for (int i = 0; i < INSTR_COUNT; i++)
        {
            cpu->Z80Step();
        }
    }

    state.SetItemsProcessed(state.iterations() * INSTR_COUNT);
    state.SetLabel("500 DD-prefixed");

    manager->RemoveEmulator(emulator->GetUUID());
}

/// Benchmark: measure decode overhead for DDCB instructions (most complex)
static void BM_Z80_DecodeOverhead_DDCB(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModel("z80-ddcb-bench", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* ctx = emulator->GetContext();
    Memory* mem = ctx ? ctx->pMemory : nullptr;
    Z80* cpu = ctx && ctx->pCore ? ctx->pCore->GetZ80() : nullptr;

    if (!mem || !cpu)
    {
        state.SkipWithError("components unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    // Fill with DDCB xx 46 (BIT 0,(IX+d)) - 4-byte instructions
    const uint16_t TEST_ADDR = 0x8000;
    const int INSTR_COUNT = 250;
    cpu->ix = 0x9000;  // Point IX somewhere safe

    for (int i = 0; i < INSTR_COUNT; i++)
    {
        mem->DirectWriteToZ80Memory(TEST_ADDR + i * 4, 0xDD);     // DD prefix
        mem->DirectWriteToZ80Memory(TEST_ADDR + i * 4 + 1, 0xCB); // CB prefix
        mem->DirectWriteToZ80Memory(TEST_ADDR + i * 4 + 2, 0x00); // displacement
        mem->DirectWriteToZ80Memory(TEST_ADDR + i * 4 + 3, 0x46); // BIT 0,(IX+0)
    }
    // Jump back
    mem->DirectWriteToZ80Memory(TEST_ADDR + INSTR_COUNT * 4, 0xC3);
    mem->DirectWriteToZ80Memory(TEST_ADDR + INSTR_COUNT * 4 + 1, TEST_ADDR & 0xFF);
    mem->DirectWriteToZ80Memory(TEST_ADDR + INSTR_COUNT * 4 + 2, TEST_ADDR >> 8);

    cpu->pc = TEST_ADDR;
    cpu->t = 0;

    for (auto _ : state)
    {
        for (int i = 0; i < INSTR_COUNT; i++)
        {
            cpu->Z80Step();
        }
    }

    state.SetItemsProcessed(state.iterations() * INSTR_COUNT);
    state.SetLabel("250 DDCB 4-byte");

    manager->RemoveEmulator(emulator->GetUUID());
}

/// Benchmark: isolated decode timing - execute large block of mixed instructions
/// Measures pure Z80Step cost without frame overhead (video, audio, etc)
static void BM_Z80_MixedInstructions_Block(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModel("z80-mixed-bench", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* ctx = emulator->GetContext();
    Memory* mem = ctx ? ctx->pMemory : nullptr;
    Z80* cpu = ctx && ctx->pCore ? ctx->pCore->GetZ80() : nullptr;

    if (!mem || !cpu)
    {
        state.SkipWithError("components unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    // Create realistic mixed code:
    // - Simple ops (LD, INC, NOP)
    // - DD/FD prefixed (IX/IY ops)
    // - DDCB/FDCB (bit ops on indexed)
    // - ED prefixed (block ops)
    const uint16_t TEST_ADDR = 0x8000;
    uint16_t addr = TEST_ADDR;

    cpu->ix = 0x9000;
    cpu->iy = 0x9100;

    // Pattern: 60% simple, 25% DD/FD, 10% ED, 5% DDCB
    for (int block = 0; block < 100; block++)
    {
        // 6 simple ops
        mem->DirectWriteToZ80Memory(addr++, 0x00); // NOP
        mem->DirectWriteToZ80Memory(addr++, 0x3C); // INC A
        mem->DirectWriteToZ80Memory(addr++, 0x04); // INC B
        mem->DirectWriteToZ80Memory(addr++, 0x78); // LD A,B
        mem->DirectWriteToZ80Memory(addr++, 0x47); // LD B,A
        mem->DirectWriteToZ80Memory(addr++, 0x00); // NOP

        // 2-3 DD prefixed (IX ops)
        mem->DirectWriteToZ80Memory(addr++, 0xDD);
        mem->DirectWriteToZ80Memory(addr++, 0x23); // INC IX
        mem->DirectWriteToZ80Memory(addr++, 0xDD);
        mem->DirectWriteToZ80Memory(addr++, 0x7E); // LD A,(IX+d)
        mem->DirectWriteToZ80Memory(addr++, 0x00); // displacement

        // 1 ED prefixed
        mem->DirectWriteToZ80Memory(addr++, 0xED);
        mem->DirectWriteToZ80Memory(addr++, 0x44); // NEG

        // 0-1 DDCB (every other block)
        if (block % 2 == 0)
        {
            mem->DirectWriteToZ80Memory(addr++, 0xDD);
            mem->DirectWriteToZ80Memory(addr++, 0xCB);
            mem->DirectWriteToZ80Memory(addr++, 0x00); // displacement
            mem->DirectWriteToZ80Memory(addr++, 0x46); // BIT 0,(IX+0)
        }
    }

    // Jump back
    mem->DirectWriteToZ80Memory(addr++, 0xC3);
    mem->DirectWriteToZ80Memory(addr++, TEST_ADDR & 0xFF);
    mem->DirectWriteToZ80Memory(addr++, TEST_ADDR >> 8);

    const int INSTRUCTIONS_PER_LOOP = 100 * (6 + 3 + 1) + 50 * 1; // ~1050 instructions per loop
    const int LOOPS = 10;

    cpu->pc = TEST_ADDR;
    cpu->t = 0;

    for (auto _ : state)
    {
        for (int loop = 0; loop < LOOPS; loop++)
        {
            cpu->pc = TEST_ADDR;
            for (int i = 0; i < INSTRUCTIONS_PER_LOOP; i++)
            {
                cpu->Z80Step();
            }
        }
    }

    state.SetItemsProcessed(state.iterations() * INSTRUCTIONS_PER_LOOP * LOOPS);
    state.SetLabel("mixed real-world code");

    manager->RemoveEmulator(emulator->GetUUID());
}

/// Benchmark: compute decode overhead ratio
/// Compare time for same t-states with different instruction mixes
static void BM_Z80_DecodeOverhead_Ratio(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModel("z80-ratio-bench", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* ctx = emulator->GetContext();
    Memory* mem = ctx ? ctx->pMemory : nullptr;
    Z80* cpu = ctx && ctx->pCore ? ctx->pCore->GetZ80() : nullptr;

    if (!mem || !cpu)
    {
        state.SkipWithError("components unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    const int TARGET_TSTATES = 70000;  // ~1 frame worth
    const uint16_t TEST_ADDR = 0x8000;

    int mode = state.range(0);  // 0=NOP only, 1=DD only, 2=DDCB only

    if (mode == 0)
    {
        // All NOPs (4 t-states each)
        for (int i = 0; i < TARGET_TSTATES / 4; i++)
        {
            mem->DirectWriteToZ80Memory(TEST_ADDR + i, 0x00);
        }
        mem->DirectWriteToZ80Memory(TEST_ADDR + TARGET_TSTATES / 4, 0xC3);
        mem->DirectWriteToZ80Memory(TEST_ADDR + TARGET_TSTATES / 4 + 1, TEST_ADDR & 0xFF);
        mem->DirectWriteToZ80Memory(TEST_ADDR + TARGET_TSTATES / 4 + 2, TEST_ADDR >> 8);
    }
    else if (mode == 1)
    {
        // All DD+NOP (8 t-states each)
        cpu->ix = 0x9000;
        for (int i = 0; i < TARGET_TSTATES / 8; i++)
        {
            mem->DirectWriteToZ80Memory(TEST_ADDR + i * 2, 0xDD);
            mem->DirectWriteToZ80Memory(TEST_ADDR + i * 2 + 1, 0x00);
        }
        uint16_t end = TEST_ADDR + (TARGET_TSTATES / 8) * 2;
        mem->DirectWriteToZ80Memory(end, 0xC3);
        mem->DirectWriteToZ80Memory(end + 1, TEST_ADDR & 0xFF);
        mem->DirectWriteToZ80Memory(end + 2, TEST_ADDR >> 8);
    }
    else
    {
        // All DDCB BIT (20 t-states each)
        cpu->ix = 0x9000;
        for (int i = 0; i < TARGET_TSTATES / 20; i++)
        {
            mem->DirectWriteToZ80Memory(TEST_ADDR + i * 4, 0xDD);
            mem->DirectWriteToZ80Memory(TEST_ADDR + i * 4 + 1, 0xCB);
            mem->DirectWriteToZ80Memory(TEST_ADDR + i * 4 + 2, 0x00);
            mem->DirectWriteToZ80Memory(TEST_ADDR + i * 4 + 3, 0x46);
        }
        uint16_t end = TEST_ADDR + (TARGET_TSTATES / 20) * 4;
        mem->DirectWriteToZ80Memory(end, 0xC3);
        mem->DirectWriteToZ80Memory(end + 1, TEST_ADDR & 0xFF);
        mem->DirectWriteToZ80Memory(end + 2, TEST_ADDR >> 8);
    }

    int instr_count = (mode == 0) ? TARGET_TSTATES / 4 :
                      (mode == 1) ? TARGET_TSTATES / 8 :
                                    TARGET_TSTATES / 20;

    cpu->pc = TEST_ADDR;
    cpu->t = 0;

    for (auto _ : state)
    {
        cpu->pc = TEST_ADDR;
        for (int i = 0; i < instr_count; i++)
        {
            cpu->Z80Step();
        }
    }

    state.SetItemsProcessed(state.iterations() * instr_count);
    const char* labels[] = {"NOP-only (4T)", "DD-prefix (8T)", "DDCB (20T)"};
    state.SetLabel(labels[mode]);

    manager->RemoveEmulator(emulator->GetUUID());
}

BENCHMARK(BM_Z80_Baseline_FrameExecution)->Iterations(1000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Z80_Predecode_ROMScan)->Iterations(100)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Z80_CacheLookup)->Iterations(1000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Z80_Predecoded_Simulated)->Iterations(1000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Z80_DecodeOverhead_NOP)->Iterations(10000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Z80_DecodeOverhead_DD_Prefix)->Iterations(10000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Z80_DecodeOverhead_DDCB)->Iterations(10000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Z80_MixedInstructions_Block)->Iterations(5000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Z80_DecodeOverhead_Ratio)->Arg(0)->Arg(1)->Arg(2)->Iterations(1000)->Unit(benchmark::kMicrosecond);

/// Benchmark: Headless frame - CPU only, no video/audio rendering
/// This isolates pure CPU cost from the full frame pipeline
static void BM_Z80_Headless_CPUOnly(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModel("z80-headless", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* ctx = emulator->GetContext();
    Core* core = ctx ? ctx->pCore : nullptr;
    Z80* cpu = core ? core->GetZ80() : nullptr;

    if (!core || !cpu)
    {
        state.SkipWithError("core/cpu unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    // Warm-up
    for (int i = 0; i < 10; i++)
        core->CPUFrameCycle();

    for (auto _ : state)
    {
        core->CPUFrameCycle();
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel("CPU frame only (no video/audio)");

    manager->RemoveEmulator(emulator->GetUUID());
}

/// Benchmark: Headless with turbo mode enabled
static void BM_Z80_Headless_Turbo(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModel("z80-headless-turbo", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* ctx = emulator->GetContext();
    Core* core = ctx ? ctx->pCore : nullptr;
    Z80* cpu = core ? core->GetZ80() : nullptr;

    if (!core || !cpu)
    {
        state.SkipWithError("core/cpu unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    core->EnableTurboMode();

    // Warm-up
    for (int i = 0; i < 10; i++)
        core->CPUFrameCycle();

    for (auto _ : state)
    {
        core->CPUFrameCycle();
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel("CPU frame turbo (no video/audio)");

    core->DisableTurboMode();
    manager->RemoveEmulator(emulator->GetUUID());
}

/// Benchmark: Compare full frame vs headless CPU-only
/// Shows exact overhead of video/audio rendering
static void BM_Z80_FrameComparison(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModel("z80-compare", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* ctx = emulator->GetContext();
    MainLoopCUT* mainLoop = ctx ? reinterpret_cast<MainLoopCUT*>(ctx->pMainLoop) : nullptr;
    Core* core = ctx ? ctx->pCore : nullptr;

    if (!mainLoop || !core)
    {
        state.SkipWithError("components unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    int mode = state.range(0);  // 0=full frame, 1=CPU only

    // Warm-up
    for (int i = 0; i < 10; i++)
    {
        if (mode == 0)
            mainLoop->RunFramePublic();
        else
            core->CPUFrameCycle();
    }

    for (auto _ : state)
    {
        if (mode == 0)
            mainLoop->RunFramePublic();
        else
            core->CPUFrameCycle();
    }

    state.SetItemsProcessed(state.iterations());
    state.SetLabel(mode == 0 ? "full frame (video+audio+CPU)" : "CPU only (headless)");

    manager->RemoveEmulator(emulator->GetUUID());
}

BENCHMARK(BM_Z80_Headless_CPUOnly)->Iterations(2000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Z80_Headless_Turbo)->Iterations(2000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Z80_FrameComparison)->Arg(0)->Arg(1)->Iterations(1000)->Unit(benchmark::kMicrosecond);

/// Benchmark: Estimate decode vs execute ratio by comparing instruction mixes
/// Same number of instructions, different decode complexity
static void BM_Z80_DecodeVsExecute(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModel("z80-decode-exec", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* ctx = emulator->GetContext();
    Memory* mem = ctx ? ctx->pMemory : nullptr;
    Z80* cpu = ctx && ctx->pCore ? ctx->pCore->GetZ80() : nullptr;

    if (!mem || !cpu)
    {
        state.SkipWithError("components unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    const uint16_t TEST_ADDR = 0x8000;
    const int INSTR_COUNT = 7000;  // ~1 frame worth of instructions

    int mode = state.range(0);  // 0=simple, 1=DD-prefix, 2=DDCB

    cpu->ix = 0x9000;

    if (mode == 0)
    {
        // Simple: INC A (4T, 1 byte, minimal decode)
        for (int i = 0; i < INSTR_COUNT; i++)
            mem->DirectWriteToZ80Memory(TEST_ADDR + i, 0x3C);  // INC A
        mem->DirectWriteToZ80Memory(TEST_ADDR + INSTR_COUNT, 0xC3);
        mem->DirectWriteToZ80Memory(TEST_ADDR + INSTR_COUNT + 1, TEST_ADDR & 0xFF);
        mem->DirectWriteToZ80Memory(TEST_ADDR + INSTR_COUNT + 2, TEST_ADDR >> 8);
    }
    else if (mode == 1)
    {
        // DD-prefix: INC IX (10T, 2 bytes, moderate decode)
        for (int i = 0; i < INSTR_COUNT; i++)
        {
            mem->DirectWriteToZ80Memory(TEST_ADDR + i * 2, 0xDD);
            mem->DirectWriteToZ80Memory(TEST_ADDR + i * 2 + 1, 0x23);  // INC IX
        }
        uint16_t end = TEST_ADDR + INSTR_COUNT * 2;
        mem->DirectWriteToZ80Memory(end, 0xC3);
        mem->DirectWriteToZ80Memory(end + 1, TEST_ADDR & 0xFF);
        mem->DirectWriteToZ80Memory(end + 2, TEST_ADDR >> 8);
    }
    else
    {
        // DDCB: BIT 0,(IX+0) (20T, 4 bytes, heavy decode)
        for (int i = 0; i < INSTR_COUNT; i++)
        {
            mem->DirectWriteToZ80Memory(TEST_ADDR + i * 4, 0xDD);
            mem->DirectWriteToZ80Memory(TEST_ADDR + i * 4 + 1, 0xCB);
            mem->DirectWriteToZ80Memory(TEST_ADDR + i * 4 + 2, 0x00);
            mem->DirectWriteToZ80Memory(TEST_ADDR + i * 4 + 3, 0x46);
        }
        uint16_t end = TEST_ADDR + INSTR_COUNT * 4;
        mem->DirectWriteToZ80Memory(end, 0xC3);
        mem->DirectWriteToZ80Memory(end + 1, TEST_ADDR & 0xFF);
        mem->DirectWriteToZ80Memory(end + 2, TEST_ADDR >> 8);
    }

    cpu->pc = TEST_ADDR;
    cpu->t = 0;

    for (auto _ : state)
    {
        cpu->pc = TEST_ADDR;
        for (int i = 0; i < INSTR_COUNT; i++)
        {
            cpu->Z80Step();
        }
    }

    state.SetItemsProcessed(state.iterations() * INSTR_COUNT);
    const char* labels[] = {"7K simple (INC A)", "7K DD-prefix (INC IX)", "7K DDCB (BIT)"};
    state.SetLabel(labels[mode]);

    manager->RemoveEmulator(emulator->GetUUID());
}

BENCHMARK(BM_Z80_DecodeVsExecute)->Arg(0)->Arg(1)->Arg(2)->Iterations(2000)->Unit(benchmark::kMicrosecond);
