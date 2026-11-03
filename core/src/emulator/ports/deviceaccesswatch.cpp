#include "deviceaccesswatch.h"

#include <algorithm>
#include <cstdio>

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portclaimindex.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/portowners.h"

namespace
{
/// The length of the instruction at @p address (the documented and undocumented opcodes; DD / FD take the HL forms'
/// lengths plus the displacement of an (IX+d) operand)
unsigned InstructionLength(AccessCodeReader read, const void* context, uint16_t address)
{
    const uint8_t op = read(context, address);
    auto plain = [](uint8_t code) -> unsigned {
        switch (code)
        {
            case 0x01: case 0x11: case 0x21: case 0x31:  // LD rr,nn
            case 0x22: case 0x2A: case 0x32: case 0x3A:  // LD (nn),HL / A and back
            case 0xC2: case 0xC3: case 0xCA: case 0xD2: case 0xDA: case 0xE2: case 0xEA: case 0xF2: case 0xFA:
            case 0xC4: case 0xCC: case 0xCD: case 0xD4: case 0xDC: case 0xE4: case 0xEC: case 0xF4: case 0xFC:
                return 3;
            case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36: case 0x3E:  // LD r,n
            case 0x10: case 0x18: case 0x20: case 0x28: case 0x30: case 0x38:                        // DJNZ, JR
            case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:  // ALU n
            case 0xD3: case 0xDB: case 0xCB:
                return 2;
            default:
                return 1;
        }
    };
    if (op == 0xED)
    {
        const uint8_t next = read(context, static_cast<uint16_t>(address + 1));
        return (next & 0xC7) == 0x43 ? 4 : 2;  // LD (nn),rr / LD rr,(nn)
    }
    if (op == 0xDD || op == 0xFD)
    {
        const uint8_t next = read(context, static_cast<uint16_t>(address + 1));
        if (next == 0xCB)
            return 4;
        if (next == 0xDD || next == 0xFD || next == 0xED)
            return 1;  // a prefix that does nothing: the next one starts over
        const bool indexed = next == 0x34 || next == 0x35 || next == 0x36 ||
                             (next >= 0x40 && next < 0xC0 && next != 0x76 &&
                              ((next & 0x07) == 0x06 || (next & 0xF8) == 0x70));
        return 1 + plain(next) + (indexed ? 1u : 0u);
    }
    return plain(op);
}
}  // namespace

