// HostSerialPort on macOS and Linux: termios, raw mode (see hostserialport.h)

#include "common/serial/hostserialport.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "platform/serialcustombaud.h"

namespace
{
/// The termios constant for a standard rate, 0 for any other
speed_t BaudConstant(uint32_t baud)
{
    switch (baud)
    {
        case 300: return B300;
        case 600: return B600;
        case 1200: return B1200;
        case 2400: return B2400;
        case 4800: return B4800;
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
#ifdef B460800
        case 460800: return B460800;
#endif
#ifdef B921600
        case 921600: return B921600;
#endif
        default: return 0;
    }
}
}  // namespace

struct HostSerialPort::Impl
{
    int fd = -1;
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
    // O_NONBLOCK: do not wait for DCD on open (macOS /dev/tty.*); cleared below
    const int fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
    {
        error = device + ": " + std::strerror(errno);
        return false;
    }
    termios tio {};
    if (::tcgetattr(fd, &tio) != 0)
    {
        error = device + ": not a serial device (" + std::strerror(errno) + ")";
        ::close(fd);
        return false;
    }
    ::cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    if (::tcsetattr(fd, TCSANOW, &tio) != 0)
    {
        error = device + ": cannot set raw mode (" + std::strerror(errno) + ")";
        ::close(fd);
        return false;
    }
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) & ~O_NONBLOCK);
    _impl->fd = fd;
    if (!Configure(line, error))
    {
        Close();
        return false;
    }
    ::tcflush(fd, TCIOFLUSH);
    return true;
}

bool HostSerialPort::Configure(const SerialLine& line, std::string& error)
{
    if (_impl->fd < 0)
    {
        error = "not open";
        return false;
    }
    termios tio {};
    if (::tcgetattr(_impl->fd, &tio) != 0)
    {
        error = std::strerror(errno);
        return false;
    }
    tio.c_cflag &= ~static_cast<tcflag_t>(CSIZE | CSTOPB | PARENB | PARODD);
#ifdef CMSPAR
    tio.c_cflag &= ~static_cast<tcflag_t>(CMSPAR);
#endif
    switch (line.dataBits)
    {
        case 5: tio.c_cflag |= CS5; break;
        case 6: tio.c_cflag |= CS6; break;
        case 7: tio.c_cflag |= CS7; break;
        default: tio.c_cflag |= CS8; break;
    }
    if (line.stopBits >= 2)
        tio.c_cflag |= CSTOPB;
    switch (line.parity)
    {
        case 'O': tio.c_cflag |= PARENB | PARODD; break;
        case 'E': tio.c_cflag |= PARENB; break;
        case 'M':
        case 'S':
#ifdef CMSPAR
            tio.c_cflag |= PARENB | CMSPAR | (line.parity == 'M' ? PARODD : 0);
            break;
#else
            error = "mark / space parity is not available on this OS";
            return false;
#endif
        default: break;
    }
    const speed_t constant = BaudConstant(line.baud);
    // A custom rate: set the line with a placeholder speed, then the rate
    ::cfsetispeed(&tio, constant ? constant : B38400);
    ::cfsetospeed(&tio, constant ? constant : B38400);
    if (::tcsetattr(_impl->fd, TCSANOW, &tio) != 0)
    {
        error = std::string("cannot set the line: ") + std::strerror(errno);
        return false;
    }
    if (!constant && !SetCustomSerialBaud(_impl->fd, line.baud))
    {
        error = "the device does not take " + std::to_string(line.baud) + " baud";
        return false;
    }
    return true;
}

void HostSerialPort::Close()
{
    if (_impl && _impl->fd >= 0)
    {
        ::close(_impl->fd);
        _impl->fd = -1;
    }
}

bool HostSerialPort::IsOpen() const
{
    return _impl->fd >= 0;
}

int HostSerialPort::Read(uint8_t* buffer, size_t max, uint32_t timeoutMs)
{
    if (_impl->fd < 0 || max == 0)
        return -1;
    pollfd item {};
    item.fd = _impl->fd;
    item.events = POLLIN;
    const int ready = ::poll(&item, 1, static_cast<int>(timeoutMs));
    if (ready < 0)
        return errno == EINTR ? 0 : -1;
    if (ready == 0)
        return 0;
    if (item.revents & (POLLERR | POLLHUP | POLLNVAL))
        return -1;
    const ssize_t n = ::read(_impl->fd, buffer, max);
    if (n < 0)
        return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
    if (n == 0)
        return -1;   // readable with no data: the device went away
    return static_cast<int>(n);
}

bool HostSerialPort::Write(const uint8_t* data, size_t length)
{
    while (length > 0 && _impl->fd >= 0)
    {
        const ssize_t n = ::write(_impl->fd, data, length);
        if (n < 0)
        {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            return false;
        }
        data += n;
        length -= static_cast<size_t>(n);
    }
    return length == 0;
}

bool HostSerialPort::SetModemLines(bool rts, bool dtr)
{
    if (_impl->fd < 0)
        return false;
    int lines = 0;
    if (::ioctl(_impl->fd, TIOCMGET, &lines) != 0)
        return false;
    lines = rts ? (lines | TIOCM_RTS) : (lines & ~TIOCM_RTS);
    lines = dtr ? (lines | TIOCM_DTR) : (lines & ~TIOCM_DTR);
    return ::ioctl(_impl->fd, TIOCMSET, &lines) == 0;
}

int HostSerialPort::ModemStatus()
{
    if (_impl->fd < 0)
        return -1;
    int lines = 0;
    if (::ioctl(_impl->fd, TIOCMGET, &lines) != 0)
        return -1;
    int msr = 0;
    if (lines & TIOCM_CTS) msr |= 0x10;
    if (lines & TIOCM_DSR) msr |= 0x20;
    if (lines & TIOCM_RI) msr |= 0x40;
    if (lines & TIOCM_CD) msr |= 0x80;
    return msr;
}

std::vector<std::string> HostSerialPort::ListDevices()
{
#if defined(__APPLE__)
    static const char* const kPrefixes[] = {"cu.", "tty."};
#else
    static const char* const kPrefixes[] = {"ttyUSB", "ttyACM", "ttyS", "ttyAMA", "rfcomm"};
#endif
    std::vector<std::string> out;
    DIR* dir = ::opendir("/dev");
    if (!dir)
        return out;
    while (const dirent* entry = ::readdir(dir))
    {
        const std::string name = entry->d_name;
        for (const char* prefix : kPrefixes)
        {
            if (name.size() > std::strlen(prefix) && name.compare(0, std::strlen(prefix), prefix) == 0)
            {
                out.push_back("/dev/" + name);
                break;
            }
        }
    }
    ::closedir(dir);
    std::sort(out.begin(), out.end());
    return out;
}
