// rasm's expression engine and symbol tables (rasm.c v3.3.3): ComputeExpressionCore, ComputeExpression,
// ExpressionFastTranslate, TranslateTag, MakeLocalLabel, PushLabel, PushExpression, PopAllExpression. Ported rule for rule;
// comments name rasm's behaviour where it is surprising, since the bytes depend on it.

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "dialects/rasm/engine/rasmdata.h"
#include "dialects/rasm/engine/rasmengine.h"
#include "dialects/rasm/engine/rasminstructions.h"

namespace unrealasm::rasm
{
namespace
{
char At(const std::string& s, size_t i)
{
    return i < s.size() ? s[i] : '\0';
}

uint8_t U(char c)
{
    return static_cast<uint8_t>(c);
}

bool StartsWith(const std::string& s, size_t pos, const char* prefix)
{
    return s.compare(pos, std::strlen(prefix), prefix) == 0;
}

/// strtol over a C string starting at `pos` (stops at the first invalid char, like rasm)
double StrToL(const std::string& s, size_t pos, int base)
{
    const char* p = s.c_str() + (pos < s.size() ? pos : s.size());
    return static_cast<double>(std::strtol(p, nullptr, base));
}

/// sprintf("%02X") / ("%03X") of a char as rasm does it (a char above 127 is negative: "FFFFFFxx")
std::string HexOfChar(char c, int width)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), width == 2 ? "#%02X" : "#%03X", static_cast<unsigned>(static_cast<int>(c)));
    return buf;
}

ComputeElement ZeroToken()
{
    ComputeElement e;
    e.op = Op::PushData;
    e.value = 0;
    return e;
}

struct MathKeyword
{
    const char* name;
    Op op;
};

const MathKeyword kMathKeywords[] = {
    {"SIN", Op::Sin}, {"COS", Op::Cos}, {"INT", Op::Int}, {"ABS", Op::Abs}, {"LN", Op::Ln}, {"LOG10", Op::Log10},
    {"SQRT", Op::Sqrt}, {"FLOOR", Op::Floor}, {"ASIN", Op::Asin}, {"ACOS", Op::Acos}, {"ATAN", Op::Atan},
    {"EXP", Op::Exp}, {"LO", Op::Low}, {"HI", Op::High}, {"PSGVALUE", Op::Psg}, {"RND", Op::Rnd}, {"FRAC", Op::Frac},
    {"CEIL", Op::Ceil}, {"GETR", Op::GetR}, {"GETV", Op::GetV}, {"GETG", Op::GetV}, {"GETB", Op::GetB},
    {"SETR", Op::SetR}, {"SETV", Op::SetV}, {"SETG", Op::SetV}, {"SETB", Op::SetB}, {"SOFT2HARD_INK", Op::Soft2Hard},
    {"S2H_INK", Op::Soft2Hard}, {"HARD2SOFT_INK", Op::Hard2Soft}, {"H2S_INK", Op::Hard2Soft}, {"PEEK", Op::Peek},
    {"POW2", Op::Pow2}, {"POW", Op::Pow}, {"FMOD", Op::Fmod}, {"ATAN2", Op::Atan2}, {"HYPOT", Op::Hypot},
    {"LDEXP", Op::Ldexp}, {"FDIM", Op::Fdim}, {"FSTEP", Op::Fstep}, {"FMAX", Op::Fmax}, {"FMIN", Op::Fmin},
    {"CLAMP", Op::Clamp}, {"LERP", Op::Lerp}, {"ISGREATER", Op::IsGreater}, {"ISLESS", Op::IsLess},
    {"FREMAIN", Op::Fmod}, {"GETNOP", Op::GetNop}, {"GETTICK", Op::GetTick}, {"DURATION", Op::Duration},
    {"FILESIZE", Op::FileSize}, {"GETSIZE", Op::GetSize}, {"IS_REGISTER", Op::IsRegister}, {"FILEBYTE", Op::FileByte},
    {"COUNTNOPS", Op::CountNops},
};

const std::unordered_map<std::string, Op>& MathTable()
{
    static const std::unordered_map<std::string, Op> table = [] {
        std::unordered_map<std::string, Op> t;
        for (const auto& k : kMathKeywords)
            t.emplace(k.name, k.op);
        return t;
    }();
    return table;
}

/// rasm's getOperatorStr (error texts)
const char* OperatorName(Op op)
{
    switch (op)
    {
        case Op::Add: return "+";
        case Op::Sub: return "-";
        case Op::Mul: return "*";
        case Op::Div: return "/";
        case Op::IntDiv: return "\\";
        case Op::And: return "&";
        case Op::Or: return "|";
        case Op::Mod: return "%";
        case Op::Xor: return "^";
        case Op::Not: return "!";
        case Op::Shl: return "<<";
        case Op::Shr: return ">>";
        case Op::BAnd: return "&&";
        case Op::BOr: return "||";
        case Op::Lower: return "<";
        case Op::Greater: return ">";
        case Op::Equal: return "==";
        case Op::NotEqual: return "!=";
        case Op::LowerEq: return "<=";
        case Op::GreaterEq: return ">=";
        default:
            for (const auto& k : kMathKeywords)
                if (k.op == op)
                    return k.name;
            return "?";
    }
}

/// rasm's TradExpression: an expression as the user wrote it (for error texts)
std::string Trad(const std::string& e)
{
    std::string out;
    for (char c : e)
    {
        if (c == '[')
            out += "<<";
        else if (c == ']')
            out += ">>";
        else if (c == 'm')
            out += "%";
        else
            out += c;
    }
    return out;
}

/// rasm's getToken: the identifiers ([A-Z][A-Z0-9_]*) of a text; the last one is only taken when something follows it
std::vector<std::string> GetTokens(const std::string& str)
{
    std::vector<std::string> out;
    int st = 0;
    size_t start = 0;
    for (size_t i = 0; i < str.size(); i++)
    {
        const char c = str[i];
        if (st == 0)
        {
            if (c >= 'A' && c <= 'Z')
            {
                start = i;
                st = 1;
            }
        }
        else if (!((c >= 'A' && c <= 'Z') || c == '_' || (c >= '0' && c <= '9')))
        {
            out.push_back(str.substr(start, i - start));
            st = 0;
        }
    }
    return out;
}

int MinMaxInt(double v, int lo, int hi)
{
    int i = static_cast<int>(v);
    if (i < lo)
        return lo;
    if (i > hi)
        return hi;
    return i;
}

/// (int) of a double as C does it for in-range values; out-of-range values are undefined in C, x86/ARM give
/// INT_MIN / saturation: keep it defined
int ToInt(double v)
{
    if (!(v == v))
        return 0;
    if (v >= 2147483647.0)
        return 2147483647;
    if (v <= -2147483648.0)
        return static_cast<int>(-2147483647 - 1);
    return static_cast<int>(v);
}

int RoundInt(double v)
{
    return ToInt(std::floor(v + 0.5));
}
}  // namespace

bool IsRegister(const std::string& w)
{
    static const std::unordered_set<std::string> regs = {"F",  "I",  "R",  "A",  "B",  "C",  "D",  "E",  "H",  "L",
                                                         "BC", "DE", "HL", "IX", "IY", "SP", "AF", "XH", "XL", "YH",
                                                         "YL", "HX", "LX", "HY", "LY", "IXL", "IXH", "IYL", "IYH"};
    return regs.count(w) != 0;
}

bool IsMath(const std::string& word)
{
    return MathTable().count(word) != 0;
}

std::string FormatDouble(double v)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf) - 1, "%lf", v);
    // TrimFloatingPointString: cut the fraction only when it is all zeros
    size_t i = 0;
    size_t pflag = 0;
    bool zflag = false;
    while (buf[i])
    {
        if (buf[i] == '.')
        {
            pflag = i;
            zflag = true;
        }
        else if (buf[i] != '0')
        {
            zflag = false;
        }
        i++;
    }
    if (zflag)
        buf[pflag] = 0;
    return buf;
}

unsigned Engine::FastRand()
{
    fastRandSeed = 214013 * fastRandSeed + 2531011;
    return (fastRandSeed >> 16) & 0x7FFF;
}

// ---------------------------------------------------------------------------------------------------------------------
// diagnostics

int Engine::GetExpIdx(int didx) const
{
    if (didx > 0 && didx < static_cast<int>(expression.size()))
        return expression[static_cast<size_t>(didx)].iw;
    return idx;
}

void Engine::MakeError(int iw, const std::string& message)
{
    Diagnostic d;
    d.severity = Severity::Error;
    if (iw >= 0 && iw < static_cast<int>(wl.size()))
    {
        const Word& w = wl[static_cast<size_t>(iw)];
        d.line = static_cast<uint32_t>(w.line);
        const std::string& file = w.file >= 0 && w.file < static_cast<int>(filename.size())
                                      ? filename[static_cast<size_t>(w.file)]
                                      : std::string();
        d.message = "[" + file + ":" + std::to_string(w.line) + "] " + message;
    }
    else
    {
        d.message = message;
    }
    while (!d.message.empty() && d.message.back() == '\n')
        d.message.pop_back();
    diagnostics.push_back(std::move(d));
    nberr++;
}

void Engine::MakeWarning(int iw, const std::string& message)
{
    if (nowarning)
        return;
    Diagnostic d;
    d.severity = Severity::Warning;
    if (iw >= 0 && iw < static_cast<int>(wl.size()))
    {
        const Word& w = wl[static_cast<size_t>(iw)];
        d.line = static_cast<uint32_t>(w.line);
        const std::string& file = w.file >= 0 && w.file < static_cast<int>(filename.size())
                                      ? filename[static_cast<size_t>(w.file)]
                                      : std::string();
        d.message = "[" + file + ":" + std::to_string(w.line) + "] Warning: " + message;
    }
    else
    {
        d.message = "Warning: " + message;
    }
    diagnostics.push_back(std::move(d));
    if (erronwarn)
        nberr++;
}

void Engine::ExpError(int didx, const std::string& message)
{
    MakeError(GetExpIdx(didx), message);
}

void Engine::ExpWarning(int didx, const std::string& message)
{
    MakeWarning(GetExpIdx(didx), message);
}

// ---------------------------------------------------------------------------------------------------------------------
// symbol tables

DicoVar* Engine::SearchDico(const std::string& name)
{
    auto it = dicoTree.find(name);
    return it == dicoTree.end() ? nullptr : &it->second;
}

Alias* Engine::SearchAlias(const std::string& name)
{
    auto it = aliasTree.find(name);
    return it == aliasTree.end() ? nullptr : &alias[it->second];
}

Label* Engine::SearchLabel(const std::string& name)
{
    auto it = labelTree.find(name);
    return it == labelTree.end() ? nullptr : &label[it->second];
}

bool Engine::InsertLabelToTree(const Label& l)
{
    if (labelTree.count(l.name))
        return false;
    labelTree.emplace(l.name, label.size());
    label.push_back(l);
    return true;
}

void Engine::ExpressionSetDicoVar(const std::string& name, double v)
{
    if (SearchLabel(name))
    {
        MakeError(idx, "cannot create variable [" + name + "] as there is already a label with the same name");
        return;
    }
    auto it = dicoTree.find(name);
    if (it != dicoTree.end())
        return;   // InsertDicoToTree: an existing variable is updated by the callers, never re-inserted
    DicoVar d;
    d.name = name;
    d.v = v;
    d.iw = idx;
    d.autoriseExport = autoriseExport != 0;
    dicoTree.emplace(name, d);
    dicoOrder.push_back(name);
}

// ---------------------------------------------------------------------------------------------------------------------
// MakeLocalLabel / TranslateTag

std::string Engine::MakeLocalLabel(const std::string& varbuffer, int* retdek)
{
    if (At(varbuffer, 0) == '.')
    {
        if (retdek)
            *retdek = 0;
        if (!lastgloballabel.empty())
            return lastgloballabel + varbuffer;
        return varbuffer;
    }
    std::string locallabel;
    std::string suffix;
    if (!retdek)
    {
        const size_t dot = varbuffer.find('.');
        if (dot != std::string::npos)
        {
            locallabel = varbuffer.substr(0, dot);
            suffix = varbuffer.substr(dot);
        }
        else
        {
            locallabel = varbuffer;
        }
    }
    std::string hash;
    char hexdigit[32];
    if (!repeat.empty())
    {
        std::snprintf(hexdigit, sizeof(hexdigit), "R%X", static_cast<unsigned>(repeat.back().value));
        hash += hexdigit;
    }
    for (const auto& r : repeat)
    {
        std::snprintf(hexdigit, sizeof(hexdigit), "R%X", static_cast<unsigned>(r.cpt));
        hash += hexdigit;
    }
    if (!whilewend.empty())
    {
        std::snprintf(hexdigit, sizeof(hexdigit), "W%X", static_cast<unsigned>(whilewend.back().value));
        hash += hexdigit;
    }
    for (const auto& w : whilewend)
    {
        std::snprintf(hexdigit, sizeof(hexdigit), "W%X", static_cast<unsigned>(w.cpt));
        hash += hexdigit;
    }
    if (!macropos.empty())
    {
        int im;
        for (im = static_cast<int>(macropos.size()) - 1; im >= 0; im--)
        {
            const auto& mp = macropos[static_cast<size_t>(im)];
            if (idx >= mp.start && idx < mp.end)
                break;
        }
        if (im >= 0)
        {
            std::snprintf(hexdigit, sizeof(hexdigit), "M%X", static_cast<unsigned>(macropos[static_cast<size_t>(im)].value));
            hash += hexdigit;
        }
    }
    if (retdek)
    {
        *retdek = static_cast<int>(hash.size());
        return hash;
    }
    return locallabel + hash + suffix;
}

