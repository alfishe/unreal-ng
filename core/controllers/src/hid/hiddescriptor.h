#pragma once

/// @file hiddescriptor.h
/// @brief The input side of a HID report descriptor (HID 1.11 §6.2.2): the variable fields of each input report, with
/// their usage, place and range, so a generic pad decodes any report its descriptor describes (architecture.md §3,
/// `hid/`). Arrays, outputs and features are not kept; neither are fields outside a pad's application collection.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace controllers
{

struct HidField
{
    uint8_t reportId = 0;  ///< 0 when the descriptor has no report ids
    uint32_t bitOffset = 0;  ///< from the first byte after the report id
    uint8_t bitSize = 0;     ///< 1..32
    uint16_t usagePage = 0;
    uint16_t usage = 0;
    int32_t logicalMin = 0;
    int32_t logicalMax = 0;
    bool nullState = false;  ///< a value outside the range means "nothing" (a centred hat)
    uint16_t application = 0;  ///< the Generic Desktop usage of the application collection (4 joystick, 5 pad, ...)
};

struct HidDescriptor
{
    std::vector<HidField> fields;
    bool hasReportIds = false;
};

/// False with the reason for a descriptor that cannot be read
bool ParseHidDescriptor(const uint8_t* data, size_t size, HidDescriptor& out, std::string& error);

/// A field's value in @p report (which starts with the report id when the descriptor has ids), sign-extended when
/// the logical minimum is negative; false when the report is too short or of another id
bool ReadHidField(const HidField& field, bool hasReportIds, const uint8_t* report, size_t size, int32_t& value);

}  // namespace controllers
