// unreal-debugger: eCat3's debugger API (ecatshim.h) over unreal-ng's core.
//
// The machine is two processors for the window: the main Z80 ("cpu") and, when the slot has one, the General Sound
// card's Z80 ("gs"). Below them the devices the dumps and the Devices tab show: the main Z80's page map (a mapper,
// so a dump names "ram 5 + 0123" for an address), all RAM, all ROM, every device memory region of the machine, and
// the core's device reports (DeviceState) as fields.
//
// Threading, the one real difference from ecat: ecat's window hands every change to the emulation thread through
// Emulator::invoke(). unreal-ng has no such queue for a running machine that also serves run control, so invoke()
// runs in place and every operation here synchronises itself the way unreal-qt does: memory and register edits are
// tool edits (EditMemoryFromTool / SetRegisterFromTool park the machine), the card's state is touched at a coherent
// moment, breakpoints go through their manager (thread-safe), steps are direct runs (DirectStepScope). Reads for
// display are direct, as in ecat: while the machine runs a value may be one instruction stale.

#include "unrealmachine.h"
#include "zxwindows.h"

#include <QCoreApplication>
#include <QDir>
#include <QPointer>
#include <QSettings>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <set>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/history/exechistory.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/search/memorysearch.h"
#include "debugger/targets/gscardtarget.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdsession.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/config.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/runcontrol.h"
#include "emulator/memory/devicememory.h"
#include "emulator/cpu/core.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/rom.h"
#include "emulator/state/statenodejson.h"
#include "emulator/notifications.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicecatalog.h"
#include "emulator/state/devicestate.h"

namespace {

using ecatshim::CpuRegister;
using ecatshim::DeviceFieldInfo;
using ecatshim::DeviceFieldValue;
using ecatshim::DisasmLine;

const char * kSource = "ecat-debugger";
const std::string kRunToOwner = "ecat-run-to";
const execution::Requester kGui{execution::Origin::Gui, "ecat-debugger"};

// A card edit or read that must not race the card's run: paused, stopped or between two frames
void at_coherent_moment(::Emulator * emu, const std::function<void()> &fn)
{
    emu->RunAtCoherentMoment(fn, 500);
}

//------------------------------------------------------------------- disassembler

// ecat's DisAsm over unreal-ng's Z80 decoder. The labels of the main machine are shown for its Z80 only
class Z80DisAsm : public ecatshim::DisAsm
{
public:
    Z80DisAsm(EmulatorContext * context, bool labels) : m_dis(context), m_context(context), m_labels(labels)
    {
        max_command_length = Z80Disassembler::MAX_INSTRUCTION_LENGTH;
    }

    unsigned int decode(ecatshim::CommandBytes bytes, unsigned int PC, unsigned int max_len, DisasmLine * out) override
    {
        std::vector<uint8_t> buf((*bytes), (*bytes) + std::min<unsigned int>(max_len, 15));
        if (buf.size() < Z80Disassembler::MAX_INSTRUCTION_LENGTH) buf.resize(Z80Disassembler::MAX_INSTRUCTION_LENGTH, 0);
        DecodedInstruction d;
        uint8_t len = 0;
        std::string text;
        // ecat's listing: upper case, labels for addresses only ("LD HL,#0000", not "LD HL,START (#0000)")
        Z80Disassembler::ListingStyleScope style({true, false});
        if (m_labels) {
            text = m_dis.disassembleSingleCommand(buf, (uint16_t)PC, &len, &d);
        } else {
            Z80Disassembler::NoLabelsScope plain;
            text = m_dis.disassembleSingleCommand(buf, (uint16_t)PC, &len, &d);
        }
        DisasmLine l;
        l.length = len ? len : 1;
        l.valid = d.isValid;
        l.text = d.isValid ? text : "DB " + hex(buf[0]);
        if (m_labels) l.label = d.label;
        const uint32_t f = d.opcode.flags;
        l.conditional = d.hasCondition || (f & OF_CONDITION) || d.isDjnz;
        if (d.hasReturn || (f & OF_RET)) {
            l.flow = ecatshim::FLOW_RET;
        } else if ((f & OF_CALL) || d.isRst) {
            l.flow = ecatshim::FLOW_CALL;
            l.has_target = true;
            l.target = d.isRst ? (buf[0] & 0x38) : d.jumpAddr;
        } else if (d.hasRelativeJump || d.isDjnz || (f & OF_RELJUMP)) {
            l.flow = ecatshim::FLOW_JUMP;
            l.has_target = true;
            l.target = d.relJumpAddr;
        } else if (d.hasJump || (f & OF_JUMP)) {
            l.flow = ecatshim::FLOW_JUMP;
            // JP (HL) / (IX) / (IY) goes through a register
            l.has_target = !(buf[0] == 0xE9 || ((buf[0] == 0xDD || buf[0] == 0xFD) && buf[1] == 0xE9));
            l.target = d.jumpAddr;
        }
        if (out) *out = l;
        return l.length;
    }

    unsigned int label_chars() override
    {
        LabelManager * lm = label_manager();
        if (lm == nullptr) return 0;
        const size_t count = lm->GetLabelCount();
        if (count != m_label_count) {
            m_label_count = count;
            m_label_chars = 0;
            for (const std::shared_ptr<Label> &l : lm->GetAllLabels())
                if (l && l->active) m_label_chars = std::max(m_label_chars, (unsigned int)l->name.size());
        }
        return m_label_chars;
    }
    unsigned int labels_serial() override
    {
        LabelManager * lm = label_manager();
        return lm ? (unsigned int)lm->GetLabelCount() : 0;
    }

private:
    Z80Disassembler m_dis;
    EmulatorContext * m_context;
    bool m_labels;
    size_t m_label_count = (size_t)-1;
    unsigned int m_label_chars = 0;

    LabelManager * label_manager() const
    {
        if (!m_labels || m_context == nullptr || m_context->pDebugManager == nullptr) return nullptr;
        return m_context->pDebugManager->GetLabelManager();
    }