std::string Engine::TranslateTag(std::string varbuffer, int* touched, int tagoption)
{
    constexpr int kRemoveSpace = 1;
    constexpr int kPreserve = 2;
    if ((tagoption & kPreserve) && (!whilewend.empty() || !repeat.empty()))
        return varbuffer;   // inside a loop the variables change: translated when the expression is pushed
    *touched = 0;
    size_t start;
    while ((start = varbuffer.find('{', 1)) != std::string::npos && varbuffer.size() > 1)
    {
        if (varbuffer.find('}', start) == std::string::npos)
        {
            MakeError(idx, "invalid tag in string [" + varbuffer + "]");
            return "invalid tag";
        }
        int tagcount = 1;
        size_t check = start + 1;
        while (check < varbuffer.size() && tagcount)
        {
            if (varbuffer[check] == '}')
                tagcount--;
            else if (varbuffer[check] == '{')
                tagcount++;
            check++;
        }
        if (tagcount)
        {
            MakeError(idx, "invalid brackets combination in string [" + varbuffer + "]");
            return "invalid tag";
        }
        const size_t end = check - 1;
        *touched = 1;
        std::string expr = varbuffer.substr(start + 1, end - start - 1);
        if (tagoption & kRemoveSpace)
        {
            std::string nospace;
            for (char c : expr)
                if (c != ' ')
                    nospace += c;
            expr = nospace;
        }
        const int validx = RoundComputeExpressionCore(expr, codeadr, 0);
        if (validx < 0)
        {
            MakeError(idx, "indexed tag must NOT be a negative value [" + varbuffer + "]");
            return varbuffer;
        }
        varbuffer.replace(start, end - start + 1, std::to_string(validx));
    }
    return varbuffer;
}

// ---------------------------------------------------------------------------------------------------------------------
// ComputeExpressionCore

double Engine::StringFunction(Op op, const std::string& arg, const std::string* arg2, double number, int didx)
{
    switch (op)
    {
        case Op::GetNop:
        case Op::GetTick:
        case Op::GetSize:
        case Op::Duration:
        {
            TimingSum s;
            if (op == Op::GetNop)
                s = GetNop(arg);
            else if (op == Op::GetTick)
                s = GetTick(arg);
            else if (op == Op::GetSize)
                s = GetSize(arg);
            else
                s = Duration(arg, forcezx != 0);
            if (!s.error.empty())
                ExpError(didx, s.error);
            return s.value;
        }
        case Op::IsRegister:
        {
            std::string up = arg;
            for (auto& c : up)
                c = static_cast<char>(std::toupper(U(c)));
            return IsRegister(up) ? 1 : 0;
        }
        case Op::FileSize:
        case Op::FileByte:
        {
            std::string name = arg;
            int curly = 0;
            for (auto& c : name)
            {
                if (c == '{')
                    curly++;
                else if (c == '}')
                    curly--;
                else if (curly)
                    c = static_cast<char>(std::toupper(U(c)));
            }
            int touched = 0;
            name = TranslateTag(name, &touched, 1);
            const int iw = GetExpIdx(didx);
            const int fileIndex = iw >= 0 && iw < static_cast<int>(wl.size()) ? wl[static_cast<size_t>(iw)].file : 0;
            std::optional<std::vector<uint8_t>> data = fs.read ? fs.read(name) : std::nullopt;
            if (!data && fileIndex >= 0 && fileIndex < static_cast<int>(filename.size()))
                data = fs.read ? fs.read(MergePath(filename[static_cast<size_t>(fileIndex)], name)) : std::nullopt;
            for (size_t i = 0; !data && i < fs.includePaths.size(); i++)
                data = fs.read ? fs.read(MergePath(fs.includePaths[i], name)) : std::nullopt;
            if (op == Op::FileSize)
            {
                if (!data)
                {
                    ExpError(didx, "FILESIZE did not find [" + arg + "] file in the path");
                    return 0;
                }
                return static_cast<double>(data->size());
            }
            if (!data)
            {
                ExpError(didx, "File [" + arg + "] not found for FILEBYTE function");
                return 0;
            }
            const auto offset = static_cast<unsigned>(ToInt(number));
            if (offset < data->size())
                return (*data)[offset];
            ExpError(didx, "invalid offset " + std::to_string(offset) + " for FILEBYTE function and file [" + name + "]");
            return 0;
        }
        case Op::CountNops:
        {
            auto proximity = [&](const std::string& l) {
                if (At(l, 0) == '.')
                {
                    if (dams)
                        return l.substr(1);
                    if (!lastgloballabel.empty())
                        return lastgloballabel + l;
                }
                return l;
            };
            const std::string label1 = arg;
            const std::string label2 = arg2 ? *arg2 : std::string("$");
            const std::string n1 = proximity(label1);
            const std::string n2 = proximity(label2);
            Label* first = nullptr;
            Label* second = nullptr;
            if (hasModule)
            {
                if (label2 != "$")
                    second = SearchLabel(module + moduleSeparator + n2);
                first = SearchLabel(module + moduleSeparator + n1);
            }
            if (!first)
                first = SearchLabel(n1);
            if (label2 != "$" && !second)
                second = SearchLabel(n2);
            if (!first)
            {
                ExpError(didx, "COUNTNOPS need a known label [" + label1 + "]");
                return 0;
            }
            if (label2 == "$")
                return static_cast<double>(static_cast<int>(nop - first->nop));
            if (!second)
            {
                ExpError(didx, "COUNTNOPS second label not known [" + label2 + "]");
                return 0;
            }
            return static_cast<double>(static_cast<int>(second->nop) - static_cast<int>(first->nop));
        }
        default:
            return 0;
    }
}

