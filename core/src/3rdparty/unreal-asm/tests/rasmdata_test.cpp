// The rasm data helpers (src/dialects/rasm/engine/rasmdata.*) against rasm v3.3.3 itself: every case of
// testdata/rasm/data/cases.json was assembled by rasm (tools/verification/unreal-asm/checks/rasmdatacheck.py), the test
// feeds the same input to the port and compares bytes, the first error and the warnings.

#include <gtest/gtest.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

#include "dialects/rasm/engine/rasmdata.h"
#include "symbols/io/json.h"
#include "testdata.h"

using namespace unrealasm::rasm;
namespace json = unrealasm::symbols::json;

namespace
{
const json::Value& Cases()
{
    static const json::Value root = [] {
        json::Value value;
        std::string error;
        size_t offset = 0;
        const bool ok = json::Parse(unrealasm::testing::ReadTestText("rasm/data/cases.json"), value, error, offset);
        EXPECT_TRUE(ok) << error << " at " << offset;
        return value;
    }();
    static const json::Value empty;
    const json::Value* cases = root.Get("cases");
    return cases ? *cases : empty;
}

std::vector<const json::Value*> CasesOf(std::string_view kind)
{
    std::vector<const json::Value*> out;
    for (const json::Value& c : Cases().array)
        if (const json::Value* k = c.Get("kind"); k && k->string == kind)
            out.push_back(&c);
    return out;
}

std::string Str(const json::Value& c, std::string_view name)
{
    const json::Value* v = c.Get(name);
    return v ? v->string : std::string();
}

int64_t Int(const json::Value& c, std::string_view name)
{
    const json::Value* v = c.Get(name);
    return v ? v->integer : 0;
}

std::vector<std::string> Strings(const json::Value& c, std::string_view name)
{
    std::vector<std::string> out;
    if (const json::Value* v = c.Get(name))
        for (const json::Value& s : v->array)
            out.push_back(s.string);
    return out;
}

Bytes FromHex(std::string_view hex)
{
    Bytes out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16)));
    return out;
}

std::string ToHex(std::span<const std::uint8_t> bytes)
{
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out;
    for (const std::uint8_t b : bytes)
    {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 15]);
    }
    return out;
}

double FromBits(std::string_view hex)
{
    const std::uint64_t bits = std::stoull(std::string(hex), nullptr, 16);
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::string Fnv1a(std::span<const std::uint8_t> bytes)
{
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (const std::uint8_t b : bytes)
        h = (h ^ b) * 0x100000001B3ull;
    char text[17];
    std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(h));
    return text;
}

// The bytes rasm wrote, in the form the fixture stores them (hex, or length + hash for long outputs)
void ExpectOutput(const json::Value& c, std::span<const std::uint8_t> bytes, const std::string& label)
{
    if (c.Get("fnv"))
    {
        EXPECT_EQ(static_cast<int64_t>(bytes.size()), Int(c, "length")) << label;
        EXPECT_EQ(Fnv1a(bytes), Str(c, "fnv")) << label;
    }
    else
    {
        EXPECT_EQ(ToHex(bytes), Str(c, "bytes")) << label;
    }
}

// rasm goes on after an error; the helpers stop at the first one
void ExpectMessages(const json::Value& c, const std::string& error, const std::vector<std::string>& warnings,
                    const std::string& label)
{
    const std::vector<std::string> errors = Strings(c, "errors");
    EXPECT_EQ(error, errors.empty() ? std::string() : errors.front()) << label;
    if (errors.empty())
    {
        EXPECT_EQ(warnings, Strings(c, "warnings")) << label;
    }
}

std::vector<std::string> SplitArgs(std::string_view text, char separator = ',')
{
    std::vector<std::string> out;
    std::string current;
    char quote = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char ch = text[i];
        if (quote)
        {
            current.push_back(ch);
            if (ch == '\\' && i + 1 < text.size())
                current.push_back(text[++i]);
            else if (ch == quote)
                quote = 0;
        }
        else if (ch == '\'' || ch == '"')
        {
            quote = ch;
            current.push_back(ch);
        }
        else if (ch == separator)
        {
            out.push_back(current);
            current.clear();
        }
        else
        {
            current.push_back(ch);
        }
    }
    if (!current.empty() || !out.empty())
        out.push_back(current);
    return out;
}

int ParseInt(std::string_view text)
{
    if (!text.empty() && text[0] == '#')
        return std::stoi(std::string(text.substr(1)), nullptr, 16);
    return std::stoi(std::string(text));
}

