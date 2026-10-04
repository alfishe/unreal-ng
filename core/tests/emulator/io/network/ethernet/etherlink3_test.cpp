// The 3Com EtherLink III 3C509B (etherlink3.h; network tdd §9, phase SN5) against its technical reference
// (09-0398-002B, "TR") and the real boards the Sprinter 3C509B kit read: the EEPROM image, the ID port's isolation
// protocol (ID sequence, contention reads of EEPROM words, tags, activation, the ID global reset), window 0's
// configuration and EEPROM access, the 8-bit slot's low / high byte rule, TX (preamble, padding, wire time, TX status
// stack, TX free, early start and underrun, overrun), RX (filter, RX status counting down, discard, underrun, CRC
// pass-through), interrupts (latch, masks, the IRQ driver rules, the timer), 10BASE-T link integrity, loopback,
// statistics, power down and the TTD state round trip. Every test drives the card the way a driver does: byte cycles.

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "emulator/io/network/ethernet/dp8390.h"
#include "emulator/io/network/ethernet/etherlink3.h"

namespace
{
constexpr uint64_t kUs = 4;   // a little more than a microsecond in base T-states (3.5 per us)
constexpr uint64_t kMs = 3500;

class CaptureLink : public IEthernetLink
{
public:
    void Transmit(IEthernetPort& from, const uint8_t* frame, size_t length) override
    {
        (void)from;
        frames.emplace_back(frame, frame + length);
    }
    std::vector<std::vector<uint8_t>> frames;
};

const uint8_t kMac[6] = {0x02, 0x53, 0x50, 0x00, 0x00, 0x02};

std::vector<uint8_t> Frame(const uint8_t dst[6], size_t length, uint8_t fill)
{
    std::vector<uint8_t> f(length, fill);
    for (int i = 0; i < 6; ++i)
    {
        f[i] = dst[i];
        f[6 + i] = static_cast<uint8_t>(0x10 + i);
    }
    f[12] = 0x08;
    f[13] = 0x00;
    return f;
}
}  // namespace

class EtherLink3_Test : public ::testing::Test
{
protected:
    void SetUp() override { Make(EtherLink3::Variant::Tpo); }

    void Make(EtherLink3::Variant variant, uint16_t base = 0x300, uint8_t irq = 3,
              std::array<uint8_t, 6> mac = {0x02, 0x53, 0x50, 0x00, 0x00, 0x02})
    {
        EtherLink3::Settings s;
        s.variant = variant;
        s.base = base;
        s.irq = irq;
        s.mac = mac;
        s.key = "isa2.eth";
        _now = 0;
        _card = std::make_unique<EtherLink3>(s, [this]() { return _now; });
        _now += 400 * kUs;   // the EEPROM is read after power-on (310 us)
    }

    // --- bus cycles ---------------------------------------------------------------------------------------------
    static uint16_t IdOffset(uint16_t port)
    {
        return static_cast<uint16_t>(EtherLink3::kIdPortOffset | ((port >> 4) & 0x0F));
    }
    void IdOut(uint8_t value, uint16_t port = 0x110) { _card->Write(IdOffset(port), value); }
    uint8_t IdIn(uint16_t port = 0x110) { return _card->Read(IdOffset(port)); }
    void Out(uint8_t reg, uint8_t value) { _card->Write(reg, value); }
    uint8_t In(uint8_t reg) { return _card->Read(reg); }
    void Out16(uint8_t reg, uint16_t value)
    {
        Out(reg, static_cast<uint8_t>(value));
        Out(static_cast<uint8_t>(reg + 1), static_cast<uint8_t>(value >> 8));
    }
    uint16_t In16(uint8_t reg)
    {
        const uint8_t lo = In(reg);
        return static_cast<uint16_t>(lo | (In(static_cast<uint8_t>(reg + 1)) << 8));
    }
    void Cmd(uint16_t word) { Out16(0x0E, word); }
    void Window(int n) { Cmd(static_cast<uint16_t>(0x0800 | n)); }

