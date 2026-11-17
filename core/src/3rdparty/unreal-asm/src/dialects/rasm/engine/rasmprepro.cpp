// rasm's preprocessor, ported from rasm.c v3.3.3 (EarlyPrepSrc, PreProcessing). The structure follows the C code closely on
// purpose (same indexes, same fall-throughs), so a difference against rasm can be traced back to its source line.

#include "rasmprepro.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string_view>

namespace unrealasm::rasm
{
namespace
{
constexpr std::string_view keywords[] = {
    "API_SEND", "LD", "DEC", "INC", "ADD", "SUB", "OR", "AND", "XOR", "POP", "PUSH", "DJNZ", "JR", "JP", "CALL", "RET", "EX",
    "ADC", "SBC", "EXA", "EXX", "CP", "BIT", "RES", "SET", "IN", "OUT", "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SL1",
    "SRL", "SRL8", "RST", "HALT", "DI", "EI", "NOP", "DF24", "DEFF", "DEFR", "DEFB", "DEFM", "DF", "DR", "DM", "DB", "DEFW",
    "DW", "DEFS", "DS", "STR", "LDI", "LDIR", "OUTI", "INI", "RLCA", "RRCA", "NEG", "RLA", "RRA", "RLD", "RRD", "DAA", "CPL",
    "SCF", "LDD", "LDDR", "CCF", "OUTD", "IND", "RETI", "RETN", "IM", "DEFI", "CPD", "CPI", "CPDR", "CPIR", "OTDR", "OTIR",
    "INDR", "INIR", "REPEAT", "STARTINGINDEX", "REND", "UNTIL", "ORG", "PROTECT", "WHILE", "WEND", "INCLUDE", "HEXBIN",
    "ALIGN", "CONFINE", "ELSEIFNOT", "ELSEIF", "ELSE", "IF", "ENDIF", "IFNOT", "IFDEF", "IFNDEF", "IFUSED", "IFNUSED",
    "UNDEF", "CASE", "BREAK", "DEFAULT", "SWITCH", "ENDSWITCH", "CODE", "NOCODE", "MACRO", "ENUM", "TICKER", "LET",
    "ASSERT", "CHARSET", "UTF8REMAP", "RUN", "SAVE", "REDEFINE_BRK", "BRK", "NOLIST", "LIST", "STOP", "PRINT", "COMZ",
    "DELAYED_PRINT", "FAIL", "BREAKPOINT", "ROMBANK", "BANK", "BANKSET", "NAMEBANK", "NAMEROM", "LIMIT", "LZEXO", "LZX7",
    "LZX0", "LZX0B", "LZAPU", "LZSA1", "LZSA2", "LZ4", "LZ48", "LZ49", "LZCLOSE", "SNASET", "SNAPINIT", "CPRINIT", "BUILDZX",
    "BUILDOBJ", "BUILDCPR", "BUILDSNA", "BUILDROM", "BUILDTAPE", "SETCPC", "SETCRTC", "AMSDOS", "OTD", "OTI", "SHL", "SHR",
    "STRUCT", "ENDSTRUCT", "ENDS", "LABEL", "NOEXPORT", "ENOEXPORT", "MODULE", "ENDMODULE", "TIMESTAMP", "SUMMEM", "SUM16",
    "XORMEM", "CIPHERMEM", "EXTERNAL", "PROCEDURE", "REBANK", "LOCALISATION", "RELOCATE", "ENDRELOCATE", "RELOCATE_START",
    "RELOCATE_END", "RELOCATE_TABLE", "EDSK", "HFE", "ERRRD",
};

constexpr const char* charWord = "@ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.=_($)][+-*/^%#|&'\"\\m}{[]";
constexpr const char* expressionValidCharExtended = "0123456789.ABCDEFGHIJKLMNOPQRSTUVWXYZ_{}@+-*/~^$#%<=>|&\\";

/// A source line (s_listing)
struct Listing
{
    std::string text;
    int file = 0;
    int line = 0;
};

/// A char of a C string: 0 past the end (the C code reads its terminator there)
char At(const std::string& s, long i)
{
    return i >= 0 && static_cast<size_t>(i) < s.size() ? s[static_cast<size_t>(i)] : '\0';
}

void Set(std::string& s, long i, char c)
{
    if (i < 0)
        return;
    if (c == '\0')
    {
        if (static_cast<size_t>(i) < s.size())
            s.resize(static_cast<size_t>(i));
        return;
    }
    if (static_cast<size_t>(i) >= s.size())
        s.resize(static_cast<size_t>(i) + 1, ' ');
    s[static_cast<size_t>(i)] = c;
}

bool StartsAt(const std::string& s, long i, std::string_view what)
{
    for (size_t k = 0; k < what.size(); ++k)
        if (At(s, i + static_cast<long>(k)) != what[k])
            return false;
    return true;
}

bool IsAlnum(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z');
}

/// rasm's FileReadLines: lines keep their LF, a CR becomes `replace`; a NUL byte ends the C string of its line
std::vector<std::string> SplitLines(const std::vector<uint8_t>& bytes, char replace)
{
    std::vector<std::string> lines;
    size_t i = 0;
    const size_t n = bytes.size();
    while (i < n)
    {
        size_t e = i;
        std::string line;
        while (e < n && bytes[e] != 0x0A)
        {
            line.push_back(bytes[e] == 0x0D ? replace : static_cast<char>(bytes[e]));
            ++e;
        }
        if (e < n)
        {
            line.push_back('\n');
            ++e;
        }
        const auto nul = line.find('\0');
        if (nul != std::string::npos)
            line.resize(nul);
        lines.push_back(std::move(line));
        i = e;
    }
    return lines;
}

class Preprocessor
{
public:
    Preprocessor(const FileSystem& fs, const PreproOptions& options) : fs(fs), options(options)
    {
        for (const char* p = expressionValidCharExtended; *p; ++p)
            validExtended[static_cast<unsigned char>(*p)] = true;
        result.freeQuote = options.freeQuote;
    }