uint8_t TestedBitsAfterIn(AccessCodeReader read, const void* context, uint16_t next)
{
    constexpr unsigned kA = 7;
    constexpr unsigned kNone = 8;
    // The register the IN loaded: IN A,(n) is DB n; IN r,(C) is ED 40 + 8r (r 6 is IN F,(C): flags only)
    unsigned target = kNone;
    if (read(context, static_cast<uint16_t>(next - 2)) == 0xDB)
        target = kA;
    else if (read(context, static_cast<uint16_t>(next - 2)) == 0xED)
    {
        const uint8_t op = read(context, static_cast<uint16_t>(next - 1));
        if ((op & 0xC7) == 0x40 && ((op >> 3) & 7) != 6)
            target = (op >> 3) & 7;
    }
    if (target == kNone)
        return 0;

    uint8_t bits = 0;
    unsigned shift = 0;          // right shifts of the value in A since the IN
    bool carryFromShift = false; // the last instruction shifted bit (shift - 1) into the carry
    unsigned held = target;      // the register that holds the value (A after LD A,r)
    uint16_t address = next;
    for (unsigned count = 0; count < 8; ++count)
    {
        const uint8_t op = read(context, address);
        const uint16_t operand = static_cast<uint16_t>(address + 1);
        const unsigned length = InstructionLength(read, context, address);
        bool shifted = false;
        if (op == 0xE6 && held == kA)  // AND n
            bits |= static_cast<uint8_t>(read(context, operand) << shift);
        else if (op == 0xCB)
        {
            const uint8_t cb = read(context, operand);
            const unsigned reg = cb & 7;
            if ((cb & 0xC0) == 0x40 && reg == held)  // BIT b,r
                bits |= static_cast<uint8_t>(1u << (((cb >> 3) & 7) + (held == kA ? shift : 0)));
            else if (reg == held && held == kA && (cb == 0x0F || cb == 0x1F || cb == 0x3F))  // RRC A, RR A, SRL A
                shifted = true;
            else if (reg == held)
                break;  // another shift or a change of the value: what follows tests something else
        }
        else if ((op == 0x0F || op == 0x1F) && held == kA)  // RRCA, RRA
            shifted = true;
        else if ((op & 0xF8) == 0x78 && (op & 7) == held && held != kA)  // LD A,r: the value goes to A
        {
            held = kA;
            shift = 0;
        }
        else if (((op & 0xC7) == 0x06 && ((op >> 3) & 7) == held) ||
                 (op >= 0x40 && op < 0x80 && op != 0x76 && ((op >> 3) & 7) == held && (op & 7) != held) ||
                 (held == kA && ((op >= 0x80 && op < 0xB8) || op == 0xC6 || op == 0xCE || op == 0xD6 || op == 0xDE ||
                                 op == 0xEE || op == 0xF6)))
            break;  // the register is loaded again or A computed: what follows tests something else
        else if (carryFromShift && (op == 0x30 || op == 0x38 || op == 0xD2 || op == 0xDA || op == 0xD0 ||
                                    op == 0xD8 || op == 0xD4 || op == 0xDC))  // JR / JP / RET / CALL on the carry
            bits |= static_cast<uint8_t>(1u << (shift - 1));
        // The scan follows the fall-through path: it stops where the code surely goes elsewhere, and at another IN
        if (op == 0x18 || op == 0xC3 || op == 0xE9 || op == 0xC9 || op == 0xCD || op == 0x76 || op == 0xDB ||
            (op & 0xC7) == 0xC7)
            break;
        if (op == 0xED)
        {
            const uint8_t ed = read(context, operand);
            if ((ed & 0xC7) == 0x40 || ed == 0x4D || ed == 0x45)
                break;
        }
        if (shifted)
            ++shift;
        carryFromShift = shifted;
        if (shift >= 8)
            break;
        address = static_cast<uint16_t>(address + length);
    }
    return bits;
}

const char* InputRoleName(slots::InputRole role)
{
    switch (role)
    {
        case slots::InputRole::None: return "";
        case slots::InputRole::KeyMatrix: return "key-matrix";
        case slots::InputRole::KempstonJoystick: return "kempston-joystick";
        case slots::InputRole::KempstonJoystick2: return "kempston-joystick-2";
        case slots::InputRole::FullerJoystick: return "fuller-joystick";
        case slots::InputRole::KempstonMouse: return "kempston-mouse";
        case slots::InputRole::AmxMouse: return "amx-mouse";
        case slots::InputRole::AyIo: return "ay-io";
        case slots::InputRole::NextReg: return "nextreg";
    }
    return "";
}

const DeviceAccessChannel* DeviceAccessEvidence::Find(slots::InputRole input, int index, bool out) const
{
    const DeviceAccessChannel* best = nullptr;
    for (const DeviceAccessChannel& channel : channels)
    {
        if (channel.input != input || channel.out != out ||
            (index != DeviceAccessChannel::kNoIndex && channel.index != index))
            continue;
        if (best == nullptr || channel.frames > best->frames ||
            (channel.frames == best->frames && channel.perFrame > best->perFrame))
            best = &channel;
    }
    return best;
}

DeviceAccessWatch::DeviceAccessWatch() = default;
DeviceAccessWatch::~DeviceAccessWatch() = default;

void DeviceAccessWatch::Build(const EmulatorContext* context, PortClaimIndex& index)
{
    Memory* memory = context != nullptr ? context->pMemory : nullptr;
    const PortDecoder* decoder = context != nullptr ? context->pPortDecoder : nullptr;
    SelectedIndexReader selected = nullptr;
    if (decoder != nullptr)
        selected = [](const void* ctx, slots::InputRole role) {
            return static_cast<const PortDecoder*>(ctx)->SelectedIndex(role);
        };
    if (memory == nullptr)
    {
        Build(index, nullptr, nullptr, selected, decoder);
        return;
    }
    Build(
        index,
        [](const void* mem, uint16_t address) {
            return static_cast<Memory*>(const_cast<void*>(mem))->DirectReadFromZ80Memory(address);
        },
        memory, selected, decoder);
}

