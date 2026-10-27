#pragma once

/// @file nextpipeer.h
/// @brief The Raspberry Pi accelerator's end of the Next's UART 1, at the level the NextZXOS tools use it: the NextPi supervisor
/// (docs/inprogress/2026-10-07-zx-next/research-pi-accelerator.md).
///
/// The Pi runs Linux with the NextPi supervisor as the getty on the UART: bash with the `nextpi-*` commands on the PATH, prompt
/// `SUP> `, tty echo. This peer answers what the Next's tools need and nothing more: the line editor with echo, ^C / ^D,
/// `nextpi-admin_version` (.PIVER), `nextpi-file_receive -nbn` and `nextpi-file_transmit -nbn` (.PIPUT / .PIGET: the NBN blocks of
/// 16 KB with an 8-bit sum, '!' / '?' answers, 3 retries on the Next's side) over an in-memory card. Everything else a real Pi
/// does (playing, tape loading, HDMI) is out of reach: a command it does not know answers like bash.

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "emulator/io/serial/serialpeer.h"

class NextPiPeer final : public ISerialPeer
{
public:
    static constexpr unsigned kBlockSize = 16384;
    static constexpr const char* kPrompt = "SUP> ";
    static constexpr const char* kVersion = "1.93D";

    // ISerialPeer
    void Transmit(uint8_t byte) override;
    bool HasByte() const override { return !_out.empty(); }
    uint8_t TakeByte() override
    {
        const uint8_t b = _out.front();
        _out.pop_front();
        return b;
    }
    size_t Pending() const override { return _out.size(); }
    void Reset() override;
    const char* Kind() const override { return "nextpi"; }

    /// The card the NBN commands read and write (name as the Next typed it)
    const std::map<std::string, std::vector<uint8_t>>& Files() const { return _files; }
    void PutFile(const std::string& name, std::vector<uint8_t> data) { _files[name] = std::move(data); }
    /// The supervisor is waiting for a command line
    bool AtPrompt() const { return _mode == Mode::Shell; }

private:
    enum class Mode : uint8_t
    {
        Shell,
        RecvHeader,     ///< nextpi-file_receive: u32 size, u32 blocks, u16 remainder, the name and a 0
        RecvBlock,      ///< the payload of a block
        RecvChecksum,   ///< the sum byte
        SendPath,       ///< nextpi-file_transmit: 0, the path, 0
        SendBlocks      ///< the Next's status byte for the previous block asks for the next one
    };

    void Out(const char* text);
    void Out(uint8_t byte) { _out.push_back(byte); }
    void Prompt() { Out(kPrompt); }
    void Execute(const std::string& line);
    void BeginReceive();
    void BeginTransmit();
    void FinishReceive();
    void SendBlock(unsigned index);
    unsigned BlockLength(unsigned index) const { return index < _blocks ? kBlockSize : _remainder; }

    std::deque<uint8_t> _out;
    std::map<std::string, std::vector<uint8_t>> _files;
    Mode _mode = Mode::Shell;
    std::string _line;
    // the NBN transfer in flight
    std::vector<uint8_t> _header;
    std::vector<uint8_t> _data;       ///< the file: being received, or being sent
    std::vector<uint8_t> _block;
    std::string _name;
    uint32_t _size = 0, _blocks = 0, _remainder = 0;
    unsigned _index = 0;              ///< the block in progress
    bool _pathTerminated = false;   ///< the partition byte of nextpi-file_transmit has come
    bool _awaitingFirst = false;    ///< transmit: the Next's one byte after the header
    uint8_t _partition = 0;
    unsigned _checksumModulus = 256;
};
