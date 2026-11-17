/* Entry points of the Exomizer unit (exomizerunit.c); not thread-safe, the caller serialises them */
#ifndef UNREALASM_EXOMIZERUNIT_H
#define UNREALASM_EXOMIZERUNIT_H

#ifdef __cplusplus
extern "C"
{
#endif

    /* What rasm's Exomizer_crunch returns for the input: a malloc'ed buffer (UnrealAsmExomizerFree), its length set */
    unsigned char* UnrealAsmExomizerCrunch(const unsigned char* input, int inputLength, int* outputLength);
    void UnrealAsmExomizerFree(unsigned char* data);

#ifdef __cplusplus
}
#endif

#endif
