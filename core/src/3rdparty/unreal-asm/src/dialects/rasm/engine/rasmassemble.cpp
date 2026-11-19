// rasm's assembler loop (rasm.c v3.3.3: Assemble, ___output, ___org_new / ___org_close, ___new_memory_space,
// OverWriteCheck, __MACRO_EXECUTE, the crunch phase) and the bridge to the instruction encoder.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "dialects/rasm/engine/rasmengine.h"
#include "dialects/rasm/engine/rasminstructions.h"
#include "unrealasm/compress.h"

namespace unrealasm::rasm
{
namespace
{
std::string ReplaceAll(std::string s, const std::string& from, const std::string& to)
{
    if (s.empty() || from.empty())
        return s;
    size_t pos = s.find(from);
    while (pos != std::string::npos)
    {
        s.replace(pos, from.size(), to);
        pos = s.find(from, pos + to.size());
    }
    return s;
}

ExprKind KindOf(FixupKind k)
{
    switch (k)
    {
        case FixupKind::J8: return ExprKind::J8;
        case FixupKind::V8At0: return ExprKind::V8At0;
        case FixupKind::V8: return ExprKind::V8;
        case FixupKind::J16: return ExprKind::J16;
        case FixupKind::V16: return ExprKind::V16;
        case FixupKind::V16At0: return ExprKind::V16At0;
        case FixupKind::V32At0: return ExprKind::V32At0;
        case FixupKind::RealAmsdos: return ExprKind::RealAmsdos;
        case FixupKind::RealMicrosoft: return ExprKind::RealMicrosoft;
        case FixupKind::Float24: return ExprKind::Float24;
        case FixupKind::IV8: return ExprKind::IV8;
        case FixupKind::IV81: return ExprKind::IV81;
        case FixupKind::V8At3: return ExprKind::V8At3;
        case FixupKind::IV16: return ExprKind::IV16;
        case FixupKind::Rst: return ExprKind::Rst;
        case FixupKind::RstConditional: return ExprKind::RstConditional;
        case FixupKind::Im: return ExprKind::Im;
        case FixupKind::BitResSet: return ExprKind::BitResSet;
    }
    return ExprKind::V8;
}

int SizeOf(ExprKind k)
{
    switch (k)
    {
        case ExprKind::V16: case ExprKind::J16: case ExprKind::V16At0: case ExprKind::IV16: return 2;
        case ExprKind::V32At0: return 4;
        case ExprKind::RealAmsdos: case ExprKind::RealMicrosoft: return 5;
        case ExprKind::Float24: return 3;
        case ExprKind::BitResSet: return 0;
        default: return 1;
    }
}

void SetChars(std::array<char, 256>& a, const char* chars)
{
    for (const char* p = chars; *p; p++)
        a[static_cast<uint8_t>(*p)] = 1;
}
}  // namespace

Engine::Engine(const FileSystem& fileSystem, const EngineOptions& engineOptions) : fs(fileSystem), options(engineOptions)
{
    maxam = options.maxam ? 1 : 0;
    as80 = options.as80 ? 1 : 0;
    maxamAs80 = maxam || as80;
    rough = maxam ? 0.0f : 0.5f;
    nowarning = options.noWarning ? 1 : 0;

    SetChars(automateExpressionValidCharExtended, "0123456789.ABCDEFGHIJKLMNOPQRSTUVWXYZ_{}@+-*/~^$#%<=>|&\\");
    SetChars(automateExpressionValidCharFirst, "#%0123456789.ABCDEFGHIJKLMNOPQRSTUVWXYZ_@${");
    SetChars(automateExpressionValidChar, "0123456789.ABCDEFGHIJKLMNOPQRSTUVWXYZ_{}@$");
    SetChars(automateValidLabelFirst, ".ABCDEFGHIJKLMNOPQRSTUVWXYZ_@");
    SetChars(automateValidVarFirst, "ABCDEFGHIJKLMNOPQRSTUVWXYZ_");
    SetChars(automateValidLabel, "0123456789.ABCDEFGHIJKLMNOPQRSTUVWXYZ_@{}");
    SetChars(automateDigit, ".0123456789");
    SetChars(automateHexa, "0123456789ABCDEF");
    automateExpressionDecision['<'] = '<';
    automateExpressionDecision['>'] = '>';
    automateExpressionDecision['='] = '=';
    automateExpressionDecision['!'] = '!';
    automateExpressionDecision['~'] = '~';
    automateExpressionDecision[0] = 'E';

    struct OpDef
    {
        char c;
        Op op;
        int rasmPriority;
        int maxamPriority;
    };
    static const OpDef ops[] = {
        {'(', Op::Open, 0, 0},       {')', Op::Close, 0, 0},     {',', Op::Comma, 0, 0},     {'b', Op::Not, 1, 128},
        {'*', Op::Mul, 2, 464},      {'/', Op::Div, 2, 464},     {'\\', Op::IntDiv, 2, 464}, {'m', Op::Mod, 2, 464},
        {'+', Op::Add, 3, 464},      {'-', Op::Sub, 3, 464},     {'[', Op::Shl, 4, 464},     {']', Op::Shr, 4, 464},
        {'l', Op::Lower, 5, 664},    {'g', Op::Greater, 5, 664}, {'e', Op::Equal, 5, 664},   {'n', Op::NotEqual, 5, 664},
        {'k', Op::LowerEq, 5, 664},  {'h', Op::GreaterEq, 5, 664}, {'&', Op::And, 6, 464},   {'^', Op::Xor, 7, 464},
        {'|', Op::Or, 8, 464},       {'a', Op::BAnd, 9, 6128},   {'o', Op::BOr, 10, 6128},
    };
    for (auto& e : automateElement)
        e.op = Op::End;
    for (const auto& d : ops)
    {
        auto& e = automateElement[static_cast<uint8_t>(d.c)];
        e.op = d.op;
        e.priority = maxam ? d.maxamPriority : d.rasmPriority;
    }

    for (int i = 0; i < 256; i++)
        charset[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    {
        static const int counts[] = {1, 1, 1, 2, 3, 4, 5, 6, 9, 18, 20, 24, 30, 34, 48, 50};
        size_t j = 0;
        for (int level = 0; level < 16; level++)
            for (int k = 0; k < counts[level]; k++)
                psgfine[j++] = static_cast<uint8_t>(level);
    }
    for (int i = 0; i < 4; i++)
    {
        bankgate[static_cast<size_t>(i)] = 0x7FC0;
        setgate[static_cast<size_t>(i)] = 0x7FC0;
    }
    for (int i = 0; i < 256; i++)
    {
        bankgate[static_cast<size_t>(i + 4)] = 0x7FC4 + (i & 3) + ((i & 31) >> 2) * 8 - 0x100 * (i >> 5);
        setgate[static_cast<size_t>(i + 4)] = 0x7FC2 + ((i & 31) >> 2) * 8 - 0x100 * (i >> 5);
    }
    // rasm allocates the banks on first use (BANK, BANKSET); the default workspace is bank 260
    mem.assign(kBankMaxNumber + 1, std::vector<uint8_t>());
    mem[kBankMaxNumber].assign(65536, 0);
    memsize.assign(kBankMaxNumber + 1, 65536);
    activebank = kBankMaxNumber;
    maxptr = memsize[static_cast<size_t>(activebank)];

    autoriseExport = 1;
    ExpressionSetDicoVar("PI", 3.1415926545);
    ExpressionSetDicoVar("ASSEMBLER_RASM", 1);
    for (const auto& d : options.defines)
        ExpressionSetDicoVar(d.first, d.second);
    expression.emplace_back();   // the fictive expression 0

    lz = -1;
    OrgZone zone;
    zone.ibank = kBankMaxNumber;
    zone.inplace = 1;
    orgzone.push_back(zone);
}

// ---------------------------------------------------------------------------------------------------------------------
// output

void Engine::Output(uint8_t v)
{
    switch (outputMode)
    {
        case OutputMode::Disabled:
            return;
        case OutputMode::NoCode:
            if (outputadr < maxptr)
            {
                if (getstruct)
                {
                    auto& fields = rasmstruct.back().fields;
                    if (!fields.empty())
                        fields.back().data.push_back(v);
                    else
                        MakeWarning(idx, "Structure field has no reference, did you forget a label?");
                }
                outputadr++;
                codeadr++;
            }
            else
            {
                MakeError(idx, "output exceed limit " + std::to_string(maxptr));
                stop = 1;
                outputMode = OutputMode::Disabled;
            }
            return;
        case OutputMode::Normal:
            break;
    }
    if (outputadr < maxptr)
    {
        TraceBytes(outputadr, 1, codeadr);
        mem[static_cast<size_t>(activebank)][static_cast<size_t>(outputadr++)] = v;
        codeadr++;
        return;
    }
    bool iscrunched = false;
    for (int i = static_cast<int>(lzsection.size()) - 1; i >= 0; i--)
    {
        if (lzsection[static_cast<size_t>(i)].ibank == activebank)
        {
            iscrunched = true;
            break;
        }
    }
    if (!iscrunched && !extended)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "output exceed limit %04X", static_cast<unsigned>(maxptr));
        MakeError(idx, buf);
        stop = 1;
        outputMode = OutputMode::Disabled;
        return;
    }
    if (iscrunched && (maxptr & 0xFFFF))
        MakeWarning(idx, "Specific limits are not applied  when using crunched sections, cause memory blocks are moved unpredictably");
    const int requestedBlock = outputadr >> 16;
    maxptr = (requestedBlock + 1) * 65536;
    if (maxptr < extendedLimit)
        maxptr = extendedLimit;
    mem[static_cast<size_t>(activebank)].resize(static_cast<size_t>(maxptr), 0);
    mem[static_cast<size_t>(activebank)][static_cast<size_t>(outputadr++)] = v;
    codeadr++;
    if (!iscrunched)
        MakeWarning(idx, "physical output overflow 64k");
}