    /// Two zeros, then the 255-byte LFSR sequence (TR 7-2; the kit's ID_SEQUENCE)
    void IdSequence(uint16_t port = 0x110)
    {
        IdOut(0x00, port);
        IdOut(0x00, port);
        for (int i = 0; i < 255; ++i)
            IdOut(EtherLink3::IdSequenceByte(i), port);
    }
    /// An EEPROM word through the ID port: the read command, 162 us, 16 contention reads MSB first (ID_READ_WORD)
    uint16_t IdReadWord(uint8_t word, uint16_t port = 0x110)
    {
        IdOut(static_cast<uint8_t>(0x80 | word), port);
        _now += 200 * kUs;
        uint16_t value = 0;
        for (int i = 0; i < 16; ++i)
            value = static_cast<uint16_t>((value << 1) | (IdIn(port) & 1));
        return value;
    }
    /// Isolation and activation at the EEPROM's base, then the kit's INIT (el3_regs.asm): masks, RX / TX off and
    /// reset, the station address, link beat + jabber, statistics cleared, RX filter individual + broadcast,
    /// thresholds, statistics on, RX / TX on, window 1
    void BringUp(bool cable = true)
    {
        if (cable)
            _card->SetLink(&_link);
        IdSequence();
        IdOut(0xD0);
        IdOut(0xFF);
        Cmd(0x7000);            // interrupt mask 0
        Cmd(0x7800 | 0x96);     // read zero mask: AF, TC, RC, US
        Cmd(0x1800);
        Cmd(0x5000);
        Cmd(0x2800);
        Cmd(0x5800);
        Window(2);
        for (int i = 0; i < 6; ++i)
            Out(static_cast<uint8_t>(i), kMac[i]);
        Window(4);
        Out16(0x0A, static_cast<uint16_t>(In16(0x0A) | 0x00C0));
        Cmd(0xB000);
        Window(6);
        for (uint8_t i = 0; i < 14; ++i)
            In(i);
        Cmd(0x8000 | 0x05);
        Cmd(0x8800 | 0x7FF);
        Cmd(0x9000 | 0x7FF);
        Cmd(0x9800 | 1536);
        Cmd(0xA800);
        Cmd(0x2000);
        Cmd(0x4800);
        Cmd(0x6800 | 0x69);
        Window(1);
        _now += 60 * kMs;       // the 10BASE-T link test passes (48 ms)
    }
    /// The kit's TX_WRITE_PACKET: preamble (length, interrupt on success), a zero word, the data, the dword pad
    void Send(const std::vector<uint8_t>& frame, bool interrupt = true)
    {
        const uint16_t length = static_cast<uint16_t>(frame.size());
        Out(0x00, static_cast<uint8_t>(length));
        Out(0x00, static_cast<uint8_t>((length >> 8) | (interrupt ? 0x80 : 0)));
        Out(0x00, 0);
        Out(0x00, 0);
        for (uint8_t b : frame)
            Out(0x00, b);
        for (size_t i = frame.size(); i % 4; ++i)
            Out(0x00, 0);
    }
    /// One received frame out of the RX FIFO (READ_FRAME): RX status, the bytes, the pad, RX Discard
    std::vector<uint8_t> Receive()
    {
        const uint16_t status = In16(0x08);
        if (status & 0x8000)
            return {};
        const size_t length = status & 0x7FF;
        std::vector<uint8_t> out;
        for (size_t i = 0; i < length; ++i)
            out.push_back(In(0x00));
        for (size_t i = length; i % 4; ++i)
            In(0x00);
        Cmd(0x4000);
        return out;
    }

    std::unique_ptr<EtherLink3> _card;
    uint64_t _now = 0;
    CaptureLink _link;
};

// The image the card is built with is the real 3C509B-TPO's (assembly 03-0020-002, the kit's EL3EEP dump) when its
// MAC, base and IRQ are the real board's: every word, both checksums and the PnP serial checksum
TEST_F(EtherLink3_Test, Eeprom_ReproducesTheRealBoards)
{
    const uint16_t tpo[56] = {
        0x0020, 0xAF5D, 0x698B, 0x9550, 0xB434, 0x0041, 0x4A41, 0x6D50, 0x0010, 0x3000, 0x0020, 0xAF5D, 0x698B, 0x1310,
        0x0000, 0x3223, 0x2083, 0x0000, 0x0000, 0x0004, 0x0001, 0x0000, 0x0000, 0x0205, 0x6D50, 0x9550, 0x698B, 0xAF5D,
        0x0A5B, 0x1010, 0x1982, 0x3300, 0x6F43, 0x206D, 0x4333, 0x3035, 0x4239, 0x4520, 0x6874, 0x7265, 0x694C, 0x6B6E,
        0x4920, 0x4949, 0x5015, 0x506D, 0x0295, 0x411C, 0x80D0, 0x22F7, 0x9EA8, 0x0147, 0x0210, 0x03E0, 0x1010, 0x3779};
    EtherLink3::Settings s;
    s.mac = {0x00, 0x20, 0xAF, 0x5D, 0x69, 0x8B};
    const auto words = EtherLink3::BuildEeprom(s);
    for (int i = 0; i < 56; ++i)
        EXPECT_EQ(words[i], tpo[i]) << "word " << i;

    // The 3C509B-TP (assembly 03-0021-201): product #9050, its own date code is not ours, the rest by rule
    s.variant = EtherLink3::Variant::Tp;
    s.mac = {0x00, 0x20, 0xAF, 0x4B, 0xAB, 0x97};
    const auto tp = EtherLink3::BuildEeprom(s);
    EXPECT_EQ(tp[0x03], 0x9050);
    EXPECT_EQ(tp[0x0F], 0x3923) << "the TP board's primary checksum";
    EXPECT_EQ(tp[0x17], 0x4505) << "and its secondary";
    EXPECT_EQ(tp[0x1C], 0x0ADF) << "the PnP serial identifier checksum";

    // Our own settings: base #320, IRQ 5, the automatic MAC - checksums still valid (the kit's VALIDATE)
    s.variant = EtherLink3::Variant::Tpo;
    s.base = 0x320;
    s.irq = 5;
    s.mac = {0x02, 0x53, 0x50, 0x00, 0x00, 0x02};
    const auto own = EtherLink3::BuildEeprom(s);
    EXPECT_EQ(own[0x08] & 0x1F, 0x12) << "(#320 - #200) / 16";
    EXPECT_EQ(own[0x09] >> 12, 5);
    EXPECT_EQ(own[0x00], 0x0253);
    EXPECT_EQ(own[0x0F], EtherLink3::PrimaryChecksum(own));
    EXPECT_EQ(own[0x17], EtherLink3::SecondaryChecksum(own));
}