    PreproResult Run(const std::string& mainName, const std::vector<uint8_t>& mainBytes);

private:
    void Error(int file, int line, std::string message)
    {
        Diagnostic d;
        d.severity = Severity::Error;
        d.line = static_cast<uint32_t>(line);
        d.message = FileName(file) + ":" + std::to_string(line) + ": " + std::move(message);
        result.diagnostics.push_back(std::move(d));
    }
    void Warning(int file, int line, std::string message)
    {
        if (options.noWarning)
            return;
        Diagnostic d;
        d.severity = Severity::Warning;
        d.line = static_cast<uint32_t>(line);
        d.message = FileName(file) + ":" + std::to_string(line) + ": " + std::move(message);
        result.diagnostics.push_back(std::move(d));
    }
    std::string FileName(int file) const
    {
        return file >= 0 && static_cast<size_t>(file) < result.files.size() ? result.files[static_cast<size_t>(file)] : "?";
    }
    std::optional<std::vector<uint8_t>> Read(const std::string& path) const
    {
        return fs.read ? fs.read(path) : std::nullopt;
    }
    bool Exists(const std::string& path) const { return Read(path).has_value(); }

    void EarlyPrep(std::vector<std::string>& listing, int file);
    void Pass1();
    void Words();
    bool IsMacro(const std::string& name) const { return macros.count(name) != 0; }
    void PutW(char c)
    {
        if (w.size() < static_cast<size_t>(lw) + 2)
            w.resize(static_cast<size_t>(lw) * 2 + 256, '\0');
        w[static_cast<size_t>(lw++)] = c;
        w[static_cast<size_t>(lw)] = '\0';
    }
    void PutWRaw(char c)
    {
        if (w.size() < static_cast<size_t>(lw) + 2)
            w.resize(static_cast<size_t>(lw) * 2 + 256, '\0');
        w[static_cast<size_t>(lw++)] = c;
    }
    std::string WString() const { return std::string(w.c_str()); }
    void PushWord(const std::string& text, uint8_t t, int line, int file)
    {
        Word word;
        word.w = text;
        word.line = line;
        word.file = file;
        word.t = t;
        word.e = currentE;
        result.words.push_back(std::move(word));
        currentE = 0;
    }

    const FileSystem& fs;
    const PreproOptions& options;
    PreproResult result;
    std::vector<Listing> listing;
    std::set<std::string> macros;
    bool validExtended[256] = {};

