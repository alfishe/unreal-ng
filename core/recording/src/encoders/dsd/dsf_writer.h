#pragma once

#include "dsd_types.h"
#include <string>
#include <fstream>
#include <vector>
#include <cstdint>

/// DSF (DSD Stream File) writer - Sony's standard DSD container format
/// Compatible with most high-end DACs and players
class DSFWriter
{
public:
    DSFWriter();
    ~DSFWriter();

    /// Open file for writing
    /// @param filename Output file path (.dsf)
    /// @param sampleRate DSD sample rate in Hz (e.g., 2822400 for DSD64)
    /// @param channels Number of audio channels (1-6)
    /// @param bitsPerSample Always 1 for DSD
    /// @return true on success
    bool Open(const std::string& filename,
              uint32_t sampleRate,
              uint8_t channels,
              uint8_t bitsPerSample = 1);

    /// Write DSD data block
    /// @param data DSD samples, byte-interleaved by channel
    ///             [ch0 ch1 ch0 ch1 ...], MSB first within each byte.
    ///             De-interleaved internally into DSF's per-channel
    ///             4096-byte blocks.
    /// @param byteCount Number of bytes to write
    /// @return true on success
    bool Write(const uint8_t* data, size_t byteCount);

    /// Write DSD data from vector
    bool Write(const std::vector<uint8_t>& data);

    /// Close file (finalizes headers)
    void Close();

    /// Check if file is open
    bool IsOpen() const { return _file.is_open(); }

    /// Get total bytes written
    uint64_t GetBytesWritten() const { return _dataSize; }

    /// Get output file size
    uint64_t GetFileSize() const;

    /// Set metadata (must be called before first Write)
    void SetTitle(const std::string& title) { _title = title; }
    void SetArtist(const std::string& artist) { _artist = artist; }
    void SetAlbum(const std::string& album) { _album = album; }

private:
    void WriteHeader();
    void WriteFmtChunk();
    void WriteDataChunkHeader();
    void FinalizeFile();

    // Write helpers
    void WriteUint32LE(uint32_t value);
    void WriteUint64LE(uint64_t value);
    void WriteChunkId(const char* id);

    std::ofstream _file;
    std::string _filename;

    // Format info
    uint32_t _sampleRate = 0;
    uint8_t _channels = 0;
    uint8_t _bitsPerSample = 1;

    // File structure positions
    uint64_t _dataChunkPos = 0;
    uint64_t _dataSize = 0;
    uint64_t _totalFileSize = 0;

    // DSF stores audio as 4096-byte blocks PER CHANNEL: block of ch0,
    // block of ch1, ... — so incoming byte-interleaved data is
    // de-interleaved into one block buffer per channel and all channel
    // blocks are flushed together when full.
    static constexpr size_t BLOCK_SIZE = 4096;
    std::vector<std::vector<uint8_t>> _channelBlocks;  // [channel][BLOCK_SIZE]
    size_t _blockOffset = 0;      // Bytes filled per channel block
    size_t _interleaveIndex = 0;  // Which channel the next incoming byte belongs to

    // Metadata
    std::string _title;
    std::string _artist;
    std::string _album;

    bool _headerWritten = false;
};