TEST_F(EtherLink3_Test, IdSequence_255BytesFromFFTo98)
{
    EXPECT_EQ(EtherLink3::IdSequenceByte(0), 0xFF);
    EXPECT_EQ(EtherLink3::IdSequenceByte(1), 0x31) << "#FF shifted out a carry: #FE ^ #CF";
    EXPECT_EQ(EtherLink3::IdSequenceByte(7), 0x69) << "the test-mode sequence ends with the eighth byte, #69 (TR 7-24)";
    EXPECT_EQ(EtherLink3::IdSequenceByte(254), 0x98) << "TR 7-3: ends with #98";
    EXPECT_EQ(EtherLink3::IdSequenceByte(255), 0xFF) << "the LFSR's period: the kit checks it returns to #FF";
}

// TR 7-2..7-4 with one card per slot: deaf while the EEPROM is read, the ID port picked by a zero, the sequence, the
// tag, contention reads of the EEPROM (bit 15 on D0, the other bits float high), activation at the EEPROM's base
TEST_F(EtherLink3_Test, Isolation_ContentionReadsAndActivation)
{
    Make(EtherLink3::Variant::Tpo);
    _now = 100 * kUs;
    uint16_t offset = 0;
    EXPECT_FALSE(_card->Decodes(0x110, offset)) << "AUTOINIT: the EEPROM is still being read (310 us)";
    _now = 400 * kUs;
    ASSERT_TRUE(_card->Decodes(0x110, offset));
    EXPECT_EQ(offset, IdOffset(0x110));
    EXPECT_TRUE(_card->Decodes(0x1F0, offset)) << "any #1x0 is watched";
    EXPECT_FALSE(_card->Decodes(0x118, offset));
    EXPECT_FALSE(_card->Decodes(0x300, offset)) << "the registers answer only after activation";
    EXPECT_FALSE(_card->Decodes(0x4110, offset)) << "A15-A0 decoded: no mirrors";

    EXPECT_EQ(IdIn(), 0xFF) << "ID_WAIT: the card does not drive the bus";
    IdSequence();
    EXPECT_EQ(_card->GetIdState(), EtherLink3::IdState::IdCmd);
    IdOut(0xD0);
    EXPECT_EQ(IdReadWord(0x07), 0x6D50) << "the manufacturer ID";
    EXPECT_EQ(IdReadWord(0x03), 0x9550) << "the 3C509B-TPO";
    EXPECT_EQ(IdReadWord(0x00), 0x0253) << "Address(0), Address(1)";
    IdOut(0x88);
    _now += 200 * kUs;
    EXPECT_EQ(IdIn() & 0xFE, 0xFE) << "only D0 is driven";

    // A read before the EEPROM finished (162 us): the old data register shifts out
    IdOut(0x83);
    _now += 50 * kUs;
    uint16_t early = 0;
    for (int i = 0; i < 16; ++i)
        early = static_cast<uint16_t>((early << 1) | (IdIn() & 1));
    EXPECT_NE(early, 0x9550);

    IdOut(0xFF);
    EXPECT_TRUE(_card->Activated());
    EXPECT_EQ(_card->IoBase(), 0x300);
    EXPECT_EQ(_card->GetIdState(), EtherLink3::IdState::IdWait) << "activation returns to ID_WAIT";
    ASSERT_TRUE(_card->Decodes(0x30E, offset));
    EXPECT_EQ(offset, 0x0E);
    EXPECT_EQ(_card->Window(), 0);
    EXPECT_EQ(In16(0x00), 0x6D50) << "window 0: manufacturer ID";
    EXPECT_EQ(In16(0x02), 0x9550);
    EXPECT_EQ(In16(0x04), 0x4F00) << "PORreg: ISA, normal mode, 10BASE-T, internal VCO; ENA clear";
    EXPECT_EQ(In16(0x06), 0x0010) << "address configuration from EEPROM word 8";
    EXPECT_EQ(In16(0x08), 0x3000) << "resource configuration: IRQ 3";
    uint32_t first = 0, last = 0;
    ASSERT_TRUE(_card->IoRange(first, last));
    EXPECT_EQ(first, 0x300u);
    EXPECT_EQ(last, 0x30Fu);
}