bool IsNumber(std::string_view text)
{
    return !text.empty() && (std::isdigit(static_cast<unsigned char>(text[0])) || text[0] == '-' || text[0] == '#');
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Reals
// ---------------------------------------------------------------------------------------------------------------------

TEST(RasmData_Test, RealsMatchRasm)
{
    int checked = 0;
    for (const json::Value* c : CasesOf("reals"))
    {
        const std::string format = Str(*c, "format");
        for (const json::Value& pair : c->Get("values")->array)
        {
            const double value = FromBits(pair.array[0].string);
            std::string got;
            if (format == "float24")
                got = ToHex(MakeFloat24(value));
            else
            {
                const Real40 real = format == "amsdos" ? MakeAmsdosReal(value) : MakeMicrosoftReal(value);
                EXPECT_EQ(real.status, RealStatus::Ok) << pair.array[0].string;
                got = ToHex(real.bytes);
            }
            EXPECT_EQ(got, pair.array[1].string) << format << " " << pair.array[0].string;
            ++checked;
        }
    }
    EXPECT_GT(checked, 1000);
    for (const json::Value* c : CasesOf("real"))
    {
        const double value = FromBits(Str(*c, "bits"));
        const Real40 real = Str(*c, "format") == "amsdos" ? MakeAmsdosReal(value) : MakeMicrosoftReal(value);
        EXPECT_EQ(real.status, RealStatus::Hangs) << Str(*c, "expr");
    }
}

TEST(RasmData_Test, RealsKnownValues)
{
    EXPECT_EQ(ToHex(MakeAmsdosReal(1.0).bytes), "0000000081");
    EXPECT_EQ(ToHex(MakeAmsdosReal(-1.0).bytes), "0000008081");
    EXPECT_EQ(ToHex(MakeAmsdosReal(0.0).bytes), "0000000000");
    EXPECT_EQ(ToHex(MakeMicrosoftReal(0.5).bytes), "8000000000");
    EXPECT_EQ(ToHex(MakeFloat24(3.14159)), "401F92");
    // sign lost below the float range, inf as exponent 0xBF
    EXPECT_EQ(ToHex(MakeFloat24(-std::ldexp(1.0, -140))), "C00400");
    EXPECT_EQ(ToHex(MakeFloat24(std::ldexp(1.0, 200))), "BF0000");
    // the u64 conversion saturates on the reference build
    EXPECT_EQ(ToHex(MakeAmsdosReal(std::ldexp(1.0, 200)).bytes), "FFFFFF7FA0");
}

// ---------------------------------------------------------------------------------------------------------------------
// INCBIN
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
IncbinOptions ParseIncbinOptions(std::string_view text)
{
    IncbinOptions o;
    const std::vector<std::string> args = SplitArgs(text);
    int integers = 0;
    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::string& a = args[i];
        auto next = [&]() { return ParseInt(args.at(++i)); };
        if (IsNumber(a))
        {
            const int v = ParseInt(a);
            if (integers == 0)
                o.offset = v;
            else if (integers == 1)
                o.size = v;
            else
                o.offset += 65536 * v;
            ++integers;
        }
        else if (a == "REVERT")
            o.revert = true;
        else if (a == "SKIPHEADER")
            o.skipHeader = true;
        else if (a == "REPACK")
            o.repack = true;
        else if (a == "MISALIGNED")
            o.misaligned = true;
        else if (a == "REMAP")
            o.remap = next();
        else if (a == "VTILES")
            o.vtiles = next();
        else if (a == "ITILES" || a == "GTILES")
        {
            o.tiles = a == "ITILES" ? IncbinTiles::ITiles : IncbinTiles::GTiles;
            o.tileWidth = next();
        }
        else if (a == "SLICE" || a == "ISLICE")
        {
            o.slice = a == "SLICE" ? IncbinSlice::Slice : IncbinSlice::ISlice;
            o.sliceWidth = next();
            o.sliceHeight = next();
        }
        else
            ADD_FAILURE() << "unknown INCBIN option " << a;
    }
    return o;
}
}  // namespace

