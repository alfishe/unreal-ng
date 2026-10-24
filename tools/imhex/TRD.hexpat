namespace trd_dos
{
    const u32 SECTOR_SIZE = 256;
    const u32 SECTORS_PER_TRACK = 16;
    const u32 MAX_FILES = 128;
    const u32 VOLUME_INFO_TRACK = 0;
    const u32 VOLUME_INFO_SECTOR = 8; // Sector 9 (0-based index)
    const u32 VOLUME_INFO_OFFSET = (VOLUME_INFO_TRACK * SECTORS_PER_TRACK + VOLUME_INFO_SECTOR) * SECTOR_SIZE;

    enum TRDDiskType : u8
    {
        DS_80 = 0x16,
        DS_40 = 0x17,
        SS_80 = 0x18,
        SS_40 = 0x19
    };

    struct TRDFileEntry
    {
        char name[8];
        char type;
        u16 start_address;
        u16 length_bytes;
        u8 length_sectors;
        u8 start_sector;
        u8 start_track;
    };

    struct TRDVolumeInfo
    {
        u8 zero_marker;
        u8 reserved[224];

        u8 first_free_sector;
        u8 first_free_track;

        TRDDiskType disk_type;
        u8 file_count;
        u16 free_sector_count;

        u8 trdos_signature;

        u8 reserved1[2];
        u8 reserved2[9];
        u8 reserved3;
        u8 deleted_file_count;

        char label[8];
        u8 reserved4[3];
    };

    struct TRDCatalog
    {
        TRDFileEntry files[MAX_FILES];
    };

    struct TRDImage
    {
        TRDCatalog catalog @ 0x0000;  // Offset 0x0000 – 0x07FF
        TRDVolumeInfo volume_info @ VOLUME_INFO_OFFSET;
        u8 remaining_data[__size__ - VOLUME_INFO_OFFSET - sizeof(TRDVolumeInfo)];
    };
}


trd_dos::TRDImage trd_dos_trdimage_at_0x00 @ 0x00;