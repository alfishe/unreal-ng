// ZRCP wire-format golden tests against a live 48K emulator.
//
// Byte-exact contracts DeZog parses positionally: the print_registers line
// (two spaces after R=, contiguous MMU words), get-memory-pages tokens
// (trailing space!), the 7-char disassembly prefix, uppercase contiguous
// read-memory hex and the extended-stack classification lines.

#include "zrcptestclient.h"

#include <string>

/// region <Registers line>

TEST_F(ZrcpEmulatorFixture, RegistersLineIsByteExact)
{
    // All registers set over the wire with decimal values, exactly like DeZog
    EXPECT_EQ(_client.command("set-register PC=4660"), "");    // 0x1234
    EXPECT_EQ(_client.command("set-register SP=65280"), "");   // 0xFF00
    EXPECT_EQ(_client.command("set-register AF=4660"), "");    // 0x1234 -> F="--5H-P--"
    EXPECT_EQ(_client.command("set-register BC=48879"), "");   // 0xBEEF
    EXPECT_EQ(_client.command("set-register HL=57005"), "");   // 0xDEAD
    EXPECT_EQ(_client.command("set-register DE=51966"), "");   // 0xCAFE
    EXPECT_EQ(_client.command("set-register IX=4369"), "");    // 0x1111
    EXPECT_EQ(_client.command("set-register IY=8738"), "");    // 0x2222
    EXPECT_EQ(_client.command("set-register AF'=13107"), "");  // 0x3333 -> F'="--5H--NC"
    EXPECT_EQ(_client.command("set-register BC'=17476"), "");  // 0x4444
    EXPECT_EQ(_client.command("set-register HL'=21845"), "");  // 0x5555
    EXPECT_EQ(_client.command("set-register DE'=26214"), "");  // 0x6666
    EXPECT_EQ(_client.command("set-register I=63"), "");       // 0x3F
    EXPECT_EQ(_client.command("set-register R=33"), "");       // 0x21
    EXPECT_EQ(_client.command("set-register IM=1"), "");

    // print_registers format: lowercase hex, TWO spaces after R=, MEMPTR
    // fixed 0000, IFF-- and VPS: 50 placeholders, 8 contiguous MMU words.
    // 128K fixture with RM_SOS reset: ROM1, 5, 2, 0 repeated twice.
    EXPECT_EQ(_client.command("get-registers"),
              "PC=1234 SP=ff00 AF=1234 BC=beef HL=dead DE=cafe IX=1111 IY=2222 "
              "AF'=3333 BC'=4444 HL'=5555 DE'=6666 I=3f R=21  F=--5H-P-- F'=--5H--NC "
              "MEMPTR=0000 IM1 IFF-- VPS: 50 MMU=80010005000200008001000500020000");
}

TEST_F(ZrcpEmulatorFixture, MemoryPagesGolden)
{
    // 128K with RM_SOS: ROM1, RAM 5, RAM 2, RAM 0 - each token followed by a
    // space; DeZog counts tokens via split(' ').length - 1
    EXPECT_EQ(_client.command("get-memory-pages"), "RO1 RA5 RA2 RA0 ");
}

TEST_F(ZrcpEmulatorFixture, MachineString128K)
{
    EXPECT_EQ(_client.command("get-current-machine"), "ZX Spectrum 128K");
}

/// endregion </Registers line>

/// region <Memory hex>

TEST_F(ZrcpEmulatorFixture, MemoryRoundTripIsUppercaseContiguousHex)
{
    EXPECT_EQ(_client.command("write-memory-raw 36864 DEADBEEF"), "");
    EXPECT_EQ(_client.command("read-memory 36864 4"), "DEADBEEF");
    EXPECT_EQ(_client.command("read-memory 36864 1"), "DE");
}

/// endregion </Memory hex>

/// region <Disassembly lines>

TEST_F(ZrcpEmulatorFixture, DisassemblyPrefixAndUpperMnemonics)
{
    installProgram();

    // Decimal address like DeZog sends; 2 lines
    const std::string out = _client.command("disassemble 32768 2");
    // Line 1: 8000 (F3 = DI), zone digit = slot 2 bank 2 on 48K
    // Line 2: 8001 (3E 01 = LD A,n)
    const size_t split = out.find('\n');
    ASSERT_NE(split, std::string::npos);
    const std::string line1 = out.substr(0, split);
    const std::string line2 = out.substr(split + 1);
    EXPECT_EQ(line1.substr(0, 7), "8000 2 ");
    EXPECT_EQ(line1.substr(7, 2), "DI");
    EXPECT_EQ(line2.substr(0, 7), "8001 2 ");
    EXPECT_EQ(line2.substr(7, 2), "LD");
}

/// endregion </Disassembly lines>

/// region <Step output>

