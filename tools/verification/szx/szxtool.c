/*
 * szxtool: SZX reference files and an outside reader, through libspectrum
 * (the library Fuse uses to read and write SZX).
 *
 *   szxtool convert <in.sna|z80|szx> <out>        read any snapshot, write SZX (or .z80 / .sna by extension)
 *   szxtool synth <machine> <out>                 a synthetic snapshot with known values (same output rule)
 *   szxtool dump <file>                           the state as libspectrum reads it
 *
 * Machines for synth: 48 128 plus2 plus2a plus3 pentagon pentagon512
 * pentagon1024 scorpion. See README.md.
 */

#include <libspectrum.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char* ReadFile(const char* path, size_t* length)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* data = malloc(size > 0 ? (size_t)size : 1);
    *length = fread(data, 1, (size_t)size, f);
    fclose(f);
    return data;
}

/* SZX, or .z80 / .sna by the output file's extension */
static int WriteSzx(libspectrum_snap* snap, const char* path)
{
    const char* dot = strrchr(path, '.');
    libspectrum_id_t type = LIBSPECTRUM_ID_SNAPSHOT_SZX;
    if (dot && !strcmp(dot, ".z80"))
        type = LIBSPECTRUM_ID_SNAPSHOT_Z80;
    else if (dot && !strcmp(dot, ".sna"))
        type = LIBSPECTRUM_ID_SNAPSHOT_SNA;
    libspectrum_creator* creator = libspectrum_creator_alloc();
    libspectrum_creator_set_program(creator, "szxtool");
    libspectrum_creator_set_major(creator, 1);
    libspectrum_creator_set_minor(creator, 0);
    libspectrum_byte* buffer = NULL;
    size_t length = 0;
    int flags = 0;
    libspectrum_error error = libspectrum_snap_write(&buffer, &length, &flags, snap, type, creator, 0);
    libspectrum_creator_free(creator);
    if (error)
        return 1;
    FILE* f = fopen(path, "wb");
    if (!f)
        return 1;
    fwrite(buffer, 1, length, f);
    fclose(f);
    libspectrum_free(buffer);
    if (flags & LIBSPECTRUM_FLAG_SNAPSHOT_MAJOR_INFO_LOSS)
        fprintf(stderr, "warning: major information loss\n");
    return 0;
}

