#pragma once

/// @file ttdcausechain.h
/// @brief The TTD cause chain (docs/inprogress/2026-11-02-ttd-cause-chain/design.md): where a value came from.
///
/// A query names a memory byte or a register at a moment; the answer is a small tree of links, the target first.
/// Each link is a name (a register byte or a memory byte), the value it held, and the instruction that put it there
/// with the kind of that step (a store, a load, a copy, a computation, a constant, a port read...). The producer's
/// inputs are the link's children, followed as far as `depth` and `maxLinks` allow.
///
/// This header holds the types, the frame trace a replay fills (TimeTravelController::QueryCauseChain does the
/// replay) and the source rules (ExplainInstruction): which names an instruction wrote and what each came from. The
/// rules use the decoder table (analysis::DecodeOp) and the instruction's bus events, nothing about the program.

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/ttd/ttdcheckpoint.h"   // TTDTimePoint
#include "debugger/ttd/ttdphyspage.h"

namespace ttd
{

/// Register bytes, as names of the chain. The primed set are names of their own (EXX and EX AF,AF' are copies)
enum class TTDCauseReg : uint8_t
{
    A, F, B, C, D, E, H, L,
    IXH, IXL, IYH, IYL,
    I, R,
    A2, F2, B2, C2, D2, E2, H2, L2,   ///< A' F' B' C' D' E' H' L'
    SPH, SPL,
    Count
};

/// "A", "IXH", "A'", "SPH"...
const char* TTDCauseRegName(TTDCauseReg reg);
/// A register byte or pair by name, any case ("a", "hl", "ix", "sp", "af'", "h'"): the bytes it names, high first.
/// False for an unknown name
bool TTDCauseRegsByName(const std::string& name, std::vector<TTDCauseReg>& out);

/// A name of the chain: a register byte or a memory byte (Z80 address and the physical page behind it then)
struct TTDCauseName
{
    bool memory = false;
    TTDCauseReg reg = TTDCauseReg::A;
    uint16_t address = 0;
    PhysPage page = kPhysPageNone;
    uint8_t flip = 0;   ///< Memory::TtdKeyOf: the byte is at (address & #3FFF) ^ (flip << 13) of the page

    bool operator==(const TTDCauseName& o) const
    {
        return memory == o.memory && (memory ? (address == o.address && page == o.page && flip == o.flip) : reg == o.reg);
    }
    static TTDCauseName Reg(TTDCauseReg r)
    {
        TTDCauseName n;
        n.reg = r;
        return n;
    }
    static TTDCauseName Mem(uint16_t address, PhysPage page, uint8_t flip = 0)
    {
        TTDCauseName n;
        n.memory = true;
        n.address = address;
        n.page = page;
        n.flip = flip;
        return n;
    }
};

/// "(#5C78 RAM 5)", "(#0038 ROM)", "A"
std::string TTDCauseNameText(const TTDCauseName& name);

// ---------------------------------------------------------------------------------------------------------------
// The frame trace
// ---------------------------------------------------------------------------------------------------------------

/// One bus cycle of a traced instruction: 'R' / 'W' memory, 'I' / 'O' port
struct TTDTraceEvent
{
    char type = 'R';
    uint8_t value = 0;
    uint16_t address = 0;
    PhysPage page = kPhysPageNone;   ///< memory: the physical page behind the address at the access
    uint16_t offset = 0;             ///< memory: the byte's offset in `page` (Memory::TtdLocate)

    /// Memory::TtdKeyOf's flip of the byte
    uint8_t Flip() const { return static_cast<uint8_t>(((offset ^ address) >> 13) & 1u); }
    uint32_t tInFrame = 0;           ///< TTD time units, as the access probe records them
};

/// One instruction of a traced frame, or an interrupt's acknowledge (`interrupt`: its pushes and vector reads)
struct TTDTraceInsn
{
    uint32_t tInFrame = 0;           ///< at the instruction start
    uint16_t pc = 0;
    PhysPage codePage = kPhysPageNone;
    uint8_t bytes[4] = {};           ///< at the start, before it ran
    bool interrupt = false;          ///< an interrupt acknowledge, not an instruction (pc: the interrupted PC)
    uint8_t regs[static_cast<size_t>(TTDCauseReg::Count)] = {};   ///< at the start
    PhysPage banks[4] = {};          ///< the pages mapped at the start
    uint32_t firstEvent = 0;
    uint32_t eventCount = 0;
};

/// A frame's instructions in execution order, with their bus events, and the registers after the last one
struct TTDFrameTrace
{
    uint64_t frame = 0;
    std::vector<TTDTraceInsn> insns;
    std::vector<TTDTraceEvent> events;
    uint8_t finalRegs[static_cast<size_t>(TTDCauseReg::Count)] = {};
    /// At the frame start (read after the checkpoint restore): the word at SP and the IM 2 vector word at
    /// I * 256 + #FF, so a frame that starts inside an interrupt routine acknowledged at the frame before's end is seen
    uint16_t startStackWord = 0;
    uint16_t startIm2Entry = 0;

