#include "screendigest.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/video/screen.h"
#include "common/stringhelper.h"
#include "emulator/state/devicestate.h"

uint64_t ScreenDigest::DigestBytes(const uint8_t* data, size_t size, uint64_t seed)
{
    uint64_t hash = seed;
    for (size_t i = 0; i < size; i++)
    {
        hash ^= data[i];
        hash *= kPrime;
    }
    return hash;
}

uint64_t ScreenDigest::DigestRAMPage(Memory* memory, uint16_t page)
{
    uint64_t hash = kInitialValue;

    if (!memory)
        return hash;

    const uint8_t* base = memory->RAMPageAddress(page);
    if (!base)
        return hash;

    for (size_t i = 0; i < kRAMPageSize; i++)
    {
        hash ^= base[i];
        hash *= kPrime;
    }

    return hash;
}

uint64_t ScreenDigest::DigestZ80Range(Memory* memory, uint16_t start, uint16_t end)
{
    uint64_t hash = kInitialValue;

    if (!memory || start > end)
        return hash;

    for (uint32_t address = start; address <= end; address++)
    {
        hash ^= memory->DirectReadFromZ80Memory(static_cast<uint16_t>(address));
        hash *= kPrime;
    }

    return hash;
}

namespace ScreenDigestCompute
{
namespace
{
bool ParseAddress(const std::string& text, uint16_t& out)
{
    std::string t = text;
    int base = 10;
    if (!t.empty() && (t[0] == '$' || t[0] == '#'))
    {
        t = t.substr(1);
        base = 16;
    }
    else if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
    {
        t = t.substr(2);
        base = 16;
    }
    if (t.empty())
        return false;
    unsigned long value = 0;
    for (char c : t)
    {
        const int digit = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                          : (base == 16 && std::isxdigit(static_cast<unsigned char>(c)))
                              ? std::tolower(static_cast<unsigned char>(c)) - 'a' + 10
                              : -1;
        if (digit < 0)
            return false;
        value = value * static_cast<unsigned long>(base) + static_cast<unsigned long>(digit);
        if (value > 0xFFFF)
            return false;
    }
    out = static_cast<uint16_t>(value);
    return true;
}
}  // namespace

bool QueryFromStrings(const std::string& mode, const std::string& banks, const std::string& start, const std::string& end,
                      const std::string& includeBorder, ScreenDigestQuery& query, std::string& error)
{
    query = ScreenDigestQuery();
    if (mode == "active")
        query.active = true;
    else if (!mode.empty() && mode != "default")
    {
        error = "Invalid 'mode' parameter (expected 'default' or 'active')";
        return false;
    }
    if (!start.empty() || !end.empty())
    {
        query.range = true;
        if ((!start.empty() && !ParseAddress(start, query.start)) || (!end.empty() && !ParseAddress(end, query.end)) ||
            query.start > query.end)
        {
            error = "Invalid 'start'/'end' parameters (expected hex or decimal, start <= end)";
            return false;
        }
    }
    if (!banks.empty())
    {
        std::istringstream stream(banks);
        std::string token;
        while (std::getline(stream, token, ','))
        {
            uint16_t page = 0;
            if (ParseAddress(token, page) && page <= 255)
                query.banks.push_back(page);
        }
        if (query.banks.empty())
        {
            error = "Invalid 'banks' parameter (expected comma-separated page numbers)";
            return false;
        }
    }
    if (!includeBorder.empty())
        query.includeBorder = includeBorder == "true" || includeBorder == "1" || includeBorder == "yes";
    return true;
}

ScreenDigestResult Compute(EmulatorContext* context, const ScreenDigestQuery& query)
{
    ScreenDigestResult r;
    if (!context || !context->pMemory || !context->pScreen)
    {
        r.error = "Emulator context is not fully initialized";
        return r;
    }
    EmulatorState& state = context->emulatorState;
    Memory* memory = context->pMemory;
    Screen& screen = *context->pScreen;

    r.ok = true;
    r.frame = state.frame_counter;
    r.previousDigest = state.last_screen_digest;
    r.previousFrame = state.last_screen_digest_frame;
    uint64_t combined = ScreenDigest::kInitialValue;

    if (query.range)
    {
        r.range = true;
        r.start = query.start;
        r.end = query.end;
        r.rangeDigest = ScreenDigest::DigestZ80Range(memory, query.start, query.end);
        combined = ScreenDigest::MixDigest(combined, r.rangeDigest);
    }
    else if (query.banks.empty() && screen.DigestSurface(r.surface))
    {
        // A picture outside the RAM pages (the Sprinter's video RAM): both the default and the
        // active mode hash it - pages 5 / 7 say nothing about it
        r.deviceSurface = true;
        r.activeSurface = query.active;
        r.videoMode = Screen::GetVideoModeName(screen.GetVideoMode());
        combined = ScreenDigest::MixDigest(combined, r.surface.digest);
    }
    else
    {
        std::vector<uint16_t> banks = query.banks;
        const bool shadow = Screen::HasShadowScreen(context->config.mem_model);
        if (banks.empty() && query.active)
        {
            // Surface actually displayed by the current video mode: ZX modes keep pages 5/7, ATM hardware
            // modes hash the 7FFD-selected bit-plane pair
            const VideoModeEnum videoMode = screen.GetVideoMode();
            banks = Screen::GetActiveSurfaceRAMPages(videoMode, state.p7FFD, shadow);
            r.activeSurface = true;
            r.videoMode = Screen::GetVideoModeName(videoMode);
            r.activePages = banks;
        }
        else if (banks.empty())
        {
            banks.push_back(ScreenDigest::kScreen0RAMPage);
            if (shadow)
                banks.push_back(ScreenDigest::kScreen1RAMPage);
        }
        for (uint16_t page : banks)
        {
            const uint64_t digest = ScreenDigest::DigestRAMPage(memory, page);
            r.banks.emplace_back(page, digest);
            combined = ScreenDigest::MixDigest(combined, digest);
        }
    }

    r.includeBorder = query.includeBorder;
    if (query.includeBorder)
    {
        r.border = screen.GetBorderColor();
        combined = ScreenDigest::MixValue(combined, r.border);
    }
    r.combined = combined;
    r.changed = combined != r.previousDigest;
    state.last_screen_digest = combined;
    state.last_screen_digest_frame = state.frame_counter;
    return r;
}
}  // namespace ScreenDigestCompute