/* CRC-32 (IEEE), for page contents in dumps */
static uint32_t Crc32(const unsigned char* data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static int Pages(libspectrum_machine machine)
{
    switch (machine)
    {
        case LIBSPECTRUM_MACHINE_PENT512: return 32;
        case LIBSPECTRUM_MACHINE_PENT1024: return 64;
        case LIBSPECTRUM_MACHINE_SCORP: return 16;
        default: return 8;
    }
}

static int Dump(const char* path)
{
    size_t length = 0;
    unsigned char* data = ReadFile(path, &length);
    if (!data)
        return 1;
    libspectrum_snap* snap = libspectrum_snap_alloc();
    if (libspectrum_snap_read(snap, data, length, LIBSPECTRUM_ID_UNKNOWN, path))
        return 1;
    libspectrum_machine machine = libspectrum_snap_machine(snap);
    printf("machine=%s\n", libspectrum_machine_name(machine));
    printf("a=%d f=%d bc=%d de=%d hl=%d\n", libspectrum_snap_a(snap), libspectrum_snap_f(snap), libspectrum_snap_bc(snap),
           libspectrum_snap_de(snap), libspectrum_snap_hl(snap));
    printf("a_=%d f_=%d bc_=%d de_=%d hl_=%d\n", libspectrum_snap_a_(snap), libspectrum_snap_f_(snap), libspectrum_snap_bc_(snap),
           libspectrum_snap_de_(snap), libspectrum_snap_hl_(snap));
    printf("ix=%d iy=%d sp=%d pc=%d i=%d r=%d\n", libspectrum_snap_ix(snap), libspectrum_snap_iy(snap), libspectrum_snap_sp(snap),
           libspectrum_snap_pc(snap), libspectrum_snap_i(snap), libspectrum_snap_r(snap));
    printf("iff1=%d iff2=%d im=%d memptr=%d tstates=%u\n", libspectrum_snap_iff1(snap), libspectrum_snap_iff2(snap),
           libspectrum_snap_im(snap), libspectrum_snap_memptr(snap), (unsigned)libspectrum_snap_tstates(snap));
    printf("halted=%d ei=%d setf=%d\n", libspectrum_snap_halted(snap), libspectrum_snap_last_instruction_ei(snap),
           libspectrum_snap_last_instruction_set_f(snap));
    printf("ula=%d p7ffd=%d p1ffd=%d\n", libspectrum_snap_out_ula(snap), libspectrum_snap_out_128_memoryport(snap),
           libspectrum_snap_out_plus3_memoryport(snap));
    printf("ay_port=%d ay=", libspectrum_snap_out_ay_registerport(snap));
    for (int i = 0; i < 16; i++)
        printf("%s%d", i ? "," : "", libspectrum_snap_ay_registers(snap, i));
    printf("\nbeta_active=%d beta_paged=%d beta_system=%d\n", libspectrum_snap_beta_active(snap), libspectrum_snap_beta_paged(snap),
           libspectrum_snap_beta_system(snap));
    for (int page = 0; page < 64; page++)
    {
        libspectrum_byte* bytes = libspectrum_snap_pages(snap, page);
        if (bytes)
            printf("page%d=%08x\n", page, (unsigned)Crc32(bytes, 0x4000));
    }
    libspectrum_snap_free(snap);
    free(data);
    return 0;
}

static int Convert(const char* in, const char* out)
{
    size_t length = 0;
    unsigned char* data = ReadFile(in, &length);
    if (!data)
        return 1;
    libspectrum_snap* snap = libspectrum_snap_alloc();
    if (libspectrum_snap_read(snap, data, length, LIBSPECTRUM_ID_UNKNOWN, in))
        return 1;
    int result = WriteSzx(snap, out);
    libspectrum_snap_free(snap);
    free(data);
    return result;
}

/* Known values: page n holds (n * 16 + offset / 1024) at every byte, the
 * registers count up from #1122, the CPU sits mid-frame with MEMPTR, Q and
 * the EI shadow set, and the paging ports select page 3 at #C000 */
static int Synth(const char* name, const char* out)
{
    struct
    {
        const char* name;
        libspectrum_machine machine;
    } machines[] = {{"48", LIBSPECTRUM_MACHINE_48},
                    {"128", LIBSPECTRUM_MACHINE_128},
                    {"plus2", LIBSPECTRUM_MACHINE_PLUS2},
                    {"plus2a", LIBSPECTRUM_MACHINE_PLUS2A},
                    {"plus3", LIBSPECTRUM_MACHINE_PLUS3},
                    {"pentagon", LIBSPECTRUM_MACHINE_PENT},
                    {"pentagon512", LIBSPECTRUM_MACHINE_PENT512},
                    {"pentagon1024", LIBSPECTRUM_MACHINE_PENT1024},
                    {"scorpion", LIBSPECTRUM_MACHINE_SCORP}};
    libspectrum_machine machine = LIBSPECTRUM_MACHINE_UNKNOWN;
    for (size_t i = 0; i < sizeof(machines) / sizeof(machines[0]); i++)
        if (!strcmp(name, machines[i].name))
            machine = machines[i].machine;
    if (machine == LIBSPECTRUM_MACHINE_UNKNOWN)
        return 2;

    libspectrum_snap* snap = libspectrum_snap_alloc();
    libspectrum_snap_set_machine(snap, machine);
    libspectrum_snap_set_a(snap, 0x11);
    libspectrum_snap_set_f(snap, 0x22);
    libspectrum_snap_set_bc(snap, 0x3344);
    libspectrum_snap_set_de(snap, 0x5566);
    libspectrum_snap_set_hl(snap, 0x7788);
    libspectrum_snap_set_a_(snap, 0x99);
    libspectrum_snap_set_f_(snap, 0xAA);
    libspectrum_snap_set_bc_(snap, 0xBBCC);
    libspectrum_snap_set_de_(snap, 0xDDEE);
    libspectrum_snap_set_hl_(snap, 0xF001);
    libspectrum_snap_set_ix(snap, 0x1234);
    libspectrum_snap_set_iy(snap, 0x5C3A);
    libspectrum_snap_set_sp(snap, 0xBEEF);
    libspectrum_snap_set_pc(snap, 0x8000);
    libspectrum_snap_set_i(snap, 0x3F);
    libspectrum_snap_set_r(snap, 0x85);
    libspectrum_snap_set_iff1(snap, 1);
    libspectrum_snap_set_iff2(snap, 1);
    libspectrum_snap_set_im(snap, 2);
    libspectrum_snap_set_memptr(snap, 0x4321);
    libspectrum_snap_set_tstates(snap, 12345);
    libspectrum_snap_set_last_instruction_ei(snap, 1);
    libspectrum_snap_set_last_instruction_set_f(snap, 1);
    libspectrum_snap_set_out_ula(snap, 0x05);  /* border 5 */
    if (machine != LIBSPECTRUM_MACHINE_48)
    {
        libspectrum_snap_set_out_128_memoryport(snap, 0x13);  /* page 3, screen 5, ROM 1 */
        libspectrum_snap_set_out_ay_registerport(snap, 7);
        for (int r = 0; r < 16; r++)
            libspectrum_snap_set_ay_registers(snap, r, (libspectrum_byte)(r * 3 + 1));
        libspectrum_snap_set_ay_registers(snap, 7, 0x38);
    }
    if (machine == LIBSPECTRUM_MACHINE_PLUS2A || machine == LIBSPECTRUM_MACHINE_PLUS3)
        libspectrum_snap_set_out_plus3_memoryport(snap, 0x04);  /* normal paging, ROM high bit */
    if (machine == LIBSPECTRUM_MACHINE_SCORP)
        libspectrum_snap_set_out_plus3_memoryport(snap, 0x00);
    if (machine == LIBSPECTRUM_MACHINE_PENT1024)
        libspectrum_snap_set_out_plus3_memoryport(snap, 0x04);  /* #EFF7: 128K compatibility */
    if (machine == LIBSPECTRUM_MACHINE_PENT || machine == LIBSPECTRUM_MACHINE_PENT512 || machine == LIBSPECTRUM_MACHINE_PENT1024 ||
        machine == LIBSPECTRUM_MACHINE_SCORP)
    {
        libspectrum_snap_set_beta_active(snap, 1);
        libspectrum_snap_set_beta_drive_count(snap, 4);
        libspectrum_snap_set_beta_system(snap, 0x3C);
    }

    const int pages48[] = {5, 2, 0};
    const int count = machine == LIBSPECTRUM_MACHINE_48 ? 3 : Pages(machine);
    for (int n = 0; n < count; n++)
    {
        /* libspectrum keeps pages by their 128K number: a 48K has 5, 2 and 0 */
        const int page = machine == LIBSPECTRUM_MACHINE_48 ? pages48[n] : n;
        libspectrum_byte* bytes = libspectrum_new(libspectrum_byte, 0x4000);
        for (int offset = 0; offset < 0x4000; offset++)
            bytes[offset] = (libspectrum_byte)(page * 16 + offset / 1024);
        libspectrum_snap_set_pages(snap, page, bytes);
    }
    int result = WriteSzx(snap, out);
    libspectrum_snap_free(snap);
    return result;
}

int main(int argc, char** argv)
{
    if (libspectrum_init())
        return 1;
    if (argc == 3 && !strcmp(argv[1], "dump"))
        return Dump(argv[2]);
    if (argc == 4 && !strcmp(argv[1], "convert"))
        return Convert(argv[2], argv[3]);
    if (argc == 4 && !strcmp(argv[1], "synth"))
        return Synth(argv[2], argv[3]);
    fprintf(stderr, "usage: szxtool dump <file> | convert <in> <out.szx> | synth <machine> <out.szx>\n");
    return 2;
}
