#include "stdafx.h"

#include "nvram.h"
#include "common/timehelper.h"

//
// Constructor for CMOS NVRAM
//
NVRAM::NVRAM()
{
}

void NVRAM::SetNVRAMAddress(uint32_t addr)
{
	_address = addr;
}

void NVRAM::WriteNVRAM(uint8_t val)
{

}

uint8_t NVRAM::ReadNVRAM()
{
	uint8_t result = 0;

	return result;
}

void NVRAM::SetCMOSType(CMOSTypeEnum type)
{
	_cmos_type = type;
}

void NVRAM::SetCMOSAddress(uint8_t addr)
{
	_cmos_addr = addr;
}

void NVRAM::WriteCMOS(uint8_t val)
{
	uint8_t cur_addr = _cmos_addr;

	if (_cmos_type == Rus512)
		cur_addr = cur_addr & 0x3F;
		
	_cmos[cur_addr] = val;
}

uint8_t NVRAM::ReadCMOS()
{
	static tm time;
	static bool UF = false;

	uint8_t result = 0;
	uint8_t cur_addr = _cmos_addr;

	if (_cmos_type == Rus512)
		cur_addr = cur_addr & 0x3F;

	// If Time/Date values requested from CMOS - provide current Host system values
	if ((1 << cur_addr) & ((1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 12)))
	{
		time = make_utc_tm(system_clock::now());
	}

	switch (cur_addr)
	{
		case CMOSMemoryEnum::Second:
			result = DecodeFromBCD((uint8_t)time.tm_sec);
			break;
		case CMOSMemoryEnum::Minute:
			result = DecodeFromBCD((uint8_t)time.tm_min);
			break;
		case CMOSMemoryEnum::Hour:
			result = DecodeFromBCD((uint8_t)time.tm_hour);
			break;
		case CMOSMemoryEnum::DayOfWeek:
			result = 1 + (time.tm_wday + 8 % 7);
			break;
		case CMOSMemoryEnum::Day:
			result = DecodeFromBCD((uint8_t)time.tm_mday);
			break;
		case CMOSMemoryEnum::Month:
			result = DecodeFromBCD((uint8_t)time.tm_mon);
			break;
		case CMOSMemoryEnum::Year:
			result = DecodeFromBCD(time.tm_year % 100);
			break;
		case CMOSMemoryEnum::Unknown_10:
			result = 0x20 | (_cmos[10] & 0xF); // molodcov_alex
			break;
		case CMOSMemoryEnum::BitFlags:
			result = (_cmos[11] & 4) | 2;
			break;
		case CMOSMemoryEnum::UF:  // [vv] UF
			result = UF ? 0x10 : 0;
			UF = false;
			break;
		case CMOSMemoryEnum::Unknown_13:
			result = 0x80;
			break;
		default:
			result = _cmos[_cmos_addr];
			break;
	}

	return result;
}

/*
void NVRAM::Write(uint8_t val)
{
	const int SCL = 0x40, SDA = 0x10, WP = 0x20,
		SDA_1 = 0xFF, SDA_0 = 0xBF,
		SDA_SHIFT_IN = 4;

	if ((val ^ prev) & SCL) // clock edge, data in/out
	{
		if (val & SCL) // nvram reads SDA
		{
			if (state == RD_ACK)
			{
				if (val & SDA) goto idle; // no ACK, stop
				// move next byte to host
				state = SEND_DATA;
				dataout = nvram[address];
				address = (address + 1) & 0x7FF;
				bitsout = 0; goto exit; // out_z==1;
			}

			if ((1 << state) & ((1 << RCV_ADDR) | (1 << RCV_CMD) | (1 << RCV_DATA)))
			{
				if (out_z) // skip nvram ACK before reading
					datain = 2 * datain + ((val >> SDA_SHIFT_IN) & 1), bitsin++;
			}

		}
		else
		{ // nvram sets SDA

			if (bitsin == 8) // byte received
			{
				bitsin = 0;
				if (state == RCV_CMD)
				{
					if ((datain & 0xF0) != 0xA0) goto idle;
					address = (address & 0xFF) + ((datain << 7) & 0x700);
					if (datain & 1) { // read from current address
						dataout = nvram[address];
						address = (address + 1) & 0x7FF;
						bitsout = 0;
						state = SEND_DATA;
					}
					else
						state = RCV_ADDR;
				}
				else if (state == RCV_ADDR)
				{
					address = (address & 0x700) + datain;
					state = RCV_DATA; bitsin = 0;
				}
				else if (state == RCV_DATA)
				{
					nvram[address] = datain;
					address = (address & 0x7F0) + ((address + 1) & 0x0F);
					// state unchanged
				}

				// EEPROM always acknowledges
				out = SDA_0;
				out_z = 0;
				goto exit;
			}

			if (state == SEND_DATA)
			{
				if (bitsout == 8)
				{
					state = RD_ACK;
					out_z = 1;
					goto exit;
				}

				out = (dataout & 0x80) ? SDA_1 : SDA_0; dataout *= 2;
				bitsout++;
				out_z = 0;
				
				goto exit;
			}

			out_z = 1; // no ACK, reading
		}
		goto exit;
	}

	if ((val & SCL) && ((val ^ prev) & SDA)) // start/stop
	{
		if (val & SDA)
		{
			idle: state = IDLE;
		} // stop
		else
		{
			state = RCV_CMD;
			bitsin = 0; // start
		}

		out_z = 1;
	}

	// else SDA changed on low SCL

exit:
	if (out_z)
		out = (val & SDA) ? SDA_1 : SDA_0;
	prev = val;
}

*/

// Helper methods
uint8_t NVRAM::DecodeFromBCD(uint8_t binary)
{
	if (!(_cmos[11] & 0x04))
		binary = (binary % 10) + 0x10 * ((binary / 10) % 10);

	return binary;
}