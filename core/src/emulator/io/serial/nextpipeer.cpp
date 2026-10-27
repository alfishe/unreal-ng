#include "emulator/io/serial/nextpipeer.h"

#include <cstdlib>
#include <cstring>

namespace
{
constexpr uint8_t kCtrlC = 0x03, kCtrlD = 0x04;
constexpr uint8_t kBlockOk = '!', kBlockFail = '?';

uint8_t Sum(const std::vector<uint8_t>& v)
{
    uint8_t s = 0;
    for (uint8_t b : v)
        s = static_cast<uint8_t>(s + b);
    return s;
}
}  // namespace

void NextPiPeer::Reset()
{
    _out.clear();
    _mode = Mode::Shell;
    _line.clear();
    _header.clear();
    _data.clear();
    _block.clear();
    _name.clear();
}

void NextPiPeer::Out(const char* text)
{
    for (; *text; text++)
        _out.push_back(static_cast<uint8_t>(*text));
}

void NextPiPeer::Transmit(uint8_t byte)
{
    switch (_mode)
    {
        case Mode::Shell:
        {
            if (byte == kCtrlC)
            {
                Out("^C\r\n");
                _line.clear();
                Prompt();
            }
            else if (byte == kCtrlD)
            {
                // an empty line's ^D ends the shell; the supervisor starts a new one
                _line.clear();
                Out("\r\n");
                Prompt();
            }
            else if (byte == '\n' || byte == '\r')
            {
                Out("\r\n");  // the tty echo of the newline
                const std::string line = _line;
                _line.clear();
                Execute(line);
            }
            else if (byte == 0x7F || byte == 0x08)
            {
                if (!_line.empty())
                {
                    _line.pop_back();
                    Out("\b \b");
                }
            }
            else
            {
                _line.push_back(static_cast<char>(byte));
                Out(byte);  // echo
            }
            break;
        }
        case Mode::RecvHeader:
        {
            _header.push_back(byte);
            // 10 bytes of numbers (big endian), then the name up to a 0
            if (_header.size() > 10 && byte == 0)
            {
                _size = (uint32_t(_header[0]) << 24) | (uint32_t(_header[1]) << 16) | (uint32_t(_header[2]) << 8) | _header[3];
                _blocks = (uint32_t(_header[4]) << 24) | (uint32_t(_header[5]) << 16) | (uint32_t(_header[6]) << 8) | _header[7];
                _remainder = (uint32_t(_header[8]) << 8) | _header[9];
                _name.assign(_header.begin() + 10, _header.end() - 1);
                _data.clear();
                _data.reserve(_size);
                _index = 0;
                _block.clear();
                // nextpi-file_receive: (blocks * 16384) + remainder must be the size; then a second OK
                if (static_cast<uint64_t>(_blocks) * kBlockSize + _remainder != _size)
                {
                    Out("Size mismatch ERROR\n");
                    _mode = Mode::Shell;
                    Prompt();
                }
                else
                {
                    Out("OK\n");
                    _mode = BlockLength(0) == 0 ? Mode::RecvChecksum : Mode::RecvBlock;
                }
            }
            break;
        }
        case Mode::RecvBlock:
            _block.push_back(byte);
            if (_block.size() >= BlockLength(_index))
                _mode = Mode::RecvChecksum;
            break;
        case Mode::RecvChecksum:
        {
            if (byte == static_cast<uint8_t>(Sum(_block) % _checksumModulus))
            {
                _data.insert(_data.end(), _block.begin(), _block.end());
                Out(kBlockOk);
                _index++;
                _block.clear();
                if (_index > _blocks)  // the remainder block follows the full ones, even when it is empty
                    FinishReceive();
                else
                    _mode = BlockLength(_index) == 0 ? Mode::RecvChecksum : Mode::RecvBlock;
            }
            else
            {
                Out(kBlockFail);  // the Next resends the block
                _block.clear();
                _mode = BlockLength(_index) == 0 ? Mode::RecvChecksum : Mode::RecvBlock;
            }
            break;
        }
        case Mode::SendPath:
        {
            // the partition (0 "/", 1 "/ram/", 2 "/NextPi/"), then the name and a 0 (nextpi-file_transmit: dirs[partition] + filename)
            if (!_pathTerminated)
            {
                _partition = byte;
                _pathTerminated = true;
                break;
            }
            if (byte != 0)
            {
                _name.push_back(static_cast<char>(byte));
                break;
            }
            static const char* const kDirs[3] = {"/", "/ram/", "/NextPi/"};
            const auto found = _partition < 3 ? _files.find(std::string(kDirs[_partition]) + _name) : _files.end();
            _data = found != _files.end() ? found->second : std::vector<uint8_t>();
            _size = static_cast<uint32_t>(_data.size());
            if (_size == 0)
            {
                // a missing or empty file: four zero bytes end the transfer (the script exits; the prompt follows)
                for (int i = 0; i < 4; i++)
                    Out(uint8_t(0));
                _mode = Mode::Shell;
                Prompt();
                break;
            }
            _blocks = _size / kBlockSize;
            _remainder = _size % kBlockSize;
            const uint8_t header[10] = {uint8_t(_size >> 24), uint8_t(_size >> 16), uint8_t(_size >> 8), uint8_t(_size),
                                        uint8_t(_blocks >> 24), uint8_t(_blocks >> 16), uint8_t(_blocks >> 8), uint8_t(_blocks),
                                        uint8_t(_remainder >> 8), uint8_t(_remainder)};
            for (uint8_t b : header)
                Out(b);
            _index = 0;
            _awaitingFirst = true;  // one byte from the Next (its header check), then the blocks
            _mode = Mode::SendBlocks;
            break;
        }
        case Mode::SendBlocks:
        {
            if (_awaitingFirst)
            {
                _awaitingFirst = false;
                SendBlock(_index);  // the first block
                break;
            }
            // the answer to the block just sent: '!' goes on, anything else sends it again
            if (byte != kBlockOk)
            {
                SendBlock(_index);
                break;
            }
            _index++;
            if (_index > _blocks)  // the remainder block (even an empty one) was the last
            {
                _mode = Mode::Shell;
                Prompt();
            }
            else
                SendBlock(_index);
            break;
        }
    }
}

