// Value faces (scenarios.md §4.2): the text a key shows for a readout's value, by a small template.
//
// A template is text with placeholders `{[field][:spec]}` or `{[field]?yes|no}` ("{{" and "}}" are braces):
//   field  a dotted field of the value ("fps", "frame_time_us.avg", "slots.0.name"); none = the value itself
//   ?a|b   a condition: `a` when the field is true (true, a number not 0, a text not empty), else `b`
//   spec   [+][0][width][.precision][type], type one of
//            f  fixed point (precision 6 when not given)   d  an integer (rounded)   x X  hex
//            %  the number times 100, fixed point (precision 0), a % sign
//            b  bytes: 512B 1.5K 340M 2.1G (precision: 1 below 10, else 0)   k  SI: 950 1.2k 3.4M
//            #  the size of an array or object (the length of a text)          s  text as it is
// Without a spec: an integral number as an integer, another with up to 2 decimals; true / false as ON / OFF; a text
// as it is. A field the value does not have (or no value yet) shows as "--". "\n" in a template starts a second line.
#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

#include "sdb/scenario.h"

namespace sdb::scn
{

// "" when `format` is a valid template, else what is wrong ("unbalanced '{'", "unknown type 'q' in {:q}")
[[nodiscard]] std::string CheckValueFormat(std::string_view format);

// The text of `value` (null: not known yet; every placeholder shows "--") by `format` ("" = "{}"). A bad template
// shows as it is.
[[nodiscard]] std::string FormatValue(std::string_view format, const json& value);

// The number the thresholds compare: the first placeholder's field of `value` (the value itself when the template
// has none); nullopt when it is not a number (true / false count as 1 / 0)
[[nodiscard]] std::optional<double> ValueNumber(std::string_view format, const json& value);

// What a value face shows: its text and its level (0 normal, 1 warn, 2 alert)
struct ValueShown
{
    std::string text;
    int level = 0;
    bool operator==(const ValueShown&) const = default;
};
// Thresholds: the level rises at warn / alert (>=); with warn above alert the low values are the bad ones (<=)
[[nodiscard]] int ValueLevel(const FaceValue& v, std::optional<double> number);
[[nodiscard]] ValueShown ShowValue(const FaceValue& v, const json& value);

}  // namespace sdb::scn
