#pragma once

/// @file answerbudget.h
/// @brief Answer budgets (docs/inprogress/2026-11-04-answer-budgets): fit an answer in N characters and say what was
/// left out. Header-only: the MCP dispatcher and the WebAPI (REST, WebSocket calls) both use it.
///
/// Text is cut on line boundaries (the first lines, an omission line, up to three last lines); JSON has its biggest
/// arrays shortened and long strings cut until the compact text fits, an object root getting a "truncated" member.
/// Characters are Unicode code points; nothing is split inside a UTF-8 sequence.

#include <json/json.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace answer_budget
{
/// The argument / query parameter name, and its range
inline constexpr const char* kArg = "max_chars";
inline constexpr size_t kMinChars = 500;
inline constexpr size_t kMaxChars = 10000000;

/// The code points in a UTF-8 text
inline size_t Chars(const std::string& text)
{
    size_t n = 0;
    for (const char c : text)
        n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    return n;
}

/// The byte offset of the code point @p chars (the size when the text is shorter)
inline size_t ByteOffset(const std::string& text, size_t chars)
{
    size_t n = 0;
    for (size_t i = 0; i < text.size(); i++)
    {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
        {
            if (n == chars)
                return i;
            n++;
        }
    }
    return text.size();
}

/// The first @p chars code points of @p text
inline std::string Head(const std::string& text, size_t chars)
{
    return text.substr(0, ByteOffset(text, chars));
}

/// A budget from text ("2000"), or why not; @p out untouched on failure
inline bool Parse(const std::string& text, size_t& out, std::string& error)
{
    if (text.empty() || text.size() > 9 || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
    {
        error = std::string(kArg) + " must be a whole number of characters (" + std::to_string(kMinChars) + ".." +
                std::to_string(kMaxChars) + ")";
        return false;
    }
    const size_t value = std::stoul(text);
    if (value < kMinChars || value > kMaxChars)
    {
        error = std::string(kArg) + " must be " + std::to_string(kMinChars) + ".." + std::to_string(kMaxChars) +
                " (got " + text + ")";
        return false;
    }
    out = value;
    return true;
}

/// A budget from a JSON argument (a number or a numeric string)
inline bool Parse(const Json::Value& value, size_t& out, std::string& error)
{
    if (value.isUInt64() || (value.isInt64() && value.asInt64() >= 0))
        return Parse(std::to_string(value.asUInt64()), out, error);
    if (value.isString())
        return Parse(value.asString(), out, error);
    return Parse(std::string("?"), out, error);
}

/// Groups digits: 17412 -> "17,412"
inline std::string Grouped(size_t value)
{
    std::string digits = std::to_string(value);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
        digits.insert(static_cast<size_t>(i), ",");
    return digits;
}

/// @p text in at most @p maxChars characters: whole when it fits; else the first lines, an omission line naming what
/// was left out and how to get it, and up to three last lines when they fit in a quarter of the budget. @p asked: the
/// max_chars the caller gave, named in the omission line (0: @p maxChars; MCP fits the text in a share of it)
inline std::string FitText(const std::string& text, size_t maxChars, size_t asked = 0)
{
    const size_t total = Chars(text);
    if (total <= maxChars)
        return text;
    std::vector<std::string> lines;
    for (size_t start = 0;;)
    {
        const size_t end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    auto note = [&](size_t omittedLines, size_t omittedChars) {
        return "... " + Grouped(omittedLines) + " of " + Grouped(lines.size()) + " lines (" + Grouped(omittedChars) +
               " chars) left out for " + kArg + "=" + std::to_string(asked != 0 ? asked : maxChars) +
               "; narrow the call or raise " + kArg;
    };
    // The note's longest form (every count as big as it can get), so the parts around it always fit
    const size_t noteChars = Chars(note(lines.size(), total)) + 1;
    const size_t budget = maxChars > noteChars ? maxChars - noteChars : 0;

    // The tail: up to three last lines, a quarter of the budget at most (never the first line)
    size_t tailCount = 0;
    size_t tailChars = 0;
    while (tailCount < 3 && tailCount + 1 < lines.size())
    {
        const size_t line = Chars(lines[lines.size() - 1 - tailCount]) + 1;
        if (tailChars + line > budget / 4)
            break;
        tailChars += line;
        tailCount++;
    }
    // The head: lines from the start while they fit; the first one cut when even it does not
    std::string head;
    size_t headChars = 0;
    size_t headCount = 0;
    const size_t headBudget = budget - tailChars;
    while (headCount < lines.size() - tailCount)
    {
        const size_t line = Chars(lines[headCount]) + 1;
        if (headChars + line > headBudget)
        {
            if (headCount == 0 && headBudget > 4)
            {
                head = Head(lines[0], headBudget - 4) + "...\n";
                headChars = headBudget;
                headCount = 1;
            }
            break;
        }
        head += lines[headCount] + "\n";
        headChars += line;
        headCount++;
    }
    std::string tail;
    for (size_t i = lines.size() - tailCount; i < lines.size(); i++)
        tail += "\n" + lines[i];
    const size_t omittedLines = lines.size() - headCount - tailCount;
    const size_t omittedChars = total - headChars - tailChars;
    return head + note(omittedLines, omittedChars) + tail;
}

namespace detail
{
/// The compact JSON size of a string's quoted, escaped form, in characters
inline size_t StringChars(const std::string& text)
{
    size_t n = 2;
    for (const char c : text)
    {
        const auto u = static_cast<unsigned char>(c);
        if ((u & 0xC0) == 0x80)
            continue;
        n += (c == '"' || c == '\\' || c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t') ? 2
             : u < 0x20                                                                               ? 6
                                                                                                      : 1;
    }
    return n;
}

/// The compact JSON size of @p value in characters (Json::writeString with no indentation, emitUTF8)
inline size_t JsonChars(const Json::Value& value)
{
    switch (value.type())
    {
        case Json::nullValue: return 4;
        case Json::booleanValue: return value.asBool() ? 4 : 5;
        case Json::intValue: return std::to_string(value.asLargestInt()).size();
        case Json::uintValue: return std::to_string(value.asLargestUInt()).size();
        case Json::realValue: return Json::valueToString(value.asDouble()).size();
        case Json::stringValue: return StringChars(value.asString());
        case Json::arrayValue:
        {
            size_t n = 2 + (value.empty() ? 0 : value.size() - 1);
            for (const Json::Value& item : value)
                n += JsonChars(item);
            return n;
        }
        case Json::objectValue:
        {
            size_t n = 2 + (value.empty() ? 0 : value.size() - 1);
            for (const std::string& name : value.getMemberNames())
                n += StringChars(name) + 1 + JsonChars(value[name]);
            return n;
        }
    }
    return 0;
}

/// The biggest array (2 items or more) or long string under @p value: its path, size and node
struct Biggest
{
    Json::Value* node = nullptr;
    std::string path;
    size_t chars = 0;
};

/// Sizes every node once (bottom-up) and remembers the biggest cuttable one; returns @p value's size
inline size_t Find(Json::Value& value, const std::string& path, Biggest& best)
{
    size_t chars = 0;
    if (value.isArray())
    {
        chars = 2 + (value.empty() ? 0 : value.size() - 1);
        for (Json::ArrayIndex i = 0; i < value.size(); i++)
            chars += Find(value[i], path + "[" + std::to_string(i) + "]", best);
        if (value.size() >= 2 && chars > best.chars)
            best = {&value, path, chars};
    }
    else if (value.isObject())
    {
        chars = 2 + (value.empty() ? 0 : value.size() - 1);
        for (const std::string& name : value.getMemberNames())
            chars += StringChars(name) + 1 + Find(value[name], path.empty() ? name : path + "." + name, best);
    }
    else
    {
        chars = JsonChars(value);
        if (value.isString() && chars > 80 && chars > best.chars)
            best = {&value, path, chars};
    }
    return chars;
}
}  // namespace detail

/// @p value fitted in @p maxChars characters of compact JSON: the biggest array shortened (its tail left out) or the
/// biggest long string cut, again until it fits. An object root gets "truncated": {"max_chars", "chars" (the full
/// size), "omitted": {path: items left out, or "<n> chars" for a string}}; a string of several lines is cut on line
/// boundaries like FitText. @p fullChars: the size before (optional); @p asked: the max_chars named in the note (0:
/// @p maxChars)
inline Json::Value FitJson(const Json::Value& value, size_t maxChars, size_t* fullChars = nullptr, size_t asked = 0)
{
    const size_t full = detail::JsonChars(value);
    if (fullChars != nullptr)
        *fullChars = full;
    if (full <= maxChars)
        return value;
    Json::Value fitted = value;
    Json::Value omitted(Json::objectValue);
    // Room for the "truncated" member, paths included (a few of them; more are summed into "others")
    const size_t reserve = fitted.isObject() ? 300 : 0;
    const size_t target = maxChars > reserve ? maxChars - reserve : maxChars / 2;
    for (int round = 0; round < 4096; round++)
    {
        detail::Biggest best;
        const size_t chars = detail::Find(fitted, "", best);
        if (chars <= target || best.node == nullptr)
            break;
        const size_t excess = chars - target;
        const std::string key = best.path.empty() ? "(root)" : best.path;
        if (best.node->isArray())
        {
            const Json::ArrayIndex size = best.node->size();
            // Keep the share of items that fits, at least one; never all of them
            const size_t room = best.chars > excess ? best.chars - excess : 0;
            Json::ArrayIndex keep = static_cast<Json::ArrayIndex>(static_cast<uint64_t>(size) * room / best.chars);
            keep = std::clamp<Json::ArrayIndex>(keep, 1, size - 1);
            Json::Value shorter(Json::arrayValue);
            for (Json::ArrayIndex i = 0; i < keep; i++)
                shorter.append((*best.node)[i]);
            *best.node = std::move(shorter);
            omitted[key] = omitted.get(key, 0).asUInt64() + (size - keep);
        }
        else
        {
            const std::string text = best.node->asString();
            const size_t length = Chars(text);
            const size_t keep = std::max<size_t>(32, length > excess + 8 ? length - excess - 8 : 0);
            if (keep >= length)
                break;
            // Lines (a hexdump, a listing): whole lines and the omission line; else the head of the text, ending on
            // a line boundary when it holds one
            std::string head = Head(text, keep);
            const size_t lineEnd = head.rfind('\n');
            if (lineEnd != std::string::npos && lineEnd > 0)
                head.resize(lineEnd + 1);
            head += "...";
            std::string shorter = text.find('\n') != std::string::npos && keep >= 200
                                      ? FitText(text, keep, asked != 0 ? asked : maxChars)
                                      : head;
            if (Chars(shorter) >= length)
                shorter = head;
            omitted[key] = std::to_string(length - std::min(length, Chars(shorter))) + " chars";
            *best.node = std::move(shorter);
        }
    }
    if (fitted.isObject())
    {
        Json::Value note;
        note["max_chars"] = static_cast<Json::UInt64>(asked != 0 ? asked : maxChars);
        note["chars"] = static_cast<Json::UInt64>(full);
        // At most 8 paths by name; the rest counted
        Json::Value paths(Json::objectValue);
        size_t others = 0;
        for (const std::string& name : omitted.getMemberNames())
        {
            if (paths.size() < 8 && name.size() <= 60)
                paths[name] = omitted[name];
            else
                others++;
        }
        if (others > 0)
            paths["others"] = static_cast<Json::UInt64>(others);
        note["omitted"] = paths;
        note["hint"] = "narrow the call (count, length, limit, offset, range) or raise max_chars";
        fitted["truncated"] = note;
    }
    return fitted;
}

/// Compact JSON text of @p value (no indentation, UTF-8 kept)
inline std::string CompactJson(const Json::Value& value)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["emitUTF8"] = true;
    return Json::writeString(builder, value);
}
}  // namespace answer_budget
