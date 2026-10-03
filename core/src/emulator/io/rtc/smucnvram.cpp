#include "stdafx.h"

#include "smucnvram.h"

#include <cstring>

//
// SMUC battery-backed parts: clock + LC16 serial EEPROM
//
SMUCNvram::SMUCNvram()
{
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

SMUCNvram::LinkState SMUCNvram::GetLinkState() const
{
	LinkState s{};
	s.mode = static_cast<uint8_t>(_mode);
	s.flags = static_cast<uint8_t>((_stable ? 0x01 : 0) | (_tx ? 0x02 : 0) | (_rx ? 0x04 : 0) | (_ack ? 0x08 : 0));
	s.bitCount = _bitCount;
	s.data = _data;
	s.addressLow = static_cast<uint8_t>(_address & 0xFF);
	s.addressHigh = static_cast<uint8_t>(_address >> 8);
	s.writePos = _writePos;
	s.sda = _sda ? 1 : 0;
	s.scl = _scl ? 1 : 0;
	std::memcpy(s.writeBuffer, _writeBuffer, sizeof(s.writeBuffer));
	return s;
}

void SMUCNvram::SetLinkState(const LinkState& s)
{
	_mode = s.mode <= NV_WRITE ? static_cast<EEPROMMode>(s.mode) : NV_IDLE;
	_stable = (s.flags & 0x01) != 0;
	_tx = (s.flags & 0x02) != 0;
	_rx = (s.flags & 0x04) != 0;
	_ack = (s.flags & 0x08) != 0;
	_bitCount = s.bitCount;
	_data = s.data;
	_address = static_cast<uint16_t>((s.addressLow | (s.addressHigh << 8)) & 0x7FF);
	_writePos = s.writePos;   // any value: the write path masks it (& 0x0F)
	_sda = s.sda ? 1 : 0;
	_scl = s.scl ? 1 : 0;
	std::memcpy(_writeBuffer, s.writeBuffer, sizeof(_writeBuffer));
}

/// endregion </Serial-link EEPROM (SMUC #FFBA)>
