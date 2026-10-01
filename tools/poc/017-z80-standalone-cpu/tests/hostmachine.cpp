// hostmachine.cpp - minimal host machine for the standalone Z80 core (PoC 017).

#include "hostmachine.h"

#include <cstdio>
#include <cstring>

HostMachine::HostMachine()
{
    cpu = Z80CpuCreate();
    Z80CpuAttachMemory(cpu, mem);
    Reset();
}

HostMachine::~HostMachine()
{
    Z80CpuDestroy(cpu);
}

void HostMachine::Reset()
{
    std::memset(mem, 0, sizeof(mem));
    Z80CpuReset(cpu);
    output.clear();
    instructions = 0;

    // Stub ROM entries used by ZEXALL-family programs: RST 0x08 (error),
    // RST 0x10 (print char) and RST 0x18 would normally live in the 48K ROM.
    // StepCapture intercepts execution at 0x0010 before these run, but keep
    // plain RETs there so an uninterrupted run cannot loop forever.
    mem[0x0008] = 0xC9;
    mem[0x0010] = 0xC9;
    mem[0x0018] = 0xC9;
}

void HostMachine::LoadCode(uint16_t addr, const uint8_t* data, size_t len)
{
    if (addr + len > 0x10000)
        len = 0x10000 - addr;
    std::memcpy(mem + addr, data, len);
}

int HostMachine::StepCapture()
{
    int t = Z80CpuStep(cpu);
    instructions++;

    const uint16_t pc = Z80CpuGetReg(cpu, Z80CpuRegPc);

    // Soft-ROM emulation of the 48K ROM entries the ZEXALL family uses
    // (the programs are built for a ZX Spectrum but run here on flat RAM):
    //
    //  RST 0x10 - print character in A, then behave like RET
    if (pc == 0x0010)
    {
        uint8_t ch = static_cast<uint8_t>(Z80CpuGetReg(cpu, Z80CpuRegAf) >> 8);
        if (ch >= 0x20 || ch == '\n' || ch == '\r')
            output.push_back(static_cast<char>(ch));
        SoftRomReturn();
    }
    //  CALL 0x1601 - "open channel K/S" (LD A,2 precedes it): no-op RET
    else if (pc == 0x1601)
    {
        SoftRomReturn();
    }
    //  RST 0x08 - ZX error: the byte after the opcode is the error number.
    //  Record it and continue (ZEX programs never report errors on success).
    else if (pc == 0x0008)
    {
        romErrorHit = true;
        romErrorCode = mem[Z80CpuGetReg(cpu, Z80CpuRegPc)];
        Z80CpuSetReg(cpu, Z80CpuRegPc, static_cast<uint16_t>(pc + 1));
        SoftRomReturn();
    }

    return t;
}

void HostMachine::SoftRomReturn()
{
    uint16_t sp = Z80CpuGetReg(cpu, Z80CpuRegSp);
    uint16_t ret = mem[sp] | (static_cast<uint16_t>(mem[static_cast<uint16_t>(sp + 1)]) << 8);
    Z80CpuSetReg(cpu, Z80CpuRegPc, ret);
    Z80CpuSetReg(cpu, Z80CpuRegSp, static_cast<uint16_t>(sp + 2));
}

uint64_t HostMachine::Run(uint64_t maxTstates, bool (*predicate)(HostMachine&, void*), void* userData,
                          uint16_t breakPc)
{
    const uint32_t t0 = Z80CpuTstates(cpu);
    while (Z80CpuTstates(cpu) - t0 < maxTstates)
    {
        if (predicate && predicate(*this, userData))
            break;
        if (breakPc != 0xFFFF && Z80CpuGetReg(cpu, Z80CpuRegPc) == breakPc)
            break;
        StepCapture();
    }
    return Z80CpuTstates(cpu) - t0;
}

namespace
{
struct MarkerCtx
{
    const std::string* marker;
    bool found;
};
}

