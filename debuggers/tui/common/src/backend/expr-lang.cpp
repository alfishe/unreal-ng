// expr-lang.cpp - compiler, evaluator and decompiler for the conditional
// breakpoint expression language (TDD-DBG-01 §10.2).
//
// Compile() upper-cases the source (characters immediately followed by a
// quote keep their case), parses into RPN with a precedence-climbing pass and
// validates the program with a dry stack walk; every failure yields the one
// canned error text. The evaluator runs on 32-bit unsigned values; division
// or modulo by zero yields the left operand (0.39.0 behaviour, kept
// faithfully). Backends call Eval() at every instruction boundary while
// conditions exist; any non-zero result breaks.
#include "backend/expr-lang.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace dbg {
namespace detail {

struct ExprParser {
    using T = CompiledExpr::Tok;  // private nested type; ExprParser is a friend

    const char* p = nullptr;
    std::vector<std::pair<T, uint32_t>> out;
    bool ok = true;

    struct OperandRule {
        const char* name;
        T tok;
    };

    void SkipWs() {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    }

    bool Eat(char c) {
        SkipWs();
        if (*p == c) { ++p; return true; }
        return false;
    }

    static bool IsHexDigit(char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
    }
    static uint32_t HexVal(char c) {
        return (c <= '9') ? static_cast<uint32_t>(c - '0')
                          : static_cast<uint32_t>(c - 'A' + 10);
    }

    void Emit(T tok, uint32_t arg = 0) { out.push_back({tok, arg}); }

    void ParsePrimary() {
        SkipWs();
        const char c = *p;
        if (c == '\0') { ok = false; return; }
        if (c == '(') {
            ++p;
            ParseExpr(1);
            if (ok && !Eat(')')) ok = false;
            return;
        }
        if (c == '\'') {  // 'c' character constant
            if (p[1] == '\0' || p[2] != '\'') { ok = false; return; }
            Emit(T::Num, static_cast<uint8_t>(p[1]));
            p += 3;
            return;
        }
        if (c >= '0' && c <= '9') {  // hex number, must start with a digit
            uint32_t v = 0;
            while (IsHexDigit(*p)) { v = (v << 4) | HexVal(*p); ++p; }
            Emit(T::Num, v);
            return;
        }
        // Operand table, strictly longest-first so e.g. "HL'" wins over
        // "HL" and "IN" over "I" (§10.2 "register names match longest-first
        // per table order")
        static const OperandRule kOperands[] = {
            {"AF'", T::RegAf2}, {"BC'", T::RegBc2}, {"DE'", T::RegDe2}, {"HL'", T::RegHl2},
            {"OUT", T::VarOut}, {"VAL", T::VarVal}, {"DOS", T::VarDos}, {"MDT", T::VarMdt},
            {"PG0", T::VarPg0}, {"PG1", T::VarPg1}, {"PG2", T::VarPg2}, {"PG3", T::VarPg3},
            {"A'", T::RegA2}, {"B'", T::RegB2}, {"C'", T::RegC2}, {"D'", T::RegD2},
            {"E'", T::RegE2}, {"F'", T::RegF2}, {"H'", T::RegH2}, {"L'", T::RegL2},
            {"AF", T::RegAf}, {"BC", T::RegBc}, {"DE", T::RegDe}, {"HL", T::RegHl},
            {"PC", T::RegPc}, {"SP", T::RegSp}, {"IX", T::RegIx}, {"IY", T::RegIy},
            {"FD", T::VarFd}, {"IN", T::VarIn}, {"RD", T::VarRd}, {"WR", T::VarWr},
            {"A", T::RegA}, {"B", T::RegB}, {"C", T::RegC}, {"D", T::RegD},
            {"E", T::RegE}, {"F", T::RegF}, {"H", T::RegH}, {"L", T::RegL},
            {"I", T::RegI}, {"R", T::RegR},
        };
        for (const OperandRule& rule : kOperands) {
            const size_t n = std::strlen(rule.name);
            if (std::strncmp(p, rule.name, n) == 0) {
                p += n;
                Emit(rule.tok);
                return;
            }
        }
        ok = false;
    }

