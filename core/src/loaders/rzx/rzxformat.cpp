#include "loaders/rzx/rzxformat.h"

#include <algorithm>

#include "3rdparty/miniz/miniz.h"

namespace rzx
{
    uint64_t File::TotalFrames() const
    {
        uint64_t total = 0;
        for (const InputBlock& block : inputs)
            total += block.frames.size();
        return total;
    }

    std::string File::VersionText() const
    {
        return std::to_string(major) + "." + (minor < 10 ? "0" : "") + std::to_string(minor);
    }

    std::string File::Describe() const
    {
        std::string text = "RZX " + VersionText() + (isSigned ? " (signed)" : "") + "\n";
        if (hasCreator)
            text += "Creator: " + creator.name + " " + std::to_string(creator.major) + "." +
                    std::to_string(creator.minor) + "\n";
        for (const BlockRef& ref : order)
        {
            if (ref.type == BlockType::Snapshot)
            {
                const Snapshot& snapshot = snapshots[ref.index];
                text += "Snapshot: " + snapshot.extension;
                if (snapshot.external)
                    text += ", external '" + snapshot.externalName + "'";
                else
                    text += ", " + std::to_string(snapshot.data.size()) + " bytes" +
                            (snapshot.compressed ? " (compressed)" : "");
                text += "\n";
            }
            else
            {
                const InputBlock& input = inputs[ref.index];
                text += "Input: " + std::to_string(input.frames.size()) + " frames, " +
                        std::to_string(input.inValues.size()) + " IN values, T-states " +
                        std::to_string(input.tstates) + (input.compressed ? " (compressed)" : "") +
                        (input.protectedFrames ? " (protected)" : "") + "\n";
            }
        }
        for (const std::string& warning : warnings)
            text += "Warning: " + warning + "\n";
        return text;
    }

    bool Inflate(const uint8_t* data, size_t size, size_t expected, size_t limit, std::vector<uint8_t>& out)
    {
        out.clear();
        if (expected > limit)
            return false;

        mz_stream stream{};
        if (mz_inflateInit(&stream) != MZ_OK)
            return false;
        stream.next_in = data;
        stream.avail_in = static_cast<unsigned int>(size);

        // Known size: one buffer of exactly that size. Unknown: grow in steps up
        // to the limit (an input block is typically 10-20 times its stream)
        size_t capacity = expected > 0 ? expected : std::min(limit, std::max<size_t>(size * 8, 4096));
        out.resize(capacity);
        size_t produced = 0;
        int status = MZ_OK;
        for (;;)
        {
            stream.next_out = out.data() + produced;
            stream.avail_out = static_cast<unsigned int>(capacity - produced);
            status = mz_inflate(&stream, MZ_NO_FLUSH);
            produced = capacity - stream.avail_out;
            if (status == MZ_STREAM_END)
                break;
            if (status != MZ_OK && status != MZ_BUF_ERROR)
                break;
            if (stream.avail_out > 0)
            {
                // Output room left yet no progress: the input ended early
                if (stream.avail_in == 0)
                    break;
                continue;
            }
            // Output full: a known size is exceeded; an unknown one grows
            if (expected > 0 || capacity >= limit)
            {
                status = MZ_DATA_ERROR;
                break;
            }
            capacity = std::min(limit, capacity * 2);
            out.resize(capacity);
        }
        mz_inflateEnd(&stream);

        if (status != MZ_STREAM_END || (expected > 0 && produced != expected))
        {
            out.clear();
            return false;
        }
        out.resize(produced);
        return true;
    }

    std::vector<uint8_t> Deflate(const uint8_t* data, size_t size)
    {
        mz_ulong length = mz_compressBound(static_cast<mz_ulong>(size));
        std::vector<uint8_t> out(length);
        if (mz_compress2(out.data(), &length, data, static_cast<mz_ulong>(size), MZ_BEST_COMPRESSION) != MZ_OK)
            return {};
        out.resize(length);
        return out;
    }
}  // namespace rzx