void Engine::OutputSetLimit(int zelimit, int ext)
{
    int limit = 65536;
    if (zelimit <= limit || ext)
    {
        limit = zelimit;
        extended = ext ? 1 : 0;
        if (ext)
        {
            limit = 65536;
            extendedLimit = zelimit;
            orgzone.back().extended = 1;
        }
    }
    else
    {
        MakeError(idx, "limit exceed hardware limitation! Use LIMIT <limit>,EXTENDED to bypass 64K");
        stop = 1;
    }
    if (outputadr >= 0 && outputadr > limit)
    {
        MakeError(idx, "limit too high for current output!");
        stop = 1;
    }
    maxptr = limit;
}

// ---------------------------------------------------------------------------------------------------------------------
// ORG zones, memory spaces

void Engine::OverWriteCheck()
{
    const size_t i = orgzone.size() - 1;
    const OrgZone& a = orgzone[i];
    if (a.memstart == a.memend)
        return;
    for (size_t j = 0; j + 1 < orgzone.size(); j++)
    {
        const OrgZone& b = orgzone[j];
        if (b.memstart == b.memend || b.nocode || a.ibank != b.ibank)
            continue;
        if ((a.memstart >= b.memstart && a.memstart < b.memend) || (a.memend > b.memstart && a.memend < b.memend) ||
            (a.memstart <= b.memstart && a.memend >= b.memend))
        {
            idx--;
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s L%d [#%04X-#%04X-B%d] with L%d [#%04X/#%04X]",
                          b.protect ? "PROTECTED section error" : "Assembling overwrite", b.iline, b.memstart, b.memend,
                          b.ibank, a.iline, a.memstart, a.memend);
            MakeError(idx, buf);
            return;
        }
    }
}

