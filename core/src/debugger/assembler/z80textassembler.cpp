#include "stdafx.h"

#include "z80textassembler.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <sstream>

/// region <Anonymous helpers>

namespace
{

std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Trim(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) b++;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
    return s.substr(b, e - b);
}

bool IsIdentStart(char c)
{
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalpha(uc) || c == '_' || c == '.' || c == '@';
}

bool IsIdentChar(char c)
{
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '_' || c == '.' || c == '@';
}

// 8-bit register codes (B=0 C=1 D=2 E=3 H=4 L=5, 6=(HL)-slot, A=7); -1 = not a plain r8
int Reg8Code(const std::string& s)
{
    std::string l = ToLower(s);
    if (l == "b") return 0;
    if (l == "c") return 1;
    if (l == "d") return 2;
    if (l == "e") return 3;
    if (l == "h") return 4;
    if (l == "l") return 5;
    if (l == "a") return 7;
    return -1;
}

// 16-bit register/pair codes for the qq field (BC=0 DE=1 HL=2 AF=3); -1 = not a plain pair
int PairCode(const std::string& s)
{
    std::string l = ToLower(s);
    if (l == "bc") return 0;
    if (l == "de") return 1;
    if (l == "hl") return 2;
    if (l == "af" || l == "af'") return 3;
    return -1;
}

// 16-bit register codes for the ss field (BC=0 DE=1 HL=2 SP=3); -1 = not a plain rr
int Reg16Code(const std::string& s)
{
    std::string l = ToLower(s);
    if (l == "bc") return 0;
    if (l == "de") return 1;
    if (l == "hl") return 2;
    if (l == "sp") return 3;
    return -1;
}

// Condition codes (NZ=0 Z=1 NC=2 C=3 PO=4 PE=5 P=6 M=7); -1 = not a condition
int CondCode(const std::string& s)
{
    std::string l = ToLower(s);
    if (l == "nz") return 0;
    if (l == "z") return 1;
    if (l == "nc") return 2;
    if (l == "c") return 3;
    if (l == "po") return 4;
    if (l == "pe") return 5;
    if (l == "p") return 6;
    if (l == "m") return 7;
    return -1;
}

// All mnemonics/pseudo-ops supported by the encoder (lower-case)
bool IsMnemonic(const std::string& l)
{
    static const char* mnemonics[] = {
        "ld", "push", "pop", "ex", "exx",
        "add", "adc", "sub", "sbc", "and", "xor", "or", "cp",
        "inc", "dec",
        "rlca", "rla", "rrca", "rra",
        "rlc", "rl", "rrc", "rr", "sla", "sra", "srl",
        "bit", "set", "res",
        "jp", "jr", "djnz", "call", "ret", "reti", "retn", "rst",
        "in", "out", "im",
        "nop", "halt", "di", "ei", "daa", "cpl", "scf", "ccf", "neg",
        "ldi", "ldd", "ldir", "lddr", "cpi", "cpd", "cpir", "cpdr",
        "ini", "ind", "inir", "indr", "outi", "outd", "otir", "otdr",
        "org", "equ", "=", "db", "defb", "byte", "dw", "defw", "word",
        "ds", "defs", "block"
    };

    for (const char* m : mnemonics)
        if (l == m)
            return true;

    return false;
}

// Strip one level of surrounding parentheses; returns false when not balanced
bool StripParens(const std::string& s, std::string& inner)
{
    if (s.size() < 2 || s.front() != '(' || s.back() != ')')
        return false;

    int depth = 0;
    bool inString = false;
    char quote = 0;
    for (size_t i = 0; i < s.size(); i++)
    {
        char c = s[i];
        if (inString)
        {
            if (c == quote && (i == 0 || s[i - 1] != '\\'))
                inString = false;
            continue;
        }
        if (c == '\'' || c == '"')
        {
            inString = true;
            quote = c;
            continue;
        }
        if (c == '(') depth++;
        if (c == ')')
        {
            depth--;
            if (depth == 0 && i + 1 != s.size())
                return false;   // Closing paren is not the last character
        }
    }

    inner = s.substr(1, s.size() - 2);
    return depth == 0 && !inString;
}

// Parse an IX/IY index operand: "(ix)", "(ix+5)", "(iy-3)" etc.
// Returns 0xDD for IX, 0xFD for IY, 0 when not an index operand, -1 on syntax error
int ParseIndexOperand(const std::string& s, int32_t& displacement, bool& hasDisplacement)
{
    std::string inner;
    if (!StripParens(s, inner))
        return 0;

    std::string t = Trim(inner);
    if (t.size() < 2) return 0;
    std::string prefix = ToLower(t.substr(0, 2));
    if (prefix != "ix" && prefix != "iy")
        return 0;

    std::string rest = Trim(t.substr(2));
    hasDisplacement = false;
    displacement = 0;

    if (rest.empty())
        return prefix == "ix" ? 0xDD : 0xFD;

    if (rest[0] != '+' && rest[0] != '-')
        return -1;

    // Signed displacement expression (numeric-only inside index operands)
    displacement = 0;
    int sign = rest[0] == '-' ? -1 : 1;
    std::string expr = Trim(rest.substr(1));

    // Simple numeric-only displacement (expressions not supported inside index operands)
    int32_t value = 0;
    bool any = false;
    for (char c : expr)
    {
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return -1;
        value = value * 10 + (c - '0');
        any = true;
    }
    if (!any) return -1;

    displacement = sign * value;
    hasDisplacement = true;
    return prefix == "ix" ? 0xDD : 0xFD;
}

uint8_t Lo(int32_t v) { return static_cast<uint8_t>(v & 0xFF); }
uint8_t Hi(int32_t v) { return static_cast<uint8_t>((v >> 8) & 0xFF); }

} // anonymous namespace

/// endregion </Anonymous helpers>

/// region <Public API>