    static std::string hex(uint8_t b)
    {
        char t[8];
        snprintf(t, sizeof(t), "#%02X", b);
        return t;
    }
};

//------------------------------------------------------------------- memories

// The physical pages of one kind (RAM, ROM) as one dump
class PageMemory : public ecatshim::Memory
{
public:
    PageMemory(::Emulator * emu, MemoryBankModeEnum kind, uint32_t pages, const char * title)
        : m_emu(emu), m_kind(kind), m_pages(pages)
    {
        name = title;
        type = (kind == BANK_RAM) ? "ram" : "rom";
    }
    unsigned int get_size() override { return m_pages * PAGE_SIZE; }
    unsigned int page_size() override { return PAGE_SIZE; }
    std::string page_title(unsigned int page) override
    {
        if (m_kind == BANK_RAM) return ZxRamNote(m_paging, page);
        // The ROM the page holds now, by its signature: a flash write or an edit gives it another name. Blank pages
        // and copies of a lower page say so (ROM::GetROMPageTitle); kept per page and signature
        ROM * rom = m_emu->GetContext()->pCore ? m_emu->GetContext()->pCore->GetROM() : nullptr;
        const uint8_t * host = MemorySearch::PageHost(m_emu->GetContext(), BANK_ROM, page);
        if (rom == nullptr || host == nullptr) return std::string();
        const std::string signature = rom->CalculateSignature(host, PAGE_SIZE);
        auto it = m_titles.find({page, signature});
        if (it == m_titles.end())
            it = m_titles.emplace(std::make_pair(page, signature), ZxRomPageNote(rom->GetROMPageTitle((uint8_t)page))).first;
        return it->second;
    }
    std::string page_role(unsigned int page) override
    {
        if (m_kind == BANK_RAM) return std::string();
        ROM * rom = m_emu->GetContext()->pCore ? m_emu->GetContext()->pCore->GetROM() : nullptr;
        const std::string role = rom ? rom->GetROMPageRole((uint8_t)page) : std::string();
        // Models without a curated layout name every slot "ROM Page N": nothing to add to "rom N"
        return role.rfind("ROM Page", 0) == 0 ? std::string() : role;
    }
    void set_paging(const ZxPaging &p) { m_paging = p; }
    unsigned int get_direct(unsigned int a) override
    {
        const uint8_t * p = MemorySearch::PageHost(m_emu->GetContext(), m_kind, a / PAGE_SIZE);
        return p ? p[a % PAGE_SIZE] : 0xFF;
    }
    void set_value(unsigned int a, unsigned int v, bool) override
    {
        ::Emulator * emu = m_emu;
        const MemoryBankModeEnum kind = m_kind;
        emu->EditMemoryFromTool(kSource, [emu, kind, a, v]() {
            ::Memory * m = emu->GetMemory();
            const uint32_t page = a / PAGE_SIZE;
            uint8_t * p = (kind == BANK_RAM) ? m->RAMPageAddress((uint16_t)page) : m->ROMPageHostAddress((uint8_t)page);
            if (p == nullptr) return;
            p[a % PAGE_SIZE] = (uint8_t)v;
            if (kind == BANK_RAM) m->MarkRamPageEdited((uint16_t)page);
        });
    }

private:
    ::Emulator * m_emu;
    MemoryBankModeEnum m_kind;
    uint32_t m_pages;
    ZxPaging m_paging;
    // A ROM's title by its signature: the lookup once per ROM image, the signature at each ask
    std::map<std::pair<unsigned int, std::string>, std::string> m_titles;
};

// A device memory region (DeviceMemory): a card's RAM and flash, video memory, a clock's CMOS. As a Memory it is a
// dump tab of its own, as a plain AddressableDevice an entry of the tabs' "+" menu
template <class Base>
class RegionMemory : public Base
{
public:
    RegionMemory(::Emulator * emu, IDeviceMemoryRegion * r) : m_emu(emu), m_name(r->Name()), m_size(r->Size())
    {
        this->name = m_name;
        this->type = short_description(r->Description());
        m_writable = r->Writable();
    }
    unsigned int get_size() override { return m_size; }
    unsigned int get_direct(unsigned int a) override
    {
        // A lookup lists every device of the machine; the dump reads a screen of bytes in order, so the region is
        // looked up again only where a run of reads starts (a card switch replaces it between two refreshes)
        if (m_region == nullptr || a != m_next) m_region = DeviceMemory::Find(m_emu->GetContext(), m_name);
        m_next = a + 1;
        return (m_region != nullptr && a < m_region->Size()) ? m_region->Read(a) : 0xFF;
    }
    void set_value(unsigned int a, unsigned int v, bool) override
    {
        std::string error;
        DeviceMemory::Write(m_emu->GetContext(), m_name, a, {(uint8_t)v}, kSource, error);
    }
    bool debug_writable(unsigned int) override { return m_writable; }