TEST_F(EtherLink3_Test, Isolation_ExplicitBaseTagsAndIdGlobalReset)
{
    IdSequence(0x150);
    IdOut(0xD0, 0x150);
    IdOut(0xE1, 0x150);
    EXPECT_EQ(_card->IoBase(), 0x210) << "E0-FE: #200 + 16 x (cmd & #1F)";
    uint16_t offset = 0;
    EXPECT_TRUE(_card->Decodes(0x21E, offset));

    // A wrong byte resets the sequence; a tagged card does not answer reads; D8 + another tag sends it to ID_WAIT
    IdOut(0x00, 0x150);
    IdOut(0xFF, 0x150);
    IdOut(0x12, 0x150);
    IdOut(0xFE, 0x150);
    EXPECT_EQ(_card->GetIdState(), EtherLink3::IdState::IdWait);
    IdSequence(0x150);
    IdOut(0xD3, 0x150);
    EXPECT_EQ(IdIn(0x150), 0xFF) << "tag 3: reads are not answered";
    IdOut(0xD5, 0x150);
    IdOut(0xDB, 0x150);
    EXPECT_EQ(_card->GetIdState(), EtherLink3::IdState::IdCmd) << "D8+3 = our tag: stays (D5 ignored: tagged)";
    IdOut(0xD9, 0x150);
    EXPECT_EQ(_card->GetIdState(), EtherLink3::IdState::IdWait) << "D8+1: another tag";

    // C0: the ID global reset - deaf for 310 us, then ID_WAIT with no ID port and not active
    IdSequence(0x150);
    IdOut(0xC0, 0x150);
    EXPECT_FALSE(_card->Activated());
    EXPECT_FALSE(_card->Decodes(0x150, offset));
    _now += 320 * kUs;
    EXPECT_TRUE(_card->Decodes(0x150, offset));
    IdOut(0x55, 0x150);   // a non-zero byte before any zero picks no port
    EXPECT_EQ(_card->GetIdState(), EtherLink3::IdState::IdWait);
}

TEST_F(EtherLink3_Test, Window0_EepromAccessAndEnable)
{
    BringUp();
    Window(0);
    Out16(0x0A, 0x80 | 0x07);
    EXPECT_EQ(In16(0x0A) & 0x8000, 0x8000) << "EEPROM busy for 162 us";
    _now += 170 * kUs;
    EXPECT_EQ(In16(0x0A) & 0x8000, 0);
    EXPECT_EQ(In16(0x0C), 0x6D50);

    // Erase / write need EWEN, a write programs zeros only, the hardware disables writes after each
    Out16(0x0C, 0x1234);
    Out16(0x0A, 0x40 | 0x30);   // write word #30 without EWEN
    _now += 12 * kMs;
    EXPECT_EQ(In16(0x0A) & 0x8000, 0) << "the driver polls EEPROM busy (the card catches up on that cycle)";
    EXPECT_EQ(_card->Eeprom()[0x30], 0x80D0) << "no EWEN: unchanged";
    Out16(0x0A, 0x30);          // EWEN
    _now += 100 * kUs;
    Out16(0x0A, 0xC0 | 0x30);   // erase: all ones
    _now += 12 * kMs;
    In16(0x0A);
    EXPECT_EQ(_card->Eeprom()[0x30], 0xFFFF);
    Out16(0x0A, 0x30);
    _now += 100 * kUs;
    Out16(0x0C, 0x1234);
    Out16(0x0A, 0x40 | 0x30);
    _now += 12 * kMs;
    In16(0x0A);
    EXPECT_EQ(_card->Eeprom()[0x30], 0x1234);

    Out16(0x04, 0x0001);
    EXPECT_EQ(In16(0x04) & 1, 1) << "ENA";
}

// The 8-bit slot: a 16-bit register is two byte cycles, low first; a command runs on its high byte (TR 6-1, 6-2)
TEST_F(EtherLink3_Test, ByteCycles_LowThenHigh)
{
    BringUp();
    Out(0x0E, 0x03);
    EXPECT_EQ(_card->Window(), 1) << "nothing runs on the low byte";
    Out(0x0F, 0x08);
    EXPECT_EQ(_card->Window(), 3);
    const uint16_t status = In16(0x0E);
    EXPECT_EQ(status >> 13, 3) << "the window in status bits 15-13";
    EXPECT_EQ(In(0x0F) >> 5, 3) << "a lone high-byte read is allowed for the status";
}

TEST_F(EtherLink3_Test, Init_ReadbacksTheKitVerifies)
{
    BringUp();
    Window(5);
    EXPECT_EQ(In16(0x08), 0x0005);
    EXPECT_EQ(In16(0x0A), 0x0000);
    EXPECT_EQ(In16(0x0C), 0x0096);
    EXPECT_EQ(In16(0x06), 0x07FC) << "thresholds read back truncated to a dword";
    EXPECT_EQ(In16(0x02), 0x07FC);
    EXPECT_EQ(In16(0x00), 1536) << "3C509B: TX start reads what was written (no + 4)";
    Window(2);
    for (int i = 0; i < 6; ++i)
        EXPECT_EQ(In(static_cast<uint8_t>(i)), kMac[i]);
    Window(4);
    const uint16_t media = In16(0x0A);
    EXPECT_EQ(media & 0xF7FF, 0xA0C0) << "TP enabled, bit 13, link beat + jabber enabled";
    EXPECT_EQ(media & 0x0800, 0x0800) << "link beat detected";
    EXPECT_EQ(In16(0x06) & 0x0C80, 0x0C80) << "net diagnostic: TX, RX and statistics enabled";
    EXPECT_EQ(In16(0x06) & 0x003E, 0x0004) << "ASIC revision 2";
    Window(3);
    EXPECT_EQ(In16(0x0C), 3072 - 4) << "TX FIFO: 3 KB of the 8 KB (3:5), 4 bytes never free";
    EXPECT_EQ(In16(0x0A), 5120 - 4);
    Window(1);
    EXPECT_EQ(In16(0x0E) >> 13, 1);
}