uint64_t HostMachine::RunUntilOutput(const std::string& marker, uint64_t maxTstates)
{
    const uint32_t t0 = Z80CpuTstates(cpu);

    // Characters arrive rarely (one RST 0x10 per printed char); scan only the
    // freshly appended region plus the marker-length overlap, not the whole
    // string, so the per-instruction cost stays negligible.
    size_t scanned = 0;
    while (Z80CpuTstates(cpu) - t0 < maxTstates)
    {
        StepCapture();
        if (output.size() > scanned)
        {
            const size_t from = scanned >= marker.size() ? scanned - marker.size() : 0;
            if (output.find(marker, from) != std::string::npos)
                break;
            scanned = output.size();
        }
    }
    return Z80CpuTstates(cpu) - t0;
}

bool HostMachine::LoadTapCodeBlocks(const std::string& path, std::vector<TapCodeBlock>& blocks)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return false;

    // TAP layout: repeated [uint16 blockLen][blockLen bytes]. The block data
    // is flag + payload + checksum. A flag=0 block of length 19 is a header
    // describing the following flag=0xFF data block.
    bool ok = false;
    TapCodeBlock pending;
    bool havePendingHeader = false;

    for (;;)
    {
        uint8_t lenb[2];
        if (fread(lenb, 1, 2, f) != 2)
        {
            ok = true;  // clean EOF
            break;
        }
        uint16_t blockLen = static_cast<uint16_t>(lenb[0] | (lenb[1] << 8));
        if (blockLen == 0)
            break;  // corrupt

        std::vector<uint8_t> block(blockLen);
        if (fread(block.data(), 1, blockLen, f) != blockLen)
            break;

        const uint8_t flag = block[0];
        const uint8_t* payload = block.data() + 1;
        const size_t payloadLen = block.size() - 1;  // minus flag; checksum is last

        if (flag == 0x00 && blockLen == 19)
        {
            // Header block: type, name[10], length, param1, param2 (LE), checksum
            // For CODE blocks param1 = load/start address, param2 = 0x8000 marker
            uint8_t type = payload[0];
            if (type == 0x03)
            {
                pending.name.assign(reinterpret_cast<const char*>(payload + 1), 10);
                pending.startAddress = static_cast<uint16_t>(payload[13] | (payload[14] << 8));
                pending.data.clear();
                havePendingHeader = true;
            }
            else
            {
                havePendingHeader = false;
            }
        }
        else if (flag == 0xFF && havePendingHeader)
        {
            // Data block for the last seen header
            if (payloadLen >= 1)
                pending.data.assign(payload, payload + payloadLen - 1);  // strip checksum
            blocks.push_back(pending);
            pending = TapCodeBlock{};
            havePendingHeader = false;
        }
    }

    fclose(f);
    return ok && !blocks.empty();
}

bool HostMachine::LoadTapProgram(const std::string& path, uint16_t& entry)
{
    std::vector<TapCodeBlock> blocks;
    if (!LoadTapCodeBlocks(path, blocks) || blocks.empty())
        return false;

    LoadCode(blocks[0].startAddress, blocks[0].data.data(), blocks[0].data.size());
    entry = blocks[0].startAddress;
    return true;
}

bool HostMachine::RunZex(const std::string& tapPath, uint64_t maxTstates, bool verbose)
{
    Reset();

    uint16_t entry;
    if (!LoadTapProgram(tapPath, entry))
    {
        fprintf(stderr, "zex: cannot load %s\n", tapPath.c_str());
        return false;
    }

    Z80CpuSetReg(cpu, Z80CpuRegPc, entry);

    // These Kevin Horton exercisers print one "....OK" line per test group
    // and finish with "Tests complete" (on failure they stop early without
    // it). First test of zexdoc (<adc,sbc> hl) alone needs ~2.3e9 T.
    RunUntilOutput("Tests complete", maxTstates);

    const bool success = output.find("Tests complete") != std::string::npos;

    if (verbose)
    {
        for (char c : output)  // drop CR so logs stay line-oriented
            if (c != '\r')
                putchar(c);
        putchar('\n');
    }
    if (romErrorHit)
        printf("  (RST 08h error code %u raised)\n", static_cast<unsigned>(romErrorCode));
    printf("[%s] %s: %.2fM T-states (%.2fs at 3.5MHz), %llu instructions\n", tapPath.c_str(),
           success ? "PASS" : "FAIL",
           static_cast<double>(Z80CpuTstates(cpu)) / 1e6,
           static_cast<double>(Z80CpuTstates(cpu)) / 3.5e6,
           static_cast<unsigned long long>(instructions));

    return success;
}
