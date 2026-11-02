#include "tsconfhistory.h"

#include <string>

#include "emulator/ports/models/portdecoder_tsconf.h"
#include "emulator/state/statenode.h"

namespace
{
/// TsConfDma's devices ({DMA_CTRL[7], DMA_CTRL[2:0]}, [V] dma.v)
const char* DmaDeviceName(uint8_t device)
{
    switch (device)
    {
        case 0x1: return "ram";
        case 0x2: return "spi-in";
        case 0x3: return "ide-in";
        case 0x4: return "fill";
        case 0x6: return "blt2";
        case 0x9: return "blt1";
        case 0xA: return "spi-out";
        case 0xB: return "ide-out";
        case 0xC: return "cram";
        case 0xD: return "sfile";
        default: return "none";
    }
}
}  // namespace

void TsRegisterRecord::ToState(StateNode& node) const
{
    node["frame"] = int64_t(frame);
    node["tacts"] = int(tacts);
    node["pc"] = int(pc);
    node["reg"] = int(reg);
    if (const char* name = PortDecoder_TSConf::RegisterName(reg); name && *name)
        node["reg_name"] = name;
    node["value"] = int(value);
}

void TsDmaRecord::ToState(StateNode& node) const
{
    node["frame"] = int64_t(frame);
    node["tacts"] = int(tacts);
    node["pc"] = int(pc);
    node["device"] = DmaDeviceName(device);
    node["flags"] = int(flags);
    node["source"] = int64_t(source);
    node["destination"] = int64_t(destination);
    node["burst"] = int(burst);
    node["blocks"] = int(blocks);
    node["aborted"] = aborted;
    node["done"] = done;
    if (done)
        node["end_frame"] = int64_t(endFrame);
}

void Vdac2Record::ToState(StateNode& node) const
{
    node["frame"] = int64_t(frame);
    node["clock"] = int64_t(clock);
    // FT81x: the first byte's bits 7-6 tell a transaction: 00 read, 10 write, 01 host command
    const uint8_t op = header[0] >> 6;
    const uint32_t address = (uint32_t(header[0] & 0x3F) << 16) | (uint32_t(header[1]) << 8) | header[2];
    node["type"] = op == 0 ? "read" : op == 2 ? "write" : "command";
    if (op == 1)
    {
        node["command"] = int(header[0]);
        node["parameter"] = int(header[1]);
    }
    else
        node["address"] = int64_t(address);
    node["length"] = int64_t(length);
    std::string bytes;
    static const char* const kHex = "0123456789ABCDEF";
    for (uint32_t i = 0; i < length && i < sizeof(data); i++)
    {
        bytes += kHex[data[i] >> 4];
        bytes += kHex[data[i] & 0x0F];
    }
    if (!bytes.empty())
        node["data"] = bytes;
}