void DeviceAccessWatch::Build(PortClaimIndex& index, AccessCodeReader read, const void* readContext,
                              SelectedIndexReader selected, const void* selectedContext)
{
    _index = &index;
    _read = read;
    _readContext = readContext;
    _selectedRead = selected;
    _selectedContext = selectedContext;
    const slots::PortOwners& owners = index.Owners();
    // The claims' rules, and a register select per device (the claims of one owner share it)
    std::vector<std::string> devices;
    _rules.assign(PortClaimIndex::kBoard, Rule{});
    _selectRole.assign(256, slots::InputRole::None);
    for (size_t i = 0; i < owners.ClaimCount() && i < PortClaimIndex::kBoard; i++)
    {
        const slots::PortOwners::Claim& claim = owners.At(static_cast<int>(i));
        auto it = std::find(devices.begin(), devices.end(), claim.owner);
        if (it == devices.end())
            it = devices.insert(devices.end(), claim.owner);
        Rule& rule = _rules[i];
        rule.channel = claim.channel;
        const bool selects = claim.channel == slots::Channel::Select || claim.channel == slots::Channel::SelectIndexed;
        rule.mask = selects && claim.channelMask == 0 ? uint16_t{0xFF} : claim.channelMask;
        rule.owner = static_cast<uint8_t>(std::min<size_t>(it - devices.begin(), 255));
        rule.input = claim.input != slots::InputRole::None;
        if (selects)
            _selectRole[rule.owner] = claim.input;
    }
    _selected.assign(256, kValueUnknown);
    Reset();
}

void DeviceAccessWatch::Start()
{
    Reset();
    // The selects the program wrote while the watch was off: as the devices hold them now
    for (size_t owner = 0; owner < _selected.size(); ++owner)
    {
        int index = -1;
        if (_selectedRead != nullptr && _selectRole[owner] != slots::InputRole::None)
            index = _selectedRead(_selectedContext, _selectRole[owner]);
        _selected[owner] = index >= 0 && index < 256 ? static_cast<uint16_t>(index) : kValueUnknown;
    }
    _on = _index != nullptr;
    _active.store(_on, std::memory_order_release);
}

void DeviceAccessWatch::Stop()
{
    _on = false;
    _active.store(false, std::memory_order_release);
}

void DeviceAccessWatch::RequestWindow(uint32_t frames)
{
    _windowRequest.store(frames == 0 ? 1 : frames, std::memory_order_release);
}

void DeviceAccessWatch::CloseWindow()
{
    _windowRequest.store(kCloseWindow, std::memory_order_release);
}

void DeviceAccessWatch::SetContinuous(bool on)
{
    _continuousRequest.store(on, std::memory_order_release);
}

void DeviceAccessWatch::OnFrameBoundary(bool loading)
{
    const bool continuous = _continuousRequest.load(std::memory_order_acquire);
    const uint32_t request = _windowRequest.exchange(0, std::memory_order_acq_rel);
    bool started = false;
    if (request == kCloseWindow)
        _windowLeft = 0;
    else if (request != 0)
    {
        _windowLeft = request;
        started = true;  // a new program: its evidence from zero
    }
    if (continuous && !_continuous)
        started = true;
    _continuous = continuous;
    if (!_continuous && _windowLeft == 0)
    {
        if (_on)
            Stop();
        return;
    }
    // The first boundary starts the count: the frame that ends here was not watched whole
    if (started || !_on)
    {
        Start();
        return;
    }
    // While the media still loads the program is not in: the window counts from the end of the load
    if (loading && _windowLeft != 0)
    {
        Reset();
        return;
    }
    EndFrame();
    if (_windowLeft != 0 && _frames >= _windowLeft)
        _windowLeft = 0;  // the window is over; a continuous watch goes on
}

