#include "loaders/rzx/rzxreader.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

#include "common/filehelper.h"

using namespace rzx;

namespace
{
    uint16_t Word(const uint8_t* p)
    {
        return static_cast<uint16_t>(p[0] | (p[1] << 8));
    }

    uint32_t Dword(const uint8_t* p)
    {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }

    /// Up to `size` bytes, stopping at the first NUL; trailing blanks removed
    std::string FixedString(const uint8_t* p, size_t size)
    {
        std::string text;
        for (size_t i = 0; i < size && p[i] != 0; i++)
            text += static_cast<char>(p[i]);
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
            text.pop_back();
        return text;
    }

    std::string Hex(uint32_t value)
    {
        char text[16];
        std::snprintf(text, sizeof(text), "#%02X", value);
        return text;
    }
}  // namespace

bool RzxReader::ParseFile(const std::string& path, File& file, std::string& error)
{
    if (!FileHelper::FileExists(path))
    {
        error = "file not found: " + path;
        return false;
    }
    const size_t size = FileHelper::GetFileSize(path);
    std::vector<uint8_t> bytes(size);
    if (size > 0 && FileHelper::ReadFileToBuffer(path, bytes.data(), size) != size)
    {
        error = "cannot read " + path;
        return false;
    }
    return Parse(bytes.data(), bytes.size(), file, error);
}

bool RzxReader::Parse(const uint8_t* data, size_t size, File& file, std::string& error)
{
    file = File{};
    if (data == nullptr || size < kHeaderSize || std::memcmp(data, "RZX!", 4) != 0)
    {
        error = "not an RZX file (no 'RZX!' signature)";
        return false;
    }

    file.major = data[4];
    file.minor = data[5];
    file.isSigned = (Dword(data + 6) & kFileFlagSigned) != 0;
    if (file.major != 0)
    {
        error = "unsupported RZX version " + file.VersionText() + " (0.x expected)";
        return false;
    }
    if (file.minor < 12)
        file.warnings.push_back("RZX version " + file.VersionText() + " predates 0.12");

    size_t offset = kHeaderSize;
    while (offset < size)
    {
        if (size - offset < kBlockHeaderSize)
        {
            file.warnings.push_back(std::to_string(size - offset) + " trailing bytes after the last block");
            break;
        }
        const uint8_t id = data[offset];
        const uint32_t length = Dword(data + offset + 1);
        if (length < kBlockHeaderSize || length > size - offset)
        {
            error = "block " + Hex(id) + " at offset " + std::to_string(offset) + " has length " +
                    std::to_string(length) + ", " + std::to_string(size - offset) + " bytes left";
            return false;
        }

        const uint8_t* block = data + offset;
        bool ok = true;
        switch (id)
        {
            case kBlockCreator:
                ok = ParseCreator(block, length, file, error);
                break;
            case kBlockSecurityInfo:
                ok = ParseSecurityInfo(block, length, file, error);
                break;
            case kBlockSecuritySignature:
                // Read and reported only: not a trust mechanism (requirements §2)
                file.hasSignature = true;
                break;
            case kBlockSnapshot:
                ok = ParseSnapshot(block, length, file, error);
                break;
            case kBlockInput:
                ok = ParseInput(block, length, file, error);
                break;
            default:
                file.warnings.push_back("unknown block " + Hex(id) + " (" + std::to_string(length) +
                                        " bytes) skipped");
                break;
        }
        if (!ok)
            return false;
        offset += length;
    }

    if (file.inputs.empty())
    {
        error = "no input recording block";
        return false;
    }
    return true;
}

bool RzxReader::ParseCreator(const uint8_t* block, uint32_t length, File& file, std::string& error)
{
    if (length < kCreatorHeaderSize)
    {
        error = "creator block too short (" + std::to_string(length) + " bytes)";
        return false;
    }
    file.hasCreator = true;
    file.creator.name = FixedString(block + 5, 20);
    file.creator.major = Word(block + 25);
    file.creator.minor = Word(block + 27);
    return true;
}

bool RzxReader::ParseSecurityInfo(const uint8_t* block, uint32_t length, File& file, std::string& error)
{
    if (length < kBlockHeaderSize + 8)
    {
        error = "security information block too short (" + std::to_string(length) + " bytes)";
        return false;
    }
    file.hasSecurityInfo = true;
    file.keyId = Dword(block + 5);
    file.weekCode = Dword(block + 9);
    return true;
}

