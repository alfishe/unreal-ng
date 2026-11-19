/* Entry points of the LZ4 unit (lz4unit.c) */
#ifndef UNREALASM_LZ4UNIT_H
#define UNREALASM_LZ4UNIT_H

#ifdef __cplusplus
extern "C"
{
#endif

    /* LZ4_compress_HC: a raw LZ4 block, 0 when it does not fit the destination */
    int UnrealAsmLz4CompressHc(const char* source, char* destination, int sourceSize, int destinationCapacity,
                               int level);
    /* LZ4_decompress_safe: the decompressed size, negative for damaged data */
    int UnrealAsmLz4DecompressSafe(const char* source, char* destination, int compressedSize, int destinationCapacity);

#ifdef __cplusplus
}
#endif

#endif
