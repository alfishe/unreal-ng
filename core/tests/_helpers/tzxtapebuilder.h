#ifndef TZXTAPEBUILDER_H
#define TZXTAPEBUILDER_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

/// Fluent TZX image builder for tests (tape-manager design §5.6 test pyramid).
///
/// Every Add* method appends one spec-1.20/1.21 block with correct framing;
/// the Put* helpers expose raw little-endian writes for malformed-input
/// tests. The constructor seeds a valid "ZXTape!" 1.20 header so a test only
/// describes the blocks it cares about.
///
/// Layout reference: docs/file-formats/tape-images/tzx-tape.md — all
/// multi-byte fields little-endian, just like LoaderTZX reads them.
class TzxTapeBuilder
{
public:
    TzxTapeBuilder()
    {
        _bytes = { 'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 0x01, 0x14 };
    }

    /// region <Raw writes>

    TzxTapeBuilder& Put8(uint8_t value)
    {
        _bytes.push_back(value);
        return *this;
    }

    TzxTapeBuilder& Put16(uint16_t value)
    {
        return Put8(static_cast<uint8_t>(value & 0xFF)).Put8(static_cast<uint8_t>(value >> 8));
    }

    TzxTapeBuilder& Put24(uint32_t value)
    {
        return Put16(static_cast<uint16_t>(value & 0xFFFF)).Put8(static_cast<uint8_t>(value >> 16));
    }

    TzxTapeBuilder& Put32(uint32_t value)
    {
        return Put24(value & 0xFFFFFF).Put8(static_cast<uint8_t>(value >> 24));
    }

    TzxTapeBuilder& PutBytes(const std::vector<uint8_t>& values)
    {
        _bytes.insert(_bytes.end(), values.begin(), values.end());
        return *this;
    }

    TzxTapeBuilder& PutString(const std::string& text)
    {
        for (char c : text)
        {
            Put8(static_cast<uint8_t>(c));
        }
        return *this;
    }

    /// Overwrite the version bytes of the seeded header
    TzxTapeBuilder& SetVersion(uint8_t major, uint8_t minor)
    {
        _bytes[8] = major;
        _bytes[9] = minor;
        return *this;
    }

    /// endregion </Raw writes>

    /// region <Playable blocks>

    /// $10 Standard speed data: [pause:2][len:2][data]
    TzxTapeBuilder& AddStandardBlock(uint16_t pauseMs, const std::vector<uint8_t>& data)
    {
        Put8(0x10).Put16(pauseMs).Put16(static_cast<uint16_t>(data.size()));
        return PutBytes(data);
    }

    /// $11 Turbo speed data — 18-byte parameter header
    TzxTapeBuilder& AddTurboBlock(uint16_t pilotHalfPeriod, uint16_t sync1, uint16_t sync2,
                                  uint16_t zeroHalfPeriod, uint16_t oneHalfPeriod, uint16_t pilotPulses,
                                  uint8_t bitsInLastByte, uint16_t pauseMs, const std::vector<uint8_t>& data)
    {
        Put8(0x11)
                .Put16(pilotHalfPeriod).Put16(sync1).Put16(sync2)
                .Put16(zeroHalfPeriod).Put16(oneHalfPeriod).Put16(pilotPulses)
                .Put8(bitsInLastByte).Put16(pauseMs).Put24(static_cast<uint32_t>(data.size()));
        return PutBytes(data);
    }

    /// $12 Pure tone: [period:2][pulses:2]
    TzxTapeBuilder& AddPureTone(uint16_t period, uint16_t pulses)
    {
        return Put8(0x12).Put16(period).Put16(pulses);
    }

    /// $13 Pulse sequence: [count:1][period:2 x count]
    TzxTapeBuilder& AddPulseSequence(const std::vector<uint16_t>& periods)
    {
        Put8(0x13).Put8(static_cast<uint8_t>(periods.size()));
        for (uint16_t period : periods)
        {
            Put16(period);
        }
        return *this;
    }

    /// $14 Pure data (raw bits, no flag/checksum framing)
    TzxTapeBuilder& AddPureData(uint16_t zeroHalfPeriod, uint16_t oneHalfPeriod, uint8_t bitsInLastByte,
                                uint16_t pauseMs, const std::vector<uint8_t>& bits)
    {
        Put8(0x14).Put16(zeroHalfPeriod).Put16(oneHalfPeriod).Put8(bitsInLastByte).Put16(pauseMs)
                .Put24(static_cast<uint32_t>(bits.size()));
        return PutBytes(bits);
    }

    /// $15 Direct recording: [tstates:2][pause:2][bits:1][len:3][samples]
    TzxTapeBuilder& AddDirectRecording(uint16_t tstatesPerSample, uint16_t pauseMs, uint8_t bitsInLastByte,
                                       const std::vector<uint8_t>& samples)
    {
        Put8(0x15).Put16(tstatesPerSample).Put16(pauseMs).Put8(bitsInLastByte)
                .Put24(static_cast<uint32_t>(samples.size()));
        return PutBytes(samples);
    }

    /// $18 CSW recording: [len:4][pause:2][rate:3][compression:1][pulses:4][data]
    TzxTapeBuilder& AddCswRecording(uint16_t pauseMs, uint32_t sampleRate, uint8_t compression,
                                    uint32_t declaredPulses, const std::vector<uint8_t>& data)
    {
        Put8(0x18).Put32(static_cast<uint32_t>(data.size())).Put16(pauseMs)
                .Put24(sampleRate).Put8(compression).Put32(declaredPulses);
        return PutBytes(data);
    }

    /// $18 helper — CSW v1 RLE: flat u16 LE sample counts, 0 = 8192 samples
    TzxTapeBuilder& AddCswRle(uint16_t pauseMs, uint32_t sampleRate, const std::vector<uint16_t>& sampleCounts)
    {
        std::vector<uint8_t> data;
        data.reserve(sampleCounts.size() * 2);
        for (uint16_t count : sampleCounts)
        {
            data.push_back(static_cast<uint8_t>(count & 0xFF));
            data.push_back(static_cast<uint8_t>(count >> 8));
        }
        return AddCswRecording(pauseMs, sampleRate, 1, static_cast<uint32_t>(sampleCounts.size()), data);
    }

    /// $19 Generalized data: [len:4][body] — catalog-only in this build
    TzxTapeBuilder& AddGeneralizedData(const std::vector<uint8_t>& body)
    {
        Put8(0x19).Put32(static_cast<uint32_t>(body.size()));
        return PutBytes(body);
    }

    /// endregion </Playable blocks>

    /// region <Control flow>

    /// $20 Pause (0 = stop-the-tape marker)
    TzxTapeBuilder& AddPause(uint16_t pauseMs)
    {
        return Put8(0x20).Put16(pauseMs);
    }

    /// $21 Group start: [len:1][name]
    TzxTapeBuilder& AddGroupStart(const std::string& name)
    {
        Put8(0x21).Put8(static_cast<uint8_t>(name.size()));
        return PutString(name);
    }

    /// $22 Group end
    TzxTapeBuilder& AddGroupEnd()
    {
        return Put8(0x22);
    }

    /// $23 Jump — offset relative to this block, +1 = the next block
    TzxTapeBuilder& AddJump(int16_t offset)
    {
        return Put8(0x23).Put16(static_cast<uint16_t>(offset));
    }

    /// $24 Loop start — count = TOTAL body executions
    TzxTapeBuilder& AddLoopStart(uint16_t count)
    {
        return Put8(0x24).Put16(count);
    }

    /// $25 Loop end
    TzxTapeBuilder& AddLoopEnd()
    {
        return Put8(0x25);
    }

    /// $26 Call sequence: [count:2][offsets:2s x count] — offsets relative to this block
    TzxTapeBuilder& AddCallSequence(const std::vector<int16_t>& offsets)
    {
        Put8(0x26).Put16(static_cast<uint16_t>(offsets.size()));
        for (int16_t offset : offsets)
        {
            Put16(static_cast<uint16_t>(offset));
        }
        return *this;
    }

    /// $27 Return
    TzxTapeBuilder& AddReturn()
    {
        return Put8(0x27);
    }

    /// $28 Select: [length:2][count:1]{[offset:2s][descLen:1][text]}
    TzxTapeBuilder& AddSelect(const std::vector<std::pair<int16_t, std::string>>& selections)
    {
        size_t length = 1;  // the count byte
        for (const auto& selection : selections)
        {
            length += 3 + selection.second.size();
        }

        Put8(0x28).Put16(static_cast<uint16_t>(length)).Put8(static_cast<uint8_t>(selections.size()));
        for (const auto& selection : selections)
        {
            Put16(static_cast<uint16_t>(selection.first))
                    .Put8(static_cast<uint8_t>(selection.second.size()));
            PutString(selection.second);
        }
        return *this;
    }

    /// $2A Stop tape if in 48K: [u32 = 0]
    TzxTapeBuilder& AddStopIf48K()
    {
        return Put8(0x2A).Put32(0);
    }

    /// $2B Set signal level: [len:4 = 1][level:1]
    TzxTapeBuilder& AddSetSignalLevel(uint8_t level)
    {
        return Put8(0x2B).Put32(1).Put8(static_cast<uint8_t>(level & 1));
    }

    /// endregion </Control flow>

    /// region <Metadata>

    /// $30 Text description: [len:1][text]
    TzxTapeBuilder& AddText(const std::string& text)
    {
        Put8(0x30).Put8(static_cast<uint8_t>(text.size()));
        return PutString(text);
    }

    /// $31 Message: [time:1][len:1][text]
    TzxTapeBuilder& AddMessage(uint8_t afterSeconds, const std::string& text)
    {
        Put8(0x31).Put8(afterSeconds).Put8(static_cast<uint8_t>(text.size()));
        return PutString(text);
    }

    /// $32 Archive info: [len:2][count:1]{[id:1][len:1][text]}
    TzxTapeBuilder& AddArchiveInfo(const std::vector<std::pair<uint8_t, std::string>>& entries)
    {
        size_t length = 1;  // the count byte
        for (const auto& entry : entries)
        {
            length += 2 + entry.second.size();
        }

        Put8(0x32).Put16(static_cast<uint16_t>(length)).Put8(static_cast<uint8_t>(entries.size()));
        for (const auto& entry : entries)
        {
            Put8(entry.first).Put8(static_cast<uint8_t>(entry.second.size()));
            PutString(entry.second);
        }
        return *this;
    }

    /// $33 Hardware type: [count:1]{[type:1][id:1][value:1]}
    struct HardwareRecord
    {
        uint8_t type;
        uint8_t id;
        uint8_t value;
    };

    TzxTapeBuilder& AddHardwareInfo(const std::vector<HardwareRecord>& records)
    {
        Put8(0x33).Put8(static_cast<uint8_t>(records.size()));
        for (const HardwareRecord& record : records)
        {
            Put8(record.type).Put8(record.id).Put8(record.value);
        }
        return *this;
    }

    /// $35 Custom info: [ASCII:16][len:4][data]
    TzxTapeBuilder& AddCustomInfo(const std::string& description, const std::vector<uint8_t>& data)
    {
        Put8(0x35);
        for (size_t i = 0; i < 16; i++)
        {
            Put8(i < description.size() ? static_cast<uint8_t>(description[i]) : 0);
        }
        Put32(static_cast<uint32_t>(data.size()));
        return PutBytes(data);
    }

    /// $5A Glue: "XTape!" 0x1A major minor — framing only
    TzxTapeBuilder& AddGlue()
    {
        Put8(0x5A);
        return PutBytes({ 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 0x01, 0x14 });
    }

    /// Deprecated block ($16/$17/$34/$40): general extension rule [len:4][body]
    TzxTapeBuilder& AddDeprecatedBlock(uint8_t blockId, const std::vector<uint8_t>& body)
    {
        Put8(blockId).Put32(static_cast<uint32_t>(body.size()));
        return PutBytes(body);
    }

    /// endregion </Metadata>

    const std::vector<uint8_t>& Bytes() const
    {
        return _bytes;
    }

    /// Write to a scratch path — integration tests load through the filesystem
    static bool WriteToFile(const std::vector<uint8_t>& bytes, const std::string& path)
    {
        FILE* file = fopen(path.c_str(), "wb");
        if (file == nullptr)
        {
            return false;
        }

        const size_t written = fwrite(bytes.data(), 1, bytes.size(), file);
        fclose(file);
        return written == bytes.size();
    }

private:
    std::vector<uint8_t> _bytes;
};

#endif // TZXTAPEBUILDER_H