    // The menu shows "name : type": the description's first clause, not its paragraph
    static std::string short_description(const std::string &d)
    {
        if (d.rfind("Device memory recorded by time travel", 0) == 0) return "device memory";
        return d.substr(0, d.find_first_of(":,;("));
    }

private:
    ::Emulator * m_emu;
    std::string m_name;
    uint32_t m_size;
    bool m_writable = true;
    IDeviceMemoryRegion * m_region = nullptr;
    unsigned int m_next = 0;
};

// A sound card's RAM or flash: its Z80 maps them a 16 KB page at a time
class CardPages : public RegionMemory<ecatshim::Memory>
{
public:
    using RegionMemory<ecatshim::Memory>::RegionMemory;
    unsigned int page_size() override { return PAGE_SIZE; }
};

// The main Z80's 64 KB as its page map: whose byte an address is
class ZxMapper : public ecatshim::MemoryMapper
{
public:
    ZxMapper(::Emulator * emu, ecatshim::AddressableDevice * ram, ecatshim::AddressableDevice * rom)
        : m_emu(emu), m_ram(ram), m_rom(rom)
    {
        name = "mapper";
        type = "z80-pages";
    }
    unsigned int get_size() override { return 0x10000; }
    unsigned int get_direct(unsigned int a) override { return m_emu->GetMemory()->DirectReadFromZ80Memory((uint16_t)a); }
    void set_value(unsigned int a, unsigned int v, bool) override
    {
        ::Emulator * emu = m_emu;
        emu->EditMemoryFromTool(kSource, [emu, a, v]() { emu->GetMemory()->DirectWriteToZ80Memory((uint16_t)a, (uint8_t)v); });
    }
    ecatshim::AddressableDevice * peek_owner(unsigned int address, unsigned int &offset) override
    {
        const MemoryPageDescriptor d = m_emu->GetMemory()->MapZ80AddressToPhysicalPage((uint16_t)address);
        offset = (unsigned int)d.page * PAGE_SIZE + d.addressInPage;
        if (d.mode == BANK_RAM) return m_ram;
        if (d.mode == BANK_ROM) return m_rom;
        return nullptr;
    }

private:
    ::Emulator * m_emu;
    ecatshim::AddressableDevice * m_ram;
    ecatshim::AddressableDevice * m_rom;
};

//------------------------------------------------------------------- devices

// The text of a report's scalar
std::string scalar_text(const StateNode &n)
{
    switch (n.kind) {
    case StateNode::Kind::Bool: return n.b ? "true" : "false";
    case StateNode::Kind::Int: return std::to_string(n.i);
    case StateNode::Kind::Double: {
        char b[32];
        snprintf(b, sizeof(b), "%g", n.d);
        return b;
    }
    case StateNode::Kind::String: return n.s;
    default: return "null";
    }
}

bool is_scalar(const StateNode &n) { return n.kind != StateNode::Kind::Object && n.kind != StateNode::Kind::Array; }

// A report's part as the Devices tab shows it (remotemachine.cpp's to_tree does the same with JSON): an object's
// members under it, named by its first naming field; a short list of scalars on one line; a longer list item by item
void to_tree(const StateNode &n, ecatshim::DeviceNode &out)
{
    if (n.kind == StateNode::Kind::Object) {
        for (const char * key : {"name", "id", "slot", "card", "device", "kind", "model"})
            if (const StateNode * v = n.find(key); v != nullptr && v->kind == StateNode::Kind::String && !v->s.empty()) {
                out.value = v->s;
                break;
            }
        for (const auto &m : n.members) {
            ecatshim::DeviceNode c;
            c.name = m.first;
            to_tree(m.second, c);
            out.children.push_back(std::move(c));
        }
    } else if (n.kind == StateNode::Kind::Array) {
        bool scalars = n.items.size() <= 32;
        for (const StateNode &i : n.items) scalars = scalars && is_scalar(i);
        if (scalars) {
            for (size_t i = 0; i < n.items.size(); i++) out.value += (i ? " " : "") + scalar_text(n.items[i]);
            if (n.items.empty()) out.value = "none";
            return;
        }
        out.value = std::to_string(n.items.size()) + (n.items.size() == 1 ? " item" : " items");
        const size_t shown = std::min<size_t>(n.items.size(), 256);
        for (size_t i = 0; i < shown; i++) {
            ecatshim::DeviceNode c;
            c.name = "[" + std::to_string(i) + "]";
            to_tree(n.items[i], c);
            out.children.push_back(std::move(c));
        }
        if (shown < n.items.size())
            out.children.push_back(ecatshim::DeviceNode{"", "+ " + std::to_string(n.items.size() - shown) + " more items", {}});
    } else {
        out.value = scalar_text(n);
    }
}

// One device's history (the machine's history registry): an entry of its own in the Devices tab, every device's
// apart. The summary is its mode and counts, opened the newest records; read as a snapshot, the machine runs on
// A history record as one line: "frame 12  pc 32785  kind ram", a nested part in braces, a list in brackets
std::string line_text(const StateNode &n)
{
    if (n.kind == StateNode::Kind::Object) {
        std::string line;
        for (const auto &m : n.members) {
            const std::string part = m.second.kind == StateNode::Kind::Object ? "{" + line_text(m.second) + "}" : line_text(m.second);
            line += (line.empty() ? "" : "  ") + m.first + " " + part;
        }
        return line;
    }
    if (n.kind == StateNode::Kind::Array) {
        std::string line;
        for (const StateNode &i : n.items) line += (line.empty() ? "" : " ") + line_text(i);
        return "[" + line + "]";
    }
    return scalar_text(n);
}

// One device's history in the core's registry for the History tab: its counts, the newest records, recording on or
// off. Read on the emulation thread (the view invokes)
class HistoryDevice : public ecatshim::History
{
public:
    HistoryDevice(::Emulator * emu, const std::string &device, std::set<std::string> * ours)
        : m_emu(emu), m_device(device), m_ours(ours)
    {
        name = device + " history";
        type = "history";
    }
    std::string device() const override { return m_device; }
    Summary summary() override
    {
        Summary out;
        m_emu->GetContext()->histories.With(m_device, [&out](history::IDeviceHistory &h) {
            const history::Stats s = h.GetStats();
            out.what = h.What();
            out.recording = s.config.mode != history::Mode::Off;
            out.kept = s.kept;
            out.written = s.written;
        });
        return out;
    }
    std::vector<std::string> records(unsigned int count) override
    {
        std::vector<std::string> out;
        StateNode records;
        m_emu->GetContext()->histories.With(m_device, [&records, count](history::IDeviceHistory &h) { records = h.Records(count); });
        for (const StateNode &r : records.items) out.push_back(line_text(r));
        return out;
    }
    void set_recording(bool on) override
    {
        bool done = false;
        m_emu->GetContext()->histories.With(m_device, [&done, on](history::IDeviceHistory &h) {
            std::string error;
            done = h.Configure(on ? history::Config{.mode = history::Mode::Ring} : history::Config{}, error);
        });
        if (!done) return;
        if (on) m_ours->insert(m_device);
        else m_ours->erase(m_device);
    }

private:
    ::Emulator * m_emu;
    std::string m_device;
    std::set<std::string> * m_ours;
};

// A core device report (DeviceState) of the device catalog: the Devices tab's entry (title, summary, the report as a
// tree) and, for scripts, ecat device fields (the leaves, dotted paths as names)
class ReportDevice : public ecatshim::ComputerDevice
{
public:
    typedef std::function<StateNode(EmulatorContext *)> Builder;
    ReportDevice(::Emulator * emu, const DeviceState::ReportEntry &entry) : m_emu(emu), m_entry(entry), m_build(entry.build)
    {
        name = entry.id;
        type = "report";
    }
    bool get_tree(bool deep, ecatshim::DeviceNode &out) override
    {
        StateNode n;
        ::Emulator * emu = m_emu;
        Builder &build = m_build;
        at_coherent_moment(emu, [&n, emu, &build]() { n = build(emu->GetContext()); });
        out = ecatshim::DeviceNode{m_entry.title, m_entry.summary ? m_entry.summary(n) : DeviceState::ReportSummary(n), {}};
        if (!deep) return true;
        ecatshim::DeviceNode parts;
        to_tree(n, parts);
        for (ecatshim::DeviceNode &c : parts.children)
            if (c.name != "available") out.children.push_back(std::move(c));
        return true;
    }
    std::vector<DeviceFieldInfo> get_device_fields() override
    {
        // The report is built once per refresh, where the machine stands still
        m_values.clear();
        StateNode n;
        ::Emulator * emu = m_emu;
        Builder &build = m_build;
        at_coherent_moment(emu, [&n, emu, &build]() { n = build(emu->GetContext()); });
        flatten(n, "");
        std::vector<DeviceFieldInfo> r;
        for (const auto &v : m_values) r.push_back(DeviceFieldInfo{v.first, false});
        return r;
    }
    bool get_field(const std::string &field, unsigned int, unsigned int, DeviceFieldValue &out) override
    {
        for (const auto &v : m_values)
            if (v.first == field) {
                out.numeric = false;
                out.text = v.second;
                return true;
            }
        return false;
    }

private:
    ::Emulator * m_emu;
    const DeviceState::ReportEntry &m_entry;
    Builder m_build;
    std::vector<std::pair<std::string, std::string>> m_values;