    void ParseUnary() {
        SkipWs();
        if (*p == '!') {
            ++p;
            ParseUnary();
            if (ok) Emit(T::Not);
            return;
        }
        if (*p == '~') {
            ++p;
            ParseUnary();
            if (ok) Emit(T::BitNot);
            return;
        }
        if (p[0] == 'M' && p[1] == '(') {
            p += 2;
            ParseExpr(1);
            if (ok && Eat(')')) Emit(T::Mem);
            else ok = false;
            return;
        }
        ParsePrimary();
        if (!ok) return;
        SkipWs();
        if (p[0] == '-' && p[1] == '>') {
            // legacy "a->b" sugar, grouped with the unary forms at
            // precedence 1 (§10.2): desugars to M(a+b), both sides binding
            // at operand level
            p += 2;
            ParseUnary();
            if (!ok) return;
            Emit(T::Add);
            Emit(T::Mem);
        }
    }

    bool MatchBinary(T* op, int* prec, int* len) {
        const char c0 = p[0], c1 = p[1];
        if (c0 == '-' && c1 == '>') return false;  // handled at unary level
        if (c0 == '>' && c1 == '>') { *op = T::Shr;  *prec = 4;  *len = 2; return true; }
        if (c0 == '<' && c1 == '<') { *op = T::Shl;  *prec = 4;  *len = 2; return true; }
        if (c0 == '>' && c1 == '=') { *op = T::Ge;   *prec = 5;  *len = 2; return true; }
        if (c0 == '<' && c1 == '=') { *op = T::Le;   *prec = 5;  *len = 2; return true; }
        if (c0 == '=' && c1 == '=') { *op = T::Eq;   *prec = 5;  *len = 2; return true; }
        if (c0 == '!' && c1 == '=') { *op = T::Ne;   *prec = 5;  *len = 2; return true; }
        if (c0 == '&' && c1 == '&') { *op = T::LAnd; *prec = 9;  *len = 2; return true; }
        if (c0 == '|' && c1 == '|') { *op = T::LOr;  *prec = 10; *len = 2; return true; }
        switch (c0) {
        case '*': *op = T::Mul; *prec = 2; *len = 1; return true;
        case '%': *op = T::Mod; *prec = 2; *len = 1; return true;
        case '/': *op = T::Div; *prec = 2; *len = 1; return true;
        case '+': *op = T::Add; *prec = 3; *len = 1; return true;
        case '-': *op = T::Sub; *prec = 3; *len = 1; return true;
        case '>': *op = T::Gt;  *prec = 5; *len = 1; return true;
        case '<': *op = T::Lt;  *prec = 5; *len = 1; return true;
        case '=': *op = T::Eq;  *prec = 5; *len = 1; return true;
        case '&': *op = T::And; *prec = 6; *len = 1; return true;
        case '^': *op = T::Xor; *prec = 7; *len = 1; return true;
        case '|': *op = T::Or;  *prec = 8; *len = 1; return true;
        default: return false;
        }
    }

    void ParseExpr(int minPrec) {
        ParseUnary();
        if (!ok) return;
        for (;;) {
            SkipWs();
            T op;
            int prec = 0, len = 0;
            if (!MatchBinary(&op, &prec, &len)) break;
            if (prec < minPrec) break;
            p += len;
            ParseExpr(prec + 1);  // every binary operator is left-associative
            if (!ok) return;
            Emit(op);
        }
    }