AsmResult Z80TextAssembler::Assemble(const std::string& source, uint16_t org)
{
    AsmResult result;
    result.startAddress = org;

    std::vector<LineParts> lines;
    if (!ParseSource(source, lines, result.error))
        return result;

    _symbols.clear();

    // Pass 1 — sizes and label addresses (expression values not needed)
    {
        uint32_t address = org;
        for (const auto& line : lines)
        {
            if (!line.label.empty() && ToLower(line.mnemonic) != "equ")
            {
                if (_symbols.count(line.label))
                {
                    result.error = { line.lineNumber, "Duplicate label: " + line.label, line.source };
                    return result;
                }
                _symbols[line.label] = address;
            }

            if (line.mnemonic.empty())
                continue;   // Label-only line — address already recorded

            if (line.mnemonic == "equ")
                continue;   // Size 0; value resolved in pass 2

            if (line.mnemonic == "org")
            {
                if (line.operands.size() != 1)
                {
                    result.error = { line.lineNumber, "ORG requires one operand", line.source };
                    return result;
                }
                int32_t v = 0;
                std::string err;
                if (!Evaluate(line.operands[0], static_cast<uint16_t>(address & 0xFFFF), v, err) || v < 0 || v > 0xFFFF)
                {
                    result.error = { line.lineNumber, err.empty() ? "ORG address out of range" : err, line.source };
                    return result;
                }
                address = static_cast<uint32_t>(v);
                continue;
            }

            std::vector<uint8_t> out;
            std::string error;
            if (!EncodeLine(line, static_cast<uint16_t>(address & 0xFFFF), 1, out, error))
            {
                result.error = { line.lineNumber, error, line.source };
                return result;
            }

            address += static_cast<uint32_t>(out.size());
            if (address > 0x10000)
            {
                result.error = { line.lineNumber, "Address overflow past 0xFFFF", line.source };
                return result;
            }
        }
    }

    // Pass 2 — full expression evaluation and emission
    {
        uint32_t address = org;
        for (const auto& line : lines)
        {
            if (line.mnemonic.empty())
            {
                AsmSourceLine entry;
                entry.address = static_cast<uint16_t>(address & 0xFFFF);
                entry.label = line.label;
                entry.source = line.source;
                result.lines.push_back(std::move(entry));
                continue;
            }

            if (line.mnemonic == "equ")
            {
                if (line.operands.size() != 1)
                {
                    result.error = { line.lineNumber, "EQU requires one operand", line.source };
                    return result;
                }
                int32_t value = 0;
                std::string error;
                if (!Evaluate(line.operands[0], static_cast<uint16_t>(address & 0xFFFF), value, error))
                {
                    result.error = { line.lineNumber, error, line.source };
                    return result;
                }
                if (!line.label.empty())
                    _symbols[line.label] = static_cast<uint32_t>(value);
                else
                {
                    result.error = { line.lineNumber, "EQU without a label", line.source };
                    return result;
                }

                AsmSourceLine entry;
                entry.address = static_cast<uint16_t>(address & 0xFFFF);
                entry.label = line.label;
                entry.source = line.source;
                result.lines.push_back(std::move(entry));
                continue;
            }

            if (line.mnemonic == "org")
            {
                int32_t v = 0;
                std::string err;
                if (!Evaluate(line.operands[0], static_cast<uint16_t>(address & 0xFFFF), v, err) || v < 0 || v > 0xFFFF)
                {
                    result.error = { line.lineNumber, err.empty() ? "ORG address out of range" : err, line.source };
                    return result;
                }
                address = static_cast<uint32_t>(v);
                continue;
            }

            std::vector<uint8_t> out;
            std::string error;
            if (!EncodeLine(line, static_cast<uint16_t>(address & 0xFFFF), 2, out, error))
            {
                result.error = { line.lineNumber, error, line.source };
                return result;
            }

            AsmSourceLine entry;
            entry.address = static_cast<uint16_t>(address & 0xFFFF);
            entry.label = line.label;
            entry.source = line.source;
            entry.bytes = out;
            result.lines.push_back(std::move(entry));

            result.bytes.insert(result.bytes.end(), out.begin(), out.end());
            address += static_cast<uint32_t>(out.size());
            if (address > 0x10000)
            {
                result.error = { line.lineNumber, "Address overflow past 0xFFFF", line.source };
                return result;
            }
        }

        result.endAddress = static_cast<uint16_t>(address & 0xFFFF);
    }

    result.symbols = _symbols;
    result.ok = true;
    return result;
}

/// endregion </Public API>

/// region <Source parsing>

bool Z80TextAssembler::ParseSource(const std::string& source, std::vector<LineParts>& lines, AsmErrorInfo& error)
{
    std::istringstream stream(source);
    std::string raw;
    int lineNumber = 0;

    while (std::getline(stream, raw))
    {
        lineNumber++;

        // Strip CR from CRLF and trailing whitespace
        if (!raw.empty() && raw.back() == '\r')
            raw.pop_back();

        // Strip comment (';') honoring quotes
        std::string text;
        bool inString = false;
        char quote = 0;
        for (char c : raw)
        {
            if (inString)
            {
                text += c;
                if (c == quote)
                    inString = false;
                continue;
            }
            if (c == '\'' || c == '"')
            {
                inString = true;
                quote = c;
                text += c;
                continue;
            }
            if (c == ';')
                break;
            text += c;
        }

        std::string trimmed = Trim(text);
        if (trimmed.empty())
            continue;

        LineParts parts;
        parts.lineNumber = lineNumber;
        parts.source = trimmed;

        // Label: "name:" or "name" followed by a mnemonic
        size_t pos = 0;
        while (pos < trimmed.size() && !std::isspace(static_cast<unsigned char>(trimmed[pos]))) pos++;
        std::string first = trimmed.substr(0, pos);

        std::string rest = Trim(trimmed.substr(pos));
        bool firstIsLabel = false;
        if (!first.empty() && first.back() == ':')
        {
            first.pop_back();
            firstIsLabel = !first.empty() && IsIdentStart(first[0]);
        }
        else if (!first.empty() && IsIdentStart(first[0]) && !IsMnemonic(ToLower(first)))
        {
            // Bare label without colon — only when followed by something or alone
            firstIsLabel = true;
        }

        if (firstIsLabel)
        {
            for (char c : first)
                if (!IsIdentChar(c))
                {
                    error = { lineNumber, "Invalid label name: " + first, trimmed };
                    return false;
                }
            parts.label = first;
            trimmed = rest;
        }

        if (trimmed.empty())
        {
            lines.push_back(parts);
            continue;
        }

        // Mnemonic
        pos = 0;
        while (pos < trimmed.size() && !std::isspace(static_cast<unsigned char>(trimmed[pos]))) pos++;
        parts.mnemonic = ToLower(trimmed.substr(0, pos));

        // EQU also accepts "name = expr"
        if (!parts.label.empty() && parts.mnemonic.empty() && !trimmed.empty() && trimmed[0] == '=')
            parts.mnemonic = "=";

        if (parts.mnemonic == "=")
            parts.mnemonic = "equ";

        if (parts.mnemonic.empty() || !IsMnemonic(parts.mnemonic))
        {
            error = { lineNumber, "Unknown mnemonic: " + parts.mnemonic, trimmed };
            return false;
        }

        std::string operandText = Trim(trimmed.substr(pos));
        if (!operandText.empty() && !SplitOperands(operandText, parts.operands))
        {
            error = { lineNumber, "Malformed operand list", trimmed };
            return false;
        }

        lines.push_back(parts);
    }

    return true;
}