bool RzxReader::ParseSnapshot(const uint8_t* block, uint32_t length, File& file, std::string& error)
{
    if (length < kSnapshotHeaderSize)
    {
        error = "snapshot block too short (" + std::to_string(length) + " bytes)";
        return false;
    }

    Snapshot snapshot;
    const uint32_t flags = Dword(block + 5);
    snapshot.external = (flags & kSnapshotFlagExternal) != 0;
    snapshot.compressed = (flags & kSnapshotFlagCompressed) != 0;
    for (char c : FixedString(block + 9, 4))
        snapshot.extension += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const uint32_t uncompressedLength = Dword(block + 13);

    const uint8_t* payload = block + kSnapshotHeaderSize;
    const size_t payloadSize = length - kSnapshotHeaderSize;
    const std::string where = "snapshot " + std::to_string(file.snapshots.size() + 1);

    // An external descriptor may itself be compressed (the flags are independent)
    std::vector<uint8_t> content;
    if (snapshot.compressed)
    {
        const size_t expected = snapshot.external ? 0 : uncompressedLength;
        if (!snapshot.external && (uncompressedLength == 0 || uncompressedLength > kMaxSnapshotSize))
        {
            error = where + ": uncompressed length " + std::to_string(uncompressedLength) + " out of range";
            return false;
        }
        if (!Inflate(payload, payloadSize, expected, kMaxSnapshotSize, content))
        {
            error = where + ": zlib data does not inflate to " + std::to_string(uncompressedLength) + " bytes";
            return false;
        }
    }
    else
    {
        content.assign(payload, payload + payloadSize);
        if (!snapshot.external && content.size() != uncompressedLength)
            file.warnings.push_back(where + ": " + std::to_string(content.size()) + " bytes stored, header says " +
                                    std::to_string(uncompressedLength));
    }

    if (snapshot.external)
    {
        if (content.size() < 5)
        {
            error = where + ": external descriptor too short";
            return false;
        }
        snapshot.externalChecksum = Dword(content.data());
        snapshot.externalName = FixedString(content.data() + 4, content.size() - 4);
        if (snapshot.externalName.empty())
        {
            error = where + ": external descriptor without a file name";
            return false;
        }
    }
    else
    {
        if (content.empty())
        {
            error = where + ": empty image";
            return false;
        }
        snapshot.data = std::move(content);
    }

    file.order.push_back({BlockType::Snapshot, file.snapshots.size()});
    file.snapshots.push_back(std::move(snapshot));
    return true;
}

bool RzxReader::ParseInput(const uint8_t* block, uint32_t length, File& file, std::string& error)
{
    if (length < kInputHeaderSize)
    {
        error = "input recording block too short (" + std::to_string(length) + " bytes)";
        return false;
    }

    InputBlock input;
    const uint32_t frameCount = Dword(block + 5);
    input.tstates = Dword(block + 10);
    const uint32_t flags = Dword(block + 14);
    input.protectedFrames = (flags & kInputFlagProtected) != 0;
    input.compressed = (flags & kInputFlagCompressed) != 0;

    const uint8_t* payload = block + kInputHeaderSize;
    const size_t payloadSize = length - kInputHeaderSize;
    const std::string where = "input block " + std::to_string(file.inputs.size() + 1);

    // Encrypted frames cannot be read: kept as a count, refused by the player
    if (input.protectedFrames)
    {
        input.frames.resize(0);
        file.warnings.push_back(where + ": protected (encrypted) frames, not playable");
        file.order.push_back({BlockType::Input, file.inputs.size()});
        file.inputs.push_back(std::move(input));
        return true;
    }

    if (input.compressed)
    {
        std::vector<uint8_t> frames;
        if (!Inflate(payload, payloadSize, 0, kMaxInputBlockSize, frames))
        {
            error = where + ": zlib data does not inflate (corrupt, or over " +
                    std::to_string(kMaxInputBlockSize) + " bytes)";
            return false;
        }
        if (!ParseFrames(frames.data(), frames.size(), frameCount, input, file, error))
            return false;
    }
    else if (!ParseFrames(payload, payloadSize, frameCount, input, file, error))
    {
        return false;
    }

    file.order.push_back({BlockType::Input, file.inputs.size()});
    file.inputs.push_back(std::move(input));
    return true;
}

bool RzxReader::ParseFrames(const uint8_t* data, size_t size, uint32_t frameCount, InputBlock& input, File& file,
                            std::string& error)
{
    const std::string where = "input block " + std::to_string(file.inputs.size() + 1);

    // Every frame takes at least 4 bytes: bound the count before reserving
    if (frameCount > size / 4)
    {
        error = where + ": " + std::to_string(frameCount) + " frames cannot fit in " + std::to_string(size) +
                " bytes";
        return false;
    }
    input.frames.reserve(frameCount);
    input.inValues.reserve(size - static_cast<size_t>(frameCount) * 4);

    size_t offset = 0;
    Frame previous;  // a repeat at the block start repeats an empty frame (as SkoolKit)
    for (uint32_t i = 0; i < frameCount; i++)
    {
        if (size - offset < 4)
        {
            error = where + ", frame " + std::to_string(i) + ": truncated";
            return false;
        }
        Frame frame;
        frame.fetchCount = Word(data + offset);
        const uint16_t inCounter = Word(data + offset + 2);
        offset += 4;

        if (inCounter == kRepeatFrame)
        {
            frame.inCount = previous.inCount;
            frame.inOffset = previous.inOffset;
            frame.repeated = true;
        }
        else
        {
            if (size - offset < inCounter)
            {
                error = where + ", frame " + std::to_string(i) + ": " + std::to_string(inCounter) +
                        " IN values, " + std::to_string(size - offset) + " bytes left";
                return false;
            }
            frame.inCount = inCounter;
            frame.inOffset = static_cast<uint32_t>(input.inValues.size());
            input.inValues.insert(input.inValues.end(), data + offset, data + offset + inCounter);
            offset += inCounter;
        }
        input.frames.push_back(frame);
        previous = frame;
    }

    if (offset != size)
        file.warnings.push_back(where + ": " + std::to_string(size - offset) + " bytes after the last frame");
    return true;
}