double Engine::ComputeExpressionCore(std::string& original, int ptr, int didx)
{
    if (original.empty())
        return 0;

    std::vector<ComputeElement> tokenstack;
    int allowMinusAsSign = 0;
    // double hack if the first value is negative
    if (original[0] == '-')
    {
        if (automateExpressionValidCharFirst[U(At(original, 1))])
            allowMinusAsSign = 1;
        else
            tokenstack.push_back(ZeroToken());
    }

    // quoted single chars become hexadecimal values, in the caller's text (rasm converts in place)
    size_t idx2 = 0;
    char c;
    while ((c = At(original, idx2)) != 0)
    {
        if (c == '\'' || c == '"')
        {
            if (At(original, idx2 + 1) == '\\')
            {
                if (At(original, idx2 + 2) && At(original, idx2 + 3) == c)
                {
                    original.replace(idx2, 4, HexOfChar(original[idx2 + 2], 3).substr(0, 4));
                    idx2 += 3;
                }
                else
                {
                    idx2++;
                    while (At(original, idx2) && At(original, idx2) != c)
                        idx2++;
                }
            }
            else if (At(original, idx2 + 1) && At(original, idx2 + 2) == c)
            {
                if (idx2 >= 12 && original.compare(idx2 - 12, 12, "IS_REGISTER(") == 0)
                {
                    idx2 += 2;
                }
                else
                {
                    original.replace(idx2, 3, HexOfChar(original[idx2 + 1], 2).substr(0, 3));
                    idx2 += 2;
                }
            }
            else
            {
                idx2++;
                while (At(original, idx2) && At(original, idx2) != c)
                    idx2++;
            }
        }
        idx2++;
    }

    // alias substitution works on a copy (rasm reallocates a private buffer)
    std::string ze = original;
    size_t pos = ze[0] == '+' ? 1 : 0;
    std::string varbuffer;
    int ivar = 0;
    int isString = 0;
    int parenth = 0;
    int curly = 0;
    int curlyflag = 0;
    size_t startvar = 0;
    double curval = 0;
    int minusptr = 0;

    while ((c = At(ze, pos)) != 0)
    {
        bool skipPush = false;
        switch (c)
        {
            case '"':
            case '\'':
                varbuffer.clear();
                pos++;
                while (At(ze, pos) && At(ze, pos) != c)
                    varbuffer += ze[pos++];
                if (At(ze, pos))
                    pos++;
                else
                    ExpError(didx, "ComputeExpression [" + Trad(ze) + "] quote bug!");
                ivar = static_cast<int>(varbuffer.size());
                isString = 1;
                break;
            case ')':
                allowMinusAsSign = 0;
                parenth--;
                break;
            case '(':
                parenth++;
                [[fallthrough]];
            case ',':
                if (!parenth)
                    ExpError(didx, " invalid comma in expression, must only be used for multiple param functions...");
                [[fallthrough]];
            case '*':
            case '/':
            case '\\':
            case '^':
            case '[':
            case 'm':
            case '+':
            case ']':
                allowMinusAsSign = 1;
                break;
            case '&':
                allowMinusAsSign = 1;
                if (At(ze, pos + 1) == '&')
                {
                    pos++;
                    c = 'a';
                }
                break;
            case '|':
                allowMinusAsSign = 1;
                if (At(ze, pos + 1) == '|')
                {
                    pos++;
                    c = 'o';
                }
                break;
            case '<':
                allowMinusAsSign = 1;
                if (At(ze, pos + 1) == '=')
                {
                    pos++;
                    c = 'k';
                }
                else if (At(ze, pos + 1) == '>')
                {
                    pos++;
                    c = 'n';
                }
                else
                {
                    c = 'l';
                }
                break;
            case '>':
                allowMinusAsSign = 1;
                if (At(ze, pos + 1) == '=')
                {
                    pos++;
                    c = 'h';
                }
                else
                {
                    c = 'g';
                }
                break;
            case '!':
                allowMinusAsSign = 1;
                if (At(ze, pos + 1) == '=')
                {
                    pos++;
                    c = 'n';
                }
                else
                {
                    c = 'b';
                }
                break;
            case '=':
                allowMinusAsSign = 1;
                if (At(ze, pos + 1) == '=')
                {
                    pos++;
                    c = 'e';
                }
                else if (maxam)
                {
                    c = 'e';
                }
                else
                {
                    ExpError(didx, "expression [" + Trad(ze) + "] cannot set variable inside an expression");
                    return 0;
                }
                break;
            case '-':
                if (allowMinusAsSign)
                {
                    // previous char was an opening parenthesis or an operator: the minus belongs to the value
                    varbuffer = "-";
                    c = At(ze, ++pos);
                    if (automateExpressionValidCharFirst[U(c)])
                    {
                        varbuffer += c;
                        c = At(ze, ++pos);
                        while (automateExpressionValidChar[U(c)])
                        {
                            varbuffer += c;
                            c = At(ze, ++pos);
                        }
                    }
                    else if (c == '(' && pos > 1 && ze[pos - 2] == '(')
                    {
                        // "(-(": a zero then a binary minus
                        tokenstack.push_back(ZeroToken());
                        allowMinusAsSign = 0;
                        ivar = 0;
                        c = '-';
                        pos--;
                        break;
                    }
                    ivar = static_cast<int>(varbuffer.size());
                    if (ivar < 2)
                    {
                        ExpError(didx, "expression [" + Trad(ze) + "] invalid minus sign");
                        return 0;
                    }
                    break;
                }
                allowMinusAsSign = 1;
                break;
            case '%':
            {
                // a modulo or a binary literal
                int isBinary = 0;
                for (size_t icheck = 1; At(ze, pos + icheck); icheck++)
                {
                    switch (ze[pos + icheck])
                    {
                        case '1':
                        case '0':
                            isBinary = 1;
                            break;
                        case '+': case '-': case '/': case '*': case '|': case 'm': case '%': case '^': case '&':
                        case '(': case ')': case '=': case '<': case '>': case '!': case '[': case ']':
                            if (isBinary)
                                isBinary = 2;
                            else
                                isBinary = -1;
                            break;
                        default:
                            isBinary = -1;
                    }
                    if (isBinary == 2)
                        break;
                    if (isBinary == -1)
                    {
                        isBinary = 0;
                        break;
                    }
                }
                if (!isBinary)
                {
                    allowMinusAsSign = 1;
                    c = 'm';
                    break;
                }
            }
                [[fallthrough]];
            default:
            {
                allowMinusAsSign = 0;
                startvar = pos;
                varbuffer.clear();
                if (automateExpressionValidCharFirst[U(c)])
                {
                    varbuffer += c;
                    if (c == '{')
                        curly++;
                    pos++;
                    c = At(ze, pos);
                    const std::array<char, 256>* automate = &automateExpressionValidChar;
                    while ((*automate)[U(c)])
                    {
                        if (c == '{')
                        {
                            curly++;
                            curlyflag = 1;
                            automate = &automateExpressionValidCharExtended;
                        }
                        else if (c == '}')
                        {
                            curly--;
                            if (!curly)
                                automate = &automateExpressionValidChar;
                        }
                        varbuffer += c;
                        pos++;
                        c = At(ze, pos);
                    }
                }
                ivar = static_cast<int>(varbuffer.size());
                if (!ivar)
                {
                    char ch[2] = {U(c) > 31 ? c : ' ', 0};
                    ExpError(didx, "ComputeExpression invalid char (" + std::to_string(static_cast<int>(c)) + "=" + ch +
                                       ") expression [" + Trad(ze) + "]");
                    return 0;
                }
                if (curly)
                {
                    ExpError(didx, "ComputeExpression wrong curly brackets in expression [" + Trad(ze) + "]");
                    return 0;
                }
            }
        }
        if (c && !ivar)
            pos++;

        // ---- stack dispatcher ----
        ComputeElement element;
        if (!ivar)
        {
            element = automateElement[U(c)];
            if (element.op > Op::GreaterEq)
            {
                char ch[2] = {U(c) > 31 ? c : '.', 0};
                ExpError(didx, "expression [" + Trad(ze) + "] has unknown operator " + ch + " (" +
                                   std::to_string(static_cast<int>(c)) + ")");
            }
            element.string.reset();
        }
        else if (isString)
        {
            element.op = Op::PushData;
            element.string = varbuffer;
            allowMinusAsSign = 0;
            ivar = isString = 0;
        }
        else
        {
            minusptr = varbuffer[0] == '-' ? 1 : 0;
            const size_t m = static_cast<size_t>(minusptr);
            auto vb = [&](size_t i) { return At(varbuffer, i); };
            bool resolved = false;   // the C `break` out of switch (varbuffer[minusptr])
            const char first = vb(m);
            if (first == '0' || (first >= '1' && first <= '9'))
            {
                if (first == '0' && vb(m + 1) == 'X' && automateHexa[U(vb(m + 2))])
                {
                    for (size_t icheck = m + 3; vb(icheck); icheck++)
                    {
                        if (automateHexa[U(vb(icheck))])
                            continue;
                        ExpError(didx, "expression [" + Trad(ze) + "] - " + varbuffer + " is not a valid hex number");
                        break;
                    }
                    curval = StrToL(varbuffer, m + 2, 16);
                    resolved = true;
                }
                else if (first == '0' && vb(m + 1) == 'B' && vb(m + 2) >= '0' && vb(m + 2) <= '1')
                {
                    for (size_t icheck = m + 3; vb(icheck); icheck++)
                    {
                        if (vb(icheck) >= '0' && vb(icheck) <= '1')
                            continue;
                        ExpError(didx, "expression [" + Trad(ze) + "] - " + varbuffer + " is not a valid binary number");
                        break;
                    }
                    curval = StrToL(varbuffer, m + 2, 2);
                    resolved = true;
                }
                else if (first == '0' && vb(m + 1) == 'O' && vb(m + 2) >= '0' && vb(m + 2) <= '7')
                {
                    for (size_t icheck = m + 3; vb(icheck); icheck++)
                    {
                        if (vb(icheck) >= '0' && vb(icheck) <= '7')
                            continue;
                        ExpError(didx, "expression [" + Trad(ze) + "] - " + varbuffer + " is not a valid octal number");
                        break;
                    }
                    curval = StrToL(varbuffer, m + 2, 8);
                    resolved = true;
                }
                if (!resolved)
                {
                    size_t icheck;
                    for (icheck = m; vb(icheck); icheck++)
                    {
                        if (automateDigit[U(vb(icheck))])
                            continue;
                        // Intel hexadecimal and binary styles
                        switch (vb(static_cast<size_t>(ivar - 1)))
                        {
                            case 'H':
                                for (size_t k = m; vb(k + 1); k++)
                                {
                                    if (automateHexa[U(vb(k))])
                                        continue;
                                    ExpError(didx, "expression [" + Trad(ze) + "] - " + varbuffer + " is not a valid hex number");
                                }
                                curval = StrToL(varbuffer, m, 16);
                                break;
                            case 'B':
                                for (size_t k = m; vb(k + 1); k++)
                                {
                                    if (vb(k) == '0' || vb(k) == '1')
                                        continue;
                                    ExpError(didx, "expression [" + Trad(ze) + "] - " + varbuffer + " is not a valid binary number");
                                }
                                curval = StrToL(varbuffer, m, 2);
                                break;
                            default:
                                ExpError(didx, "expression [" + Trad(ze) + "] - " + varbuffer + " is not a valid number");
                                curval = 0;
                        }
                        icheck = 0;
                        break;
                    }
                    if (!vb(icheck))
                        curval = std::atof(varbuffer.c_str() + m);
                    resolved = true;
                }
            }
            else if (first == '%')
            {
                if (!vb(m + 1))
                    ExpError(didx, "expression [" + Trad(ze) + "] - " + varbuffer + " is an empty binary number");
                for (size_t icheck = m + 1; vb(icheck); icheck++)
                {
                    if (vb(icheck) == '0' || vb(icheck) == '1')
                        continue;
                    ExpError(didx, "expression [" + Trad(ze) + "] - " + varbuffer + " is not a valid binary number");
                    break;
                }
                curval = StrToL(varbuffer, m + 1, 2);
                resolved = true;
            }
            else if (first == '#')
            {
                if (!vb(m + 1))
                    ExpError(didx, "expression [" + Trad(ze) + "] - " + varbuffer + " is an empty hex number");
                for (size_t icheck = m + 1; vb(icheck); icheck++)
                {
                    if (automateHexa[U(vb(icheck))])
                        continue;
                    ExpError(didx, "expression [" + Trad(ze) + "] - " + varbuffer + " is not a valid hex number");
                    break;
                }
                curval = StrToL(varbuffer, m + 1, 16);
                resolved = true;
            }
            else
            {
                if (!curlyflag)
                {
                    if (vb(m) == '$' && automateHexa[U(vb(m + 1))])
                    {
                        for (size_t icheck = m + 2; vb(icheck); icheck++)
                        {
                            if (automateHexa[U(vb(icheck))])
                                continue;
                            ExpError(didx, "expression [" + Trad(ze) + "] - " + varbuffer + " is not a valid hex number");
                            break;
                        }
                        curval = StrToL(varbuffer, m + 1, 16);
                        resolved = true;
                    }
                    else if (vb(m) == '@' && vb(m + 1) >= '0' && vb(m + 1) <= '7')
                    {
                        for (size_t icheck = m + 2; vb(icheck); icheck++)
                        {
                            if (vb(icheck) >= '0' && vb(icheck) <= '7')
                                continue;
                            ExpError(didx, "expression [" + Trad(ze) + "] - " + varbuffer + " is not a valid octal number");
                            break;
                        }
                        curval = StrToL(varbuffer, m + 1, 8);
                        resolved = true;
                    }
                    else if (automateHexa[U(vb(m))] && vb(static_cast<size_t>(ivar - 1)) == 'H')
                    {
                        size_t icheck;
                        for (icheck = m; vb(icheck + 1); icheck++)
                            if (!automateHexa[U(vb(icheck))])
                                break;
                        if (!vb(icheck + 1))
                        {
                            curval = StrToL(varbuffer, m, 16);
                            resolved = true;
                        }
                    }
                }
                else
                {
                    int touched = 0;
                    const std::string mini = TranslateTag(varbuffer.substr(m), &touched, 0);
                    varbuffer = varbuffer.substr(0, m) + mini;
                    curlyflag = 0;
                }
                if (!resolved)
                {
                    const std::string name = varbuffer.substr(m);
                    if (vb(m) == '$' && vb(m + 1) == '$' && ivar == 2 + minusptr)
                    {
                        curval = outputadr;   // $$: always the physical output position
                    }
                    else if (vb(m) == '$' && ivar == 1 + minusptr)
                    {
                        curval = ptr;
                    }
                    else
                    {
                        // a function, only when a parenthesis follows (a leading minus is lost, as in rasm)
                        if (c == '(')
                        {
                            auto f = MathTable().find(name);
                            if (f != MathTable().end())
                            {
                                ComputeElement fe;
                                fe.op = f->second;
                                tokenstack.push_back(fe);
                                fe.op = Op::Open;
                                tokenstack.push_back(fe);
                                allowMinusAsSign = 1;
                                parenth++;
                                pos++;
                                ivar = 0;
                                continue;
                            }
                        }
                        if (DicoVar* curdic = SearchDico(name))
                        {
                            curval = curdic->v;
                            curdic->used = true;
                        }
                        else
                        {
                            int bank = 0;
                            int page = 0;
                            if (vb(m) != '{')
                            {
                            }
                            else if (StartsWith(varbuffer, m, "{BANK}"))
                            {
                                bank = 6;
                            }
                            else if (StartsWith(varbuffer, m, "{PAGE}"))
                            {
                                bank = 6;
                                page = 1;
                                if (forcecpr)
                                    ExpError(didx, "expression [" + Trad(ze) + "] cannot use PAGE in cartridge mode");
                            }
                            else if (StartsWith(varbuffer, m, "{PAGESET}"))
                            {
                                bank = 9;
                                page = 2;
                                if (forcecpr)
                                    ExpError(didx, "expression [" + Trad(ze) + "] cannot use PAGESET in cartridge mode");
                            }
                            else if (StartsWith(varbuffer, m, "{SIZEOF}"))
                            {
                                bank = 8;
                                page = 3;
                                const std::string sname = varbuffer.substr(m + 8);
                                curval = -1;
                                for (const auto& sa : rasmstructalias)
                                {
                                    if (sa.name == sname)
                                    {
                                        curval = sa.size;
                                        break;
                                    }
                                }
                                if (curval == -1)
                                {
                                    for (const auto& rs : rasmstruct)
                                    {
                                        if (rs.name == sname)
                                        {
                                            curval = rs.size;
                                            break;
                                        }
                                        bool found = false;
                                        for (const auto& fld : rs.fields)
                                        {
                                            if (fld.fullname == sname)
                                            {
                                                curval = fld.size;
                                                found = true;
                                                break;
                                            }
                                        }
                                        if (found)
                                            break;
                                    }
                                }
                                if (curval == -1)
                                {
                                    ExpError(didx, "cannot SIZEOF unknown structure or field [" + sname + "]!");
                                    curval = 0;
                                }
                            }
                            else
                            {
                                ExpError(didx, "expression [" + Trad(ze) + "] - " + varbuffer + " is an unknown prefix!");
                            }
                            if (page != 3)
                            {
                                const std::string lname = varbuffer.substr(m + static_cast<size_t>(bank));
                                Label* curlabel = nullptr;
                                if (didx > 0 && didx < static_cast<int>(expression.size()))
                                {
                                    const auto& em = expression[static_cast<size_t>(didx)].module;
                                    if (em)
                                        curlabel = SearchLabel(*em + moduleSeparator + lname);
                                    if (!curlabel)
                                        curlabel = SearchLabel(lname);
                                }
                                else
                                {
                                    if (hasModule)
                                        curlabel = SearchLabel(module + moduleSeparator + lname);
                                    if (!curlabel)
                                        curlabel = SearchLabel(lname);
                                }
                                auto bankValue = [&](const Label& l) -> double {
                                    switch (page)
                                    {
                                        case 2:
                                            if (l.ibank < kBankMaxNumber)
                                                return setgate[static_cast<size_t>(l.ibank)];
                                            ExpError(didx, "expression [" + Trad(ze) + "] cannot use PAGESET - label [" +
                                                               varbuffer + "] is in a temporary space!");
                                            return l.ibank;
                                        case 1:
                                            if (l.ibank < kBankMaxNumber)
                                            {
                                                if (bankset[static_cast<size_t>(l.ibank >> 2)])
                                                    return bankgate[static_cast<size_t>((l.ibank & 0x1FC) + (l.ptr >> 14))];
                                                return bankgate[static_cast<size_t>(l.ibank)];
                                            }
                                            ExpError(didx, "expression [" + Trad(ze) + "] cannot use PAGE - label [" +
                                                               varbuffer + "] is in a temporary space!");
                                            return l.ibank;
                                        default:
                                            if (forcesnapshot && l.ibank > 260)
                                            {
                                                for (int isr = 0; isr < 256; isr++)
                                                    if (rombank[static_cast<size_t>(isr)] == l.ibank)
                                                        return isr;
                                                ExpError(didx, "expression [" + Trad(ze) + "] cannot use BANK - label [" +
                                                                   varbuffer + "] is in a temporary space!");
                                                return curval;
                                            }
                                            return l.ibank;
                                    }
                                };
                                if (curlabel)
                                {
                                    curlabel->used = true;
                                    if (stage < 2)
                                    {
                                        if (curlabel->lz == -1)
                                        {
                                            curval = !bank ? curlabel->ptr : bankValue(*curlabel);
                                        }
                                        else
                                        {
                                            bool process = true;
                                            // a label of a crunched section: only from a previous / the same section or
                                            // when its ORG zone is relocated
                                            if (lzsection[static_cast<size_t>(curlabel->lz)].lzversion)
                                            {
                                                const int explz = didx >= 0 && didx < static_cast<int>(expression.size())
                                                                      ? expression[static_cast<size_t>(didx)].lz
                                                                      : -1;
                                                if (!(curlabel->lz <= explz ||
                                                      orgzone[static_cast<size_t>(curlabel->iorgzone)].inplace == 0))
                                                    process = false;
                                            }
                                            if (process)
                                            {
                                                if (!bank)
                                                    curval = curlabel->ptr;
                                                else if (page)
                                                    curval = bankValue(*curlabel);
                                                // {BANK} of such a label: rasm leaves the value unset (previous one)
                                            }
                                            else
                                            {
                                                ExpError(didx, "Label [" + varbuffer + "] evaluation error!");
                                                curval = 0;
                                            }
                                        }
                                    }
                                    else
                                    {
                                        curval = bank ? bankValue(*curlabel) : curlabel->ptr;
                                    }
                                }
                                else
                                {
                                    // aliases declared after their use
                                    Alias* curalias = nullptr;
                                    const std::string aname = varbuffer.substr(m);
                                    if (didx > 0 && didx < static_cast<int>(expression.size()))
                                    {
                                        const auto& em = expression[static_cast<size_t>(didx)].module;
                                        if (em)
                                            curalias = SearchAlias(*em + moduleSeparator + aname);
                                        if (!curalias)
                                            curalias = SearchAlias(aname);
                                    }
                                    else
                                    {
                                        if (hasModule)
                                            curalias = SearchAlias(module + moduleSeparator + aname);
                                        if (!curalias)
                                            curalias = SearchAlias(aname);
                                    }
                                    if (curalias)
                                    {
                                        curalias->used = true;
                                        // the leading unary minus stays, only the name is replaced
                                        const size_t from = startvar + m;
                                        const size_t len = static_cast<size_t>(ivar - minusptr);
                                        if (from <= ze.size())
                                            ze.replace(from, std::min(len, ze.size() - from), curalias->translation);
                                        if (minusptr && !automateExpressionValidCharFirst[U(At(ze, startvar + m))])
                                        {
                                            tokenstack.push_back(ZeroToken());
                                            allowMinusAsSign = 0;
                                        }
                                        pos = startvar;
                                        ivar = 0;
                                        skipPush = true;
                                    }
                                    else
                                    {
                                        // an index on a struct array? NAME<digits>
                                        int validx = -1;
                                        size_t reverse = varbuffer.size() - 1;
                                        if (varbuffer[reverse] >= '0' && varbuffer[reverse] <= '9')
                                        {
                                            reverse--;
                                            while (varbuffer[reverse] >= '0' && varbuffer[reverse] <= '9')
                                                reverse--;
                                            reverse++;
                                            validx = std::atoi(varbuffer.c_str() + reverse);
                                            const std::string structlabel = varbuffer.substr(m, reverse - m);
                                            size_t i;
                                            for (i = 0; i < rasmstructalias.size(); i++)
                                            {
                                                const auto& sa = rasmstructalias[i];
                                                if (sa.name == structlabel)
                                                {
                                                    curval = sa.size * validx + sa.ptr;
                                                    if (validx >= sa.nbelem)
                                                        ExpWarning(didx, "index out of array size!");
                                                    break;
                                                }
                                            }
                                            if (i == rasmstructalias.size())
                                                validx = -1;
                                        }
                                        if (validx < 0)
                                        {
                                            if (IsRegister(aname))
                                                ExpError(didx, "cannot use register " + Trad(ze) + " in this context");
                                            else if (IsKeyword(aname))
                                                ExpError(didx, "cannot use directive " + Trad(ze) + " in this context");
                                            else
                                                ExpError(didx, "expression [" + Trad(ze) + "] keyword [" + aname +
                                                                   "] not found in variables, labels or aliases");
                                            curval = 0;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (skipPush)
                continue;
            if (minusptr)
                curval = -curval;
            element.op = Op::PushData;
            element.value = curval;
            element.string.reset();
            allowMinusAsSign = 0;
            ivar = 0;
        }
        tokenstack.push_back(std::move(element));
    }

    // ---- shunting yard ----
    std::vector<ComputeElement> computestack;
    std::vector<ComputeElement> operatorstack;
    for (auto& token : tokenstack)
    {
        switch (token.op)
        {
            case Op::PushData:
                computestack.push_back(token);
                break;
            case Op::Open:
                operatorstack.push_back(token);
                break;
            case Op::Close:
            case Op::Comma:
            {
                bool okclose = false;
                while (!operatorstack.empty())
                {
                    if (operatorstack.back().op != Op::Open)
                    {
                        computestack.push_back(operatorstack.back());
                        operatorstack.pop_back();
                    }
                    else
                    {
                        if (token.op == Op::Close)
                            operatorstack.pop_back();   // discard the opening parenthesis; a comma keeps it
                        okclose = true;
                        break;
                    }
                }
                if (!okclose)
                {
                    ExpError(didx, "missing parenthesis [" + Trad(ze) + "]");
                    return 0;
                }
                // a function right before the parenthesis is popped with it
                if (token.op == Op::Close && !operatorstack.empty() && operatorstack.back().op >= Op::Sin)
                {
                    computestack.push_back(operatorstack.back());
                    operatorstack.pop_back();
                }
                break;
            }
            default:
                if (token.op >= Op::Add && token.op <= Op::GreaterEq)
                {
                    while (!operatorstack.empty() && operatorstack.back().op != Op::Open)
                    {
                        if (token.priority >= operatorstack.back().priority || operatorstack.back().op >= Op::Sin)
                        {
                            computestack.push_back(operatorstack.back());
                            operatorstack.pop_back();
                        }
                        else
                        {
                            break;
                        }
                    }
                    operatorstack.push_back(token);
                }
                else if (token.op >= Op::Sin && token.op < Op::End)
                {
                    operatorstack.push_back(token);
                }
                break;
        }
    }
    while (!operatorstack.empty())
    {
        computestack.push_back(operatorstack.back());
        operatorstack.pop_back();
    }

    // ---- execution ----
    std::vector<double> accu(computestack.size() + 16, 0.0);
    int paccu = 0;
    int accuErr = 0;
    const int nbcomputestack = static_cast<int>(computestack.size());
    auto stringAt = [&](double v) -> std::optional<std::string>* {
        const int i = ToInt(std::floor(v));
        if (i >= 0 && i < nbcomputestack && computestack[static_cast<size_t>(i)].string)
            return &computestack[static_cast<size_t>(i)].string;
        return nullptr;
    };
    auto stringIndexValid = [&](double v) {
        const int i = ToInt(std::floor(v));
        return i >= 0 && i < nbcomputestack;
    };
    auto A = [&](int k) -> double& { return accu[static_cast<size_t>(paccu - k)]; };
    auto shiftWarning = [&](double count) {
        if (ToInt(count) > 31 || ToInt(count) < -31)
        {
            ExpWarning(didx, "shifting " + std::to_string(ToInt(count)) + " is architecture dependant, result forced to ZERO");
            return true;
        }
        return false;
    };
    // functions on strings: GETNOP ... COUNTNOPS (same code in both modes)
    auto stringOp = [&](Op op) {
        const char* fname = OperatorName(op);
        if (op == Op::FileByte)
        {
            if (paccu > 1)
            {
                if (auto* s = stringAt(A(2)))
                {
                    A(2) = StringFunction(op, **s, nullptr, A(1) + 0.5, didx);
                    s->reset();
                    paccu--;
                }
                else if (stringIndexValid(A(2)))
                {
                    ExpError(didx, "FILEBYTE function needs a proper string");
                }
                else
                {
                    ExpError(didx, "FILEBYTE internal error (wrong string index)");
                }
            }
            else
            {
                ExpError(didx, "FILEBYTE needs 2 parameters, '<filename>' and offset");
            }
            return;
        }
        if (op == Op::CountNops)
        {
            if (paccu > 1)
            {
                int isDollar = 0;
                auto* s2 = stringAt(A(1));
                if (!s2)
                {
                    if (ToInt(A(1) + 0.5) == '$')
                    {
                        isDollar = 1;
                    }
                    else
                    {
                        ExpError(didx, "COUNTNOPS needs 2 strings parameters, 'label' and ( 'label' or '$' )");
                        isDollar = -1;
                    }
                }
                if (stringIndexValid(A(2)) && isDollar >= 0)
                {
                    auto* s1 = stringAt(A(2));
                    if (s1)
                    {
                        std::string l1 = **s1;
                        for (auto& ch : l1)
                            ch = static_cast<char>(std::toupper(U(ch)));
                        if (isDollar)
                        {
                            A(2) = StringFunction(op, l1, nullptr, 0, didx);
                        }
                        else
                        {
                            std::string l2 = **s2;
                            for (auto& ch : l2)
                                ch = static_cast<char>(std::toupper(U(ch)));
                            A(2) = StringFunction(op, l1, &l2, 0, didx);
                            s2->reset();
                        }
                        s1->reset();
                        paccu--;
                    }
                    else
                    {
                        if (s2)
                            s2->reset();
                        ExpError(didx, "COUNTNOPS needs strings parameters, '<label>' and ( 'label' or '$' )");
                    }
                }
                else
                {
                    ExpError(didx, "COUNTNOPS needs 2 strings parameters, 'label' and ( 'label' or '$' )");
                }
            }
            else
            {
                ExpError(didx, "COUNTNOPS needs 2 strings parameters, 'label' and ( 'label' or '$' )");
            }
            return;
        }
        if (paccu > 0)
        {
            if (auto* s = stringAt(A(1)))
            {
                A(1) = StringFunction(op, **s, nullptr, 0, didx);
                s->reset();
            }
            else if (stringIndexValid(A(1)))
            {
                ExpError(didx, std::string(fname) + " function needs a proper string");
            }
            else
            {
                ExpError(didx, std::string(fname) + " internal error (wrong string index)");
            }
        }
        else
        {
            ExpError(didx, std::string(fname) + " is empty");
        }
    };
    auto twoParams = [&](const char* name) {
        ExpError(didx, std::string(name) + " requires 2 parameters");
    };

    if (maxamAs80)
    {
        const int workinterval = as80 ? static_cast<int>(0xFFFFFFFF) : 0xFFFF;
        for (int i = 0; i < nbcomputestack; i++)
        {
            const auto& ce = computestack[static_cast<size_t>(i)];
            switch (ce.op)
            {
                case Op::PushData:
                    accu[static_cast<size_t>(paccu)] = ce.string ? i + 0.1 : ce.value;
                    paccu++;
                    break;
                case Op::Open:
                case Op::Close:
                    break;
                case Op::Add: if (paccu > 1) A(2) = (ToInt(A(2)) + ToInt(A(1))) & workinterval; paccu--; break;
                case Op::Sub: if (paccu > 1) A(2) = (ToInt(A(2)) - ToInt(A(1))) & workinterval; paccu--; break;
                case Op::Mul: if (paccu > 1) A(2) = (ToInt(A(2)) * ToInt(A(1))) & workinterval; paccu--; break;
                case Op::IntDiv:
                case Op::Div:
                    if (paccu > 1)
                    {
                        const int divisor = ToInt(A(1)) & workinterval;
                        if (divisor == 0)
                        {
                            ExpError(didx, "division by zero in expression [" + Trad(ze) + "]");
                            A(2) = 0;
                        }
                        else
                        {
                            A(2) = (ToInt(A(2)) & workinterval) / divisor;
                        }
                    }
                    paccu--;
                    break;
                case Op::And: if (paccu > 1) A(2) = (ToInt(A(2)) & ToInt(A(1))) & workinterval; paccu--; break;
                case Op::Or: if (paccu > 1) A(2) = (ToInt(A(2)) | ToInt(A(1))) & workinterval; paccu--; break;
                case Op::Xor: if (paccu > 1) A(2) = (ToInt(A(2)) ^ ToInt(A(1))) & workinterval; paccu--; break;
                case Op::Mod:
                    if (paccu > 1)
                    {
                        const int divisor = ToInt(A(1)) & workinterval;
                        if (divisor == 0)
                        {
                            ExpError(didx, "division by zero (mod) in expression [" + Trad(ze) + "]");
                            A(2) = 0;
                        }
                        else
                        {
                            A(2) = (ToInt(A(2)) & workinterval) % divisor;
                        }
                    }
                    paccu--;
                    break;
                case Op::Shl:
                    if (paccu > 1)
                        A(2) = static_cast<double>(static_cast<int>(static_cast<unsigned>(ToInt(A(2))) << (ToInt(A(1)) & 31)));
                    if (shiftWarning(A(1)) && paccu > 1)
                        A(2) = 0;
                    paccu--;
                    break;
                case Op::Shr:
                    if (paccu > 1)
                        A(2) = ToInt(A(2)) >> (ToInt(A(1)) & 31);
                    if (shiftWarning(A(1)) && paccu > 1)
                        A(2) = 0;
                    paccu--;
                    break;
                case Op::BAnd: if (paccu > 1) A(2) = (ToInt(A(2)) && ToInt(A(1))) & workinterval; paccu--; break;
                case Op::BOr: if (paccu > 1) A(2) = (ToInt(A(2)) || ToInt(A(1))) & workinterval; paccu--; break;
                case Op::Lower: if (paccu > 1) A(2) = (ToInt(A(2)) & workinterval) < (ToInt(A(1)) & workinterval); paccu--; break;
                case Op::LowerEq: if (paccu > 1) A(2) = (ToInt(A(2)) & workinterval) <= (ToInt(A(1)) & workinterval); paccu--; break;
                case Op::Equal: if (paccu > 1) A(2) = (ToInt(A(2)) & workinterval) == (ToInt(A(1)) & workinterval); paccu--; break;
                case Op::NotEqual: if (paccu > 1) A(2) = (ToInt(A(2)) & workinterval) != (ToInt(A(1)) & workinterval); paccu--; break;
                case Op::Greater: if (paccu > 1) A(2) = (ToInt(A(2)) & workinterval) > (ToInt(A(1)) & workinterval); paccu--; break;
                case Op::GreaterEq: if (paccu > 1) A(2) = (ToInt(A(2)) & workinterval) >= (ToInt(A(1)) & workinterval); paccu--; break;
                case Op::Sin: if (paccu > 0) A(1) = ToInt(std::sin(A(1) * 3.1415926545 / 180.0)); break;
                case Op::Cos: if (paccu > 0) A(1) = ToInt(std::cos(A(1) * 3.1415926545 / 180.0)); break;
                case Op::Asin: if (paccu > 0) A(1) = ToInt(std::asin(A(1))) * 180.0 / 3.1415926545; break;
                case Op::Acos: if (paccu > 0) A(1) = ToInt(std::acos(A(1))) * 180.0 / 3.1415926545; break;
                case Op::Atan: if (paccu > 0) A(1) = ToInt(std::atan(A(1))) * 180.0 / 3.1415926545; break;
                case Op::Int: break;
                case Op::Floor: if (paccu > 0) A(1) = ToInt(std::floor(A(1))) & workinterval; break;
                case Op::Abs: if (paccu > 0) A(1) = ToInt(std::fabs(A(1))) & workinterval; break;
                case Op::Exp: if (paccu > 0) A(1) = ToInt(std::exp(A(1))) & workinterval; break;
                case Op::Ln: if (paccu > 0) A(1) = ToInt(std::log(A(1))) & workinterval; break;
                case Op::Log10: if (paccu > 0) A(1) = ToInt(std::log10(A(1))) & workinterval; break;
                case Op::Sqrt: if (paccu > 0) A(1) = ToInt(std::sqrt(A(1))) & workinterval; break;
                case Op::Low: if (paccu > 0) A(1) = ToInt(A(1)) & 0xFF; break;
                case Op::High: if (paccu > 0) A(1) = (ToInt(A(1)) & 0xFF00) >> 8; break;
                case Op::Psg: if (paccu > 0) A(1) = psgfine[static_cast<size_t>(ToInt(A(1)) & 0xFF)]; break;
                case Op::Rnd:
                    if (paccu > 0)
                    {
                        const int zemod = RoundInt(A(1));
                        if (zemod > 0)
                        {
                            A(1) = static_cast<double>(FastRand() % static_cast<unsigned>(zemod));
                        }
                        else
                        {
                            ExpError(didx, "RND function needs a value greater than zero to perform a random value");
                            A(1) = 0;
                        }
                    }
                    break;
                case Op::Frac: if (paccu > 0) A(1) = ToInt(A(1) - ToInt(A(1))); break;
                case Op::Ceil: if (paccu > 0) A(1) = ToInt(std::ceil(A(1))) & workinterval; break;
                case Op::GetR: if (paccu > 0) A(1) = (ToInt(A(1)) & 0xF0) >> 4; break;
                case Op::GetV: if (paccu > 0) A(1) = (ToInt(A(1)) & 0xF00) >> 8; break;
                case Op::GetB: if (paccu > 0) A(1) = ToInt(A(1)) & 0xF; break;
                case Op::SetR: if (paccu > 0) A(1) = MinMaxInt(A(1), 0, 15) << 4; break;
                case Op::SetV: if (paccu > 0) A(1) = MinMaxInt(A(1), 0, 15) << 8; break;
                case Op::SetB: if (paccu > 0) A(1) = MinMaxInt(A(1), 0, 15); break;
                case Op::Soft2Hard:
                    if (paccu > 0)
                    {
                        auto h = Soft2HardInk(A(1));
                        if (!h)
                            ExpError(didx, "SOFT2HARD_INK needs 0-26 color index");
                        A(1) = h ? *h : 0;
                    }
                    break;
                case Op::Hard2Soft: if (paccu > 0) A(1) = Hard2SoftInk(A(1)); break;
                case Op::Peek:
                    if (paccu > 0)
                    {
                        const auto& m = mem[static_cast<size_t>(activebank)];
                        const auto a = static_cast<uint16_t>(ToInt(A(1)));
                        A(1) = a < m.size() ? m[a] : 0;
                    }
                    break;
                case Op::Pow2: if (paccu > 0) A(1) = ToInt(std::pow(2, A(1))) & workinterval; break;
                case Op::Pow:
                    if (paccu > 1) { A(2) = ToInt(std::pow(A(2), A(1))) & workinterval; paccu--; } else twoParams("POW");
                    break;
                case Op::Fmod:
                    if (paccu > 1) { A(2) = ToInt(std::fmod(A(2), A(1))) & workinterval; paccu--; } else twoParams("FMOD");
                    break;
                case Op::Atan2:
                    if (paccu > 1) { A(2) = ToInt(std::atan2(A(2), A(1))) & workinterval; paccu--; } else twoParams("ATAN2");
                    break;
                case Op::Hypot:
                    if (paccu > 1) { A(2) = ToInt(std::hypot(A(2), A(1))) & workinterval; paccu--; } else twoParams("HYPOT");
                    break;
                case Op::Ldexp:
                    if (paccu > 1) { A(2) = ToInt(std::ldexp(A(2), ToInt(A(1)))) & workinterval; paccu--; } else twoParams("LDEXP");
                    break;
                case Op::Fdim:
                    if (paccu > 1) { A(2) = ToInt(std::fdim(A(2), A(1))) & workinterval; paccu--; } else twoParams("FDIM");
                    break;
                case Op::Fstep:
                    if (paccu > 1) { A(2) = A(2) <= A(1) ? 1 : 0; paccu--; } else twoParams("FSTEP");
                    break;
                case Op::Fmax:
                    if (paccu > 1) { A(2) = A(2) < A(1) ? ToInt(A(1)) & workinterval : ToInt(A(2)) & workinterval; paccu--; } else twoParams("FMAX");
                    break;
                case Op::Fmin:
                    if (paccu > 1) { A(2) = A(2) > A(1) ? ToInt(A(1)) & workinterval : ToInt(A(2)) & workinterval; paccu--; } else twoParams("FMIN");
                    break;
                case Op::Clamp:
                    if (paccu > 2)
                    {
                        if (A(3) < A(2))
                            A(3) = ToInt(A(2)) & workinterval;
                        else if (A(3) > A(1))
                            A(3) = ToInt(A(1)) & workinterval;
                        paccu -= 2;
                    }
                    else
                    {
                        ExpError(didx, "CLAMP requires 3 parameters (val,min,max)");
                    }
                    break;
                case Op::Lerp:
                    if (paccu > 2)
                    {
                        A(3) = ToInt(A(3) + A(1) * (A(2) - A(3))) & workinterval;
                        paccu -= 2;
                    }
                    else
                    {
                        ExpError(didx, "LERP requires 3 parameters (x,y,alpha)");
                    }
                    break;
                case Op::IsGreater:
                    if (paccu > 1) { A(2) = A(2) > A(1) ? 1 : 0; paccu--; } else twoParams("ISGREATER");
                    break;
                case Op::IsLess:
                    if (paccu > 1) { A(2) = A(2) < A(1) ? 1 : 0; paccu--; } else twoParams("ISLESS");
                    break;
                case Op::GetNop:
                case Op::GetTick:
                case Op::Duration:
                case Op::FileSize:
                case Op::GetSize:
                case Op::IsRegister:
                case Op::FileByte:
                case Op::CountNops:
                    stringOp(ce.op);
                    break;
                default:
                    ExpError(didx, "invalid computing state! (" + std::to_string(static_cast<int>(ce.op)) + ")");
                    paccu = 0;
            }
            if (!paccu)
            {
                if (ze[0] == '&')
                    ExpError(didx, "Missing operand for calculation [" + Trad(ze) + "] Did you use & for an hexadecimal value?");
                else
                    ExpError(didx, "Missing operand for calculation [" + Trad(ze) + "] at operator or function [" +
                                       OperatorName(ce.op) + "]");
                accuErr = 1;
                break;
            }
        }
    }
    else
    {
        for (int i = 0; i < nbcomputestack; i++)
        {
            const auto& ce = computestack[static_cast<size_t>(i)];
            switch (ce.op)
            {
                case Op::PushData:
                    accu[static_cast<size_t>(paccu)] = ce.string ? i + 0.1 : ce.value;
                    paccu++;
                    break;
                case Op::Open:
                case Op::Close:
                    break;
                case Op::Add: if (paccu > 1) A(2) += A(1); paccu--; break;
                case Op::Sub: if (paccu > 1) A(2) -= A(1); paccu--; break;
                case Op::Mul: if (paccu > 1) A(2) *= A(1); paccu--; break;
                case Op::Div:
                    if (paccu > 1)
                    {
                        if (A(1) == 0)
                        {
                            ExpError(didx, "division by zero in expression [" + Trad(ze) + "]");
                            A(2) = 0;
                        }
                        else
                        {
                            A(2) /= A(1);
                        }
                    }
                    paccu--;
                    break;
                case Op::IntDiv:
                    if (paccu > 1)
                    {
                        const int divisor = ToInt(A(1));
                        if (divisor == 0)
                        {
                            ExpError(didx, "division by zero in expression [" + Trad(ze) + "]");
                            A(2) = 0;
                        }
                        else
                        {
                            A(2) = ToInt(A(2)) / divisor;
                        }
                    }
                    paccu--;
                    break;
                case Op::And: if (paccu > 1) A(2) = RoundInt(A(2)) & RoundInt(A(1)); paccu--; break;
                case Op::Or: if (paccu > 1) A(2) = RoundInt(A(2)) | RoundInt(A(1)); paccu--; break;
                case Op::Xor: if (paccu > 1) A(2) = RoundInt(A(2)) ^ RoundInt(A(1)); paccu--; break;
                case Op::Not: if (paccu > 0) A(1) = !RoundInt(A(1)); break;
                case Op::Mod:
                    if (paccu > 1)
                    {
                        const int divisor = RoundInt(A(1));
                        if (divisor == 0)
                        {
                            ExpError(didx, "division by zero (mod) in expression [" + Trad(ze) + "]");
                            A(2) = 0;
                        }
                        else
                        {
                            A(2) = RoundInt(A(2)) % divisor;
                        }
                    }
                    paccu--;
                    break;
                case Op::Shl:
                    if (paccu > 1)
                        A(2) = static_cast<double>(
                            static_cast<int>(static_cast<unsigned>(RoundInt(A(2))) << (RoundInt(A(1)) & 31)));
                    if (shiftWarning(A(1)) && paccu > 1)
                        A(2) = 0;
                    paccu--;
                    break;
                case Op::Shr:
                    if (paccu > 1)
                        A(2) = RoundInt(A(2)) >> (RoundInt(A(1)) & 31);
                    if (shiftWarning(A(1)) && paccu > 1)
                        A(2) = 0;
                    paccu--;
                    break;
                case Op::BAnd: if (paccu > 1) A(2) = RoundInt(A(2)) && RoundInt(A(1)); paccu--; break;
                case Op::BOr: if (paccu > 1) A(2) = RoundInt(A(2)) || RoundInt(A(1)); paccu--; break;
                case Op::Lower: if (paccu > 1) A(2) = A(2) < A(1); paccu--; break;
                case Op::LowerEq: if (paccu > 1) A(2) = A(2) <= A(1); paccu--; break;
                case Op::Equal: if (paccu > 1) A(2) = std::fabs(A(2) - A(1)) < 0.000001; paccu--; break;
                case Op::NotEqual: if (paccu > 1) A(2) = A(2) != A(1); paccu--; break;
                case Op::Greater: if (paccu > 1) A(2) = A(2) > A(1); paccu--; break;
                case Op::GreaterEq: if (paccu > 1) A(2) = A(2) >= A(1); paccu--; break;
                case Op::Sin: if (paccu > 0) A(1) = std::sin(A(1) * 3.1415926545 / 180.0); break;
                case Op::Cos: if (paccu > 0) A(1) = std::cos(A(1) * 3.1415926545 / 180.0); break;
                case Op::Asin: if (paccu > 0) A(1) = std::asin(A(1)) * 180.0 / 3.1415926545; break;
                case Op::Acos: if (paccu > 0) A(1) = std::acos(A(1)) * 180.0 / 3.1415926545; break;
                case Op::Atan: if (paccu > 0) A(1) = std::atan(A(1)) * 180.0 / 3.1415926545; break;
                case Op::Int: if (paccu > 0) A(1) = std::floor(A(1) + 0.5); break;
                case Op::Floor: if (paccu > 0) A(1) = std::floor(A(1)); break;
                case Op::Abs: if (paccu > 0) A(1) = std::fabs(A(1)); break;
                case Op::Exp: if (paccu > 0) A(1) = std::exp(A(1)); break;
                case Op::Ln: if (paccu > 0) A(1) = std::log(A(1)); break;
                case Op::Log10: if (paccu > 0) A(1) = std::log10(A(1)); break;
                case Op::Sqrt: if (paccu > 0) A(1) = std::sqrt(A(1)); break;
                case Op::Low: if (paccu > 0) A(1) = RoundInt(A(1)) & 0xFF; break;
                case Op::High: if (paccu > 0) A(1) = (RoundInt(A(1)) & 0xFF00) >> 8; break;
                case Op::Psg: if (paccu > 0) A(1) = psgfine[static_cast<size_t>(RoundInt(A(1)) & 0xFF)]; break;
                case Op::Rnd:
                    if (paccu > 0)
                    {
                        const int zemod = RoundInt(A(1));
                        if (zemod > 0)
                        {
                            A(1) = static_cast<double>(FastRand() % static_cast<unsigned>(zemod));
                        }
                        else
                        {
                            ExpError(didx, "RND function requires a value greater than zero to perform a random value");
                            A(1) = 0;
                        }
                    }
                    break;
                case Op::Frac:
                    if (paccu > 0)
                    {
                        double whole;
                        A(1) = std::modf(A(1), &whole);
                    }
                    break;
                case Op::Ceil: if (paccu > 0) A(1) = std::ceil(A(1)); break;
                case Op::GetR: if (paccu > 0) A(1) = (ToInt(A(1)) & 0xF0) >> 4; break;
                case Op::GetV: if (paccu > 0) A(1) = (ToInt(A(1)) & 0xF00) >> 8; break;
                case Op::GetB: if (paccu > 0) A(1) = ToInt(A(1)) & 0xF; break;
                case Op::SetR: if (paccu > 0) A(1) = MinMaxInt(A(1), 0, 15) << 4; break;
                case Op::SetV: if (paccu > 0) A(1) = MinMaxInt(A(1), 0, 15) << 8; break;
                case Op::SetB: if (paccu > 0) A(1) = MinMaxInt(A(1), 0, 15); break;
                case Op::Soft2Hard:
                    if (paccu > 0)
                    {
                        auto h = Soft2HardInk(A(1));
                        if (!h)
                            ExpError(didx, "SOFT2HARD_INK needs 0-26 color index");
                        A(1) = h ? *h : 0;
                    }
                    break;
                case Op::Hard2Soft: if (paccu > 0) A(1) = Hard2SoftInk(A(1)); break;
                case Op::Peek:
                    if (paccu > 0)
                    {
                        const auto& m = mem[static_cast<size_t>(activebank)];
                        const auto a = static_cast<uint16_t>(ToInt(A(1)));
                        A(1) = a < m.size() ? m[a] : 0;
                    }
                    break;
                case Op::Pow2: if (paccu > 0) A(1) = std::pow(2.0, A(1)); break;
                case Op::Pow:
                    if (paccu > 1) { A(2) = std::pow(A(2), A(1)); paccu--; } else twoParams("POW");
                    break;
                case Op::Fmod:
                    if (paccu > 1) { A(2) = std::fmod(A(2), A(1)); paccu--; } else twoParams("FMOD");
                    break;
                case Op::Atan2:
                    if (paccu > 1) { A(2) = std::atan2(A(2), A(1)); paccu--; } else twoParams("ATAN2");
                    break;
                case Op::Hypot:
                    if (paccu > 1) { A(2) = std::hypot(A(2), A(1)); paccu--; } else twoParams("HYPOT");
                    break;
                case Op::Ldexp:
                    if (paccu > 1) { A(2) = std::ldexp(A(2), ToInt(A(1))); paccu--; } else twoParams("LDEXP");
                    break;
                case Op::Fdim:
                    if (paccu > 1) { A(2) = std::fdim(A(2), A(1)); paccu--; } else twoParams("FDIM");
                    break;
                case Op::Fstep:
                    if (paccu > 1) { A(2) = A(2) <= A(1) ? 1 : 0; paccu--; } else twoParams("FSTEP");
                    break;
                case Op::Fmax:
                    if (paccu > 1) { A(2) = A(2) < A(1) ? A(1) : A(2); paccu--; } else twoParams("FMAX");
                    break;
                case Op::Fmin:
                    if (paccu > 1) { A(2) = A(2) > A(1) ? A(1) : A(2); paccu--; } else twoParams("FMIN");
                    break;
                case Op::Clamp:
                    if (paccu > 2)
                    {
                        if (A(3) < A(2))
                            A(3) = A(2);
                        else if (A(3) > A(1))
                            A(3) = A(1);
                        paccu -= 2;
                    }
                    else
                    {
                        ExpError(didx, "CLAMP requires 3 parameters (val,min,max)");
                    }
                    break;
                case Op::Lerp:
                    if (paccu > 2)
                    {
                        A(3) = A(3) + A(1) * (A(2) - A(3));
                        paccu -= 2;
                    }
                    else
                    {
                        ExpError(didx, "LERP requires 3 parameters (x,y,alpha)");
                    }
                    break;
                case Op::IsGreater:
                    if (paccu > 1) { A(2) = A(2) > A(1) ? 1 : 0; paccu--; } else twoParams("ISGREATER");
                    break;
                case Op::IsLess:
                    if (paccu > 1) { A(2) = A(2) < A(1) ? 1 : 0; paccu--; } else twoParams("ISLESS");
                    break;
                case Op::GetNop:
                case Op::GetTick:
                case Op::Duration:
                case Op::FileSize:
                case Op::GetSize:
                case Op::IsRegister:
                case Op::FileByte:
                case Op::CountNops:
                    stringOp(ce.op);
                    break;
                default:
                    ExpError(didx, "invalid computing state! (" + std::to_string(static_cast<int>(ce.op)) + ")");
                    paccu = 0;
            }
            if (!paccu)
            {
                if (ze[0] == '&')
                    ExpError(didx, "Missing operand for calculation [" + Trad(ze) + "] Did you use & for an hexadecimal value?");
                else
                    ExpError(didx, "Missing operand for calculation [" + Trad(ze) + "] at operator or function [" +
                                       OperatorName(ce.op) + "]");
                accuErr = 1;
                break;
            }
        }
    }
    if (parenth)
        ExpError(didx, "missing parenthesis in expression");
    if (paccu == 1)
        return accu[0];
    if (!accuErr)
    {
        if (paccu)
            ExpError(didx, "Missing operator");
        else
            ExpError(didx, "Missing operand for calculation");
    }
    return 0;
}

int Engine::RoundComputeExpressionCore(std::string& expr, int ptr, int didx)
{
    return ToInt(std::floor(ComputeExpressionCore(expr, ptr, didx) + static_cast<double>(rough)));
}

// ---------------------------------------------------------------------------------------------------------------------
// ComputeExpression

double Engine::ComputeExpression(std::string& expr, int ptr, int didx, int expectedEval)
{
    size_t i = 0;
    while (!automateExpressionDecision[U(At(expr, i))])
        i++;
    switch (automateExpressionDecision[U(At(expr, i))])
    {
        case '~':
        {
            // alias (EQU)
            const std::string name = expr.substr(0, i);
            const std::string text = expr.substr(i + 1);
            Alias a;
            int touched = 0;
            if (At(name, 0) == '@' || At(name, 0) == '.')
            {
                a.alias = TranslateTag(name, &touched, 0);
                a.alias = MakeLocalLabel(a.alias, nullptr);
            }
            else if (name.find('{') != std::string::npos)
            {
                a.alias = TranslateTag(name, &touched, 0);
            }
            else
            {
                a.alias = name;
            }
            if (At(a.alias, 0) != '@' && hasModule && !module.empty())
                a.alias = module + moduleSeparator + a.alias;
            a.ptr = codeadr;
            a.lz = static_cast<int>(lzsection.size());
            if (IsMath(a.alias))
            {
                MakeError(idx, "Alias cannot be a math function [" + name + "]");
            }
            else if (IsRegister(a.alias))
            {
                MakeError(idx, "Alias cannot be a register [" + name + "]");
            }
            else if (SearchLabel(a.alias))
            {
                MakeError(idx, "Alias cannot override existing label [" + name + "]");
            }
            else if (SearchDico(a.alias))
            {
                MakeError(idx, "Alias cannot override existing variable [" + name + "]");
            }
            else
            {
                a.translation = "(" + text + ")";
                ExpressionFastTranslate(a.translation, 2);
                a.autoriseExport = autoriseExport != 0;
                a.iw = idx;
                if (aliasTree.count(a.alias))
                {
                    MakeError(idx, "Duplicate alias [" + name + "]");
                }
                else
                {
                    bool ok = true;
                    for (const auto& token : GetTokens(a.translation))
                    {
                        if (token == a.alias)
                        {
                            MakeError(idx, "Alias are not variables, use = instead of EQU");
                            ok = false;
                        }
                    }
                    // rasm inserts the alias in its tree before this check: a refused alias still occupies its name
                    aliasTree.emplace(a.alias, alias.size());
                    alias.push_back(a);
                    if (ok)
                    {
                        Alias& added = alias.back();
                        for (size_t k = 0; k + 1 < alias.size(); k++)
                        {
                            if (added.translation.find(alias[k].alias) != std::string::npos)
                            {
                                ExpressionFastTranslate(added.translation, 2);
                                break;
                            }
                        }
                    }
                    else
                    {
                        alias.back().translation.clear();
                    }
                }
            }
            return 0;
        }
        case '=':
        {
            // a variable assignment when no other comparison char follows (or '!')
            if (automateExpressionDecision[U(At(expr, i + 1))] == 0 || At(expr, i + 1) == '!')
            {
                if (expectedEval)
                {
                    if (!maxam)
                    {
                        MakeError(idx, "meaningless use of an expression [" + expr + "]");
                        return 0;
                    }
                    break;   // Maxam: a single '=' is a comparison
                }
                if (!automateValidVarFirst[U(At(expr, 0))])
                {
                    MakeError(idx, "variable name must begin by a letter or '_' [" + expr + "]");
                    return 0;
                }
                std::string dblexp = expr.substr(i + 1);
                ExpressionFastTranslate(dblexp, 3);
                const double v = ComputeExpressionCore(dblexp, ptr, didx);
                char operatorassignment = 0;
                size_t nameEnd = i;
                if (i > 0)
                {
                    switch (expr[i - 1])
                    {
                        case '+': case '-': case '*': case '/': case '^': case '&': case '|': case '%': case ']':
                        case '[':
                            operatorassignment = expr[i - 1];
                            nameEnd = i - 1;
                            break;
                        default:
                            break;
                    }
                }
                std::string dexpr = expr.substr(0, nameEnd);
                if (dexpr.find('{') != std::string::npos)
                {
                    int touched = 0;
                    dexpr = TranslateTag(dexpr, &touched, 0);
                }
                DicoVar* curdic = SearchDico(dexpr);
                if (curdic)
                {
                    switch (operatorassignment)
                    {
                        case 0: curdic->v = v; break;
                        case '+': curdic->v += v; break;
                        case '-': curdic->v -= v; break;
                        case '*': curdic->v *= v; break;
                        case '/': curdic->v /= v; break;
                        case '|': curdic->v = ToInt(curdic->v) | ToInt(v); break;
                        case '&': curdic->v = ToInt(curdic->v) & ToInt(v); break;
                        case '^': curdic->v = ToInt(curdic->v) ^ ToInt(v); break;
                        case '%':
                            curdic->v = ToInt(v) ? ToInt(curdic->v) % ToInt(v) : 0;
                            break;
                        case ']':
                            curdic->v = ToInt(curdic->v) >> (ToInt(v) & 31);
                            if (v > 31 || v < -31)
                            {
                                MakeWarning(idx, "shifting " + std::to_string(ToInt(v)) +
                                                     " is architecture dependant, result forced to ZERO");
                                curdic->v = 0;
                            }
                            break;
                        case '[':
                            curdic->v = static_cast<int>(static_cast<unsigned>(ToInt(curdic->v)) << (ToInt(v) & 31));
                            if (v > 31 || v < -31)
                            {
                                MakeWarning(idx, "shifting " + std::to_string(ToInt(v)) +
                                                     " is architecture dependant, result forced to ZERO");
                                curdic->v = 0;
                            }
                            break;
                        default:
                            break;
                    }
                }
                else
                {
                    if (operatorassignment)
                    {
                        MakeError(idx, "Cannot do an operator assignment on non existing variable [" + expr + "]");
                        return 0;
                    }
                    if (SearchAlias(dexpr))
                    {
                        MakeError(idx, "Variable cannot override existing alias [" + expr + "]");
                        return 0;
                    }
                    if (SearchLabel(dexpr))
                    {
                        MakeError(idx, "Variable cannot override existing label [" + expr + "]");
                        return 0;
                    }
                    ExpressionSetDicoVar(dexpr, v);
                }
                return v;
            }
            break;
        }
        default:
            break;
    }
    std::string copy = expr;
    ExpressionFastTranslate(copy, 2);
    return ComputeExpressionCore(copy, ptr, didx);
}

int Engine::RoundComputeExpression(std::string& expr, int ptr, int didx, int expectedEval)
{
    return ToInt(std::floor(ComputeExpression(expr, ptr, didx, expectedEval) + static_cast<double>(rough)));
}

// ---------------------------------------------------------------------------------------------------------------------
// ExpressionFastTranslate: substitutes what is known now ($, variables, aliases, local label hashes) in an expression
// fullreplace 0: aliases only; 1: deferred expression; 2: EQU / plain evaluation; 3: assignment (no variables)

void Engine::ExpressionFastTranslate(std::string& expr, int fullreplace)
{
    size_t idx3 = 0;
    while (!automateExpressionDecision[U(At(expr, idx3))])
        idx3++;
    if (!maxam)
    {
        if (At(expr, idx3) == '~' || (At(expr, idx3) == '=' && At(expr, idx3 + 1) != '='))
            idx3++;
        else
            idx3 = 0;
    }
    else
    {
        if (At(expr, idx3) == '~')
            idx3++;
        else
            idx3 = 0;
    }
    size_t i = idx3;
    std::string varbuffer;
    int ivar = 0;
    int curly = 0;
    int curlyflag = 0;
    int recurse = -1;
    int recursecount = 0;
    char c;
    auto replaceVar = [&](size_t startvar, const std::string& text) {
        expr.replace(startvar, static_cast<size_t>(ivar), text);
    };
    while ((c = At(expr, i)) != 0)
    {
        size_t startvar = i;
        switch (c)
        {
            case '"':
            case '\'':
                if (At(expr, i + 1) == '\\')
                {
                    if (At(expr, i + 2) && At(expr, i + 3) == c)
                    {
                        char e = expr[i + 2];
                        switch (e)
                        {
                            case 'b': e = '\b'; break;
                            case 'v': e = '\v'; break;
                            case 'f': e = '\f'; break;
                            case '0': e = '\0'; break;
                            case 'r': e = '\r'; break;
                            case 'n': e = '\n'; break;
                            case 't': e = '\t'; break;
                            default: break;
                        }
                        expr.replace(i, 4, HexOfChar(e, 3).substr(0, 4));
                        i += 3;
                    }
                    else
                    {
                        i++;
                        while (At(expr, i) && At(expr, i) != c)
                            i += expr[i] == '\\' ? 2 : 1;
                    }
                }
                else if (At(expr, i + 1) && At(expr, i + 2) == c)
                {
                    if (!(i >= 12 && expr.compare(i - 12, 12, "IS_REGISTER(") == 0))
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "#%02X", charset[U(expr[i + 1])]);
                        expr.replace(i, 3, buf);
                    }
                    i += 2;
                }
                else
                {
                    i++;
                    while (At(expr, i) && At(expr, i) != c)
                        i += expr[i] == '\\' ? 2 : 1;
                }
                i++;
                ivar = 0;
                break;
            case '!': case '=': case '>': case '<': case '(': case ')': case ']': case '[': case '*': case '/':
            case '\\': case '+': case '~': case '-': case '^': case 'm': case '|': case '&': case ',':
                i++;
                break;
            default:
                startvar = i;
                varbuffer.clear();
                if (automateExpressionValidCharFirst[U(c)])
                {
                    varbuffer += c;
                    if (c == '{')
                        curly++;
                    i++;
                    c = At(expr, i);
                    const std::array<char, 256>* automate = &automateExpressionValidChar;
                    while ((*automate)[U(c)])
                    {
                        if (c == '{')
                        {
                            curly++;
                            curlyflag = 1;
                            automate = &automateExpressionValidCharExtended;
                        }
                        else if (c == '}')
                        {
                            curly--;
                            if (!curly)
                                automate = &automateExpressionValidChar;
                        }
                        varbuffer += c;
                        i++;
                        c = At(expr, i);
                    }
                }
                ivar = static_cast<int>(varbuffer.size());
                if (!ivar)
                {
                    MakeError(idx, "invalid expression [" + expr + "] c=[" + std::string(1, c) + "] idx=" + std::to_string(i));
                    return;
                }
                if (curly)
                {
                    MakeError(idx, "wrong curly brackets in expression [" + expr + "]");
                    return;
                }
        }

        // nothing beginning with a digit is translated
        if (ivar && (varbuffer[0] < '0' || varbuffer[0] > '9'))
        {
            if (curlyflag)
            {
                int touched = 0;
                const std::string mini = TranslateTag(varbuffer, &touched, fullreplace ? 0 : 2);
                expr.replace(startvar, static_cast<size_t>(ivar), mini);
                varbuffer = mini;
                i = startvar + mini.size();
                curlyflag = 0;
                ivar = static_cast<int>(mini.size());
            }
            bool foundReplace = false;
            if (fullreplace)
            {
                if (varbuffer == "$$")
                {
                    if (lz == -1)
                    {
                        const std::string val = std::to_string(outputadr);
                        replaceVar(startvar, val);
                        i = startvar + val.size();
                        ivar = 0;
                    }
                    foundReplace = true;
                }
                else if (varbuffer == "$")
                {
                    if (lz == -1)
                    {
                        const std::string val = std::to_string(insideORG ? outputadr : codeadr);
                        replaceVar(startvar, val);
                        i = startvar + val.size();
                        ivar = 0;
                    }
                    foundReplace = true;
                }
                else if (fullreplace != 3)
                {
                    if (DicoVar* curdic = SearchDico(varbuffer))
                    {
                        curdic->used = true;
                        const std::string val = FormatDouble(curdic->v);
                        replaceVar(startvar, val);
                        i = startvar + val.size();
                        ivar = 0;
                        foundReplace = true;
                    }
                }
            }
            if (!foundReplace)
            {
                Alias* curalias = nullptr;
                if (hasModule)
                    curalias = SearchAlias(module + moduleSeparator + varbuffer);
                if (curalias || (curalias = SearchAlias(varbuffer)) != nullptr)
                {
                    curalias->used = true;
                    if (recurse <= static_cast<int>(startvar))
                    {
                        if (recursecount > static_cast<int>(alias.size()))
                        {
                            const size_t tilde = expr.find('~');
                            MakeError(idx, "alias definition of " + (tilde == std::string::npos ? expr : expr.substr(0, tilde)) +
                                               " has infinite recursivity");
                            expr.clear();
                            return;
                        }
                        recursecount++;
                    }
                    const std::string translation = curalias->translation;
                    replaceVar(startvar, translation);
                    foundReplace = true;
                    recurse = static_cast<int>(startvar);
                    i = startvar;
                    ivar = 0;
                }
            }
            if (!foundReplace)
            {
                if (fullreplace)
                {
                    if (varbuffer[0] == '@' && (At(varbuffer, 1) < '0' || At(varbuffer, 1) > '9'))
                    {
                        int dek = 0;
                        const std::string hash = MakeLocalLabel(varbuffer, &dek);
                        const size_t dot = varbuffer.find('.');
                        if (dot != std::string::npos)
                            expr.insert(startvar + dot, hash);   // far proximity access: the hash before the dot
                        else
                            expr.insert(startvar + varbuffer.size(), hash);
                        i += static_cast<size_t>(dek);
                        foundReplace = true;
                    }
                    else if (varbuffer[0] == '.' && (At(varbuffer, 1) < '0' || At(varbuffer, 1) > '9'))
                    {
                        // proximity label (not marked as replaced: it is also recorded as used)
                        int dek = 0;
                        const std::string full = MakeLocalLabel(varbuffer, &dek);
                        expr.replace(startvar, varbuffer.size(), full);
                        i = i + full.size() - varbuffer.size();
                    }
                }
                if (varbuffer[0] == '{')
                {
                    size_t tagoffset;
                    if (StartsWith(varbuffer, 0, "{BANK}") || StartsWith(varbuffer, 0, "{PAGE}"))
                        tagoffset = 6;
                    else if (StartsWith(varbuffer, 0, "{PAGESET}"))
                        tagoffset = 9;
                    else if (StartsWith(varbuffer, 0, "{SIZEOF}"))
                        tagoffset = 8;
                    else
                    {
                        tagoffset = 0;
                        MakeError(idx, "Unknown prefix tag");
                    }
                    const char t = At(varbuffer, tagoffset);
                    if (t == '.')
                    {
                        const std::string rest = varbuffer.substr(tagoffset);
                        const std::string full = MakeLocalLabel(rest, nullptr);
                        // rasm inserts the start of the translated name (its length difference) before the name
                        const size_t lenadd = full.size() - rest.size();
                        expr.insert(startvar + tagoffset, full.substr(0, lenadd));
                        foundReplace = true;
                        i += lenadd;
                    }
                    else if (t == '@')
                    {
                        const std::string rest = varbuffer.substr(tagoffset);
                        int dek = 0;
                        const std::string hash = MakeLocalLabel(rest, &dek);
                        const size_t dot = rest.find('.');
                        if (dot != std::string::npos)
                            expr.insert(startvar + tagoffset + dot + tagoffset, hash);   // rasm's dotpos is from the tag
                        else
                            expr.insert(startvar + tagoffset + rest.size(), hash);
                        i += static_cast<size_t>(dek);
                        foundReplace = true;
                    }
                    else if (t == '$')
                    {
                        int tagvalue = -1;
                        if (varbuffer == "{BANK}$")
                        {
                            if (forcecpr)
                            {
                                if (activebank < 32)
                                {
                                    tagvalue = activebank;
                                }
                                else
                                {
                                    MakeError(idx, "expression [" + Trad(expr) + "] cannot use BANK $ in a temporary space!");
                                    tagvalue = 0;
                                }
                            }
                            else if (forcesnapshot)
                            {
                                if (activebank < kBankMaxNumber)
                                    tagvalue = bankset[static_cast<size_t>(activebank >> 2)] ? activebank + (codeadr >> 14)
                                                                                             : activebank;
                                else
                                {
                                    MakeError(idx, "expression [" + Trad(expr) + "] cannot use BANK $ in a temporary space!");
                                    tagvalue = 0;
                                }
                            }
                        }
                        else if (varbuffer == "{PAGE}$")
                        {
                            if (forcecpr)
                                MakeError(idx, "expression [" + Trad(expr) + "] cannot use PAGE in cartridge mode");
                            if (activebank < kBankMaxNumber)
                                tagvalue = bankset[static_cast<size_t>(activebank >> 2)]
                                               ? bankgate[static_cast<size_t>((activebank & 0x1FC) + (codeadr >> 14))]
                                               : bankgate[static_cast<size_t>(activebank)];
                            else
                            {
                                MakeError(idx, "expression [" + Trad(expr) + "] cannot use PAGE $ in a temporary space!");
                                tagvalue = activebank;
                            }
                        }
                        else if (varbuffer == "{PAGESET}$")
                        {
                            if (forcecpr)
                                MakeError(idx, "expression [" + Trad(expr) + "] cannot use PAGESET in cartridge mode");
                            if (activebank < kBankMaxNumber)
                                tagvalue = setgate[static_cast<size_t>(activebank)];
                            else
                            {
                                MakeError(idx, "expression [" + Trad(expr) + "] cannot use PAGESET $ in a temporary space!");
                                tagvalue = activebank;
                            }
                        }
                        const std::string val = std::to_string(tagvalue);
                        replaceVar(startvar, val);
                        i = startvar + val.size();
                        ivar = 0;
                        foundReplace = true;
                    }
                }
            }
            // unknown symbol: recorded as used (IFUSED)
            if (!foundReplace && automateValidLabelFirst[U(varbuffer[0])])
                InsertUsedToTree(varbuffer);
        }
        ivar = 0;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// labels

void Engine::PushLabelLight(Label curlabel)
{
    curlabel.backidx = static_cast<int>(label.size());
    curlabel.localExport = localExport != 0;
    curlabel.autoriseExport = autoriseExport && !getstruct;
    curlabel.makeAlias = getstruct != 0;
    const std::string name = curlabel.name;
    if (!InsertLabelToTree(curlabel))
        MakeError(idx, std::string(idx ? "Structure insertion" : "Label import") + " caused duplicate label [" + name + "]");
}

void Engine::PushLabel()
{
    Word& word = wl[static_cast<size_t>(idx)];
    const std::string& w = word.w;
    deadend = 0;
    size_t i = 1;
    bool tagmatch = false;
    if (automateValidLabelFirst[U(At(w, 0))])
    {
        while (At(w, i))
        {
            if (w[i] == '{')
            {
                tagmatch = true;
                int tagcount = 1;
                do
                {
                    i++;
                    if (At(w, i) == '}')
                        tagcount--;
                } while (tagcount && At(w, i));
                continue;
            }
            if (!automateValidLabel[U(w[i])])
            {
                MakeError(idx, std::string("Invalid char in label declaration (") + w[i] + ")");
                return;
            }
            i++;
        }
    }
    else
    {
        MakeError(idx, std::string("Invalid first char in label declaration (") + At(w, 0) + ")");
        return;
    }
    Label curlabel;
    if (i == 1)
    {
        switch (w[0])
        {
            case '_':
            {
                // anonymous proximity label (RASM Atlas)
                curProxIndex++;
                curlabel.name = "LPR" + std::to_string(curProxIndex) + "OX";
                curlabel.ptr = codeadr;
                curlabel.ibank = activebank;
                curlabel.iorgzone = static_cast<int>(orgzone.size()) - 1;
                curlabel.lz = lz;
                curlabel.fileidx = word.file;
                curlabel.fileline = word.line;
                curlabel.backidx = static_cast<int>(label.size());
                curlabel.nop = nop;
                curlabel.relocatable = relocblockactive != 0;
                if (!InsertLabelToTree(curlabel))
                    MakeError(idx, "Duplicate label [" + curlabel.name + "]");
                return;
            }
            case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'H': case 'L': case 'I': case 'R':
                MakeError(idx, "Cannot use reserved word [" + w + "] for label");
                return;
            default:
                break;
        }
    }
    else if (i == 2 || i == 3)
    {
        if (IsRegister(w) && w != "I" && w != "R")
        {
            MakeError(idx, "Cannot use reserved word [" + w + "] for label");
            return;
        }
    }
    std::string varbuffer = w;
    if (tagmatch)
    {
        int touched = 0;
        varbuffer = TranslateTag(varbuffer, &touched, 0);
    }
    if (getstruct)
    {
        if (At(varbuffer, 0) == '@')
        {
            MakeError(idx, "Please no local label in a struct [" + w + "]");
            return;
        }
        RasmStructField field;
        field.name = varbuffer;
        field.offset = codeadr;
        rasmstruct.back().fields.push_back(field);
        curlabel.name = rasmstruct.back().name + "." + varbuffer;
        curlabel.ptr = codeadr;
        curlabel.makeAlias = true;
        curlabel.lz = -1;
    }
    else
    {
        if (At(varbuffer, 0) == '@' && (!repeat.empty() || !whilewend.empty() || imacro))
        {
            curlabel.local = true;
            curlabel.name = MakeLocalLabel(varbuffer, nullptr);
            lastgloballabel = curlabel.name;   // local labels also set the reference of proximity labels
        }
        else
        {
            if (At(varbuffer, 0) == '.')
            {
                if (dams)
                    curlabel.name = varbuffer.substr(1);
                else if (!lastgloballabel.empty())
                    curlabel.name = lastgloballabel + varbuffer;
                else
                    curlabel.name = varbuffer;
            }
            else
            {
                curlabel.name = varbuffer;
                lastgloballabel = varbuffer;
            }
            if (At(curlabel.name, 0) != '@' && hasModule && !module.empty())
                curlabel.name = module + moduleSeparator + curlabel.name;
            if (SearchDico(curlabel.name))
            {
                MakeError(idx, "cannot create label [" + curlabel.name + "] as there is already a variable with the same name");
                return;
            }
            if (SearchAlias(curlabel.name))
            {
                MakeError(idx, "cannot create label [" + curlabel.name + "] as there is already an alias with the same name");
                return;
            }
        }
        curlabel.ptr = codeadr;
        curlabel.ibank = activebank;
        curlabel.iorgzone = static_cast<int>(orgzone.size()) - 1;
        curlabel.lz = lz;
    }
    curlabel.fileidx = word.file;
    curlabel.fileline = word.line;
    curlabel.localExport = localExport != 0;
    curlabel.autoriseExport = autoriseExport && !getstruct;
    curlabel.backidx = static_cast<int>(label.size());
    curlabel.nop = nop;
    curlabel.relocatable = relocblockactive != 0;
    if (!InsertLabelToTree(curlabel))
    {
        const Label* prev = SearchLabel(curlabel.name);
        std::string where;
        if (prev && prev->fileidx >= 0 && prev->fileidx < static_cast<int>(filename.size()))
            where = filename[static_cast<size_t>(prev->fileidx)] + ":" + std::to_string(prev->fileline);
        MakeError(idx, "Duplicate label [" + curlabel.name + "] - previously defined in [" + where + "]");
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// deferred expressions

void Engine::PushExpression(int iw, ExprKind kind)
{
    if (nocode)
    {
        switch (kind)
        {
            case ExprKind::J8: case ExprKind::V8At0: case ExprKind::V8: outputadr++; codeadr++; break;
            case ExprKind::J16: case ExprKind::V16: case ExprKind::V16At0: outputadr += 2; codeadr += 2; break;
            case ExprKind::V32At0: outputadr += 4; codeadr += 4; break;
            case ExprKind::RealAmsdos: case ExprKind::RealMicrosoft: outputadr += 5; codeadr += 5; break;
            case ExprKind::Float24: outputadr += 3; codeadr += 3; break;
            case ExprKind::IV8: case ExprKind::IV81: case ExprKind::V8At3: outputadr++; codeadr++; break;
            case ExprKind::IV16: outputadr += 2; codeadr += 2; break;
            case ExprKind::Rst: case ExprKind::RstConditional: case ExprKind::Im: outputadr++; codeadr++; break;
            default: break;
        }
        if (outputadr > maxptr)
        {
            if (!stop)
                MakeError(idx, "(PushExpr) NOCODE output exceed limit");
            stop = 1;
        }
        return;
    }
    Expression curexp;
    curexp.iw = iw;
    curexp.wptr = outputadr;
    curexp.kind = kind;
    curexp.ibank = activebank;
    curexp.iorgzone = static_cast<int>(orgzone.size()) - 1;
    curexp.lz = lz;
    curexp.wcodeadr = codeadr;
    curexp.relocblock = relocblockactive != 0;
    curexp.relocdollar = wl[static_cast<size_t>(iw)].w.find('$') != std::string::npos;
    if (hasModule)
        curexp.module = module;
    std::string& w = wl[static_cast<size_t>(iw)].w;
    char bakXY = 0;
    bool skipTranslate = false;
    switch (kind)
    {
        case ExprKind::J8:
        case ExprKind::J16:
            // anonymous proximity references _+ / _-
            if (At(w, 0) == '_' && (At(w, 1) == '+' || At(w, 1) == '-') && !At(w, 2))
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
                codeadr -= 1;
                skipTranslate = true;
                break;
            }
            [[fallthrough]];
        case ExprKind::V8:
        case ExprKind::V16:
        case ExprKind::Im:
            codeadr -= 1;   // `$` hack: the address of the instruction
            break;
        case ExprKind::IV8:
        case ExprKind::IV81:
            // IX / IY are not values: "(IX+5)" is evaluated as "(%0+5)"
            bakXY = At(w, 2);
            if (w.size() >= 3)
            {
                w[1] = '%';
                w[2] = '0';
            }
            [[fallthrough]];
        case ExprKind::IV16:
            codeadr -= 2;
            break;
        case ExprKind::V8At3:
            codeadr -= 3;
            break;
        default:
            break;
    }
    if (!skipTranslate)
    {
        if (!repeat.empty() || !whilewend.empty() || imacro)
        {
            curexp.reference = w;
            ExpressionFastTranslate(*curexp.reference, 1);
            if (bakXY)
            {
                w[1] = 'I';
                w[2] = bakXY;
            }
        }
        else
        {
            ExpressionFastTranslate(w, 1);   // translated in place: the word keeps it
        }
    }
    const int traceOut = outputadr, traceCode = codeadr;
    switch (kind)
    {
        case ExprKind::J8: curexp.ptr = codeadr; outputadr++; codeadr += 2; break;
        case ExprKind::V8At0: curexp.ptr = codeadr; outputadr++; codeadr++; break;
        case ExprKind::V8: curexp.ptr = codeadr; outputadr++; codeadr += 2; break;
        case ExprKind::J16:
        case ExprKind::V16: curexp.ptr = codeadr; outputadr += 2; codeadr += 3; break;
        case ExprKind::V16At0: curexp.ptr = codeadr; outputadr += 2; codeadr += 2; break;
        case ExprKind::V32At0: curexp.ptr = codeadr; outputadr += 4; codeadr += 4; break;
        case ExprKind::RealAmsdos:
        case ExprKind::RealMicrosoft: curexp.ptr = codeadr; outputadr += 5; codeadr += 5; break;
        case ExprKind::Float24: curexp.ptr = codeadr; outputadr += 3; codeadr += 3; break;
        case ExprKind::IV8:
        case ExprKind::IV81: curexp.ptr = codeadr; outputadr++; codeadr += 3; break;
        case ExprKind::V8At3: curexp.ptr = codeadr; outputadr++; codeadr += 4; break;
        case ExprKind::IV16: curexp.ptr = codeadr; outputadr += 2; codeadr += 4; break;
        case ExprKind::Rst:
        case ExprKind::RstConditional: curexp.ptr = codeadr; outputadr++; codeadr++; break;
        case ExprKind::Im: curexp.ptr = codeadr; outputadr++; codeadr += 2; break;
        case ExprKind::BitResSet: curexp.ptr = codeadr; break;
        default: break;
    }
    if (outputadr > traceOut)
        TraceBytes(traceOut, outputadr - traceOut, traceCode);
    if (outputadr > maxptr)
    {
        bool iscrunched = false;
        for (int k = static_cast<int>(lzsection.size()) - 1; k >= 0; k--)
        {
            if (lzsection[static_cast<size_t>(k)].ibank == activebank)
            {
                iscrunched = true;
                break;
            }
        }
        if (!iscrunched && !extended)
        {
            MakeError(idx, "output exceed limit");
            outputMode = OutputMode::Disabled;
            stop = 1;
            return;
        }
        if (iscrunched && (maxptr & 0xFFFF))
            MakeWarning(idx, "Specific limits are not applied when using crunched sections, cause memory blocks are moved unpredictably");
        if (!iscrunched)
            MakeWarning(idx, "physical output overflow 64k");
        const int requestedBlock = outputadr >> 16;
        maxptr = (requestedBlock + 1) * 65536;
        mem[static_cast<size_t>(activebank)].resize(static_cast<size_t>(maxptr), 0);
    }
    if ((kind == ExprKind::V8At0 || kind == ExprKind::V16At0) && !trace.empty() && trace.back().outputStart <= traceOut &&
        trace.back().outputEnd == outputadr)
    {
        TraceItem::Value v;
        v.offset = traceOut - trace.back().outputStart;
        v.size = kind == ExprKind::V8At0 ? 1 : 2;
        v.expression = curexp.reference ? *curexp.reference : wl[static_cast<size_t>(iw)].w;
        trace.back().values.push_back(std::move(v));
    }
    expression.push_back(std::move(curexp));
}

void Engine::PopAllExpression(int crunchedZone)
{
    size_t first;
    if (crunchedZone >= 0)
    {
        stage = 1;
        first = static_cast<size_t>(lzsection[static_cast<size_t>(crunchedZone)].iexpr);
    }
    else
    {
        stage = 2;
        first = 1;
    }
    for (size_t i = first; i < expression.size(); i++)
    {
        if (crunchedZone >= 0)
        {
            if (expression[i].lz != crunchedZone)
                break;
        }
        else if (expression[i].lz >= 0)
        {
            continue;
        }
        Expression& e = expression[i];
        std::vector<uint8_t>& m = mem[static_cast<size_t>(e.ibank)];
        std::string& text = e.reference ? *e.reference : wl[static_cast<size_t>(e.iw)].w;
        const double v = ComputeExpressionCore(text, e.ptr, static_cast<int>(i));
        long r = static_cast<long>(std::floor(v + static_cast<double>(rough)));
        const size_t wp = static_cast<size_t>(e.wptr);
        auto put = [&](size_t at, uint8_t b) {
            if (at < m.size())
                m[at] = b;
        };
        auto truncWarn = [&](long value, long mask) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "truncating value #%lX to #%lX", static_cast<unsigned long>(value),
                          static_cast<unsigned long>(value & mask));
            MakeWarning(e.iw, buf);
        };
        switch (e.kind)
        {
            case ExprKind::J8:
                r = r - e.ptr - 2;
                if (r < -128 || r > 127)
                    MakeError(e.iw, "relative offset " + std::to_string(r) + " too far [" + wl[static_cast<size_t>(e.iw)].w + "]");
                put(wp, static_cast<uint8_t>(r));
                break;
            case ExprKind::IV81:
                r++;
                [[fallthrough]];
            case ExprKind::IV8:
                if (r > 32767)
                    r -= 65536;
                if (r > 127 || r < -128)
                    MakeError(e.iw, "offset modifier must be between -128 and 127 inclusive : current value = " + std::to_string(r));
                put(wp, static_cast<uint8_t>(r));
                break;
            case ExprKind::V8At0:
            case ExprKind::V8At3:
            case ExprKind::V8:
                if (r > 255 || r < -128)
                    truncWarn(r, 0xFF);
                put(wp, static_cast<uint8_t>(r));
                break;
            case ExprKind::J16:
            case ExprKind::IV16:
            case ExprKind::V16:
            case ExprKind::V16At0:
                if (r > 65535 || r < -32768)
                    truncWarn(r, 0xFFFF);
                put(wp, static_cast<uint8_t>(r & 0xFF));
                put(wp + 1, static_cast<uint8_t>((r & 0xFF00) >> 8));
                break;
            case ExprKind::V32At0:
                if (v > 4294967295.0 || v < -2147483648.0)
                    MakeWarning(e.iw, "truncating value");
                put(wp, static_cast<uint8_t>(r & 0xFF));
                put(wp + 1, static_cast<uint8_t>((r >> 8) & 0xFF));
                put(wp + 2, static_cast<uint8_t>((r >> 16) & 0xFF));
                put(wp + 3, static_cast<uint8_t>((r >> 24) & 0xFF));
                break;
            case ExprKind::RealAmsdos:
            case ExprKind::RealMicrosoft:
            {
                const Real40 real = e.kind == ExprKind::RealAmsdos ? MakeAmsdosReal(v) : MakeMicrosoftReal(v);
                if (real.status == RealStatus::ExponentOverflow)
                    MakeError(e.iw, "Exponent overflow");
                else if (real.status == RealStatus::Hangs)
                    MakeError(e.iw, "invalid real value (rasm never returns on it)");
                for (size_t k = 0; k < 5; k++)
                    put(wp + k, real.bytes[k]);
                break;
            }
            case ExprKind::Float24:
            {
                const auto f = MakeFloat24(v);
                for (size_t k = 0; k < 3; k++)
                    put(wp + k, f[k]);
                break;
            }
            case ExprKind::Im:
                switch (r)
                {
                    case 0: put(wp, 0x46); break;
                    case 1: put(wp, 0x56); break;
                    case 2: put(wp, 0x5E); break;
                    default:
                        MakeError(e.iw, "IM 0,1 or 2 only");
                        put(wp, 0);
                }
                break;
            case ExprKind::RstConditional:
                if (r == 0x38)
                    put(wp, 0xFF);
                else
                    MakeError(e.iw, "RST condition,#38 only");
                break;
            case ExprKind::Rst:
                switch (r)
                {
                    case 0x00: put(wp, 0xC7); break;
                    case 0x08: put(wp, 0xCF); break;
                    case 0x10: put(wp, 0xD7); break;
                    case 0x18: put(wp, 0xDF); break;
                    case 0x20: put(wp, 0xE7); break;
                    case 0x28: put(wp, 0xEF); break;
                    case 0x30: put(wp, 0xF7); break;
                    case 0x38: put(wp, 0xFF); break;
                    default:
                        MakeError(e.iw, "RST #0,#8,#10,#18,#20,#28,#30,#38 only");
                        put(wp, 0);
                }
                break;
            case ExprKind::Run:
                if (r < 0 || r > 65535)
                    MakeWarning(e.iw, "run address truncated");
                snapshotRun = static_cast<int>(r & 0xFFFF);
                break;
            case ExprKind::ZxRun:
                if (r < 0 || r > 65535)
                    MakeWarning(e.iw, "run address truncated");
                zxsnapshot.run = static_cast<int>(r & 0xFFFF);
                break;
            case ExprKind::ZxStack:
                if (r < 0 || r > 65535)
                    MakeWarning(e.iw, "stack address truncated");
                zxsnapshot.stack = static_cast<int>(r & 0xFFFF);
                break;
            case ExprKind::BitResSet:
                if (r >= 0 && r < 8)
                {
                    if (wp < m.size())
                        m[wp] = static_cast<uint8_t>(m[wp] + r * 8);
                }
                else
                {
                    MakeError(e.iw, "SET,RES,BIT shift value from 0 to 7 only");
                }
                break;
        }
    }
}
}  // namespace unrealasm::rasm
