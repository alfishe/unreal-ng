#include "stdafx.h"

#include "ttdvdac2.h"

#include "emulator/platforms/tsconf/vdac2card.h"

namespace ttd
{

size_t TTDVdac2Memory::TTDStateSize() const { return _card.TtdMemorySize(); }
void TTDVdac2Memory::TTDSaveState(uint8_t* dst) const { _card.TtdSaveMemory(dst); }
void TTDVdac2Memory::TTDSaveStateTo(std::vector<uint8_t>& out) const
{
    out.resize(_card.TtdMemorySize());
    out.resize(_card.TtdSaveMemory(out.data()));
}
void TTDVdac2Memory::TTDLoadState(const uint8_t* src) { _card.TtdLoadMemory(src); }
uint64_t TTDVdac2Memory::TTDHashState() const { return _card.TtdMemoryHash(); }

size_t TTDVdac2::TTDStateSize() const { return _card.TtdStateSize(); }
void TTDVdac2::TTDSaveState(uint8_t* dst) const { _card.TtdSaveState(dst); }
void TTDVdac2::TTDLoadState(const uint8_t* src) { _card.TtdLoadState(src); }
uint64_t TTDVdac2::TTDHashState() const { return _card.TtdStateHash(); }

}  // namespace ttd