TEST(RasmData_Test, IncbinMatchesRasm)
{
    std::map<std::string, Bytes> files;
    int checked = 0;
    for (const json::Value* c : CasesOf("incbin"))
    {
        const std::string name = Str(*c, "file");
        const std::string options = Str(*c, "options");
        const std::string label = name + " [" + options + "] org " + std::to_string(Int(*c, "org"));
        if (!files.count(name))
            files[name] = unrealasm::testing::ReadTestData("rasm/data/inputs/" + name);
        IncbinResult r = IncbinTransform(files[name], ParseIncbinOptions(options));
        // rasm reads outside its file buffer there (its bytes are whatever lies there): the port refuses
        if (const json::Value* outside = c->Get("outside"); outside && outside->boolean)
        {
            EXPECT_FALSE(r.Ok()) << label;
            continue;
        }
        // the engine's part: SLICE alignment of the code address
        const int org = static_cast<int>(Int(*c, "org"));
        if (r.Ok() && r.alignTo256 && (org & 255))
        {
            r.bytes.insert(r.bytes.begin(), static_cast<size_t>(256 - (org & 255)), 0);
            r.warnings.push_back("INCBIN ISLICE was automatically aligned, use MISALIGNED to force in place");
        }
        if (r.Ok())
            ExpectOutput(*c, r.bytes, label);
        ExpectMessages(*c, r.error, r.warnings, label);
        ++checked;
    }
    EXPECT_GT(checked, 70);
}

// ---------------------------------------------------------------------------------------------------------------------
// WAV
// ---------------------------------------------------------------------------------------------------------------------

TEST(RasmData_Test, WavMatchesRasm)
{
    int checked = 0;
    for (const json::Value* c : CasesOf("wav"))
    {
        AudioFilterState filter;  // static in rasm: one per assembly
        DataResult all;
        std::string label;
        for (const std::string& step : Strings(*c, "steps"))
        {
            label += step + " ";
            const size_t colon = step.find(':');
            const Bytes file = unrealasm::testing::ReadTestData("rasm/data/inputs/" + step.substr(0, colon));
            const std::vector<std::string> args = SplitArgs(step.substr(colon + 1));
            WavOptions o;
            const std::string& mode = args.at(0);
            o.mode = mode == "SMP" ? WavMode::Smp : mode == "SM2" ? WavMode::Sm2 : mode == "SM4" ? WavMode::Sm4 : WavMode::Dma;
            for (size_t i = 1; i < args.size(); ++i)
            {
                if (args[i] == "DMA_INT")
                    o.dmaInt = true;
                else if (args[i] == "DMA_REPEAT")
                    o.dmaRepeat = ParseInt(args.at(++i));
                else if (args[i] == "DMA_CHANNEL_A")
                    o.channel = DmaChannel::A;
                else if (args[i] == "DMA_CHANNEL_B")
                    o.channel = DmaChannel::B;
                else if (args[i] == "DMA_CHANNEL_C")
                    o.channel = DmaChannel::C;
                else
                    o.amplification = std::stod(args[i]);
            }
            const DataResult r = ImportWav(file, o, filter);
            all.bytes.insert(all.bytes.end(), r.bytes.begin(), r.bytes.end());
            all.warnings.insert(all.warnings.end(), r.warnings.begin(), r.warnings.end());
            if (!r.Ok() && all.Ok())
                all.error = r.error;
        }
        if (all.Ok())
            ExpectOutput(*c, all.bytes, label);
        ExpectMessages(*c, all.error, all.warnings, label);
        ++checked;
    }
    EXPECT_GT(checked, 30);
}

// ---------------------------------------------------------------------------------------------------------------------
// SAVE headers, pokers, colours
// ---------------------------------------------------------------------------------------------------------------------

TEST(RasmData_Test, SaveMatchesRasm)
{
    const Bytes image = unrealasm::testing::ReadTestData("rasm/data/inputs/save512.bin");
    Bytes bank(65536, 0);
    std::copy(image.begin(), image.end(), bank.begin() + 0x4000);
    int checked = 0;
    for (const json::Value* c : CasesOf("save"))
    {
        const std::string name = Str(*c, "name");
        const std::string format = Str(*c, "format");
        const json::Value* run = c->Get("run");
        const SaveFormat f = format == "AMSDOS"   ? SaveFormat::Amsdos
                             : format == "HOBETA" ? SaveFormat::Hobeta
                                                  : SaveFormat::Plain;
        const DataResult r = MakeSaveFile(name, bank, ParseInt(Str(*c, "offset")), ParseInt(Str(*c, "size")),
                                          run && run->type == json::Value::Type::String ? ParseInt(run->string) : -1, f);
        if (r.Ok())
            ExpectOutput(*c, r.bytes, name);
        ExpectMessages(*c, r.error, r.warnings, name);
        ++checked;
    }
    EXPECT_GT(checked, 10);
}