    void flatten(const StateNode &n, const std::string &path)
    {
        if (m_values.size() > 400) return;
        switch (n.kind) {
        case StateNode::Kind::Object:
            for (const auto &m : n.members) flatten(m.second, path.empty() ? m.first : path + "." + m.first);
            break;
        case StateNode::Kind::Array:
            for (size_t i = 0; i < n.items.size() && i < 32; i++) flatten(n.items[i], path + "[" + std::to_string(i) + "]");
            break;
        case StateNode::Kind::Bool: m_values.emplace_back(path, n.b ? "true" : "false"); break;
        case StateNode::Kind::Int: m_values.emplace_back(path, std::to_string(n.i)); break;
        case StateNode::Kind::Double: m_values.emplace_back(path, std::to_string(n.d)); break;
        case StateNode::Kind::String: m_values.emplace_back(path, n.s); break;
        default: m_values.emplace_back(path, "null"); break;
        }
    }
};

//------------------------------------------------------------------- processors

std::vector<CpuRegister> z80_registers(const Z80Registers &r)
{
    auto reg = [](const char * n, unsigned int v, unsigned int bits) { return CpuRegister{n, v, bits}; };
    const CpuRegister gap;
    return {
        reg("AF", r.af, 16), reg("BC", r.bc, 16), reg("DE", r.de, 16), reg("HL", r.hl, 16), gap,
        reg("AF'", r.alt.af, 16), reg("BC'", r.alt.bc, 16), reg("DE'", r.alt.de, 16), reg("HL'", r.alt.hl, 16), gap,
        reg("IX", r.ix, 16), reg("IY", r.iy, 16), gap,
        reg("SP", r.sp, 16), reg("PC", r.pc, 16), gap,
        reg("I", r.i, 8), reg("R", Z80::RegisterR(&r), 8), reg("IM", r.im, 8),
        reg("IFF1", r.iff1, 8), reg("IFF2", r.iff2, 8),
    };
}

const char * const kFlagNames[8] = {"S", "Z", "5", "H", "3", "P", "N", "C"};

std::vector<std::pair<std::string, std::string>> z80_flags(uint8_t f)
{
    std::vector<std::pair<std::string, std::string>> r;
    for (int i = 0; i < 8; i++) r.emplace_back(kFlagNames[i], ((f >> (7 - i)) & 1) ? "1" : "0");
    return r;
}

int flag_bit(const std::string &name)
{
    for (int i = 0; i < 8; i++)
        if (name == kFlagNames[i]) return 7 - i;
    return -1;
}

// Execution breakpoints of one CPU, kept in its BreakpointManager (the main one or the card's)
class Z80CpuBase : public ecatshim::CPU
{
public:
    explicit Z80CpuBase(::Emulator * emu) : m_emu(emu) {}

    bool stack_address(int i, unsigned int &address) override
    {
        address = (sp() + 2 * i) & 0xFFFF;
        return true;
    }

    bool has_breakpoint(unsigned int address) override { return find_breakpoint(address) != BRK_INVALID; }
    void add_breakpoint(unsigned int address) override
    {
        if (has_breakpoint(address)) return;
        BreakpointManager * bm = breakpoints();
        if (bm == nullptr) return;
        // The manager keeps one breakpoint per address: a run-to there would take this one's place and take it
        // away at the next stop. The user's stops there all the same
        for (const auto &b : bm->GetAllBreakpoints())
            if (b.second->owner == kRunToOwner && b.second->z80address == (address & 0xFFFF))
                bm->RemoveBreakpointByID(b.first);
        BreakpointSpec spec;
        spec.type = BRK_MEMORY;
        spec.access = BRK_MEM_EXECUTE;
        spec.address = (uint16_t)address;
        std::string error;
        bm->AddBreakpoint(spec, error);
    }
    void remove_breakpoint(unsigned int address) override
    {
        BreakpointManager * bm = breakpoints();
        for (uint16_t id = find_breakpoint(address); bm != nullptr && id != BRK_INVALID; id = find_breakpoint(address))
            bm->RemoveBreakpointByID(id);
    }
    void clear_breakpoints() override
    {
        for (unsigned int a : get_breakpoints()) remove_breakpoint(a);
    }
    std::vector<unsigned int> get_breakpoints() override
    {
        std::vector<unsigned int> r;
        BreakpointManager * bm = breakpoints();
        if (bm == nullptr) return r;
        for (const auto &b : bm->GetAllBreakpoints()) {
            const BreakpointDescriptor &d = *b.second;
            if (is_user_execution(d) && std::find(r.begin(), r.end(), d.z80address) == r.end()) r.push_back(d.z80address);
        }
        std::sort(r.begin(), r.end());
        return r;
    }
    void add_run_to(unsigned int address)
    {
        BreakpointManager * bm = breakpoints();
        if (bm == nullptr) return;
        BreakpointSpec spec;
        spec.type = BRK_MEMORY;
        spec.access = BRK_MEM_EXECUTE;
        spec.address = (uint16_t)address;
        spec.owner = kRunToOwner;
        spec.note = "run to the cursor";
        std::string error;
        bm->AddBreakpoint(spec, error);
    }
    void clear_run_to()
    {
        BreakpointManager * bm = breakpoints();
        if (bm == nullptr) return;
        for (const auto &b : bm->GetAllBreakpoints())
            if (b.second->owner == kRunToOwner) bm->RemoveBreakpointByID(b.first);
    }

    virtual BreakpointManager * breakpoints() = 0;
    virtual unsigned int sp() = 0;

protected:
    ::Emulator * m_emu;

    static bool is_user_execution(const BreakpointDescriptor &d)
    {
        return d.type == BRK_MEMORY && (d.memoryType & BRK_MEM_EXECUTE) && !d.hidden && !d.isRange &&
               d.owner != kRunToOwner;
    }
    uint16_t find_breakpoint(unsigned int address)
    {
        BreakpointManager * bm = breakpoints();
        if (bm == nullptr) return BRK_INVALID;
        for (const auto &b : bm->GetAllBreakpoints())
            if (is_user_execution(*b.second) && b.second->z80address == (address & 0xFFFF)) return b.first;
        return BRK_INVALID;
    }
};

class MainZ80 : public Z80CpuBase
{
public:
    explicit MainZ80(::Emulator * emu) : Z80CpuBase(emu)
    {
        name = "cpu";
        type = "Z80";
    }
    unsigned int get_pc() override { return m_emu->GetZ80State()->pc; }
    unsigned int sp() override { return m_emu->GetZ80State()->sp; }
    unsigned int peek_mem(unsigned int a) override { return m_emu->GetMemory()->DirectReadFromZ80Memory((uint16_t)a); }
    bool poke_mem(unsigned int a, unsigned int v) override
    {
        ::Emulator * emu = m_emu;
        return emu->EditMemoryFromTool(kSource, [emu, a, v]() {
                   emu->GetMemory()->DirectWriteToZ80Memory((uint16_t)a, (uint8_t)v);
               }).empty();
    }
    ecatshim::DisAsm * create_disasm(const std::string &, emulator::Result &) override
    {
        return new Z80DisAsm(m_emu->GetContext(), true);
    }
    std::vector<CpuRegister> get_register_values() override { return z80_registers(*m_emu->GetZ80State()); }
    std::vector<std::pair<std::string, std::string>> get_flags() override { return z80_flags(m_emu->GetZ80State()->f); }
    void set_context_value(const std::string &n, unsigned int v) override
    {
        m_emu->SetRegisterFromTool(kSource, n, (uint16_t)v);
    }
    bool set_flag(const std::string &n, bool value) override
    {
        const int bit = flag_bit(n);
        if (bit < 0) return false;
        uint8_t f = m_emu->GetZ80State()->f;
        f = value ? (uint8_t)(f | (1 << bit)) : (uint8_t)(f & ~(1 << bit));
        return m_emu->SetRegisterFromTool(kSource, "F", f);
    }
    std::vector<unsigned int> get_history() override
    {
        // Oldest first, as ecat keeps it: the execution history `cpu`, a snapshot (the machine runs on)
        std::vector<unsigned int> r;
        m_emu->GetContext()->histories.With("cpu", [&r](history::IDeviceHistory &h) {
            if (auto * exec = dynamic_cast<history::DeviceHistory<ExecRecord> *>(&h))
                for (const ExecRecord &e : exec->Snapshot(256)) r.push_back(e.pc);
        });
        return r;
    }
    BreakpointManager * breakpoints() override { return m_emu->GetBreakpointManager(); }