TEST_F(EtherLink3_Test, Tx_PaddedFrameWireTimeStatusAndFree)
{
    BringUp();
    const uint8_t router[6] = {0x52, 0x55, 0x0A, 0x00, 0x02, 0x02};
    const std::vector<uint8_t> arp = Frame(router, 42, 0xAA);
    Send(arp);
    EXPECT_TRUE(_link.frames.empty()) << "on the wire for (60 + 4 + 20) byte times";
    EXPECT_EQ(In16(0x0C), static_cast<uint16_t>((3068 - 44 - 4) & ~3)) << "TX free while it leaves";
    EXPECT_EQ(In(0x0B), 0) << "no completion yet";
    _now += Dp8390::WireTime(64);
    EXPECT_EQ(In(0x0B), 0xC0) << "complete, interrupt on success requested";
    ASSERT_EQ(_link.frames.size(), 1u);
    EXPECT_EQ(_link.frames[0].size(), 60u) << "padded to the minimum";
    EXPECT_TRUE(std::equal(arp.begin(), arp.end(), _link.frames[0].begin()));
    EXPECT_EQ(_link.frames[0][59], 0);
    EXPECT_EQ(In16(0x0E) & 0x04, 0x04) << "TX Complete in the status";
    Out(0x0B, 0);
    EXPECT_EQ(In(0x0B), 0);
    EXPECT_EQ(In16(0x0E) & 0x04, 0);
    EXPECT_EQ(In16(0x0C), 3068 & ~3) << "the space is back";

    // Without the interrupt bit a good frame leaves no status
    Send(Frame(router, 100, 0x11), false);
    _now += Dp8390::WireTime(104);
    EXPECT_EQ(In(0x0B), 0);
    EXPECT_EQ(_link.frames.size(), 2u);

    // Statistics: two frames, 42 + 100 bytes
    Cmd(0xB000);
    Window(6);
    EXPECT_EQ(In(0x06), 2);
    EXPECT_EQ(In16(0x0C), 142);
    EXPECT_EQ(In(0x06), 0) << "reading a statistic zeroes it";
}

TEST_F(EtherLink3_Test, Tx_WaitsForTxEnableAndQueuesPackets)
{
    BringUp();
    const uint8_t router[6] = {0x52, 0x55, 0x0A, 0x00, 0x02, 0x02};
    Cmd(0x5000);   // TX disable
    Send(Frame(router, 60, 1));
    Send(Frame(router, 61, 2));
    _now += 10 * kMs;
    EXPECT_TRUE(_link.frames.empty());
    Window(3);
    EXPECT_EQ(In16(0x0C), 3068 - (60 + 4) - (64 + 4));
    Window(1);
    Cmd(0x4800);
    _now += Dp8390::WireTime(64) + Dp8390::WireTime(65) + 10;
    EXPECT_EQ(In(0x0B), 0xC0);
    EXPECT_EQ(_link.frames.size(), 2u) << "back to back";
    Out(0x0B, 0);
    EXPECT_EQ(In(0x0B), 0xC0) << "the second completion";
}

// TR 4-4, 6-18: an early start (TX start threshold below the packet) with a host slower than the wire underruns
TEST_F(EtherLink3_Test, Tx_EarlyStartUnderrunNeedsTxReset)
{
    BringUp();
    Cmd(0x9800 | 0);   // start as soon as any byte is in
    const uint8_t router[6] = {0x52, 0x55, 0x0A, 0x00, 0x02, 0x02};
    const std::vector<uint8_t> f = Frame(router, 200, 0x33);
    Out(0x00, 200);
    Out(0x00, 0x80);
    Out(0x00, 0);
    Out(0x00, 0);
    for (size_t i = 0; i < 20; ++i)
        Out(0x00, f[i]);
    _now += 1 * kMs;   // the wire empties the FIFO long before the host writes more
    EXPECT_EQ(In(0x0B), 0x90 | 0x40) << "complete + underrun (interrupt on success mirrored)";
    Window(4);
    EXPECT_EQ(In16(0x06) & 0x0900, 0x0100) << "TX disabled, TX reset needed";
    Window(1);
    for (size_t i = 20; i < 200; ++i)
        Out(0x00, f[i]);   // the rest of the packet goes nowhere
    EXPECT_TRUE(_link.frames.empty()) << "the far end saw a bad CRC";
    Cmd(0x5800);
    Cmd(0x4800);
    Window(4);
    EXPECT_EQ(In16(0x06) & 0x0900, 0x0800);
}

TEST_F(EtherLink3_Test, Tx_OverrunIsAnAdapterFailure)
{
    BringUp();
    Cmd(0x5000);
    const uint8_t router[6] = {0x52, 0x55, 0x0A, 0x00, 0x02, 0x02};
    for (int n = 0; n < 3; ++n)
        Send(Frame(router, 1514, 0x44));   // 3 KB of TX FIFO: the third one does not fit
    EXPECT_EQ(In16(0x0E) & 0x02, 0x02) << "Adapter Failure: more data than room";
    Window(4);
    EXPECT_EQ(In16(0x04) & 0x0400, 0x0400) << "FIFO diagnostic: TX overrun";
    Window(1);
    Cmd(0x5800);
    EXPECT_EQ(In16(0x0E) & 0x02, 0);
}

