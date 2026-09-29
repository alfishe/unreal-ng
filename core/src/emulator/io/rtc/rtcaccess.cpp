#include "stdafx.h"

#include "rtcaccess.h"

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/rtc/ds12887.h"
#include "emulator/ports/portdecoder.h"

namespace RtcAccess
{

Ds12887* Find(EmulatorContext* context, std::string* reason)
{
    if (!context || !context->pPortDecoder)
    {
        if (reason)
            *reason = "No machine";
        return nullptr;
    }

    const PortDecoder::RtcBinding binding = context->pPortDecoder->GetRtcBinding();
    if (!binding.chip && reason)
        *reason = binding.absentReason;
    return binding.chip;
}

namespace
{
    bool CheckRange(const Ds12887& chip, unsigned start, size_t count, std::string& error)
    {
        const size_t cells = chip.GetCellCount();
        if (count == 0)
        {
            error = "Nothing to access: count is 0";
            return false;
        }
        if (start >= cells || count > cells - start)
        {
            error = "Cells " + std::to_string(start) + ".." + std::to_string(start + count - 1) +
                    " are outside the chip (" + std::to_string(cells) + " cells: 0.." + std::to_string(cells - 1) + ")";
            return false;
        }
        return true;
    }
}  // namespace

bool Read(EmulatorContext* context, unsigned start, unsigned count, std::vector<uint8_t>& out, std::string& error)
{
    Ds12887* chip = Find(context, &error);
    if (!chip || !CheckRange(*chip, start, count, error))
        return false;

    out.clear();
    out.reserve(count);
    for (unsigned i = 0; i < count; ++i)
        out.push_back(chip->PeekRegister(static_cast<uint8_t>(start + i)));
    return true;
}

bool Write(EmulatorContext* context, unsigned start, const std::vector<uint8_t>& bytes, const char* source,
           std::string& error)
{
    Ds12887* chip = Find(context, &error);
    if (!chip || !CheckRange(*chip, start, bytes.size(), error))
        return false;

    auto edit = [&]() {
        for (size_t i = 0; i < bytes.size(); ++i)
            chip->WriteRegister(static_cast<uint8_t>(start + i), bytes[i]);
    };
    if (context->pEmulator)
        context->pEmulator->EditMemoryFromTool(source, edit);
    else
        edit();
    return true;
}

}  // namespace RtcAccess