namespace DeviceState
{
StateNode ScreenDigestReport(EmulatorContext* context, const ScreenDigestQuery& query)
{
    const ScreenDigestResult r = ScreenDigestCompute::Compute(context, query);
    StateNode ret = StateNode::Object();
    if (!r.ok)
    {
        ret["available"] = false;
        ret["description"] = r.error;
        return ret;
    }
    auto hex = [](uint64_t v) { return StringHelper::Format("0x%016llX", static_cast<unsigned long long>(v)); };
    ret["frame"] = static_cast<uint64_t>(r.frame);
    ret["algorithm"] = "fnv1a-64";
    if (r.range)
    {
        StateNode range = StateNode::Object();
        range["start"] = StringHelper::Format("0x%04X", r.start);
        range["end"] = StringHelper::Format("0x%04X", r.end);
        range["digest"] = hex(r.rangeDigest);
        ret["z80_range"] = range;
    }
    else if (r.deviceSurface)
    {
        StateNode surface = StateNode::Object();
        surface["video_mode"] = r.videoMode;
        surface["memory"] = r.surface.name;
        surface["description"] = r.surface.description;
        surface["bytes"] = static_cast<uint64_t>(r.surface.bytes);
        surface["digest"] = hex(r.surface.digest);
        ret["active_surface"] = surface;
    }
    else
    {
        if (r.activeSurface)
        {
            StateNode surface = StateNode::Object();
            surface["video_mode"] = r.videoMode;
            StateNode pages = StateNode::Array();
            for (uint16_t page : r.activePages)
                pages.push(int(page));
            surface["pages"] = pages;
            ret["active_surface"] = surface;
        }
        StateNode banks = StateNode::Array();
        for (const auto& [page, digest] : r.banks)
        {
            StateNode item = StateNode::Object();
            item["page"] = int(page);
            item["digest"] = hex(digest);
            item["size"] = static_cast<uint64_t>(ScreenDigest::kRAMPageSize);
            banks.push(item);
        }
        ret["banks"] = banks;
    }
    ret["combined"] = hex(r.combined);
    ret["include_border"] = r.includeBorder;
    if (r.includeBorder)
        ret["border_color"] = int(r.border);
    ret["changed"] = r.changed;
    ret["previous_digest"] = hex(r.previousDigest);
    if (r.previousFrame != 0)
    {
        ret["previous_digest_frame"] = static_cast<uint64_t>(r.previousFrame);
        ret["frames_since_previous"] = static_cast<uint64_t>(r.frame - r.previousFrame);
    }
    return ret;
}
}  // namespace DeviceState
