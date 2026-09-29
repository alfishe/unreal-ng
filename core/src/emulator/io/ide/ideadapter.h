#pragma once

/// @file ideadapter.h
/// @brief The glue of every Spectrum IDE board: which Z80 ports reach which
/// ATA register, when the board answers, and how the 16-bit data word is
/// split into two 8-bit transfers (IDE design §3-§4, §9; ZX-Evo latch:
/// tdd-storage-sd-ide-cd.md §3.2). One class for all boards: the scheme
/// ([HDD] Scheme) picks the decode; the latches are plain data (TTD).
///
/// | Scheme | Ports | Register from | High byte | Answers when |
/// |---|---|---|---|---|
/// | NEMO | A2 = A1 = 0: CS0 when A4 A3 = 10, CS1 register 6 at #C8 | A7..A5 | latch at A0 = 1 (#11): read #10 → #11, write #11 → #10 | TR-DOS ports off |
/// | NEMO-A8 | as NEMO | A7..A5 | latch at A8 = 1 (#110) | TR-DOS ports off |
/// | NEMO-DIVIDE (ZX-Evo) | rrr1000x, #C8, and the RTL aliases #08 #28 #48 #68 #88 #A8 #E8 | A7..A5 | Nemo latch (#11) or two #10 accesses (low, then high); any other IDE port resets the pair | always |
/// | ATM | (port & #1F) = #0F | A7..A5 | latch at A8 = 1 (#FF0F) | TR-DOS ports on; the #7FFD-class read ((port & #8202) = #0200: #3F + INTRQ on bit 6) always |
/// | SMUC | #F8BE-#FFBE (A13 = 1); #D8BE latch (A13 = 0, always); #FFBA bit 7 turns #FEBE into the control block | A10..A8 | latch, Nemo order | TR-DOS ports on (via the Scorpion decoder) |
/// | PROFI | (port & #9F) = #8B: read #xxCB register / #xxEB latch, write #xxCB latch / #xxEB register, #06AB device control | A10..A8 | two latches, mirrored roles | Profi EXT mode |
/// | DIVIDE | (port & #E3) = #A3 (#E3 / #E7 / #EB are its paging, not IDE) | A4..A2 | two #A3 accesses (low, then high) | TR-DOS ports off |
///
/// Worked example (Nemo write): OUT (#11),#AB : OUT (#10),#CD sends the word
/// #ABCD; the image stores CD AB.

#include <cstdint>
#include <type_traits>

#include "emulator/platform.h"

class EmulatorContext;
class AtaChannel;

/// The adapter's latches, for TTD (no padding: its bytes are its value)
struct IdeAdapterState
{
    uint8_t readLatch = 0xFF;  ///< high byte of the last 16-bit read
    uint8_t writeLatch = 0;    ///< the byte waiting for the other half of a write
    uint8_t readPair = 0;      ///< a same-port read pair: the low byte was read
    uint8_t writePair = 0;     ///< a same-port write pair: the low byte waits in writeLatch
    uint8_t writeHigh = 0;     ///< ZX-Evo: #11 armed a Nemo-order write
    uint8_t reserved[3] = {};
};
static_assert(std::has_unique_object_representations_v<IdeAdapterState>, "IdeAdapterState must have no padding");

class IdeAdapter
{
public:
    /// What the machine's decoder knows about the bus state
    struct Gate
    {
        bool dosPorts = false;  ///< TR-DOS (Beta 128) ports are on
        bool profiExt = false;  ///< Profi EXT mode (#DFFD.5 and #7FFD.4)
    };

    explicit IdeAdapter(EmulatorContext* context) : _context(context) {}

    /// The board of the configured scheme decodes `port`: true when it is an
    /// IDE port now (the read value in `value`). SMUC is not decoded here: the
    /// Scorpion decoder owns its port family and calls SmucIn / SmucOut
    bool In(uint16_t port, const Gate& gate, uint8_t& value);
    bool Out(uint16_t port, const Gate& gate, uint8_t value);

    /// SMUC IDE window (#xxBE with (port & #8044) = #8004). `system` is the
    /// #FFBA latch (bit 7: the control block)
    uint8_t SmucIn(uint16_t port, uint8_t system);
    void SmucOut(uint16_t port, uint8_t system, uint8_t value);

    /// ATM Turbo 2+: bit 6 of the #7FFD-class read. 1 when the selected unit
    /// raises INTRQ or no unit answers (UnrealSpeccy `read_intrq`)
    uint8_t AtmIntrqBit();

    /// The scheme's IDE units reset (SMUC #FFBA bit 0; the machine's reset goes through IdeController)
    void ResetUnits();
    /// Latches cleared (machine reset)
    void Reset() { _s = IdeAdapterState{}; }

    IDE_SCHEME Scheme() const;
    bool Active() const { return Channel() != nullptr; }

    const IdeAdapterState& State() const { return _s; }
    void SetState(const IdeAdapterState& state) { _s = state; }

private:
    AtaChannel* Channel() const;

    uint8_t ReadRegister(uint8_t reg);
    void WriteRegister(uint8_t reg, uint8_t value);
    /// 16-bit read: returns the low byte, the high byte goes to the read latch
    uint8_t ReadDataLow();

    bool NemoIn(uint16_t port, bool a8Latch, uint8_t& value);
    bool NemoOut(uint16_t port, bool a8Latch, uint8_t value);
    bool EvoIn(uint16_t port, uint8_t& value);
    bool EvoOut(uint16_t port, uint8_t value);
    bool AtmIn(uint16_t port, uint8_t& value);
    bool AtmOut(uint16_t port, uint8_t value);
    bool ProfiIn(uint16_t port, uint8_t& value);
    bool ProfiOut(uint16_t port, uint8_t value);
    bool DivideIn(uint16_t port, uint8_t& value);
    bool DivideOut(uint16_t port, uint8_t value);

    EmulatorContext* _context = nullptr;
    IdeAdapterState _s;
};
