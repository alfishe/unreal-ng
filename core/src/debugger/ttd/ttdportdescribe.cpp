#include "debugger/ttd/ttdportdescribe.h"

#include <bit>
#include <cstdio>

#include "debugger/ttd/ttdportsearch.h"

namespace ttd
{
namespace
{
std::string Hex2(uint8_t value)
{
    char text[4];
    std::snprintf(text, sizeof(text), "#%02X", value);
    return text;
}

std::string Join(const std::vector<std::string>& parts, const char* separator)
{
    std::string text;
    for (const std::string& part : parts)
        text += (text.empty() ? "" : separator) + part;
    return text;
}

constexpr const char* kColours[8] = {"black", "blue", "red", "magenta", "green", "cyan", "yellow", "white"};

/// The keyboard half-row of address line A(8+n), as printed on the keys
constexpr const char* kHalfRows[8] = {"CAPS SHIFT-V", "A-G", "Q-T", "1-5", "0-6", "P-Y", "ENTER-H", "SPACE-B"};

std::string KeyboardRead(uint16_t port, uint8_t value)
{
    const uint8_t rows = KeyboardRowsRead(port);
    std::string text = "keyboard: ";
    if (rows == 0xFF)
        text += "all half-rows";
    else
    {
        std::vector<std::string> names;
        for (int n = 0; n < 8; ++n)
            if (rows & (1u << n))
                names.push_back(kHalfRows[n]);
        text += (names.size() == 1 ? "half-row " : "half-rows ") + Join(names, ", ");
    }
    const std::vector<std::string> keys = KeysSeenDown(port, value);
    if (!keys.empty())
        text += ", keys down: " + Join(keys, " ");
    else if ((value & 0x1F) != 0x1F)
        text += ", a key down";
    else
        text += ", no key";
    text += std::string(", EAR ") + ((value & 0x40) ? "1" : "0");
    return text;
}

std::string UlaWrite(uint8_t value)
{
    return std::string("border ") + kColours[value & 0x07] + ", MIC " + ((value & 0x08) ? "1" : "0") + ", beeper " +
           ((value & 0x10) ? "1" : "0");
}

std::string AyAccess(bool out, uint16_t port, uint8_t value, int ayRegister)
{
    const bool select = (port & 0xC002) == 0xC000;  // #FFFD; #BFFD writes the data
    if (select && out)
    {
        if (value > 15)
            return "AY: select " + Hex2(value) + " (not a register: a TurboSound chip select or ignored)";
        return "AY: select register " + std::to_string(value) + " (" + AyRegisterName(value) + ")";
    }
    const std::string reg = ayRegister >= 0 && ayRegister <= 15
                                ? std::string(AyRegisterName(ayRegister))
                                : std::string("the selected register");
    if (!out)
        return "AY: read " + reg + " = " + Hex2(value);
    std::string text = "AY: write " + reg + " = " + Hex2(value);
    if (ayRegister == 7)
    {
        std::vector<std::string> tone, noise;
        for (int c = 0; c < 3; ++c)
        {
            const std::string channel(1, static_cast<char>('A' + c));
            if (!(value & (1u << c)))
                tone.push_back(channel);
            if (!(value & (8u << c)))
                noise.push_back(channel);
        }
        text += " (tone on: " + (tone.empty() ? std::string("none") : Join(tone, " ")) +
                ", noise on: " + (noise.empty() ? std::string("none") : Join(noise, " ")) + ")";
    }
    else if (ayRegister >= 8 && ayRegister <= 10)
        text += (value & 0x10) ? " (the envelope)" : " (level " + std::to_string(value & 0x0F) + ")";
    return text;
}

/// The WD1793's command by its high bits
const char* FdcCommand(uint8_t value)
{
    switch (value >> 4)
    {
        case 0x0: return "Restore";
        case 0x1: return "Seek";
        case 0x2: case 0x3: return "Step";
        case 0x4: case 0x5: return "Step in";
        case 0x6: case 0x7: return "Step out";
        case 0x8: return "Read sector";
        case 0x9: return "Read sectors (multiple)";
        case 0xA: return "Write sector";
        case 0xB: return "Write sectors (multiple)";
        case 0xC: return "Read address";
        case 0xD: return "Force interrupt";
        case 0xE: return "Read track";
        default: return "Write track";
    }
}

/// The Beta 128 interface: the WD1793 at #1F / #3F / #5F / #7F, its system register at #FF
std::string BetaAccess(bool out, uint16_t port, uint8_t value)
{
    switch (port & 0xFF)
    {
        case 0x1F:
        {
            if (out)
                return std::string("FDC: command ") + FdcCommand(value) + " (" + Hex2(value) + ")";
            static constexpr const char* kStatus[8] = {"BUSY", "DRQ/INDEX", "LOST DATA/TRACK 0", "CRC ERROR",
                                                       "NOT FOUND/SEEK ERROR", "DELETED/HEAD LOADED",
                                                       "WRITE PROTECT", "NOT READY"};
            std::vector<std::string> flags;
            for (int bit = 0; bit < 8; ++bit)
                if (value & (1u << bit))
                    flags.push_back(kStatus[bit]);
            return "FDC: status " + Hex2(value) + (flags.empty() ? "" : " (" + Join(flags, ", ") + ")");
        }
        case 0x3F: return std::string("FDC: ") + (out ? "set" : "read") + " track register = " + std::to_string(value);
        case 0x5F: return std::string("FDC: ") + (out ? "set" : "read") + " sector register = " + std::to_string(value);
        case 0x7F: return std::string("FDC: data ") + (out ? "write " : "read ") + Hex2(value);
        case 0xFF:
            if (out)
                return std::string("FDC system: drive ") + static_cast<char>('A' + (value & 0x03)) + ", side " +
                       ((value & 0x10) ? "0" : "1") + ((value & 0x04) ? "" : ", controller reset") +
                       ((value & 0x08) ? ", head load" : "") + ((value & 0x40) ? ", FM" : ", MFM");
            return std::string("FDC system: INTRQ ") + ((value & 0x80) ? "1" : "0") + ", DRQ " +
                   ((value & 0x40) ? "1" : "0");
        default: break;
    }
    return {};
}

std::string Paging7FFD(uint8_t value)
{
    std::string text = "paging: RAM " + std::to_string(value & 0x07) + " at #C000, screen " +
                       ((value & 0x08) ? "7" : "5") + ", ROM " + ((value & 0x10) ? "1" : "0");
    if (value & 0x20)
        text += ", paging locked";
    if (value & 0xC0)
        text += ", bits 6-7 " + Hex2(value & 0xC0) + " (extended RAM on some models)";
    return text;
}

std::string Paging1FFD(uint8_t value)
{
    return std::string("paging (+2A/+3): ") + ((value & 0x01) ? "special (all-RAM) mode" : "normal mode") +
           ", ROM high bit " + ((value & 0x04) ? "1" : "0") + ", disk motor " + ((value & 0x08) ? "on" : "off") +
           ((value & 0x10) ? ", printer strobe" : "");
}

std::string Kempston(uint8_t value)
{
    static constexpr const char* kDirections[5] = {"right", "left", "down", "up", "fire"};
    std::vector<std::string> pressed;
    for (int bit = 0; bit < 5; ++bit)
        if (value & (1u << bit))
            pressed.push_back(kDirections[bit]);
    return "joystick: " + (pressed.empty() ? std::string("nothing pressed") : Join(pressed, ", "));
}
}  // namespace

const char* AyRegisterName(int reg)
{
    static constexpr const char* kNames[16] = {"tone A fine",   "tone A coarse",   "tone B fine",    "tone B coarse",
                                               "tone C fine",   "tone C coarse",   "noise period",   "mixer",
                                               "volume A",      "volume B",        "volume C",       "envelope fine",
                                               "envelope coarse", "envelope shape", "I/O port A",    "I/O port B"};
    return reg >= 0 && reg < 16 ? kNames[reg] : "";
}

PortAccessDescriber::PortAccessDescriber(const PortDecoder* decoder)
{
    if (decoder)
        _rows = decoder->getPortMapEntries();
    _standard = _rows.empty();
    for (const PortMapEntry& row : _rows)
    {
        const std::string device = row.device ? row.device : "";
        if ((row.tags & PortTag::Storage) &&
            (device.find("Beta128") != std::string::npos || device.find("WD1793") != std::string::npos))
        {
            _betaDevice = device.find("WD1793") != std::string::npos ? device : "Beta128 FDC";
            break;
        }
    }
}

const PortMapEntry* PortAccessDescriber::RowOf(uint16_t port) const
{
    // The most specific row (the most address bits decoded), as the port classifier picks it
    const PortMapEntry* best = nullptr;
    int bestBits = -1;
    for (const PortMapEntry& row : _rows)
        if ((port & row.mask) == row.match && std::popcount(row.mask) > bestBits)
        {
            bestBits = std::popcount(row.mask);
            best = &row;
        }
    return best;
}

TTDPortAccessText PortAccessDescriber::Describe(bool out, uint16_t port, uint8_t value, uint16_t pc,
                                                int ayRegister) const
{
    TTDPortAccessText text;
    // The Beta 128 answers its five low bytes (any high byte) only while the TR-DOS ROM runs
    const uint8_t low = static_cast<uint8_t>(port & 0xFF);
    if (!_betaDevice.empty() && pc < 0x4000 && (low == 0x1F || low == 0x3F || low == 0x5F || low == 0x7F || low == 0xFF))
    {
        text.device = _betaDevice;
        text.tags = PortTagSetToStrings(static_cast<PortTagSet>(PortTag::StorageFdc));
        text.what = BetaAccess(out, port, value);
        return text;
    }
    const PortMapEntry* row = RowOf(port);
    // Outside the TR-DOS ROM the Beta's rows do not answer: #1F is the Kempston joystick's (any high byte)
    if (row && row->device && std::string(row->device).find("Beta128") != std::string::npos)
    {
        row = nullptr;
        if (low == 0x1F)
            for (const PortMapEntry& candidate : _rows)
                if ((candidate.tags & PortTag::Joystick) && (candidate.match & 0xFF) == 0x1F)
                {
                    row = &candidate;
                    break;
                }
    }
    PortTagSet tags = row ? row->tags : 0;
    PagingLatch latch = row ? row->latch : PagingLatch::None;
    if (row)
    {
        text.device = row->device ? row->device : "";
        text.tags = PortTagSetToStrings(tags);
    }
    else if (_standard)
    {
        // No port map (a file): the ports every Spectrum decodes the same way
        if ((port & 1) == 0)
        {
            tags = static_cast<PortTagSet>(PortTag::Keyboard);
            latch = PagingLatch::PFE;
        }
        else if ((port & 0x8002) == 0x8000)
            tags = static_cast<PortTagSet>(PortTag::SoundAy);
    }

    if (latch == PagingLatch::PFE || (tags & PortTag::Keyboard))
        text.what = out ? UlaWrite(value) : KeyboardRead(port, value);
    else if ((tags & PortTag::SoundAy) == static_cast<PortTagSet>(PortTag::SoundAy))
        text.what = AyAccess(out, port, value, ayRegister);
    else if (latch == PagingLatch::P7FFD && out)
        text.what = Paging7FFD(value);
    else if (latch == PagingLatch::P1FFD && out)
        text.what = Paging1FFD(value);
    else if ((tags & PortTag::Joystick) && !out)
        text.what = Kempston(value);

    if (text.what.empty())
    {
        if (row)
            text.what = (text.device.empty() ? std::string("device") : text.device) + (out ? ": write " : ": read ") +
                        Hex2(value);
        else if (_standard)
            text.what = std::string(out ? "write " : "read ") + Hex2(value);
        else
            text.what = std::string("no device decodes this port: ") + (out ? "write " : "read ") + Hex2(value);
    }
    return text;
}

}  // namespace ttd
