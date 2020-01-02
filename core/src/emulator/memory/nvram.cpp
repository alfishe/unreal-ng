#include "stdafx.h"

#include "nvram.h"

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