    std::string w = std::string(256, '\0');
    int lw = 0;
    int currentE = 0;
};

void Preprocessor::EarlyPrep(std::vector<std::string>& lines, int file)
{
    size_t l = 0;
    long idx = 0;
    bool qopen = false;
    while (l < lines.size())
    {
        std::string& s = lines[l];
        const char c = At(s, idx++);
        switch (c)
        {
            case 0:
                if (idx > 1)
                {
                    idx -= 2;
                    while (idx && (At(s, idx) == ' ' || (At(s, idx) <= 0x0D && At(s, idx))))
                        idx--;
                    if (At(s, idx) == '\\')
                    {
                        if (l + 1 < lines.size())
                        {
                            std::string& next = lines[l + 1];
                            bool superFusion = false;
                            long nextidx = 0;
                            char zeQuote = 0;
                            while (At(next, nextidx) == ' ' || At(next, nextidx) == 0x09)
                                nextidx++;
                            long previdx = idx ? idx - 1 : 0;
                            while (previdx && (At(s, previdx) == ' ' || At(s, previdx) == 0x09))
                                previdx--;
                            if (At(s, previdx) == '\'' || At(s, previdx) == '"')
                            {
                                if (At(s, previdx) == At(next, nextidx))
                                {
                                    zeQuote = At(s, previdx);
                                    idx = previdx;
                                    nextidx++;
                                    superFusion = true;
                                }
                            }
                            const std::string tail = static_cast<size_t>(nextidx) < next.size() ? next.substr(static_cast<size_t>(nextidx)) : "";
                            s = s.substr(0, static_cast<size_t>(idx)) + tail;
                            next.clear();
                            if (superFusion)
                            {
                                qopen = true;
                                while (At(s, idx))
                                {
                                    if (At(s, idx) == '\\')
                                    {
                                        if (At(s, idx + 1))
                                            idx++;
                                        else
                                        {
                                            Error(file, static_cast<int>(l) + 1, "Cannot escape at the end of the line inside a quote");
                                            break;
                                        }
                                    }
                                    else if (At(s, idx) == zeQuote)
                                    {
                                        qopen = false;
                                        idx++;
                                        break;
                                    }
                                    idx++;
                                }
                                if (qopen)
                                    Error(file, static_cast<int>(l) + 1, "Quote opened but not closed before the end of the line");
                            }
                        }
                        else
                        {
                            Error(file, static_cast<int>(l) + 1, "Cannot merge with next line as there is no more line!");
                            idx = 0;
                            l++;
                        }
                    }
                    else
                    {
                        idx = 0;
                        l++;
                    }
                }
                else
                {
                    idx = 0;
                    l++;
                }
                break;
            case 'f':
                Set(s, idx - 1, 'F');
                [[fallthrough]];
            case 'F':
                if (At(s, idx) == '\'' && idx > 3 && At(s, idx - 2) == 'A')
                {
                    long reverseIdx = idx - 3;
                    while (reverseIdx && At(s, reverseIdx) == ' ')
                        reverseIdx--;
                    if (At(s, reverseIdx) == ',')
                        Set(s, idx, ' ');
                }
                break;
            case '\t':
                Set(s, idx - 1, ' ');
                break;
            case '\\':
                if (At(s, idx))
                    idx++;
                break;
            case '\'':
            case '"':
                qopen = true;
                while (At(s, idx))
                {
                    if (At(s, idx) == '\\')
                    {
                        if (At(s, idx + 1))
                            idx++;
                        else
                        {
                            Error(file, static_cast<int>(l) + 1, "Cannot escape at the end of the line inside a quote");
                            break;
                        }
                    }
                    else if (At(s, idx) == c)
                    {
                        qopen = false;
                        idx++;
                        break;
                    }
                    idx++;
                }
                if (qopen)
                    Error(file, static_cast<int>(l) + 1, "Quote opened but not closed before the end of the line");
                break;
            case '/':
                if (At(s, idx) == '*')
                {
                    size_t mlcStart = l;
                    long mlcIdx = idx - 1;
                    idx++;
                    bool closed = false;
                    while (!closed)
                    {
                        const char cc = At(lines[l], idx++);
                        if (!cc)
                        {
                            idx = 0;
                            l++;
                            if (l >= lines.size())
                            {
                                Error(file, static_cast<int>(l) + 1, "opened comment to the end of the file");
                                return;
                            }
                        }
                        else if (cc == '*' && At(lines[l], idx) == '/')
                        {
                            idx++;
                            closed = true;
                        }
                    }
                    if (mlcStart == l)
                    {
                        while (mlcIdx < idx)
                            Set(lines[l], mlcIdx++, ' ');
                    }
                    else
                    {
                        Set(lines[mlcStart], mlcIdx, '\0');
                        mlcStart++;
                        while (mlcStart < l)
                            lines[mlcStart++].clear();
                        mlcIdx = 0;
                        while (mlcIdx < idx)
                            Set(lines[l], mlcIdx++, ' ');
                    }
                    break;
                }
                if (At(s, idx) != '/')
                    break;
                [[fallthrough]];
            case ';':
                idx--;
                while (At(s, idx) && At(s, idx) != 0x0D && At(s, idx) != 0x0A)
                    Set(s, idx++, ':');
                idx--;
                break;
            case '>':
                if (At(s, idx) == '>')
                {
                    Set(s, idx - 1, ']');
                    Set(s, idx++, ' ');
                }
                break;
            case '<':
                if (At(s, idx) == '<')
                {
                    Set(s, idx - 1, '[');
                    Set(s, idx++, ' ');
                }
                break;
            default:
                if (c >= 'a' && c <= 'z')
                    Set(s, idx - 1, static_cast<char>(c - 'a' + 'A'));
                break;
        }
    }
}

void Preprocessor::Pass1()
{
    size_t l = 0;
    long idx = 0;
    int ival = 0;
    int crunch = 0;
    while (l < listing.size())
    {
        const char c = At(listing[l].text, idx);
        bool toF = false;
        switch (c)
        {
            case 0:
                l++;
                idx = -1;
                ival = 0;
                break;
            case '\\':
                if (At(listing[l].text, idx + 1))
                    idx++;
                break;
            case 0x0D:
            case 0x0A:
                Set(listing[l].text, idx, ':');
                ival = 0;
                break;
            case '\'':
            case '"':
                idx++;
                while (true)
                {
                    if (!At(listing[l].text, idx))
                    {
                        idx = -1;
                        l++;
                        if (l >= listing.size())
                            break;
                    }
                    else if (At(listing[l].text, idx) == '\\' && At(listing[l].text, idx + 1))
                        idx++;
                    else if (At(listing[l].text, idx) == c)
                        break;
                    idx++;
                }
                break;
            case '&':
                if (options.noAmpersand)
                    Set(listing[l].text, idx, '#');
                break;
            case 'I':
            {
                const std::string& s = listing[l].text;
                if (!ival && At(s, idx + 1) == 'N' && At(s, idx + 2) == 'C' && At(s, idx + 3) != ' ')
                {
                    int include = 0;
                    long incCheck = 0;
                    switch (At(s, idx + 3))
                    {
                        case 'A':
                            if (StartsAt(s, idx + 4, "PU")) { include = 17; incCheck = 6; }
                            break;
                        case 'B':
                            if (StartsAt(s, idx + 4, "IN")) { include = 10; incCheck = 6; }
                            break;
                        case 'E':
                            if (StartsAt(s, idx + 4, "XO")) { include = 8; incCheck = 6; }
                            break;
                        case 'L':
                            if (StartsAt(s, idx + 4, "ZSA1")) { include = 18; incCheck = 8; }
                            else if (StartsAt(s, idx + 4, "ZSA2")) { include = 19; incCheck = 8; }
                            else if (StartsAt(s, idx + 4, "UDE")) { include = 1; incCheck = 7; }
                            else if (StartsAt(s, idx + 4, "48")) { include = 48; incCheck = 6; }
                            else if (StartsAt(s, idx + 4, "49")) { include = 49; incCheck = 6; }
                            else if (StartsAt(s, idx + 4, "Z4")) { include = 4; incCheck = 6; }
                            break;
                        case 'W':
                            if (StartsAt(s, idx + 4, "AV")) { include = 10; incCheck = 6; }
                            break;
                        case 'Z':
                            if (StartsAt(s, idx + 4, "X0B")) { include = 71; incCheck = 7; }
                            else if (StartsAt(s, idx + 4, "X0")) { include = 70; incCheck = 6; }
                            else if (StartsAt(s, idx + 4, "X7")) { include = 7; incCheck = 6; }
                            break;
                        default:
                            break;
                    }
                    if (include)
                    {
                        const char after = At(s, idx + incCheck);
                        if (after == ' ' || after == '\'' || after == '"')
                        {
                            while (At(s, idx + incCheck) == ' ')
                                incCheck++;
                            if (At(s, idx + incCheck) == '\'' || At(s, idx + incCheck) == '"')
                            {
                                const char quoteType = At(s, idx + incCheck);
                                incCheck++;
                                std::string qval;
                                while (At(s, idx + incCheck) && At(s, idx + incCheck) != quoteType)
                                {
                                    qval.push_back(At(s, idx + incCheck));
                                    incCheck++;
                                }
                                if (At(s, idx + incCheck) == quoteType)
                                {
                                    long rewrite = idx;
                                    idx += incCheck + 1;
                                    const int parent = listing[l].file;
                                    std::string toRead = MergePath(FileName(parent), qval);
                                    bool fileOk = Exists(toRead);
                                    for (size_t p = 0; p < fs.includePaths.size() && !fileOk; ++p)
                                    {
                                        toRead = MergePath(fs.includePaths[p], qval);
                                        fileOk = Exists(toRead);
                                    }
                                    if (include == 1)
                                    {
                                        if (fileOk)
                                        {
                                            std::vector<std::string> included = SplitLines(*Read(toRead), ':');
                                            result.files.push_back(toRead);
                                            const int ifile = static_cast<int>(result.files.size()) - 1;
                                            EarlyPrep(included, ifile);
                                            // split the current line: before the INCLUDE, the included lines, after it
                                            Listing rest;
                                            rest.file = listing[l].file;
                                            rest.line = listing[l].line;
                                            rest.text = At(listing[l].text, idx) ? listing[l].text.substr(static_cast<size_t>(idx)) : ":";
                                            listing[l].text = listing[l].text.substr(0, static_cast<size_t>(rewrite)) + ":";
                                            std::vector<Listing> inserted;
                                            for (size_t li = 0; li < included.size(); ++li)
                                                inserted.push_back(Listing{std::move(included[li]), ifile, static_cast<int>(li) + 1});
                                            inserted.push_back(std::move(rest));
                                            listing.insert(listing.begin() + static_cast<long>(l) + 1, inserted.begin(), inserted.end());
                                            idx = -1;
                                        }
                                        else
                                        {
                                            std::string& t = listing[l].text;
                                            for (long i = rewrite; i < idx; ++i)
                                                Set(t, i, ' ');
                                            const std::string tag = "ERRRD " + std::to_string(result.hexbins.size());
                                            for (size_t i = 0; i < tag.size(); ++i)
                                                Set(t, rewrite + static_cast<long>(i), tag[i]);
                                            HexBin hb;
                                            hb.fileName = MergePath(FileName(parent), qval);
                                            hb.crunch = crunch;
                                            hb.missingInclude = true;
                                            result.hexbins.push_back(std::move(hb));
                                            idx--;
                                        }
                                    }
                                    else
                                    {
                                        HexBin hb;
                                        hb.fileName = toRead;
                                        crunch = include == 10 ? 0 : include;
                                        hb.crunch = crunch;
                                        if (crunch == 18)
                                        {
                                            hb.version = 1;
                                            hb.minMatch = 5;
                                        }
                                        else if (crunch == 19)
                                        {
                                            hb.crunch = 18;
                                            hb.version = 2;
                                            hb.minMatch = 2;
                                        }
                                        result.hexbins.push_back(std::move(hb));
                                        char tag[32];
                                        std::snprintf(tag, sizeof tag, "HEXBIN #%X", static_cast<unsigned>(result.hexbins.size() - 1));
                                        std::string& t = listing[l].text;
                                        const std::string tail = static_cast<size_t>(idx) < t.size() ? t.substr(static_cast<size_t>(idx)) : "";
                                        t = t.substr(0, static_cast<size_t>(rewrite)) + tag + tail;
                                        rewrite += static_cast<long>(std::string_view(tag).size());
                                        idx = rewrite - 1;
                                    }
                                }
                                else
                                {
                                    idx += incCheck - 1;
                                    Error(listing[l].file, listing[l].line, "INCLUDE/INC* filename cannot be on multiple lines");
                                }
                            }
                            else
                                Error(listing[l].file, listing[l].line, "A quoted string must follow any INCLUDE/INC* directive");
                        }
                        else
                        {
                            switch (after)
                            {
                                case 0:
                                case ':':
                                case 0x0A:
                                case 0x0D:
                                    Error(listing[l].file, listing[l].line, "A quoted string must follow any INCLUDE/INC* directive");
                                    break;
                                default:
                                    break;
                            }
                        }
                    }
                }
                toF = true;
                break;
            }
            case 'F':
                toF = true;
                break;
            case 'M':
            {
                std::string& s = listing[l].text;
                if (!ival && StartsAt(s, idx + 1, "ACRO") && !validExtended[static_cast<unsigned char>(At(s, idx + 5))])
                {
                    long p = idx + 5;
                    long comma = -1;
                    while (At(s, p) == ' ' || At(s, p) == '\t')
                        p++;
                    if (At(s, p) && (IsAlnum(At(s, p)) || At(s, p) == '_'))
                    {
                        std::string name(1, At(s, p));
                        p++;
                        while (IsAlnum(At(s, p)) || At(s, p) == '_')
                            name.push_back(At(s, p++));
                        macros.insert(name);
                        result.macroNames.push_back(name);
                        if (At(s, p) && (At(s, p) == ' ' || At(s, p) == '\t' || At(s, p) == ','))
                        {
                            if (At(s, p) == ',')
                                comma = p;
                            p++;
                            while (At(s, p) == ' ' || At(s, p) == '\t' || At(s, p) == ',')
                            {
                                if (At(s, p) == ',')
                                    comma = p;
                                p++;
                            }
                            if (validExtended[static_cast<unsigned char>(At(s, p))] && comma < 0)
                                Set(s, p - 1, ',');
                        }
                    }
                }
                ival = 1;
                break;
            }
            case 'S':
            {
                std::string& s = listing[l].text;
                if (!ival && StartsAt(s, idx + 1, "TRUCT") && !validExtended[static_cast<unsigned char>(At(s, idx + 6))])
                {
                    long p = idx + 6;
                    long comma = -1;
                    while (At(s, p) == ' ' || At(s, p) == '\t')
                        p++;
                    if (At(s, p) && (IsAlnum(At(s, p)) || At(s, p) == '_'))
                    {
                        p++;
                        while (IsAlnum(At(s, p)) || At(s, p) == '_')
                            p++;
                        if (At(s, p) && (At(s, p) == ' ' || At(s, p) == '\t' || At(s, p) == ','))
                        {
                            if (At(s, p) == ',')
                                comma = p;
                            p++;
                            while (At(s, p) == ' ' || At(s, p) == '\t' || At(s, p) == ',')
                            {
                                if (At(s, p) == ',')
                                    comma = p;
                                p++;
                            }
                            if (validExtended[static_cast<unsigned char>(At(s, p))] && comma < 0)
                                Set(s, p - 1, ',');
                        }
                    }
                }
                ival++;
                break;
            }
            default:
                if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '@' || c == '_' || c == '{' || c == '}')
                    ival++;
                else
                    ival = 0;
                break;
        }
        if (toF && l < listing.size())
        {
            const std::string& s = listing[l].text;
            if (!ival && StartsAt(s, idx + 1, "REEQUOTES") &&
                (At(s, idx + 10) == ' ' || At(s, idx + 10) == ':' || At(s, idx + 10) < 32))
                result.freeQuote = true;
            ival = 1;
        }
        idx++;
    }
}

void Preprocessor::Words()
{
    char automate[256] = {};
    for (const char* p = charWord; *p; ++p)
        automate[static_cast<unsigned char>(*p)] = 1;
    automate[0] = 1;
    automate[static_cast<unsigned char>(' ')] = 2;
    automate[static_cast<unsigned char>(',')] = 2;
    automate[static_cast<unsigned char>('\t')] = 2;
    automate[static_cast<unsigned char>(':')] = 3;
    automate[static_cast<unsigned char>('=')] = 4;
    automate[static_cast<unsigned char>('<')] = 4;
    automate[static_cast<unsigned char>('>')] = 4;
    automate[static_cast<unsigned char>('!')] = 4;

    auto push0 = [&](std::string text, uint8_t t, int e) {
        Word word;
        word.w = std::move(text);
        word.t = t;
        word.e = e;
        result.words.push_back(std::move(word));
    };
    push0("BEGIN", 1, 0);
    // two variables for IX / IY address arithmetic and the version, as rasm adds them
    push0("SYNCHRO~256", 1, 7);
    push0("RASM_VERSION~3.33", 1, 12);

    bool texpr = false;
    int parenth = 0;
    int ispace = 0;
    char quoteType = 0;
    bool escapeCode = false;
    bool hadComma = false;
    bool noBreak = false;
    char opassign = 0;
    size_t l = 0;
    long idx = 0;
    lw = 0;
    w[0] = '\0';
    currentE = 0;

    auto keyMatched = [&](const std::string& word) { return IsKeyword(word) || (!macros.empty() && IsMacro(word)); };

    while (l < listing.size())
    {
        char c = At(listing[l].text, idx++);
        int state = automate[static_cast<unsigned char>(c)];
        if (state == 0)
        {
            Error(listing[l].file, listing[l].line,
                  "invalid char '" + std::string(1, c > 31 ? c : '?') + "' (" + std::to_string(static_cast<int>(c)) + ")");
            result.fatal = true;
            return;
        }
        if (state == 1)
        {
            switch (c)
            {
                case 0:
                    idx = 0;
                    l++;
                    continue;
                case '\'':
                case '"':
                    if (std::string_view(w.c_str()) == "SAVE")
                    {
                        idx--;
                        noBreak = true;
                    }
                    else
                    {
                        quoteType = c;
                        PutWRaw(c);
                        const int qempty = lw + 1;
                        do
                        {
                            c = At(listing[l].text, idx++);
                            if (!c)
                            {
                                Error(listing[l].file, listing[l].line, "Quoted string must be on a single line!");
                                idx = 0;
                                l++;
                                lw = 0;
                                quoteType = 0;
                            }
                            else
                            {
                                PutWRaw(c);
                                if (!escapeCode)
                                {
                                    if (c == '\\')
                                        escapeCode = true;
                                    else if (c == quoteType)
                                    {
                                        quoteType = 0;
                                        if (lw == qempty)
                                            Warning(listing[std::min(l, listing.size() - 1)].file,
                                                    listing[std::min(l, listing.size() - 1)].line, "Quote is empty");
                                    }
                                }
                                else
                                    escapeCode = false;
                            }
                        } while (quoteType);
                        w[static_cast<size_t>(lw)] = '\0';
                        const size_t li = std::min(l, listing.size() - 1);
                        if (options.utf8)
                        {
                            for (size_t ut = 0; w[ut]; ++ut)
                            {
                                if (static_cast<unsigned char>(w[ut]) > 126)
                                {
                                    const auto next = static_cast<unsigned char>(w[ut + 1]);
                                    bool bad = false;
                                    switch (static_cast<unsigned char>(w[ut]))
                                    {
                                        case 0xC2:
                                            if (next == 0xBF) w[ut] = static_cast<char>(-82);
                                            else if (next == 0xA1) w[ut] = static_cast<char>(-81);
                                            else bad = true;
                                            break;
                                        case 0xC3:
                                            if (next == 0xB9) w[ut] = 124;
                                            else if (next == 0xA9) w[ut] = 123;
                                            else if (next == 0xA8) w[ut] = 125;
                                            else if (next == 0xA0) w[ut] = 64;
                                            else if (next == 0x91) w[ut] = static_cast<char>(-95);
                                            else if (next == 0xB1) w[ut] = static_cast<char>(-85);
                                            else if (next == 0xA7)
                                            {
                                                w[ut] = 92;
                                                w[ut + 1] = 92;
                                            }
                                            else bad = true;
                                            break;
                                        default:
                                            bad = true;
                                            break;
                                    }
                                    if (bad)
                                        Error(listing[li].file, listing[li].line, "Unsupported UTF8 char for quoted string");
                                    if (w[ut + 1] && w[ut + 1] != 92)
                                    {
                                        size_t shift = ut + 1;
                                        do
                                        {
                                            w[shift] = w[shift + 1];
                                            shift++;
                                        } while (w[shift]);
                                    }
                                }
                            }
                        }
                        else if (!result.freeQuote)
                        {
                            for (size_t ut = 0; w[ut]; ++ut)
                            {
                                const signed char sc = static_cast<signed char>(w[ut]);
                                if (!((sc >= 32 && sc < 127) || sc == '\t'))
                                    Error(listing[li].file, listing[li].line,
                                          "Invalid char for quoted string use -fq option to allow UTF8");
                            }
                        }
                    }
                    break;
                case '(':
                    parenth++;
                    automate[static_cast<unsigned char>(',')] = 1;
                    PutW(c);
                    break;
                case ')':
                    parenth--;
                    if (!parenth)
                        automate[static_cast<unsigned char>(',')] = 2;
                    PutW(c);
                    break;
                case '\t':
                case ' ':
                    if (texpr)
                    {
                        const auto at = static_cast<size_t>(ispace);
                        switch (lw - ispace)
                        {
                            case 1:
                                if (w[at] == '%')
                                    w[at] = 'm';
                                break;
                            case 2:
                                if (w[at] == 'O' && w[at + 1] == 'R')
                                {
                                    w[at] = '|';
                                    lw = ispace + 1;
                                }
                                break;
                            case 3:
                                if (w[at] == 'A' && w[at + 1] == 'N' && w[at + 2] == 'D')
                                {
                                    w[at] = '&';
                                    lw = ispace + 1;
                                }
                                else if (w[at + 1] == 'O')
                                {
                                    if (w[at] == 'X' && w[at + 2] == 'R')
                                    {
                                        w[at] = '^';
                                        lw = ispace + 1;
                                    }
                                    else if (w[at] == 'M' && w[at + 2] == 'D')
                                    {
                                        w[at] = 'm';
                                        lw = ispace + 1;
                                    }
                                }
                                break;
                            default:
                                break;
                        }
                    }
                    ispace = lw;
                    break;
                default:
                    PutW(c);
                    break;
            }
            if (!noBreak)
                continue;
            noBreak = false;
            state = 2;
        }
        if (l >= listing.size())
            break;
        const int curLine = listing[l].line;
        const int curFile = listing[l].file;
        if (state == 2)
        {
            if (c == ',')
                hadComma = true;
            if (lw)
            {
                w[static_cast<size_t>(lw)] = '\0';
                if (lw == 3 && std::string_view(w.c_str()) == "EQU")
                {
                    if (!result.words.back().t)
                    {
                        const std::string previous = result.words.back().w;
                        result.words.pop_back();
                        lw = 0;
                        for (char pc : previous)
                            PutWRaw(pc);
                        currentE = lw + 1;
                        PutW('~');
                        automate[static_cast<unsigned char>(' ')] = 1;
                        automate[static_cast<unsigned char>('\t')] = 1;
                        ispace = lw;
                        texpr = true;
                    }
                    else
                        Error(curFile, curLine, "an alias name must precede the EQU");
                }
                else
                {
                    const std::string word(w.c_str());
                    PushWord(word, 0, curLine, curFile);
                    lw = 0;
                    w[0] = '\0';
                    if (keyMatched(word))
                    {
                        automate[static_cast<unsigned char>(' ')] = 1;
                        automate[static_cast<unsigned char>('\t')] = 1;
                        ispace = 0;
                        texpr = true;
                    }
                }
            }
            else if (hadComma && c == ',' && !options.macroMultiLine)
                Error(curFile, curLine,
                      "empty parameter right after word [" + (result.words.size() > 1 ? result.words.back().w : "(null)") + "]");
            continue;
        }
        if (state == 3)
        {
            parenth = 0;
            automate[static_cast<unsigned char>(',')] = 2;
            automate[static_cast<unsigned char>(' ')] = 2;
            automate[static_cast<unsigned char>('\t')] = 2;
            ispace = 0;
            if (lw)
            {
                PushWord(WString(), 1, curLine, curFile);
                lw = 0;
                w[0] = '\0';
            }
            else
            {
                result.words.back().t = 1;
                w[static_cast<size_t>(lw)] = '\0';
            }
            texpr = false;
            hadComma = false;
            continue;
        }
        // state 4: '=', '<', '>', '!': an assignment or a comparison
        texpr = true;
        if (lw == 1 && c == '=')
        {
            switch (w[0])
            {
                case '[': case ']': case '+': case '*': case '-': case '/': case '|': case '&':
                    opassign = w[0];
                    lw = 0;
                    break;
                default:
                    opassign = 0;
                    break;
            }
        }
        else
            opassign = 0;
        if (lw)
        {
            automate[static_cast<unsigned char>(' ')] = 1;
            automate[static_cast<unsigned char>('\t')] = 1;
            if (!currentE)
                currentE = lw + 1;
            PutW(c);
        }
        else
        {
            if (hadComma && c == '!')
                PutW(c);
            else if (!result.words.back().t)
            {
                if (!keyMatched(result.words.back().w))
                {
                    const std::string previous = result.words.back().w;
                    result.words.pop_back();
                    for (char pc : previous)
                        PutW(pc);
                    if (opassign)
                        PutW(opassign);
                    currentE = lw + 1;
                }
                PutW(c);
                automate[static_cast<unsigned char>(' ')] = 1;
                automate[static_cast<unsigned char>('\t')] = 1;
            }
            else
                Error(curFile, curLine, "cannot start expression with '=','!','<','>'");
        }
    }
    if (!result.words.empty() && !result.words.back().t)
        result.words.back().t = 1;
    Word end;
    end.w = "END";
    end.t = 2;
    result.words.push_back(std::move(end));

    if (options.as80)
    {
        for (size_t i = 0; i + 1 < result.words.size(); ++i)
            if (!result.words[i].t && !result.words[i].e && result.words[i + 1].w == "MACRO")
                std::swap(result.words[i].w, result.words[i + 1].w);
    }
}

PreproResult Preprocessor::Run(const std::string& mainName, const std::vector<uint8_t>& mainBytes)
{
    std::vector<std::string> lines = SplitLines(mainBytes, ':');
    result.files.push_back(mainName);
    EarlyPrep(lines, 0);
    for (size_t i = 0; i < lines.size(); ++i)
        listing.push_back(Listing{std::move(lines[i]), 0, static_cast<int>(i) + 1});
    if (!listing.empty())
        listing.back().text += ':';
    Pass1();
    Words();
    return std::move(result);
}
}  // namespace

std::string MergePath(const std::string& parent, const std::string& name)
{
    std::string file = name;
    std::replace(file.begin(), file.end(), '\\', '/');
    std::string dir;
    const auto slash = parent.rfind('/');
    dir = slash == std::string::npos ? "./" : parent.substr(0, slash + 1);
    if (!file.empty() && file[0] == '/')
        return file;
    if (file.size() >= 2 && file[0] == '.' && file[1] == '/')
        return dir + file.substr(2);
    return dir + file;
}

bool IsKeyword(const std::string& word)
{
    return std::find(std::begin(keywords), std::end(keywords), word) != std::end(keywords);
}

PreproResult Preprocess(const std::string& mainName, const std::vector<uint8_t>& mainBytes, const FileSystem& fs,
                        const PreproOptions& options)
{
    Preprocessor p(fs, options);
    return p.Run(mainName, mainBytes);
}
}  // namespace unrealasm::rasm
