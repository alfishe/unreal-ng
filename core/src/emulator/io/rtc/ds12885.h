#pragma once

/// @file ds12885.h
/// @brief DS12885-family RTC/CMOS register map.
///
/// Chip-level definitions only - shared because more than one machine wires
/// the same part, not because the machines share anything else:
///   - ATM3 / ZX-Evo BaseConf  -> memory/atm/cmos.h  (direct CMOS access)
///   - Scorpion SMUC           -> io/rtc/smucnvram.h (behind an I2C serial link)
///
/// The register layout belongs to the chip, so it lives once here. Everything
/// about how a given machine reaches the chip stays in that machine's own
/// header. Both previously carried byte-identical private copies, which
/// collided the moment both machines existed in one build.

enum CMOSTypeEnum
{
	None = 0,
	Dallas = 1,
	Rus512 = 2
};

enum CMOSMemoryEnum
{
	Second = 0,
	Reserved_1 = 1,
	Minute = 2,
	Reserved_3 = 3,
	Hour = 4,
	Reserved_5 = 5,
	DayOfWeek = 6,
	Day = 7,
	Month = 8,
	Year = 9,
	Unknown_10 = 10,
	BitFlags = 11,
	UF = 12,
	Unknown_13 = 13
};
