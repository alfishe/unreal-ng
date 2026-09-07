#pragma once

#include "stdafx.h"

#include "emulator/io/tape/tape.h"

class EmulatorContext;
class ModuleLogger;
class Z80;

/// region <Constants>

/// T-states charged for the LD-BYTES invocation itself — the CALL/RET balance
/// of the replaced routine (design §8). Payload stores additionally self-account
/// ~3T per byte (+ ULA contention) through the CPU write path, so a full screen
/// load still completes well within a single frame.
constexpr uint16_t kFastLoadTStates = 128;

/// Byte signature of the LD-BYTES prologue at $0556 (INC D / EX AF,AF' / DEC D /
/// DI / LD A,$0F / OUT ($FE),A) — verified against data/rom/48.rom.
constexpr size_t kLDBytesSignature1Size = 8;

/// Byte signature of the EAR poll at $0562 (IN A,($FE) / RRA) — the same anchor
/// the legacy 0x0564 auto-start hack uses.
constexpr size_t kLDBytesSignature2Size = 3;

/// Offset of the EAR poll anchor from LD_BYTES: $0562 - $0556 = 12 (the
/// prologue's OUT is followed by LD HL,$053F / PUSH HL before the poll).
constexpr size_t kLDBytesSignature2Offset = 12;

/// endregion </Constants>

/// Fast tape loading trap (design: docs/inprogress/2026-08-30-fast-tape-loading).
///
/// Replaces the whole ROM LD-BYTES ($0556) invocation at its first opcode fetch:
/// the block payload is copied from the tape image directly into Z80 memory and
/// the routine's documented exit state is emulated. Any decline is completely
/// inert — the CPU proceeds into the real ROM code and the signal pipeline takes
/// over from the same tape position (seamless fallback, no restart, no position
/// loss).
///
/// Arm state is evaluated lazily, only when PC actually sits at $0556 (a rare
/// event by definition): 'fasttape' feature live-read (CLI / WebAPI / Qt
/// toggles take effect immediately), TR-DOS session check, and a byte
/// signature check of the bank currently mapped at Z80 $0000-$3FFF. The signature is the ground truth —
/// it automatically covers 128K ROM paging (the editor ROM0 does not contain
/// the loader), Pentagon/Scorpion SOS derivatives and TR-DOS paging, with zero
/// per-model logic.
class TapeFastLoad
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_IO;
    const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_TAPE;
    ModuleLogger* _logger;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
    EmulatorContext* _context;
    Tape& _tape;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    TapeFastLoad(EmulatorContext* context, Tape& tape);
    virtual ~TapeFastLoad();
    /// endregion </Constructors / destructors>

    /// region <Trap interface>
public:
    /// Full arm check: 'fasttape' feature enabled, no active TR-DOS session,
    /// and the bank mapped at Z80 $0000-$3FFF really carries LD-BYTES. Cheap by
    /// construction (only ever consulted when PC == $0556).
    bool IsArmed() const;

    /// Handle a LD-BYTES invocation at PC == $0556.
    /// Returns true when the trap consumed the invocation (Z80Step must return
    /// immediately — the routine never executes). Returns false = decline: no
    /// CPU, memory or tape state was touched (image loading excepted, which is
    /// CPU-inert), and the ROM loader must execute for real.
    bool HandleLDBytesTrap(Z80& cpu);
    /// endregion </Trap interface>

    /// region <Helper methods>
protected:
    /// Byte signature check of the mapped bank 0 against the known LD-BYTES
    /// prologue (see design §6.1).
    bool CheckROMSignature() const;

    /// Block at the consumption cursor, or nullptr when at end-of-tape.
    const TapeBlock* PeekNextBlock();

    /// Vanilla-block gate (design §6.2 rows 6-8): expected flag, XOR checksum,
    /// exact payload length.
    bool BlockMatches(const TapeBlock& block, uint8_t expectedFlag, uint16_t expectedLength) const;

    /// Perform the routine's memory side effects and register postconditions
    /// (design §7): payload stores via the hooked CPU write path, RET semantics,
    /// IX/DE/AF update, synthetic M1 accounting.
    void ApplyLoadEffects(Z80& cpu, const TapeBlock& block);

    /// XOR of all block bytes (flag ... checksum) must be zero — the ROM's
    /// own validity test (CP $01 at $05E0 succeeds iff the total XOR is 0).
    static bool IsBlockChecksumValid(const TapeBlock& block);
    /// endregion </Helper methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class TapeFastLoadCUT : public TapeFastLoad
{
public:
    TapeFastLoadCUT(EmulatorContext* context, Tape& tape) : TapeFastLoad(context, tape) {};

public:
    using TapeFastLoad::_context;
    using TapeFastLoad::_tape;

    using TapeFastLoad::CheckROMSignature;
    using TapeFastLoad::PeekNextBlock;
    using TapeFastLoad::BlockMatches;
    using TapeFastLoad::ApplyLoadEffects;
    using TapeFastLoad::IsBlockChecksumValid;
};

#endif // _CODE_UNDER_TEST