    void set_pages(ecatshim::AddressableDevice * ram, ecatshim::AddressableDevice * rom, const ZxPaging &paging)
    {
        m_ram = ram;
        m_rom = rom;
        m_paging = paging;
    }
    std::vector<ecatshim::AddressWindow> get_windows() override
    {
        std::vector<ecatshim::AddressWindow> r;
        ::Memory * m = m_emu->GetMemory();
        //Four 16 KB windows; the Next's MMU has eight 8 KB slots, each its own page
        const unsigned int slot = 1u << m->MapZ80AddressToPhysicalPage(0).slotShift;
        for (unsigned int start = 0; start < 0x10000; start += slot) {
            ecatshim::AddressWindow x;
            const uint8_t w = (uint8_t)(start / PAGE_SIZE);
            x.start = start;
            x.size = slot;
            x.switchable = m_paging.switchable[w];
            const MemoryPageDescriptor d = m->MapZ80AddressToPhysicalPage((uint16_t)x.start);
            //The 16 KB banks' write map is not the Next's: there a ROM slot is the read-only one
            x.writable = slot == PAGE_SIZE ? m->IsWindowWritable(w) : d.mode != BANK_ROM;
            x.offset = (unsigned int)d.page * PAGE_SIZE + d.addressInPage;
            //A page in the slot's size: the Next's 8 KB pages as its MMU numbers them
            x.page = x.offset / slot;
            if (d.mode == BANK_ROM) {
                x.kind = "ROM";
                x.device = m_rom;
                if (auto * pages = dynamic_cast<ecatshim::Memory *>(m_rom)) {
                    x.name = pages->page_title(d.page);
                    x.role = pages->page_role(d.page);
                }
            } else if (d.mode == BANK_RAM) {
                x.kind = "RAM";
                x.device = m_ram;
                x.name = ZxRamNote(m_paging, d.page);
            } else if (d.mode == BANK_CACHE) {
                x.kind = "cache";
            }
            r.push_back(x);
        }
        return r;
    }

private:
    ecatshim::AddressableDevice * m_ram = nullptr;
    ecatshim::AddressableDevice * m_rom = nullptr;
    ZxPaging m_paging;
};

// A sound card's Z80: the General Sound's ("gs") or the GS of a MultiSound card ("ms"). Its registers and memory
// come through the card's debug target; a step of this tab is the card's step (RunControl::CardStep: the machine
// runs until the card's step ends)
class CardZ80 : public Z80CpuBase
{
public:
    CardZ80(::Emulator * emu, DebugCpuId id) : Z80CpuBase(emu), m_id(id)
    {
        name = DebugCpuName(id);
        type = (id == DebugCpuId::GS) ? "Z80 (General Sound)" : "Z80 (MultiSound GS)";
    }
    DebugCpuId id() const { return m_id; }
    GSCardTarget * target()
    {
        DebugManager * dm = m_emu->GetDebugManager();
        GSCardTarget * t = dm ? dm->GetCardTarget(m_id) : nullptr;
        return (t != nullptr && t->Available()) ? t : nullptr;
    }
    Z80Registers regs()
    {
        Z80Registers r{};
        if (GSCardTarget * t = target()) t->Registers(r);
        return r;
    }
    unsigned int get_pc() override { return regs().pc; }
    unsigned int sp() override { return regs().sp; }
    unsigned int peek_mem(unsigned int a) override
    {
        uint8_t v = 0xFF;
        if (GSCardTarget * t = target()) t->Peek((uint16_t)a, v);
        return v;
    }
    std::vector<unsigned int> get_history() override
    {
        // Oldest first, as ecat keeps it: the card's execution history (`<slot>.cpu`), a snapshot (the card runs on).
        // Off it comes on the first time it is looked at (it costs the card's idle sleep its records), off again when
        // the debugger goes
        std::vector<unsigned int> r;
        GSCardTarget * t = target();
        history::DeviceHistory<ExecRecord> * h = t ? t->Card()->execHistory() : nullptr;
        if (h == nullptr) return r;
        if (h->GetStats().config.mode == history::Mode::Off) {
            std::string error;
            if (!h->Configure(history::Config{.mode = history::Mode::Ring}, error)) return r;
            m_history_ours = h->Device();
        }
        for (const ExecRecord &e : h->Snapshot(256)) r.push_back(e.pc);
        return r;
    }
    // The history this CPU switched on ("" none): the machine turns it off when it goes
    const std::string &history_ours() const { return m_history_ours; }

    bool poke_mem(unsigned int a, unsigned int v) override
    {
        bool done = false;
        CardZ80 * self = this;
        at_coherent_moment(m_emu, [self, a, v, &done]() {
            if (GSCardTarget * t = self->target()) done = t->Card()->pokeCardMemory((uint16_t)a, (uint8_t)v);
        });
        return done;
    }
    ecatshim::DisAsm * create_disasm(const std::string &, emulator::Result &) override
    {
        return new Z80DisAsm(m_emu->GetContext(), false);
    }
    std::vector<CpuRegister> get_register_values() override { return z80_registers(regs()); }
    std::vector<std::pair<std::string, std::string>> get_flags() override { return z80_flags(regs().f); }
    void set_context_value(const std::string &n, unsigned int v) override { set_card_register(n, v); }
    bool set_flag(const std::string &n, bool value) override
    {
        const int bit = flag_bit(n);
        if (bit < 0) return false;
        uint8_t f = regs().f;
        f = value ? (uint8_t)(f | (1 << bit)) : (uint8_t)(f & ~(1 << bit));
        return set_card_register("F", f);
    }
    BreakpointManager * breakpoints() override
    {
        DebugManager * dm = m_emu->GetDebugManager();
        return dm ? dm->GetBreakpointsManager(m_id) : nullptr;
    }