TEST(RasmData_Test, HeadersKnownLayout)
{
    int user = 7;
    const std::array<char, 11> name = MakeAmsdosName("3:dir/prog.bin", user);
    EXPECT_EQ(user, 3);
    EXPECT_EQ(std::string(name.data(), 11), "PROG    BIN");
    const auto h = MakeAmsdosHeader(0x4123, 0x4000, 0x4100, name, 0);
    EXPECT_EQ(h[18], 2);
    EXPECT_EQ(h[19] | (h[20] << 8), 0x100);
    EXPECT_EQ(h[26] | (h[27] << 8), 0x4123);
    EXPECT_TRUE(HasAmsdosHeader([&] {
        Bytes file(h.begin(), h.end());
        file.resize(128 + 0x100, 0);
        return file;
    }()));
    const auto hobeta = MakeHobetaHeader(0x8000, 0x8100, "NAME    ");
    EXPECT_EQ(hobeta[8], 'C');
    EXPECT_EQ(hobeta[9] | (hobeta[10] << 8), 0x100);  // rasm's start field holds the length
    EXPECT_EQ(hobeta[0xD], 1);
}

TEST(RasmData_Test, PokersMatchRasm)
{
    int checked = 0;
    for (const json::Value* c : CasesOf("poker"))
    {
        const std::string poker = Str(*c, "poker");
        Bytes memory = FromHex(Str(*c, "data"));
        const size_t space = poker.find(' ');
        const std::string verb = poker.substr(0, space);
        const std::vector<std::string> args = SplitArgs(poker.substr(space + 1));
        const int start = ParseInt(args.at(0));
        const int end = ParseInt(args.at(1));
        std::string error;
        if (verb == "summem")
            memory.push_back(SumMem(memory, start, end));
        else if (verb == "xormem")
            memory.push_back(XorMem(memory, start, end));
        else if (verb == "sum16")
        {
            const std::uint16_t sum = Sum16(memory, start, end);
            memory.push_back(static_cast<std::uint8_t>(sum & 0xFF));
            memory.push_back(static_cast<std::uint8_t>(sum >> 8));
        }
        else
        {
            const int method = args.size() > 2 ? ParseInt(args[2]) : 1;
            std::string key = args.size() > 3 ? args[3].substr(1, args[3].size() - 2) : std::string();
            error = CipherMem(memory, start, end, method, key);
        }
        if (error.empty())
        {
            EXPECT_EQ(ToHex(memory), Str(*c, "bytes")) << poker;
        }
        const std::vector<std::string> errors = Strings(*c, "errors");
        EXPECT_EQ(error, errors.empty() ? std::string() : errors.front()) << poker;
        ++checked;
    }
    EXPECT_GT(checked, 15);
}

TEST(RasmData_Test, ColoursMatchRasm)
{
    int checked = 0;
    for (const json::Value* c : CasesOf("colour"))
    {
        const std::string function = Str(*c, "function");
        const double arg = FromBits(Str(*c, "arg"));
        const std::string label = function + "(" + Str(*c, "expr") + ")";
        if (c->Get("errors"))
        {
            EXPECT_FALSE(Soft2HardInk(arg).has_value()) << label;
            EXPECT_EQ(Strings(*c, "errors").at(0), kSoft2HardError) << label;
            continue;
        }
        int value = -1;
        if (function == "s2h_ink")
            value = Soft2HardInk(arg).value_or(-1);
        else if (function == "h2s_ink")
            value = Hard2SoftInk(arg);
        else if (function == "getr")
            value = GetR(arg);
        else if (function == "getv")
            value = GetV(arg);
        else if (function == "getb")
            value = GetB(arg);
        else if (function == "setr")
            value = SetR(arg);
        else if (function == "setv")
            value = SetV(arg);
        else if (function == "setb")
            value = SetB(arg);
        EXPECT_EQ(value & 0xFFFF, Int(*c, "value")) << label;
        ++checked;
    }
    EXPECT_GT(checked, 150);
}

// ---------------------------------------------------------------------------------------------------------------------
// BUILDZX
// ---------------------------------------------------------------------------------------------------------------------

