#include "porthistory.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portclaimindex.h"
#include "emulator/slots/portowners.h"
#include "emulator/state/statenode.h"

void PortAccessRecord::ToState(StateNode& node) const
{
    node["frame"] = int64_t(frame);
    node["tacts"] = int(tacts);
    node["pc"] = int(pc);
    node["dir"] = out ? "out" : "in";
    node["port"] = int(port);
    node["value"] = int(value);
    if (dos)
        node["dos"] = true;
}

history::FieldGetter<PortAccessRecord> PortAccessRecord::FilterField(const std::string& name)
{
    using R = PortAccessRecord;
    if (name == "frame")
        return [](const R& r, int64_t& v) { v = static_cast<int64_t>(r.frame); return true; };
    if (name == "tacts")
        return [](const R& r, int64_t& v) { v = r.tacts; return true; };
    if (name == "pc")
        return [](const R& r, int64_t& v) { v = r.pc; return true; };
    if (name == "dir")
        return [](const R& r, int64_t& v) { v = r.out; return true; };
    if (name == "port")
        return [](const R& r, int64_t& v) { v = r.port; return true; };
    if (name == "value")
        return [](const R& r, int64_t& v) { v = r.value; return true; };
    // ToState shows `dos` only when it is set
    if (name == "dos")
        return [](const R& r, int64_t& v) { v = 1; return r.dos; };
    return nullptr;
}

bool PortAccessRecord::FilterWord(const std::string& field, const std::string& word, int64_t& value)
{
    bool flag = false;
    if (field == "dir" && (word == "in" || word == "out"))
        value = word == "out";
    else if (field == "dos" && history::RecordFilter::FlagWord(word, flag))
        value = flag ? 1 : 0;
    else
        return false;
    return true;
}

const char* PortAccessRecord::FilterFields()
{
    return "dir: in, out; dos: true; numbers: port, pc, value, frame, tacts";
}

PortHistories::PortHistories() = default;
PortHistories::~PortHistories() = default;

void PortHistories::Build(EmulatorContext* context, PortClaimIndex& index)
{
    _context = context;
    _index = &index;
    const slots::PortOwners& owners = index.Owners();
    std::vector<Device> devices;
    auto take = [&](const std::string& id) {
        for (const Device& d : devices)
            if (d.id == id)
                return;
        Device device;
        device.id = id;
        for (Device& old : _devices)
            if (old.id == id)
                device.history = std::move(old.history);  // its records and configuration stay
        if (!device.history)
        {
            device.history = std::make_unique<History>(_context ? &_context->histories : nullptr, id + ".ports",
                                                       "port accesses", kDefaultRing);
            (*device.history)->CountOnIn(&_on);
        }
        devices.push_back(std::move(device));
    };
    take("board");
    for (size_t i = 0; i < owners.ClaimCount(); i++)
        take(owners.ClaimOwner(static_cast<int>(i)));
    _devices = std::move(devices);  // the ones gone leave the registry with their history
    // 256 devices at most in a byte: a machine has some tens. Index 255 (PortClaimIndex::kBoard) is the board's
    _claimDevice.assign(256, 0);
    for (size_t i = 0; i < owners.ClaimCount() && i < PortClaimIndex::kBoard; i++)
        for (size_t d = 0; d < _devices.size() && d < 256; d++)
            if (_devices[d].id == owners.ClaimOwner(static_cast<int>(i)))
                _claimDevice[i] = static_cast<uint8_t>(d);
}

void PortHistories::Record(uint16_t port, uint8_t value, bool out, bool dos, uint16_t pc)
{
    if (_devices.empty() || (_context && _context->ttdReplayActive))
        return;
    History* history = _devices[_claimDevice[_index->Claim(port, out, dos)]].history.get();
    if (!history->On())
        return;
    PortAccessRecord record;
    record.port = port;
    record.value = value;
    record.out = out;
    record.dos = dos;
    record.pc = pc;
    if (_context)
    {
        if (_context->pCore != nullptr)
            if (const Z80* z80 = _context->pCore->GetZ80())
                record.tacts = static_cast<uint16_t>(z80->t);
        record.frame = _context->emulatorState.frame_counter;
    }
    (*history)->Push(record);
}

std::vector<std::string> PortHistories::Devices() const
{
    std::vector<std::string> ids;
    for (const Device& device : _devices)
        ids.push_back(device.id);
    return ids;
}
