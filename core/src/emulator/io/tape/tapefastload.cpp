#include "tapefastload.h"

#include <cstring>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "emulator/cpu/cputables.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/spectrumconstants.h"

/// region <Constructors / destructors>

TapeFastLoad::TapeFastLoad(EmulatorContext* context, Tape& tape)
    : _context(context), _tape(tape)
{
    _logger = context->pModuleLogger;
}

TapeFastLoad::~TapeFastLoad()
{
    _context = nullptr;
}

/// endregion </Constructors / destructors>

/// region <Trap interface>

bool TapeFastLoad::IsArmed() const
{
    // Master feature gate: the whole mechanism lives behind the runtime
    // 'fasttape' feature (CLI 'feature fasttape' / setting fast_tape, WebAPI,
    // Qt Machine menu, features.ini). Live lookup with the same lazy discipline
    // as the rest of this method (design §6.1) — every real Emulator instance
    // owns a FeatureManager, so the null case only arises in degenerate
    // contexts that must not fast-load anyway.
    FeatureManager* featureManager = _context->pFeatureManager;
    if (featureManager == nullptr || !featureManager->isEnabled(Features::kFastTape))
        return false;

    // Active TR-DOS session: bank 0 carries the DOS ROM. The byte signature
    // below would fail anyway — this check just short-circuits the compares.
    if (_context->emulatorState.flags & CF_TRDOS)
        return false;

    return CheckROMSignature();
}

bool TapeFastLoad::HandleLDBytesTrap(Z80& cpu)
{
    /// region <Arm state>

    // Evaluated lazily — this method is only ever called when PC == $0556, so
    // the checks below never touch the per-step hot path (design §6.1).
    if (!IsArmed())
        return false;

    /// endregion </Arm state>

    /// region <Decline matrix (design §6.2) — checked in order; any failure is a fully inert decline>

    // Row 1: Fc == 0 -> VERIFY. The routine only compares bytes against memory;
    // the trap must not fire or it would store the tape payload over the very
    // bytes VERIFY exists to compare — silent corruption plus a false success
    // report (design §13). Signal emulation reproduces the real byte-compare.
    if ((cpu.f & FLAG_CF) == 0)
        return false;

    // Row 2: degenerate zero-length call — the real routine just waits for a
    // pilot tone that never resolves into data.
    if (cpu.de == 0)
        return false;

    // Row 3: no tape image loaded and none can be loaded. Image loading is
    // CPU-inert, so it is performed here as part of evaluating this row.
    if (!_tape.EnsureImageLoaded())
        return false;

    // Row 4: consumption cursor at end of tape — a real tape has run out.
    const size_t cursor = _tape.GetConsumptionCursor();
    const std::vector<TapeBlock>& blocks = _tape.GetBlocks();
    if (cursor >= blocks.size())
        return false;

    // Row 5: signal playback currently active — the cursor is mid-stream and a
    // second consumer would double-consume blocks.
    if (_tape.IsPlaying())
        return false;

    // Rows 6-8: vanilla-block gate (expected flag, XOR checksum, exact length).
    const TapeBlock& block = blocks[cursor];
    if (!BlockMatches(block, cpu.a, cpu.de))
        return false;

    /// endregion </Decline matrix>

    MLOGINFO("Fast tape load: block %zu, %u bytes -> $%04X", cursor, static_cast<unsigned>(cpu.de), cpu.ix);

    ApplyLoadEffects(cpu, block);
    _tape.ConsumeBlock(cursor);

    return true;
}

/// endregion </Trap interface>

/// region <Helper methods>

bool TapeFastLoad::CheckROMSignature() const
{
    // Whatever bank is ACTUALLY mapped at Z80 $0000-$3FFF right now. This is
    // the ground truth for "the loader is really there": it covers 48K SOS,
    // 128K ROM1 paging (the editor ROM0 has string data at $0556, not code),
    // Pentagon/Scorpion derivatives and TR-DOS paging with zero per-model
    // logic (design §6.1).
    const uint8_t* romBank = _context->pMemory->GetPhysicalAddressForZ80Page(0);
    if (romBank == nullptr)
        return false;

    // $0556: INC D / EX AF,AF' / DEC D / DI / LD A,$0F / OUT ($FE),A
    static constexpr uint8_t signature1[kLDBytesSignature1Size] = { 0x14, 0x08, 0x15, 0xF3, 0x3E, 0x0F, 0xD3, 0xFE };
    // $0562: IN A,($FE) / RRA — the EAR poll anchor (LD HL,$053F / PUSH HL
    // sits between it and the prologue, hence the +12 offset)
    static constexpr uint8_t signature2[kLDBytesSignature2Size] = { 0xDB, 0xFE, 0x1F };

    return std::memcmp(romBank + ROMAddresses::LD_BYTES, signature1, sizeof signature1) == 0 &&
           std::memcmp(romBank + ROMAddresses::LD_BYTES + kLDBytesSignature2Offset, signature2, sizeof signature2) == 0;
}