TEST_F(EtherLink3_Test, Rx_FilterStatusCountsDownAndDiscard)
{
    BringUp();
    const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t other[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x09};
    const uint8_t multicast[6] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01};
    EXPECT_EQ(In16(0x08), 0x8000) << "empty: incomplete, 0 bytes";
    EXPECT_TRUE(_card->Offer(Frame(other, 60, 1).data(), 60));
    EXPECT_TRUE(_card->Offer(Frame(multicast, 60, 1).data(), 60));
    EXPECT_EQ(In16(0x08), 0x8000) << "filter 5: individual + broadcast only";
    const std::vector<uint8_t> bc = Frame(broadcast, 61, 0x5A);
    ASSERT_TRUE(_card->Offer(bc.data(), bc.size()));
    const std::vector<uint8_t> mine = Frame(kMac, 70, 0x6B);
    ASSERT_TRUE(_card->Offer(mine.data(), mine.size()));
    EXPECT_EQ(In16(0x0E) & 0x10, 0x10) << "RX Complete";
    EXPECT_EQ(In16(0x08), 61);
    In(0x00);
    In(0x00);
    EXPECT_EQ(In16(0x08), 59) << "RX bytes count down as the packet is read";
    Cmd(0x4000);
    EXPECT_EQ(Receive(), mine);
    EXPECT_EQ(In16(0x08), 0x8000);
    EXPECT_EQ(In16(0x0E) & 0x10, 0);
    Window(3);
    EXPECT_EQ(In16(0x0A), 5116) << "RX free back";
    Window(1);

    // Group addresses, then promiscuous
    Cmd(0x8000 | 0x07);
    ASSERT_TRUE(_card->Offer(Frame(multicast, 60, 1).data(), 60));
    EXPECT_EQ(In16(0x08), 60);
    Cmd(0x4000);
    Cmd(0x8000 | 0x08);
    ASSERT_TRUE(_card->Offer(Frame(other, 64, 1).data(), 64));
    EXPECT_EQ(In16(0x08), 64);
}

TEST_F(EtherLink3_Test, Rx_PastThePadUnderrunsAndAFullFifoKeepsTheFrame)
{
    BringUp();
    const std::vector<uint8_t> mine = Frame(kMac, 61, 0x6B);
    ASSERT_TRUE(_card->Offer(mine.data(), mine.size()));
    for (int i = 0; i < 64; ++i)
        In(0x00);
    EXPECT_EQ(In16(0x08) & 0x7FF, 0x7FD) << "-3: three pad bytes read";
    EXPECT_EQ(In16(0x0E) & 0x02, 0) << "still within the pad";
    In(0x00);
    EXPECT_EQ(In16(0x0E) & 0x02, 0x02) << "RX underrun: Adapter Failure";
    Cmd(0x2800);
    Cmd(0x8000 | 0x05);
    Cmd(0x2000);
    EXPECT_EQ(In16(0x0E) & 0x02, 0);

    // 5 KB of RX FIFO: three full frames fit (3 x 1520), the fourth waits in the switch
    const std::vector<uint8_t> big = Frame(kMac, 1514, 0x77);
    int taken = 0;
    while (_card->Offer(big.data(), big.size()) && taken < 10)
        ++taken;
    EXPECT_EQ(taken, 3);
    EXPECT_EQ(_card->GetCounters().rxFifoFullWaits, 1u);
}

// CRC pass-through (TR 4-3, 6-27): CRC strip disabled brings the 4 CRC bytes with the data; a transmit with the
// Disable CRC Generation bit sends the host's CRC - a wrong one is dropped by the far end
TEST_F(EtherLink3_Test, Crc_PassThroughBothWays)
{
    BringUp();
    Window(4);
    Out16(0x0A, static_cast<uint16_t>(In16(0x0A) | 0x0004));
    Window(1);
    const std::vector<uint8_t> mine = Frame(kMac, 60, 0x21);
    ASSERT_TRUE(_card->Offer(mine.data(), mine.size()));
    std::vector<uint8_t> got = Receive();
    ASSERT_EQ(got.size(), 64u);
    const uint32_t crc = Dp8390::Crc32(mine.data(), mine.size());
    EXPECT_EQ(got[60], static_cast<uint8_t>(crc));
    EXPECT_EQ(got[63], static_cast<uint8_t>(crc >> 24));

    // TX with DCG: the data's last 4 bytes are its CRC
    const uint8_t router[6] = {0x52, 0x55, 0x0A, 0x00, 0x02, 0x02};
    std::vector<uint8_t> f = Frame(router, 60, 0x31);
    const uint32_t fcs = Dp8390::Crc32(f.data(), f.size());
    for (int i = 0; i < 4; ++i)
        f.push_back(static_cast<uint8_t>(fcs >> (8 * i)));
    auto sendDcg = [&](const std::vector<uint8_t>& data) {
        Out(0x00, static_cast<uint8_t>(data.size()));
        Out(0x00, 0x80 | 0x20);
        Out(0x00, 0);
        Out(0x00, 0);
        for (uint8_t b : data)
            Out(0x00, b);
    };
    sendDcg(f);
    _now += Dp8390::WireTime(64) + 1;
    In16(0x0E);
    ASSERT_EQ(_link.frames.size(), 1u);
    EXPECT_EQ(_link.frames[0].size(), 60u) << "the gateway gets the frame without its CRC";
    f[20] ^= 1;
    sendDcg(f);
    _now += Dp8390::WireTime(64) + 1;
    In16(0x0E);
    EXPECT_EQ(_link.frames.size(), 1u) << "a bad CRC: the far end drops it";
}

