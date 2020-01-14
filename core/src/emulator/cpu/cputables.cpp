#include "stdafx.h"

#include "cputables.h"

//
// Generate table with all ADD/ADC operation flags
//
void CPUTables::MakeADC()
{
	for (int c = 0; c < 2; c++)
	{
		for (int x = 0; x < 0x100; x++)
		{
			for (int y = 0; y < 0x100; y++)
			{
				uint32_t res = x + y + c;
				uint8_t flag = 0;

				if (!(res & 0xFF))
					flag |= ZF;

				flag |= (res & (F3 | F5 | SF));

				if (res >= 0x100)
					flag |= CF;

				if (((x & 0x0F) + (y & 0x0F) + c) & 0x10)
					flag |= HF;

				int32_t ri = (int8_t)x + (int8_t)y + c;

				if (ri >= 0x80 || ri <= -0x81)
					flag |= PV;

				add_flags[c * 0x10000 + x * 0x100 + y] = flag;
			}
		}
	}
}

void CPUTables::MakeSBC()
{
	for (int c = 0; c < 2; c++)
	{
		for (int x = 0; x < 0x100; x++)
		{
			for (int y = 0; y < 0x100; y++)
			{
				int32_t res = x - y - c;
				uint8_t fl = res & (F3 | F5 | SF);

				if (!(res & 0xFF))
					fl |= ZF;

				if (res & 0x10000)
					fl |= CF;

				int32_t r = (int8_t)x - (int8_t)y - c;

				if (r >= 0x80 || r < -0x80)
					fl |= PV;

				if (((x & 0x0F) - (res & 0x0F) - c) & 0x10)
					fl |= HF;

				fl |= NF;

				sub_flags[c * 0x10000 + x * 0x100 + y] = fl;
			}
		}
	}

	for (int i = 0; i < 0x10000; i++)
	{
		cp_flags[i] = (sub_flags[i] & ~(F3 | F5)) | (i & (F3 | F5));

		uint8_t tempbyte = (i >> 8) - (i & 0xFF) - ((sub_flags[i] & HF) >> 4);
		cpf8b[i] = (sub_flags[i] & ~(F3 | F5 | PV | CF)) + (tempbyte & F3) + ((tempbyte << 4) & F5);
	}
}

void CPUTables::MakeLog()
{
	for (int x = 0; x < 0x100; x++)
	{
		uint8_t fl = x & (F3 | F5 | SF);
		uint8_t p = PV;

		for (int i = 0x80; i; i /= 2)
		{
			if (x & i)
				p ^= PV;
		}

		logic_flags[x] = fl | p;
	}

	logic_flags[0] |= ZF;
}

void CPUTables::MakeRot()
{
	for (int i = 0; i < 0x100; i++)
	{
		// rra,rla uses same tables
		rlca_flags[i] = rlc_flags[i] & 0x3B;
		rrca_flags[i] = rrc_flags [i] & 0x3B;

		rol_table[i] = (i << 1) + (i >> 7);
		ror_table[i] = (i >> 1) + (i << 7);
	}
}

CPUTables::CPUTables()
{
	InitCPUTables();
}

CPUTables::~CPUTables()
{
}

void CPUTables::InitCPUTables()
{
	MakeADC();
	MakeSBC();
	MakeLog();
	MakeRot();
}