void Engine::OrgClose()
{
    if (lz >= 0 && lzsection[static_cast<size_t>(lzsection.size() - 1)].lzversion)
    {
        MakeError(idx, "Cannot ORG inside a LZ section");
        return;
    }
    lz = -1;
    if (!orgzone.empty())
        orgzone.back().memend = outputadr;
}

void Engine::OrgNew(int nocodeFlag)
{
    for (const auto& z : orgzone)
    {
        if (z.memstart != z.memend && !z.nocode && z.ibank == activebank && outputadr < z.memend && outputadr >= z.memstart)
        {
            char buf[160];
            if (z.protect)
                std::snprintf(buf, sizeof(buf), "ORG located a PROTECTED section [#%04X-#%04X-B%d] line %d", z.memstart,
                              z.memend, z.ibank, z.iline);
            else
                std::snprintf(buf, sizeof(buf), "ORG (output at #%04X) located in a previous ORG section [#%04X-#%04X-B%d] line %d",
                              outputadr, z.memstart, z.memend, z.ibank, z.iline);
            MakeError(idx, buf);
        }
    }
    OverWriteCheck();
    if (lz >= 0)
        lz = -1;
    OrgZone zone;
    zone.extended = extended;
    zone.memstart = outputadr;
    zone.ibank = activebank;
    zone.ifile = wl[static_cast<size_t>(idx)].file;
    zone.iline = wl[static_cast<size_t>(idx)].line;
    zone.nocode = nocode = nocodeFlag;
    outputMode = nocodeFlag ? OutputMode::NoCode : OutputMode::Normal;
    orgzone.push_back(zone);
}

void Engine::NewMemorySpace()
{
    lz = -1;
    if (!orgzone.empty())
        orgzone.back().memend = outputadr;
    if (lz >= 0)
    {
        MakeWarning(idx, "LZ section wasn't closed before a new memory space directive");
        LZCLOSE();
    }
    lastbank = activebank;
    mem.emplace_back(65536, 0);
    memsize.push_back(65536);
    activebank = static_cast<int>(mem.size()) - 1;
    maxptr = memsize[static_cast<size_t>(activebank)];
    OrgZone zone;
    zone.extended = extended;
    outputadr = 0;
    codeadr = 0;
    zone.memstart = 0;
    zone.ibank = activebank;
    zone.nocode = nocode = 0;
    zone.inplace = 1;
    orgzone.push_back(zone);
    OverWriteCheck();
}

void Engine::GetBackToMemorySpace()
{
    int old = 0;
    for (int i = static_cast<int>(orgzone.size()) - 1; i >= 0; i--)
    {
        if (orgzone[static_cast<size_t>(i)].ibank == activebank)
        {
            old = orgzone[static_cast<size_t>(i)].memend;
            break;
        }
    }
    outputadr = old;
    codeadr = old;
    OrgZone zone;
    zone.memstart = outputadr;
    zone.ibank = activebank;
    zone.nocode = nocode = 0;
    zone.extended = extended;
    orgzone.push_back(zone);
    OverWriteCheck();
}

void Engine::PushGlobal()
{
    globalLabelStack.push_back({lastgloballabel});
}

void Engine::PopGlobal()
{
    if (globalLabelStack.empty())
        return;   // rasm reads below its stack here (unbalanced loops already reported)
    lastgloballabel = globalLabelStack.back().label;
    globalLabelStack.pop_back();
}

// ---------------------------------------------------------------------------------------------------------------------
// macros

Macro* Engine::SearchMacro(const std::string& name)
{
    auto it = macroIndex.find(name);
    return it == macroIndex.end() ? nullptr : &macro[it->second];
}

