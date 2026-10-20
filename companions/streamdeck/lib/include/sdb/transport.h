#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sdb/input.h"

namespace sdb
{

// Abstracts hidapi so Device logic is testable with a mock.
class Transport
{
public:
    virtual ~Transport() = default;
    virtual int Write(std::span<const std::uint8_t> report) = 0;  // output report; returns bytes or -1
    virtual int SendFeature(std::span<const std::uint8_t> report) = 0;
    virtual int GetFeature(std::span<std::uint8_t> buf) = 0;           // buf[0] = report id
    virtual int Read(std::span<std::uint8_t> buf, int timeoutMs) = 0;  // bytes read, 0 timeout, -1 error
    virtual std::string LastError() const
    {
        return {};
    }
    // Source of the report the last successful read() returned (a mirror merges two).
    [[nodiscard]] virtual InputSource LastSource() const
    {
        return InputSource::Usb;
    }
    // A line for Stream Deck Studio's protocol log (actions, navigation); devices without a log ignore it.
    virtual void Note(std::string_view text)
    {
        (void)text;
    }
    // The meaning of what is on the device (JSON), for Stream Deck Studio's automation API; ignored elsewhere.
    virtual void PublishPage(std::string_view json)
    {
        (void)json;
    }
};

struct HidInfo
{
    std::string path, serial, product, manufacturer;
    std::uint16_t vid = 0, pid = 0;
    int usagePage = 0, usage = 0, interfaceNumber = -1;
};

// Fresh enumeration every call: a cached enumeration misses decks plugged in later.
[[nodiscard]] std::vector<HidInfo> EnumerateElgato();
[[nodiscard]] std::unique_ptr<Transport> OpenHid(const std::string& path, std::string* error = nullptr);

}  // namespace sdb
