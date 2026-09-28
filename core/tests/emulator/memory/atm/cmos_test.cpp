// Regression for the ATM3 (ZX-Evo) non-determinism root cause: CMOS::_cmos /
// _nvram were plain uint8_t[] with no in-class initializer and an empty
// constructor, so every instance started from whatever garbage its backing
// storage held. ReadCMOS()'s default case (registers outside the RTC/status
// block) serves _cmos[addr] straight to the guest, and the BaseConf ROM
// probes those registers during boot - garbage bytes made the boot path (and
// the RAM/CPU state it settles into) depend on process/instance memory
// layout instead of on the frozen clock and seeded RAM fill.
//
// SMUCNvram (Scorpion) already zeroes the same shape of storage in its
// constructor for the same reason; CMOS did not.

#include <cstring>
#include <new>

#include <gtest/gtest.h>

#include "emulator/memory/atm/cmos.h"
#include "emulator/io/rtc/ds12885.h"

// Any address that falls through ReadCMOS()'s switch to the raw
// `_cmos[cur_addr]` default case - not one of the RTC (Second..Year),
// Unknown_10 / BitFlags / UF / Unknown_13 special cases.
constexpr uint8_t kRawRegister = 20;

TEST(CMOS_Test, ConstructionOverwritesWhateverStorageHeld)
{
    // Not relying on the allocator happening to hand back zeroed pages: pin
    // storage down, poison every byte, then construct CMOS directly on top
    // of it. This is exactly what "another instance's memory, reused" looks
    // like - the scenario the ATM3 repro hit via heap alloc/free churn
    // between emulator instances - without depending on the allocator's
    // reuse behaviour to expose it.
    alignas(CMOS) uint8_t storage[sizeof(CMOS)];
    memset(storage, 0xFF, sizeof(storage));

    CMOS* cmos = new (storage) CMOS();
    cmos->SetCMOSAddress(kRawRegister);
    EXPECT_EQ(cmos->ReadCMOS(), 0);
    cmos->~CMOS();
}
