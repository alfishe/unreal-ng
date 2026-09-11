#include "stdafx.h"

#include "smucnvram.h"
#include "common/timehelper.h"

#include <cstring>

//
// Constructor for CMOS NVRAM
//
SMUCNvram::SMUCNvram()
{
	memset(_cmos, 0x00, sizeof(_cmos));
	memset(_nvram, 0x00, sizeof(_nvram));
	memset(_writeBuffer, 0x00, sizeof(_writeBuffer));
}

SMUCNvram::~SMUCNvram()
{
}

/// region <Serial-link EEPROM (SMUC #FFBA)>

void SMUCNvram::WriteSerialLink(uint8_t val)
{
	// Bit roles on the SMUC system port (unreal-speccy wiring, Xpeccy nvWr)
	const int sda = val & 0x10;
	const int scl = val & 0x40;
	const int wp = val & 0x20;

	if (!_scl && scl)
	{
		// SCL rising edge
		_stable = true;

		// Transmit mode: present the next byte once the previous one drained
		// (the host samples the current MSB on this very edge)
		if (_tx && !_ack && _bitCount == 0)
		{
			_data = _nvram[_address & 0x7FF];
			_address = static_cast<uint16_t>((_address + 1) & 0x7FF);
			_bitCount = 8;
		}
	}
	else if (_scl && scl)
	{
		// SCL high: START / STOP conditions
		if (_sda && !sda)
		{
			// START: first byte is the select byte
			_stable = false;
			_tx = false;
			_rx = true;
			_mode = NV_COM;
			_bitCount = 8;
		}
		else if (!_sda && sda)
		{
			// STOP: commit the write page (unless write-protected)
			if (_mode == NV_WRITE && !wp)
			{
				_writePos &= 0x0F;
				for (uint8_t i = 0; i < _writePos; i++)
				{
					_nvram[_address] = _writeBuffer[i];
					if ((_address & 0xFF) == 0xFF)
						_address &= 0x700; // LC16: the address cycles inside a page
					else
						_address = static_cast<uint16_t>((_address + 1) & 0x7FF);
				}
			}
			_mode = NV_IDLE;
			_stable = false;
			_rx = false;
			_tx = false;
		}
	}
	else if (_scl && !scl)
	{
		// SCL falling edge
		if (_ack)
		{
			_ack = false; // ACK holds the line low for exactly one clock
		}
		else
		{
			if (_tx)
			{
				// Shift the next bit to the MSB position for the host to sample
				_data = static_cast<uint8_t>(_data << 1);
			}
			else if (_rx && _stable)
			{
				// Sample the host's SDA into the shift register
				_data = static_cast<uint8_t>((_data << 1) | (_sda ? 1 : 0));
				_bitCount--;

				if (_bitCount == 0)
				{
					switch (_mode)
					{
						case NV_COM:
							if ((_data & 0xF0) == 0xA0)
							{
								if (_data & 1)
								{
									// Read select: switch to transmit on the next rising edge
									_bitCount = 0;
									_rx = false;
									_tx = true;
								}
								else
								{
									// Write select: bits 3-1 carry the high address bits A10-A8
									_address = static_cast<uint16_t>((_address & 0x0FF) | ((_data & 0x0E) << 7));
									_bitCount = 8;
									_mode = NV_ADR;
								}
								_ack = true; // ACK the select byte
							}
							else
							{
								// Not a 1010xxxxx select - ignore the transaction
								_mode = NV_IDLE;
								_stable = false;
								_ack = false;
								_rx = false;
								_tx = false;
							}
							break;

						case NV_ADR:
								_address = static_cast<uint16_t>((_address & 0x700) | _data);
								_bitCount = 8;
								_mode = NV_WRITE;
								_ack = true;
								_writePos = 0;
								break;

						case NV_WRITE:
								_writeBuffer[_writePos & 0x0F] = _data;
								_writePos++;
								_ack = true;
								break;

						default:
							break;
					}
				}
			}
		}
	}

	_sda = sda;
	_scl = scl;
}

bool SMUCNvram::ReadSerialLink() const
{
	// Open-collector data line as seen on #FFBA bit 6: pulled low while
	// ACKing, while idle and while receiving (Xpeccy LC16 convention); in
	// transmit mode the current shift-register MSB drives the line
	if (_ack || _mode == NV_IDLE)
		return false;

	if (_tx)
		return (_data & 0x80) != 0;

	return false;
}

void SMUCNvram::ResetSerialLinkState()
{
	_mode = NV_IDLE;
	_stable = false;
	_tx = false;
	_rx = false;
	_ack = false;
	_bitCount = 0;
	_data = 0;
	_address = 0;
	_writePos = 0;
	_sda = 1;
	_scl = 1;
}

/// endregion </Serial-link EEPROM (SMUC #FFBA)>

void SMUCNvram::SetCMOSType(CMOSTypeEnum type)
{
	_cmos_type = type;
}

void SMUCNvram::SetCMOSAddress(uint8_t addr)
{
	_cmos_addr = addr;
}

void SMUCNvram::WriteCMOS(uint8_t val)
{
	uint8_t cur_addr = _cmos_addr;

	if (_cmos_type == Rus512)
		cur_addr = cur_addr & 0x3F;
		
	_cmos[cur_addr] = val;
}

uint8_t SMUCNvram::ReadCMOS()
{
	tm& time = _lastTime;
	bool& UF = _updateFinished;

	uint8_t result = 0;
	uint8_t cur_addr = _cmos_addr;

	if (_cmos_type == Rus512)
		cur_addr = cur_addr & 0x3F;

	// If Time/Date values requested from CMOS - provide current Host system values
	if ((1 << cur_addr) & ((1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 12)))
	{
		// Deterministic mode serves the frozen instant; the function-local
		// statics this replaces also leaked time state across instances
		time = make_utc_tm(_fixedTime
					   ? std::chrono::system_clock::from_time_t(_fixedTimeValue)
					   : std::chrono::system_clock::now());
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

void SMUCNvram::SetFixedTime(time_t t)
{
	_fixedTime = true;
	_fixedTimeValue = t;
}

void SMUCNvram::UseLiveTime()
{
	_fixedTime = false;
}

// Helper methods
uint8_t SMUCNvram::DecodeFromBCD(uint8_t binary)
{
	if (!(_cmos[11] & 0x04))
		binary = (binary % 10) + 0x10 * ((binary / 10) % 10);

	return binary;
}