    /// The registers after instruction @p index (the next one's start, or finalRegs)
    const uint8_t* RegsAfter(size_t index) const
    {
        return index + 1 < insns.size() ? insns[index + 1].regs : finalRegs;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// The source rules
// ---------------------------------------------------------------------------------------------------------------

/// The kind of a link: how its producer made the value
enum class TTDCauseKind : uint8_t
{
    Store,       ///< a register written to memory (LD (mem),r, PUSH, LD (nn),rr, EX (SP),rr)
    Load,        ///< memory read into a register (LD r,(mem), POP, LD rr,(nn), EX (SP),rr)
    Copy,        ///< a register into another (LD r,r', EX, EXX, LD SP,HL, LD I,A)
    Computed,    ///< ALU, INC / DEC, rotates, bit operations on a register, pointer updates
    Constant,    ///< an immediate of the instruction (LD r,n, LD (HL),n, XOR A)
    Port,        ///< a port read (IN, INI ...)
    Block,       ///< a block copy (LDI, LDIR, LDD, LDDR): the byte read at HL
    Modify,      ///< a memory byte changed in place (INC (HL), RLC (HL), SET b,(HL), RLD)
    Return,      ///< a return address pushed by CALL / RST
    Interrupt,   ///< the PC pushed by an interrupt acknowledge
    Refresh,     ///< the R register: the refresh counter
    Initial,     ///< not written in the recorded history: held since the session start
    Rom,         ///< a ROM byte
    Device,      ///< written by something other than the CPU (a device, DMA, a ROM trap): the CPU wrote another value
    Stop,        ///< the chain stops here (see the link's stop reason)
};
const char* TTDCauseKindName(TTDCauseKind kind);

/// One input of an output: a name followed further, or a leaf value (the constant, the port read)
struct TTDCauseInput
{
    TTDCauseName name;
    uint8_t value = 0;               ///< the value the producer used
    bool followed = true;            ///< a name the chain goes on with (false: F unless asked for)
};

/// One name an instruction wrote and what it came from
struct TTDCauseOutput
{
    TTDCauseName name;
    uint8_t value = 0;
    TTDCauseKind kind = TTDCauseKind::Computed;
    std::vector<TTDCauseInput> inputs;   ///< names; empty for a leaf kind (Constant, Port, Return, Interrupt, Refresh)
    uint16_t port = 0;                   ///< Port: the port read
    uint16_t leafValue = 0;              ///< Return / Interrupt: the address pushed
};

/// True when instruction @p index of @p trace wrote @p name (a register byte, or a memory byte at its page)
bool TTDCauseWrites(const TTDFrameTrace& trace, size_t index, const TTDCauseName& name);
/// The register bytes instruction @p index of @p trace wrote: a bit per TTDCauseReg
uint32_t TTDCauseWrittenRegs(const TTDFrameTrace& trace, size_t index);

/// What instruction @p index of @p trace wrote and where each value came from (design §4). @p followFlags: F is an
/// input the chain follows (otherwise it is listed only)
std::vector<TTDCauseOutput> TTDExplainInstruction(const TTDFrameTrace& trace, size_t index, bool followFlags);

// ---------------------------------------------------------------------------------------------------------------
// The query and the answer
// ---------------------------------------------------------------------------------------------------------------

struct TTDCauseQuery
{
    bool memory = true;              ///< a memory byte (else a register)
    uint16_t address = 0;
    bool hasPage = false;            ///< memory: the physical page given (else the one mapped at `at`)
    PhysPage page = kPhysPageNone;
    std::string reg;                 ///< register: a byte or a pair ("A", "HL", "IX", "SP", "BC'")
    bool hasTime = false;            ///< at: this moment (else the current position)
    TTDTimePoint at;
    uint32_t depth = 4;
    uint32_t maxLinks = 32;
    uint32_t maxFrames = 50;
    bool followFlags = false;

    static constexpr uint32_t kMaxDepth = 16;
    static constexpr uint32_t kMaxLinks = 256;
    static constexpr uint32_t kMaxFrames = 1000;
};

/// One link of the answer: a name, its value and how it got it
struct TTDCauseLink
{
    int32_t parent = -1;             ///< index of the link whose producer read this name (-1: the target)
    uint32_t depth = 0;
    TTDCauseName name;
    uint8_t value = 0;
    TTDCauseKind kind = TTDCauseKind::Stop;
    bool hasProducer = false;        ///< an instruction (or an interrupt acknowledge) produced it
    TTDTimePoint time;               ///< the producer's start
    uint16_t pc = 0;
    PhysPage codePage = kPhysPageNone;
    std::string text;                ///< the producer's instruction ("LD (HL),A")
    std::vector<std::string> inputs; ///< the producer's input names with their values ("A=#03")
    std::string detail;              ///< a port read: "port #FE (ULA)"; a return address; a device write's values
    std::string stop;                ///< why the chain stops here: depth, max_links, max_frames, session_start,
                                     ///< barrier, seen (link N), mismatch
};

struct TTDCauseResult
{
    std::string error;               ///< non-empty: no answer (no history, a bad target)
    TTDTimePoint at;                 ///< the moment asked about
    std::vector<TTDCauseLink> links; ///< depth first: a link's inputs follow it
    uint32_t framesTraced = 0;
    double ms = 0.0;
};

/// The answer as indented text, one line per link (the MCP and CLI form)
std::string TTDCauseChainText(const TTDCauseResult& result);

}  // namespace ttd