void DeviceAccessWatch::Reset()
{
    _claimSlots.clear();
    _boardSlots.clear();
    _slots.clear();
    _slots.reserve(kSlots);
    _frame.fill(0);
    _frameTested.fill(0);
    _dropped = 0;
    std::lock_guard<std::mutex> lock(_mutex);
    _published.clear();
    _publishedDropped = 0;
    _ring.assign(kWindow, {});
    _ringTested.assign(kWindow, {});
    _frames = 0;
}

uint8_t DeviceAccessWatch::SlotOf(uint16_t claim, uint16_t value, uint16_t port, bool out)
{
    uint8_t* entry;
    if (claim == PortClaimIndex::kBoard)
    {
        if (_boardSlots.empty()) [[unlikely]]
            _boardSlots.assign(size_t(2) << 16, 0);
        entry = &_boardSlots[(size_t(out) << 16) | port];
        value = port;
    }
    else
    {
        if (_claimSlots.empty()) [[unlikely]]
            _claimSlots.assign(size_t(PortClaimIndex::kBoard) * 2 * kValues, 0);
        entry = &_claimSlots[(size_t(claim) * 2 + (out ? 1 : 0)) * kValues + value];
    }
    if (*entry != 0)
        return static_cast<uint8_t>(*entry - 1);
    if (_slots.size() >= kSlots)
        return kSlots;  // full: counted as dropped
    Slot slot;
    slot.claim = claim;
    slot.value = value;
    slot.out = out;
    _slots.push_back(slot);
    *entry = static_cast<uint8_t>(_slots.size());
    return static_cast<uint8_t>(_slots.size() - 1);
}

void DeviceAccessWatch::Count(uint8_t index, uint16_t pc, bool input)
{
    if (index >= kSlots)
    {
        ++_dropped;
        return;
    }
    if (_frame[index] != UINT16_MAX)
        ++_frame[index];
    Slot& slot = _slots[index];
    for (uint8_t i = 0; i < slot.siteCount; ++i)
    {
        DeviceAccessSite& site = slot.sites[i];
        if (site.pc == pc)
        {
            ++site.count;
            _frameTested[index] |= site.tested;
            return;
        }
    }
    if (slot.siteCount == kSites)
    {
        ++slot.otherSites;
        return;
    }
    // A new site: its tested bits once (a polling loop reads from the same address thousands of times a frame).
    // Every IN form is two bytes from its start (DB n, ED xx)
    DeviceAccessSite& site = slot.sites[slot.siteCount++];
    site.pc = pc;
    site.count = 1;
    if (input && _read != nullptr)
        site.tested = TestedBitsAfterIn(_read, _readContext, static_cast<uint16_t>(pc + 2));
    _frameTested[index] |= site.tested;
}

void DeviceAccessWatch::Record(uint16_t port, uint8_t value, bool out, bool dos, uint16_t pc)
{
    const uint8_t claim = _index->Claim(port, out, dos);
    if (claim == PortClaimIndex::kBoard)
    {
        Count(SlotOf(claim, 0, port, out), pc, false);
        return;
    }
    const Rule& rule = _rules[claim];
    uint16_t channel = 0;
    switch (rule.channel)
    {
        case slots::Channel::None:
            break;
        case slots::Channel::AddressBits:
        {
            const uint16_t low = static_cast<uint16_t>(~port & rule.mask);
            if (low == 0)
                channel = kValueNone;
            else if ((low & (low - 1)) != 0)
                channel = kValueMulti;
            else
            {
                while ((low >> channel) != 1)
                    ++channel;
            }
            break;
        }
        case slots::Channel::Select:
            if (out && (value & ~rule.mask) == 0)
                _selected[rule.owner] = value;
            channel = kValueSelect;
            break;
        case slots::Channel::Indexed:
            channel = _selected[rule.owner];
            break;
        case slots::Channel::SelectIndexed:
            if (out)
            {
                if ((value & ~rule.mask) == 0)  // an AY's chip select or FM address keeps the register
                    _selected[rule.owner] = value;
                channel = kValueSelect;
            }
            else
                channel = _selected[rule.owner];
            break;
    }
    Count(SlotOf(claim, channel, port, out), pc, rule.input && !out);
}

