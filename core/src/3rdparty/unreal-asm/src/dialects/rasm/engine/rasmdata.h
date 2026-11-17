#pragma once

// Stateless data helpers of the rasm engine: ports of rasm.c (v3.3.3) functions that turn plain values into the bytes rasm
// writes, byte for byte. No engine state: everything a function needs comes in as arguments, results come out as bytes
// plus rasm's messages. Spec, quirks and verification: docs/inprogress/2026-10-05-unreal-asm/research-rasm-data.md;
// fixtures: testdata/rasm/data (tools/verification/unreal-asm/checks/rasmdatacheck.py regenerates them from the rasm binary).
//
// Messages are rasm's texts without the "[file:line] " prefix, the "Warning: " word and the trailing newline; the engine
// adds its own location. Where rasm reports an error and goes on, the helper stops at the first error (an error fails the
// assembly anyway, so later bytes are never written).
//
// Floating point: rasm computes in IEEE doubles; the helpers do the same operations in the same order with no libm
// transcendental call (powers of two are std::ldexp, exact), and rasmdata.cpp builds with -ffp-contract=off (/fp:precise),
// so the results are identical on every platform. The reference rasm binary (arm64 macOS, clang -O2) fuses a few
// multiply-adds (WAV sample scaling, the DMA filter); the port writes those as explicit std::fma, which is correctly
// rounded everywhere. Where rasm's result depends on its build (double -> integer conversion out of range, plain char
// signedness), the helpers reproduce the reference binary and say so.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace unrealasm::rasm
{
using Bytes = std::vector<std::uint8_t>;

// Bytes and rasm's messages. error empty = success.
struct DataResult
{
    Bytes bytes;
    std::string error;
    std::vector<std::string> warnings;
    bool Ok() const { return error.empty(); }
};

// ---------------------------------------------------------------------------------------------------------------------
// 1. Floating point data: DEFR / DR, DEFF / DF, DF24
// ---------------------------------------------------------------------------------------------------------------------

enum class RealStatus
{
    Ok,
    ExponentOverflow,  // rasm: error "Exponent overflow", writes the bytes with exponent 128 (unreachable in practice)
    Hangs,             // rasm never returns on this value (its bit search loops forever): the engine reports an error
};

struct Real40
{
    std::array<std::uint8_t, 5> bytes{};
    RealStatus status = RealStatus::Ok;
};

// Locomotive BASIC 40-bit real (__internal_MakeAmsdosREAL): 4 mantissa bytes little endian (bit 31 replaced by the sign),
// then exponent + 128. DEFR / DR.
Real40 MakeAmsdosReal(double value);
// Microsoft 40-bit real (__internal_MakeRosoftREAL): exponent + 128 first, then the mantissa most significant byte first.
// DEFF / DF.
Real40 MakeMicrosoftReal(double value);
// Float24 (z80float f24): the value rounded to an IEEE float, then exponent - 64 with the sign in bit 7, mantissa bits
// 7..14, mantissa bits 15..22. DF24.
std::array<std::uint8_t, 3> MakeFloat24(double value);

inline constexpr std::string_view kRealOverflowMessage = "Exponent overflow";
inline constexpr std::string_view kRealHangsMessage = "value cannot be converted to a real (rasm loops forever on it)";

// ---------------------------------------------------------------------------------------------------------------------
// 2. INCBIN transforms and WAV import
// ---------------------------------------------------------------------------------------------------------------------

enum class IncbinTiles
{
    None,
    ITiles,  // ITILES,w: 8-line tiles, gray line order 0,1,3,2,6,7,5,4 and every other line mirrored (zigzag)
    GTiles,  // GTILES,w: 8-line tiles, gray line order only
};

enum class IncbinSlice
{
    None,
    Slice,   // SLICE,w,h
    ISlice,  // ISLICE,w,h: SLICE after the gray line swap of each 8-line group
};

// INCBIN 'file'[,offset[,size[,offsetHigh]]][,options] as rasm parsed it. Integers in order: offset, size, then
// offset += 65536 * third. Options keep "given" apart from their value where rasm does (REMAP 0 is an error, VTILES 0 is
// "no VTILES").
struct IncbinOptions
{
    int offset = 0;
    int size = 0;  // 0: up to the end; negative: file size + size
    bool skipHeader = false;
    bool revert = false;
    bool repack = false;
    std::optional<int> remap;   // REMAP,columns
    int vtiles = 0;             // VTILES,lines (0 = off, as in rasm)
    IncbinTiles tiles = IncbinTiles::None;
    int tileWidth = 0;          // ITILES / GTILES width
    IncbinSlice slice = IncbinSlice::None;
    int sliceWidth = 0;
    int sliceHeight = 0;
    bool misaligned = false;    // SLICE / ISLICE ...,MISALIGNED
};

struct IncbinResult : DataResult
{
    // SLICE / ISLICE without MISALIGNED: before the bytes, the engine writes zeros up to the next 256-byte boundary of
    // the code address and, if it wrote any, warns "INCBIN ISLICE was automatically aligned, use MISALIGNED to force in
    // place".
    bool alignTo256 = false;
    // Size of the selected area (rasm's `size` after the offset / size rules): the engine's crunch check for INCL48 & co
    // is "Cannot crunch [file]" when the crunched length is 0 or >= this. When it is 0, rasm does not crunch and warns
    // "wont try to crunch no data at all" (already in warnings).
    int selectedSize = 0;
};

// The bytes INCBIN writes for a file (rasm reloads the file on every execution, so the transforms that rasm applies in
// place to its buffer do not leak from one execution to the next).
IncbinResult IncbinTransform(std::span<const std::uint8_t> file, const IncbinOptions& options);

// SKIPHEADER's AMSDOS header detection rule (file longer than 128 bytes, checksum, type, lengths).
bool HasAmsdosHeader(std::span<const std::uint8_t> file);

enum class WavMode
{
    Smp,  // INCBIN 'f',SMP[,amplification]: one PSG level per byte
    Sm2,  // SM2: two 4-bit PSG levels per byte
    Sm4,  // SM4: four 2-bit levels per byte
    Dma,  // DMA[,DMA_INT][,DMA_REPEAT,n][,DMA_CHANNEL_A|B|C]: Amstrad Plus DMA list
};

enum class DmaChannel
{
    A,
    B,
    C,
};

struct WavOptions
{
    WavMode mode = WavMode::Smp;
    double amplification = 1.0;  // SMP / SM2 / SM4 only (rasm stores it as a float); DMA always uses 1.0
    DmaChannel channel = DmaChannel::C;
    bool dmaInt = false;
    std::optional<int> dmaRepeat;  // DMA_REPEAT,n: 1..4095, else an error (rasm truncates the expression)
};

// rasm's DMA band-pass filter keeps its state in static variables: it carries over from one DMA import to the next one in
// the same rasm run. The engine keeps one per assembly.
struct AudioFilterState
{
    float oldOldS = 0.0f;
    float oldS = 0.0f;
    float s = 0.0f;
    float oldOldE = 0.0f;
    float oldE = 0.0f;
};

// The bytes of INCBIN 'file',SMP|SM2|SM4|DMA (the whole file: offset, size and other options are ignored, as in rasm).
DataResult ImportWav(std::span<const std::uint8_t> file, const WavOptions& options, AudioFilterState& filter);

// PSG level tables rasm builds at start-up (psgfine: PSGVALUE() and SMP / SM2; psgtab: SM4 and the first DMA sample;
// dmatab: DMA samples).
const std::array<std::uint8_t, 256>& PsgFineTable();
const std::array<std::uint8_t, 256>& PsgTable();
const std::array<std::uint8_t, 256>& DmaTable();

// ---------------------------------------------------------------------------------------------------------------------
// 3. Headers, memory checks and CPC colours
// ---------------------------------------------------------------------------------------------------------------------

// MakeAMSDOS_name: "[user:][path/]name.ext" -> 8+3 upper-case space-padded name and the user number. warnings gets
// "filename [x] too long for AMSDOS, will be truncated" (rasm prints it with "Warning - ").
std::array<char, 11> MakeAmsdosName(std::string_view filename, int& user, std::vector<std::string>* warnings = nullptr);

inline constexpr std::string_view kRasmSnapVersion = "RASM v3.3";  // RASM_SNAP_VERSION, written in AMSDOS headers

// _internal_MakeAMSDOSHeader: 128 bytes. type 2 = binary (SAVE, AMSDOS directive). length = maxmem - minmem.
std::array<std::uint8_t, 128> MakeAmsdosHeader(int run, int minmem, int maxmem, std::span<const char, 11> name, int user,
                                               int type = 2, std::string_view version = kRasmSnapVersion);
// MakeHobetaHeader: 17 bytes; rasm passes the AMSDOS name of the file (first 8 characters used).
std::array<std::uint8_t, 17> MakeHobetaHeader(int minmem, int maxmem, std::string_view trdosName);

// SAVE 'file',offset,size[,run],AMSDOS / HOBETA / plain: the file contents. bank is the 64K (or LIMIT ...,EXTENDED) bank
// image; run < 0 means "not given" (run = offset). Errors are PopAllSave's range checks (savelimit 65536 for AMSDOS /
// HOBETA, the bank size for a plain SAVE).
enum class SaveFormat
{
    Plain,
    Amsdos,
    Hobeta,
};
DataResult MakeSaveFile(std::string_view filename, std::span<const std::uint8_t> bank, int offset, int size, int run,
                        SaveFormat format);

// Pokers (run at the end of assembly in declaration order on the bank memory; start / end are rasm's evaluated values,
// end exclusive). Indices outside the bank are skipped (rasm reads / writes outside its buffer there).
std::uint8_t SumMem(std::span<const std::uint8_t> bank, int start, int end);
std::uint16_t Sum16(std::span<const std::uint8_t> bank, int start, int end);
std::uint8_t XorMem(std::span<const std::uint8_t> bank, int start, int end);
// CIPHERMEM start,end[,method[,'key']]: method 1 (default, also 0 and any unknown value), 2, 3 or 4. key = the text
// between the quotes as rasm holds it (no escape decoding); method 4 needs at least 2 characters, else the error
// "CIPHER needs a string of one char or more to run properly".
std::string CipherMem(std::span<std::uint8_t> bank, int start, int end, int method, std::string_view key = {});

// CPC colour functions of expressions. Arguments as rasm's accumulator (double, truncated toward zero).
std::optional<int> Soft2HardInk(double soft);  // nullopt: "SOFT2HARD_INK needs 0-26 color index" (value 0)
int Hard2SoftInk(double hard);
int GetR(double colour);
int GetV(double colour);
int GetB(double colour);
int SetR(double level);
int SetV(double level);
int SetB(double level);
inline constexpr std::string_view kSoft2HardError = "SOFT2HARD_INK needs 0-26 color index";

// ---------------------------------------------------------------------------------------------------------------------
// 4. ZX output: BUILDZX snapshot (48K .sna)
// ---------------------------------------------------------------------------------------------------------------------

// Bank images as rasm holds them (64K each, the 16K page is the first 16K of the bank; absent = zeros). With BANKSET 0,
// bank0 is the flat 64K memory and the snapshot is its upper 48K. RUN pc,sp: pc is written at bank0[sp] (so it only lands
// in the snapshot when that address is saved: sp >= #4000 with BANKSET 0, sp < #4000 otherwise).
struct ZxSnapshotInput
{
    std::span<const std::uint8_t> bank0;
    std::span<const std::uint8_t> bank2;
    std::span<const std::uint8_t> bank5;
    bool bankset0 = false;
    unsigned run = 0;
    unsigned stack = 0;
};
Bytes BuildZxSnapshot(const ZxSnapshotInput& input);

// ---------------------------------------------------------------------------------------------------------------------
// 5. Strings: escapes, UTF-8, CHARSET
// ---------------------------------------------------------------------------------------------------------------------

enum class StringDirective
{
    Defb,         // DEFB / DB: \e \a \b \v \f \0 \r \n \t \xHH, "\\" through CHARSET, other "\c" = c raw
    DefbStruct,   // DEFB / DB while declaring a STRUCT: \b \v \f \0 \r \n \t only
    DefbAs80,     // DEFB with -ass / -pasmo: no escapes ("\c" = c through CHARSET), no UTF8REMAP
    Str,          // STR: \b \v \f \0 \r \n \t \xHH, "\\" raw, no UTF8REMAP, bit 7 on the last character (see markEnd)
    StructField,  // a string initialising a STRUCT field: as Defb, at most maxBytes characters
};

struct Utf8Remap
{
    Bytes code;  // UTF-8 bytes (up to 15)
    std::uint8_t value = 0;
};

// UTF8REMAP 'c',value: quotedWord with its quotes as rasm holds it. nullopt when the word is not a quoted string (rasm
// ignores the directive then).
std::optional<Utf8Remap> ParseUtf8Remap(std::string_view quotedWord, int value);

struct StringResult : DataResult
{
    int count = 0;         // StructField: characters consumed against maxBytes (rasm's l)
    bool overrun = false;  // StructField: "struct field number %d overrun! (%d byte%s max)!" applies
};

struct StringOptions
{
    StringDirective directive = StringDirective::Defb;
    bool markEnd = false;  // Str: this string is the last argument or is followed by another quoted string
    int maxBytes = 0;      // StructField: field size
};

// The bytes of one quoted argument (quotedWord with its quotes, as the preprocessor left it).
StringResult DecodeQuotedString(std::string_view quotedWord, const std::array<std::uint8_t, 256>& charset,
                                std::span<const Utf8Remap> remap, const StringOptions& options);

// A character literal 'c' / '\c' in an expression: escaped characters skip CHARSET (\b \v \f \0 \r \n \t, others as is).
int EscapedCharValue(char c);

// -utf8: the preprocessor rewrites each quoted word (é è à ù ç ñ Ñ ¿ ¡); errors "Unsupported UTF8 char for quoted string".
std::string ApplyUtf8Option(std::string_view quotedWord, std::vector<std::string>* errors);
// Without -utf8 and -fq: rasm's check of a quoted word (bytes 32..126 and tab only); one error per bad byte, as rasm.
std::vector<std::string> CheckQuotedChars(std::string_view quotedWord);

// CHARSET: the default table is the identity.
std::array<std::uint8_t, 256> DefaultCharset();
// CHARSET 'abc','xyz'
std::string CharsetMapStrings(std::array<std::uint8_t, 256>& charset, std::string_view from, std::string_view to);
// CHARSET 'abc',value
std::string CharsetMapString(std::array<std::uint8_t, 256>& charset, std::string_view from, int value);
// CHARSET code,value
std::string CharsetMapByte(std::array<std::uint8_t, 256>& charset, int code, int value);
// CHARSET start,end,value
std::string CharsetMapRange(std::array<std::uint8_t, 256>& charset, int start, int end, int value);
}  // namespace unrealasm::rasm