// TR 6-13, 6-22, 7-13, 7-20: the latch follows the unmasked causes; the line is driven with ENA, an IRQ the 8-bit
// slot has, and a window other than 0; acknowledging the latch with a cause still pending latches it again
TEST_F(EtherLink3_Test, Interrupt_LatchMasksDriversAndTimer)
{
    BringUp();
    EXPECT_FALSE(_card->Irq());
    EXPECT_EQ(_card->IrqLine(), 3);
    EXPECT_FALSE(_card->IrqDriven()) << "ENA clear";
    Window(0);
    Out16(0x04, 0x0001);
    EXPECT_FALSE(_card->IrqDriven()) << "window 0 disables the drivers";
    Window(1);
    EXPECT_TRUE(_card->IrqDriven());
    int notices = 0;
    _card->SetIrqListener([&] { ++notices; });

    Cmd(0x7000 | 0x10);   // RX Complete
    ASSERT_TRUE(_card->Offer(Frame(kMac, 60, 1).data(), 60));
    EXPECT_TRUE(_card->Irq());
    EXPECT_GE(notices, 1);
    EXPECT_EQ(In16(0x0E) & 0x11, 0x11) << "RX Complete + the latch";
    _now += 350;   // 100 us
    EXPECT_EQ(In(0x0A), 31) << "the timer: 3.2 us per count since the interrupt went active";
    Cmd(0x6801);
    EXPECT_TRUE(_card->Irq()) << "the cause is still there: latched again";
    Receive();
    Cmd(0x6801);
    EXPECT_FALSE(_card->Irq());

    // Request Interrupt, masked off by the read zero mask unless enabled there
    Cmd(0x7000 | 0x40);
    Cmd(0x6000);
    EXPECT_FALSE(_card->Irq()) << "the read zero mask hides Interrupt Requested (#96)";
    Cmd(0x7800 | 0xD6);
    EXPECT_TRUE(_card->Irq());
    Cmd(0x6800 | 0x41);
    EXPECT_FALSE(_card->Irq());

    // IRQ 10 is on the 16-bit connector: never driven in this slot; IRQ 4 is no 3C509B setting
    Window(0);
    Out16(0x08, 0xA000);
    Window(1);
    EXPECT_FALSE(_card->IrqDriven());
    Window(0);
    Out16(0x08, 0x4000);
    EXPECT_EQ(_card->IrqLine(), -1);
}

TEST_F(EtherLink3_Test, Link_IntegrityTestAndNoCable)
{
    Make(EtherLink3::Variant::Tpo);
    _card->SetLink(&_link);
    IdSequence();
    IdOut(0xD0);
    IdOut(0xFF);
    Window(4);
    EXPECT_EQ(In16(0x0A) & 0x0800, 0) << "link beat not enabled: no detection";
    Out16(0x0A, 0x00C0);
    _now += 30 * kMs;
    EXPECT_EQ(In16(0x0A) & 0x0800, 0) << "three link pulses take 48 ms";
    _now += 20 * kMs;
    EXPECT_EQ(In16(0x0A) & 0x0800, 0x0800);

    // No cable: nothing comes in or goes out; the frame counts as carrier lost
    Make(EtherLink3::Variant::Tpo);
    BringUp(false);
    Window(4);
    EXPECT_EQ(In16(0x0A) & 0x0800, 0);
    Window(1);
    const uint8_t router[6] = {0x52, 0x55, 0x0A, 0x00, 0x02, 0x02};
    Send(Frame(router, 60, 1));
    _now += 1 * kMs;
    EXPECT_EQ(In(0x0B), 0xC0);
    EXPECT_EQ(_card->GetCounters().txNoLink, 1u);
    EXPECT_TRUE(_card->Offer(Frame(kMac, 60, 1).data(), 60));
    EXPECT_EQ(In16(0x08), 0x8000);
}

TEST_F(EtherLink3_Test, Loopback_ControllerReturnsTheFrame)
{
    BringUp();
    Window(4);
    Out16(0x06, 0x2000);   // Ethernet controller loopback (EL3LB)
    Window(1);
    const std::vector<uint8_t> f = Frame(kMac, 64, 0x5C);
    Send(f);
    _now += Dp8390::WireTime(68) + 1;
    EXPECT_EQ(Receive(), f);
    EXPECT_TRUE(_link.frames.empty()) << "internal loopback keeps the frame off the wire";
}