    // The card's RAM and ROM (flash on the NeoGS) as paged memories, when the machine lists them
    void set_ram(ecatshim::AddressableDevice * ram) { m_ram = ram; }
    void set_rom(ecatshim::AddressableDevice * rom) { m_rom = rom; }
    // The prefix of the card's device memories
    std::string region_prefix()
    {
        if (m_id == DebugCpuId::MultiSoundGS) return "multisound.gs";
        GSCardTarget * t = target();
        return (t != nullptr && t->Card()->implementation() == GSCardImplementation::NGS) ? "neogs" : "gs";
    }
    // The classic card maps ROM at #0000 and RAM page 0 at #4000 for good, its #00 port pages #8000-#FFFF; the
    // NeoGS also puts RAM at #0000 (GSCFG0.NOROM) and pages #C000 on its own (MPAGEX)
    std::vector<ecatshim::AddressWindow> get_windows() override
    {
        std::vector<ecatshim::AddressWindow> r;
        GSCardTarget * t = target();
        if (t == nullptr) return r;
        const bool neo = t->Card()->implementation() == GSCardImplementation::NGS;
        for (unsigned int w = 0; w < 4; w++) {
            ecatshim::AddressWindow x;
            x.start = w * PAGE_SIZE;
            x.size = PAGE_SIZE;
            x.switchable = (w >= 2) || (w == 0 && neo);
            const MemoryPageDescriptor d = t->MapAddress((uint16_t)x.start);
            x.page = d.page;
            x.offset = (unsigned int)d.page * PAGE_SIZE;
            if (d.mode == BANK_ROM) {
                x.kind = neo ? "flash" : "ROM";
                x.device = m_rom;
                x.writable = false;
            } else if (d.mode == BANK_RAM) {
                x.kind = "RAM";
                x.device = m_ram;
            }
            r.push_back(x);
        }
        return r;
    }

private:
    DebugCpuId m_id;
    std::string m_history_ours;
    ecatshim::AddressableDevice * m_ram = nullptr;
    ecatshim::AddressableDevice * m_rom = nullptr;

    bool set_card_register(const std::string &n, unsigned int v)
    {
        bool done = false;
        CardZ80 * self = this;
        at_coherent_moment(m_emu, [self, n, v, &done]() {
            GSCardTarget * t = self->target();
            Z80CpuRegisters r{};
            if (t == nullptr || !t->Card()->cardRegisters(r)) return;
            done = GSCardTarget::SetRegister(r, n, (uint16_t)v) && t->Card()->setCardRegisters(r);
        });
        return done;
    }
};

//------------------------------------------------------------------- the machine

class UnrealMachine : public ecatshim::Emulator
{
public:
    explicit UnrealMachine(std::shared_ptr<::Emulator> emu) : m_emu(std::move(emu)), m_settings("unreal-ng", "unreal-debugger")
    {
        ::Emulator * e = m_emu.get();
        EmulatorContext * ctx = e->GetContext();

        m_main = std::make_shared<MainZ80>(e);
        m_dm.add(m_main);
        // A processor tab for each sound card's Z80 the machine has
        for (DebugCpuId id : {DebugCpuId::GS, DebugCpuId::MultiSoundGS}) {
            GSCardTarget * t = ctx->pDebugManager ? ctx->pDebugManager->GetCardTarget(id) : nullptr;
            if (t == nullptr || !t->Available()) continue;
            m_cards.push_back(std::make_shared<CardZ80>(e, id));
            m_dm.add(m_cards.back());
        }

        // The ROM pages the machine loaded (16 KB each): 1 on the 48K, 32 on the TS-Conf
        auto ram = std::make_shared<PageMemory>(e, BANK_RAM, MemorySearch::RamPageCount(ctx), "ram");
        const ROM * romImage = ctx->pCore ? ctx->pCore->GetROM() : nullptr;
        const uint32_t romPages = (romImage && romImage->GetROMBanksLoaded()) ? romImage->GetROMBanksLoaded() : 4;
        auto rom = std::make_shared<PageMemory>(e, BANK_ROM, std::min<uint32_t>(romPages, MAX_ROM_PAGES), "rom");
        // The page map names the owner of each byte in the CPU's tab; as a tab of its own it would repeat that tab
        m_mapper = std::make_shared<ZxMapper>(e, ram.get(), rom.get());
        m_main->mm = m_mapper.get();
        const ZxPaging paging = ZxModelPaging(Config::GetModelFullName(ctx->config.mem_model));
        m_main->set_pages(ram.get(), rom.get(), paging);
        ram->set_paging(paging);
        m_dm.add(ram);
        m_dm.add(rom);
        // A tab for what a processor runs or a screen shows (a card's RAM, video and fast RAM); the rest, firmware
        // flash, EEPROM, CMOS, palettes and sprite tables, in the "+" menu
        for (IDeviceMemoryRegion * r : DeviceMemory::Regions(ctx)) {
            const std::string n = r->Name();
            const std::string kind = n.substr(n.rfind('.') + 1);
            const std::string owner = n.substr(0, n.rfind('.'));
            // A sound card's RAM and ROM: its Z80's pages ("gs.ram", "neogs.flash", "multisound.gs.ram")
            CardZ80 * card = nullptr;
            for (const auto &c : m_cards)
                if (owner == c->region_prefix()) card = c.get();
            if (card != nullptr && (kind == "ram" || kind == "rom" || kind == "flash")) {
                auto pages = std::make_shared<CardPages>(e, r);
                if (kind == "ram") card->set_ram(pages.get());
                else card->set_rom(pages.get());
                m_dm.add(pages);
                continue;
            }
            if (kind == "ram" || kind == "vram" || kind == "fastram")
                m_dm.add(std::make_shared<RegionMemory<ecatshim::Memory>>(e, r));
            else
                m_dm.add(std::make_shared<RegionMemory<ecatshim::AddressableDevice>>(e, r));
        }

        // The reports this machine has, as the device catalog lists them (its cards are fixed: a slot change makes a
        // new machine)
        StateNode catalog;
        at_coherent_moment(e, [&catalog, ctx]() { catalog = DeviceState::Catalog(ctx); });
        std::set<std::string> present;
        if (const StateNode * reports = catalog.find("reports"))
            for (const StateNode &r : reports->items) present.insert(r.find("id")->s);
        for (const DeviceState::ReportEntry &entry : DeviceState::CatalogEntries())
            if (present.count(entry.id)) m_dm.add(std::make_shared<ReportDevice>(e, entry));
        // Every device's history for the History tab; a processor's ("cpu", "zxbus.1.cpu") comes through its CPU
        const StateNode histories = ctx->histories.List();
        for (const StateNode &h : histories.items) {
            const StateNode * device = h.find("device");
            if (device == nullptr || device->s == "cpu" ||
                (device->s.size() > 4 && device->s.compare(device->s.size() - 4, 4, ".cpu") == 0))
                continue;
            m_dm.add(std::make_shared<HistoryDevice>(e, device->s, &m_histories_ours));
        }

        dm = &m_dm;
        loaded = true;
        data_path = QDir::currentPath().toStdString();

        // The core's events come on the message center's thread; the window's listener posts itself
        MessageCenter &mc = MessageCenter::DefaultMessageCenter();
        const unreal::UUID id = m_emu->GetUUID();
        auto on_event = [this, id](int, Message * message) {
            if (message != nullptr && message->obj != nullptr) {
                if (auto * s = dynamic_cast<EmulatorStateChangePayload *>(message->obj)) {
                    if (!(s->emulatorId == id)) return;
                } else if (auto * b = dynamic_cast<BreakpointTriggeredPayload *>(message->obj)) {
                    if (!(b->emulatorId == id)) return;
                } else if (auto * c = dynamic_cast<CpuStepPayload *>(message->obj)) {
                    if (!(c->emulatorId == id)) return;
                }
            }
            on_core_event();
        };
        for (const char * topic : {NC_EMULATOR_STATE_CHANGE, NC_EXECUTION_CPU_STEP, NC_EXECUTION_BREAKPOINT})
            m_observers.emplace_back(topic, mc.AddObserver(topic, ObserverCallbackFunc(on_event)));
    }