    // Dry stack walk: rejects unbalanced programs ("leftover stack when
    // test-evaluated", §10.2).
    static bool ValidateRpn(const std::vector<std::pair<T, uint32_t>>& rpn) {
        int depth = 0;
        for (const auto& ins : rpn) {
            switch (ins.first) {
            case T::Mem:
            case T::Not:
            case T::BitNot:
                if (depth < 1) return false;
                break;
            case T::Mul: case T::Div: case T::Mod: case T::Add: case T::Sub:
            case T::Shr: case T::Shl: case T::Lt: case T::Gt: case T::Le:
            case T::Ge: case T::Eq: case T::Ne: case T::And: case T::Xor:
            case T::Or: case T::LAnd: case T::LOr:
                if (depth < 2) return false;
                --depth;
                break;
            default:
                ++depth;
                break;
            }
        }
        return depth == 1;
    }
};

// Upper-case everything except characters immediately followed by a quote,
// so 'c' constants keep their case while a lower-case "hl'" stays broken
// (§10.2 quirk).
std::string Preprocess(const std::string& text) {
    std::string s;
    s.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const bool beforeQuote = (i + 1 < text.size()) && (text[i + 1] == '\'');
        s += beforeQuote
                 ? text[i]
                 : static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
    }
    return s;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// CompiledExpr
// ---------------------------------------------------------------------------

bool CompiledExpr::Compile(const std::string& text, CompiledExpr* outExpr, std::string* error) {
    static constexpr char kError[] = "Error in expression\nPlease do RTFM";
    const std::string upper = detail::Preprocess(text);
    detail::ExprParser parser;
    parser.p = upper.c_str();
    parser.ParseExpr(1);
    parser.SkipWs();
    if (!parser.ok || *parser.p != '\0' || !detail::ExprParser::ValidateRpn(parser.out)) {
        if (error != nullptr) *error = kError;
        return false;
    }
    outExpr->rpn_ = std::move(parser.out);
    outExpr->source_ = text;
    return true;
}

uint32_t CompiledExpr::Eval(const EvalContext& ctx) const {
    std::vector<uint32_t> st;
    st.reserve(rpn_.size() + 1);
    const Z80Regs* regs = ctx.regs;
    auto pop = [&st]() {
        const uint32_t v = st.back();
        st.pop_back();
        return v;
    };
    for (const auto& ins : rpn_) {
        switch (ins.first) {
        case Tok::Num: st.push_back(ins.second); break;
        case Tok::RegA: st.push_back(regs ? regs->a : 0u); break;
        case Tok::RegF: st.push_back(regs ? regs->f : 0u); break;
        case Tok::RegB: st.push_back(regs ? (regs->bc >> 8) : 0u); break;
        case Tok::RegC: st.push_back(regs ? (regs->bc & 0xFFu) : 0u); break;
        case Tok::RegD: st.push_back(regs ? (regs->de >> 8) : 0u); break;
        case Tok::RegE: st.push_back(regs ? (regs->de & 0xFFu) : 0u); break;
        case Tok::RegH: st.push_back(regs ? (regs->hl >> 8) : 0u); break;
        case Tok::RegL: st.push_back(regs ? (regs->hl & 0xFFu) : 0u); break;
        case Tok::RegAf: st.push_back(regs ? (uint32_t(regs->a) << 8) | regs->f : 0u); break;
        case Tok::RegBc: st.push_back(regs ? regs->bc : 0u); break;
        case Tok::RegDe: st.push_back(regs ? regs->de : 0u); break;
        case Tok::RegHl: st.push_back(regs ? regs->hl : 0u); break;
        case Tok::RegAf2: st.push_back(regs ? regs->af2 : 0u); break;
        case Tok::RegBc2: st.push_back(regs ? regs->bc2 : 0u); break;
        case Tok::RegDe2: st.push_back(regs ? regs->de2 : 0u); break;
        case Tok::RegHl2: st.push_back(regs ? regs->hl2 : 0u); break;
        case Tok::RegA2: st.push_back(regs ? (regs->af2 >> 8) : 0u); break;
        case Tok::RegF2: st.push_back(regs ? (regs->af2 & 0xFFu) : 0u); break;
        case Tok::RegB2: st.push_back(regs ? (regs->bc2 >> 8) : 0u); break;
        case Tok::RegC2: st.push_back(regs ? (regs->bc2 & 0xFFu) : 0u); break;
        case Tok::RegD2: st.push_back(regs ? (regs->de2 >> 8) : 0u); break;
        case Tok::RegE2: st.push_back(regs ? (regs->de2 & 0xFFu) : 0u); break;
        case Tok::RegH2: st.push_back(regs ? (regs->hl2 >> 8) : 0u); break;
        case Tok::RegL2: st.push_back(regs ? (regs->hl2 & 0xFFu) : 0u); break;
        case Tok::RegPc: st.push_back(regs ? regs->pc : 0u); break;
        case Tok::RegSp: st.push_back(regs ? regs->sp : 0u); break;
        case Tok::RegIx: st.push_back(regs ? regs->ix : 0u); break;
        case Tok::RegIy: st.push_back(regs ? regs->iy : 0u); break;
        case Tok::RegI: st.push_back(regs ? regs->i : 0u); break;
        case Tok::RegR: st.push_back(regs ? regs->r : 0u); break;
        case Tok::VarFd: st.push_back(ctx.fd); break;
        case Tok::VarOut: st.push_back(ctx.outPort); break;
        case Tok::VarIn: st.push_back(ctx.inPort); break;
        case Tok::VarVal: st.push_back(ctx.val); break;
        case Tok::VarDos: st.push_back(ctx.dos ? 1u : 0u); break;
        case Tok::VarRd: st.push_back(ctx.rd); break;
        case Tok::VarWr: st.push_back(ctx.wr); break;
        case Tok::VarMdt: st.push_back(ctx.mdt); break;
        case Tok::VarPg0: st.push_back(ctx.page[0]); break;
        case Tok::VarPg1: st.push_back(ctx.page[1]); break;
        case Tok::VarPg2: st.push_back(ctx.page[2]); break;
        case Tok::VarPg3: st.push_back(ctx.page[3]); break;
        case Tok::Mem: {
            const uint32_t addr = pop();
            st.push_back((ctx.mem != nullptr) ? (*ctx.mem)[addr & 0xFFFFu] : 0u);
            break;
        }
        case Tok::Not: { const uint32_t x = pop(); st.push_back(x ? 0u : 1u); break; }
        case Tok::BitNot: { const uint32_t x = pop(); st.push_back(~x); break; }
        case Tok::Mul: { const uint32_t b = pop(), a = pop(); st.push_back(a * b); break; }
        case Tok::Div: { const uint32_t b = pop(), a = pop(); st.push_back(b == 0 ? a : a / b); break; }
        case Tok::Mod: { const uint32_t b = pop(), a = pop(); st.push_back(b == 0 ? a : a % b); break; }
        case Tok::Add: { const uint32_t b = pop(), a = pop(); st.push_back(a + b); break; }
        case Tok::Sub: { const uint32_t b = pop(), a = pop(); st.push_back(a - b); break; }
        case Tok::Shr: { const uint32_t b = pop(), a = pop(); st.push_back(b >= 32 ? 0u : (a >> b)); break; }
        case Tok::Shl: { const uint32_t b = pop(), a = pop(); st.push_back(b >= 32 ? 0u : (a << b)); break; }
        case Tok::Gt: { const uint32_t b = pop(), a = pop(); st.push_back(a > b ? 1u : 0u); break; }
        case Tok::Lt: { const uint32_t b = pop(), a = pop(); st.push_back(a < b ? 1u : 0u); break; }
        case Tok::Ge: { const uint32_t b = pop(), a = pop(); st.push_back(a >= b ? 1u : 0u); break; }
        case Tok::Le: { const uint32_t b = pop(), a = pop(); st.push_back(a <= b ? 1u : 0u); break; }
        case Tok::Eq: { const uint32_t b = pop(), a = pop(); st.push_back(a == b ? 1u : 0u); break; }
        case Tok::Ne: { const uint32_t b = pop(), a = pop(); st.push_back(a != b ? 1u : 0u); break; }
        case Tok::And: { const uint32_t b = pop(), a = pop(); st.push_back(a & b); break; }
        case Tok::Xor: { const uint32_t b = pop(), a = pop(); st.push_back(a ^ b); break; }
        case Tok::Or: { const uint32_t b = pop(), a = pop(); st.push_back(a | b); break; }
        case Tok::LAnd: { const uint32_t b = pop(), a = pop(); st.push_back((a && b) ? 1u : 0u); break; }
        case Tok::LOr: { const uint32_t b = pop(), a = pop(); st.push_back((a || b) ? 1u : 0u); break; }
        }
    }
    return st.empty() ? 0u : st.back();
}

std::string CompiledExpr::Decompile() const {
    struct Item {
        std::string text;    // rendered operand (when !bareNum)
        uint32_t num = 0;    // immediate value (when bareNum)
        bool bareNum = false;
    };
    std::vector<Item> st;
    auto render = [](const Item& item, char nextChar) -> std::string {
        if (!item.bareNum) return item.text;
        char buf[16];
        std::snprintf(buf, sizeof buf, "0%X", item.num);
        std::string s = buf;
        if (s.size() > 1 && s[0] == '0' && nextChar >= '0' && nextChar <= '9') {
            s.erase(0, 1);  // §10.2: strip the leading 0 before a digit
        }
        return s;
    };
    auto regName = [](Tok t) -> const char* {
        switch (t) {
        case Tok::RegA: return "A";    case Tok::RegF: return "F";
        case Tok::RegB: return "B";    case Tok::RegC: return "C";
        case Tok::RegD: return "D";    case Tok::RegE: return "E";
        case Tok::RegH: return "H";    case Tok::RegL: return "L";
        case Tok::RegAf: return "AF";  case Tok::RegBc: return "BC";
        case Tok::RegDe: return "DE";  case Tok::RegHl: return "HL";
        case Tok::RegAf2: return "AF'"; case Tok::RegBc2: return "BC'";
        case Tok::RegDe2: return "DE'"; case Tok::RegHl2: return "HL'";
        case Tok::RegA2: return "A'";  case Tok::RegF2: return "F'";
        case Tok::RegB2: return "B'";  case Tok::RegC2: return "C'";
        case Tok::RegD2: return "D'";  case Tok::RegE2: return "E'";
        case Tok::RegH2: return "H'";  case Tok::RegL2: return "L'";
        case Tok::RegPc: return "PC";  case Tok::RegSp: return "SP";
        case Tok::RegIx: return "IX";  case Tok::RegIy: return "IY";
        case Tok::RegI: return "I";    case Tok::RegR: return "R";
        case Tok::VarFd: return "FD";  case Tok::VarOut: return "OUT";
        case Tok::VarIn: return "IN";  case Tok::VarVal: return "VAL";
        case Tok::VarDos: return "DOS";
        case Tok::VarRd: return "RD";  case Tok::VarWr: return "WR";
        case Tok::VarMdt: return "MDT";
        case Tok::VarPg0: return "PG0"; case Tok::VarPg1: return "PG1";
        case Tok::VarPg2: return "PG2"; case Tok::VarPg3: return "PG3";
        default: return nullptr;  // Num / Mem / Not / BitNot / binary operators
        }
    };
    auto opName = [](Tok t) -> const char* {
        switch (t) {
        case Tok::Mul: return "*";   case Tok::Mod: return "%";
        case Tok::Div: return "/";   case Tok::Add: return "+";
        case Tok::Sub: return "-";   case Tok::Shr: return ">>";
        case Tok::Shl: return "<<";  case Tok::Gt: return ">";
        case Tok::Lt: return "<";    case Tok::Ge: return ">=";
        case Tok::Le: return "<=";   case Tok::Eq: return "=";
        case Tok::Ne: return "!=";   case Tok::And: return "&";
        case Tok::Xor: return "^";   case Tok::Or: return "|";
        case Tok::LAnd: return "&&"; case Tok::LOr: return "||";
        default: return nullptr;
        }
    };
    for (const auto& ins : rpn_) {
        switch (ins.first) {
        case Tok::Num:
            st.push_back(Item{"", ins.second, true});
            break;
        case Tok::Mem:
        case Tok::Not:
        case Tok::BitNot: {
            if (st.empty()) return {};
            const Item a = st.back();
            st.pop_back();
            const char* open = (ins.first == Tok::Mem)   ? "M("
                               : (ins.first == Tok::Not) ? "!("
                                                         : "~(";
            st.push_back(Item{std::string(open) + render(a, ')') + ")", 0, false});
            break;
        }
        default: {
            const char* name = regName(ins.first);
            if (name != nullptr) {
                st.push_back(Item{name, 0, false});
                break;
            }
            const char* op = opName(ins.first);
            if (op == nullptr || st.size() < 2) return {};
            const Item b = st.back();
            st.pop_back();
            const Item a = st.back();
            st.pop_back();
            st.push_back(Item{"(" + render(a, op[0]) + op + render(b, ')') + ")", 0, false});
            break;
        }
        }
    }
    return (st.size() == 1) ? render(st[0], '\0') : std::string();
}

}  // namespace dbg