TEST_F(ZrcpEmulatorFixture, StepOutputIncludesRegistersWithTstates)
{
    initSession();
    installProgram();

    // debug settings bit 0: step/run output carries the registers line
    EXPECT_EQ(_client.command("set-debug-settings 1"), "");

    const std::string out = _client.command("cpu-step");
    const size_t split = out.find('\n');
    ASSERT_NE(split, std::string::npos);
    const std::string regs = out.substr(0, split);
    const std::string disasm = out.substr(split + 1);

    EXPECT_EQ(regs.substr(0, 3), "PC=");
    EXPECT_EQ(regs.substr(regs.size() - 11), " TSTATES: 0");
    // Stepped the DI at 8000: disassembly at 8001, slot 2
    EXPECT_EQ(disasm.substr(0, 7), "8001 2 ");
}

/// endregion </Step output>

/// region <cpu-history line>

TEST_F(ZrcpEmulatorFixture, HistoryLineShapeAndFields)
{
    initSession();
    installProgram();

    // Produce history: run until the JP breakpoint
    EXPECT_EQ(_client.command("set-breakpoint 1 PC=08006h"), "");
    EXPECT_EQ(_client.command("enable-breakpoint 1"), "");
    _client.sendLine("run");
    EXPECT_EQ(_client.readLine(), "Running until a breakpoint, key press or data sent, menu opening or other event");
    EXPECT_EQ(_client.readLine(), "Breakpoint fired: PC=08006h");
    EXPECT_FALSE(_client.readUntilPrompt().empty());  // step output (settings 0: 1 line)

    EXPECT_EQ(_client.command("cpu-history get-max-size"), "32768");
    EXPECT_EQ(_client.command("cpu-history set-max-size 100"), "");
    EXPECT_EQ(_client.command("cpu-history get-max-size"), "100");

    // get-size is probed over getHistoryEntry: some loop iterations recorded
    const std::string size = _client.command("cpu-history get-size");
    ASSERT_FALSE(size.empty());
    EXPECT_GT(std::stoul(size), 0u);

    // cpu_history_legacy_regs_bin_to_string: same register prefix, then
    // IM%d IFF-- (PC)=<8 hex> (SP)=<4 hex> MMU=<32 hex> with a TRAILING space
    const std::string line = _client.command("cpu-history get 0");
    ASSERT_EQ(line.substr(0, 3), "PC=");

    const size_t pcEq = line.find("PC=");
    const uint16_t pc = static_cast<uint16_t>(std::stoul(line.substr(pcEq + 3, 4), nullptr, 16));
    const size_t spEq = line.find("SP=", pcEq + 3);
    const uint16_t sp = static_cast<uint16_t>(std::stoul(line.substr(spEq + 3, 4), nullptr, 16));

    const size_t pcMem = line.find("(PC)=");
    ASSERT_NE(pcMem, std::string::npos);
    const std::string pcBytes = line.substr(pcMem + 5, 8);
    const size_t spMem = line.find("(SP)=");
    ASSERT_NE(spMem, std::string::npos);
    const std::string spBytes = line.substr(spMem + 5, 4);

    // (PC) holds the 4 opcode bytes at PC (lowercase hex, like ZEsarUX),
    // (SP) the word at SP (hi byte first) - compare against read-memory
    // (uppercase) case-insensitively
    std::string pcUpper;
    for (char c : pcBytes)
        pcUpper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::string spUpper;
    for (char c : spBytes)
        spUpper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    EXPECT_EQ(pcUpper, _client.command("read-memory " + std::to_string(pc) + " 4"));
    EXPECT_EQ(spUpper, _client.command("read-memory " + std::to_string(sp) + " 2"));

    // MMU: 128K RM_SOS paging repeated twice, single trailing space
    EXPECT_NE(line.find("MMU=80010005000200008001000500020000 "), std::string::npos);
    EXPECT_EQ(line.back(), ' ');
    // No MEMPTR/VPS fields in the history variant
    EXPECT_EQ(line.find("MEMPTR"), std::string::npos);
    EXPECT_EQ(line.find("VPS"), std::string::npos);

    // Out of range: exact ZEsarUX error (DeZog checks substring(0,5)=="error")
    EXPECT_EQ(_client.command("cpu-history get 99999999"), "ERROR: index out of range");
}

/// endregion </cpu-history line>

/// region <extended-stack lines>

TEST_F(ZrcpEmulatorFixture, ExtendedStackClassificationLines)
{
    initSession();
    installProgram();

    // Opcode anchors: CALL at 0x9000, RST 8 at 0x9004
    EXPECT_EQ(_client.command("write-memory-raw 36864 CD000000CF"), "");
    // Stack at SP=0xFF00: 0x9003 (call), 0x9005 (rst), 0x8007 (push)
    EXPECT_EQ(_client.command("write-memory-raw 65280 039005900780"), "");

    EXPECT_EQ(_client.command("extended-stack enabled yes"), "");
    EXPECT_EQ(_client.command("extended-stack get 3"),
              "9003H call\n9005H rst\n8007H push");

    // Not enabled in a fresh session part of the same connection
    EXPECT_EQ(_client.command("extended-stack enabled no"), "");
    EXPECT_EQ(_client.command("extended-stack get 1"), "Error. It's not enabled");
}

/// endregion </extended-stack lines>