const TapeBlock* TapeFastLoad::PeekNextBlock()
{
    if (!_tape.EnsureImageLoaded())
        return nullptr;

    const size_t cursor = _tape.GetConsumptionCursor();
    const std::vector<TapeBlock>& blocks = _tape.GetBlocks();

    return cursor < blocks.size() ? &blocks[cursor] : nullptr;
}

bool TapeFastLoad::BlockMatches(const TapeBlock& block, uint8_t expectedFlag, uint16_t expectedLength) const
{
    // Minimum sane block: flag byte + at least one payload byte + checksum.
    if (block.data.size() < 3)
        return false;

    // Row 6: flag byte must equal the expected flag in A ($00 header / $FF data
    // for ROM callers). Custom-flag blocks decline here.
    if (block.data[0] != expectedFlag)
        return false;

    // Row 7: XOR of ALL bytes (flag ... checksum) must be zero — this is the
    // ROM's own validity test (CP $01 at $05E0 succeeds iff the total XOR is 0).
    // A decline makes the signal path reproduce the authentic loading error.
    if (!IsBlockChecksumValid(block))
        return false;

    // Row 8: payload length (block minus flag and trailing checksum) must equal
    // DE exactly (17 implied for header blocks).
    const size_t payloadLength = block.data.size() - 2;
    if (payloadLength != expectedLength)
        return false;

    return true;
}

void TapeFastLoad::ApplyLoadEffects(Z80& cpu, const TapeBlock& block)
{
    const size_t payloadLength = block.data.size() - 2;  // BlockMatches guaranteed >= 3

    // RET semantics: pc <- stacked return address. Both stack reads go through
    // the hooked CPU read path (self-accounting 3T each, like the real POPs).
    const uint16_t returnAddress =
        cpu.rd(cpu.sp) | (static_cast<uint16_t>(cpu.rd(static_cast<uint16_t>(cpu.sp + 1))) << 8);
    cpu.sp = static_cast<uint16_t>(cpu.sp + 2);

    // Payload stores: bytes [1 .. payloadLength] (skip the flag, exclude the
    // trailing checksum) written from IX with natural 64K wrap — through
    // Z80::wd, the same dispatch every CPU store uses. TTD dirty tracking,
    // watchpoints and debugger memory views therefore observe every store
    // (design §9.3); stores into ROM pages are dropped by the memory layer
    // exactly like on real hardware.
    uint16_t address = cpu.ix;
    for (size_t i = 0; i < payloadLength; i++, address++)
    {
        cpu.wd(address, block.data[1 + i]);
    }

    // Register postconditions (design §4.3 / §7)
    cpu.ix = static_cast<uint16_t>(cpu.ix + payloadLength);
    cpu.de = 0;

    // AF: the real routine ends with LD A,H (the running checksum, == 0 on this
    // valid-block path) followed by CP $01, and SA/LD-RET at $053F preserves
    // those flags through its PUSH AF / POP AF. Reproduce the exact result word
    // of CP $01 with A == 0: S Z Y H X PV N C = 1 0 1 1 1 1 1 1 = $BF — carry
    // set = success.
    cpu.a = 0x00;
    cpu.f = 0xBF;

    // B, C, H, L and AF' are clobbered by the real routine (A ends as the
    // checksum accumulator via CP $01 at $05E0); ROM callers do not depend on
    // them — the differential test (design §12.2-2) is the guard. Left as-is.
    //
    // IFF1: net unchanged. The trap skips both the DI at $0559 and the EI at
    // $054F inside SA/LD-RET, so ROM callers observe interrupts enabled either
    // way (design §7.1).

    cpu.pc = returnAddress;

    // Attribute the synthetic instruction to the replaced routine: TTD write
    // journal records and the next instruction's prev_pc then point at $0556.
    cpu.m1_pc = ROMAddresses::LD_BYTES;

    // Synthetic M1 accounting (design §8): R ticks like a single opcode fetch
    // and the invocation is charged its CALL/RET balance; the payload stores
    // above already advanced t by ~3T per byte (+ ULA contention).
    cpu.r_low = ((cpu.r_low + 1) & 0x7F) | (cpu.r_low & 0x80);
    cpu.tt += kFastLoadTStates * cpu.rate;
}

bool TapeFastLoad::IsBlockChecksumValid(const TapeBlock& block)
{
    uint8_t parity = 0;
    for (const uint8_t byte : block.data)
    {
        parity ^= byte;
    }

    return parity == 0;
}

/// endregion </Helper methods>