bool Z80TextAssembler::SplitOperands(const std::string& text, std::vector<std::string>& operands)
{
    int depth = 0;
    bool inString = false;
    char quote = 0;
    std::string current;

    for (size_t i = 0; i < text.size(); i++)
    {
        char c = text[i];

        if (inString)
        {
            current += c;
            if (c == quote && (i == 0 || text[i - 1] != '\\'))
                inString = false;
            continue;
        }

        if (c == '\'' || c == '"')
        {
            inString = true;
            quote = c;
            current += c;
            continue;
        }

        if (c == '(') depth++;
        if (c == ')') depth--;

        if (c == ',' && depth == 0)
        {
            operands.push_back(Trim(current));
            current.clear();
            continue;
        }

        current += c;
    }

    if (inString || depth != 0)
        return false;

    operands.push_back(Trim(current));
    return true;
}

/// endregion </Source parsing>

/// region <Expression evaluation>

namespace
{

struct ExprToken
{
    enum Kind { Number, Ident, Dollar, Op, LParen, RParen } kind;
    int32_t number = 0;
    char op = 0;
    std::string text;

    explicit ExprToken(Kind k) : kind(k) {}
    ExprToken(Kind k, int32_t n) : kind(k), number(n) {}
    ExprToken(Kind k, char o) : kind(k), op(o) {}
};

bool LexExpression(const std::string& s, std::vector<ExprToken>& tokens, std::string& error)
{
    size_t i = 0;
    while (i < s.size())
    {
        char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) { i++; continue; }

        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            // Radix detection: 0x/0b handled below via prefix scan
            if (c == '0' && i + 1 < s.size() && (s[i + 1] == 'x' || s[i + 1] == 'X'))
            {
                size_t j = i + 2;
                int32_t v = 0;
                while (j < s.size() && std::isxdigit(static_cast<unsigned char>(s[j])))
                {
                    char d = s[j];
                    int dv = std::isdigit(static_cast<unsigned char>(d)) ? d - '0' : (std::tolower(d) - 'a' + 10);
                    v = v * 16 + dv;
                    j++;
                }
                if (j == i + 2) { error = "Malformed hex literal"; return false; }
                tokens.push_back(ExprToken(ExprToken::Number, v));
                i = j;
                continue;
            }
            if (c == '0' && i + 1 < s.size() && (s[i + 1] == 'b' || s[i + 1] == 'B'))
            {
                size_t j = i + 2;
                int32_t v = 0;
                while (j < s.size() && (s[j] == '0' || s[j] == '1'))
                {
                    v = v * 2 + (s[j] - '0');
                    j++;
                }
                if (j == i + 2) { error = "Malformed binary literal"; return false; }
                tokens.push_back(ExprToken(ExprToken::Number, v));
                i = j;
                continue;
            }
            size_t j = i;
            int32_t v = 0;
            while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])))
            {
                v = v * 10 + (s[j] - '0');
                j++;
            }
            tokens.push_back(ExprToken(ExprToken::Number, v));
            i = j;
            continue;
        }

        if (c == '$')
        {
            // "$12AB" hex literal or "$" current address
            if (i + 1 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])))
            {
                size_t j = i + 1;
                int32_t v = 0;
                while (j < s.size() && std::isxdigit(static_cast<unsigned char>(s[j])))
                {
                    char d = s[j];
                    int dv = std::isdigit(static_cast<unsigned char>(d)) ? d - '0' : (std::tolower(d) - 'a' + 10);
                    v = v * 16 + dv;
                    j++;
                }
                tokens.push_back(ExprToken(ExprToken::Number, v));
                i = j;
                continue;
            }
            tokens.push_back(ExprToken(ExprToken::Dollar));
            i++;
            continue;
        }

        if (c == '#')
        {
            if (i + 1 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])))
            {
                size_t j = i + 1;
                int32_t v = 0;
                while (j < s.size() && std::isxdigit(static_cast<unsigned char>(s[j])))
                {
                    char d = s[j];
                    int dv = std::isdigit(static_cast<unsigned char>(d)) ? d - '0' : (std::tolower(d) - 'a' + 10);
                    v = v * 16 + dv;
                    j++;
                }
                tokens.push_back(ExprToken(ExprToken::Number, v));
                i = j;
                continue;
            }
            error = "Stray '#' in expression";
            return false;
        }

        if (c == '%')
        {
            if (i + 1 < s.size() && (s[i + 1] == '0' || s[i + 1] == '1'))
            {
                size_t j = i + 1;
                int32_t v = 0;
                while (j < s.size() && (s[j] == '0' || s[j] == '1'))
                {
                    v = v * 2 + (s[j] - '0');
                    j++;
                }
                tokens.push_back(ExprToken(ExprToken::Number, v));
                i = j;
                continue;
            }
            // '%' is also the modulo operator
            tokens.push_back(ExprToken(ExprToken::Op, '%'));
            i++;
            continue;
        }

        if (c == '\'')
        {
            // Character literal: 'A' or escaped '\n'
            if (i + 2 < s.size() && s[i + 2] == '\'')
            {
                char ch = s[i + 1];
                if (ch == '\\' && i + 3 < s.size() && s[i + 3] == '\'')
                {
                    char e = s[i + 2];
                    switch (e)
                    {
                        case 'n': ch = '\n'; break;
                        case 'r': ch = '\r'; break;
                        case 't': ch = '\t'; break;
                        case '0': ch = '\0'; break;
                        case '\\': ch = '\\'; break;
                        case '\'': ch = '\''; break;
                        default: error = "Unknown escape in character literal"; return false;
                    }
                    tokens.push_back(ExprToken(ExprToken::Number, static_cast<int32_t>(static_cast<unsigned char>(ch))));
                    i += 4;
                }
                else
                {
                    tokens.push_back(ExprToken(ExprToken::Number, static_cast<int32_t>(static_cast<unsigned char>(ch))));
                    i += 3;
                }
                continue;
            }
            error = "Malformed character literal";
            return false;
        }

        if (IsIdentStart(c))
        {
            size_t j = i + 1;
            while (j < s.size() && IsIdentChar(s[j])) j++;
            ExprToken t(ExprToken::Ident);
            t.text = s.substr(i, j - i);
            tokens.push_back(t);
            i = j;
            continue;
        }

        if (c == '(') { tokens.push_back(ExprToken(ExprToken::LParen)); i++; continue; }
        if (c == ')') { tokens.push_back(ExprToken(ExprToken::RParen)); i++; continue; }

        if (c == '<' && i + 1 < s.size() && s[i + 1] == '<') { tokens.push_back(ExprToken(ExprToken::Op, 'L')); i += 2; continue; }
        if (c == '>' && i + 1 < s.size() && s[i + 1] == '>') { tokens.push_back(ExprToken(ExprToken::Op, 'R')); i += 2; continue; }

        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '&' || c == '|' || c == '^' || c == '~')
        {
            tokens.push_back(ExprToken(ExprToken::Op, c));
            i++;
            continue;
        }

        error = std::string("Unexpected character in expression: '") + c + "'";
        return false;
    }

    return true;
}

} // anonymous namespace