void Engine::MacroExecute(size_t imacroIndex)
{
    const Macro& m = macro[imacroIndex];
    int nbparam = 0;
    size_t i = static_cast<size_t>(idx);
    while (!wl[i].t)
    {
        nbparam++;
        i++;
    }
    if (options.prepro.macroMultiLine)
    {
        while (nbparam < static_cast<int>(m.param.size()) && wl[i].t != 2)
        {
            wl[i++].t = 0;
            nbparam++;
        }
        if (!wl[i].t)
        {
            MakeError(idx, "MACRO (multi-line mode) [" + m.mnemo + "] was defined with " + std::to_string(m.param.size()) +
                               " parameter(s)");
            while (!wl[static_cast<size_t>(idx)].t)
                idx++;
            idx++;
            macrocounter++;
            return;
        }
    }
    int reload = 0;
    const size_t call = static_cast<size_t>(idx);
    if (m.param.empty())
    {
        if (nbparam)
        {
            if (nbparam == 1 && wl[call + 1].w == "(VOID)")
            {
                nbparam = 0;
                reload = 1;
            }
        }
    }
    auto skipCall = [&] {
        while (!wl[static_cast<size_t>(idx)].t)
            idx++;
        idx++;
    };
    if (!m.param.empty() && wl[call + 1].w == "(VOID)")
    {
        MakeError(idx, "MACRO [" + m.mnemo + "] has " + std::to_string(m.param.size()) + " parameter(s)");
        skipCall();
        macrocounter++;
        return;
    }
    if (!m.multiArg && nbparam != static_cast<int>(m.param.size()))
    {
        std::string list;
        for (const auto& p : m.param)
            list += p + " ";
        MakeError(idx, "MACRO [" + m.mnemo + "] was defined with " + std::to_string(m.param.size()) + " parameter" +
                           (m.param.size() > 1 ? "s " : " ") + list);
        skipCall();
        macrocounter++;
        return;
    }
    if (m.multiArg && nbparam > m.maxArg && nbparam > static_cast<int>(m.param.size()))
        MakeWarning(idx, "MACRO [" + m.mnemo + "] has more parameters than used");

    for (int p = 0; p < nbparam; p++)
    {
        std::string& w = wl[call + 1 + static_cast<size_t>(p)].w;
        if (w.compare(0, 6, "{EVAL}") == 0)
        {
            std::string zeparam = w.substr(6);
            ExpressionFastTranslate(zeparam, 1);
            const double v = ComputeExpressionCore(zeparam, codeadr, 0);
            char buf[64];
            std::snprintf(buf, 31, "%lf", v);
            buf[31] = 0;
            w = buf;
        }
    }
    std::vector<std::string> cpybackup;
    for (int p = 0; p < nbparam; p++)
        cpybackup.push_back(wl[call + 1 + static_cast<size_t>(p)].w);

    MacroPosition pos;
    pos.start = idx;
    pos.end = idx + static_cast<int>(m.wc.size());
    pos.value = macrocounter;
    pos.pushed = 0;
    if (!imacropos)
        pos.level = 1;
    else if (macropos[static_cast<size_t>(imacropos - 1)].end <= pos.start)
        pos.level = macropos[static_cast<size_t>(imacropos - 1)].level;
    else
        pos.level = macropos[static_cast<size_t>(imacropos - 1)].level + 1;
    // rasm's imacropos is the logical length of the array: loops shrink it back without clearing the entries
    if (static_cast<size_t>(imacropos) < macropos.size())
        macropos[static_cast<size_t>(imacropos)] = pos;
    else
        macropos.push_back(pos);
    imacropos++;
    for (auto& r : repeat)
        if (r.maxim < imacropos)
            r.maxim = imacropos;
    for (auto& w : whilewend)
        if (w.maxim < imacropos)
            w.maxim = imacropos;
    const int grow = static_cast<int>(m.wc.size()) - 1 - nbparam - reload;
    for (int dad = 0; dad < imacropos - 1; dad++)
        if (macropos[static_cast<size_t>(dad)].end > pos.start)
            macropos[static_cast<size_t>(dad)].end += grow;

    const int iline = wl[call].line;
    const int ifile = wl[call].file;
    std::vector<Word> body = m.wc;
    for (auto& w : body)
    {
        w.line = iline;
        w.file = ifile;
    }
    // the call (name, parameters, VOID) is replaced by the body
    const auto first = wl.begin() + static_cast<std::ptrdiff_t>(call);
    wl.erase(first, first + 1 + nbparam + reload);
    wl.insert(wl.begin() + static_cast<std::ptrdiff_t>(call), body.begin(), body.end());

    for (int replaceIdx : m.replaceIdx)
    {
        std::string& w = wl[call + static_cast<size_t>(replaceIdx)].w;
        if (m.multiArg)
        {
            const size_t brace = w.find('{');
            if (brace != std::string::npos)
            {
                const char c = brace + 1 < w.size() ? w[brace + 1] : '\0';
                if (c == '#')
                {
                    if (brace + 2 < w.size() && w[brace + 2] == '}')
                        w = ReplaceAll(w, "{#}", std::to_string(nbparam));
                }
                else if (c >= '0' && c <= '9')
                {
                    const size_t startChr = brace + 1;
                    size_t pidx = 1;
                    while (startChr + pidx < w.size() && std::isdigit(static_cast<unsigned char>(w[startChr + pidx])))
                        pidx++;
                    const char after = startChr + pidx < w.size() ? w[startChr + pidx] : '\0';
                    const int ptoCopy = std::atoi(w.c_str() + startChr);
                    if (after == '}')
                    {
                        if (ptoCopy < nbparam)
                            w = ReplaceAll(w, "{" + std::to_string(ptoCopy) + "}", cpybackup[static_cast<size_t>(ptoCopy)]);
                        else
                            MakeError(static_cast<int>(call) + replaceIdx,
                                      "cannot map param " + std::to_string(ptoCopy) + " as there was only " +
                                          std::to_string(nbparam) + " in this MACRO call");
                    }
                    else if (after == '|' && startChr + pidx + 1 < w.size() && w[startChr + pidx + 1] == '=')
                    {
                        size_t icp = 0;
                        while (startChr + icp < w.size() && w[startChr + icp] != '}')
                            icp++;
                        icp += 3;
                        const std::string fullParam = w.substr(brace, icp - 1);
                        if (ptoCopy < nbparam)
                        {
                            w = ReplaceAll(w, fullParam, cpybackup[static_cast<size_t>(ptoCopy)]);
                        }
                        else
                        {
                            std::string endParam = fullParam.substr(fullParam.find('=') + 1);
                            if (!endParam.empty())
                                endParam.pop_back();
                            w = ReplaceAll(w, fullParam, endParam);
                        }
                    }
                    else
                    {
                        MakeError(static_cast<int>(call) + replaceIdx,
                                  std::string("Looks like there is a wrong tag inside macro, expecting endmark '}' or ':=', not '") +
                                      after + "'");
                    }
                }
            }
            for (size_t p = 0; p < m.param.size(); p++)
                w = ReplaceAll(w, m.param[p], p < cpybackup.size() ? cpybackup[p] : std::string());
        }
        else
        {
            for (int p = 0; p < nbparam; p++)
                w = ReplaceAll(w, m.param[static_cast<size_t>(p)], cpybackup[static_cast<size_t>(p)]);
        }
    }
    // rasm's AF.HIGH / BC.LOW ... tags never fire (their CRC constants are built from "AF_HIGH"): not replaced
    macrocounter++;
}

