import std.core;
import std.io;
import std.mem;

namespace scl_format
{
    const u32 SECTOR_SIZE = 256;
    const u32 MAX_FILES = 128;
    const u32 HEADER_SIZE = 9;
    const u32 FILE_ENTRY_SIZE = 14;
    const u32 FILE_DATA_OFFSET = HEADER_SIZE + (MAX_FILES * FILE_ENTRY_SIZE);
    
   
    // SCL header structure
    struct SCLHeader
    {
        char signature[8];  // Must be "SINCLAIR" value
        u8 file_count;     // Number of files (1-128)
    };
    
    // SCL file entry structure
    struct SCLFileEntry
    {
        char name[8];      // File name (space padded)
        char type;         // 'B'=Basic, 'C'=Code, 'D'=Data
        u16 start_address; // Load address for CODE files
        u16 length_bytes;  // File length in bytes
        u8 length_sectors; // Length in 256-byte sectors
    };
    
    struct Sector
    {
        u8 data[SECTOR_SIZE];
    };
    
    // Complete SCL image structure
    struct SCLImage
    {
        SCLHeader header @ 0x00;
        SCLFileEntry files[header.file_count] @ HEADER_SIZE;
        
        // Compute the dynamic offset and number of sectors inline
        Sector sectors[
            (std::mem::size() - (HEADER_SIZE + header.file_count * sizeof(SCLFileEntry))) / SECTOR_SIZE
        ] @ (HEADER_SIZE + header.file_count * sizeof(SCLFileEntry));            
            
    };
}

scl_format::SCLImage scl_image @ 0x00;