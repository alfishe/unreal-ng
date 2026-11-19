// rasm's directives, second part: banks, crunched sections, LIMIT, PRINT / STOP / FAIL, CHARSET, MODULE, LABEL,
// TICKER, RUN, SAVE, the pokers (SUMMEM ...), TIMESTAMP, INCBIN (HEXBIN), the output selection (BUILDZX ...), the
// directive table and the end of the assembly (pokers, SAVE files, the ZX snapshot). Ported from rasm.c v3.3.3; the
// rasm function each method comes from is named in its comment. CPC media (snapshots, cartridges, ROMs, tapes, disks)
// are not produced: the directives keep their effect on the assembly and the output is reported as not converted.

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unordered_map>

#include "dialects/rasm/engine/rasmdata.h"
#include "dialects/rasm/engine/rasmengine.h"
#include "unrealasm/compress.h"

namespace unrealasm::rasm
{
namespace
{
std::string Unquote(const std::string& w)
{
    if (w.size() >= 2 && (w[0] == '"' || w[0] == '\'') && w.back() == w[0])
        return w.substr(1, w.size() - 2);
    return w;
}

void ReplaceAll(std::string& s, const std::string& from, const std::string& to)
{
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos)
    {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::tm LocalTime(int64_t seconds)
{
    const std::time_t t = seconds < 0 ? std::time(nullptr) : static_cast<std::time_t>(seconds);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

const char* const kPokerNames[] = {
    "XORMEM",
    "SUMMEM",
    "CIPHER001 running XOR initialised with first value",
    "CIPHER002 running XOR initialised with memory location",
    "CIPHER003 XOR with LSB of memory location",
    "CIPHER004 XOR with key looping",
    "SUM16",
};
}  // namespace

std::optional<compress::Algorithm> CrunchAlgorithm(int code, int version)
{
    switch (code)
    {
        case 70: return compress::Algorithm::Zx0;
        case 71: return compress::Algorithm::Zx0Backward;
        case 7: return compress::Algorithm::Zx7;
        case 4: return compress::Algorithm::Lz4;
        case 8: return compress::Algorithm::Exomizer;
        case 17: return compress::Algorithm::ApUltra;
        case 18: return version == 1 ? compress::Algorithm::Lzsa1 : compress::Algorithm::Lzsa2;
        case 48: return compress::Algorithm::Lz48;
        case 49: return compress::Algorithm::Lz49;
        default: return std::nullopt;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// diagnostics and printing

void Engine::Print(const std::string& text)
{
    for (const char c : text)
    {
        if (c == '\n')
        {
            diagnostics.push_back({Severity::Info, 0, 0, printLine});
            printLine.clear();
        }
        else
        {
            printLine += c;
        }
    }
}

void Engine::NotConverted(const std::string& what)
{
    for (const auto& n : notConverted)
    {
        if (n == what)
            return;
    }
    notConverted.push_back(what);
    diagnostics.push_back({Severity::Warning, 0, 0, "not converted: " + what});
}

/// rasm upper-cases the text between braces before TranslateTag
std::string Engine::UpperTags(std::string text) const
{
    int touched = 0;
    for (char& c : text)
    {
        if (c == '{')
            touched++;
        else if (c == '}')
            touched--;
        else if (touched)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return text;
}

// __PRINT
void Engine::PRINT()
{
    while (W(0).t != 1)
    {
        const std::string& arg = W(1).w;
        if (!StringIsQuote(arg))
        {
            std::string expr;
            int hex = 0, bin = 0, entier = 0;
            auto starts = [&arg](const char* p) { return arg.compare(0, std::strlen(p), p) == 0; };
            if (starts("{HEX}"))
            {
                expr = arg.substr(5);
                hex = 1;
            }
            else if (starts("{HEX2}"))
            {
                expr = arg.substr(6);
                hex = 2;
            }
            else if (starts("{HEX4}"))
            {
                expr = arg.substr(6);
                hex = 4;
            }
            else if (starts("{HEX8}"))
            {
                expr = arg.substr(6);
                hex = 8;
            }
            else if (starts("{BIN}"))
            {
                expr = arg.substr(5);
                bin = 1;
            }
            else if (starts("{BIN8}"))
            {
                expr = arg.substr(6);
                bin = 8;
            }
            else if (starts("{BIN16}"))
            {
                expr = arg.substr(7);
                bin = 16;
            }
            else if (starts("{BIN32}"))
            {
                expr = arg.substr(7);
                bin = 32;
            }
            else if (starts("{INT}"))
            {
                expr = arg.substr(5);
                entier = 1;
            }
            else if (starts("{INT") && arg.size() > 5 && arg[5] == '}')
            {
                expr = arg.substr(6);
                entier = arg[4] - '0';
                if (entier < 2 || entier > 5)
                {
                    MakeError(idx, "invalid prefix, must be from INT2 to INT5");
                    entier = 5;
                }
            }
            else
            {
                expr = arg;
            }
            char buf[96];
            if (hex)
            {
                const int zv = RoundComputeExpressionCore(expr, codeadr, 0);
                const auto u = static_cast<unsigned>(zv);
                switch (hex)
                {
                    case 1:
                        if (u & 0xFFFFFF00u)
                        {
                            if (u & 0xFFFF0000u)
                                std::snprintf(buf, sizeof buf, "#%-8.08X ", u);
                            else
                                std::snprintf(buf, sizeof buf, "#%-4.04X ", u);
                        }
                        else
                        {
                            std::snprintf(buf, sizeof buf, "#%-2.02X ", u);
                        }
                        break;
                    case 2: std::snprintf(buf, sizeof buf, "#%-2.02X ", u); break;
                    case 4: std::snprintf(buf, sizeof buf, "#%-4.04X ", u); break;
                    default: std::snprintf(buf, sizeof buf, "#%-8.08X ", u); break;
                }
                Print(buf);
            }
            else if (bin)
            {
                auto zv = static_cast<unsigned>(RoundComputeExpressionCore(expr, codeadr, 0));
                if (bin < 32 && (zv & 0xFFFF0000u) == 0xFFFF0000u)
                    zv &= 0xFFFF;
                int d = 31;
                switch (bin)
                {
                    case 1: d = (zv & 0xFF00) ? 15 : 7; break;
                    case 8: d = 7; break;
                    case 16: d = 15; break;
                    default: d = 31; break;
                }
                std::string s = "%";
                for (; d >= 0; d--)
                    s += ((zv >> d) & 1) ? '1' : '0';
                Print(s + " ");
            }
            else if (entier)
            {
                std::snprintf(buf, sizeof buf, "%0*d ", entier, RoundComputeExpressionCore(expr, codeadr, 0));
                Print(buf);
            }
            else
            {
                std::snprintf(buf, sizeof buf, "%.2lf ", ComputeExpressionCore(expr, codeadr, 0));
                Print(buf);
            }
        }
        else
        {
            const size_t lm = arg.size() - 2;
            if (lm)
            {
                std::string text = UpperTags(arg.substr(1, lm) + " ");
                int touched = 0;
                text = TranslateTag(text, &touched, kTagRemoveSpace);
                ReplaceAll(text, "\\b", "\b");
                ReplaceAll(text, "\\v", "\v");
                ReplaceAll(text, "\\f", "\f");
                ReplaceAll(text, "\\r", "\r");
                ReplaceAll(text, "\\n", "\n");
                ReplaceAll(text, "\\t", "\t");
                Print(text + " ");
            }
        }
        idx++;
    }
    Print("\n");
}

// __DELAYED_PRINT: printed after the deferred expressions are resolved
void Engine::DELAYEDPRINT()
{
    delayedPrintIdx.push_back(idx);
    while (W(0).t != 1)
        idx++;
}

// __STOP
void Engine::STOP()
{
    Print("[" + CurrentFile() + ":" + std::to_string(W(0).line) + "] STOP assembling requested\n");
    while (W(0).t != 2)
        idx++;
    idx--;
    stop = 1;
}

// __FAIL: PRINT, STOP, then the assembly counts as failed (MaxError)
void Engine::FailDirective()
{
    const int at = idx;
    PRINT();
    STOP();
    MakeError(at, "FAIL directive");
}

// ---------------------------------------------------------------------------------------------------------------------
// memory spaces

// __LIMIT
void Engine::LIMIT()
{
    if (W(1).t != 2)
    {
        const int zelimit = RoundComputeExpression(W(1).w, outputadr, 0, 0);
        int ext = 0;
        if (!W(1).t)
        {
            if (W(2).w == "EXTENDED")
            {
                ext = 1;
                idx++;
            }
            else
            {
                MakeError(idx, "LIMIT 2nd parameter must be EXTENDED");
            }
        }
        OutputSetLimit(zelimit, ext);
        idx++;
    }
    else
    {
        MakeError(idx, "LIMIT directive need one integer parameter");
    }
}

// __BANK
void Engine::BANK()
{
    if (getstruct == 1)
    {
        MakeError(idx, "You cannot change BANK inside a structure declaration");
        return;
    }
    lz = -1;
    if (!orgzone.empty())
        orgzone.back().memend = outputadr;
    if (W(0).t == 1)
    {
        NewMemorySpace();
        return;
    }
    bankmode = 1;
    // BANK without a BUILD directive selects the cartridge output
    if (!forceROM && !forcecpr && !forcesnapshot && !forcezx)
        forcecpr = 1;
    if (W(1).t != 2)
    {
        if (W(1).w == "NEXT")
        {
            if (activebank >= kBankMaxNumber - 1)
            {
                NewMemorySpace();
                return;
            }
            lastbank = activebank;
            activebank++;
        }
        else
        {
            lastbank = activebank;
            const int bank = RoundComputeExpression(W(1).w, codeadr, 0, 0);
            if (bank < 0 || bank >= kBankMaxNumber)
            {
                // rasm indexes its tables with the value before its range checks; every mode refuses it
                MakeError(idx, "FATAL - Bank selection must be from 0 to 259");
                stop = 1;
                return;
            }
            activebank = bank;
            maxptr = memsize[static_cast<size_t>(activebank)];
        }
        auto& m = mem[static_cast<size_t>(activebank)];
        if (m.empty())
            m.assign(65536, 0);
        const char* fatal = nullptr;
        if (forcecpr && (activebank < 0 || activebank > 31) && !extendedCPR)
            fatal = "FATAL - Bank selection must be from 0 to 31 in cartridge mode";
        else if (forcezx && (activebank < 0 || activebank > 7))
            fatal = "FATAL - Bank selection must be from 0 to 7 in ZX Spectrum mode";
        else if (forceROM && (activebank < 0 || activebank >= 256))
            fatal = "FATAL - Bank selection must be from 0 to 255 in ROM mode";
        else if (forcesnapshot && (activebank < 0 || activebank >= 260))
            fatal = "FATAL - Bank selection must be from 0 to 259 in snapshot mode";
        if (fatal)
        {
            MakeError(idx, fatal);
            stop = 1;
            return;
        }
        if (forcesnapshot && bankset[static_cast<size_t>(activebank >> 2)])
        {
            MakeError(idx, "Cannot BANK " + std::to_string(activebank) + " was already select by a previous BANKSET " +
                               std::to_string(activebank / 4));
            idx++;
            return;
        }
        bankused[static_cast<size_t>(activebank)] = 1;
        idx++;
    }
    else
    {
        MakeError(idx, "BANK directive need one integer parameter");
        return;
    }
    if (lz >= 0)
    {
        MakeWarning(idx, "LZ section wasn't closed before a new BANK directive");
        LZCLOSE();
    }
    GetBackToMemorySpace();
}

// __REBANK
void Engine::REBANK()
{
    if (getstruct == 1)
    {
        MakeError(idx, "You cannot change BANK inside a structure declaration");
        return;
    }
    if (W(0).t)
    {
        lz = -1;
        if (!orgzone.empty())
            orgzone.back().memend = outputadr;
        activebank = lastbank;
        maxptr = memsize[static_cast<size_t>(activebank)];
        GetBackToMemorySpace();
    }
    else
    {
        MakeError(idx, "REBANK directive has no parameter");
    }
}

// __ROMBANK
void Engine::ROMBANK()
{
    if (getstruct == 1)
    {
        MakeError(idx, "You cannot change BANK inside a structure declaration");
        return;
    }
    if (!forcesnapshot)
    {
        MakeError(idx, "ROMBANK can be used only with snapshot output");
        return;
    }
    lz = -1;
    if (!orgzone.empty())
        orgzone.back().memend = outputadr;
    int romSelect = 0;
    if (!W(0).t)
    {
        if (W(1).w == "NEXT")
        {
            if (activebank >= kBankMaxNumber - 1)
            {
                int i = 0;
                for (; i < 257; i++)
                {
                    if (rombank[static_cast<size_t>(i)] == activebank)
                        break;
                }
                if (i < 257)
                    romSelect = i + 1;
                if (romSelect >= 256)
                    romSelect = 0;
            }
        }
        else if (W(1).w == "LOWER")
        {
            romSelect = 256;
            idx++;
        }
        else
        {
            romSelect = RoundComputeExpression(W(1).w, codeadr, 0, 0);
        }
    }
    else
    {
        MakeError(idx, "usage is ROMBANK <rom number>");
        return;
    }
    if (romSelect < 0 || romSelect > 256)
    {
        MakeError(idx, "usage is ROMBANK <rom number>");
        return;
    }
    if (!rombank[static_cast<size_t>(romSelect)])
    {
        NewMemorySpace();
        rombank[static_cast<size_t>(romSelect)] = activebank;
    }
    else
    {
        activebank = rombank[static_cast<size_t>(romSelect)];
        maxptr = memsize[static_cast<size_t>(activebank)];
        GetBackToMemorySpace();
    }
}

// __BANKSET
void Engine::BANKSET()
{
    if (getstruct == 1)
    {
        MakeError(idx, "You cannot change BANK inside a structure declaration");
        return;
    }
    lz = -1;
    if (!forcesnapshot && !forcecpr && !forcezx && !forceROM)
        forcesnapshot = 1;
    if (!forcesnapshot)
    {
        MakeError(idx, "BANKSET directive is specific to snapshot output");
        return;
    }
    if (!orgzone.empty())
        orgzone.back().memend = outputadr;
    bankmode = 1;
    if (W(1).t != 2)
    {
        lastbank = activebank;
        const int set = RoundComputeExpression(W(1).w, codeadr, 0, 0);
        if (set < 0 || set * 4 >= kBankMaxNumber)
        {
            MakeError(idx, "FATAL - Bank set selection must be from 0 to 64 in snapshot mode");
            stop = 1;
            return;
        }
        activebank = set * 4;
        maxptr = memsize[static_cast<size_t>(activebank)];
        const auto ibank = static_cast<size_t>(activebank);
        if (mem[ibank].empty())
            mem[ibank].assign(65536, 0);
        if (!bankset[ibank >> 2])
        {
            if (bankused[ibank] || bankused[ibank + 1] || bankused[ibank + 2] || bankused[ibank + 3])
            {
                MakeError(idx, "Cannot BANKSET because bank " + std::to_string(ibank) +
                                   " was already selected in single page mode");
                idx++;
                return;
            }
            for (size_t k = 1; k <= 3; k++)
            {
                if (mem[ibank + k].empty())
                    mem[ibank + k].assign(65536, 0);
            }
            // rebuild the set from the pages already written (done once)
            bankset[ibank >> 2] = 1;
            for (size_t i = 16384; i < 32768; i++)
            {
                mem[ibank][i] = mem[ibank + 1][i];
                mem[ibank][i + 16384] = mem[ibank + 2][i + 16384];
                mem[ibank][i + 32768] = mem[ibank + 3][i + 32768];
            }
        }
        idx++;
    }
    else
    {
        MakeError(idx, "BANKSET directive need one integer parameter");
        return;
    }
    if (lz >= 0)
    {
        MakeWarning(idx, "LZ section wasn't closed before a new BANKSET directive");
        LZCLOSE();
    }
    outputadr = 0;
    codeadr = 0;
    OrgZone zone;
    zone.memstart = 0;
    zone.ibank = activebank;
    zone.nocode = nocode = 0;
    orgzone.push_back(zone);
    OverWriteCheck();
}

// ---------------------------------------------------------------------------------------------------------------------
// crunched sections

// __LZ4, __LZ48, __LZSA1 ...: open a section crunched at the end of the assembly
void Engine::LzOpen(int lzversion, int version, bool takesMinMatch, const char* name)
{
    LzSection cur;
    if (takesMinMatch)
    {
        if (!W(0).t)
        {
            idx++;
            cur.minmatch = std::atoi(W(0).w.c_str());
            if (!W(0).t)
                MakeError(idx, std::string(name) + " directive may only have 1 parameter");
        }
    }
    else if (!W(0).t)
    {
        MakeError(idx, "LZ directive does not need any parameter");
        return;
    }
    if (lz >= 0 && lz < static_cast<int>(lzsection.size()) && lzsection.back().lzversion)
    {
        MakeError(idx, "Cannot start a new LZ section inside another one (" + std::to_string(lz) + ")");
        stop = 1;
        return;
    }
    cur.iexpr = static_cast<int>(expression.size());
    cur.ilabel = static_cast<int>(label.size());
    cur.version = version;
    cur.iw = idx;
    cur.iorgzone = static_cast<int>(orgzone.size()) - 1;
    cur.ibank = activebank;
    cur.memstart = outputadr;
    cur.memend = -1;
    cur.lzversion = lzversion;
    lz = static_cast<int>(lzsection.size());
    lzsection.push_back(cur);
}

void Engine::LZ4() { LzOpen(4, 0, false, "LZ4"); }
void Engine::LZ48() { LzOpen(48, 0, false, "LZ48"); }
void Engine::LZ49() { LzOpen(49, 0, false, "LZ49"); }
void Engine::LZX7() { LzOpen(7, 0, false, "LZX7"); }
void Engine::LZX0() { LzOpen(70, 32640, false, "LZX0"); }
void Engine::LZX0B() { LzOpen(71, 32640, false, "LZX0B"); }
void Engine::LZEXO() { LzOpen(8, 0, false, "LZEXO"); }
void Engine::LZAPU() { LzOpen(17, 0, false, "LZAPU"); }
void Engine::LZSA1() { LzOpen(18, 1, true, "LZSA1"); }
void Engine::LZSA2() { LzOpen(18, 2, true, "LZSA2"); }

// __LZCLOSE: ends the section and opens an intermediate zone (rasm never comes back to "no section")
void Engine::LZCLOSE()
{
    if (lzsection.empty() || lz == -1)
    {
        MakeError(idx, "Cannot close LZ section as it wasn't opened");
        return;
    }
    LzSection& last = lzsection.back();
    last.memend = outputadr;
    last.iendlabel = static_cast<int>(label.size());
    last.iendexpr = static_cast<int>(expression.size());
    LzSection cur;
    cur.iexpr = static_cast<int>(expression.size());
    cur.ilabel = static_cast<int>(label.size());
    cur.iw = idx;
    cur.iorgzone = static_cast<int>(orgzone.size()) - 1;
    cur.ibank = activebank;
    cur.memstart = outputadr;
    cur.memend = -1;
    cur.lzversion = 0;
    lz = static_cast<int>(lzsection.size());
    lzsection.push_back(cur);
}

// ---------------------------------------------------------------------------------------------------------------------
// strings

// __CHARSET
void Engine::CHARSET()
{
    if (W(0).t == 1)
    {
        for (int i = 0; i < 256; i++)
            charset[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    }
    else if (!W(0).t && !W(1).t && W(2).t == 1)
    {
        std::string err;
        if (StringIsQuote(W(1).w) && StringIsQuote(W(2).w))
        {
            err = CharsetMapStrings(charset, W(1).w, W(2).w);
        }
        else
        {
            const int v = RoundComputeExpression(W(2).w, codeadr, 0, 0);
            if (W(1).w[0] == '\'' || W(1).w[0] == '"')
                err = CharsetMapString(charset, W(1).w, v);
            else
                err = CharsetMapByte(charset, RoundComputeExpression(W(1).w, codeadr, 0, 0), v);
        }
        if (!err.empty())
            MakeError(idx, err);
        idx += 2;
    }
    else if (!W(0).t && !W(1).t && !W(2).t && W(3).t == 1)
    {
        const int s = RoundComputeExpression(W(1).w, codeadr, 0, 0);
        const int e = RoundComputeExpression(W(2).w, codeadr, 0, 0);
        const int v = RoundComputeExpression(W(3).w, codeadr, 0, 0);
        idx += 3;
        const std::string err = CharsetMapRange(charset, s, e, v);
        if (!err.empty())
            MakeError(idx, err);
    }
    else
    {
        MakeError(idx, "CHARSET Winape directive wrong parameter count");
    }
}

// __UTF8REMAP
void Engine::UTF8REMAP()
{
    if (W(0).t == 1)
    {
        utf8Remap.clear();
    }
    else if (!W(0).t && !W(1).t && W(2).t == 1)
    {
        if (StringIsQuote(W(1).w))
        {
            const int v = RoundComputeExpression(W(2).w, codeadr, 0, 0);
            if (auto r = ParseUtf8Remap(W(1).w, v))
                utf8Remap.push_back(*r);
        }
    }
    else
    {
        MakeError(idx, "UTF8REMAP must be used without parameter OR two params : utf8remap <utf8char>,<conversion value>");
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// modules, labels, export flags

// __MODULE
void Engine::MODULE()
{
    if (!W(0).t)
    {
        if (W(1).t == 1)
        {
            if (W(1).w == "OFF")
            {
                module.clear();
                hasModule = false;
            }
            else
            {
                int touched = 0;
                module = TranslateTag(W(1).w, &touched, kTagRemoveSpace);
                hasModule = true;
            }
        }
        else
        {
            MakeError(idx, "usage is MODULE [<module name>|OFF], directive without parameter or using 'OFF' will quit the current module");
        }
        idx++;
    }
    else
    {
        module.clear();
        hasModule = false;
    }
}

// __ENDMODULE
void Engine::ENDMODULE()
{
    if (!W(0).t && W(1).t == 1)
        MakeError(idx, "ENDMODULE does not need any parameter");
    else
        MODULE();
}

// __internal_EXPORT
void Engine::ExportFlags(int exportval)
{
    if (W(0).t)
    {
        autoriseExport = exportval;
        return;
    }
    while (!W(0).t)
    {
        idx++;
        const std::string& w = W(0).w;
        const std::string name = (w[0] == '.' && !lastgloballabel.empty()) ? lastgloballabel + w : w;
        if (Label* l = SearchLabel(name))
        {
            l->autoriseExport = exportval != 0;
        }
        else if (DicoVar* d = SearchDico(w))
        {
            d->autoriseExport = exportval != 0;
        }
        else if (Alias* a = SearchAlias(w))
        {
            a->autoriseExport = exportval != 0;
        }
        else
        {
            MakeError(idx, "(E)NOEXPORT did not found [" + w + "] in variables, labels or aliases");
        }
    }
}

// __internal_LOCAL
void Engine::LocalFlags(int localval)
{
    if (W(0).t)
    {
        localExport = localval;
        return;
    }
    while (!W(0).t)
    {
        idx++;
        const std::string& w = W(0).w;
        const std::string name = (w[0] == '.' && !lastgloballabel.empty()) ? lastgloballabel + w : w;
        if (Label* l = SearchLabel(name))
            l->localExport = localval != 0;
        else
            MakeError(idx, "LABEL directive did not found [" + w + "] in labels");
    }
}

void Engine::NOEXPORT() { ExportFlags(0); }
void Engine::ENOEXPORT() { ExportFlags(1); }

// __LABEL
void Engine::LABEL()
{
    static const char* usage = "usage is LABEL [NOEXPORT|EXPORT|LOCAL|GLOBAL[<label>,<label>,...]]";
    if (W(0).t)
    {
        MakeError(idx, usage);
        return;
    }
    const std::string k = W(1).w;
    if (k == "NOEXPORT" || k == "EXPORT" || k == "LOCAL" || k == "GLOBAL")
    {
        idx++;
        if (k == "NOEXPORT")
            ExportFlags(0);
        else if (k == "EXPORT")
            ExportFlags(1);
        else
            LocalFlags(k == "LOCAL" ? 1 : 0);
    }
    else
    {
        MakeError(idx, usage);
        while (!W(0).t)
            idx++;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// variables, counters, run address

// __LET (Winape)
void Engine::LET()
{
    if (!W(0).t && W(1).t == 1)
    {
        idx++;
        RoundComputeExpression(W(0).w, codeadr, 0, 0);
    }
    else
    {
        MakeError(idx, "LET useless Winape directive need one expression");
    }
}

// __TICKER
void Engine::TICKER()
{
    static const char* usage = "usage is TICKER start/stop(z),<variable>";
    if (!W(0).t && !W(1).t && W(2).t == 1)
    {
        const std::string var = W(2).w;
        size_t i = 0;
        for (; i < ticker.size(); i++)
        {
            if (ticker[i].varname == var)
                break;
        }
        if (W(1).w == "START")
        {
            if (i == ticker.size())
            {
                Ticker t;
                t.varname = var;
                ticker.push_back(t);
            }
            ticker[i].nopstart = nop;
            ticker[i].tickerstart = tick;
        }
        else if (W(1).w.compare(0, 4, "STOP") == 0)
        {
            if (i < ticker.size())
            {
                const bool z = W(1).w.size() > 4 && W(1).w[4] == 'Z';
                const double v = z ? static_cast<double>(static_cast<long>(tick) - ticker[i].tickerstart)
                                   : static_cast<double>(static_cast<long>(nop) - ticker[i].nopstart);
                if (DicoVar* d = SearchDico(var))
                {
                    d->v = v;
                    d->used = true;
                }
                else
                {
                    ExpressionSetDicoVar(var, v);
                }
            }
            else
            {
                MakeError(idx, "TICKER not found");
            }
        }
        else
        {
            MakeError(idx, usage);
        }
        idx += 2;
    }
    else
    {
        MakeError(idx, usage);
    }
}

// __RUN: ZX mode takes the address and the stack (BUILDZX snapshot), CPC mode the address and the gate array RAM
void Engine::RUN()
{
    if (!W(0).t)
    {
        currentRunIdx = idx + 1;
        if (forcezx)
        {
            PushExpression(idx + 1, ExprKind::ZxRun);
            idx++;
            if (!W(0).t)
            {
                PushExpression(idx + 1, ExprKind::ZxStack);
                idx++;
            }
            else
            {
                MakeError(idx, "usage is RUN <address>,<stack> (ZX mode, you must set address+stack)");
            }
        }
        else
        {
            PushExpression(idx + 1, ExprKind::Run);
            idx++;
            if (!W(0).t)
            {
                const int ramconf = RoundComputeExpression(W(1).w, codeadr, 0, 0);
                idx++;
                if (ramconf < 0xC0 || ramconf > 0xFF)
                {
                    char buf[80];
                    std::snprintf(buf, sizeof buf, "ram configuration out of bound %X forced to #C0", static_cast<unsigned>(ramconf));
                    MakeWarning(idx, buf);
                }
            }
        }
    }
    else
    {
        MakeError(idx, forcezx ? "usage is RUN <address>,<stack> (ZX mode)" : "usage is RUN <address>[,<ppi>]");
    }
    if (rundefined)
        MakeWarning(idx, "run address redefinition");
    rundefined = 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// SAVE (written after the crunched sections and the pokers)

// __SAVE
void Engine::SAVE()
{
    bool ko = true;
    if (!W(0).t)
    {
        if (!StringIsQuote(W(1).w))
        {
            MakeError(idx, "SAVE invalid filename quote");
            ko = false;
        }
        else if (!W(1).t)
        {
            std::string& fw = W(1).w;
            for (char& c : fw)
            {
                if (c == '\\')
                    c = '/';
            }
            int touched = 0;
            std::string fn = TranslateTag(UpperTags(fw), &touched, kTagRemoveSpace);
            if (!W(2).t && W(3).t != 2)
            {
                Save cur;
                cur.ibank = activebank;
                cur.ioffset = idx + 2;
                ExpressionFastTranslate(W(2).w, 1);
                cur.isize = idx + 3;
                ExpressionFastTranslate(W(3).w, 1);
                if (!touched)
                    cur.iw = idx + 1;
                else
                    cur.filename = fn;
                cur.irun = currentRunIdx;
                if (!W(3).t)
                {
                    const std::string& k = W(4).w;
                    if (k == "TAPE")
                    {
                        cur.tape = 1;
                        if (W(4).t)
                            MakeError(idx, "cannot autoselect TAPE, please specify a filename after TAPE arg");
                    }
                    else if (k == "AMSDOS")
                    {
                        cur.amsdos = 1;
                    }
                    else if (k == "HOBETA")
                    {
                        cur.hobeta = 1;
                    }
                    else if (k == "DSK")
                    {
                        cur.dsk = 1;
                        bool previous = false;
                        for (const auto& s : save)
                            previous = previous || s.dsk;
                        if (W(4).t && !previous)
                            MakeError(idx, "cannot autoselect DSK as there was not a previous selection");
                    }
                    else
                    {
                        MakeError(idx, "SAVE 4th parameter must be empty or AMSDOS or HOBETA or DSK");
                        ko = false;
                    }
                }
                save.push_back(cur);
                ko = false;
            }
        }
    }
    if (ko)
        MakeError(idx, "Use SAVE 'filename',offset,size[,AMSDOS|DSK[,A|B|'dskname'[,A|B]]]");
    while (!W(0).t)
        idx++;
}

// PopAllSave
void Engine::PopAllSave()
{
    for (const Save& s : save)
    {
        const std::string fname = s.iw ? Unquote(wl[static_cast<size_t>(s.iw)].w) : Unquote(s.filename);
        idx = s.ioffset;
        const int offset = RoundComputeExpression(W(0).w, 0, 0, 0);
        idx = s.isize;
        const int size = RoundComputeExpression(W(0).w, 0, 0, 0);
        int run = offset;
        if (s.irun)
        {
            idx = s.irun;
            run = RoundComputeExpression(W(0).w, 0, 0, 0);
        }
        if (s.dsk)
        {
            NotConverted("SAVE ...,DSK (CPC disk image) [" + fname + "]");
            continue;
        }
        if (s.tape)
        {
            NotConverted("SAVE ...,TAPE (CDT tape) [" + fname + "]");
            continue;
        }
        const SaveFormat format = s.amsdos ? SaveFormat::Amsdos : s.hobeta ? SaveFormat::Hobeta : SaveFormat::Plain;
        const DataResult r = MakeSaveFile(fname, mem[static_cast<size_t>(s.ibank)], offset, size, run, format);
        for (const auto& w : r.warnings)
            MakeWarning(-1, w);
        if (!r.error.empty())
        {
            MakeError(-1, r.error);
            continue;
        }
        outputFiles.push_back({fname, r.bytes, s.ibank, offset, size, format});
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// pokers: checksums and ciphers computed on the final memory

void Engine::PokerDirective(PokerMethod method, int bytes, const char* name)
{
    if (!W(0).t && !W(1).t && W(2).t == 1)
    {
        if (!nocode)
        {
            Poker p;
            p.method = method;
            p.istart = idx + 1;
            p.iend = idx + 2;
            ExpressionFastTranslate(W(1).w, 1);
            ExpressionFastTranslate(W(2).w, 1);
            p.outputadr = outputadr;
            p.ibank = activebank;
            p.ipoker = idx;
            poker.push_back(p);
        }
        for (int i = 0; i < bytes; i++)
            Output(0);
    }
    else
    {
        MakeError(idx, std::string("usage is ") + name + " start,end");
    }
}

void Engine::SUMMEM() { PokerDirective(PokerMethod::Sum8, 1, "SUMMEM"); }
void Engine::SUM16() { PokerDirective(PokerMethod::Sum16, 2, "SUM16"); }
void Engine::XORMEM() { PokerDirective(PokerMethod::Xor8, 1, "XORMEM"); }

// __CIPHERMEM
void Engine::CIPHERMEM()
{
    if (!W(0).t && !W(1).t)
    {
        if (nocode)
            return;
        Poker p;
        p.method = PokerMethod::Cipher001;
        if (!W(2).t)
        {
            const int mode = RoundComputeExpression(W(3).w, outputadr, 0, 0);
            switch (mode)
            {
                case 2: p.method = PokerMethod::Cipher002; break;
                case 3: p.method = PokerMethod::Cipher003; break;
                case 4:
                    p.method = PokerMethod::Cipher004;
                    if (!W(3).t)
                        p.istring = idx + 4;
                    else
                        MakeError(idx, "usage is CIPHERMEM start,end,4,'keystring'");
                    break;
                default: p.method = PokerMethod::Cipher001; break;
            }
            if (mode != 4 && !W(3).t)
                MakeWarning(idx, "keystring parameter will be ignored");
            if (!W(3).t && !W(4).t)
                MakeError(idx, "Too many parameters! Usage is CIPHERMEM start,end[,method[,'keystring']]");
        }
        p.istart = idx + 1;
        p.iend = idx + 2;
        ExpressionFastTranslate(W(1).w, 1);
        ExpressionFastTranslate(W(2).w, 1);
        p.outputadr = outputadr;
        p.ibank = activebank;
        p.ipoker = idx;
        poker.push_back(p);
    }
    else
    {
        MakeError(idx, "usage is CIPHERMEM start,end[,method[,'keystring']]");
    }
}

// the poker loop of rasm's end of assembly
void Engine::ApplyPokers()
{
    for (size_t i = 0; i < poker.size(); i++)
    {
        const Poker& p = poker[i];
        idx = p.istart;
        const int istart = RoundComputeExpression(W(0).w, 0, 0, 0);
        idx = p.iend;
        const int iend = RoundComputeExpression(W(0).w, 0, 0, 0);
        auto& bank = mem[static_cast<size_t>(p.ibank)];
        auto put = [&bank](int at, uint8_t v) {
            if (at >= 0 && at < static_cast<int>(bank.size()))
                bank[static_cast<size_t>(at)] = v;
        };
        switch (p.method)
        {
            case PokerMethod::Xor8: put(p.outputadr, XorMem(bank, istart, iend)); break;
            case PokerMethod::Sum8: put(p.outputadr, SumMem(bank, istart, iend)); break;
            case PokerMethod::Sum16:
            {
                const uint16_t s = Sum16(bank, istart, iend);
                put(p.outputadr, static_cast<uint8_t>(s & 0xFF));
                put(p.outputadr + 1, static_cast<uint8_t>(s >> 8));
                break;
            }
            case PokerMethod::Cipher001:
            case PokerMethod::Cipher002:
            case PokerMethod::Cipher003:
            case PokerMethod::Cipher004:
            {
                const int method = static_cast<int>(p.method) - static_cast<int>(PokerMethod::Cipher001) + 1;
                std::string key;
                if (p.method == PokerMethod::Cipher004)
                {
                    // the text between the quotes as rasm holds it (no escape decoding)
                    key = Unquote(wl[static_cast<size_t>(p.istring)].w);
                }
                const std::string err = CipherMem(bank, istart, iend, method, key);
                if (!err.empty())
                    MakeError(idx, err);
                break;
            }
        }
        for (const Poker& o : poker)
        {
            if (p.ibank == o.ibank && o.outputadr >= istart && o.outputadr < iend)
            {
                const auto mi = static_cast<size_t>(p.method);
                const auto mo = static_cast<size_t>(o.method);
                MakeWarning(o.istart, std::string(kPokerNames[mi]) + " poker result is inside another " +
                                          kPokerNames[mo] + " poker calculation");
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// TIMESTAMP, BRK, LIST

// __TIMESTAMP: date and time as text through the CHARSET table
void Engine::TIMESTAMP()
{
    const std::tm local = LocalTime(options.timestamp);
    std::string timestr = "[Y-M-D h:m]";
    if (!W(0).t)
    {
        idx++;
        timestr = W(0).w.substr(1);
    }
    auto put2 = [this](int v) {
        char t[16];
        std::snprintf(t, sizeof t, "%02d", v);
        Output(charset[static_cast<uint8_t>(t[0])]);
        Output(charset[static_cast<uint8_t>(t[1])]);
    };
    size_t i = 0;
    while (i < timestr.size())
    {
        const char c = timestr[i];
        auto run = [&]() {
            int n = 0;
            while (i < timestr.size() && timestr[i] == c)
            {
                i++;
                n++;
            }
            return n;
        };
        switch (c)
        {
            case 'Y':
            {
                const int cpt = run();
                char t[16];
                std::snprintf(t, sizeof t, "%04d", local.tm_year + 1900);
                if (cpt == 2)
                {
                    Output(charset[static_cast<uint8_t>(t[2])]);
                    Output(charset[static_cast<uint8_t>(t[3])]);
                }
                else if (cpt == 1 || cpt == 4)
                {
                    for (int k = 0; k < 4; k++)
                        Output(charset[static_cast<uint8_t>(t[k])]);
                }
                break;
            }
            case 'M': run(); put2(local.tm_mon + 1); break;
            case 'D': run(); put2(local.tm_mday); break;
            case 'h': run(); put2(local.tm_hour); break;
            case 'm': run(); put2(local.tm_min); break;
            case 's': run(); put2(local.tm_sec); break;
            default:
                if (!((c == '\'' || c == '"') && i + 1 == timestr.size()))
                    Output(charset[static_cast<uint8_t>(c)]);
                i++;
                break;
        }
    }
}

// __BRK (Winape): ED FF, or the bytes of REDEFINE_BRK
void Engine::BRK()
{
    if (!W(0).t)
    {
        MakeError(idx, "BRK Winape directive does not need parameter");
        return;
    }
    if (winapeBRK.empty())
    {
        Output(0xED);
        Output(0xFF);
        nop += 2;
    }
    else
    {
        for (const int b : winapeBRK)
        {
            Output(static_cast<uint8_t>(b));
            nop++;
        }
    }
}

// __REDEFINE_BRK
void Engine::REDEFINEBRK()
{
    if (!W(0).t)
    {
        winapeBRK.clear();
        do
        {
            int o = 0;
            if (!StringIsQuote(W(1).w) || W(1).w.size() == 3)
                o = RoundComputeExpressionCore(W(1).w, codeadr, 0);
            else
                MakeError(idx, "REDEFINE_BRK needs one or more parameters but NOT strings");
            winapeBRK.push_back(o);
            idx++;
        } while (W(0).t == 0);
    }
    else if (getstruct)
    {
        Output(0);
    }
    else
    {
        MakeError(idx, "REDEFINE_BRK needs one or more parameters");
    }
}

void Engine::LIST()
{
    if (!W(0).t)
        MakeError(idx, "LIST Winape directive does not need parameter");
}

void Engine::NOLIST()
{
    if (!W(0).t)
        MakeError(idx, "NOLIST Winape directive does not need parameter");
}

// ---------------------------------------------------------------------------------------------------------------------
// INCBIN and friends (the preprocessor turned them into HEXBIN #n / ERRRD n)

// __ERRRD: an INCLUDE whose file was not found
void Engine::ERRRD()
{
    if (!W(0).t)
    {
        const int i = std::atoi(W(1).w.c_str());
        idx++;
        if (i >= 0 && i < static_cast<int>(hexbin.size()))
            MakeError(idx, "File to include was not found [" + hexbin[static_cast<size_t>(i)].fileName + "]");
        else
            MakeError(idx, "internal error with text file import (index out of bounds)");
    }
    else
    {
        MakeError(idx, "READ directive need a proper filename as argument");
    }
}

// __HEXBIN
void Engine::HEXBIN()
{
    if (W(0).t)
    {
        MakeError(idx, "INTERNAL - HEXBIN need one HEX parameter and is a RESERVED keyword");
        return;
    }
    const int hb = RoundComputeExpressionCore(W(1).w, codeadr, 0);
    if (hb < 0 || hb >= static_cast<int>(hexbin.size()))
    {
        MakeError(idx, "internal error with binary file import (index out of bounds), please report");
        return;
    }
    const HexBin& cur = hexbin[static_cast<size_t>(hb)];
    IncbinOptions o;
    bool overwritecheck = true, ifExists = false, incwav = false;
    bool hasOffset = false, hasSize = false, hasExtendedOffset = false;
    int maxOpt = 0;
    if (!W(1).t)
    {
        const std::string& k = W(2).w;
        if (k.size() == 3 && std::strchr("SD", k[0]) && k[1] == 'M' && std::strchr("P24A", k[2]))
        {
            incwav = true;
        }
        else
        {
            idx++;
            do
            {
                idx++;
                const std::string w = W(0).w;
                if (w == "EXISTS")
                {
                    ifExists = true;
                }
                else if (w == "SKIPHEADER")
                {
                    o.skipHeader = true;
                }
                else if (w == "REVERT")
                {
                    o.revert = true;
                }
                else if (w == "REPACK")
                {
                    o.repack = true;
                    maxOpt++;
                }
                else if (w == "REMAP")
                {
                    if (!W(0).t)
                    {
                        idx++;
                        const int remap = RoundComputeExpressionCore(W(0).w, codeadr, 0);
                        if (remap < 1)
                        {
                            MakeError(idx, "INCBIN REMAP need a proper number of columns (>1)");
                            return;
                        }
                        o.remap = remap;
                        maxOpt++;
                    }
                    else
                    {
                        MakeError(idx, "INCBIN REMAP need a number of columns for reordering");
                    }
                }
                else if (w == "SLICE" || w == "ISLICE")
                {
                    if (!W(0).t)
                    {
                        idx++;
                        o.sliceWidth = RoundComputeExpressionCore(W(0).w, codeadr, 0);
                        if (!W(0).t)
                        {
                            idx++;
                            o.sliceHeight = RoundComputeExpressionCore(W(0).w, codeadr, 0);
                            o.slice = w[0] == 'I' ? IncbinSlice::ISlice : IncbinSlice::Slice;
                            if (!W(0).t && W(1).w == "MISALIGNED")
                            {
                                idx++;
                                o.misaligned = true;
                            }
                        }
                        else
                        {
                            MakeError(idx, "usage is INCBIN'file',(I)SLICE,width,height");
                        }
                    }
                    else
                    {
                        MakeError(idx, "usage is INCBIN'file',(I)SLICE,width,height");
                    }
                }
                else if (w == "GTILES" || w == "ITILES")
                {
                    if (!W(0).t)
                    {
                        idx++;
                        o.tileWidth = RoundComputeExpressionCore(W(0).w, codeadr, 0);
                        o.tiles = w[0] == 'G' ? IncbinTiles::GTiles : IncbinTiles::ITiles;
                        maxOpt++;
                    }
                    else
                    {
                        MakeError(idx, "usage is INCBIN'file'," + w + ",width");
                    }
                }
                else if (w == "VTILES")
                {
                    if (!W(0).t)
                    {
                        idx++;
                        o.vtiles = RoundComputeExpressionCore(W(0).w, codeadr, 0);
                        if (o.vtiles)
                            maxOpt++;
                    }
                    else
                    {
                        MakeError(idx, "INCBIN VTILES need a number of lines for reordering");
                    }
                }
                else if (w == "OFF")
                {
                    overwritecheck = false;
                }
                else if (w == "ON")
                {
                    overwritecheck = true;
                }
                else
                {
                    if (StringIsQuote(w))
                    {
                        MakeError(idx, "INCBIN has unexpected string parameter [" + w + "]");
                        return;
                    }
                    std::string expwrk = w;
                    const int v = RoundComputeExpressionCore(expwrk, codeadr, 0);
                    if (!hasOffset)
                    {
                        o.offset = v;
                        hasOffset = true;
                    }
                    else if (!hasSize)
                    {
                        o.size = v;
                        hasSize = true;
                    }
                    else if (!hasExtendedOffset)
                    {
                        o.offset += 65536 * v;
                        hasExtendedOffset = true;
                    }
                    else
                    {
                        MakeError(idx, "INCBIN has unexpected integer parameter [" + w + "]");
                        return;
                    }
                }
            } while (!W(0).t);
            if (o.size > 65536)
                MakeError(idx, "INCBIN invalid size, must be lower than 65536");
        }
    }
    else
    {
        idx++;
    }
    if (maxOpt > 1)
    {
        MakeError(idx, "INCBIN options ITLES, GTILES, VTILES, REPACK and REMAP are mutually exclusives");
        return;
    }

    // the file: as the preprocessor resolved it, else its name with the tags translated now, in the include paths
    std::optional<std::vector<uint8_t>> file = fs.read ? fs.read(cur.fileName) : std::nullopt;
    std::string shownName = cur.fileName;
    if (!file && fs.read)
    {
        int touched = 0;
        const std::string translated = TranslateTag(UpperTags(cur.fileName), &touched, kTagRemoveSpace);
        shownName = translated;
        file = fs.read(translated);
        std::vector<std::string> paths = fs.includePaths;
        paths.insert(paths.end(), options.includePaths.begin(), options.includePaths.end());
        for (size_t i = 0; i < paths.size() && !file; i++)
        {
            std::string p = paths[i];
            if (!p.empty() && p.back() != '/' && p.back() != '\\')
                p += '/';
            file = fs.read(p + translated);
        }
    }
    if (!file)
    {
        if (!ifExists)
            MakeError(idx, "file not found [" + shownName + "]");
        return;
    }

    if (incwav)
    {
        const std::string& k = W(2).w;
        WavOptions wo;
        switch (k[2])
        {
            case 'P': wo.mode = WavMode::Smp; break;
            case '2': wo.mode = WavMode::Sm2; break;
            case '4': wo.mode = WavMode::Sm4; break;
            default: wo.mode = WavMode::Dma; break;
        }
        if (wo.mode != WavMode::Dma)
        {
            if (!W(2).t)
                wo.amplification = ComputeExpressionCore(W(3).w, codeadr, 0);
        }
        else if (!W(2).t)
        {
            int a = 2;
            while (!W(a).t)
            {
                a++;
                const std::string opt = W(a).w;
                if (opt == "DMA_INT")
                {
                    wo.dmaInt = true;
                }
                else if (opt == "DMA_REPEAT")
                {
                    if (!W(a).t)
                    {
                        a++;
                        wo.dmaRepeat = static_cast<int>(ComputeExpressionCore(W(a).w, codeadr, 0));
                    }
                    else
                    {
                        MakeError(idx, "DMA_REPEAT must be followed by another parameter");
                    }
                }
                else if (opt == "DMA_CHANNEL_A")
                {
                    wo.channel = DmaChannel::A;
                }
                else if (opt == "DMA_CHANNEL_B")
                {
                    wo.channel = DmaChannel::B;
                }
                else if (opt == "DMA_CHANNEL_C")
                {
                    wo.channel = DmaChannel::C;
                }
                else
                {
                    MakeError(idx, "Unrecognized DMA option [" + opt + "]");
                }
            }
        }
        const DataResult r = ImportWav(*file, wo, audioFilter);
        for (const auto& w : r.warnings)
            MakeWarning(idx, w);
        if (!r.error.empty())
            MakeError(idx, r.error);
        for (const uint8_t b : r.bytes)
            Output(b);
        idx += 2;
        return;
    }

    if (file->empty())
    {
        if (o.size)
            MakeError(idx, "INCBIN cannot read " + std::to_string(o.size) + " byte" + (o.size > 1 ? "s" : "") +
                               " as the file is empty");
        else
            MakeWarning(idx, "INCBIN of an empty file");
        return;
    }
    IncbinResult r = IncbinTransform(*file, o);
    for (const auto& w : r.warnings)
        MakeWarning(idx, w);
    if (!r.error.empty())
    {
        MakeError(idx, r.error);
        if (r.bytes.empty())
            return;
    }
    std::vector<uint8_t> out = std::move(r.bytes);
    if (cur.crunch && r.selectedSize)
    {
        const auto alg = CrunchAlgorithm(cur.crunch, cur.version);
        if (alg)
        {
            const compress::Result c = compress::CrunchRaw(*alg, out.data(), out.size(), cur.minMatch);
            if (!c.error.empty())
                MakeError(idx, c.error);
            out = c.bytes;
            if (out.empty() || static_cast<int>(out.size()) >= r.selectedSize)
                MakeError(idx, "Cannot crunch [" + cur.fileName + "]");
        }
    }
    if (r.alignTo256 && (codeadr & 0xFF))
    {
        while (codeadr & 0xFF)
            Output(0);
        MakeWarning(idx, "INCBIN ISLICE was automatically aligned, use MISALIGNED to force in place");
    }
    if (overwritecheck)
    {
        for (const uint8_t b : out)
            Output(b);
    }
    else
    {
        // rasm's hack to disable the overwrite check: a zone of its own, marked nocode 2
        OrgClose();
        OrgNew(0);
        for (const uint8_t b : out)
            Output(b);
        orgzone.back().nocode = 2;
        OrgClose();
        OrgNew(0);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// output selection

void Engine::AMSDOS() { amsdos = 1; }

// __BUILDZX: 48K snapshot (.sna) written at the end
void Engine::BUILDZX()
{
    if (!W(0).t)
        MakeError(idx, "BUILDZX does not need a parameter");
    if (!forcesnapshot && !forcetape && !forcecpr && !forceROM)
    {
        forcesnapshot = 1;
        forcezx = 1;
    }
    else
    {
        MakeError(idx, "Cannot select ZX output when already in Amstrad ROM/cartridge/snapshot/tape output");
    }
    if (orgDone || codeadr || outputadr)
        MakeWarning(idx, "using BUILDZX whereas there was a previous ORG or code");
}

// __BUILDSNA (CPC snapshot)
void Engine::BUILDSNA()
{
    while (!W(0).t)
    {
        const std::string& k = W(1).w;
        if (k != "V2" && k != "FORCE" && k != "AUTOTEST" && k != "CPR")
        {
            if (StringIsQuote(k))
            {
                const std::string n = Unquote(k);
                const bool ok = n.size() > 4 && n[n.size() - 4] == '.' && std::toupper(static_cast<unsigned char>(n[n.size() - 3])) == 'S' &&
                                std::toupper(static_cast<unsigned char>(n[n.size() - 2])) == 'N' &&
                                std::toupper(static_cast<unsigned char>(n[n.size() - 1])) == 'A';
                if (!ok)
                    MakeWarning(idx, "snapshot filename does not end with .sna extension");
            }
            else
            {
                MakeError(idx, "BUILDSNA unrecognized option [" + k + "]");
            }
        }
        idx++;
    }
    if (orgDone || codeadr || outputadr)
        MakeWarning(idx, "using BUILDSNA whereas there was a previous ORG or code");
    if (!forcecpr && !forcetape && !forcezx && !forceROM)
    {
        forcesnapshot = 1;
        bankRemu = 1;
    }
    else
    {
        MakeError(idx, "Cannot select snapshot output when already in ZX/ROM/cartridge/tape output");
    }
}

// __BUILDCPR (CPC cartridge)
void Engine::BUILDCPR()
{
    while (!W(0).t)
    {
        idx++;
        const std::string& k = W(0).w;
        if (k == "SYMBOL" || k == "SYMBOLS")
        {
        }
        else if (k == "LEGACY")
        {
            extendedCPR |= 2;
        }
        else if (k == "EXTENDED")
        {
            extendedCPR |= 1;
        }
        else if (StringIsQuote(k))
        {
            const std::string n = Unquote(k);
            bool ok = false;
            if (n.size() > 4)
            {
                const size_t e = n.size() - 4;
                const auto up = [&n](size_t i) { return std::toupper(static_cast<unsigned char>(n[i])); };
                ok = n[e] == '.' && (up(e + 1) == 'C' || up(e + 1) == 'X') && up(e + 2) == 'P' && up(e + 3) == 'R';
            }
            if (!ok)
                MakeWarning(idx, "cartridge filename does not end with .cpr or .xpr extension");
        }
        else
        {
            MakeError(idx, "BUILDCPR unknown parameter, may be EXTENDED, LEGACY, SYMBOL(S) or 'filename'");
        }
    }
    if (orgDone || codeadr || outputadr)
        MakeWarning(idx, "using BUILDCPR whereas there was a previous ORG or code");
    if (!forcesnapshot && !forcetape && !forcezx && !forceROM)
        forcecpr = 1;
    else
        MakeError(idx, "Cannot select Amstrad cartridge output when already in ZX/ROM/snapshot/tape output");
}

// __BUILDROM (CPC ROM files)
void Engine::BUILDROM()
{
    if (orgDone || codeadr || outputadr)
        MakeWarning(idx, "using BUILDROM whereas there was a previous ORG or code");
    if (!forcesnapshot && !forcetape && !forcezx && !forcecpr)
    {
        if (!W(0).t && W(1).t)
        {
            if (W(1).w != "CONCAT")
                MakeError(idx, "usage is BUILDROM [CONCAT]");
        }
        forceROM = 1;
    }
    else
    {
        MakeError(idx, "Cannot select ROM output when already in ZX/cartridge/snapshot/tape output");
    }
}

// __BUILDTAPE (CDT / TZX of CPC blocks)
void Engine::BUILDTAPE()
{
    if (!W(0).t)
    {
        idx++;
        if (W(0).t == 1 && StringIsQuote(W(0).w))
        {
            const std::string n = Unquote(W(0).w);
            if (n.size() > 4)
            {
                std::string ext = n.substr(n.size() - 4);
                for (char& c : ext)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                if (ext != ".CDT" && ext != ".TZX")
                    MakeWarning(idx, "tape filename does not end with .cdt or .tzx extension");
            }
        }
        else
        {
            MakeError(idx, "BUILDTAPE optional parameter must be a string");
        }
    }
    if (orgDone || codeadr || outputadr)
        MakeWarning(idx, "using BUILDTAPE whereas there was a previous ORG or code");
    if (!forcesnapshot && !forcecpr && !forcezx && !forceROM)
        forcetape = 1;
    else
        MakeError(idx, "Cannot select tape output when already in ZX/ROM/snapshot/cartridge output");
}

// __BUILDOBJ: object file output (not produced)
void Engine::BUILDOBJ()
{
    NotConverted("BUILDOBJ (object file output)");
    while (!W(0).t)
        idx++;
}

// __NameROM: ROM names of a CPC snapshot
void Engine::NAMEROM()
{
    if (!forcesnapshot)
    {
        MakeError(idx, "NAMEROM can be used only with snapshot output");
        return;
    }
    bankmode = 1;
    NotConverted("NAMEROM (CPC snapshot ROM names)");
    while (!W(0).t)
        idx++;
}

// __DELAYED_COMZ: comments for the ACE emulator's symbol file
void Engine::COMZ()
{
    NotConverted("COMZ (emulator comments)");
    while (W(0).t != 1)
        idx++;
}

// CPC hardware set-up, disk images, relocation, web calls: no effect this port reproduces
void Engine::NotConvertedDirective()
{
    static const std::unordered_map<std::string, std::string> what = {
        {"SNASET", "SNASET (CPC snapshot settings)"},
        {"SNAPINIT", "SNAPINIT (memory loaded from a CPC snapshot)"},
        {"CPRINIT", "CPRINIT (memory loaded from a CPC cartridge)"},
        {"SETCPC", "SETCPC (CPC snapshot model)"},
        {"SETCRTC", "SETCRTC (CPC snapshot CRTC)"},
        {"NAMEBANK", "NAMEBANK (CPC cartridge bank names)"},
        {"LOCALISATION", "LOCALISATION (CPC emulator memory map)"},
        {"BREAKPOINT", "BREAKPOINT (emulator breakpoints)"},
        {"EDSK", "EDSK (CPC disk image)"},
        {"HFE", "HFE (floppy image)"},
        {"API_SEND", "API_SEND (web API call)"},
        {"EXTERNAL", "EXTERNAL (object file symbols)"},
        {"PROCEDURE", "PROCEDURE (object file symbols)"},
        {"RELOCATE", "RELOCATE ... ENDRELOCATE (.rel relocation file)"},
        {"ENDRELOCATE", "RELOCATE ... ENDRELOCATE (.rel relocation file)"},
    };
    const auto it = what.find(W(0).w);
    NotConverted(it != what.end() ? it->second : W(0).w);
    while (!W(0).t)
        idx++;
}

// ---------------------------------------------------------------------------------------------------------------------
// RELOCATE_START / RELOCATE_END / RELOCATE_TABLE: WinAPE / SymbOS inline relocation table

// ___RelocCountAtoms: relocatable labels in the expression (plus `$` inside a block)
int Engine::RelocCountAtoms(const std::string& expr, bool dollarAtom)
{
    int count = dollarAtom ? 1 : 0;
    size_t i = 0;
    while (i < expr.size())
    {
        if (automateValidLabelFirst[static_cast<unsigned char>(expr[i])])
        {
            const size_t start = i++;
            while (i < expr.size() && automateValidLabel[static_cast<unsigned char>(expr[i])])
                i++;
            const Label* l = SearchLabel(expr.substr(start, std::min<size_t>(i - start, 255)));
            if (l && l->relocatable)
                count++;
        }
        else
        {
            i++;
        }
    }
    return count;
}

// ___RelocNetZero: the relocatable atoms cancel out (A-B): no table entry needed
bool Engine::RelocNetZero(const std::string& expr)
{
    int depth = 0;
    int signstack[64];
    int relocstack[64];
    int cursign = 1, netsum = 0, lastreloc = 0;
    signstack[0] = 1;
    relocstack[0] = 0;
    size_t i = 0;
    while (i < expr.size())
    {
        const char c = expr[i];
        if (c == ' ' || c == '\t')
        {
            i++;
            continue;
        }
        if (c == '(')
        {
            if (depth < 63)
            {
                depth++;
                signstack[depth] = signstack[depth - 1] * cursign;
                relocstack[depth] = 0;
            }
            cursign = signstack[depth];
            lastreloc = 0;
            i++;
            continue;
        }
        if (c == ')')
        {
            const int grouphadreloc = relocstack[depth];
            if (depth > 0)
                depth--;
            if (grouphadreloc)
                relocstack[depth] = 1;
            lastreloc = grouphadreloc;
            i++;
            continue;
        }
        if (c == '+' || c == '-')
        {
            cursign = c == '+' ? signstack[depth] : -signstack[depth];
            lastreloc = 0;
            i++;
            continue;
        }
        if (c == '*' || c == '/')
        {
            if (lastreloc)
                return false;
            i++;
            continue;
        }
        if (automateValidLabelFirst[static_cast<unsigned char>(c)])
        {
            const size_t start = i++;
            while (i < expr.size() && automateValidLabel[static_cast<unsigned char>(expr[i])])
                i++;
            const Label* l = SearchLabel(expr.substr(start, std::min<size_t>(i - start, 255)));
            if (l && l->relocatable)
            {
                netsum += cursign;
                relocstack[depth] = 1;
                lastreloc = 1;
            }
            else
            {
                lastreloc = 0;
            }
            continue;
        }
        lastreloc = 0;
        i++;
    }
    return netsum == 0;
}

// ___RelocSetSymbol: RELOCATE_COUNT / RELOCATE_SIZE, labels usable before the table
void Engine::RelocSetSymbol(const std::string& name, int value)
{
    if (Label* l = SearchLabel(name))
    {
        l->ptr = value;
        return;
    }
    Label curlabel;
    curlabel.name = name;
    curlabel.ptr = value;
    curlabel.ibank = activebank;
    curlabel.iorgzone = static_cast<int>(orgzone.size()) - 1;
    curlabel.lz = lz;
    curlabel.fileidx = W(0).file;
    curlabel.fileline = W(0).line;
    curlabel.autoriseExport = autoriseExport != 0;
    curlabel.backidx = static_cast<int>(label.size());
    curlabel.nop = nop;
    InsertLabelToTree(curlabel);
}

// __RELOCATE_START
void Engine::RELOCATESTART()
{
    int ishigh = 0;
    if (!W(0).t)
    {
        if (W(1).w == "HIGH")
        {
            ishigh = 1;
            idx++;
        }
        else
        {
            MakeError(idx, "usage is RELOCATE_START [HIGH]");
            return;
        }
    }
    if (relocblockactive)
    {
        const Word& b = wl[static_cast<size_t>(relocblockidx)];
        const std::string f = b.file >= 0 && b.file < static_cast<int>(filename.size()) ? filename[static_cast<size_t>(b.file)] : "";
        MakeError(idx, "RELOCATE_START: a relocation block is already open (see RELOCATE_START in [" + f + ":" + std::to_string(b.line) + "])");
        return;
    }
    if (lz >= 0)
    {
        MakeError(idx, "RELOCATE_START must not be used inside a crunched section");
        return;
    }
    if (!repeat.empty() || !whilewend.empty())
    {
        MakeError(idx, "RELOCATE_START must not be used inside a loop section");
        return;
    }
    if (nocode)
    {
        MakeError(idx, "RELOCATE_START must not be used inside a NOCODE section");
        return;
    }
    if (relocbankset && relocbank != activebank)
    {
        MakeError(idx, "RELOCATE_START must stay in the same BANK for every block feeding the same RELOCATE_TABLE");
        return;
    }
    if (relocmode && relocmode != (ishigh ? 2 : 1))
    {
        MakeError(idx, "RELOCATE_START: HIGH mode cannot be mixed with regular mode in the same file");
        return;
    }
    relocmode = relocblockactive = ishigh ? 2 : 1;
    relocblockidx = idx;
    if (!relocbankset)
    {
        relocbank = activebank;
        relocbankset = true;
    }
}

// __RELOCATE_END
void Engine::RELOCATEEND()
{
    if (W(0).t)
    {
        if (!relocblockactive)
        {
            MakeError(idx, "RELOCATE_END: no RELOCATE_START block is open");
            return;
        }
        relocblockactive = 0;
    }
    else
    {
        MakeError(idx, "RELOCATE_END directive must be used without parameter");
    }
}

// __RELOCATE_TABLE: the positions of the 16-bit values pointing into the blocks, written in place
void Engine::RELOCATETABLE()
{
    int subtractOffset = 0;
    const bool ishigh = relocmode == 2;
    if (relocblockactive)
    {
        MakeError(idx, "RELOCATE_TABLE: a RELOCATE_START block is still open (missing RELOCATE_END)");
        return;
    }
    if (!W(0).t)
    {
        idx++;
        subtractOffset = RoundComputeExpression(W(0).w, outputadr, 0, 0);
    }
    const int targetbank = relocbankset ? relocbank : activebank;
    std::vector<int> addrs;
    for (const Expression& x : expression)
    {
        if (x.ibank != targetbank || x.lz >= 0)
            continue;
        switch (x.kind)
        {
            case ExprKind::V16At0:
            case ExprKind::V16:
            case ExprKind::J16:
            case ExprKind::IV16: break;
            default: continue;
        }
        const std::string& text = x.reference ? *x.reference : wl[static_cast<size_t>(x.iw)].w;
        const int atoms = RelocCountAtoms(text, x.relocblock && x.relocdollar);
        if (atoms == 1)
            addrs.push_back(x.wcodeadr + (ishigh ? 1 : 0) - subtractOffset);
        else if (atoms >= 2 && !RelocNetZero(text))
            MakeError(x.iw, "RELOCATE_TABLE: expression [" + text +
                                "] combines relocatable addresses in a way that cannot be relocated correctly (would need more than one fixup delta)");
    }
    for (const int a : addrs)
    {
        Output(static_cast<uint8_t>(a & 0xFF));
        Output(static_cast<uint8_t>((a >> 8) & 0xFF));
    }
    const int count = static_cast<int>(addrs.size());
    RelocSetSymbol("RELOCATE_COUNT", count);
    RelocSetSymbol("RELOCATE_SIZE", count * 2);
}

// ---------------------------------------------------------------------------------------------------------------------
// the directive table (rasm's instruction[] without the Z80 mnemonics)

Engine::Directive Engine::FindDirective(const std::string& name) const
{
    static const std::unordered_map<std::string, Directive> table = {
        {"REPEAT", &Engine::REPEAT}, {"STARTINGINDEX", &Engine::STARTINGINDEX}, {"REND", &Engine::REND},
        {"UNTIL", &Engine::UNTIL}, {"ORG", &Engine::ORG}, {"PROTECT", &Engine::PROTECT}, {"WHILE", &Engine::WHILE},
        {"WEND", &Engine::WEND}, {"INCLUDE", &Engine::ERRRD}, {"ERRRD", &Engine::ERRRD}, {"HEXBIN", &Engine::HEXBIN},
        {"ALIGN", &Engine::ALIGN}, {"CONFINE", &Engine::CONFINE}, {"ELSEIFNOT", &Engine::ELSEIFNOT},
        {"ELSEIF", &Engine::ELSEIF}, {"ELSE", &Engine::ELSE}, {"IF", &Engine::IF}, {"ENDIF", &Engine::ENDIF},
        {"IFNOT", &Engine::IFNOT}, {"IFDEF", &Engine::IFDEF}, {"IFNDEF", &Engine::IFNDEF}, {"IFUSED", &Engine::IFUSED},
        {"IFNUSED", &Engine::IFNUSED}, {"UNDEF", &Engine::UNDEF}, {"CASE", &Engine::CASE}, {"BREAK", &Engine::BREAK},
        {"DEFAULT", &Engine::DEFAULT}, {"SWITCH", &Engine::SWITCH}, {"ENDSWITCH", &Engine::ENDSWITCH},
        {"CODE", &Engine::CODE}, {"NOCODE", &Engine::NOCODE}, {"MACRO", &Engine::MACRO}, {"ENUM", &Engine::ENUM},
        {"TICKER", &Engine::TICKER}, {"LET", &Engine::LET}, {"ASSERT", &Engine::ASSERT}, {"CHARSET", &Engine::CHARSET},
        {"UTF8REMAP", &Engine::UTF8REMAP}, {"RUN", &Engine::RUN}, {"SAVE", &Engine::SAVE},
        {"REDEFINE_BRK", &Engine::REDEFINEBRK}, {"BRK", &Engine::BRK}, {"NOLIST", &Engine::NOLIST},
        {"LIST", &Engine::LIST}, {"STOP", &Engine::STOP}, {"PRINT", &Engine::PRINT}, {"COMZ", &Engine::COMZ},
        {"DELAYED_PRINT", &Engine::DELAYEDPRINT}, {"FAIL", &Engine::FailDirective}, {"BREAKPOINT", &Engine::NotConvertedDirective},
        {"ROMBANK", &Engine::ROMBANK}, {"BANK", &Engine::BANK}, {"BANKSET", &Engine::BANKSET},
        {"NAMEBANK", &Engine::NotConvertedDirective}, {"NAMEROM", &Engine::NAMEROM}, {"LIMIT", &Engine::LIMIT},
        {"LZEXO", &Engine::LZEXO}, {"LZX7", &Engine::LZX7}, {"LZX0", &Engine::LZX0}, {"LZX0B", &Engine::LZX0B},
        {"LZAPU", &Engine::LZAPU}, {"LZSA1", &Engine::LZSA1}, {"LZSA2", &Engine::LZSA2}, {"LZ4", &Engine::LZ4},
        {"LZ48", &Engine::LZ48}, {"LZ49", &Engine::LZ49}, {"LZCLOSE", &Engine::LZCLOSE},
        {"SNASET", &Engine::NotConvertedDirective}, {"SNAPINIT", &Engine::NotConvertedDirective},
        {"CPRINIT", &Engine::NotConvertedDirective}, {"BUILDZX", &Engine::BUILDZX}, {"BUILDOBJ", &Engine::BUILDOBJ},
        {"BUILDCPR", &Engine::BUILDCPR}, {"BUILDSNA", &Engine::BUILDSNA}, {"BUILDROM", &Engine::BUILDROM},
        {"BUILDTAPE", &Engine::BUILDTAPE}, {"SETCPC", &Engine::NotConvertedDirective},
        {"SETCRTC", &Engine::NotConvertedDirective}, {"AMSDOS", &Engine::AMSDOS}, {"STRUCT", &Engine::STRUCT},
        {"ENDSTRUCT", &Engine::ENDSTRUCT}, {"ENDS", &Engine::ENDSTRUCT}, {"LABEL", &Engine::LABEL},
        {"NOEXPORT", &Engine::NOEXPORT}, {"ENOEXPORT", &Engine::ENOEXPORT}, {"MODULE", &Engine::MODULE},
        {"ENDMODULE", &Engine::ENDMODULE}, {"TIMESTAMP", &Engine::TIMESTAMP}, {"SUMMEM", &Engine::SUMMEM},
        {"SUM16", &Engine::SUM16}, {"XORMEM", &Engine::XORMEM}, {"CIPHERMEM", &Engine::CIPHERMEM},
        {"EXTERNAL", &Engine::NotConvertedDirective}, {"PROCEDURE", &Engine::NotConvertedDirective},
        {"REBANK", &Engine::REBANK}, {"LOCALISATION", &Engine::NotConvertedDirective},
        {"RELOCATE", &Engine::NotConvertedDirective}, {"ENDRELOCATE", &Engine::NotConvertedDirective},
        {"RELOCATE_START", &Engine::RELOCATESTART}, {"RELOCATE_END", &Engine::RELOCATEEND},
        {"RELOCATE_TABLE", &Engine::RELOCATETABLE}, {"EDSK", &Engine::NotConvertedDirective},
        {"HFE", &Engine::NotConvertedDirective}, {"API_SEND", &Engine::NotConvertedDirective},
        // data
        {"DF24", &Engine::DF24}, {"DEFF", &Engine::DEFF}, {"DEFR", &Engine::DEFR}, {"DEFB", &Engine::DEFB},
        {"DEFM", &Engine::DEFB}, {"DF", &Engine::DEFF}, {"DR", &Engine::DEFR}, {"DM", &Engine::DEFB},
        {"DB", &Engine::DEFB}, {"DEFW", &Engine::DEFW}, {"DW", &Engine::DEFW}, {"DEFS", &Engine::DEFS},
        {"DS", &Engine::DEFS}, {"STR", &Engine::STR}, {"DEFI", &Engine::DEFI},
    };
    const auto it = table.find(name);
    return it != table.end() ? it->second : nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// symbols

bool Engine::InsertAlias(const Alias& a)
{
    if (aliasTree.count(a.alias))
        return false;
    aliasTree.emplace(a.alias, alias.size());
    alias.push_back(a);
    return true;
}

// DelDico (UNDEF)
void Engine::DelDico(const std::string& name)
{
    if (dicoTree.erase(name))
    {
        for (auto it = dicoOrder.begin(); it != dicoOrder.end(); ++it)
        {
            if (*it == name)
            {
                dicoOrder.erase(it);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// end of the assembly: the files rasm writes

void Engine::BuildOutputs()
{
    binaryOutput = save.empty() && !forcecpr && !forcesnapshot && !forceROM && !forcetape;
    PopAllSave();
    if (forcezx)
    {
        ZxSnapshotInput in;
        in.bank0 = mem[0];
        in.bank2 = mem[2];
        in.bank5 = mem[5];
        in.bankset0 = bankset[0] != 0;
        in.run = static_cast<unsigned>(zxsnapshot.run);
        in.stack = static_cast<unsigned>(zxsnapshot.stack);
        outputFiles.push_back({"", BuildZxSnapshot(in), -1, 0, 0, SaveFormat::Plain});
    }
    else if (forcesnapshot)
    {
        NotConverted("CPC snapshot (BUILDSNA / BANKSET)");
    }
    if (forcecpr)
        NotConverted("CPC cartridge (BUILDCPR, or BANK without a BUILD directive)");
    if (forceROM)
        NotConverted("CPC ROM files (BUILDROM)");
    if (forcetape)
        NotConverted("CDT tape (BUILDTAPE)");
}
}  // namespace unrealasm::rasm