// ---------------------------------------------------------------------------------------------------------------------
// instructions (the encoder) and their deferred operands

void Engine::PushFixup(const Fixup& fixup, int dollar)
{
    const ExprKind kind = KindOf(fixup.kind);
    const int size = SizeOf(kind);
    if (nocode)
    {
        outputadr += size;
        codeadr += size;
        if (outputadr > maxptr)
        {
            if (!stop)
                MakeError(idx, "(PushExpr) NOCODE output exceed limit");
            stop = 1;
        }
        return;
    }
    Expression curexp;
    curexp.iw = idx;
    curexp.wptr = outputadr;
    curexp.kind = kind;
    curexp.ibank = activebank;
    curexp.iorgzone = static_cast<int>(orgzone.size()) - 1;
    curexp.lz = lz;
    curexp.wcodeadr = codeadr;
    curexp.relocblock = relocblockactive != 0;
    curexp.relocdollar = fixup.expression.find('$') != std::string::npos;
    if (hasModule)
        curexp.module = module;
    const std::string& w = fixup.expression;
    if ((kind == ExprKind::J8 || kind == ExprKind::J16) && w.size() == 2 && w[0] == '_' && (w[1] == '+' || w[1] == '-'))
    {
        if (w[1] == '+')
        {
            curexp.reference = "LPR" + std::to_string(curProxIndex + 1) + "OX";
        }
        else if (curProxIndex < 0)
        {
            MakeError(idx, "You cannot use _- before declaring _ proximity label");
            curexp.reference = "LPR0OX";
        }
        else
        {
            curexp.reference = "LPR" + std::to_string(curProxIndex) + "OX";
        }
    }
    else
    {
        const int saved = codeadr;
        codeadr = dollar;
        curexp.reference = w;
        ExpressionFastTranslate(*curexp.reference, 1);
        codeadr = saved;
    }
    curexp.ptr = dollar;
    TraceBytes(outputadr, size, codeadr);
    outputadr += size;
    codeadr += size;
    if (outputadr > maxptr)
    {
        bool iscrunched = false;
        for (const auto& s : lzsection)
            if (s.ibank == activebank)
                iscrunched = true;
        if (!iscrunched && !extended)
        {
            MakeError(idx, "output exceed limit");
            outputMode = OutputMode::Disabled;
            stop = 1;
            return;
        }
        if (!iscrunched)
            MakeWarning(idx, "physical output overflow 64k");
        maxptr = ((outputadr >> 16) + 1) * 65536;
        mem[static_cast<size_t>(activebank)].resize(static_cast<size_t>(maxptr), 0);
    }
    expression.push_back(std::move(curexp));
}

// one trace item per contiguous run of bytes of a statement
void Engine::TraceBytes(int start, int count, int code)
{
    const int zone = static_cast<int>(orgzone.size()) - 1;
    if (!trace.empty())
    {
        TraceItem& last = trace.back();
        if (last.serial == traceSerial && last.bank == activebank && last.orgzone == zone && last.outputEnd == start &&
            last.codeStart + (last.outputEnd - last.outputStart) == code)
        {
            last.outputEnd += count;
            return;
        }
    }
    TraceItem t;
    t.serial = traceSerial;
    t.iw = idx;
    if (idx >= 0 && idx < static_cast<int>(wl.size()))
    {
        t.file = wl[static_cast<size_t>(idx)].file;
        t.line = wl[static_cast<size_t>(idx)].line;
    }
    t.bank = activebank;
    t.orgzone = zone;
    t.lz = lz;
    t.outputStart = start;
    t.outputEnd = start + count;
    t.codeStart = code;
    trace.push_back(std::move(t));
}