void NextPiPeer::SendBlock(unsigned index)
{
    const size_t from = static_cast<size_t>(index) * kBlockSize;
    const size_t length = BlockLength(index);
    uint8_t sum = 0;
    for (size_t i = 0; i < length; i++)
    {
        const uint8_t b = from + i < _data.size() ? _data[from + i] : 0;
        sum = static_cast<uint8_t>(sum + b);
        Out(b);
    }
    Out(sum);
}

void NextPiPeer::FinishReceive()
{
    _files["/ram/" + _name] = _data;
    _mode = Mode::Shell;
    Prompt();
}

void NextPiPeer::Execute(const std::string& text)
{
    // trim
    size_t a = text.find_first_not_of(" \t");
    const size_t b = text.find_last_not_of(" \t");
    const std::string line = a == std::string::npos ? std::string() : text.substr(a, b - a + 1);
    if (line.empty())
    {
        Prompt();
        return;
    }
    const std::string command = line.substr(0, line.find(' '));
    const bool nbn = line.find("-nbn") != std::string::npos;
    if (command == "nextpi-admin_version")
    {
        Out(kVersion);
        Out("\r\n");
        Prompt();
    }
    else if (command == "nextpi-file_receive" && nbn)
    {
        _checksumModulus = 256;
        const size_t at = line.find("-nbn");
        const unsigned value = static_cast<unsigned>(std::atoi(line.c_str() + at + 4));
        if (value >= 1 && value <= 256)
            _checksumModulus = value;
        BeginReceive();
    }
    else if (command == "nextpi-file_transmit" && nbn)
        BeginTransmit();
    else if (command.rfind("nextpi-", 0) == 0)
    {
        Out("ERROR:2 not implemented by this peer\r\n");
        Prompt();
    }
    else
    {
        Out("bash: ");
        Out(command.c_str());
        Out(": command not found\r\n");
        Prompt();
    }
}

void NextPiPeer::BeginReceive()
{
    Out("OK\n");  // the script runs the terminal raw: no CR
    _header.clear();
    _mode = Mode::RecvHeader;
}

void NextPiPeer::BeginTransmit()
{
    Out("OK\n");
    _name.clear();
    _pathTerminated = false;
    _mode = Mode::SendPath;
}