    ~UnrealMachine() override
    {
        MessageCenter &mc = MessageCenter::DefaultMessageCenter();
        for (const auto &o : m_observers) mc.RemoveObserverById(o.first, o.second);
        set_debug_listener(nullptr);
        // The histories the debugger switched on, off again
        std::set<std::string> ours = m_histories_ours;
        for (const auto &card : m_cards)
            if (!card->history_ours().empty()) ours.insert(card->history_ours());
        for (const std::string &device : ours)
            m_emu->GetContext()->histories.With(device, [](history::IDeviceHistory &h) {
                std::string error;
                h.Configure(history::Config{}, error);
            });
        EmulatorManager::GetInstance()->RemoveEmulator(m_emu->GetId());
    }

    ::Emulator * core() const { return m_emu.get(); }

    void debug_attach(bool on) override
    {
        if (!on) return;
        m_emu->DebugOn();
        // The execution history `cpu` on while the debugger is (a ring), unless the user has it some way already
        m_emu->GetContext()->histories.With("cpu", [this](history::IDeviceHistory &h) {
            if (h.GetStats().config.mode != history::Mode::Off) return;
            std::string error;
            if (h.Configure(history::Config{.mode = history::Mode::Ring}, error)) m_histories_ours.insert("cpu");
        });
        m_main->m_debug = ecatshim::DEBUG_BRAKES;
    }
    void debug_detach() override
    {
        clear_run_to();
        if (m_emu->IsPaused()) m_emu->Resume(kGui);
    }
    bool debug_paused() const override { return m_emu->IsPaused(); }
    bool debug_stepping() const override { return m_emu->IsDirectStepping(); }
    void debug_pause() override
    {
        m_emu->DebugOn();
        m_emu->Pause(kGui, "debugger");
    }
    void debug_run(bool breakpoints) override
    {
        // A run-to of an earlier command is over whatever happened since: a new command never meets it (on a CPU in
        // HALT at its address it would stop the run at once)
        if (!m_arming_run_to) clear_run_to();
        // Breakpoints only fire in debug mode: a run without them is a run with it off
        if (breakpoints) m_emu->DebugOn();
        else m_emu->DebugOff();
        for (ecatshim::CPU * c : m_dm.get_cpus()) c->m_debug = breakpoints ? ecatshim::DEBUG_BRAKES : ecatshim::DEBUG_OFF;
        if (!m_emu->IsPaused()) return;
        const execution::Result r = m_emu->Resume(kGui);
        if (r.outcome != execution::Outcome::Applied && r.outcome != execution::Outcome::Coalesced)
            fprintf(stderr, "ecat-debugger: resume not applied (outcome %d): %s\n", (int)r.outcome, r.detail.c_str());
    }
    // A step of a card's tab is the card's step (RunControl::CardStep): the machine runs until it ends
    void debug_step(ecatshim::CPU * c, unsigned int count) override
    {
        clear_run_to();
        if (card_step(c, "in", count)) return;
        for (unsigned int i = 0; i < count; i++) m_emu->RunSingleCPUCycle(true);
    }
    void debug_step_over(ecatshim::CPU * c) override
    {
        clear_run_to();
        if (card_step(c, "over", 1)) return;
        m_emu->StepOver();
    }
    void debug_step_out(ecatshim::CPU * c) override
    {
        clear_run_to();
        if (card_step(c, "out", 1)) return;
        m_emu->StepOut();
    }
    void debug_run_to(ecatshim::CPU * c, unsigned int address) override
    {
        // A breakpoint of its own that goes away at the next stop, whatever stops the machine: a stop after this run
        // starts (the run start and the stop each move the debug seq), not the stop it was set at, whose event may
        // come only now
        clear_run_to();
        m_run_to_seq = m_emu->DebugSeq();
        if (auto * z = dynamic_cast<Z80CpuBase *>(c)) z->add_run_to(address);
        m_arming_run_to = true;
        debug_run(true);
        m_arming_run_to = false;
    }
    unsigned int debug_serial() const override { return m_serial.load(); }
    ecatshim::CPU * debug_stop_cpu() const override
    {
        const ::Emulator::DebugStop s = m_emu->LastStop();
        if (s.reason == ::Emulator::DebugStop::Reason::Breakpoint || s.reason == ::Emulator::DebugStop::Reason::Step)
            for (const auto &c : m_cards)
                if (s.breakpoint.cpu == c->id()) return c.get();
        return m_main.get();
    }
    int debug_stop_reason() const override
    {
        switch (m_emu->LastStop().reason) {
        case ::Emulator::DebugStop::Reason::Pause:
        case ::Emulator::DebugStop::Reason::Watch: return DSTOP_PAUSE;
        case ::Emulator::DebugStop::Reason::Breakpoint: return DSTOP_BREAKPOINT;
        case ::Emulator::DebugStop::Reason::Step: return DSTOP_STEP;
        default: return DSTOP_NONE;
        }
    }
    void set_debug_listener(const std::function<void()> &fn) override
    {
        std::lock_guard<std::mutex> lock(m_listener_mutex);
        m_listener = fn;
    }

    std::string read_setup(const std::string &section, const std::string &key, const std::string &def) override
    {
        return m_settings.value(QString::fromStdString(section + "/" + key), QString::fromStdString(def)).toString().toStdString();
    }
    void write_setup(const std::string &section, const std::string &key, const std::string &value) override
    {
        m_settings.setValue(QString::fromStdString(section + "/" + key), QString::fromStdString(value));
    }
    std::string get_last_path() override { return read_setup("Paths", "last", QDir::homePath().toStdString()); }
    void set_last_path(const std::string &path) override { write_setup("Paths", "last", path); }
    std::string translate_char(unsigned int code) override
    {
        if (code == 0x7F) return "\xC2\xA9";
        if (code >= 0x20 && code < 0x7F) return std::string(1, (char)code);
        return ".";
    }
    int char_code(const std::string &text) override
    {
        if (text == "\xC2\xA9") return 0x7F;
        return (text.size() == 1 && (unsigned char)text[0] < 0x80) ? (unsigned char)text[0] : -1;
    }

private:
    std::shared_ptr<::Emulator> m_emu;
    ecatshim::DeviceManager m_dm;
    std::shared_ptr<MainZ80> m_main;
    std::vector<std::shared_ptr<CardZ80>> m_cards;
    std::shared_ptr<ZxMapper> m_mapper;
    QSettings m_settings;
    std::vector<std::pair<std::string, uint64_t>> m_observers;
    std::set<std::string> m_histories_ours;  // the histories the debugger turned on: off again when it goes
    std::mutex m_listener_mutex;
    std::function<void()> m_listener;
    std::atomic<unsigned int> m_serial{0};
    std::atomic<uint64_t> m_run_to_seq{0};
    bool m_arming_run_to = false;

