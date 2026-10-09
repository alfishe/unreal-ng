#include "stdafx.h"

#include "nextregtable.h"

namespace
{
const NextRegInfo kTable[] = {
#include "nextregtable.inc"
};
}  // namespace

const NextRegInfo* NextRegTable(size_t& count)
{
    count = sizeof kTable / sizeof kTable[0];
    return kTable;
}

const NextRegInfo* FindNextReg(uint8_t number)
{
    static const NextRegInfo* index[256];
    static bool built = false;
    if (!built)
    {
        for (const NextRegInfo& r : kTable)
            index[r.number] = &r;
        built = true;
    }
    return index[number];
}