bool Z80TextAssembler::Evaluate(const std::string& expression, uint16_t currentAddress, int32_t& value, std::string& error)
{
    std::vector<ExprToken> tokens;
    if (!LexExpression(expression, tokens, error))
        return false;

    size_t pos = 0;
    int32_t currentValue = currentAddress;  // bound to '$'

    std::function<bool(int32_t&)> parseExpr;
    std::function<bool(int32_t&)> parseTerm;
    std::function<bool(int32_t&)> parseFactor;

    auto peek = [&tokens, &pos]() -> const ExprToken* {
        return pos < tokens.size() ? &tokens[pos] : nullptr;
    };

    parseFactor = [&](int32_t& out) -> bool {
        const ExprToken* t = peek();
        if (!t) { error = "Unexpected end of expression"; return false; }

        if (t->kind == ExprToken::Number) { out = t->number; pos++; return true; }
        if (t->kind == ExprToken::Dollar) { out = currentValue; pos++; return true; }
        if (t->kind == ExprToken::Ident)
        {
            auto it = _symbols.find(t->text);
            if (it == _symbols.end())
            {
                error = "Undefined symbol: " + t->text;
                return false;
            }
            out = static_cast<int32_t>(it->second);
            pos++;
            return true;
        }
        if (t->kind == ExprToken::LParen)
        {
            pos++;
            if (!parseExpr(out)) return false;
            const ExprToken* r = peek();
            if (!r || r->kind != ExprToken::RParen) { error = "Missing ')'"; return false; }
            pos++;
            return true;
        }
        if (t->kind == ExprToken::Op && (t->op == '-' || t->op == '+' || t->op == '~'))
        {
            pos++;
            int32_t v;
            if (!parseFactor(v)) return false;
            out = t->op == '-' ? -v : (t->op == '+' ? v : ~v);
            return true;
        }

        error = "Unexpected token in expression";
        return false;
    };

    parseTerm = [&](int32_t& out) -> bool {
        if (!parseFactor(out)) return false;
        while (true)
        {
            const ExprToken* t = peek();
            if (!t || t->kind != ExprToken::Op) break;
            char op = t->op;
            if (op != '*' && op != '/' && op != '%' && op != '&' && op != '^' &&
                op != 'L' && op != 'R')
                break;
            pos++;
            int32_t rhs;
            if (!parseFactor(rhs)) return false;
            switch (op)
            {
                case '*': out = out * rhs; break;
                case '/': if (rhs == 0) { error = "Division by zero"; return false; } out = out / rhs; break;
                case '%': if (rhs == 0) { error = "Division by zero"; return false; } out = out % rhs; break;
                case '&': out = out & rhs; break;
                case '^': out = out ^ rhs; break;
                case 'L': out = out << (rhs & 31); break;
                case 'R': out = static_cast<int32_t>(static_cast<uint32_t>(out) >> (rhs & 31)); break;
            }
        }
        return true;
    };

    parseExpr = [&](int32_t& out) -> bool {
        if (!parseTerm(out)) return false;
        while (true)
        {
            const ExprToken* t = peek();
            if (!t || t->kind != ExprToken::Op) break;
            char op = t->op;
            if (op != '+' && op != '-' && op != '|') break;
            pos++;
            int32_t rhs;
            if (!parseTerm(rhs)) return false;
            if (op == '+') out = out + rhs;
            else if (op == '-') out = out - rhs;
            else out = out | rhs;
        }
        return true;
    };

    if (!parseExpr(value))
        return false;

    if (pos != tokens.size())
    {
        error = "Trailing tokens in expression";
        return false;
    }

    return true;
}

/// endregion </Expression evaluation>

/// region <Instruction encoding>