TEST(RasmData_Test, ZxSnapshotMatchesRasm)
{
    int checked = 0;
    for (const json::Value* c : CasesOf("zx"))
    {
        std::map<int, Bytes> banks;
        for (int b = 0; b < 8; ++b)
            banks[b].assign(65536, 0);
        const bool bankset0 = c->Get("bankset0")->boolean;
        for (const json::Value& block : c->Get("blocks")->array)
        {
            const int bank = bankset0 ? 0 : static_cast<int>(block.array[0].integer);
            const Bytes data = FromHex(block.array[2].string);
            std::copy(data.begin(), data.end(), banks[bank].begin() + block.array[1].integer);
        }
        ZxSnapshotInput input;
        input.bank0 = banks[0];
        input.bank2 = banks[2];
        input.bank5 = banks[5];
        input.bankset0 = bankset0;
        input.run = static_cast<unsigned>(Int(*c, "run"));
        input.stack = static_cast<unsigned>(Int(*c, "stack"));
        const Bytes sna = BuildZxSnapshot(input);
        Bytes expected(static_cast<size_t>(Int(*c, "size")), 0);
        for (const json::Value& run : c->Get("sna")->array)
        {
            const Bytes data = FromHex(run.array[1].string);
            std::copy(data.begin(), data.end(), expected.begin() + run.array[0].integer);
        }
        EXPECT_EQ(sna, expected) << "case " << checked;
        ++checked;
    }
    EXPECT_GE(checked, 4);
}

// ---------------------------------------------------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
bool IsQuoted(std::string_view w)
{
    return w.size() >= 2 && (w[0] == '\'' || w[0] == '"') && w.back() == w[0];
}

// The fixture's expressions: terms joined by + or *, a term an integer or a one-character literal
int EvalExpression(std::string_view text, const std::array<std::uint8_t, 256>& charset)
{
    int value = 0;
    char op = '+';
    size_t i = 0;
    while (i < text.size())
    {
        int term = 0;
        if (text[i] == '\'')
        {
            if (text[i + 1] == '\\')
            {
                term = EscapedCharValue(text[i + 2]);
                i += 4;
            }
            else
            {
                term = charset[static_cast<std::uint8_t>(text[i + 1])];
                i += 3;
            }
        }
        else
        {
            size_t end = i;
            while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])))
                ++end;
            term = std::stoi(std::string(text.substr(i, end - i)));
            i = end;
        }
        value = op == '+' ? value + term : value * term;
        if (i < text.size())
            op = text[i++];
    }
    return value;
}

struct StringMachine
{
    std::array<std::uint8_t, 256> charset = DefaultCharset();
    std::vector<Utf8Remap> remap;
    bool utf8 = false;
    bool fq = false;
    bool as80 = false;
    std::string error;
    std::vector<std::string> warnings;
    Bytes out;
    std::string structDefault;  // the field initialiser of `fld defb '...'`
    int structSize = 0;         // `fld defs n`

    void Fail(const std::string& message)
    {
        if (error.empty())
            error = message;
    }

    // the preprocessor's treatment of quoted words
    std::string Prepare(const std::string& word)
    {
        if (!IsQuoted(word))
            return word;
        if (utf8)
        {
            std::vector<std::string> errors;
            const std::string converted = ApplyUtf8Option(word, &errors);
            for (const std::string& e : errors)
                Fail(e);
            return converted;
        }
        if (!fq)
            for (const std::string& e : CheckQuotedChars(word))
                Fail(e);
        return word;
    }

    void Setup(const std::string& line)
    {
        const size_t space = line.find(' ');
        const std::string verb = line.substr(0, space);
        std::vector<std::string> args = space == std::string::npos ? std::vector<std::string>{}
                                                                    : SplitArgs(line.substr(space + 1));
        for (std::string& a : args)
            a = Prepare(a);
        if (verb == "charset")
        {
            std::string e;
            if (args.empty())
                charset = DefaultCharset();
            else if (args.size() == 2 && IsQuoted(args[0]) && IsQuoted(args[1]))
                e = CharsetMapStrings(charset, args[0], args[1]);
            else if (args.size() == 2 && IsQuoted(args[0]))
                e = CharsetMapString(charset, args[0], EvalExpression(args[1], charset));
            else if (args.size() == 2)
                e = CharsetMapByte(charset, EvalExpression(args[0], charset), EvalExpression(args[1], charset));
            else
                e = CharsetMapRange(charset, EvalExpression(args[0], charset), EvalExpression(args[1], charset),
                                    EvalExpression(args[2], charset));
            if (!e.empty())
                Fail(e);
        }
        else if (verb == "utf8remap")
        {
            if (args.empty())
                remap.clear();
            else if (const auto entry = ParseUtf8Remap(args[0], EvalExpression(args[1], charset)))
                remap.push_back(*entry);
        }
        else if (verb == "fld" && args.size() == 1)
        {
            // "fld defb '...'" or "fld defs n" (split on the first space only)
            const std::string rest = line.substr(space + 1);
            if (rest.rfind("defb ", 0) == 0)
                structDefault = Prepare(rest.substr(5));
            else if (rest.rfind("defs ", 0) == 0)
                structSize = std::stoi(rest.substr(5));
        }
    }

