#include "stdafx.h"

#include "loaders/tape/writer_tap.h"

#include "common/filehelper.h"

/// region <Documentation>

/// See writer_tap.h — the framing walk LoaderTAP::Load() parses back:
/// [u16 len][flag ... checksum] per block, nothing else.

/// endregion </Documentation>

bool TapArchiveWriter::IsExportable(const TapeImage& image, std::string* reason)
{
    for (size_t i = 0; i < image.blocks.size(); i++)
    {
        const TapeBlock& block = image.blocks[i];
        const TapeBlockDescriptor* descriptor = i < image.descriptors.size() ? &image.descriptors[i] : nullptr;
        const char* kindName = descriptor != nullptr ? getTapeBlockKindName(descriptor->kind) : "unknown";

        if (block.data.empty() || block.timing.has_value())
        {
            if (reason != nullptr)
            {
                *reason = "block " + std::to_string(i) + " (" + kindName +
                          ") is not a ROM-standard byte block — save as .tzx to keep its exact pulses";
            }
            return false;
        }
        if (block.data.size() > 0xFFFF)
        {
            if (reason != nullptr)
            {
                *reason = "block " + std::to_string(i) + " is " + std::to_string(block.data.size()) +
                          " bytes — beyond the TAP u16 length limit; save as .tzx";
            }
            return false;
        }
    }
    return true;
}

std::vector<uint8_t> TapArchiveWriter::Write(const TapeImage& image)
{
    std::vector<uint8_t> out;
    size_t total = 0;
    for (const TapeBlock& block : image.blocks)
    {
        total += 2 + block.data.size();
    }
    out.reserve(total);

    for (const TapeBlock& block : image.blocks)
    {
        out.push_back(static_cast<uint8_t>(block.data.size() & 0xFF));
        out.push_back(static_cast<uint8_t>(block.data.size() >> 8));
        out.insert(out.end(), block.data.begin(), block.data.end());
    }

    return out;
}

bool TapArchiveWriter::Save(const TapeImage& image, const std::string& path, std::string& errorText)
{
    std::string reason;
    if (!IsExportable(image, &reason))
    {
        errorText = reason;
        return false;
    }

    std::vector<uint8_t> bytes = Write(image);  // non-const: FileHelper takes uint8_t*
    if (!FileHelper::SaveBufferToFile(path, bytes.data(), bytes.size()))
    {
        errorText = "cannot write '" + path + "'";
        return false;
    }
    return true;
}
