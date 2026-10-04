#include "memoryread.h"

#include <algorithm>
#include <cstdlib>

#include "debugger/search/memorysearch.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

namespace MemoryRead
{
namespace
{
bool ParseNumber(const std::string& text, uint32_t& out)
{
    std::string digits = text;
    int base = 10;
    if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
    {
        digits = digits.substr(2);
        base = 16;
    }
    else if (!digits.empty() && (digits[0] == '#' || digits[0] == '$'))
    {
        digits = digits.substr(1);
        base = 16;
    }
    if (digits.empty())
        return false;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(digits.c_str(), &end, base);
    if (*end != '\0' || value > 0xFFFFFFFFull)
        return false;
    out = static_cast<uint32_t>(value);
    return true;
}
}  // namespace

Result Bytes(EmulatorContext* context, const std::string& space, uint32_t address, uint32_t length)
{
    Result result;
    Memory* memory = context ? context->pMemory : nullptr;
    if (!memory)
    {
        result.error = "memory not available";
        return result;
    }
    if (length == 0 || length > kMaxLength)
    {
        result.error = "the length must be 1 to 65536";
        return result;
    }
    MemorySearchRequest where;
    std::string error;
    if (!MemorySearch::ParseSpace(space, where, error))
    {
        result.error = error;
        return result;
    }
    result.space = MemorySearch::SpaceName(where);
    result.address = address;

    switch (where.space)
    {
        case MemorySearchRequest::Space::Cpu:
        {
            if (address > 0xFFFF)
            {
                result.error = "a CPU address is 0 to #FFFF";
                return result;
            }
            result.bytes.resize(length);
            for (uint32_t i = 0; i < length; i++)
                result.bytes[i] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(address + i));   // wraps at #FFFF
            return result;
        }
        case MemorySearchRequest::Space::Page:
        {
            const uint8_t* page = MemorySearch::PageHost(context, where.pageType, where.page);
            if (!page)
            {
                result.error = "this machine has no page " + result.space;
                return result;
            }
            if (address >= PAGE_SIZE)
            {
                result.error = "a page offset is 0 to #3FFF";
                return result;
            }
            const uint32_t n = std::min<uint32_t>(length, PAGE_SIZE - address);
            result.bytes.assign(page + address, page + address + n);
            return result;
        }
        case MemorySearchRequest::Space::AllRam:
        {
            const uint32_t pages = MemorySearch::RamPageCount(context);
            const uint64_t size = static_cast<uint64_t>(pages) * PAGE_SIZE;
            if (address >= size)
            {
                result.error = "past the last RAM page";
                return result;
            }
            const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(length, size - address));
            result.bytes.reserve(n);
            for (uint32_t i = 0; i < n;)
            {
                const uint32_t at = address + i;
                const uint8_t* page = memory->RAMPageAddress(static_cast<uint16_t>(at / PAGE_SIZE));
                if (!page)
                    break;
                const uint32_t run = std::min<uint32_t>(n - i, PAGE_SIZE - at % PAGE_SIZE);
                result.bytes.insert(result.bytes.end(), page + at % PAGE_SIZE, page + at % PAGE_SIZE + run);
                i += run;
            }
            return result;
        }
    }
    return result;
}

bool ParseWindow(const std::string& text, std::string& space, uint32_t& address, uint32_t& length, std::string& error)
{
    const size_t first = text.find(':');
    const size_t second = first == std::string::npos ? std::string::npos : text.find(':', first + 1);
    if (second == std::string::npos)
    {
        error = "a memory window is <space>:<address>:<length> (cpu:0x8000:256)";
        return false;
    }
    space = text.substr(0, first);
    if (!ParseNumber(text.substr(first + 1, second - first - 1), address) || !ParseNumber(text.substr(second + 1), length))
    {
        error = "bad address or length in '" + text + "'";
        return false;
    }
    if (length == 0 || length > kMaxLength)
    {
        error = "the length must be 1 to 65536";
        return false;
    }
    return true;
}

}  // namespace MemoryRead