void Engine::ExecuteInstruction(const std::string& mnemonic)
{
    std::vector<std::string> operands;
    size_t i = static_cast<size_t>(idx);
    while (!wl[i].t)
    {
        i++;
        operands.push_back(wl[i].w);
    }
    EncoderOptions eo;
    eo.rough = static_cast<double>(rough);
    eo.noWarning = nowarning != 0;
    eo.insideLoopOrMacro = !repeat.empty() || !whilewend.empty() || !macro.empty();
    const int base = codeadr;
    auto evaluateNow = [&](const std::string& e) -> std::optional<double> {
        std::string copy = e;
        return ComputeExpression(copy, base, 0, 0);
    };
    Encoded enc = EncodeInstruction(mnemonic, operands, eo, evaluateNow);
    if (!enc.error.empty())
        MakeError(idx, enc.error);
    for (const auto& w : enc.warnings)
        MakeWarning(idx, w);
    // the bytes, with the deferred operands pushed where they go (they take their bytes, like rasm's PushExpression)
    const size_t traceFirst = trace.size();
    const size_t exprFirst = expression.size();
    size_t b = 0;
    size_t f = 0;
    std::vector<const Fixup*> order;
    for (const auto& fx : enc.fixups)
        order.push_back(&fx);
    std::stable_sort(order.begin(), order.end(), [](const Fixup* x, const Fixup* y) { return x->offset < y->offset; });
    while (b < enc.bytes.size() || f < order.size())
    {
        if (f < order.size() && order[f]->offset == b)
        {
            const Fixup& fx = *order[f++];
            PushFixup(fx, base + static_cast<int>(fx.instructionOffset));
            if (stop)
                return;
            b += static_cast<size_t>(SizeOf(KindOf(fx.kind)));
            continue;
        }
        if (b >= enc.bytes.size())
            break;
        Output(enc.bytes[b++]);
    }
    // the frontend writes the instruction back when its bytes are one run (an expansion into several is data)
    if (trace.size() == traceFirst + 1 && enc.error.empty())
    {
        TraceItem& t = trace.back();
        if (t.outputEnd - t.outputStart == static_cast<int>(enc.bytes.size()) && t.codeStart == base)
        {
            t.instruction = true;
            t.mnemonic = mnemonic;
            t.operands = operands;
            t.encoded = enc.bytes;
            // in byte order, each with the text rasm evaluates (translated when pushed)
            for (const Fixup* fx : order)
                t.fixups.push_back(*fx);
            if (expression.size() - exprFirst == t.fixups.size())
            {
                for (size_t k = 0; k < t.fixups.size(); k++)
                {
                    const Expression& x = expression[exprFirst + k];
                    t.fixups[k].expression = x.reference ? *x.reference : wl[static_cast<size_t>(x.iw)].w;
                }
            }
        }
    }
    nop += static_cast<unsigned>(enc.nops);
    tick += static_cast<unsigned>(enc.ticks);
    idx = static_cast<int>(i);
}

// ---------------------------------------------------------------------------------------------------------------------
// the main loop

