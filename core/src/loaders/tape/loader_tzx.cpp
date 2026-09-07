#include "loader_tzx.h"

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"

/// region <Little-endian readers and shared helpers>

namespace
{
    // Every multi-byte TZX field is little-endian (spec: "all words are stored
    // with the least significant byte first"). Bounds are the caller's job —
    // ScanOneBlock checks the framing before any read.
    uint16_t ReadU16(const std::span<const uint8_t>& bytes, size_t offset)
    {
        return static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
    }

    uint32_t ReadU24(const std::span<const uint8_t>& bytes, size_t offset)
    {
        return static_cast<uint32_t>(bytes[offset]) |
               (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
               (static_cast<uint32_t>(bytes[offset + 2]) << 16);
    }

    uint32_t ReadU32(const std::span<const uint8_t>& bytes, size_t offset)
    {
        return ReadU24(bytes, offset) | (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    }

    int32_t ReadS16(const std::span<const uint8_t>& bytes, size_t offset)
    {
        return static_cast<int16_t>(ReadU16(bytes, offset));
    }

    // TZX text fields: raw bytes with '\r' line endings normalised to '\n'
    // (libspectrum does the same on read).
    std::string ReadText(const std::span<const uint8_t>& bytes, size_t offset, size_t length)
    {
        std::string result;
        result.reserve(length);
        for (size_t i = 0; i < length; i++)
        {
            char c = static_cast<char>(bytes[offset + i]);
            result.push_back(c == '\r' ? '\n' : c);
        }
        return result;
    }

    std::string HexByte(uint8_t value)
    {
        static const char digits[] = "0123456789ABCDEF";
        return std::string{ digits[value >> 4], digits[value & 0x0F] };
    }

    // libspectrum tzx_normalise_used_bits(): a zero "used bits in last byte"
    // field with a non-empty payload means "8 bits, and the last byte is
    // padding" — the padding byte is dropped. Values above 8 clamp to 8.
    void NormaliseUsedBits(uint8_t& bitsInLastByte, std::vector<uint8_t>& data)
    {
        if (bitsInLastByte == 0 && !data.empty())
        {
            bitsInLastByte = 8;
            data.pop_back();
        }
        else if (bitsInLastByte > 8)
        {
            bitsInLastByte = 8;
        }
    }

    /// region <Hardware ID table (TZX $33 and legacy parseHardware)>

    // Single source of truth for both the legacy parseHardware() walker and
    // the $33 hardwareNote assembler: '\0'-separated strings, an empty string
    // terminating each group. Group index = hardware type; entry 0 of a group
    // is the category name, entries 1..N are the IDs.
    const char hardwareIDs[] =
            "computer\0"
            "ZX Spectrum 16k\0"
            "ZX Spectrum 48k, Plus\0"
            "ZX Spectrum 48k ISSUE 1\0"
            "ZX Spectrum 128k (Sinclair)\0"
            "ZX Spectrum 128k +2 (Grey case)\0"
            "ZX Spectrum 128k +2A, +3\0"
            "Timex Sinclair TC-2048\0"
            "Timex Sinclair TS-2068\0"
            "Pentagon 128\0"
            "Sam Coupe\0"
            "Didaktik M\0"
            "Didaktik Gama\0"
            "ZX-81 or TS-1000 with  1k RAM\0"
            "ZX-81 or TS-1000 with 16k RAM or more\0"
            "ZX Spectrum 128k, Spanish version\0"
            "ZX Spectrum, Arabic version\0"
            "TK 90-X\0"
            "TK 95\0"
            "Byte\0"
            "Elwro\0"
            "ZS Scorpion\0"
            "Amstrad CPC 464\0"
            "Amstrad CPC 664\0"
            "Amstrad CPC 6128\0"
            "Amstrad CPC 464+\0"
            "Amstrad CPC 6128+\0"
            "Jupiter ACE\0"
            "Enterprise\0"
            "Commodore 64\0"
            "Commodore 128\0"
            "\0"
            "ext. storage\0"
            "Microdrive\0"
            "Opus Discovery\0"
            "Disciple\0"
            "Plus-D\0"
            "Rotronics Wafadrive\0"
            "TR-DOS (BetaDisk)\0"
            "Byte Drive\0"
            "Watsford\0"
            "FIZ\0"
            "Radofin\0"
            "Didaktik disk drives\0"
            "BS-DOS (MB-02)\0"
            "ZX Spectrum +3 disk drive\0"
            "JLO (Oliger) disk interface\0"
            "FDD3000\0"
            "Zebra disk drive\0"
            "Ramex Millenia\0"
            "Larken\0"
            "\0"
            "ROM/RAM type add-on\0"
            "Sam Ram\0"
            "Multiface\0"
            "Multiface 128k\0"
            "Multiface +3\0"
            "MultiPrint\0"
            "MB-02 ROM/RAM expansion\0"
            "\0"
            "sound device\0"
            "Classic AY hardware\0"
            "Fuller Box AY sound hardware\0"
            "Currah microSpeech\0"
            "SpecDrum\0"
            "AY ACB stereo; Melodik\0"
            "AY ABC stereo\0"
            "\0"
            "joystick\0"
            "Kempston\0"
            "Cursor, Protek, AGF\0"
            "Sinclair 2\0"
            "Sinclair 1\0"
            "Fuller\0"
            "\0"
            "mice\0"
            "AMX mouse\0"
            "Kempston mouse\0"
            "\0"
            "other controller\0"
            "Trickstick\0"
            "ZX Light Gun\0"
            "Zebra Graphics Tablet\0"
            "\0"
            "serial port\0"
            "ZX Interface 1\0"
            "ZX Spectrum 128k\0"
            "\0"
            "parallel port\0"
            "Kempston S\0"
            "Kempston E\0"
            "ZX Spectrum 128k +2A, +3\0"
            "Tasman\0"
            "DK'Tronics\0"
            "Hilderbay\0"
            "INES Printerface\0"
            "ZX LPrint Interface 3\0"
            "MultiPrint\0"
            "Opus Discovery\0"
            "Standard 8255 chip with ports 31,63,95\0"
            "\0"
            "printer\0"
            "ZX Printer, Alphacom 32 & compatibles\0"
            "Generic Printer\0"
            "EPSON Compatible\0"
            "\0"
            "modem\0"
            "VTX 5000\0"
            "T/S 2050 or Westridge 2050\0"
            "\0"
            "digitaiser\0"
            "RD Digital Tracer\0"
            "DK'Tronics Light Pen\0"
            "British MicroGraph Pad\0"
            "\0"
            "network adapter\0"
            "ZX Interface 1\0"
            "\0"
            "keyboard / keypad\0"
            "Keypad for ZX Spectrum 128k\0"
            "\0"
            "AD/DA converter\0"
            "Harley Systems ADC 8.2\0"
            "Blackboard Electronics\0"
            "\0"
            "EPROM Programmer\0"
            "Orme Electronics\0"
            "\0"
            "\0";

    const char* LookupHardwareEntry(uint8_t groupIndex, uint8_t entryIndex)
    {
        const char* cursor = hardwareIDs;

        for (uint8_t group = 0; group < groupIndex; group++)
        {
            while (*cursor != '\0')
            {
                cursor += strlen(cursor) + 1;
            }
            cursor++;  // past the group terminator
        }

        for (uint8_t entry = 0; entry < entryIndex; entry++)
        {
            if (*cursor == '\0')
            {
                return nullptr;  // past the last ID of this group
            }
            cursor += strlen(cursor) + 1;
        }

        return *cursor != '\0' ? cursor : nullptr;
    }

    const char* HardwareValueName(uint8_t value)
    {
        switch (value)
        {
            case 0: return "compatible with";
            case 1: return "uses";
            case 2: return "compatible, but doesn't use";
            case 3: return "incompatible with";
            default: return "unknown relation to";
        }
    }

    /// endregion </Hardware ID table (TZX $33 and legacy parseHardware)>
}

/// endregion </Little-endian readers and shared helpers>

/// region <Constructors / destructors>

LoaderTZX::LoaderTZX()
{
    _context = nullptr;
    _logger = nullptr;
}

LoaderTZX::LoaderTZX(EmulatorContext* context, std::string path)
{
    _context = context;
    _logger = context->pModuleLogger;

    _path = path;
}

LoaderTZX::~LoaderTZX()
{
    if (_file)
    {
        FileHelper::CloseFile(_file);
        _file = nullptr;
    }

    if (_buffer)
    {
        free(_buffer);
    }
}

/// endregion </Constructors / destructors>

// Registration lives in TapeLoaderRegistry::Instance() (loader_tape.cpp):
// a registrar flag here is invisible to other translation units, so the
// static linker drops this object from binaries that never name LoaderTZX.

/// region <LoaderTapeBase contract>

const TapeFormatInfo& LoaderTZX::Format() const
{
    static const TapeFormatInfo format =
    {
        "tzx",
        "TZX (tagged tape image)",
        { "tzx" },
        &LoaderTZX::Probe
    };

    return format;
}

int LoaderTZX::Probe(std::span<const uint8_t> bytes)
{
    // Magic-bearing format (design §5.3): "ZXTape!" + 0x1A answers 100,
    // anything else 0 — never graded.
    static constexpr uint8_t MAGIC[] = { 'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A };

    if (bytes.size() < sizeof(MAGIC))
    {
        return 0;
    }

    for (size_t i = 0; i < sizeof(MAGIC); i++)
    {
        if (bytes[i] != MAGIC[i])
        {
            return 0;
        }
    }

    return 100;
}

TapeImage LoaderTZX::Load(std::span<const uint8_t> bytes, const std::string& sourceName)
{
    TapeImage image;
    image.formatId = "tzx";

    // Stage 1 (design §5.6): header validation + one pass over the raw blocks
    // in file order. A false return means the header was rejected outright;
    // the scan itself only ever stops early with a warning, keeping the parsed
    // prefix usable.
    std::vector<TzxRawEntry> entries;
    if (!ParseHeaderAndScan(bytes, sourceName, entries, image))
    {
        return image;
    }

    // Stage 2: control-flow linearization + emission into blocks/descriptors.
    Linearize(entries, image);

    if (image.blocks.empty())
    {
        image.status = TapeLoadStatus::Unsupported;
        image.errorText = "No playable blocks in '" + sourceName + "'";
        return image;
    }

    if (!image.parseWarnings.empty())
    {
        image.status = TapeLoadStatus::Warnings;
    }

    return image;
}

/// endregion </LoaderTapeBase contract>

/// region <Parse: header and block scan>

bool LoaderTZX::ParseHeaderAndScan(std::span<const uint8_t> bytes, const std::string& sourceName,
                                   std::vector<TzxRawEntry>& entries, TapeImage& image)
{
    // D1 version policy: 1.20/1.21 framing only. The minor version is the
    // plain decimal number (spec: "e.g. 13 or 20 for version 1.13 or 1.20"),
    // so 1.20 is 0x14 and 1.21 is 0x15 — NOT 0x20/0x21. Major != 1 is a hard
    // reject; a newer minor may carry blocks this scanner does not know (they
    // stop the scan with a warning), an older minor may carry deprecated
    // layouts.
    if (bytes.size() < 10)
    {
        image.status = TapeLoadStatus::Malformed;
        image.errorText = "'" + sourceName + "' too short for a TZX header";
        return false;
    }

    static constexpr uint8_t MAGIC[] = { 'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A };
    for (size_t i = 0; i < sizeof(MAGIC); i++)
    {
        if (bytes[i] != MAGIC[i])
        {
            image.status = TapeLoadStatus::Malformed;
            image.errorText = "'" + sourceName + "' is not a TZX image (bad signature)";
            return false;
        }
    }

    uint8_t major = bytes[8];
    uint8_t minor = bytes[9];
    if (major != 1)
    {
        image.status = TapeLoadStatus::Malformed;
        image.errorText = "TZX major version " + std::to_string(major) + " not supported (1.x only)";
        return false;
    }

    if (minor > 0x15)
    {
        image.parseWarnings.push_back("TZX version 1." + std::to_string(minor) +
                                      " is newer than 1.21 — unknown blocks may stop the scan early");
    }
    else if (minor < 0x14)
    {
        image.parseWarnings.push_back("TZX version 1." + std::to_string(minor) +
                                      " predates 1.20 — deprecated block layouts may appear");
    }

    // One pass in file order (design §5.6): every block lands as a raw entry
    // with only its ID-implied fields filled; control flow and emission come
    // later. ScanOneBlock returning false stops the scan, keeping the prefix.
    size_t offset = 10;
    while (offset < bytes.size())
    {
        TzxRawEntry entry;
        entry.id = bytes[offset];
        entry.fileOffset = offset;

        if (!ScanOneBlock(bytes, offset, entry, image))
        {
            break;
        }

        entries.push_back(std::move(entry));
    }

    // Title (design §5.5): $32 archive "full title" (type 00) wins, then the
    // first $30 text. Anything else a $32 carried is diagnostics only.
    for (const TzxRawEntry& entry : entries)
    {
        if (entry.id == 0x32 && !entry.text.empty())
        {
            image.title = entry.text;
            break;
        }
    }
    if (image.title.empty())
    {
        for (const TzxRawEntry& entry : entries)
        {
            if (entry.id == 0x30 && !entry.text.empty())
            {
                image.title = entry.text;
                break;
            }
        }
    }

    return true;
}

bool LoaderTZX::ScanOneBlock(std::span<const uint8_t> bytes, size_t& offset, TzxRawEntry& entry, TapeImage& image)
{
    const size_t size = bytes.size();
    size_t pos = offset + 1;   // past the ID byte
    bool framed = true;        // false = framing truncated, scan must stop

    // Clamped payload copy: keep what is present, warn about the rest — the
    // prefix stays usable (same policy as LoaderTAP's truncated final block).
    auto takePayload = [&bytes, &pos, size, &offset, &entry, &image](uint32_t declaredLength, uint8_t blockId) -> size_t
    {
        size_t available = size - pos;
        size_t take = std::min<size_t>(declaredLength, available);
        if (take < declaredLength)
        {
            image.parseWarnings.push_back("Truncated $" + HexByte(blockId) + " payload at offset " +
                                          std::to_string(offset) + ": " + std::to_string(take) + " of " +
                                          std::to_string(declaredLength) + " bytes present");
        }
        entry.data.assign(bytes.begin() + pos, bytes.begin() + pos + take);
        pos += take;
        return take;
    };

    switch (entry.id)
    {
        case 0x10:  // $10 Standard speed data: [pause:2][len:2][data]
        {
            if (pos + 4 > size) { framed = false; break; }
            entry.timing.pauseMs = ReadU16(bytes, pos);
            uint32_t length = ReadU16(bytes, pos + 2);
            pos += 4;
            takePayload(length, 0x10);
            break;
        }

        case 0x11:  // $11 Turbo speed data — 18-byte header (settled layout, see file docs)
        {
            if (pos + 18 > size) { framed = false; break; }
            entry.timing.profile = TapeSpeedProfileEnum::Custom;
            entry.timing.pilotHalfPeriod = ReadU16(bytes, pos);
            entry.timing.sync1 = ReadU16(bytes, pos + 2);
            entry.timing.sync2 = ReadU16(bytes, pos + 4);
            entry.timing.zeroHalfPeriod = ReadU16(bytes, pos + 6);
            entry.timing.oneHalfPeriod = ReadU16(bytes, pos + 8);
            entry.timing.pilotPulses = ReadU16(bytes, pos + 10);
            entry.timing.bitsInLastByte = bytes[pos + 12];
            entry.timing.pauseMs = ReadU16(bytes, pos + 13);
            uint32_t length = ReadU24(bytes, pos + 15);
            entry.useTiming = true;
            pos += 18;
            takePayload(length, 0x11);
            NormaliseUsedBits(entry.timing.bitsInLastByte, entry.data);
            break;
        }

        case 0x12:  // $12 Pure tone: [period:2][count:2]
        {
            if (pos + 4 > size) { framed = false; break; }
            uint32_t period = ReadU16(bytes, pos);
            uint32_t count = ReadU16(bytes, pos + 2);
            pos += 4;
            if (count > 0 && period > 0)
            {
                entry.pulses.assign(count, period);
            }
            break;
        }

        case 0x13:  // $13 Pulse sequence: [count:1][period:2 x count] — count is u8 (C1)
        {
            if (pos + 1 > size) { framed = false; break; }
            size_t count = bytes[pos];
            pos += 1;
            if (pos + count * 2 > size) { framed = false; break; }
            entry.pulses.reserve(count);
            for (size_t i = 0; i < count; i++)
            {
                entry.pulses.push_back(ReadU16(bytes, pos));
                pos += 2;
            }
            break;
        }

        case 0x14:  // $14 Pure data: [zero:2][one:2][bits:1][pause:2][len:3][raw bits MSB-first]
        {
            if (pos + 10 > size) { framed = false; break; }
            entry.timing.profile = TapeSpeedProfileEnum::Custom;
            entry.timing.zeroHalfPeriod = ReadU16(bytes, pos);
            entry.timing.oneHalfPeriod = ReadU16(bytes, pos + 2);
            entry.timing.bitsInLastByte = bytes[pos + 4];
            entry.timing.pauseMs = ReadU16(bytes, pos + 5);
            uint32_t length = ReadU24(bytes, pos + 7);
            entry.useTiming = true;
            pos += 10;
            takePayload(length, 0x14);
            NormaliseUsedBits(entry.timing.bitsInLastByte, entry.data);
            break;
        }

        case 0x15:  // $15 Direct recording: [tstates:2][pause:2][bits:1][len:3][samples]
        {
            if (pos + 8 > size) { framed = false; break; }
            uint32_t tstatesPerSample = ReadU16(bytes, pos);
            entry.timing.pauseMs = ReadU16(bytes, pos + 2);
            uint8_t bitsInLastByte = bytes[pos + 4];
            uint32_t length = ReadU24(bytes, pos + 5);
            pos += 8;
            takePayload(length, 0x15);
            NormaliseUsedBits(bitsInLastByte, entry.data);

            // The sample bytes stay in entry.data (catalog rawSize); the
            // playable content is the run-length pulse train.
            if (!entry.data.empty() && tstatesPerSample > 0)
            {
                size_t bitCount = (entry.data.size() - 1) * 8 + bitsInLastByte;
                AppendDirectRecordingRuns(entry.data.data(), bitCount,
                                          static_cast<uint16_t>(tstatesPerSample), entry.pulses);
            }
            break;
        }

        case 0x18:  // $18 CSW recording: [len:4][pause:2][rate:3][compression:1][pulses:4][data]
        {
            if (pos + 14 > size) { framed = false; break; }
            uint32_t length = ReadU32(bytes, pos);
            entry.timing.pauseMs = ReadU16(bytes, pos + 4);
            uint32_t rate = ReadU24(bytes, pos + 6);
            uint8_t compression = bytes[pos + 9];
            uint32_t pulseCount = ReadU32(bytes, pos + 10);
            pos += 14;

            size_t available = size - pos;
            size_t take = std::min<size_t>(length, available);
            if (take < length)
            {
                image.parseWarnings.push_back("Truncated $18 payload at offset " + std::to_string(offset) +
                                              ": " + std::to_string(take) + " of " +
                                              std::to_string(length) + " bytes present");
            }

            if (compression == 1 && rate > 0)
            {
                if (!DecodeCswRle16(bytes.data() + pos, take, pulseCount, entry.pulses))
                {
                    image.parseWarnings.push_back("CSW RLE stream at offset " + std::to_string(offset) +
                                                  " ends before its declared pulse count");
                }
                if (entry.pulses.size() != static_cast<size_t>(pulseCount))
                {
                    image.parseWarnings.push_back("CSW pulse count mismatch at offset " + std::to_string(offset) +
                                                  ": decoded " + std::to_string(entry.pulses.size()) +
                                                  ", header declares " + std::to_string(pulseCount));
                }

                // Sample counts -> T-states at the ZX clock (round to nearest)
                for (uint32_t& pulse : entry.pulses)
                {
                    pulse = static_cast<uint32_t>((static_cast<uint64_t>(pulse) * 3500000 + rate / 2) / rate);
                }
            }
            else
            {
                // zlib-compressed (method 2) or unknown rate: keep the payload
                // for the catalog, emit catalog-only (design §5.6)
                entry.data.assign(bytes.begin() + pos, bytes.begin() + pos + take);
                entry.unplayable = true;
            }
            pos += take;
            break;
        }

        case 0x19:  // $19 Generalized data: [len:4][body] — catalog-only in v1 (design §5.6)
        {
            if (pos + 4 > size) { framed = false; break; }
            uint32_t length = ReadU32(bytes, pos);
            pos += 4;
            takePayload(length, 0x19);
            entry.unplayable = true;
            break;
        }

        case 0x20:  // $20 Pause: [ms:2] — 0 = stop the tape
        {
            if (pos + 2 > size) { framed = false; break; }
            entry.timing.pauseMs = ReadU16(bytes, pos);
            pos += 2;
            break;
        }

        case 0x21:  // $21 Group start: [len:1][name]
        {
            if (pos + 1 > size) { framed = false; break; }
            size_t length = bytes[pos];
            pos += 1;
            size_t available = size - pos;
            size_t take = std::min(length, available);
            if (take < length)
            {
                image.parseWarnings.push_back("Truncated $21 group name at offset " + std::to_string(offset));
            }
            entry.text = ReadText(bytes, pos, take);
            pos += take;
            break;
        }

        case 0x22:  // $22 Group end (empty body)
            break;

        case 0x23:  // $23 Jump: [offset:2s] — relative to this block, +1 = next
        {
            if (pos + 2 > size) { framed = false; break; }
            entry.jumpOffset = ReadS16(bytes, pos);
            pos += 2;
            break;
        }

        case 0x24:  // $24 Loop start: [count:2] — TOTAL body executions (C3)
        {
            if (pos + 2 > size) { framed = false; break; }
            entry.loopCount = ReadU16(bytes, pos);
            pos += 2;
            break;
        }

        case 0x25:  // $25 Loop end (empty body)
            break;

        case 0x26:  // $26 Call sequence: [count:2][offsets:2s x count] — no length prefix
        {
            if (pos + 2 > size) { framed = false; break; }
            size_t count = ReadU16(bytes, pos);
            pos += 2;
            if (pos + count * 2 > size) { framed = false; break; }
            entry.flowOffsets.reserve(count);
            for (size_t i = 0; i < count; i++)
            {
                entry.flowOffsets.push_back(ReadS16(bytes, pos));
                pos += 2;
            }
            break;
        }

        case 0x27:  // $27 Return (empty body)
            break;

        case 0x28:  // $28 Select: [length:2][count:1]{[offset:2s][descLen:1][text]} (C2)
        {
            if (pos + 3 > size) { framed = false; break; }
            size_t length = ReadU16(bytes, pos);
            pos += 2;
            size_t count = bytes[pos];
            pos += 1;

            // length covers the count byte plus every selection entry
            size_t declaredEnd = std::min(pos - 1 + length, size);
            bool malformed = false;
            for (size_t i = 0; i < count; i++)
            {
                if (pos + 3 > declaredEnd)
                {
                    malformed = true;
                    break;
                }
                int32_t selectionOffset = ReadS16(bytes, pos);
                size_t textLength = bytes[pos + 2];
                pos += 3;
                if (pos + textLength > declaredEnd)
                {
                    malformed = true;
                    break;
                }
                entry.flowOffsets.push_back(selectionOffset);
                entry.flowTexts.push_back(ReadText(bytes, pos, textLength));
                pos += textLength;
            }

            if (malformed)
            {
                entry.flowOffsets.clear();
                entry.flowTexts.clear();
                image.parseWarnings.push_back("Malformed $28 select at offset " + std::to_string(offset) +
                                              " — continuing without jump");
            }
            else if (entry.flowOffsets.size() > 1)
            {
                image.parseWarnings.push_back("Select block with " + std::to_string(entry.flowOffsets.size()) +
                                              " branches at offset " + std::to_string(offset) +
                                              " — taking 1: '" + entry.flowTexts[0] + "'");
            }
            pos = std::max(pos, declaredEnd);
            break;
        }

        case 0x2A:  // $2A Stop tape if in 48K: [u32 = 0]
        {
            if (pos + 4 > size) { framed = false; break; }
            pos += 4;
            break;
        }

        case 0x2B:  // $2B Set signal level: [len:4 = 1][level:1]
        {
            if (pos + 5 > size) { framed = false; break; }
            uint32_t length = ReadU32(bytes, pos);
            if (length != 1)
            {
                image.parseWarnings.push_back("$2B block at offset " + std::to_string(offset) +
                                              " declares length " + std::to_string(length) + " (expected 1)");
            }
            entry.level = bytes[pos + 4] & 1;
            pos += 5;
            break;
        }

        case 0x30:  // $30 Text: [len:1][text] — title candidate
        {
            if (pos + 1 > size) { framed = false; break; }
            size_t length = bytes[pos];
            pos += 1;
            size_t available = size - pos;
            size_t take = std::min(length, available);
            if (take < length)
            {
                image.parseWarnings.push_back("Truncated $30 text at offset " + std::to_string(offset));
            }
            entry.text = ReadText(bytes, pos, take);
            pos += take;
            break;
        }

        case 0x31:  // $31 Message: [time:1][len:1][text] — load-time UI hint, not modeled
        {
            if (pos + 2 > size) { framed = false; break; }
            size_t length = bytes[pos + 1];
            pos += 2;
            pos += std::min<size_t>(length, size - pos);
            break;
        }

        case 0x32:  // $32 Archive info: [len:2][count:1]{[id:1][len:1][text]}
        {
            if (pos + 3 > size) { framed = false; break; }
            size_t length = ReadU16(bytes, pos);
            pos += 2;
            size_t count = bytes[pos];
            pos += 1;

            size_t declaredEnd = std::min(pos - 1 + length, size);
            bool truncated = false;
            for (size_t i = 0; i < count; i++)
            {
                if (pos + 2 > declaredEnd)
                {
                    truncated = true;
                    break;
                }
                uint8_t infoId = bytes[pos];
                size_t textLength = bytes[pos + 1];
                pos += 2;
                if (pos + textLength > declaredEnd)
                {
                    truncated = true;
                    break;
                }
                std::string text = ReadText(bytes, pos, textLength);
                pos += textLength;

                if (infoId == 0 && entry.text.empty())
                {
                    entry.text = text;  // type 00 = full title (title precedence)
                }
                else
                {
                    entry.flowTexts.push_back(text);  // publisher, author, ... — diagnostics
                }
            }
            if (truncated)
            {
                image.parseWarnings.push_back("Truncated $32 archive info at offset " + std::to_string(offset));
            }
            pos = std::max(pos, declaredEnd);
            break;
        }

        case 0x33:  // $33 Hardware type: [count:1]{[type:1][id:1][value:1]} — no prefix
        {
            if (pos + 1 > size) { framed = false; break; }
            size_t count = bytes[pos];
            pos += 1;
            if (pos + count * 3 > size) { framed = false; break; }

            for (size_t i = 0; i < count; i++)
            {
                uint8_t hardwareType = bytes[pos];
                uint8_t hardwareId = bytes[pos + 1];
                uint8_t hardwareValue = bytes[pos + 2];
                pos += 3;

                const char* category = LookupHardwareEntry(hardwareType, 0);
                const char* name = LookupHardwareEntry(hardwareType, hardwareId);
                std::string record = std::string(category ? category : "hardware") + ": " +
                                     (name ? name : "unknown") + " (" +
                                     HardwareValueName(hardwareValue) + ")";

                if (!image.hardwareNote.empty())
                {
                    image.hardwareNote += "; ";
                }
                image.hardwareNote += record;
            }
            break;
        }

        case 0x35:  // $35 Custom info: [ASCII:16][len:4][data] — data not modeled in v1
        {
            if (pos + 20 > size) { framed = false; break; }
            size_t descriptionLength = 0;
            while (descriptionLength < 16 && bytes[pos + descriptionLength] != 0)
            {
                descriptionLength += 1;
            }
            entry.text = ReadText(bytes, pos, descriptionLength);

            uint32_t length = ReadU32(bytes, pos + 16);
            pos += 20;
            size_t available = size - pos;
            size_t take = std::min<size_t>(length, available);
            if (take < length)
            {
                image.parseWarnings.push_back("Truncated $35 custom info at offset " + std::to_string(offset));
            }
            pos += take;
            break;
        }

        case 0x16:  // $16/$17/$34/$40 deprecated (1.13-era C64/emulation/
        case 0x17:  // snapshot blocks). All follow the general extension rule
        case 0x34:  // ([len:4][len bytes]) — skipped with a warning; nothing
        case 0x40:  // in them is playable by this build.
        {
            if (pos + 4 > size) { framed = false; break; }
            uint32_t length = ReadU32(bytes, pos);
            pos += 4;
            size_t available = size - pos;
            size_t take = std::min<size_t>(length, available);
            if (take < length)
            {
                image.parseWarnings.push_back("Truncated deprecated block $" + HexByte(entry.id) +
                                              " at offset " + std::to_string(offset));
            }
            image.parseWarnings.push_back("Deprecated block $" + HexByte(entry.id) + " at offset " +
                                          std::to_string(offset) + " skipped (not playable)");
            pos += take;
            break;
        }

        case 0x5A:  // $5A Glue: "XTape!" 0x1A major minor — framing only
        {
            static constexpr uint8_t GLUE[] = { 'X', 'T', 'a', 'p', 'e', '!', 0x1A };
            if (pos + 9 > size) { framed = false; break; }

            bool glueOk = true;
            for (size_t i = 0; i < sizeof(GLUE); i++)
            {
                glueOk = glueOk && bytes[pos + i] == GLUE[i];
            }
            if (!glueOk)
            {
                image.parseWarnings.push_back("Glue block at offset " + std::to_string(offset) +
                                              " has unexpected content");
            }
            if (bytes[pos + 7] != bytes[8] || bytes[pos + 8] != bytes[9])
            {
                image.parseWarnings.push_back("Glue block at offset " + std::to_string(offset) +
                                              " version differs from the file header");
            }
            pos += 9;
            break;
        }

        default:
        {
            // D3: an unknown ID means the framing beyond this point cannot be
            // trusted — stop the scan, keep the parsed prefix usable.
            image.parseWarnings.push_back("Unknown block ID " + HexByte(entry.id) + " at offset " +
                                          std::to_string(offset) + " — scan stopped (D3)");
            return false;
        }
    }

    if (!framed)
    {
        image.parseWarnings.push_back("Truncated block $" + HexByte(entry.id) + " at offset " +
                                      std::to_string(offset) + " — scan stopped");
        return false;
    }

    offset = pos;
    return true;
}

/// endregion </Parse: header and block scan>

/// region <Linearization (control flow)>

namespace
{
    // Emission-relevant IDs (design §5.6): playable payloads plus the context
    // carriers whose position in the output stream matters ($20 pauses,
    // $21/$22 groups, $2A stop-48K, $2B signal level).
    bool TzxIsEmitted(uint8_t id)
    {
        switch (id)
        {
            case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
            case 0x18: case 0x19:
            case 0x20: case 0x21: case 0x22: case 0x2A: case 0x2B:
                return true;
            default:
                return false;
        }
    }
}

void LoaderTZX::Linearize(const std::vector<TzxRawEntry>& entries, TapeImage& image)
{
    std::vector<size_t> order;

    struct LoopFrame
    {
        size_t bodyStart;         // first block after the $24
        uint32_t remaining;       // body executions still owed (C3: count = total)
    };
    struct CallFrame
    {
        size_t callPc;            // index of the $26 itself — offsets are relative to it
        std::vector<int32_t> offsets;
        size_t next;              // next offset to take on return
    };
    std::vector<LoopFrame> loopStack;
    std::vector<CallFrame> callStack;

    // Signed-aware index clamp: offsets are relative to the carrying block,
    // +1 = next block. Out-of-range targets clamp to the tape bounds with a
    // warning — the file is malformed, the walk must not be.
    auto clampIndex = [&image, &entries](ptrdiff_t target, const char* what) -> size_t
    {
        if (target < 0)
        {
            image.parseWarnings.push_back(std::string(what) +
                                          " before the start of the tape — clamped to block 0");
            return 0;
        }
        size_t index = static_cast<size_t>(target);
        if (index > entries.size())
        {
            image.parseWarnings.push_back(std::string(what) +
                                          " past the end of the tape — clamped to the end");
            return entries.size();
        }
        return index;
    };

    // Budget (design §5.6): the flattened stream is capped, and the walk itself
    // is capped so a pathological jump/loop pattern that produces no output
    // cannot spin forever.
    size_t stepBudget = 16 * TAPE_LINEARIZE_MAX_BLOCKS + 8 * entries.size();
    bool bounded = false;
    size_t pc = 0;

    while (pc < entries.size())
    {
        if (order.size() >= TAPE_LINEARIZE_MAX_BLOCKS || stepBudget-- == 0)
        {
            bounded = true;
            break;
        }

        const TzxRawEntry& entry = entries[pc];

        switch (entry.id)
        {
            case 0x23:  // Jump
            {
                if (entry.jumpOffset == 0)
                {
                    image.parseWarnings.push_back("$23 jump of +0 at block " + std::to_string(pc) +
                                                  " — treated as +1");
                    pc += 1;
                }
                else
                {
                    pc = clampIndex(static_cast<ptrdiff_t>(pc) + entry.jumpOffset, "$23 jump");
                }
                break;
            }

            case 0x24:  // Loop start — count = TOTAL body executions (C3)
            {
                uint32_t executions = entry.loopCount == 0 ? 1 : entry.loopCount;
                loopStack.push_back({ pc + 1, executions - 1 });
                pc += 1;
                break;
            }

            case 0x25:  // Loop end
            {
                if (loopStack.empty())
                {
                    image.parseWarnings.push_back("$25 loop end without a matching $24 at block " +
                                                  std::to_string(pc) + " — ignored");
                    pc += 1;
                    break;
                }
                LoopFrame& frame = loopStack.back();
                if (frame.remaining > 0)
                {
                    frame.remaining -= 1;
                    pc = frame.bodyStart;
                }
                else
                {
                    loopStack.pop_back();
                    pc += 1;
                }
                break;
            }

            case 0x26:  // Call sequence — next callee taken on each $27 return
            {
                if (entry.flowOffsets.empty())
                {
                    image.parseWarnings.push_back("$26 call with an empty sequence at block " +
                                                  std::to_string(pc) + " — ignored");
                    pc += 1;
                    break;
                }
                callStack.push_back({ pc, entry.flowOffsets, 1 });
                pc = clampIndex(static_cast<ptrdiff_t>(pc) + entry.flowOffsets[0], "$26 call");
                break;
            }

            case 0x27:  // Return — pops the innermost call, resumes its sequence
            {
                if (callStack.empty())
                {
                    image.parseWarnings.push_back("$27 return without a matching $26 at block " +
                                                  std::to_string(pc) + " — ignored");
                    pc += 1;
                    break;
                }
                CallFrame& frame = callStack.back();
                if (frame.next < frame.offsets.size())
                {
                    // Next callee of the sequence, relative to the $26 itself
                    int32_t callOffset = frame.offsets[frame.next];
                    frame.next += 1;
                    pc = clampIndex(static_cast<ptrdiff_t>(frame.callPc) + callOffset, "$26 call sequence");
                }
                else
                {
                    // Sequence exhausted — the spec's $27: "the tape continues
                    // with the block following this Return block"
                    callStack.pop_back();
                    pc += 1;
                }
                break;
            }

            case 0x28:  // Select — v1 takes the first selection (scan warned)
            {
                if (entry.flowOffsets.empty())
                {
                    pc += 1;
                }
                else
                {
                    pc = clampIndex(static_cast<ptrdiff_t>(pc) + entry.flowOffsets[0], "$28 selection");
                }
                break;
            }

            default:
            {
                if (TzxIsEmitted(entry.id))
                {
                    order.push_back(pc);
                }
                pc += 1;
                break;
            }
        }
    }

    if (bounded)
    {
        // Linear-tail degradation (design §5.6): the remaining blocks append in
        // file order; flow operations become inert control entries so the
        // catalog still mirrors the file's structure.
        image.controlFlowLinearized = false;
        image.parseWarnings.push_back("Control flow exceeded the linearization budget — remaining "
                                      "blocks appended in file order, control operations inert");
        for (size_t i = pc; i < entries.size(); i++)
        {
            uint8_t id = entries[i].id;
            if (TzxIsEmitted(id) || (id >= 0x23 && id <= 0x28))
            {
                order.push_back(i);
            }
        }
    }

    if (!loopStack.empty())
    {
        image.parseWarnings.push_back("Unterminated $24 loop at end of tape");
    }
    if (!callStack.empty())
    {
        image.parseWarnings.push_back("Unterminated $26 call sequence at end of tape");
    }

    // Emission walk (design §5.6): group labels, polarity and short pauses
    // resolve here, in output order — loop-duplicated blocks each inherit the
    // context of their own iteration.
    TzxEmitContext context;
    for (size_t rawIndex : order)
    {
        EmitEntry(entries[rawIndex], image, context);
    }
}

/// endregion </Linearization (control flow)>

/// region <Emission>

void LoaderTZX::EmitEntry(const TzxRawEntry& entry, TapeImage& image, TzxEmitContext& context)
{
    // One-shot context attach (design §5.6): $21 group label and $2B polarity
    // ride along until the next emitted playable block consumes them.
    auto attachPendingContext = [&context](TapeBlock& block, TapeBlockDescriptor& descriptor)
    {
        if (context.hasPendingGroupLabel)
        {
            descriptor.groupLabel = context.pendingGroupLabel;
            context.hasPendingGroupLabel = false;
        }
        if (context.hasPendingInvertedLevel)
        {
            descriptor.timing.invertedLevel = context.pendingInvertedLevel;
            if (block.timing.has_value())
            {
                block.timing->invertedLevel = context.pendingInvertedLevel;
            }
            context.hasPendingInvertedLevel = false;
        }
    };

    switch (entry.id)
    {
        case 0x10:  // $10 Standard speed — ROM encoding; pause stays a catalog hint
        case 0x11:  // $11 Turbo — representation 2, full profile pass-through
        case 0x14:  // $14 Pure data — representation 2, no pilot, no flag framing
        {
            TapeBlock block;
            block.blockIndex = image.blocks.size();
            block.type = entry.data.empty() ? TAP_BLOCK_FLAG_DATA
                                            : static_cast<TapeBlockFlagEnum>(entry.data[0]);
            block.data = entry.data;
            if (entry.useTiming)
            {
                block.timing = entry.timing;
            }

            TapeBlockDescriptor descriptor;
            if (entry.id == 0x14)
            {
                // Raw bits, no flag/checksum framing — keep the catalog from
                // misreading payload bits as a header flag
                descriptor.kind = TapeBlockKindEnum::Custom;
                descriptor.flagBytePresent = false;
            }
            descriptor.timing = entry.timing;  // $10: pauseMs hint; $11/$14: full profile
            descriptor.playable = true;

            attachPendingContext(block, descriptor);
            context.lastPlayableIndex = image.blocks.size();

            image.blocks.push_back(std::move(block));
            image.descriptors.push_back(std::move(descriptor));
            break;
        }

        case 0x12:  // Pure tone
        case 0x13:  // Pulse sequence
        case 0x15:  // Direct recording
        case 0x18:  // CSW recording
        case 0x19:  // Generalized data (catalog-only in v1)
        {
            TapeBlock block;
            block.blockIndex = image.blocks.size();
            block.type = TAP_BLOCK_FLAG_DATA;

            TapeBlockDescriptor descriptor;
            descriptor.kind = entry.id == 0x12 ? TapeBlockKindEnum::Tone : TapeBlockKindEnum::PulseStream;
            descriptor.timing.profile = TapeSpeedProfileEnum::PulseStream;
            descriptor.rawSize = entry.data.size();  // sample/payload bytes; 0 when none kept

            if (!entry.unplayable && !entry.pulses.empty())
            {
                // Representation 3 (design §5.7): the pulse train IS the
                // content; the pause is a trailing hold-edge, exactly how
                // generateBitstream() encodes pauses (1 ms == 3500 T-states).
                block.edgePulseTimings = entry.pulses;
                uint32_t pauseMs = entry.timing.pauseMs;  // $15/$18 only
                if (pauseMs > 0)
                {
                    block.edgePulseTimings.push_back(static_cast<uint32_t>(pauseMs) * 3500);
                }
                for (uint32_t pulse : block.edgePulseTimings)
                {
                    block.totalBitstreamLength += pulse;
                }

                descriptor.timing.pauseMs = pauseMs;
                descriptor.playable = true;

                attachPendingContext(block, descriptor);
                context.lastPlayableIndex = image.blocks.size();
            }
            else
            {
                // Catalog-only entry (design §5.6): the block stays empty, the
                // descriptor carries playability=false and the source size.
                descriptor.playable = false;
                if (entry.id == 0x19)
                {
                    image.parseWarnings.push_back("$19 generalized data block is catalog-only in this build");
                }
                else if (entry.unplayable)
                {
                    image.parseWarnings.push_back("Compressed $18 CSW block is catalog-only in this build");
                }
            }

            image.blocks.push_back(std::move(block));
            image.descriptors.push_back(std::move(descriptor));
            break;
        }

        case 0x20:  // Pause / stop-the-tape marker
        {
            uint32_t pauseMs = entry.timing.pauseMs;

            if (pauseMs == 0)
            {
                // "Stop the tape": v1 keeps later blocks playable and lets
                // playback run past the marker (catalog-first design §5.6) —
                // the engine has no mid-stream stop concept.
                image.parseWarnings.push_back("$20 stop-the-tape marker — playback continues past it in this build");

                TapeBlock block;
                block.blockIndex = image.blocks.size();
                block.type = TAP_BLOCK_FLAG_DATA;

                TapeBlockDescriptor descriptor;
                descriptor.kind = TapeBlockKindEnum::Control;
                descriptor.playable = false;

                image.blocks.push_back(std::move(block));
                image.descriptors.push_back(std::move(descriptor));
            }
            else if (pauseMs <= 5)
            {
                // Short pause merges into the preceding block (spec note on
                // $20). A leading short pause has no target — dropped.
                if (context.lastPlayableIndex == SIZE_MAX)
                {
                    break;
                }

                TapeBlock& target = image.blocks[context.lastPlayableIndex];
                TapeBlockDescriptor& targetDescriptor = image.descriptors[context.lastPlayableIndex];

                // Catalog: grow the target's pause
                uint32_t merged = static_cast<uint32_t>(targetDescriptor.timing.pauseMs) + pauseMs;
                targetDescriptor.timing.pauseMs = static_cast<uint16_t>(std::min<uint32_t>(merged, 0xFFFF));
                if (target.timing.has_value())
                {
                    merged = static_cast<uint32_t>(target.timing->pauseMs) + pauseMs;
                    target.timing->pauseMs = static_cast<uint16_t>(std::min<uint32_t>(merged, 0xFFFF));
                }

                // Pulse representation: append a hold of pauseMs ms of real
                // silence. Representation-1 blocks regenerate on demand with
                // the ROM's 1000 ms pause — the merged value shows in the
                // catalog only (documented v1 limitation).
                if (target.data.empty() && !target.edgePulseTimings.empty())
                {
                    uint32_t pauseTstates = static_cast<uint32_t>(pauseMs) * 3500;
                    target.edgePulseTimings.push_back(pauseTstates);
                    target.totalBitstreamLength += pauseTstates;
                }
            }
            else
            {
                // Real silence: a control entry whose content is one hold-edge
                TapeBlock block;
                block.blockIndex = image.blocks.size();
                block.type = TAP_BLOCK_FLAG_DATA;
                block.edgePulseTimings.push_back(static_cast<uint32_t>(pauseMs) * 3500);
                block.totalBitstreamLength = static_cast<uint32_t>(pauseMs) * 3500;

                TapeBlockDescriptor descriptor;
                descriptor.kind = TapeBlockKindEnum::Control;
                descriptor.timing.pauseMs = static_cast<uint16_t>(std::min<uint32_t>(pauseMs, 0xFFFF));
                descriptor.playable = true;

                image.blocks.push_back(std::move(block));
                image.descriptors.push_back(std::move(descriptor));
            }
            break;
        }

        case 0x21:  // Group start — label attaches to the next emitted playable block
        {
            context.pendingGroupLabel = entry.text;
            context.hasPendingGroupLabel = true;
            break;  // no block emitted
        }

        case 0x22:  // Group end — clears a pending label (one-shot attach)
        {
            context.pendingGroupLabel.clear();
            context.hasPendingGroupLabel = false;
            break;
        }

        case 0x2A:  // Stop tape if in 48K — inert in v1 (no machine check here)
        {
            TapeBlock block;
            block.blockIndex = image.blocks.size();
            block.type = TAP_BLOCK_FLAG_DATA;

            TapeBlockDescriptor descriptor;
            descriptor.kind = TapeBlockKindEnum::Control;
            descriptor.playable = false;

            image.blocks.push_back(std::move(block));
            image.descriptors.push_back(std::move(descriptor));
            break;
        }

        case 0x2B:  // Set signal level — one-shot polarity for the next block
        {
            context.pendingInvertedLevel = entry.level != 0;
            context.hasPendingInvertedLevel = true;
            break;  // no block emitted
        }

        default:
        {
            // Flow operations reaching emission (linear-tail mode): inert
            // control entries so the catalog keeps the file's structure.
            TapeBlock block;
            block.blockIndex = image.blocks.size();
            block.type = TAP_BLOCK_FLAG_DATA;

            TapeBlockDescriptor descriptor;
            descriptor.kind = TapeBlockKindEnum::Control;
            descriptor.playable = false;

            image.blocks.push_back(std::move(block));
            image.descriptors.push_back(std::move(descriptor));
            break;
        }
    }
}

/// endregion </Emission>

/// region <Pulse helpers>

void LoaderTZX::AppendDirectRecordingRuns(const uint8_t* samples, size_t bitCount, uint16_t tstatesPerSample,
                                          std::vector<uint32_t>& pulses)
{
    // Direct recording (design §5.7 representation 3): the payload is a raw
    // 1-bit sample stream, MSB first. A run of N identical samples is one
    // hold of N * tstatesPerSample T-states — the pulse train carries run
    // lengths, not per-sample edges.
    if (samples == nullptr || bitCount == 0 || tstatesPerSample == 0)
    {
        return;
    }

    uint8_t currentBit = (samples[0] & 0x80) != 0 ? 1 : 0;
    uint64_t runLength = 1;

    for (size_t i = 1; i < bitCount; i++)
    {
        size_t byteIndex = i / 8;
        uint8_t bitMask = static_cast<uint8_t>(0x80 >> (i % 8));
        uint8_t bit = (samples[byteIndex] & bitMask) != 0 ? 1 : 0;

        if (bit == currentBit)
        {
            runLength += 1;
        }
        else
        {
            uint64_t duration = runLength * tstatesPerSample;
            pulses.push_back(static_cast<uint32_t>(std::min<uint64_t>(duration, 0xFFFFFFFFull)));
            currentBit = bit;
            runLength = 1;
        }
    }

    uint64_t finalDuration = runLength * tstatesPerSample;
    pulses.push_back(static_cast<uint32_t>(std::min<uint64_t>(finalDuration, 0xFFFFFFFFull)));
}

bool LoaderTZX::DecodeCswRle16(const uint8_t* data, size_t dataLen, uint32_t pulseCount,
                               std::vector<uint32_t>& pulses)
{
    // CSW v1 RLE (TZX $18 compression 1): a flat sequence of u16 LE sample
    // counts, one per level run; the special value 0 stands for 8192 samples.
    // Returns false when the stream is shorter than the declared pulse count.
    if (data == nullptr)
    {
        return false;
    }

    size_t words = dataLen / 2;
    pulses.reserve(std::min<size_t>(words, pulseCount));

    for (size_t i = 0; i < words; i++)
    {
        uint16_t sampleCount = static_cast<uint16_t>(data[2 * i] | (data[2 * i + 1] << 8));
        pulses.push_back(sampleCount == 0 ? 8192 : sampleCount);
    }

    return dataLen % 2 == 0 && pulses.size() >= static_cast<size_t>(pulseCount);
}

/// endregion </Pulse helpers>

/// region <Helper methods>

void LoaderTZX::parseHardware(uint8_t* data)
{
    /// region <Hardware IDs>

    // The string table lives at file scope — shared with the $33
    // hardwareNote assembler (LookupHardwareEntry above).

    static const char* UNKNOWN_ID = "??";
    /// endregion </Hardware IDs>

    uint8_t* ptr = data;
    uint16_t hardwareRecords = *data;

    for (uint16_t i = 0; i < hardwareRecords; i++)
    {
        uint8_t type_n = *ptr++;
        uint8_t id_n = *ptr++;
        uint8_t value_n = *ptr++;
        const char *type = hardwareIDs;
        const char *ptrID;
        const char *value;

        for (uint16_t j = 0; j < type_n; j++)
        {
            if (!*type)
                break;

            while (*type)
                type++;
            type += 2;
        }

        if (!*type)
        {
            type = UNKNOWN_ID;
            ptrID = UNKNOWN_ID;
            break;
        }
        else
        {
            ptrID = type + strlen(type) + 1;

            for (uint16_t k = 0; k < id_n; k++)
            {
                if (!*ptrID)
                {
                    ptrID = UNKNOWN_ID;
                    break;
                }

                ptrID += strlen(ptrID) + 1;
            }
        }

        switch (value_n)
        {
            case 0: value = "compatible with"; break;
            case 1: value = "uses"; break;
            case 2: value = "compatible, but doesn't use"; break;
            case 3: value = "incompatible with"; break;
            default: value = "??";
        }

        char bf[512];
        snprintf(bf, sizeof(bf), "%s %s: %s", value, type, ptrID);
        //named_cell(bf);
    }
    //named_cell("-");
}

/// endregion </Helper methods>