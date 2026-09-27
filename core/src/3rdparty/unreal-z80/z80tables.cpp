// z80tables.cpp - flag/decode tables of the standalone Z80 core (unreal-z80).
//
// The static const tables (inc_f/dec_f/rlc_f/rrc_f/rl0/rl1/rr0/rr1/sra_f) are
// extracted verbatim from core/src/emulator/cpu/cputables.h into
// z80tables-data.inc. The large computed tables (adc_f/sbc_f/cp_f/cpf8b/log_f,
// rol/ror and the derived rlca_f/rrca_f) are built once at library init,
// ports of CPUTables::MakeADC/MakeSBC/MakeLogicTable/MakeRotationsTables.

#include "z80cpu-internal.h"

namespace Z80Lib  // see z80cpu-internal.h
{

// Static tables, byte-for-byte identical to the core's cputables.h members.
#include "z80tables-data.inc"

// Computed tables (the core keeps these as CPUTables class members).
uint8_t log_f[0x100];
uint8_t adc_f[0x20000];
uint8_t sbc_f[0x20000];
uint8_t cp_f[0x10000];
uint8_t cpf8b[0x10000];
uint8_t rol[0x100];
uint8_t ror[0x100];
uint8_t rlca_f[0x100];
uint8_t rrca_f[0x100];

static bool tablesReady = false;

// Port of CPUTables::MakeADC(): flags for x + y + carry.
static void MakeAdc()
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

                adc_f[c * 0x10000 + x * 0x100 + y] = flag;
            }
        }
    }
}

// Port of CPUTables::MakeSBC(): flags for x - y - carry, the CP table and
// the block-CPI flag table (cpf8b).
static void MakeSbc()
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

                sbc_f[c * 0x10000 + x * 0x100 + y] = fl;
            }
        }
    }

    for (int i = 0; i < 0x10000; i++)
    {
        cp_f[i] = (sbc_f[i] & ~(F3 | F5)) | (i & (F3 | F5));

        uint8_t tempbyte = (i >> 8) - (i & 0xFF) - ((sbc_f[i] & HF) >> 4);
        cpf8b[i] = (sbc_f[i] & ~(F3 | F5 | PV | CF)) + (tempbyte & F3) + ((tempbyte << 4) & F5);
    }
}

// Port of CPUTables::MakeLogicTable(): SZ+PV(even parity)+X/Y for AND/OR/XOR.
static void MakeLogicTable()
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

        log_f[x] = fl | p;
    }

    log_f[0] |= ZF;
}

// Port of CPUTables::MakeRotationsTables(): RLCA/RRCA flag variants
// (carry-only, X/Y cleared - mask 0x3B) and rol/ror byte tables.
static void MakeRotationsTables()
{
    for (int i = 0; i < 0x100; i++)
    {
        // rra,rla uses same tables
        rlca_f[i] = rlc_f[i] & 0x3B;
        rrca_f[i] = rrc_f[i] & 0x3B;

        rol[i] = (i << 1) + (i >> 7);
        ror[i] = (i >> 1) + (i << 7);
    }
}

void Z80TablesInit(void)
{
    if (tablesReady)
        return;


    MakeAdc();
    MakeSbc();
    MakeLogicTable();
    MakeRotationsTables();

    tablesReady = true;
}

}  // namespace Z80Lib