TEST_F(EtherLink3_Test, Statistics_EnableLatchAndWrites)
{
    BringUp();
    Cmd(0xB000);   // statistics off: one update request per counter is latched
    ASSERT_TRUE(_card->Offer(Frame(kMac, 60, 1).data(), 60));
    ASSERT_TRUE(_card->Offer(Frame(kMac, 60, 1).data(), 60));
    Window(6);
    EXPECT_EQ(In(0x07), 0);
    Out(0x07, 0x7F);   // a write adds while disabled
    Cmd(0xA800);
    Cmd(0xB000);
    EXPECT_EQ(In(0x07), 0x80) << "#7F + the latched request";
    Out(0x04, 0x80);
    EXPECT_EQ(In16(0x0E) & 0x80, 0x80) << "Update Statistics: a counter reached half its range";
    In(0x04);
    EXPECT_EQ(In16(0x0E) & 0x80, 0);
}

TEST_F(EtherLink3_Test, PowerDown_OnlyPowerUpIsTaken)
{
    BringUp();
    Cmd(0xE000);
    Window(3);
    EXPECT_EQ(_card->Window(), 1) << "Power Down Full: only Power Up is legal";
    EXPECT_TRUE(_card->Offer(Frame(kMac, 60, 1).data(), 60));
    Cmd(0xD800);
    Window(1);
    EXPECT_EQ(In16(0x08), 0x8000) << "nothing was received while powered down";
}

TEST_F(EtherLink3_Test, GlobalReset_DeactivatesTheCard)
{
    BringUp();
    Cmd(0x0000);
    uint16_t offset = 0;
    EXPECT_FALSE(_card->Activated());
    _now += 400 * kUs;
    EXPECT_FALSE(_card->Decodes(0x30E, offset)) << "after a Global Reset only the ID port finds the card";
    EXPECT_TRUE(_card->Decodes(0x110, offset));

    // A masked Global Reset (AISM and host kept): the card stays active, the FIFOs empty
    BringUp();
    ASSERT_TRUE(_card->Offer(Frame(kMac, 60, 1).data(), 60));
    Cmd(0x0000 | 0x33);
    EXPECT_TRUE(_card->Activated());
    EXPECT_EQ(In16(0x08), 0x8000);
}

TEST_F(EtherLink3_Test, Ttd_StateRoundTripMidSession)
{
    BringUp();
    const uint8_t router[6] = {0x52, 0x55, 0x0A, 0x00, 0x02, 0x02};
    Cmd(0x5000);
    Send(Frame(router, 90, 0x12));
    ASSERT_TRUE(_card->Offer(Frame(kMac, 70, 0x34).data(), 70));
    In(0x00);
    std::vector<uint8_t> saved;
    _card->SaveCardState(saved);
    ASSERT_LE(saved.size(), _card->CardStateBound());

    EtherLink3::Settings s = _card->GetSettings();
    EtherLink3 other(s, [this]() { return _now; });
    ASSERT_TRUE(other.LoadCardState(saved.data(), saved.size()));
    std::vector<uint8_t> again;
    other.SaveCardState(again);
    EXPECT_EQ(again, saved);
    EXPECT_EQ(other.Status(), _card->Status());
    EXPECT_TRUE(other.Activated());
    EXPECT_EQ(other.Peek(0x08), _card->Peek(0x08)) << "RX status of the half-read packet";

    s.variant = EtherLink3::Variant::Tp;
    EtherLink3 tp(s, [this]() { return _now; });
    EXPECT_FALSE(tp.LoadCardState(saved.data(), saved.size())) << "another board";
    EXPECT_FALSE(other.LoadCardState(saved.data(), saved.size() / 2)) << "cut short";
}

TEST_F(EtherLink3_Test, Report_WhatIsPluggedAndWhatItUses)
{
    BringUp();
    StateNode out = StateNode::Object();
    _card->Describe(out);
    EXPECT_EQ(out.find("chip")->s, "3C509B-TPO");
    EXPECT_EQ(out.find("base")->s, "#300");
    EXPECT_EQ(out.find("id_port")->s, "#110");
    EXPECT_TRUE(out.find("activated")->b);
    EXPECT_EQ(out.find("window")->i, 1);
    EXPECT_EQ(out.find("mac")->s, "02:53:50:00:00:02");
    EXPECT_EQ(out.find("link_state")->s, "link pass");
    ASSERT_NE(out.find("fifo"), nullptr);
    EXPECT_EQ(out.find("fifo")->find("tx_free")->i, 3068);
    EXPECT_EQ(out.find("eeprom")->find("checksums")->s, "primary OK, secondary OK");
    EXPECT_NE(out.find("summary")->s.find("ID port #110"), std::string::npos);
    const auto aux = _card->AuxIoRanges();
    ASSERT_EQ(aux.size(), 1u);
    EXPECT_EQ(aux[0].name, "id_port");
    EXPECT_EQ(aux[0].first, 0x100u);
    EXPECT_EQ(aux[0].step, 0x10u);
    EXPECT_STREQ(_card->RegisterName(0x08, false), "RX status");
    EXPECT_STREQ(_card->RegisterName(0x00, true), "TX PIO data");
    EXPECT_STREQ(_card->RegisterName(IdOffset(0x110), true), "ID port");
}
