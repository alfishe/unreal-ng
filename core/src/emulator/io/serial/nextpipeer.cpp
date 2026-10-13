#include "emulator/io/serial/nextpipeer.h"

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
                if (_size == 0 || BlocksTotal() == 0)
                    FinishReceive();
                else
                    _mode = Mode::RecvBlock;
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
            if (byte == Sum(_block))
            {
                _data.insert(_data.end(), _block.begin(), _block.end());
                Out(kBlockOk);
                _index++;
                _block.clear();
                if (_index >= BlocksTotal())
                    FinishReceive();
                else
                    _mode = Mode::RecvBlock;
            }
            else
            {
                Out(kBlockFail);  // the Next resends the block
                _block.clear();
                _mode = Mode::RecvBlock;
            }
            break;
        }
        case Mode::SendPath:
        {
            // a 0, the path, a 0
            if (_name.empty() && byte == 0 && !_pathTerminated)
            {
                _pathTerminated = true;  // the opening 0
                break;
            }
            if (byte != 0)
            {
                _name.push_back(static_cast<char>(byte));
                break;
            }
            const auto found = _files.find(_name);
            _data = found != _files.end() ? found->second : std::vector<uint8_t>();
            _size = static_cast<uint32_t>(_data.size());
            _blocks = _size / kBlockSize;
            _remainder = _size % kBlockSize;
            const uint8_t header[10] = {uint8_t(_size >> 24), uint8_t(_size >> 16), uint8_t(_size >> 8), uint8_t(_size),
                                        uint8_t(_blocks >> 24), uint8_t(_blocks >> 16), uint8_t(_blocks >> 8), uint8_t(_blocks),
                                        uint8_t(_remainder >> 8), uint8_t(_remainder)};
            for (uint8_t b : header)
                Out(b);
            _index = 0;
            if (_size == 0)
            {
                _mode = Mode::Shell;
                Prompt();
            }
            else
                _mode = Mode::SendBlocks;
            break;
        }
        case Mode::SendBlocks:
        {
            // the status of the previous block ('!' ok, '?' send it again), or the first request
            if (byte == kBlockFail && _index > 0)
                SendBlock(_lastSent);
            else if (_index < BlocksTotal())
            {
                SendBlock(_index);
                _lastSent = _index++;
            }
            else
            {
                _mode = Mode::Shell;  // the status of the last block
                Prompt();
            }
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
    _files[_name] = _data;
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
        BeginReceive();
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
    Out("OK\r\n");
    _header.clear();
    _mode = Mode::RecvHeader;
}

void NextPiPeer::BeginTransmit()
{
    Out("OK\r\n");
    _name.clear();
    _pathTerminated = false;
    _mode = Mode::SendPath;
}
