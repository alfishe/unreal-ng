#pragma once

/// @file hostserialport.h
/// @brief A host serial port (a real ESP module on USB, a modem, a null-modem
/// cable to another computer): raw 8N1 bytes at a fixed baud rate (network
/// adapters TDD §7.2, HostSerialPeer). Blocking calls with timeouts, used from
/// the host network bridge's serial thread only.
///
/// One class, one implementation per OS: platform/posix/hostserialport_posix.cpp
/// (macOS, Linux: termios), platform/windows/hostserialport_windows.cpp (COMM API).
/// Device names are UTF-8: /dev/tty.usbserial-0001, /dev/ttyUSB0, COM3.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "common/network/nettypes.h"

class HostSerialPort
{
public:
    HostSerialPort();
    ~HostSerialPort();

    HostSerialPort(const HostSerialPort&) = delete;
    HostSerialPort& operator=(const HostSerialPort&) = delete;

    /// Open `device` raw at `baud`, 8 data bits, no parity, 1 stop bit, no
    /// flow control. False with a reason in `error`
    bool Open(const std::string& device, uint32_t baud, std::string& error);
    bool Open(const std::string& device, const SerialLine& line, std::string& error);
    void Close();
    bool IsOpen() const;

    /// Up to `max` bytes, waiting at most `timeoutMs` for the first one.
    /// 0 = nothing arrived, -1 = the device failed (unplugged)
    int Read(uint8_t* buffer, size_t max, uint32_t timeoutMs);

    /// Write all `length` bytes (blocks until the OS took them). False = failed
    bool Write(const uint8_t* data, size_t length);

    /// Line format: any baud rate the OS and the adapter take (standard rates
    /// and, where the OS allows, others: macOS IOSSIOSPEED, Linux termios2,
    /// Windows DCB), 5..8 data bits, N / O / E / M / S parity, 1 or 2 stop bits
    bool Configure(const SerialLine& line, std::string& error);

    /// Drive RTS and DTR (the ZX side's modem control); false if not supported
    bool SetModemLines(bool rts, bool dtr);

    /// The device's input lines in the 16550 MSR layout (CTS #10, DSR #20,
    /// RI #40, DCD #80); -1 when they cannot be read
    int ModemStatus();

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
