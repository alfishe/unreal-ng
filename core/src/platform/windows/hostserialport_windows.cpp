// HostSerialPort on Windows: the COMM API (see hostserialport.h)

#include "common/serial/hostserialport.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <vector>

namespace
{
std::wstring Widen(const std::string& utf8)
{
    if (utf8.empty())
        return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string LastError()
{
    return "Windows error " + std::to_string(static_cast<unsigned long>(::GetLastError()));
}
}  // namespace

struct HostSerialPort::Impl
{
    HANDLE handle = INVALID_HANDLE_VALUE;
    DWORD lastTimeoutMs = 0xFFFFFFFFu;
};

HostSerialPort::HostSerialPort() : _impl(std::make_unique<Impl>())
{
}

HostSerialPort::~HostSerialPort()
{
    Close();
}

bool HostSerialPort::Open(const std::string& device, uint32_t baud, std::string& error)
{
    SerialLine line;
    line.baud = baud;
    return Open(device, line, error);
}

bool HostSerialPort::Open(const std::string& device, const SerialLine& line, std::string& error)
{
    Close();
    // COM10 and up need the \\.\ prefix; it works for COM1..COM9 too
    std::string path = device;
    if (path.rfind("\\\\.\\", 0) != 0)
        path = "\\\\.\\" + path;
    HANDLE h = ::CreateFileW(Widen(path).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        error = device + ": " + LastError();
        return false;
    }
    DCB dcb {};
    dcb.DCBlength = sizeof(dcb);
    if (!::GetCommState(h, &dcb))
    {
        error = device + ": not a serial device (" + LastError() + ")";
        ::CloseHandle(h);
        return false;
    }
    _impl->handle = h;
    _impl->lastTimeoutMs = 0xFFFFFFFFu;
    if (!Configure(line, error))
    {
        error = device + ": " + error;
        Close();
        return false;
    }
    ::PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return true;
}

bool HostSerialPort::Configure(const SerialLine& line, std::string& error)
{
    if (_impl->handle == INVALID_HANDLE_VALUE)
    {
        error = "not open";
        return false;
    }
    DCB dcb {};
    dcb.DCBlength = sizeof(dcb);
    if (!::GetCommState(_impl->handle, &dcb))
    {
        error = LastError();
        return false;
    }
    dcb.BaudRate = line.baud;   // any rate the driver takes
    dcb.ByteSize = static_cast<BYTE>(line.dataBits >= 5 && line.dataBits <= 8 ? line.dataBits : 8);
    switch (line.parity)
    {
        case 'O': dcb.Parity = ODDPARITY; break;
        case 'E': dcb.Parity = EVENPARITY; break;
        case 'M': dcb.Parity = MARKPARITY; break;
        case 'S': dcb.Parity = SPACEPARITY; break;
        default: dcb.Parity = NOPARITY; break;
    }
    dcb.StopBits = line.stopBits >= 2 ? TWOSTOPBITS : ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = dcb.Parity != NOPARITY;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fAbortOnError = FALSE;
    if (!::SetCommState(_impl->handle, &dcb))
    {
        error = "cannot set the line (" + LastError() + ")";
        return false;
    }
    return true;
}

void HostSerialPort::Close()
{
    if (_impl && _impl->handle != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(_impl->handle);
        _impl->handle = INVALID_HANDLE_VALUE;
    }
}

bool HostSerialPort::IsOpen() const
{
    return _impl->handle != INVALID_HANDLE_VALUE;
}

int HostSerialPort::Read(uint8_t* buffer, size_t max, uint32_t timeoutMs)
{
    if (_impl->handle == INVALID_HANDLE_VALUE || max == 0)
        return -1;
    if (_impl->lastTimeoutMs != timeoutMs)
    {
        // Return as soon as a byte is there, or after timeoutMs with nothing
        COMMTIMEOUTS t {};
        t.ReadIntervalTimeout = MAXDWORD;
        t.ReadTotalTimeoutMultiplier = MAXDWORD;
        t.ReadTotalTimeoutConstant = std::max<DWORD>(1, timeoutMs);
        t.WriteTotalTimeoutConstant = 0;
        if (!::SetCommTimeouts(_impl->handle, &t))
            return -1;
        _impl->lastTimeoutMs = timeoutMs;
    }
    DWORD got = 0;
    const DWORD want = static_cast<DWORD>(std::min<size_t>(max, 0x10000));
    if (!::ReadFile(_impl->handle, buffer, want, &got, nullptr))
        return -1;
    return static_cast<int>(got);
}

bool HostSerialPort::Write(const uint8_t* data, size_t length)
{
    while (length > 0 && _impl->handle != INVALID_HANDLE_VALUE)
    {
        DWORD done = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(length, 0x10000));
        if (!::WriteFile(_impl->handle, data, chunk, &done, nullptr))
            return false;
        data += done;
        length -= done;
    }
    return length == 0;
}

bool HostSerialPort::SetModemLines(bool rts, bool dtr)
{
    if (_impl->handle == INVALID_HANDLE_VALUE)
        return false;
    const bool ok1 = ::EscapeCommFunction(_impl->handle, rts ? SETRTS : CLRRTS) != 0;
    const bool ok2 = ::EscapeCommFunction(_impl->handle, dtr ? SETDTR : CLRDTR) != 0;
    return ok1 && ok2;
}

int HostSerialPort::ModemStatus()
{
    if (_impl->handle == INVALID_HANDLE_VALUE)
        return -1;
    DWORD status = 0;
    if (!::GetCommModemStatus(_impl->handle, &status))
        return -1;
    int msr = 0;
    if (status & MS_CTS_ON) msr |= 0x10;
    if (status & MS_DSR_ON) msr |= 0x20;
    if (status & MS_RING_ON) msr |= 0x40;
    if (status & MS_RLSD_ON) msr |= 0x80;
    return msr;
}