bool Z80TextAssembler::EncodeLine(const LineParts& line, uint16_t address, int pass,
                                  std::vector<uint8_t>& out, std::string& error)
{
    const std::string& m = line.mnemonic;
    const std::vector<std::string>& ops = line.operands;

    // Pass-1 expression evaluation: relaxed (undefined symbols -> 0, errors suppressed)
    // because instruction sizes never depend on expression values
    auto eval = [&](const std::string& expr, int32_t& value) -> bool {
        if (pass == 1)
        {
            std::string ignored;
            if (Evaluate(expr, address, value, ignored))
                return true;
            value = 0;
            return true;
        }
        return Evaluate(expr, address, value, error);
    };

    auto emit = [&out](std::initializer_list<uint8_t> bytes) {
        for (uint8_t b : bytes) out.push_back(b);
    };

    // ---- pseudo-ops ----

    if (m == "db" || m == "defb" || m == "byte")
    {
        if (ops.empty()) { error = "DB requires at least one item"; return false; }
        for (const auto& item : ops)
        {
            const std::string t = Trim(item);
            if (t.size() >= 2 && (t.front() == '"' || t.front() == '\'') && t.back() == t.front())
            {
                for (size_t i = 1; i + 1 < t.size(); i++)
                {
                    char c = t[i];
                    if (c == '\\' && i + 2 < t.size())
                    {
                        i++;
                        switch (t[i])
                        {
                            case 'n': c = '\n'; break;
                            case 'r': c = '\r'; break;
                            case 't': c = '\t'; break;
                            case '0': c = '\0'; break;
                            case '\\': c = '\\'; break;
                            case '"': c = '"'; break;
                            case '\'': c = '\''; break;
                            default: error = "Unknown escape in string"; return false;
                        }
                    }
                    out.push_back(static_cast<uint8_t>(c));
                }
            }
            else
            {
                int32_t v;
                if (!eval(t, v)) return false;
                if (pass == 2 && (v < -128 || v > 255)) { error = "DB value out of byte range"; return false; }
                out.push_back(Lo(v));
            }
        }
        return true;
    }

    if (m == "dw" || m == "defw" || m == "word")
    {
        if (ops.empty()) { error = "DW requires at least one item"; return false; }
        for (const auto& item : ops)
        {
            int32_t v;
            if (!eval(Trim(item), v)) return false;
            if (pass == 2 && (v < -32768 || v > 0xFFFF)) { error = "DW value out of word range"; return false; }
            out.push_back(Lo(v));
            out.push_back(Hi(v));
        }
        return true;
    }

    if (m == "ds" || m == "defs" || m == "block")
    {
        if (ops.empty() || ops.size() > 2) { error = "DS requires count[, fill]"; return false; }
        int32_t count = 0;
        // DS size must resolve on the first pass — no forward references
        if (!Evaluate(ops[0], address, count, error))
        {
            error = "DS count must not use forward references (" + error + ")";
            return false;
        }
        if (count < 0 || count > 0x10000) { error = "DS count out of range"; return false; }
        int32_t fill = 0;
        if (ops.size() == 2 && !eval(ops[1], fill)) return false;
        for (int32_t i = 0; i < count; i++)
            out.push_back(Lo(fill));
        return true;
    }

    // ---- no-operand instructions ----

    if (ops.empty() && m != "ld" && m != "add" && m != "adc" && m != "sub" && m != "sbc" &&
        m != "and" && m != "xor" && m != "or" && m != "cp" && m != "in" && m != "out" &&
        m != "im" && m != "inc" && m != "dec" && m != "push" && m != "pop" && m != "ex" &&
        m != "bit" && m != "set" && m != "res" && m != "jp" && m != "jr" && m != "djnz" &&
        m != "call" && m != "rst" && m != "rlc" && m != "rl" && m != "rrc" && m != "rr" &&
        m != "sla" && m != "sra" && m != "srl" && m != "org" && m != "equ")
    {
        if (m == "nop") { emit({0x00}); return true; }
        if (m == "halt") { emit({0x76}); return true; }
        if (m == "exx") { emit({0xD9}); return true; }
        if (m == "daa") { emit({0x27}); return true; }
        if (m == "cpl") { emit({0x2F}); return true; }
        if (m == "scf") { emit({0x37}); return true; }
        if (m == "ccf") { emit({0x3F}); return true; }
        if (m == "di") { emit({0xF3}); return true; }
        if (m == "ei") { emit({0xFB}); return true; }
        if (m == "rlca") { emit({0x07}); return true; }
        if (m == "rla") { emit({0x17}); return true; }
        if (m == "rrca") { emit({0x0F}); return true; }
        if (m == "rra") { emit({0x1F}); return true; }
        if (m == "ret") { emit({0xC9}); return true; }
        if (m == "reti") { emit({0xED, 0x4D}); return true; }
        if (m == "retn") { emit({0xED, 0x45}); return true; }
        if (m == "neg") { emit({0xED, 0x44}); return true; }

        static const std::map<std::string, uint8_t> blockOps = {
            {"ldi", 0xA0}, {"ldd", 0xA8}, {"ldir", 0xB0}, {"lddr", 0xB8},
            {"cpi", 0xA1}, {"cpd", 0xA9}, {"cpir", 0xB1}, {"cpdr", 0xB9},
            {"ini", 0xA2}, {"ind", 0xAA}, {"inir", 0xB2}, {"indr", 0xBA},
            {"outi", 0xA3}, {"outd", 0xAB}, {"otir", 0xB3}, {"otdr", 0xBB}
        };
        auto block = blockOps.find(m);
        if (block != blockOps.end())
        {
            emit({0xED, block->second});
            return true;
        }

        error = "Unknown instruction: " + m;
        return false;
    }

    // ---- LD family ----

    if (m == "ld")
    {
        if (ops.size() != 2) { error = "LD requires two operands"; return false; }
        const std::string dst = Trim(ops[0]);
        const std::string src = Trim(ops[1]);
        const std::string dstL = ToLower(dst);
        const std::string srcL = ToLower(src);

        // Index operands (IX+d)/(IY+d) are resolved once here and handled by the
        // 8-bit matrix below — every (nn) indirect handler must skip them
        int32_t dstDisp = 0, srcDisp = 0;
        bool dstHasDisp = false, srcHasDisp = false;
        int dstPrefix = ParseIndexOperand(dst, dstDisp, dstHasDisp);
        int srcPrefix = ParseIndexOperand(src, srcDisp, srcHasDisp);
        if (dstPrefix < 0 || srcPrefix < 0) { error = "Malformed index operand"; return false; }

        // Special register moves
        if (dstL == "a" && srcL == "i") { emit({0xED, 0x57}); return true; }
        if (dstL == "a" && srcL == "r") { emit({0xED, 0x5F}); return true; }
        if (dstL == "i" && srcL == "a") { emit({0xED, 0x47}); return true; }
        if (dstL == "r" && srcL == "a") { emit({0xED, 0x4F}); return true; }

        // SP from HL/IX/IY
        if (dstL == "sp" && srcL == "hl") { emit({0xF9}); return true; }
        if (dstL == "sp" && srcL == "ix") { emit({0xDD, 0xF9}); return true; }
        if (dstL == "sp" && srcL == "iy") { emit({0xFD, 0xF9}); return true; }

        // A from/to (BC)/(DE)
        if (dstL == "a" && srcL == "(bc)") { emit({0x0A}); return true; }
        if (dstL == "a" && srcL == "(de)") { emit({0x1A}); return true; }
        if (dstL == "(bc)" && srcL == "a") { emit({0x02}); return true; }
        if (dstL == "(de)" && srcL == "a") { emit({0x12}); return true; }

        // Indexed 16-bit registers
        bool dstIX = dstL == "ix", dstIY = dstL == "iy";
        bool srcIX = srcL == "ix", srcIY = srcL == "iy";

        // rr,nn / IX,nn / IY,nn
        if ((dstIX || dstIY) && src.size() >= 2 && src.front() == '(')
        {
            std::string inner;
            if (!StripParens(src, inner)) { error = "Malformed indirect operand"; return false; }
            int32_t v;
            if (!eval(inner, v)) return false;
            emit({static_cast<uint8_t>(dstIX ? 0xDD : 0xFD), 0x2A, Lo(v), Hi(v)});   // ld ix,(nn)
            return true;
        }
        if (dstIX || dstIY)
        {
            int32_t v;
            if (!eval(src, v)) return false;
            emit({static_cast<uint8_t>(dstIX ? 0xDD : 0xFD), 0x21, Lo(v), Hi(v)});   // ld ix,nn
            return true;
        }

        // (nn),IX / (nn),IY
        if ((srcIX || srcIY) && dst.size() >= 2 && dst.front() == '(')
        {
            std::string inner;
            if (!StripParens(dst, inner)) { error = "Malformed indirect operand"; return false; }
            int32_t v;
            if (!eval(inner, v)) return false;
            emit({static_cast<uint8_t>(srcIX ? 0xDD : 0xFD), 0x22, Lo(v), Hi(v)});   // ld (nn),ix
            return true;
        }

        // (nn),A / A,(nn) — plain (nn) only; register-indirect forms are handled
        // elsewhere ((bc)/(de) above, (HL) by the 8-bit matrix below, (IX+d)/(IY+d)
        // via the prefix slots)
        if (dstL == "a" && !src.empty() && src.front() == '(' && !srcPrefix && srcL != "(hl)")
        {
            std::string inner;
            if (!StripParens(src, inner)) { error = "Malformed indirect operand"; return false; }
            int32_t v;
            if (!eval(inner, v)) return false;
            emit({0x3A, Lo(v), Hi(v)});
            return true;
        }
        if (!dst.empty() && dst.front() == '(' && !dstPrefix && srcL == "a" && dstL != "(hl)")
        {
            std::string inner;
            if (!StripParens(dst, inner)) { error = "Malformed indirect operand"; return false; }
            int32_t v;
            if (!eval(inner, v)) return false;
            emit({0x32, Lo(v), Hi(v)});
            return true;
        }

        // HL/rr with (nn)
        auto isIndirect = [](const std::string& s) { return !s.empty() && s.front() == '('; };

        if (dstL == "hl" && isIndirect(src) && !srcPrefix)
        {
            std::string inner;
            if (!StripParens(src, inner)) { error = "Malformed indirect operand"; return false; }
            int32_t v;
            if (!eval(inner, v)) return false;
            emit({0x2A, Lo(v), Hi(v)});
            return true;
        }
        if (isIndirect(dst) && !dstPrefix && srcL == "hl")
        {
            std::string inner;
            if (!StripParens(dst, inner)) { error = "Malformed indirect operand"; return false; }
            int32_t v;
            if (!eval(inner, v)) return false;
            emit({0x22, Lo(v), Hi(v)});
            return true;
        }

        int rr = Reg16Code(dstL);
        if (rr >= 0 && isIndirect(src) && !srcPrefix)
        {
            std::string inner;
            if (!StripParens(src, inner)) { error = "Malformed indirect operand"; return false; }
            int32_t v;
            if (!eval(inner, v)) return false;
            emit({0xED, static_cast<uint8_t>(0x4B | (rr << 4)), Lo(v), Hi(v)});      // ld rr,(nn)
            return true;
        }
        if (isIndirect(dst) && !dstPrefix && Reg16Code(srcL) >= 0)
        {
            std::string inner;
            if (!StripParens(dst, inner)) { error = "Malformed indirect operand"; return false; }
            int32_t v;
            if (!eval(inner, v)) return false;
            emit({0xED, static_cast<uint8_t>(0x43 | (Reg16Code(srcL) << 4)), Lo(v), Hi(v)});  // ld (nn),rr
            return true;
        }

        // rr,nn
        if (rr >= 0)
        {
            int32_t v;
            if (!eval(src, v)) return false;
            emit({static_cast<uint8_t>(0x01 | (rr << 4)), Lo(v), Hi(v)});
            return true;
        }

        // 8-bit moves: r8 / (HL) / (IX+d) / (IY+d) with r8 / (HL) / (IX+d) / (IY+d) / n
        // (dstPrefix/srcPrefix and displacements resolved at the top of the LD block)
        int dstSlot = Reg8Code(dstL);
        int srcSlot = Reg8Code(srcL);

        uint8_t prefix = 0;
        if (dstPrefix) { if (srcPrefix && srcPrefix != dstPrefix) { error = "Cannot mix IX and IY"; return false; } prefix = static_cast<uint8_t>(dstPrefix); }
        else if (srcPrefix) prefix = static_cast<uint8_t>(srcPrefix);

        if (dstPrefix) dstSlot = 6;
        if (srcPrefix) srcSlot = 6;

        if (dstSlot < 0 && !dstPrefix)
        {
            // Destination not a plain register and not an index form — must be (HL)
            if (dstL != "(hl)") { error = "Invalid LD destination: " + dst; return false; }
            if (prefix) { error = "Cannot mix (HL) with IX/IY"; return false; }
            dstSlot = 6;
        }
        if (srcSlot < 0 && !srcPrefix)
        {
            if (srcL != "(hl)")
            {
                // Immediate byte
                if (dstSlot < 0) { error = "Invalid LD operands"; return false; }
                if (dstSlot == 6 && !prefix) { emit({0x36}); }               // ld (hl),n
                else if (dstSlot == 6) { emit({prefix, 0x36}); }              // ld (ix+d),n
                else { emit({static_cast<uint8_t>(0x06 | (dstSlot << 3))}); } // ld r,n
                if (dstSlot == 6 && prefix) out.push_back(Lo(dstDisp));
                int32_t v;
                if (!eval(src, v)) return false;
                if (pass == 2 && (v < -128 || v > 255)) { error = "Immediate out of byte range"; return false; }
                out.push_back(Lo(v));
                return true;
            }
            if (prefix) { error = "Cannot mix (HL) with IX/IY"; return false; }
            srcSlot = 6;
        }

        if (dstSlot == 6 && srcSlot == 6) { error = "Invalid LD operand combination"; return false; }

        if (prefix) out.push_back(prefix);
        out.push_back(static_cast<uint8_t>(0x40 | (dstSlot << 3) | srcSlot));
        if (dstSlot == 6 && prefix) out.push_back(Lo(dstDisp));
        if (srcSlot == 6 && prefix) out.push_back(Lo(srcDisp));
        return true;
    }

    // ---- 8-bit arithmetic / logic ----

    int aluCode = -1;
    if (m == "add") aluCode = 0;
    else if (m == "adc") aluCode = 1;
    else if (m == "sub") aluCode = 2;
    else if (m == "sbc") aluCode = 3;
    else if (m == "and") aluCode = 4;
    else if (m == "xor") aluCode = 5;
    else if (m == "or") aluCode = 6;
    else if (m == "cp") aluCode = 7;

    if (aluCode >= 0)
    {
        // 16-bit forms first — they must be checked before the 8-bit 'A,' validation
        if (ops.size() == 2)
        {
            const std::string firstL = ToLower(Trim(ops[0]));

            // add/adc/sbc hl,rr
            if ((m == "add" || m == "adc" || m == "sbc") && firstL == "hl")
            {
                int rr = Reg16Code(ToLower(Trim(ops[1])));
                if (rr >= 0)
                {
                    if (m == "add") { emit({static_cast<uint8_t>(0x09 | (rr << 4))}); return true; }
                    if (m == "adc") { emit({0xED, static_cast<uint8_t>(0x4A | (rr << 4))}); return true; }
                    emit({0xED, static_cast<uint8_t>(0x42 | (rr << 4))});
                }
                // Invalid rr falls through to the 8-bit error below
            }

            // add ix/iy,rr
            if (m == "add" && (firstL == "ix" || firstL == "iy"))
            {
                bool isIX = firstL == "ix";
                std::string rrText = ToLower(Trim(ops[1]));
                int rr;
                if (rrText == "ix" || rrText == "iy") rr = 2;
                else rr = Reg16Code(rrText);
                if (rr < 0 || rrText == "iy") { error = "Invalid operand for ADD IX/IY"; return false; }
                emit({static_cast<uint8_t>(isIX ? 0xDD : 0xFD), static_cast<uint8_t>(0x09 | (rr << 4))});
                return true;
            }
        }

        std::string x;
        if (ops.size() == 2)
        {
            // Optional leading A,
            if (ToLower(Trim(ops[0])) != "a") { error = "First operand must be A for " + m; return false; }
            x = Trim(ops[1]);
        }
        else if (ops.size() == 1)
        {
            x = Trim(ops[0]);
        }
        else { error = m + " requires one or two operands"; return false; }

        // 8-bit forms
        int32_t disp = 0;
        bool hasDisp = false;
        int prefix = ParseIndexOperand(x, disp, hasDisp);
        if (prefix < 0) { error = "Malformed index operand"; return false; }

        int slot = Reg8Code(ToLower(x));
        if (prefix)
        {
            emit({static_cast<uint8_t>(prefix), static_cast<uint8_t>(0x80 | (aluCode << 3) | 6), Lo(disp)});
            return true;
        }
        if (slot >= 0)
        {
            emit({static_cast<uint8_t>(0x80 | (aluCode << 3) | slot)});
            return true;
        }
        if (ToLower(x) == "(hl)")
        {
            emit({static_cast<uint8_t>(0x80 | (aluCode << 3) | 6)});
            return true;
        }

        // Immediate
        int32_t v;
        if (!eval(x, v)) return false;
        if (pass == 2 && (v < -128 || v > 255)) { error = "Immediate out of byte range"; return false; }
        emit({static_cast<uint8_t>(0xC6 | (aluCode << 3)), Lo(v)});
        return true;
    }

    // ---- INC/DEC ----

    if (m == "inc" || m == "dec")
    {
        if (ops.size() != 1) { error = m + " requires one operand"; return false; }
        const std::string x = Trim(ops[0]);
        const std::string xL = ToLower(x);
        uint8_t base = m == "inc" ? 0x04 : 0x05;

        int32_t disp = 0;
        bool hasDisp = false;
        int prefix = ParseIndexOperand(x, disp, hasDisp);
        if (prefix < 0) { error = "Malformed index operand"; return false; }
        if (prefix)
        {
            emit({static_cast<uint8_t>(prefix), static_cast<uint8_t>(base | (6 << 3)), Lo(disp)});
            return true;
        }

        int r8 = Reg8Code(xL);
        if (r8 >= 0) { emit({static_cast<uint8_t>(base | (r8 << 3))}); return true; }
        if (xL == "(hl)") { emit({static_cast<uint8_t>(base | (6 << 3))}); return true; }

        int rr = Reg16Code(xL);
        if (rr >= 0) { emit({static_cast<uint8_t>((m == "inc" ? 0x03 : 0x0B) | (rr << 4))}); return true; }
        if (xL == "ix") { emit({0xDD, static_cast<uint8_t>(m == "inc" ? 0x23 : 0x2B)}); return true; }
        if (xL == "iy") { emit({0xFD, static_cast<uint8_t>(m == "inc" ? 0x23 : 0x2B)}); return true; }

        error = "Invalid operand for " + m;
        return false;
    }

    // ---- CB rotates / shifts / bit test ----

    int rotCode = -1;
    if (m == "rlc") rotCode = 0;
    else if (m == "rrc") rotCode = 1;
    else if (m == "rl") rotCode = 2;
    else if (m == "rr") rotCode = 3;
    else if (m == "sla") rotCode = 4;
    else if (m == "sra") rotCode = 5;
    else if (m == "srl") rotCode = 7;

    bool isBit = m == "bit", isSet = m == "set", isRes = m == "res";
    if (rotCode >= 0 || isBit || isSet || isRes)
    {
        if (rotCode >= 0)
        {
            if (ops.size() != 1) { error = m + " requires one operand"; return false; }
        }
        else
        {
            if (ops.size() != 2) { error = m + " requires two operands"; return false; }
        }

        const std::string x = Trim(ops.back());

        int32_t disp = 0;
        bool hasDisp = false;
        int prefix = ParseIndexOperand(x, disp, hasDisp);
        if (prefix < 0) { error = "Malformed index operand"; return false; }

        int slot = Reg8Code(ToLower(x));
        bool hlInd = ToLower(x) == "(hl)";

        uint8_t cbByte = 0;
        if (rotCode >= 0)
        {
            if (prefix) { emit({static_cast<uint8_t>(prefix), 0xCB, Lo(disp), static_cast<uint8_t>(rotCode << 3 | 6)}); return true; }
            if (slot >= 0) cbByte = static_cast<uint8_t>((rotCode << 3) | slot);
            else if (hlInd) cbByte = static_cast<uint8_t>((rotCode << 3) | 6);
            else { error = "Invalid operand for " + m; return false; }
            emit({0xCB, cbByte});
            return true;
        }

        // BIT/SET/RES b,x
        int32_t bit;
        if (!eval(Trim(ops[0]), bit)) return false;
        if (bit < 0 || bit > 7) { error = "Bit number out of range 0-7"; return false; }

        uint8_t group = isBit ? 0x40 : (isSet ? 0xC0 : 0x80);
        if (prefix)
        {
            emit({static_cast<uint8_t>(prefix), 0xCB, Lo(disp), static_cast<uint8_t>(group | (bit << 3) | 6)});
            return true;
        }
        if (slot >= 0) cbByte = static_cast<uint8_t>(group | (bit << 3) | slot);
        else if (hlInd) cbByte = static_cast<uint8_t>(group | (bit << 3) | 6);
        else { error = "Invalid operand for " + m; return false; }
        emit({0xCB, cbByte});
        return true;
    }

    // ---- jumps / calls / returns ----

    if (m == "jp")
    {
        std::string target;
        int cc = -1;
        if (ops.size() == 1) target = Trim(ops[0]);
        else if (ops.size() == 2) { cc = CondCode(Trim(ops[0])); if (cc < 0) { error = "Invalid condition"; return false; } target = Trim(ops[1]); }
        else { error = "JP requires one or two operands"; return false; }

        std::string targetL = ToLower(target);
        if (targetL == "(hl)") { emit({0xE9}); return true; }
        if (targetL == "(ix)") { emit({0xDD, 0xE9}); return true; }
        if (targetL == "(iy)") { emit({0xFD, 0xE9}); return true; }

        int32_t v;
        if (!eval(target, v)) return false;
        if (cc < 0) { emit({0xC3, Lo(v), Hi(v)}); return true; }
        emit({static_cast<uint8_t>(0xC2 | (cc << 3)), Lo(v), Hi(v)});
        return true;
    }

    if (m == "jr" || m == "djnz")
    {
        std::string target;
        int cc = -1;
        if (m == "djnz")
        {
            if (ops.size() != 1) { error = "DJNZ requires one operand"; return false; }
            target = Trim(ops[0]);
        }
        else if (ops.size() == 1) target = Trim(ops[0]);
        else if (ops.size() == 2)
        {
            cc = CondCode(Trim(ops[0]));
            if (cc < 0 || cc > 3) { error = "JR supports NZ, Z, NC, C only"; return false; }
            target = Trim(ops[1]);
        }
        else { error = "JR requires one or two operands"; return false; }

        int32_t v;
        if (!eval(target, v)) return false;
        int32_t offset = v - (static_cast<int32_t>(address) + 2);
        if (pass == 2 && (offset < -128 || offset > 127))
        {
            error = "Relative jump out of range (" + std::to_string(offset) + " bytes)";
            return false;
        }

        if (m == "djnz") { emit({0x10, Lo(offset)}); return true; }
        if (cc < 0) { emit({0x18, Lo(offset)}); return true; }
        emit({static_cast<uint8_t>(0x20 | ((cc & 3) << 3)), Lo(offset)});
        return true;
    }

    if (m == "call")
    {
        std::string target;
        int cc = -1;
        if (ops.size() == 1) target = Trim(ops[0]);
        else if (ops.size() == 2) { cc = CondCode(Trim(ops[0])); if (cc < 0) { error = "Invalid condition"; return false; } target = Trim(ops[1]); }
        else { error = "CALL requires one or two operands"; return false; }

        int32_t v;
        if (!eval(target, v)) return false;
        if (cc < 0) { emit({0xCD, Lo(v), Hi(v)}); return true; }
        emit({static_cast<uint8_t>(0xC4 | (cc << 3)), Lo(v), Hi(v)});
        return true;
    }

    if (m == "ret" && !ops.empty())
    {
        if (ops.size() != 1) { error = "RET cc requires one operand"; return false; }
        int cc = CondCode(Trim(ops[0]));
        if (cc < 0) { error = "Invalid condition"; return false; }
        emit({static_cast<uint8_t>(0xC0 | (cc << 3))});
        return true;
    }

    if (m == "rst")
    {
        if (ops.size() != 1) { error = "RST requires one operand"; return false; }
        int32_t v;
        if (!eval(Trim(ops[0]), v)) return false;
        if (pass == 2 && (v < 0 || v > 0x38 || (v & 7) != 0)) { error = "RST target must be 0x00-0x38 in steps of 8"; return false; }
        emit({static_cast<uint8_t>(0xC7 | (v & 0x38))});
        return true;
    }

    // ---- PUSH/POP ----

    if (m == "push" || m == "pop")
    {
        if (ops.size() != 1) { error = m + " requires one operand"; return false; }
        const std::string x = Trim(ops[0]);
        const std::string xL = ToLower(x);

        if (xL == "ix") { emit({0xDD, static_cast<uint8_t>(m == "push" ? 0xE5 : 0xC1)}); return true; }
        if (xL == "iy") { emit({0xFD, static_cast<uint8_t>(m == "push" ? 0xE5 : 0xC1)}); return true; }

        int q = PairCode(xL);
        if (q < 0) { error = "Invalid register pair for " + m; return false; }
        emit({static_cast<uint8_t>((m == "push" ? 0xC5 : 0xC1) | (q << 4))});
        return true;
    }

    // ---- EX ----

    if (m == "ex")
    {
        if (ops.size() != 2) { error = "EX requires two operands"; return false; }
        const std::string a = ToLower(Trim(ops[0]));
        const std::string b = ToLower(Trim(ops[1]));

        if (a == "de" && b == "hl") { emit({0xEB}); return true; }
        if (a == "af" && (b == "af'" || b == "af")) { emit({0x08}); return true; }
        if (a == "(sp)" && b == "hl") { emit({0xE3}); return true; }
        if (a == "(sp)" && b == "ix") { emit({0xDD, 0xE3}); return true; }
        if (a == "(sp)" && b == "iy") { emit({0xFD, 0xE3}); return true; }
        error = "Invalid EX operands";
        return false;
    }

    // ---- IN/OUT ----

    if (m == "in")
    {
        if (ops.size() != 2) { error = "IN requires two operands"; return false; }
        const std::string dst = ToLower(Trim(ops[0]));
        const std::string src = ToLower(Trim(ops[1]));

        if (dst == "a" && src == "(c)") { /* handled below */ }
        if (dst == "a" && src.front() == '(' && src.back() == ')')
        {
            std::string inner;
            StripParens(src, inner);
            if (Trim(inner) == "c") { emit({0xED, 0x78}); return true; }
            int32_t v;
            if (!eval(Trim(inner), v)) return false;
            emit({0xDB, Lo(v)});
            return true;
        }
        if (src == "(c)")
        {
            if (dst == "f") { emit({0xED, 0x70}); return true; }
            int r = Reg8Code(dst);
            if (r < 0) { error = "Invalid IN destination"; return false; }
            emit({0xED, static_cast<uint8_t>(0x40 | (r << 3))});
            return true;
        }
        error = "Invalid IN operands";
        return false;
    }

    if (m == "out")
    {
        if (ops.size() != 2) { error = "OUT requires two operands"; return false; }
        const std::string dst = ToLower(Trim(ops[0]));
        const std::string src = ToLower(Trim(ops[1]));

        if (dst == "(c)")
        {
            if (src == "0") { emit({0xED, 0x71}); return true; }
            int r = Reg8Code(src);
            if (r < 0) { error = "Invalid OUT source"; return false; }
            emit({0xED, static_cast<uint8_t>(0x41 | (r << 3))});
            return true;
        }
        if (dst.front() == '(' && dst.back() == ')' && src == "a")
        {
            std::string inner;
            StripParens(dst, inner);
            int32_t v;
            if (!eval(Trim(inner), v)) return false;
            emit({0xD3, Lo(v)});
            return true;
        }
        error = "Invalid OUT operands";
        return false;
    }

    // ---- IM ----

    if (m == "im")
    {
        if (ops.size() != 1) { error = "IM requires one operand"; return false; }
        int32_t v;
        if (!eval(Trim(ops[0]), v)) return false;
        if (pass == 2 && (v != 0 && v != 1 && v != 2)) { error = "IM mode must be 0, 1 or 2"; return false; }
        emit({0xED, static_cast<uint8_t>(v == 1 ? 0x56 : (v == 2 ? 0x5E : 0x46))});
        return true;
    }

    error = "Unknown instruction: " + m;
    return false;
}

/// endregion </Instruction encoding>