    void clear_run_to()
    {
        m_main->clear_run_to();
        for (const auto &c : m_cards) c->clear_run_to();
    }
    // A card's step when the tab is a card's: false for the main CPU's tab
    bool card_step(ecatshim::CPU * c, const char * kind, unsigned int count)
    {
        auto * card = dynamic_cast<CardZ80 *>(c);
        if (card == nullptr) return false;
        const execution::RunReply r = execution::RunControl(m_emu).CardStep(kGui, card->id(), kind, std::max(1u, count));
        if (!r.Ok()) fprintf(stderr, "ecat-debugger: card step refused: %s\n", r.message.c_str());
        return true;
    }

    // Time travel in process: the verb on the machine's context (the views follow through the window, which reads
    // them again after a move)
    bool ttd_supported() const override { return true; }
    TtdAnswer ttd_call(const std::string &verb, const std::map<std::string, std::string> &options) override
    {
        ttd::TTDReply reply;
        {
            const ::Emulator::ContextLease lease = m_emu->LeaseContext();
            EmulatorContext * context = lease.get();
            if (context == nullptr || !ttd::HasTimeTravelSession(context))
                return {false, R"({"error":"Not Implemented","message":"This machine has no time travel"})"};
            reply = ttd::TTDControl(context).Execute({verb, options});
        }
        return {reply.Ok(), StateNodeToJsonText(reply.ToValue())};
    }

    void on_core_event()
    {
        m_serial++;
        if (m_emu->IsPaused() && m_emu->DebugSeq() >= m_run_to_seq + 2) clear_run_to();
        std::lock_guard<std::mutex> lock(m_listener_mutex);
        if (m_listener) m_listener();
    }
};

} // namespace

std::unique_ptr<ecatshim::Emulator> CreateUnrealMachine(const MachineOptions &options, std::string &error)
{
    std::function<void(CONFIG &)> override;
    if (options.card == "multisound") {
        // ZX-MultiSound with its GS part on: the card's own Z80
        if (!SlotControl::CreateOverride({{"zxbus.1", "multisound"}, {"zxbus.1.dip", "ym,saa,gs"}}, override, error))
            return nullptr;
    } else if (!options.card.empty() && !SlotControl::CreateOverride({{"zxbus.1", options.card}}, override, error)) {
        return nullptr;
    }

    EmulatorManager * manager = EmulatorManager::GetInstance();
    std::shared_ptr<::Emulator> emu = manager->CreateEmulatorWithModel("unreal-debugger", options.model, LoggerLevel::LogError, &error, override);
    if (!emu) return nullptr;
    emu->DebugOff();
    emu->GetLogger()->TurnOffLoggingForAll();

    if (!options.file.empty()) {
        const std::string &f = options.file;
        std::string ext = f.substr(f.find_last_of('.') + 1);
        for (char &c : ext) c = (char)tolower((unsigned char)c);
        bool ok = true;
        if (ext == "sna" || ext == "z80" || ext == "szx") ok = emu->LoadSnapshot(f);
        else if (ext == "tap" || ext == "tzx") ok = emu->LoadTape(f, &error);
        else if (ext == "trd" || ext == "scl" || ext == "fdi" || ext == "udi") ok = emu->AutostartDisk(f, 0).mounted;
        if (!ok) {
            if (error.empty()) error = "cannot load " + f;
            manager->RemoveEmulator(emu->GetId());
            return nullptr;
        }
    }
    manager->StartEmulatorAsync(emu->GetId());
    return std::unique_ptr<ecatshim::Emulator>(new UnrealMachine(emu));
}

bool UnrealMachineScreen(ecatshim::Emulator * e, std::vector<uint32_t> &pixels, int &width, int &height)
{
    auto * m = dynamic_cast<UnrealMachine *>(e);
    if (m == nullptr) return false;
    const FramebufferDescriptor fb = m->core()->GetFramebuffer();
    if (fb.memoryBuffer == nullptr || fb.width == 0 || fb.memoryBufferSize < (size_t)fb.width * fb.height * 4) return false;
    width = fb.width;
    height = fb.height;
    pixels.resize((size_t)width * height);
    // RGBA bytes in memory to 0xAARRGGBB
    const uint8_t * p = fb.memoryBuffer;
    for (size_t i = 0; i < pixels.size(); i++, p += 4)
        pixels[i] = 0xFF000000u | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    return true;
}

uint64_t UnrealMachineFrames(ecatshim::Emulator * e)
{
    auto * m = dynamic_cast<UnrealMachine *>(e);
    return m ? m->core()->GetContext()->emulatorState.frame_counter : 0;
}

ecatshim::DisAsm * CreatePlainZ80DisAsm()
{
    return new Z80DisAsm(nullptr, false);
}

std::string UnrealMachineId(ecatshim::Emulator * e)
{
    auto * m = dynamic_cast<UnrealMachine *>(e);
    return m ? m->core()->GetId() : std::string();
}

void UnrealMachineAction(ecatshim::Emulator * e, const std::string &action, const std::string &params, QObject * context,
                         MachineActionDone done)
{
    auto * m = dynamic_cast<UnrealMachine *>(e);
    actions::ActionValue value;
    std::string error;
    if (m == nullptr || !StateNodeFromJsonText(params.empty() ? std::string("{}") : params, value.params, &error)) {
        done(MachineActionResult{false, m ? "Bad Request" : "not_now", m ? error : "no machine", "{}"});
        return;
    }
    actions::ActionOrigin origin;
    origin.origin = execution::Origin::Gui;  // as the debugger's own buttons
    origin.identity = "gui-control|unreal-debugger";
    origin.client = "unreal-debugger";
    // The registry answers on the machine's actions worker: the answer goes over to the GUI thread, where the
    // context's life is known
    const QPointer<QObject> alive(context);
    actions::ActionRegistry::Instance().Submit(
        action, std::move(value), m->core()->GetId(), std::move(origin), [alive, done](actions::ActionResult result) {
            MachineActionResult r{result.ok, result.error, result.message, StateNodeToJsonText(result.body)};
            QMetaObject::invokeMethod(
                QCoreApplication::instance(), [alive, done, r]() {
                    if (alive) done(r);
                },
                Qt::QueuedConnection);
        });
}