void Engine::Assemble()
{
    idx = 1;
    while (wl[static_cast<size_t>(idx)].t != 2)
    {
        traceSerial++;
        const int curE = wl[static_cast<size_t>(idx)].e;
        const std::string name = curE ? std::string() : wl[static_cast<size_t>(idx)].w;
        if (!ifthen.empty() || !switchcase.empty())
        {
            int inhibe = 0;
            size_t curii;
            for (curii = 0; curii < ifthen.size(); curii++)
            {
                if (!ifthen[curii].v || ifthen[curii].v == -1)
                {
                    inhibe = 1;
                    break;
                }
            }
            if (inhibe)
            {
                const bool activeLevel = curii == ifthen.size() - 1;
                if (name == "IF") IFLight();
                else if (name == "ELSE") ELSE();
                else if (name == "CASE") CASELight();
                else if (name == "ENDIF") ENDIF();
                else if (name == "BREAK") BREAKLight();
                else if (name == "IFDEF") IFDEFLight();
                else if (name == "IFNOT") IFNOTLight();
                else if (name == "ELSEIF") { if (activeLevel) ELSEIF(); else ELSEIFLight(); }
                else if (name == "SWITCH") SWITCHLight();
                else if (name == "IFNDEF") IFNDEFLight();
                else if (name == "IFUSED") IFUSEDLight();
                else if (name == "IFNUSED") IFNUSEDLight();
                else if (name == "DEFAULT") DEFAULTLight();
                else if (name == "ELSEIFNOT") { if (activeLevel) ELSEIFNOT(); else ELSEIFNOTLight(); }
                else if (name == "ENDSWITCH") ENDSWITCH();
                while (wl[static_cast<size_t>(idx)].t == 0)
                    idx++;
                idx++;
                continue;
            }
            for (const auto& s : switchcase)
            {
                if (!s.execute)
                {
                    inhibe = 2;
                    break;
                }
            }
            if (inhibe)
            {
                if (name == "IF") IFLight();
                else if (name == "ELSE") ELSE();
                else if (name == "CASE") CASE();
                else if (name == "ENDIF") ENDIF();
                else if (name == "BREAK") BREAK();
                else if (name == "IFDEF") IFDEF();
                else if (name == "IFNOT") IFNOT();
                else if (name == "ELSEIF") ELSEIF();
                else if (name == "SWITCH") SWITCH();
                else if (name == "IFNDEF") IFNDEF();
                else if (name == "IFUSED") IFUSED();
                else if (name == "IFNUSED") IFNUSED();
                else if (name == "DEFAULT") DEFAULT();
                else if (name == "ELSEIFNOT") ELSEIFNOT();
                else if (name == "ENDSWITCH") ENDSWITCH();
                while (wl[static_cast<size_t>(idx)].t == 0)
                    idx++;
                idx++;
                continue;
            }
        }
        if (imacropos)
        {
            for (int k = 0; k < imacropos; k++)
            {
                MacroPosition& mp = macropos[static_cast<size_t>(k)];
                if (idx == mp.end && mp.pushed)
                {
                    PopGlobal();
                    mp.pushed = 0;
                }
            }
            for (int k = 0; k < imacropos; k++)
            {
                MacroPosition& mp = macropos[static_cast<size_t>(k)];
                if (idx == mp.start)
                {
                    PushGlobal();
                    mp.pushed = 1;
                }
                else if (idx < mp.start)
                {
                    break;
                }
            }
            if (idx >= macropos[0].end && repeat.empty() && whilewend.empty())
                imacropos = 0;
        }
        if (!curE)
        {
            if (Directive d = FindDirective(name))
            {
                (this->*d)();
                while (!wl[static_cast<size_t>(idx)].t)
                    idx++;
                if (stop)
                    break;
                idx++;
                continue;
            }
            if (IsInstruction(name))
            {
                ExecuteInstruction(name);
                while (!wl[static_cast<size_t>(idx)].t)
                    idx++;
                if (stop)
                    break;
                idx++;
                continue;
            }
            auto mi = macroIndex.find(name);
            if (mi != macroIndex.end())
            {
                MacroExecute(mi->second);
                continue;
            }
            PushLabel();
        }
        else
        {
            std::string& w = wl[static_cast<size_t>(idx)].w;
            ComputeExpression(w, codeadr, 0, 0);
        }
        idx++;
    }

    if (!stop)
    {
        if (getstruct)
            MakeError(0, "STRUCT declaration was not closed");
        if (!orgzone.empty())
            orgzone.back().memend = outputadr;
        OverWriteCheck();
        lz = -1;
        for (const auto& r : repeat)
            MakeError(r.start, "REPEAT was not closed");
        for (const auto& w : whilewend)
            MakeError(w.start, "WHILE was not closed");
        for (const auto& it : ifthen)
        {
            static const char* names[] = {"IF", "IFNOT", "IFDEF", "IFNDEF", "IFUSED", "IFNUSED", "ELSE", "ELSEIF", "ELSEIFNOT"};
            const auto t = static_cast<size_t>(it.type);
            MakeError(0, std::string(t < 9 ? names[t] : "<unknown>") + " conditionnal block was not closed");
        }
    }
    if (!stop || !nberr)
        CrunchAndResolve();
    for (const auto& z : orgzone)
    {
        if (z.memend > 0x10000 && memsize[static_cast<size_t>(z.ibank)] <= 0x10000 && !z.extended)
            MakeError(0, "ORG section is overflowing 64K bank by " + std::to_string(z.memend - 0x10000) + " byte(s)");
    }
    ApplyPokers();
    for (const int i : delayedPrintIdx)
    {
        idx = i;
        PRINT();
    }
    BuildOutputs();
}

// ---------------------------------------------------------------------------------------------------------------------
// end of assembly: crunched sections, then every deferred expression

