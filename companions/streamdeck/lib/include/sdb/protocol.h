// Pure functions that build HID reports (no I/O) so they can be golden-tested without hardware.
// Byte layouts: docs/02-usb-protocol.md.
#pragma once
#include <cstdint>
#include <span>
#include <vector>

#include "sdb/models.h"

namespace sdb::proto
{

using Report = std::vector<std::uint8_t>;

[[nodiscard]] Report Brightness(const Model&, int percent);  // feature report
[[nodiscard]] Report Reset(const Model&);                    // feature report
[[nodiscard]] Report TouchLed(const Model&, int point, std::uint8_t r, std::uint8_t g, std::uint8_t b);  // Neo, feature

// Output reports (report ID 0x02), each already padded to the model's report length.
[[nodiscard]] std::vector<Report> KeyImage(const Model&, int key, std::span<const std::uint8_t> encoded);
[[nodiscard]] std::vector<Report> LcdFill(const Model&, std::span<const std::uint8_t> encoded);  // Neo / Plus / Plus XL
// Rectangle in DEVICE space (Plus/Plus XL).
[[nodiscard]] std::vector<Report> LcdRegion(const Model&, int x, int y, int w, int h,
                                            std::span<const std::uint8_t> encoded);

// Elgato docs (expanded protocol, all V2 models).
[[nodiscard]] std::vector<Report> FullScreen(const Model&, std::span<const std::uint8_t> encoded);   // output 0x08
[[nodiscard]] std::vector<Report> WindowImage(const Model&, std::span<const std::uint8_t> encoded);  // output 0x0B
[[nodiscard]] Report FillKeyColor(const Model&, int key, std::uint8_t r, std::uint8_t g,
                                  std::uint8_t b);                                                   // feature 03 06
[[nodiscard]] Report FillScreenColor(const Model&, std::uint8_t r, std::uint8_t g, std::uint8_t b);  // feature 03 05
[[nodiscard]] Report SleepAfter(const Model&, std::int32_t seconds);  // 03 0D / Mini 0B A2
[[nodiscard]] Report ShowBackground(const Model&, int index);         // 03 13 (XL)

// Serial / firmware feature-report parameters.
struct FeatureRead
{
    std::uint8_t id;
    std::size_t length;
    std::size_t skip;
};
[[nodiscard]] FeatureRead SerialRead(const Model&);
[[nodiscard]] FeatureRead FirmwareRead(const Model&);
[[nodiscard]] FeatureRead FirmwareReadFallback(const Model&);  // crate's id for old Mini firmware (0x04)
[[nodiscard]] FeatureRead SleepRead(const Model&);             // 0x0A / Mini 0xA3
inline constexpr std::uint8_t kUnitInfoReport = 0x08;          // V2 "Get Unit Information"

[[nodiscard]] std::size_t InputReportLength(const Model&);  // bytes to ask hid_read for
[[nodiscard]] int FlipKeyOriginal(const Model&, int key);   // Original v1 mirrors columns

}  // namespace sdb::proto