void DeviceAccessWatch::EndFrame()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _ring[_frames % kWindow] = _frame;
    _ringTested[_frames % kWindow] = _frameTested;
    ++_frames;
    _published = _slots;
    _publishedDropped = _dropped;
    _frame.fill(0);
    _frameTested.fill(0);
}

namespace
{
std::string Hex(unsigned value, int digits)
{
    char text[8];
    std::snprintf(text, sizeof(text), "%0*x", digits, value);
    return text;
}
}  // namespace

DeviceAccessEvidence DeviceAccessWatch::Evidence() const
{
    DeviceAccessEvidence evidence;
    evidence.active = _active.load(std::memory_order_acquire);
    std::lock_guard<std::mutex> lock(_mutex);
    evidence.framesObserved = _frames;
    evidence.window = std::min(_frames, kWindow);
    evidence.droppedChannels = _publishedDropped;
    if (_index == nullptr)
        return evidence;
    const slots::PortOwners& owners = _index->Owners();
    for (size_t i = 0; i < _published.size(); ++i)
    {
        const Slot& slot = _published[i];
        DeviceAccessChannel channel;
        channel.out = slot.out;
        if (slot.claim == PortClaimIndex::kBoard)
        {
            channel.device = "board";
            channel.port = slot.value;
            channel.channel = Hex(slot.value, 4);
        }
        else
        {
            const slots::PortOwners::Claim& claim = owners.At(slot.claim);
            channel.device = claim.owner;
            channel.port = claim.port;
            channel.input = claim.input;
            // A plain claim is named by its port: by the low byte when the claim decodes no high line ("1f")
            const bool lowOnly = (claim.mask & 0xFF00) == 0;
            const std::string prefix = !claim.channelName.empty() ? claim.channelName
                                       : lowOnly                  ? Hex(claim.port & 0xFFu, 2)
                                                                  : Hex(claim.port, 4);
            switch (claim.channel)
            {
                case slots::Channel::None:
                    channel.channel = prefix;
                    break;
                case slots::Channel::AddressBits:
                    if (slot.value == kValueNone)
                        channel.channel = prefix + ".none";
                    else if (slot.value == kValueMulti)
                        channel.channel = prefix + ".multi";
                    else
                    {
                        // The line's byte with that line low: a half-row is named by its high byte ("row.fe")
                        channel.index = slot.value;
                        const unsigned shift = (claim.channelMask & 0x00FF) == 0 ? 8 : 0;
                        channel.channel = prefix + "." + Hex((0xFFFFu & ~(1u << slot.value)) >> shift & 0xFF, 2);
                    }
                    break;
                case slots::Channel::Select:
                case slots::Channel::Indexed:
                case slots::Channel::SelectIndexed:
                    if (slot.value == kValueSelect)
                        channel.channel = prefix + ".select";
                    else if (slot.value == kValueUnknown)
                        channel.channel = prefix + ".unknown";
                    else
                    {
                        channel.index = slot.value;
                        channel.channel = prefix + "." + Hex(slot.value, 2);
                    }
                    break;
            }
        }
        unsigned frames = 0, total = 0;
        uint8_t tested = 0;
        for (uint32_t f = 0; f < evidence.window; ++f)
        {
            const uint16_t count = _ring[f][i];
            if (count != 0)
                ++frames;
            total += count;
            tested |= _ringTested[f][i];
        }
        channel.frames = static_cast<uint16_t>(frames);
        channel.perFrame = evidence.window != 0 ? static_cast<float>(total) / static_cast<float>(evidence.window) : 0;
        channel.tested = tested;
        channel.sites.assign(slot.sites.begin(), slot.sites.begin() + slot.siteCount);
        std::stable_sort(channel.sites.begin(), channel.sites.end(),
                         [](const DeviceAccessSite& a, const DeviceAccessSite& b) { return a.count > b.count; });
        channel.otherSites = slot.otherSites;
        evidence.channels.push_back(std::move(channel));
    }
    return evidence;
}
