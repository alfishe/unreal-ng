#include "snapshotimage.h"

#include <cstdio>

namespace snapshot
{
const char* ToText(MemoryModel model)
{
    switch (model)
    {
        case MemoryModel::Mem48k: return "48k";
        case MemoryModel::Mem128k: return "128k";
        case MemoryModel::Extended: return "extended";
        case MemoryModel::Physical: return "physical";
    }
    return "?";
}

size_t Image::RamBytes() const
{
    size_t total = 0;
    for (const auto& bank : banks)
        total += bank.second.size();
    for (const PhysicalRun& run : physical)
        total += run.data.size();
    return total;
}

std::string HashText(const std::vector<uint8_t>& bytes)
{
    uint64_t h = 14695981038346656037ull;
    for (uint8_t b : bytes)
    {
        h ^= b;
        h *= 1099511628211ull;
    }
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(h));
    return text;
}

namespace
{
std::string Hex(unsigned value, int digits)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%0*X", digits, value);
    return text;
}

void PutOptional(StateNode& node, const char* name, const std::optional<uint8_t>& value)
{
    if (value)
        node[name] = Hex(*value, 2);
}
}  // namespace

StateNode ToStateNode(const Image& image)
{
    StateNode n = StateNode::Object();
    n["format"] = image.format;
    n["format_version"] = image.formatVersion;
    n["machine_hint"] = image.machineHint;
    if (!image.rawMachineId.empty())
        n["raw_machine_id"] = image.rawMachineId;
    n["memory_model"] = ToText(image.memoryModel);

    StateNode banks = StateNode::Array();
    for (const auto& bank : image.banks)
    {
        StateNode b = StateNode::Object();
        b["bank"] = static_cast<uint64_t>(bank.first);
        b["size"] = static_cast<uint64_t>(bank.second.size());
        b["hash"] = HashText(bank.second);
        banks.push(b);
    }
    n["banks"] = banks;
    if (!image.physical.empty())
    {
        StateNode runs = StateNode::Array();
        for (const PhysicalRun& run : image.physical)
        {
            StateNode r = StateNode::Object();
            r["address"] = Hex(run.address, 6);
            r["size"] = static_cast<uint64_t>(run.data.size());
            r["hash"] = HashText(run.data);
            runs.push(r);
        }
        n["physical"] = runs;
    }

    StateNode paging = StateNode::Object();
    PutOptional(paging, "p7FFD", image.paging.p7FFD);
    PutOptional(paging, "p1FFD", image.paging.p1FFD);
    PutOptional(paging, "pEFF7", image.paging.pEFF7);
    PutOptional(paging, "pDFFD", image.paging.pDFFD);
    n["paging"] = paging;
    n["trdos_paged"] = image.trdosPaged;

    const Cpu& c = image.cpu;
    StateNode cpu = StateNode::Object();
    for (const auto& reg : {std::pair<const char*, uint16_t>{"af", c.af}, {"bc", c.bc}, {"de", c.de}, {"hl", c.hl},
                            {"ix", c.ix}, {"iy", c.iy}, {"sp", c.sp}, {"pc", c.pc}, {"af2", c.af2}, {"bc2", c.bc2},
                            {"de2", c.de2}, {"hl2", c.hl2}})
        cpu[reg.first] = Hex(reg.second, 4);
    cpu["i"] = Hex(c.i, 2);
    cpu["r"] = Hex(c.r, 2);
    cpu["iff1"] = c.iff1;
    cpu["iff2"] = c.iff2;
    cpu["im"] = static_cast<uint64_t>(c.im);
    if (c.memptr)
        cpu["memptr"] = Hex(*c.memptr, 4);
    if (c.q)
        cpu["q"] = Hex(*c.q, 2);
    if (c.halted)
        cpu["halted"] = *c.halted;
    if (c.eiShadow)
        cpu["ei_shadow"] = *c.eiShadow;
    n["cpu"] = cpu;

    if (image.framePosition)
        n["frame_position"] = static_cast<uint64_t>(*image.framePosition);
    n["border"] = static_cast<uint64_t>(image.border);
    if (!image.timingHint.empty())
        n["timing_hint"] = image.timingHint;

    StateNode ay = StateNode::Array();
    for (const Ay& chip : image.ay)
    {
        StateNode a = StateNode::Object();
        std::string regs;
        for (uint8_t r : chip.registers)
            regs += Hex(r, 2);
        a["registers"] = regs;
        a["selected"] = static_cast<uint64_t>(chip.selected);
        ay.push(a);
    }
    n["ay"] = ay;

    StateNode extensions = StateNode::Array();
    for (const Extension& e : image.extensions)
    {
        StateNode x = StateNode::Object();
        x["origin"] = e.origin;
        x["kind"] = e.kind;
        x["size"] = static_cast<uint64_t>(e.size);
        if (!e.note.empty())
            x["note"] = e.note;
        extensions.push(x);
    }
    n["extensions"] = extensions;

    StateNode warnings = StateNode::Array();
    for (const std::string& w : image.warnings)
        warnings.push(w);
    n["warnings"] = warnings;
    return n;
}
}  // namespace snapshot
