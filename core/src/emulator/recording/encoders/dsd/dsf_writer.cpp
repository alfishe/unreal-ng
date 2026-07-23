#include "dsf_writer.h"
#include <cstring>

DSFWriter::DSFWriter()
{
}

DSFWriter::~DSFWriter()
{
    if (_file.is_open())
    {
        Close();
    }
}

bool DSFWriter::Open(const std::string& filename,
                     uint32_t sampleRate,
                     uint8_t channels,
                     uint8_t bitsPerSample)
{
    if (_file.is_open())
    {
        Close();
    }

    _filename = filename;
    _sampleRate = sampleRate;
    _channels = channels;
    _bitsPerSample = bitsPerSample;
    _dataSize = 0;
    _blockOffset = 0;
    _interleaveIndex = 0;
    _headerWritten = false;

    // One 4096-byte block buffer per channel
    _channelBlocks.assign(_channels, std::vector<uint8_t>(BLOCK_SIZE, 0));

    _file.open(filename, std::ios::binary | std::ios::trunc);
    if (!_file.is_open())
    {
        return false;
    }

    // Write initial header (will be updated on close)
    WriteHeader();
    WriteFmtChunk();
    WriteDataChunkHeader();

    _headerWritten = true;
    return true;
}

void DSFWriter::WriteHeader()
{
    // DSD chunk (28 bytes)
    WriteChunkId("DSD ");           // 4 bytes: chunk ID
    WriteUint64LE(28);              // 8 bytes: chunk size
    WriteUint64LE(0);               // 8 bytes: total file size (updated on close)
    WriteUint64LE(0);               // 8 bytes: metadata offset (0 = no metadata)
}

void DSFWriter::WriteFmtChunk()
{
    // fmt chunk (52 bytes)
    WriteChunkId("fmt ");           // 4 bytes: chunk ID
    WriteUint64LE(52);              // 8 bytes: chunk size

    WriteUint32LE(1);               // 4 bytes: format version
    WriteUint32LE(0);               // 4 bytes: format ID (0 = DSD raw)

    // Channel type
    uint32_t channelType = 2;       // Default: stereo
    if (_channels == 1) channelType = 1;       // Mono
    else if (_channels == 3) channelType = 3;  // 3 channels
    else if (_channels == 4) channelType = 4;  // Quad
    else if (_channels == 5) channelType = 5;  // 5 channels
    else if (_channels == 6) channelType = 6;  // 5.1
    WriteUint32LE(channelType);     // 4 bytes: channel type

    WriteUint32LE(_channels);       // 4 bytes: channel count
    WriteUint32LE(_sampleRate);     // 4 bytes: sample rate
    WriteUint32LE(_bitsPerSample);  // 4 bytes: bits per sample (always 1 for DSD)
    WriteUint64LE(0);               // 8 bytes: sample count (updated on close)
    WriteUint32LE(BLOCK_SIZE);      // 4 bytes: block size per channel
    WriteUint32LE(0);               // 4 bytes: reserved
}

void DSFWriter::WriteDataChunkHeader()
{
    _dataChunkPos = _file.tellp();

    WriteChunkId("data");           // 4 bytes: chunk ID
    WriteUint64LE(0);               // 8 bytes: chunk size (updated on close)
}

bool DSFWriter::Write(const uint8_t* data, size_t byteCount)
{
    if (!_file.is_open())
        return false;

    // De-interleave [ch0 ch1 ch0 ch1 ...] input into per-channel block
    // buffers; when every channel's block is full, flush them sequentially
    // (DSF block interleaving: 4096 bytes ch0, 4096 bytes ch1, ...)
    for (size_t i = 0; i < byteCount; i++)
    {
        _channelBlocks[_interleaveIndex][_blockOffset] = data[i];

        _interleaveIndex++;
        if (_interleaveIndex == _channels)
        {
            _interleaveIndex = 0;
            _blockOffset++;

            if (_blockOffset == BLOCK_SIZE)
            {
                for (uint8_t ch = 0; ch < _channels; ch++)
                {
                    _file.write(reinterpret_cast<const char*>(_channelBlocks[ch].data()), BLOCK_SIZE);
                    std::memset(_channelBlocks[ch].data(), 0, BLOCK_SIZE);
                }
                _dataSize += static_cast<uint64_t>(BLOCK_SIZE) * _channels;
                _blockOffset = 0;
            }
        }
    }

    return !_file.fail();
}

bool DSFWriter::Write(const std::vector<uint8_t>& data)
{
    return Write(data.data(), data.size());
}

void DSFWriter::Close()
{
    if (!_file.is_open())
        return;

    // Flush partial blocks (zero-padded) — one final block per channel
    if (_blockOffset > 0 || _interleaveIndex > 0)
    {
        for (uint8_t ch = 0; ch < _channels; ch++)
        {
            _file.write(reinterpret_cast<const char*>(_channelBlocks[ch].data()), BLOCK_SIZE);
        }
        _dataSize += static_cast<uint64_t>(BLOCK_SIZE) * _channels;
    }

    FinalizeFile();
    _file.close();
}

void DSFWriter::FinalizeFile()
{
    // Calculate totals
    _totalFileSize = _file.tellp();

    // Update DSD chunk: total file size at offset 12
    _file.seekp(12);
    WriteUint64LE(_totalFileSize);

    // Update fmt chunk sample count. Field layout from file start:
    // fmt starts at 28; id(4) size(8) version(4) formatid(4) chtype(4)
    // chnum(4) rate(4) bits(4) -> sample count lives at 28 + 36 = 64
    uint64_t sampleCount = (_dataSize * 8) / _channels;
    _file.seekp(64);
    WriteUint64LE(sampleCount);

    // Update data chunk: size at offset _dataChunkPos + 4
    _file.seekp(_dataChunkPos + 4);
    WriteUint64LE(_dataSize + 12);  // +12 for chunk header

    // Seek to end
    _file.seekp(0, std::ios::end);
}

uint64_t DSFWriter::GetFileSize() const
{
    if (!_file.is_open())
        return 0;

    auto pos = const_cast<std::ofstream&>(_file).tellp();
    return static_cast<uint64_t>(pos);
}

void DSFWriter::WriteUint32LE(uint32_t value)
{
    uint8_t bytes[4] = {
        static_cast<uint8_t>(value & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 24) & 0xFF)
    };
    _file.write(reinterpret_cast<const char*>(bytes), 4);
}

void DSFWriter::WriteUint64LE(uint64_t value)
{
    uint8_t bytes[8];
    for (int i = 0; i < 8; i++)
    {
        bytes[i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
    _file.write(reinterpret_cast<const char*>(bytes), 8);
}

void DSFWriter::WriteChunkId(const char* id)
{
    _file.write(id, 4);
}