void Engine::CrunchAndResolve()
{
    if (!lzsection.empty())
    {
        for (auto& a : alias)
        {
            if (a.lz >= 0)
            {
                idx = a.iw;
                const double v = ComputeExpressionCore(a.translation, a.ptr, 0);
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%.8lf", v);
                a.translation = buf;
            }
        }
    }
    for (size_t i = 0; i < lzsection.size(); i++)
    {
        if (!lzsection[i].lzversion)
            continue;
        if (lzsection[i].memend == -1)
        {
            idx = lzsection[i].iw;
            MakeError(idx, "Crunched section was not closed");
            continue;
        }
        PopAllExpression(static_cast<int>(i));
        curlz = static_cast<int>(i);
        LzSection& s = lzsection[i];
        int iorg = s.iorgzone;
        const int ibank = s.ibank;
        std::vector<uint8_t>& m = mem[static_cast<size_t>(ibank)];
        const int inputSize = s.memend - s.memstart;
        std::vector<uint8_t> lzdata;
        int lzlen = 0;
        if (!inputSize)
        {
            MakeWarning(idx, "crunched section is empty");
        }
        else
        {
            const std::optional<compress::Algorithm> alg = CrunchAlgorithm(s.lzversion, s.version);
            if (!alg)
            {
                MakeError(idx, "Crunch method not available in this version");
            }
            else
            {
                const compress::Result r =
                    compress::CrunchRaw(*alg, m.data() + s.memstart, static_cast<size_t>(inputSize), s.minmatch);
                if (!r.error.empty())
                    MakeError(idx, r.error);
                lzdata = r.bytes;
                lzlen = static_cast<int>(lzdata.size());
            }
        }
        if (inputSize < lzlen || lzlen <= 0)
        {
            // rasm reverses ZX0 backward input in place before crunching: an uncrunched section stays reversed
            if (s.lzversion == 71 && inputSize > 0)
                std::reverse(m.begin() + s.memstart, m.begin() + s.memend);
            MakeWarning(-1, "LZ section is bigger than original, it wont be crunched!");
            continue;
        }
        const int lzshift = lzlen - inputSize;
        int morgzone = iorg;
        if (lzshift < 0)
        {
            while (morgzone + 1 < static_cast<int>(orgzone.size()))
            {
                const OrgZone& next = orgzone[static_cast<size_t>(morgzone + 1)];
                const OrgZone& here = orgzone[static_cast<size_t>(morgzone)];
                if (next.memstart == here.memend && next.ibank == here.ibank)
                    morgzone++;
                else
                    break;
            }
            const int lzmove = orgzone[static_cast<size_t>(morgzone)].memend - s.memend;
            if (lzmove)
            {
                std::memmove(m.data() + s.memend + lzshift, m.data() + s.memend, static_cast<size_t>(lzmove));
                std::memset(m.data() + s.memend + lzshift + lzmove, 0, static_cast<size_t>(-lzshift));
            }
        }
        std::memcpy(m.data() + s.memstart, lzdata.data(), static_cast<size_t>(lzlen));
        s.crunchedSize = lzlen;
        // the trace follows the bytes: after the section, in its zone and the zones moved with it
        for (TraceItem& t : trace)
        {
            if (t.bank != ibank || t.orgzone < iorg || t.orgzone > morgzone || t.outputStart < s.memend)
                continue;
            t.outputStart += lzshift;
            t.outputEnd += lzshift;
            if (t.orgzone == iorg || orgzone[static_cast<size_t>(t.orgzone)].inplace)
                t.codeStart += lzshift;
        }
        // labels of this ORG zone and of the contiguous ones placed "in place"
        size_t il = static_cast<size_t>(s.iendlabel);
        const int saveorgzone = iorg;
        do
        {
            while (il < label.size() && label[il].iorgzone < iorg)
                il++;
            while (il < label.size() && label[il].iorgzone == iorg)
            {
                label[il].ptr += lzshift;
                il++;
            }
            iorg++;
            while (iorg < static_cast<int>(orgzone.size()) && !orgzone[static_cast<size_t>(iorg)].inplace && iorg <= morgzone)
                iorg++;
        } while (iorg <= morgzone);
        iorg = saveorgzone;
        size_t ie = static_cast<size_t>(s.iendexpr);
        do
        {
            while (ie < expression.size() && expression[ie].iorgzone < iorg)
                ie++;
            while (ie < expression.size() && expression[ie].iorgzone == iorg)
            {
                expression[ie].wptr += lzshift;
                if (orgzone[static_cast<size_t>(iorg)].inplace)
                    expression[ie].ptr += lzshift;
                ie++;
            }
            iorg++;
        } while (iorg <= morgzone);
        iorg = saveorgzone;
        size_t jl = i + 1;
        do
        {
            while (jl < lzsection.size() && lzsection[jl].iorgzone == iorg && lzsection[jl].ibank == ibank)
            {
                lzsection[jl].memstart += lzshift;
                lzsection[jl].memend += lzshift;
                jl++;
            }
            iorg++;
        } while (iorg <= morgzone);
        iorg = saveorgzone;
        orgzone[static_cast<size_t>(iorg)].memend += lzshift;
        while (iorg < morgzone)
        {
            iorg++;
            orgzone[static_cast<size_t>(iorg)].memstart += lzshift;
            orgzone[static_cast<size_t>(iorg)].memend += lzshift;
        }
    }
    for (size_t i = 0; i < lzsection.size(); i++)
        if (!lzsection[i].lzversion)
            PopAllExpression(static_cast<int>(i));
    PopAllExpression(-1);
}

// ---------------------------------------------------------------------------------------------------------------------
// the plain binary rasm writes (no SAVE, no snapshot / cartridge / ROM)

bool Engine::PlainBinary(std::vector<uint8_t>& out, int& start, int* bank) const
{
    int lastspaceid = -1;
    for (const auto& z : orgzone)
        if (z.memstart != z.memend && z.nocode != 1)
            lastspaceid = z.ibank;
    int minmem = 65536;
    int maxmem = 0;
    if (lastspaceid != -1)
    {
        for (const auto& z : orgzone)
        {
            if (z.protect)
                continue;
            if (z.ibank == lastspaceid && z.memstart != z.memend && z.nocode != 1)
            {
                minmem = std::min(minmem, z.memstart);
                maxmem = std::max(maxmem, z.memend);
            }
        }
    }
    out.clear();
    start = minmem;
    if (bank)
        *bank = lastspaceid;
    if (maxmem - minmem <= 0)
        return false;
    const auto& m = mem[static_cast<size_t>(lastspaceid)];
    out.assign(m.begin() + minmem, m.begin() + maxmem);
    return true;
}
}  // namespace unrealasm::rasm
