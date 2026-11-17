// rasm's directives (rasm.c v3.3.3): data (_DEFB ... _DEFS, _STR and their STRUCT variants), loops (REPEAT, WHILE,
// UNTIL), conditions (IF family, SWITCH), ORG / NOCODE / CODE / PROTECT, ALIGN / CONFINE, ENUM, STRUCT, MACRO, ASSERT.
// Every handler starts with idx on the directive word and leaves it inside the statement, as rasm's do.

#include <cstdio>
#include <cstring>

#include "dialects/rasm/engine/rasmdata.h"
#include "dialects/rasm/engine/rasmengine.h"

namespace unrealasm::rasm
{
namespace
{
bool IsUpperAlnum(char c)
{
    return (c >= 'A' && c <= 'Z') || c == '_' || (c >= '0' && c <= '9');
}
}  // namespace

/// rasm's StringIsQuote: the quote character when the word is one quoted string, else 0
int StringIsQuote(const std::string& w)
{
    if (w.empty() || (w[0] != '\'' && w[0] != '"'))
        return 0;
    const char q = w[0];
    size_t i = 1;
    while (i < w.size() && w[i] != q)
    {
        if (w[i] == '\\' && i + 1 < w.size())
            i++;
        i++;
    }
    return i + 1 == w.size() ? q : 0;
}

Word& Engine::W(int offset)
{
    return wl[static_cast<size_t>(idx + offset)];
}

std::string Engine::CurrentFile() const
{
    const Word& w = wl[static_cast<size_t>(idx)];
    return w.file >= 0 && w.file < static_cast<int>(filename.size()) ? filename[static_cast<size_t>(w.file)] : std::string();
}

void Engine::OutputString(const std::string& quoted, StringDirective directive, bool markEnd)
{
    StringOptions so;
    so.directive = directive;
    so.markEnd = markEnd;
    const StringResult r = DecodeQuotedString(quoted, charset, utf8Remap, so);
    if (!r.error.empty())
        MakeError(idx, r.error);
    for (const auto& w : r.warnings)
        MakeWarning(idx, w);
    for (uint8_t b : r.bytes)
    {
        Output(b);
        nop++;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// data

void Engine::DataDirective(ExprKind kind, int zeros, const char* name)
{
    if (!W(0).t)
    {
        do
        {
            idx++;
            PushExpression(idx, kind);
        } while (W(0).t == 0);
    }
    else if (getstruct)
    {
        for (int i = 0; i < zeros; i++)
            Output(0);
    }
    else
    {
        MakeError(idx, std::string(name) + " needs one or more parameters");
    }
}

void Engine::DEFB()
{
    if (getstruct)
    {
        DEFBStruct();
        return;
    }
    if (!W(0).t)
    {
        do
        {
            idx++;
            if (StringIsQuote(W(0).w))
            {
                OutputString(W(0).w, StringDirective::Defb, false);
            }
            else
            {
                PushExpression(idx, ExprKind::V8At0);
                nop++;
            }
        } while (W(0).t == 0);
    }
    else
    {
        MakeError(idx, "DEFB needs one or more parameters");
    }
}

void Engine::DEFBStruct()
{
    if (!W(0).t)
    {
        do
        {
            idx++;
            if (StringIsQuote(W(0).w))
            {
                OutputString(W(0).w, StringDirective::DefbStruct, false);
            }
            else
            {
                const int v = RoundComputeExpressionCore(W(0).w, outputadr, 0);
                Output(static_cast<uint8_t>(v));
                nop++;
            }
        } while (W(0).t == 0);
    }
    else
    {
        Output(0);
    }
}

void Engine::DEFW()
{
    if (!getstruct)
    {
        DataDirective(ExprKind::V16At0, 2, "DEFW");
        return;
    }
    if (!W(0).t)
    {
        do
        {
            idx++;
            const int v = RoundComputeExpressionCore(W(0).w, outputadr, 0);
            Output(static_cast<uint8_t>(v & 0xFF));
            Output(static_cast<uint8_t>((v >> 8) & 0xFF));
        } while (W(0).t == 0);
    }
    else
    {
        Output(0);
        Output(0);
    }
}

void Engine::DEFI()
{
    if (!getstruct)
    {
        DataDirective(ExprKind::V32At0, 4, "DEFI");
        return;
    }
    if (!W(0).t)
    {
        do
        {
            idx++;
            const int v = RoundComputeExpressionCore(W(0).w, outputadr, 0);
            for (int s = 0; s < 32; s += 8)
                Output(static_cast<uint8_t>((v >> s) & 0xFF));
        } while (W(0).t == 0);
    }
    else
    {
        for (int i = 0; i < 4; i++)
            Output(0);
    }
}

void Engine::RealStruct(int which)
{
    if (!W(0).t)
    {
        do
        {
            idx++;
            ExpressionFastTranslate(W(0).w, 0);
            const double v = ComputeExpressionCore(W(0).w, outputadr, 0);
            if (which == 3)
            {
                for (uint8_t b : MakeFloat24(v))
                    Output(b);
                continue;
            }
            const Real40 r = which == 1 ? MakeAmsdosReal(v) : MakeMicrosoftReal(v);
            if (r.status == RealStatus::ExponentOverflow)
                MakeError(idx, std::string(kRealOverflowMessage));
            else if (r.status == RealStatus::Hangs)
                MakeError(idx, std::string(kRealHangsMessage));
            for (uint8_t b : r.bytes)
                Output(b);
        } while (W(0).t == 0);
    }
    else
    {
        for (int i = 0; i < (which == 3 ? 3 : 5); i++)
            Output(0);
    }
}

void Engine::DEFR()
{
    if (getstruct)
        RealStruct(1);
    else
        DataDirective(ExprKind::RealAmsdos, 5, "DEFR");
}

void Engine::DEFF()
{
    if (getstruct)
        RealStruct(2);
    else
        DataDirective(ExprKind::RealMicrosoft, 5, "DEFF");
}

void Engine::DF24()
{
    if (getstruct)
        RealStruct(3);
    else
        DataDirective(ExprKind::Float24, 3, "DF24");
}

void Engine::DEFS()
{
    if (W(0).t)
    {
        MakeError(idx, "Syntax is DEFS repeat,value or DEFS repeat");
        return;
    }
    do
    {
        idx++;
        if (!getstruct && StringIsQuote(W(0).w) && W(0).w.size() > 3)
            MakeError(idx, "DEFS repeat value cannot be a string!");
        if (!W(0).t)
        {
            int r;
            if (getstruct)
            {
                r = RoundComputeExpressionCore(W(0).w, codeadr, 0);
                const int v = RoundComputeExpressionCore(W(1).w, codeadr, 0);
                if (r < 0)
                    MakeError(idx, "DEFS size must be greater or equal to zero");
                for (int i = 0; i < r; i++)
                {
                    Output(static_cast<uint8_t>(v));
                    nop++;
                }
            }
            else
            {
                ExpressionFastTranslate(W(1).w, 0);
                r = RoundComputeExpressionCore(W(0).w, codeadr, 0);
                if (r < 0)
                    MakeError(idx, "DEFS size must be greater or equal to zero");
                for (int i = 0; i < r; i++)
                {
                    PushExpression(idx + 1, ExprKind::V8At0);
                    nop++;
                }
            }
            idx++;
        }
        else if (W(0).t == 1)
        {
            const int r = RoundComputeExpressionCore(W(0).w, codeadr, 0);
            if (r < 0)
                MakeError(idx, "DEFS size must be greater or equal to zero");
            for (int i = 0; i < r; i++)
            {
                Output(0);
                nop++;
            }
        }
    } while (!W(0).t);
}

void Engine::STR()
{
    if (W(0).t)
    {
        MakeError(idx, "STR needs one or more quotes parameters");
        return;
    }
    do
    {
        idx++;
        if (StringIsQuote(W(0).w))
        {
            const bool markEnd = W(0).t != 0 || StringIsQuote(W(1).w) != 0;
            OutputString(W(0).w, StringDirective::Str, markEnd);
        }
        else
        {
            PushExpression(idx, ExprKind::V8At0);
            nop++;
        }
    } while (W(0).t == 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// ALIGN / CONFINE

void Engine::ALIGN()
{
    if (!orgzone.empty())
        orgzone.back().memend = outputadr;
    if (W(0).t)
    {
        MakeError(idx, "ALIGN <boundary>[,fill] directive need one or two integers parameters");
        return;
    }
    int aval = RoundComputeExpression(W(1).w, codeadr, 0, 0);
    int ifill = -1;
    idx++;
    if (!W(0).t)
    {
        ifill = RoundComputeExpression(W(1).w, codeadr, 0, 0);
        idx++;
        if (ifill < 0 || ifill > 255)
        {
            MakeError(idx, "ALIGN fill value must be 0 to 255");
            ifill = 0;
        }
    }
    if (aval < 1 || aval > 65535)
    {
        MakeError(idx, "ALIGN boundary must be greater than zero and lower than 65536");
        aval = 1;
    }
    if (codeadr % aval)
    {
        if (ifill == -1)
        {
            outputadr = outputadr - (codeadr % aval) + aval;
            codeadr = codeadr - (codeadr % aval) + aval;
        }
        else
        {
            while (codeadr % aval)
            {
                Output(static_cast<uint8_t>(ifill));
                nop++;
                if (stop)
                    break;
            }
        }
    }
}

void Engine::CONFINE()
{
    if (!orgzone.empty())
        orgzone.back().memend = outputadr;
    if (W(0).t)
    {
        MakeError(idx, "CONFINE <confined size>[,fill] directive need one or two integers parameters");
        return;
    }
    int aval = RoundComputeExpression(W(1).w, codeadr, 0, 0) - 1;
    int ifill = 0;
    bool warning = false;
    bool enforce = false;
    idx++;
    while (!W(0).t)
    {
        if (W(1).w == "ENFORCE")
        {
            enforce = true;
        }
        else if (W(1).w == "WARNING")
        {
            warning = true;
        }
        else
        {
            ifill = RoundComputeExpression(W(1).w, codeadr, 0, 0);
            if (ifill < 0 || ifill > 255)
            {
                MakeError(idx, "ALIGN fill value must be 0 to 255");
                ifill = 0;
            }
        }
        idx++;
    }
    if (aval < 1 || aval > 256)
    {
        MakeError(idx, "CONFINE size must be in [2-256] interval");
        aval = 1;
    }
    if (((codeadr + aval) & 0xFF00) != (codeadr & 0xFF00))
    {
        if (!nowarning || warning || enforce)
        {
            const int lost = ((codeadr + aval) & 0xFF) + 1;
            std::string msg = "CONFINE is losing " + std::to_string(lost) + " byte(s)";
            if (enforce)
                MakeError(idx, msg);
            else
            {
                const int saved = nowarning;
                nowarning = 0;
                MakeWarning(idx, msg);
                nowarning = saved;
            }
        }
        while ((codeadr & 0xFF) != 0)
        {
            Output(static_cast<uint8_t>(ifill));
            nop++;
            if (stop)
                break;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// loops

void Engine::SkipLoopBlock(LoopStyle style)
{
    std::vector<LoopStyle> loopstyle{style};
    size_t cidx = static_cast<size_t>(idx) + 2;
    while (!loopstyle.empty())
    {
        const std::string& w = wl[cidx].w;
        auto close = [&](LoopStyle expected, const char* name) {
            if (loopstyle.empty())
            {
                MakeError(idx, std::string(name) + " encountered that was not expected");
                return;
            }
            if (loopstyle.back() != expected)
                MakeError(idx, std::string(name) + " encountered but expecting another loop end");
            loopstyle.pop_back();
        };
        if (w == "REPEAT")
        {
            if (wl[cidx].t)
                loopstyle.push_back(LoopStyle::RepeatUntil);
            else if (wl[cidx + 1].t)
                loopstyle.push_back(LoopStyle::RepeatN);
            else
                MakeError(idx, "Invalid REPEAT syntax");
        }
        else if (w == "WHILE")
        {
            if (!wl[cidx].t && wl[cidx + 1].t)
                loopstyle.push_back(LoopStyle::While);
            else
                MakeError(idx, "Invalid WHILE syntax");
        }
        else if (w == "WEND")
        {
            close(LoopStyle::While, "WEND");
        }
        else if (w == "REND")
        {
            close(LoopStyle::RepeatN, "REND");
        }
        else if (w == "UNTIL")
        {
            close(LoopStyle::RepeatUntil, "UNTIL");
        }
        while (!wl[cidx].t)
            cidx++;
        if (wl[cidx].t == 2)
        {
            if (!loopstyle.empty())
                MakeError(idx, "End of file encountered with unclosed loop(s)");
            break;
        }
        cidx++;
    }
    idx = static_cast<int>(cidx) - 1;
}

void Engine::SetLoopVar(const std::string& name, double v)
{
    if (DicoVar* var = SearchDico(name))
    {
        var->v = v;
        var->used = true;
    }
    else
    {
        ExpressionSetDicoVar(name, v);
        if (DicoVar* created = SearchDico(name))
            created->used = true;
    }
}

void Engine::WHILE()
{
    if (W(0).t)
    {
        MakeError(idx, "syntax is WHILE <expression>[,<varcounter>]");
        return;
    }
    WhileWend ww;
    if (!W(1).t)
    {
        SetLoopVar(W(2).w, repeatStart);
        ww.whileVar = W(2).w;
        if (W(2).t != 1)
            MakeError(idx, "syntax is WHILE <expression>[,<varcounter>]");
    }
    ExpressionFastTranslate(W(1).w, 0);
    if (ComputeExpression(W(1).w, codeadr, 0, 2) == 0.0)
    {
        SkipLoopBlock(LoopStyle::While);
        return;
    }
    PushGlobal();
    idx++;
    ww.start = idx;
    ww.cpt = 0;
    ww.value = whilecounter;
    ww.maxim = imacropos;
    ww.whileCounter = 1;
    whilecounter++;
    if (!whilewend.empty())
        ww.maxim = whilewend.back().maxim;
    if (!repeat.empty() && repeat.back().maxim > ww.maxim)
        ww.maxim = repeat.back().maxim;
    whilewend.push_back(ww);
}

void Engine::WEND()
{
    if (whilewend.empty())
    {
        MakeError(idx, "WEND encounter whereas there is no referent WHILE");
        return;
    }
    if (W(0).t != 1)
    {
        MakeError(idx, "WEND does not need any parameter");
        return;
    }
    WhileWend& ww = whilewend.back();
    if (ComputeExpression(wl[static_cast<size_t>(ww.start)].w, codeadr, 0, 2) != 0.0)
    {
        if (ww.whileCounter > 65536)
        {
            PopGlobal();
            MakeError(idx, "Bypass infinite WHILE loop");
            whilewend.pop_back();
            if (!whilewend.empty())
                imacropos = whilewend.back().maxim;
        }
        else
        {
            ww.cpt++;
            ww.whileCounter++;
            idx = ww.start;
            imacropos = ww.maxim;
            if (!ww.whileVar.empty())
                if (DicoVar* var = SearchDico(ww.whileVar))
                    var->v++;
        }
    }
    else
    {
        PopGlobal();
        whilewend.pop_back();
        if (!whilewend.empty())
            imacropos = whilewend.back().maxim;
    }
}

void Engine::STARTINGINDEX()
{
    if (W(0).t == 0)
    {
        repeatStart = RoundComputeExpression(W(1).w, 0, 0, 0);
        idx++;
        if (W(0).t == 0)
        {
            repeatIncrement = RoundComputeExpression(W(1).w, 0, 0, 0);
            idx++;
        }
    }
    else
    {
        repeatStart = 1;
        repeatIncrement = 1;
    }
}

void Engine::REPEAT()
{
    if (W(1).t == 2)
    {
        MakeError(idx, "wrong REPEAT usage");
        return;
    }
    Repeat cur;
    if (W(0).t == 0)
    {
        cur.cpt = RoundComputeExpression(W(1).w, 0, 0, 0);
        if (!cur.cpt)
        {
            SkipLoopBlock(LoopStyle::RepeatN);
            return;
        }
        if (cur.cpt < 1 || cur.cpt > 65536)
        {
            MakeError(idx, "Repeat value (" + std::to_string(cur.cpt) + ") must be from 1 to 65535. Skipping block");
            SkipLoopBlock(LoopStyle::RepeatN);
            return;
        }
        idx++;
        if (W(0).t == 0)
        {
            idx++;
            cur.varIncrement = repeatIncrement;
            double vstart = repeatStart;
            if (W(0).t == 0)
            {
                vstart = RoundComputeExpression(W(1).w, 0, 0, 0);
                if (W(1).t == 0)
                {
                    ExpressionFastTranslate(W(2).w, 0);
                    cur.varIncrement = RoundComputeExpression(W(2).w, 0, 0, 0);
                }
            }
            SetLoopVar(W(0).w, vstart);
            cur.repeatVar = W(0).w;
            while (W(0).t == 0)
                idx++;
        }
        cur.start = idx - 1;
    }
    else
    {
        cur.start = idx;
        cur.cpt = -1;
    }
    PushGlobal();
    cur.value = repeatcounter;
    cur.repeatCounter = 1;
    repeatcounter++;
    if (!repeat.empty())
        cur.maxim = repeat.back().maxim;
    if (!whilewend.empty() && whilewend.back().maxim > cur.maxim)
        cur.maxim = whilewend.back().maxim;
    if (imacropos > cur.maxim)
        cur.maxim = imacropos;
    repeat.push_back(cur);
}

void Engine::REND()
{
    if (!W(0).t)
        MakeError(idx, "REND does not need any parameter!");
    if (repeat.empty())
    {
        MakeError(idx, "REND encounter whereas there is no referent REPEAT");
        return;
    }
    Repeat& r = repeat.back();
    if (r.cpt == -1)
    {
        MakeError(idx, "REND encounter whereas referent REPEAT was waiting for UNTIL");
        return;
    }
    r.cpt--;
    r.repeatCounter++;
    if (!r.repeatVar.empty())
        if (DicoVar* var = SearchDico(r.repeatVar))
            var->v += r.varIncrement;
    if (r.cpt)
    {
        idx = r.start;
        imacropos = r.maxim;
    }
    else
    {
        repeat.pop_back();
        if (!repeat.empty())
            imacropos = repeat.back().maxim;
        PopGlobal();
    }
}

void Engine::UNTIL()
{
    if (repeat.empty())
    {
        MakeError(idx, "UNTIL encounter whereas there is no referent REPEAT");
        return;
    }
    Repeat& r = repeat.back();
    if (r.cpt >= 0)
    {
        MakeError(idx, "UNTIL encounter whereas referent REPEAT n was waiting for REND");
        return;
    }
    if (!(W(0).t == 0 && W(1).t == 1))
    {
        MakeError(idx, "UNTIL need one expression/evaluation as parameter");
        return;
    }
    r.repeatCounter++;
    ExpressionFastTranslate(W(1).w, 0);
    if (ComputeExpression(W(1).w, codeadr, 0, 2) == 0.0)
    {
        if (r.repeatCounter > 65536)
        {
            PopGlobal();
            MakeError(idx, "Bypass infinite REPEAT loop");
            repeat.pop_back();
            if (!repeat.empty())
                imacropos = repeat.back().maxim;
        }
        else
        {
            idx = r.start;
            r.cpt--;
            imacropos = r.maxim;
        }
    }
    else
    {
        PopGlobal();
        repeat.pop_back();
        if (!repeat.empty())
            imacropos = repeat.back().maxim;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// conditions

void Engine::PushIf(int v, IfType type)
{
    IfThen it;
    it.v = v;
    it.filename = CurrentFile();
    it.line = W(0).line;
    it.type = type;
    ifthen.push_back(it);
    idx++;
}

bool Engine::OneParameter(const char* error)
{
    if (!W(0).t && W(1).t == 1)
        return true;
    MakeError(idx, error);
    return false;
}

void Engine::IF()
{
    if (OneParameter("IF need one expression"))
        PushIf(!!RoundComputeExpression(W(1).w, codeadr, 0, 1), IfType::If);
}

void Engine::IFLight()
{
    if (OneParameter("IF need one expression"))
        PushIf(0, IfType::If);
}

void Engine::IFNOT()
{
    if (OneParameter("IFNOT need one expression"))
        PushIf(!RoundComputeExpression(W(1).w, codeadr, 0, 1), IfType::IfNot);
}

void Engine::IFNOTLight()
{
    if (OneParameter("IFNOT need one expression"))
        PushIf(0, IfType::IfNot);
}

bool Engine::SymbolDefined(const std::string& word)
{
    int touched = 0;
    const std::string var = TranslateTag(word, &touched, kTagRemoveSpace);
    if (SearchDico(var))
        return true;
    if (hasModule && SearchLabel(module + moduleSeparator + var))
        return true;
    if (SearchLabel(var))
        return true;
    if (SearchAlias(var))
        return true;
    return SearchMacro(var) != nullptr;
}

void Engine::IFDEF()
{
    if (OneParameter("IFDEF need one variable or label"))
        PushIf(SymbolDefined(W(1).w) ? 1 : 0, IfType::IfDef);
}

void Engine::IFDEFLight()
{
    if (OneParameter("IFDEF need one variable or label"))
        PushIf(0, IfType::IfDef);
}

void Engine::IFNDEF()
{
    if (OneParameter("IFNDEF need one variable or label"))
        PushIf(SymbolDefined(W(1).w) ? 0 : 1, IfType::IfNDef);
}

void Engine::IFNDEFLight()
{
    if (OneParameter("IFNDEF need one variable or label"))
        PushIf(0, IfType::IfNDef);
}

void Engine::IFUSED()
{
    if (!OneParameter("IFUSED need one variable or label"))
        return;
    int touched = 0;
    const std::string var = TranslateTag(W(1).w, &touched, kTagRemoveSpace);
    if (var.empty() || !automateValidLabelFirst[static_cast<uint8_t>(var[0])])
    {
        MakeError(idx, "IFUSED argument must be a variable, alias or a label");
        return;
    }
    int rexpr = 0;
    bool lm = false;
    if (DicoVar* d = SearchDico(var))
    {
        rexpr = d->used ? 1 : 0;
    }
    else
    {
        Label* l;
        if (hasModule)
        {
            l = SearchLabel(module + moduleSeparator + var);
            lm = true;
        }
        else
        {
            l = SearchLabel(var);
        }
        if (l)
            rexpr = l->used ? 1 : 0;
        else if (Alias* a = SearchAlias(var))
            rexpr = a->used ? 1 : 0;
    }
    if (!rexpr && !lm)
        rexpr |= SearchUsed(W(1).w) ? 1 : 0;
    PushIf(rexpr, IfType::IfUsed);
}

void Engine::IFNUSED()
{
    const size_t before = ifthen.size();
    IFUSED();
    if (ifthen.size() > before)
    {
        ifthen.back().v = 1 - ifthen.back().v;
        ifthen.back().type = IfType::IfNUsed;
    }
}

void Engine::IFUSEDLight()
{
    if (OneParameter("IFUSED need one variable or label"))
        PushIf(0, IfType::IfUsed);
}

void Engine::IFNUSEDLight()
{
    const size_t before = ifthen.size();
    IFUSEDLight();
    if (ifthen.size() > before)
        ifthen.back().type = IfType::IfNUsed;
}

void Engine::UNDEF()
{
    if (!W(0).t && W(1).t == 1)
    {
        DelDico(W(1).w);
        idx++;
    }
    else
    {
        MakeError(idx, "syntax is UNDEF <variable>");
    }
}

void Engine::ElseIfCommon(bool light, bool negate, const char* name)
{
    const std::string n(name);
    if (ifthen.empty())
    {
        MakeError(idx, n + " encounter whereas there is no referent IF");
        return;
    }
    if (!(!W(0).t && W(1).t == 1))
    {
        MakeError(idx, n + " need one expression");
        return;
    }
    IfThen& it = ifthen.back();
    if (it.first)
    {
        MakeError(idx, "unexpected " + n + " (no previous IF or previous ELSE)");
    }
    else
    {
        it.type = negate ? IfType::ElseIfNot : IfType::ElseIf;
        it.line = W(0).line;
        it.filename = CurrentFile();
        if (it.v)
            it.v = -1;
        else if (light)
            it.v = 0;
        else
        {
            const int r = RoundComputeExpression(W(1).w, codeadr, 0, 1);
            it.v = negate ? !r : !!r;
        }
    }
    idx++;
}

void Engine::ELSEIF() { ElseIfCommon(false, false, "ELSEIF"); }
void Engine::ELSEIFLight() { ElseIfCommon(true, false, "ELSEIF"); }
void Engine::ELSEIFNOT() { ElseIfCommon(false, true, "ELSEIFNOT"); }
void Engine::ELSEIFNOTLight() { ElseIfCommon(true, true, "ELSEIFNOT"); }

void Engine::ELSE()
{
    if (ifthen.empty())
    {
        MakeError(idx, "ELSE encounter whereas there is no referent IF");
        return;
    }
    if (W(0).t != 1)
    {
        MakeError(idx, "ELSE does not need any parameter");
        return;
    }
    IfThen& it = ifthen.back();
    if (it.first)
    {
        MakeError(idx, "unexpected ELSE (no previous IF or previous ELSE)");
        return;
    }
    if (it.v == 0)
        it.v = 1;
    else if (it.v == 1)
        it.v = 0;
    it.first = 1;
    it.type = IfType::Else;
    it.line = W(0).line;
    it.filename = CurrentFile();
}

void Engine::ENDIF()
{
    if (ifthen.empty())
        MakeError(idx, "ENDIF encounter whereas there is no referent IF");
    else if (W(0).t != 1)
        MakeError(idx, "ENDIF does not need any parameter");
    else
        ifthen.pop_back();
}

void Engine::SWITCH()
{
    if (!OneParameter("SWITCH need one expression"))
        return;
    SwitchCase sc;
    sc.refval = RoundComputeExpression(W(1).w, codeadr, 0, 1);
    switchcase.push_back(sc);
    idx++;
}

void Engine::SWITCHLight()
{
    if (!OneParameter("SWITCH need one expression"))
        return;
    switchcase.push_back(SwitchCase{});
    idx++;
}

void Engine::CASE()
{
    if (switchcase.empty())
    {
        MakeError(idx, "CASE encounter whereas there is no referent SWITCH");
        return;
    }
    if (!W(0).t && W(1).t == 1)
    {
        const int rexpr = RoundComputeExpression(W(1).w, codeadr, 0, 1);
        if (switchcase.back().refval == rexpr)
        {
            switchcase.back().execute = 1;
            switchcase.back().casematch = 1;
        }
    }
    else
    {
        MakeError(idx, "CASE not need one parameter");
    }
}

void Engine::CASELight()
{
    if (switchcase.empty())
        MakeError(idx, "CASE encounter whereas there is no referent SWITCH");
}

void Engine::DEFAULT()
{
    if (switchcase.empty())
        MakeError(idx, "DEFAULT encounter whereas there is no referent SWITCH");
    else if (W(0).t != 1)
        MakeError(idx, "DEFAULT does not need parameter");
    else if (!switchcase.back().casematch)
        switchcase.back().execute = 1;
}

void Engine::DEFAULTLight()
{
    if (switchcase.empty())
        MakeError(idx, "DEFAULT encounter whereas there is no referent SWITCH");
}

void Engine::BREAK()
{
    if (switchcase.empty())
        MakeError(idx, "BREAK encounter whereas there is no referent SWITCH");
    else if (W(0).t != 1)
        MakeError(idx, "BREAK does not need parameter");
    else
        switchcase.back().execute = 0;
}

void Engine::BREAKLight()
{
    if (switchcase.empty())
        MakeError(idx, "BREAK encounter whereas there is no referent SWITCH");
}

void Engine::ENDSWITCH()
{
    if (switchcase.empty())
        MakeError(idx, "ENDSWITCH encounter whereas there is no referent SWITCH");
    else if (W(0).t != 1)
        MakeError(idx, "ENDSWITCH does not need any parameter");
    else
        switchcase.pop_back();
}

void Engine::ASSERT()
{
    if (W(0).t)
    {
        MakeError(idx, "ASSERT need one expression");
        return;
    }
    const std::string text = W(1).w;
    const int rexpr = !!RoundComputeExpression(W(1).w, codeadr, 0, 1);
    if (!rexpr)
    {
        std::string shown = text.substr(0, 29) + (text.size() > 29 ? "..." : "");
        std::string copy = W(1).w;
        ExpressionFastTranslate(copy, 1);
        MakeError(idx, "ASSERT " + shown + " failed with " + copy);
        if (!W(1).t)
        {
            idx++;
            PRINT();
        }
        STOP();
    }
    else
    {
        while (!W(0).t)
            idx++;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// ORG and friends

void Engine::PROTECT()
{
    if (!W(0).t && !W(1).t && W(2).t == 1)
    {
        const int memstart = RoundComputeExpression(W(1).w, 0, 0, 0);
        const int memend = RoundComputeExpression(W(2).w, 0, 0, 0);
        OrgZone fake;
        fake.memstart = memstart;
        fake.memend = memend;
        fake.ibank = activebank;
        fake.protect = 1;
        orgzone.insert(orgzone.end() - 1, fake);
    }
    else
    {
        MakeError(idx, "PROTECT need two parameters: startadr,endadr");
    }
}

bool Engine::BankIsCrunched() const
{
    for (int i = static_cast<int>(lzsection.size()) - 1; i >= 0; i--)
        if (lzsection[static_cast<size_t>(i)].ibank == activebank)
            return true;
    return false;
}

void Engine::ORG()
{
    if (getstruct == 1)
    {
        MakeError(idx, "You cannot ORG inside a structure declaration");
        return;
    }
    OrgClose();
    orgDone = 1;
    if (W(0).t)
    {
        MakeError(idx, "ORG code location[,output location]");
        return;
    }
    insideORG = 1;
    codeadr = RoundComputeExpression(W(1).w, outputadr, 0, 0);
    insideORG = 0;
    if (codeadr < 0)
    {
        codeadr = 0;
        MakeError(idx, "cannot ORG outside memory!");
    }
    else if (codeadr > 0xFFFF && !BankIsCrunched())
    {
        codeadr = 0;
        MakeError(idx, "cannot ORG outside memory!");
    }
    if (!W(1).t && W(2).t != 2)
    {
        insideORG = 1;
        outputadr = RoundComputeExpression(W(2).w, outputadr, 0, 0);
        insideORG = 0;
        if (outputadr < 0)
        {
            outputadr = 0;
            MakeError(idx, "cannot ORG outside memory!");
        }
        else if (outputadr > 0xFFFF && !BankIsCrunched() && memsize[static_cast<size_t>(activebank)] <= 0x10000)
        {
            outputadr = 0;
            MakeError(idx, "cannot ORG outside memory!");
        }
        idx += 2;
    }
    else
    {
        outputadr = codeadr;
        idx++;
    }
    OrgNew(nocode);
    if (outputadr == codeadr)
        orgzone.back().inplace = 1;
}

void Engine::NOCODE()
{
    if (getstruct == 1)
    {
        MakeError(idx, "You cannot NOCODE inside a structure declaration");
        return;
    }
    if (W(0).t == 1)
    {
        OrgClose();
        codeadrbackup = codeadr;
        outputadrbackup = outputadr;
        OrgNew(1);
    }
    else
    {
        MakeError(idx, "NOCODE directive does not need parameter");
    }
}

void Engine::CODE()
{
    if (getstruct == 1)
    {
        MakeError(idx, "You cannot CODE inside a structure declaration");
        return;
    }
    if (!W(0).t)
    {
        if (W(1).w == "SKIP")
        {
            OrgClose();
            codeadr = codeadrbackup;
            outputadr = outputadrbackup;
            OrgNew(0);
        }
        else
        {
            MakeError(idx, "unknown parameter for CODE directive");
        }
        idx++;
    }
    else if (W(0).t == 1)
    {
        OrgClose();
        OrgNew(0);
    }
    else
    {
        MakeError(idx, "CODE directive does not need parameter");
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// ENUM

void Engine::ENUM()
{
    int start = 0;
    int increment = 1;
    std::string prefix;
    if (!W(0).t)
    {
        idx++;
        int touched = 0;
        prefix = TranslateTag(W(0).w, &touched, kTagRemoveSpace);
        bool ok = !prefix.empty() && ((prefix[0] >= 'A' && prefix[0] <= 'Z') || prefix[0] == '_');
        if (!ok)
        {
            MakeError(idx, "ENUM name must begin by a letter or '_' [" + prefix + "]");
            prefix = "_";
        }
        else
        {
            for (size_t i = 1; i < prefix.size(); i++)
            {
                if (!IsUpperAlnum(prefix[i]))
                {
                    MakeError(idx, "ENUM name must contains letters, digits or '_' [" + prefix + "]");
                    prefix = "_";
                    break;
                }
            }
        }
        prefix += "_";
        if (!W(0).t)
        {
            idx++;
            std::string e = W(0).w;
            start = RoundComputeExpression(e, outputadr, 0, 0);
            if (!W(0).t)
            {
                idx++;
                std::string inc = W(0).w;
                increment = RoundComputeExpression(inc, outputadr, 0, 0);
                if (!W(0).t)
                    MakeError(idx, "ENUM declaration has too much parameters");
            }
        }
    }
    while (!W(0).t)
        idx++;
    idx++;
    while (true)
    {
        if (W(0).t == 2)
        {
            idx--;
            MakeError(idx, "End of file reached before the end of ENUM");
            break;
        }
        if (W(0).w == "MEND" || W(0).w == "ENDM")
            break;
        if (W(0).t == 0)
        {
            MakeError(idx, "ENUM elements must not have parameter(s)");
            while (!W(0).t)
                idx++;
        }
        else
        {
            std::string& expwrk = W(0).w;
            const size_t eq = expwrk.find('=');
            if (eq != std::string::npos)
            {
                std::string dbl = expwrk.substr(eq + 1);
                start = static_cast<int>(ComputeExpressionCore(dbl, codeadr, 0));
                expwrk.resize(eq);
            }
            for (size_t i = 1; i < expwrk.size(); i++)
            {
                if (!IsUpperAlnum(expwrk[i]))
                {
                    MakeError(idx, "ENUM name must contains letters, digits or '_' [" + expwrk + "]");
                    expwrk = "_";
                    break;
                }
            }
            Alias a;
            a.alias = prefix + expwrk;
            if (SearchLabel(a.alias))
            {
                MakeError(idx, "ENUM cannot create alias because there is already a label with this name [" + a.alias + "]");
            }
            else if (SearchDico(a.alias))
            {
                MakeError(idx, "ENUM cannot create alias because there is already a variable with this name [" + a.alias + "]");
            }
            else
            {
                a.ptr = codeadr;
                a.iw = idx;
                a.lz = static_cast<int>(lzsection.size());
                a.translation = "(" + std::to_string(start) + ")";
                a.autoriseExport = autoriseExport != 0;
                if (!InsertAlias(a))
                    MakeError(idx, "ENUM cannot create alias because there is already an alias with this name [" + a.alias + "]");
            }
        }
        start += increment;
        idx++;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// STRUCT

void Engine::STRUCT()
{
    if (W(0).t)
    {
        MakeError(idx, "STRUCT directive needs one or two parameters");
        return;
    }
    if (W(1).t)
    {
        // declaration
        if (getstruct)
        {
            MakeError(idx, "STRUCT cannot be declared inside previous opened STRUCT [" + backupFilename + "] Line " +
                               std::to_string(backupLine));
            return;
        }
        if (SearchLabel(W(1).w) || SearchAlias(W(1).w))
        {
            MakeError(idx, "STRUCT name must be different from existing labels ou aliases");
            return;
        }
        backupFilename = CurrentFile();
        backupLine = W(0).line;
        backupOutputadr = outputadr;
        backupCodeadr = codeadr;
        getstruct = 1;
        lz = -1;
        OrgClose();
        codeadr = 0;
        OrgNew(1);
        RasmStruct rs;
        rs.name = W(1).w;
        rasmstruct.push_back(rs);
        idx++;
        return;
    }
    // insertion
    int nbelem = 1;
    size_t irs = 0;
    while (irs < rasmstruct.size() && rasmstruct[irs].name != W(1).w)
        irs++;
    if (irs == rasmstruct.size())
    {
        MakeError(idx, "Unknown STRUCT " + W(1).w + " to insert");
        return;
    }
    int touched = 0;
    const std::string structName = TranslateTag(W(2).w, &touched, kTagRemoveSpace);
    const bool local = !W(2).w.empty() && W(2).w[0] == '@';
    RasmStruct structAlias;
    if (!getstruct)
    {
        structAlias.name = local ? MakeLocalLabel(structName, nullptr) : structName;
    }
    else
    {
        if (irs == rasmstruct.size() - 1)
        {
            MakeError(idx, "Cannot insert self struct Object inside struct");
            while (!W(0).t)
                idx++;
            return;
        }
        structAlias.name = rasmstruct.back().name + "." + structName;
    }
    structAlias.size = rasmstruct[irs].size;
    structAlias.ptr = codeadr;
    if (!W(2).t)
    {
        if (!StringIsQuote(W(3).w))
        {
            nbelem = RoundComputeExpression(W(3).w, outputadr, 0, 0);
            if (nbelem < 1)
            {
                MakeError(idx, "Struct array need a positive number of elements!");
                nbelem = 1;
            }
        }
        else
        {
            MakeError(idx, "usage is STRUCT <structType> <structName>[,<arrayNbElement>[,<fieldFiller>, ...]]");
        }
    }
    structAlias.nbelem = nbelem;
    rasmstructalias.push_back(structAlias);

    Label curlabel;
    curlabel.lz = -1;
    curlabel.ptr = codeadr;
    curlabel.relocatable = relocblockactive != 0;
    curlabel.fileidx = W(2).file;
    if (!getstruct)
    {
        curlabel.name = structAlias.name;
        PushLabelLight(curlabel);
    }
    else
    {
        if (local)
        {
            MakeError(idx, "Meaningless use of local label in a STRUCT definition");
        }
        else
        {
            curlabel.name = structAlias.name;
            PushLabelLight(curlabel);
        }
        RasmStructField f;
        f.name = structName;
        f.offset = codeadr;
        rasmstruct.back().fields.push_back(f);
    }
    curlabel.ptr = codeadr;
    curlabel.ibank = activebank < kBankMaxNumber ? activebank : 0;
    const std::vector<RasmStructField> typeFields = rasmstruct[irs].fields;   // copy: the current struct may grow
    for (const auto& tf : typeFields)
    {
        curlabel.ptr = codeadr + tf.offset;
        if (!getstruct)
        {
            curlabel.name = structName + "." + tf.name;
            if (local)
                curlabel.name = MakeLocalLabel(curlabel.name, nullptr);
            PushLabelLight(curlabel);
        }
        else
        {
            RasmStructField f;
            f.name = structName + "." + tf.name;
            f.offset = curlabel.ptr;
            rasmstruct.back().fields.push_back(f);
            curlabel.name = rasmstruct.back().name + "." + f.name;
            PushLabelLight(curlabel);
        }
    }
    idx += 2;
    if (!W(0).t)
        idx++;
    const RasmStruct& type = rasmstruct[irs];
    while (nbelem)
    {
        size_t i = 0;
        int j = 0;
        for (; i < type.fields.size(); i++)
        {
            const RasmStructField& field = type.fields[i];
            if (!field.size)
                continue;
            if (W(j).t)
                break;
            j++;
            const std::string& filler = W(j).w;
            if (StringIsQuote(filler))
            {
                StringOptions so;
                so.directive = StringDirective::StructField;
                so.maxBytes = field.size;
                const StringResult r = DecodeQuotedString(filler, charset, utf8Remap, so);
                if (!r.error.empty())
                    MakeError(idx + j, r.error);
                for (const auto& w : r.warnings)
                    MakeWarning(idx + j, w);
                for (uint8_t b : r.bytes)
                {
                    Output(b);
                    nop++;
                }
                if (r.overrun)
                    MakeWarning(idx + j, "struct field number " + std::to_string(i) + " overrun! (" +
                                             std::to_string(field.size) + " byte(s) max)!");
                else if (r.count < field.size)
                    for (size_t k = static_cast<size_t>(r.count); k < field.data.size(); k++)
                    {
                        Output(field.data[k]);
                        nop++;
                    }
            }
            else
            {
                switch (field.size)
                {
                    case 1: PushExpression(idx + j, ExprKind::V8At0); nop += 1; break;
                    case 2: PushExpression(idx + j, ExprKind::V16At0); nop += 2; break;
                    case 4: PushExpression(idx + j, ExprKind::V32At0); nop += 4; break;
                    case 5: PushExpression(idx + j, ExprKind::RealAmsdos); nop += 5; break;
                    default:
                        MakeWarning(idx + j, "cannot initialise a field of " + std::to_string(field.size) +
                                                 " bytes with an expression, default data used");
                        for (uint8_t b : field.data)
                        {
                            Output(b);
                            nop++;
                        }
                        break;
                }
            }
        }
        for (; i < type.fields.size(); i++)
        {
            for (uint8_t b : type.fields[i].data)
            {
                Output(b);
                nop++;
            }
        }
        nbelem--;
    }
}

void Engine::ENDSTRUCT()
{
    if (!W(0).t)
    {
        MakeError(idx, "ENDSTRUCT directive does not need parameter");
        return;
    }
    if (!getstruct)
    {
        MakeError(idx, "ENDSTRUCT encountered outside STRUCT declaration");
        return;
    }
    RasmStruct& rs = rasmstruct.back();
    rs.size = codeadr;
    Label curlabel;
    curlabel.name = rs.name;
    curlabel.lz = -1;
    curlabel.ptr = rs.size;
    PushLabelLight(curlabel);
    getstruct = 0;
    if (!rs.fields.empty())
    {
        for (size_t i = 0; i + 1 < rs.fields.size(); i++)
        {
            rs.fields[i].size = rs.fields[i + 1].offset - rs.fields[i].offset;
            rs.fields[i].fullname = rs.name + "." + rs.fields[i].name;
        }
        rs.fields.back().size = rs.size - rs.fields.back().offset;
        rs.fields.back().fullname = rs.name + "." + rs.fields.back().name;
    }
    else
    {
        MakeWarning(idx, "STRUCT " + rs.name + " has no field");
    }
    outputadr = backupOutputadr;
    codeadr = backupCodeadr;
    OrgClose();
    OrgNew(0);
}

// ---------------------------------------------------------------------------------------------------------------------
// MACRO (definition)

void Engine::MACRO()
{
    if (getstruct == 1)
    {
        MakeError(idx, "Please, why define a MACRO inside a structure declaration?");
        return;
    }
    if (W(0).t || W(1).t == 2)
    {
        MakeError(idx, "MACRO definition need at least one parameter for the name of the macro");
        return;
    }
    Macro m;
    m.mnemo = W(1).w;
    bool getparam = W(1).t == 0;
    if (SearchMacro(m.mnemo))
        MakeError(idx, "Macro already defined with this name");
    else if (SearchDico(m.mnemo))
        MakeError(idx, "Macro definition: There is already a variable with this name");
    else if (SearchLabel(m.mnemo))
        MakeError(idx, "Macro definition: There is already a label with this name");
    else if (SearchAlias(m.mnemo))
        MakeError(idx, "Macro definition: There is already an alias with this name");
    else if (IsRegister(m.mnemo))
        MakeError(idx, "Macro definition: Cannot choose a register as macro name");

    size_t i = static_cast<size_t>(idx) + 2;
    int enumCount = 0;
    while (true)
    {
        if (wl[i].t == 2)
            break;
        const std::string& w = wl[i].w;
        if (w == "MEND" || w == "ENDM")
        {
            if (enumCount)
                enumCount--;
            else
                break;
        }
        if (w == "MACRO")
        {
            const int refidx = idx;
            idx = static_cast<int>(i);
            MakeError(idx, "You cannot define a macro inside another one (MACRO " + wl[static_cast<size_t>(refidx) + 1].w + ")");
            STOP();
        }
        if (getparam)
        {
            if (w == "...")
                m.multiArg = true;
            else
                m.param.push_back(as80 ? w : "{" + w + "}");
            if (wl[i].t)
                getparam = false;
        }
        else
        {
            if (w == "ENUM")
                enumCount++;
            m.wc.push_back(wl[i]);
        }
        i++;
    }
    if (wl[i].t == 2)
        MakeError(idx, "Macro was not closed");
    for (size_t k = 0; k < m.wc.size(); k++)
    {
        std::string& w = m.wc[k].w;
        if (StringIsQuote(w))
        {
            int depth = 0;
            for (char& c : w)
            {
                if (c == '{')
                    depth++;
                else if (c == '}')
                    depth--;
                else if (depth)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        const size_t brace = w.find('{');
        if (brace != std::string::npos)
        {
            m.replaceIdx.push_back(static_cast<int>(k));
            if (m.multiArg && brace + 1 < w.size() && std::isdigit(static_cast<unsigned char>(w[brace + 1])))
            {
                const size_t bar = w.find('|', brace + 1);
                const size_t close = w.find('}', brace + 1);
                // strchr(...,'|') < strchr(...,'}'): a missing one is NULL, below any pointer
                const bool before = bar == std::string::npos ? close != std::string::npos
                                                             : close != std::string::npos && bar < close;
                if (before)
                {
                    const int cur = std::atoi(w.c_str() + brace + 1);
                    if (cur > m.maxArg)
                        m.maxArg = cur;
                }
            }
        }
    }
    if (!macroIndex.count(m.mnemo))
        macroIndex[m.mnemo] = macro.size();
    macro.push_back(std::move(m));
    imacro = static_cast<int>(macro.size());
    if (wl[i].t == 2)
        i--;
    idx = static_cast<int>(i);
}
}  // namespace unrealasm::rasm