    void Append(const StringResult& r)
    {
        if (!r.Ok())
            Fail(r.error);
        out.insert(out.end(), r.bytes.begin(), r.bytes.end());
        warnings.insert(warnings.end(), r.warnings.begin(), r.warnings.end());
    }

    void Statement(const std::string& statement)
    {
        const size_t space = statement.find(' ');
        const std::string verb = statement.substr(0, space);
        std::vector<std::string> args = SplitArgs(statement.substr(space + 1));
        for (std::string& a : args)
            a = Prepare(a);
        if (verb == "struct")
        {
            if (args.size() == 1)
            {
                // "struct st inst": the field's own default
                StringOptions o;
                o.directive = StringDirective::DefbStruct;
                Append(DecodeQuotedString(structDefault, charset, remap, o));
                return;
            }
            StringOptions o;
            o.directive = StringDirective::StructField;
            o.maxBytes = structSize;
            const StringResult r = DecodeQuotedString(args.back(), charset, remap, o);
            Append(r);
            if (r.overrun)
                warnings.push_back("struct field number 1 overrun! (" + std::to_string(structSize) + " byte" +
                                   (structSize > 1 ? "s" : "") + " max)!");
            out.insert(out.end(), static_cast<size_t>(std::max(0, structSize - r.count)), 0);
            return;
        }
        for (size_t i = 0; i < args.size(); ++i)
        {
            const std::string& a = args[i];
            if (IsQuoted(a))
            {
                StringOptions o;
                o.directive = verb == "str" ? StringDirective::Str
                              : as80        ? StringDirective::DefbAs80
                                            : StringDirective::Defb;
                o.markEnd = i + 1 == args.size() || IsQuoted(args[i + 1]);
                Append(DecodeQuotedString(a, charset, remap, o));
            }
            else
            {
                out.push_back(static_cast<std::uint8_t>(EvalExpression(a, charset) & 0xFF));
            }
        }
    }
};
}  // namespace

TEST(RasmData_Test, StringsMatchRasm)
{
    int checked = 0;
    for (const json::Value* c : CasesOf("string"))
    {
        StringMachine m;
        for (const std::string& option : Strings(*c, "options"))
        {
            m.utf8 |= option == "-utf8";
            m.fq |= option == "-fq";
            m.as80 |= option == "-ass";
        }
        const std::string line = Str(*c, "line");
        for (const std::string& setup : Strings(*c, "setup"))
            m.Setup(setup);
        for (const std::string& statement : SplitArgs(line, ':'))
            m.Statement(statement);
        if (m.error.empty())
        {
            EXPECT_EQ(ToHex(m.out), Str(*c, "bytes")) << line;
        }
        ExpectMessages(*c, m.error, m.warnings, line);
        ++checked;
    }
    EXPECT_GT(checked, 35);
}

TEST(RasmData_Test, CharsetAndRemapApi)
{
    std::array<std::uint8_t, 256> charset = DefaultCharset();
    EXPECT_EQ(CharsetMapRange(charset, 'a', 'c', 1), "");
    EXPECT_EQ(charset['b'], 2);
    EXPECT_EQ(CharsetMapByte(charset, 256, 0), "CHARSET byte value must be 0-255");
    const auto remap = ParseUtf8Remap("'\xC3\xA9'", 300);
    ASSERT_TRUE(remap.has_value());
    EXPECT_EQ(remap->code, (Bytes{0xC3, 0xA9}));
    EXPECT_EQ(remap->value, 300 & 0xFF);
    EXPECT_FALSE(ParseUtf8Remap("x", 1).has_value());
    EXPECT_EQ(EscapedCharValue('n'), '\n');
    EXPECT_EQ(EscapedCharValue('q'), 'q');
}
