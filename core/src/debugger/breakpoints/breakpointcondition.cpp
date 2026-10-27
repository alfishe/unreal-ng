#include "breakpointcondition.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "debugger/labels/labelmanager.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

/// region <Operands>

namespace
{
using Op = BreakpointConditionProgram::Op;

enum Reg8 : uint8_t
{
    R8_A, R8_F, R8_B, R8_C, R8_D, R8_E, R8_H, R8_L,
    R8_A2, R8_F2, R8_B2, R8_C2, R8_D2, R8_E2, R8_H2, R8_L2,
    R8_IXH, R8_IXL, R8_IYH, R8_IYL, R8_I, R8_R, R8_IM, R8_IFF1, R8_IFF2, R8_HALTED
};
enum Reg16 : uint8_t
{
    R16_AF, R16_BC, R16_DE, R16_HL, R16_AF2, R16_BC2, R16_DE2, R16_HL2, R16_IX, R16_IY, R16_SP, R16_PC, R16_WZ
};
enum Var : uint8_t
{
    V_RD, V_WR, V_MDT, V_IN, V_OUT, V_PORTVAL, V_VAL, V_ADDR, V_PORT,
    V_T, V_FRAME, V_HITS, V_FD, V_DOS, V_PG0, V_PG1, V_PG2, V_PG3, V_ROMPG
};

struct Name
{
    const char* text;
    uint8_t id;
};

// Canonical names first: the printer takes the first name of an id
const Name kReg8Names[] = {
    {"A", R8_A}, {"F", R8_F}, {"B", R8_B}, {"C", R8_C}, {"D", R8_D}, {"E", R8_E}, {"H", R8_H}, {"L", R8_L},
    {"A'", R8_A2}, {"F'", R8_F2}, {"B'", R8_B2}, {"C'", R8_C2}, {"D'", R8_D2}, {"E'", R8_E2}, {"H'", R8_H2},
    {"L'", R8_L2}, {"IXH", R8_IXH}, {"IXL", R8_IXL}, {"IYH", R8_IYH}, {"IYL", R8_IYL}, {"I", R8_I}, {"R", R8_R},
    {"IM", R8_IM}, {"IFF1", R8_IFF1}, {"IFF2", R8_IFF2}, {"HALTED", R8_HALTED},
    {"XH", R8_IXH}, {"XL", R8_IXL}, {"YH", R8_IYH}, {"YL", R8_IYL}, {"HX", R8_IXH}, {"LX", R8_IXL}, {"HY", R8_IYH},
    {"LY", R8_IYL},
};
const Name kReg16Names[] = {
    {"AF", R16_AF}, {"BC", R16_BC}, {"DE", R16_DE}, {"HL", R16_HL}, {"AF'", R16_AF2}, {"BC'", R16_BC2},
    {"DE'", R16_DE2}, {"HL'", R16_HL2}, {"IX", R16_IX}, {"IY", R16_IY}, {"SP", R16_SP}, {"PC", R16_PC},
    {"WZ", R16_WZ}, {"MEMPTR", R16_WZ},
};
// Unreal / TS-Labs names first, then the ZXSpin and the F1 aliases (rules.md §5.3, design.md §4.2)
const Name kVarNames[] = {
    {"RD", V_RD}, {"WR", V_WR}, {"MDT", V_MDT}, {"IN", V_IN}, {"OUT", V_OUT}, {"PWV", V_PORTVAL}, {"VAL", V_VAL},
    {"ADDR", V_ADDR}, {"PORT", V_PORT}, {"T", V_T}, {"FRAME", V_FRAME}, {"HITS", V_HITS}, {"FD", V_FD},
    {"DOS", V_DOS}, {"PG0", V_PG0}, {"PG1", V_PG1}, {"PG2", V_PG2}, {"PG3", V_PG3}, {"ROMPG", V_ROMPG},
    {"RADDR", V_RD}, {"WADDR", V_WR}, {"RPORT", V_IN}, {"WPORT", V_OUT}, {"VALUE", V_VAL},
    {"MRA", V_RD}, {"MWA", V_WR}, {"MRV", V_MDT}, {"MWV", V_MDT}, {"PRA", V_IN}, {"PWA", V_OUT}, {"PRV", V_PORTVAL},
};

/// Which triggers supply an access operand (0: every one)
uint8_t TriggersOf(uint8_t var)
{
    switch (var)
    {
        case V_RD: return BRK_COND_READ;
        case V_WR: return BRK_COND_WRITE;
        case V_MDT: return BRK_COND_READ | BRK_COND_WRITE;
        case V_IN: return BRK_COND_PORT_IN;
        case V_OUT: return BRK_COND_PORT_OUT;
        case V_PORTVAL: return BRK_COND_PORT_OUT;
        case V_VAL: return BRK_COND_READ | BRK_COND_WRITE | BRK_COND_PORT_OUT;
        case V_ADDR: return BRK_COND_READ | BRK_COND_WRITE | BRK_COND_PORT_IN | BRK_COND_PORT_OUT;
        case V_PORT: return BRK_COND_PORT_IN | BRK_COND_PORT_OUT;
        default: return 0;
    }
}

const char* NameOf(const Name* names, size_t count, uint8_t id)
{
    for (size_t i = 0; i < count; i++)
        if (names[i].id == id)
            return names[i].text;
    return "?";
}

bool FindName(const Name* names, size_t count, const std::string& upper, uint8_t& id)
{
    for (size_t i = 0; i < count; i++)
        if (upper == names[i].text)
        {
            id = names[i].id;
            return true;
        }
    return false;
}

inline int64_t ReadReg8(const Z80Registers& r, uint8_t reg)
{
    switch (reg)
    {
        case R8_A: return r.a;
        case R8_F: return r.f;
        case R8_B: return r.b;
        case R8_C: return r.c;
        case R8_D: return r.d;
        case R8_E: return r.e;
        case R8_H: return r.h;
        case R8_L: return r.l;
        case R8_A2: return r.alt.a;
        case R8_F2: return r.alt.f;
        case R8_B2: return r.alt.bc >> 8;
        case R8_C2: return r.alt.bc & 0xFF;
        case R8_D2: return r.alt.de >> 8;
        case R8_E2: return r.alt.de & 0xFF;
        case R8_H2: return r.alt.hl >> 8;
        case R8_L2: return r.alt.hl & 0xFF;
        case R8_IXH: return r.xh;
        case R8_IXL: return r.xl;
        case R8_IYH: return r.yh;
        case R8_IYL: return r.yl;
        case R8_I: return r.i;
        case R8_R: return (r.r_low & 0x7F) | (r.r_hi & 0x80);
        case R8_IM: return r.im;
        case R8_IFF1: return r.iff1 ? 1 : 0;
        case R8_IFF2: return r.iff2 ? 1 : 0;
        case R8_HALTED: return r.halted ? 1 : 0;
    }
    return 0;
}

inline int64_t ReadReg16(const Z80Registers& r, uint8_t reg)
{
    switch (reg)
    {
        case R16_AF: return r.af;
        case R16_BC: return r.bc;
        case R16_DE: return r.de;
        case R16_HL: return r.hl;
        case R16_AF2: return r.alt.af;
        case R16_BC2: return r.alt.bc;
        case R16_DE2: return r.alt.de;
        case R16_HL2: return r.alt.hl;
        case R16_IX: return r.ix;
        case R16_IY: return r.iy;
        case R16_SP: return r.sp;
        case R16_PC: return r.pc;
        case R16_WZ: return r.memptr;
    }
    return 0;
}

/// The page in window `bank`: RAM pages by number, anything else $FF (design.md §4.2)
int64_t RamPageOf(Memory& memory, uint8_t bank)
{
    if (memory.GetMemoryBankMode(bank) != BANK_RAM)
        return 0xFF;
    return memory.GetRAMPageForBank(bank);
}

/// One variable; false (with the reason) when the context lacks what it needs
inline bool ReadVar(const BreakpointConditionContext& x, uint8_t var, int64_t& v, std::string* error)
{
    const BreakpointConditionAccess& a = x.access;
    constexpr uint32_t kNone = BreakpointConditionAccess::kNone;
    switch (var)
    {
        case V_RD: v = a.readAddress; return true;
        case V_WR: v = a.writeAddress; return true;
        case V_MDT: v = a.memoryValue; return true;
        case V_IN: v = a.portIn; return true;
        case V_OUT: v = a.portOut; return true;
        case V_PORTVAL: v = a.portValue; return true;
        case V_VAL: v = a.memoryValue != kNone ? a.memoryValue : a.portValue; return true;
        case V_ADDR:
            v = a.readAddress != kNone ? a.readAddress
                : a.writeAddress != kNone ? a.writeAddress
                : a.portIn != kNone ? a.portIn : a.portOut;
            return true;
        case V_PORT: v = a.portIn != kNone ? a.portIn : a.portOut; return true;
        case V_HITS: v = x.hits; return true;
        case V_T:
            if (!x.cpu)
                break;
            v = x.cpu->t;
            return true;
        case V_FRAME:
        case V_FD:
        case V_DOS:
            if (!x.state)
                break;
            v = var == V_FRAME ? static_cast<int64_t>(x.state->frame_counter)
                : var == V_FD  ? x.state->p7FFD
                               : ((x.state->flags & CF_TRDOS) ? 1 : 0);
            return true;
        case V_PG0:
        case V_PG1:
        case V_PG2:
        case V_PG3:
            if (!x.memory)
                break;
            v = RamPageOf(*x.memory, static_cast<uint8_t>(var - V_PG0));
            return true;
        case V_ROMPG:
            if (!x.memory)
                break;
            v = x.memory->GetMemoryBankMode(0) == BANK_ROM ? x.memory->GetROMPageForBank(0) : 0xFF;
            return true;
    }
    if (error)
        *error = std::string("no ") + NameOf(kVarNames, std::size(kVarNames), var) + " here";
    return false;
}

inline bool Compare(Op op, int64_t a, int64_t b)
{
    switch (op)
    {
        case Op::Lt: return a < b;
        case Op::Gt: return a > b;
        case Op::Le: return a <= b;
        case Op::Ge: return a >= b;
        case Op::Eq: return a == b;
        case Op::Ne: return a != b;
        default: return false;
    }
}

/// The binary operators' values (division and modulo by zero leave the left operand, as in Unreal)
inline int64_t Binary(Op op, int64_t a, int64_t b)
{
    switch (op)
    {
        case Op::Mul: return static_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
        case Op::Div: return b == 0 ? a : a / b;
        case Op::Mod: return b == 0 ? a : a % b;
        case Op::Add: return static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
        case Op::Sub: return static_cast<int64_t>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
        case Op::Shl: return b < 0 || b > 63 ? 0 : static_cast<int64_t>(static_cast<uint64_t>(a) << b);
        case Op::Shr: return b < 0 || b > 63 ? 0 : static_cast<int64_t>(static_cast<uint64_t>(a) >> b);
        case Op::And: return a & b;
        case Op::Xor: return a ^ b;
        case Op::Or: return a | b;
        default: return Compare(op, a, b) ? 1 : 0;
    }
}

const char* OpText(Op op)
{
    switch (op)
    {
        case Op::Mul: return "*";
        case Op::Div: return "/";
        case Op::Mod: return "%";
        case Op::Add: return "+";
        case Op::Sub: return "-";
        case Op::Shl: return "<<";
        case Op::Shr: return ">>";
        case Op::Lt: return "<";
        case Op::Gt: return ">";
        case Op::Le: return "<=";
        case Op::Ge: return ">=";
        case Op::Eq: return "==";
        case Op::Ne: return "!=";
        case Op::And: return "&";
        case Op::Xor: return "^";
        case Op::Or: return "|";
        case Op::JumpIfZero: return "&&";
        case Op::JumpIfNotZero: return "||";
        case Op::Neg: return "-";
        case Op::Not: return "!";
        case Op::BitNot: return "~";
        default: return "?";
    }
}

bool IsCompare(Op op)
{
    return op == Op::Lt || op == Op::Gt || op == Op::Le || op == Op::Ge || op == Op::Eq || op == Op::Ne;
}

/// The comparison with its operands swapped: k < x is x > k
Op Mirror(Op op)
{
    switch (op)
    {
        case Op::Lt: return Op::Gt;
        case Op::Gt: return Op::Lt;
        case Op::Le: return Op::Ge;
        case Op::Ge: return Op::Le;
        default: return op;
    }
}

std::string HexText(int64_t value)
{
    if (value >= 0 && value <= 9)
        return std::to_string(value);
    char text[24];
    if (value < 0)
        std::snprintf(text, sizeof(text), "-#%llX", static_cast<unsigned long long>(-value));
    else
        std::snprintf(text, sizeof(text), "#%llX", static_cast<unsigned long long>(value));
    return text;
}
}  // namespace

/// endregion </Operands>

/// region <Compiler>

class BreakpointConditionCompiler
{
public:
    BreakpointConditionCompiler(const std::string& text, uint8_t trigger, const LabelManager* labels,
                                BreakpointConditionDialect dialect)
        : _text(text), _trigger(trigger), _labels(labels), _dialect(dialect)
    {
    }

    std::shared_ptr<BreakpointConditionProgram> Run(std::string& error)
    {
        Next();
        int root = -1;
        if (_token.kind == Token::End)
            Fail("empty condition");
        else
            root = ParseBinary(1);
        if (_error.empty() && _token.kind != Token::End)
            Fail("unexpected '" + _token.text + "'");
        if (!_error.empty())
        {
            error = _error + " at column " + std::to_string(_errorColumn);
            return nullptr;
        }
        auto program = std::make_shared<BreakpointConditionProgram>();
        program->_source = _text;
        program->_canonical = Print(root, true);  // as written: folding is for the code only
        root = Fold(root);
        program->_usesLabels = _usesLabels;
        int depth = 0, maxDepth = 0;
        Emit(root, program->_code, depth, maxDepth);
        program->_code.push_back({Op::End});
        if (maxDepth > kMaxStack)
        {
            error = "condition too complex (stack depth " + std::to_string(maxDepth) + ", at most " +
                    std::to_string(kMaxStack) + ")";
            return nullptr;
        }
        program->_fast = MatchFast(root);
        return program;
    }

    static constexpr int kMaxStack = 32;

private:
    struct Token
    {
        enum Kind { End, Number, Name, Operator, Open, Close } kind = End;
        std::string text;
        int64_t value = 0;
        size_t column = 0;
    };

    struct Node
    {
        enum Kind { Const, Reg8, Reg16, Var, Mem8, Mem16, Unary, Binary, LogicalAnd, LogicalOr } kind = Const;
        Op op = Op::Const;
        uint8_t arg = 0;
        int64_t value = 0;
        int left = -1, right = -1;
        std::string label;  // a constant that came from a label prints as its name
    };

    /// region <Lexer>

    void Fail(const std::string& message, size_t column = 0)
    {
        if (!_error.empty())
            return;
        _error = message;
        _errorColumn = column ? column : _token.column;
    }

    static bool IsNameChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'; }

    void Next()
    {
        while (_pos < _text.size() && std::isspace(static_cast<unsigned char>(_text[_pos])))
            _pos++;
        _token = Token{};
        _token.column = _pos + 1;
        if (_pos >= _text.size())
            return;
        const char c = _text[_pos];
        if (c == '(' || c == ')')
        {
            _token.kind = c == '(' ? Token::Open : Token::Close;
            _token.text = std::string(1, c);
            _pos++;
            return;
        }
        if (c == '\'')
        {
            if (_pos + 2 < _text.size() && _text[_pos + 2] == '\'')
            {
                _token.kind = Token::Number;
                _token.value = static_cast<uint8_t>(_text[_pos + 1]);
                _token.text = _text.substr(_pos, 3);
                _pos += 3;
                return;
            }
            Fail("unterminated character constant", _pos + 1);
            _pos = _text.size();
            return;
        }
        if (c == '#' || c == '$')
        {
            size_t end = _pos + 1;
            while (end < _text.size() && std::isxdigit(static_cast<unsigned char>(_text[end])))
                end++;
            // 16 digits fill the 64 bits; more would throw in stoull
            if (end == _pos + 1 || end - _pos - 1 > 16 || (end < _text.size() && IsNameChar(_text[end])))
            {
                Fail("bad hex number", _pos + 1);
                _pos = _text.size();
                return;
            }
            _token.kind = Token::Number;
            _token.text = _text.substr(_pos, end - _pos);
            _token.value = static_cast<int64_t>(std::stoull(_text.substr(_pos + 1, end - _pos - 1), nullptr, 16));
            _pos = end;
            return;
        }
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            size_t end = _pos;
            while (end < _text.size() && IsNameChar(_text[end]) && _text[end] != '.')
                end++;
            _token.kind = Token::Number;
            _token.text = _text.substr(_pos, end - _pos);
            if (!ParseNumber(_token.text, _token.value))
                Fail("bad number '" + _token.text + "'", _pos + 1);
            _pos = end;
            return;
        }
        if (IsNameChar(c))
        {
            size_t end = _pos;
            while (end < _text.size() && IsNameChar(_text[end]))
                end++;
            if (end < _text.size() && _text[end] == '\'')  // AF', A'
                end++;
            _token.kind = Token::Name;
            _token.text = _text.substr(_pos, end - _pos);
            _pos = end;
            return;
        }
        static const char* const kOperators[] = {"&&", "||", "==", "!=", "<>", "<=", ">=", "<<", ">>", "->",
                                                 "+", "-", "*", "/", "%", "&", "|", "^", "!", "~", "<", ">", "="};
        for (const char* op : kOperators)
        {
            const size_t n = std::char_traits<char>::length(op);
            if (_text.compare(_pos, n, op) == 0)
            {
                _token.kind = Token::Operator;
                _token.text = op;
                _pos += n;
                return;
            }
        }
        Fail(std::string("unexpected '") + c + "'", _pos + 1);
        _pos = _text.size();
    }

    /// A number token that starts with a digit: 0x.., ..h hex; ..d decimal; otherwise hex (Unreal) or decimal (C)
    bool ParseNumber(const std::string& text, int64_t& value) const
    {
        auto all = [](const std::string& s, int (*pred)(int)) {
            return !s.empty() && std::all_of(s.begin(), s.end(), [pred](char ch) { return pred(static_cast<unsigned char>(ch)) != 0; });
        };
        std::string body = text;
        int base = _dialect == BreakpointConditionDialect::Unreal ? 16 : 10;
        if (body.size() > 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X'))
        {
            body = body.substr(2);
            base = 16;
        }
        else if (body.size() > 1 && (body.back() == 'h' || body.back() == 'H'))
        {
            body.pop_back();
            base = 16;
        }
        else if (body.size() > 1 && (body.back() == 'd' || body.back() == 'D') &&
                 all(body.substr(0, body.size() - 1), std::isdigit))
        {
            body.pop_back();
            base = 10;
        }
        if (base == 16 ? !all(body, std::isxdigit) : !all(body, std::isdigit))
            return false;
        if (body.size() > (base == 16 ? 15u : 18u))
            return false;
        value = static_cast<int64_t>(std::stoull(body, nullptr, base));
        return true;
    }

    /// endregion </Lexer>

    /// region <Parser>

    int Add(Node node)
    {
        _nodes.push_back(std::move(node));
        return static_cast<int>(_nodes.size()) - 1;
    }

    /// The binding power of a binary operator token (0: not one); higher binds tighter
    int Power(const std::string& op, Op& code) const
    {
        const bool unreal = _dialect == BreakpointConditionDialect::Unreal;
        struct Entry
        {
            const char* text;
            Op op;
            int unreal, c;
        };
        static const Entry kTable[] = {
            {"||", Op::JumpIfNotZero, 1, 1}, {"&&", Op::JumpIfZero, 2, 2}, {"|", Op::Or, 3, 3},
            {"^", Op::Xor, 4, 4}, {"&", Op::And, 5, 5},
            {"==", Op::Eq, 6, 6}, {"=", Op::Eq, 6, 6}, {"!=", Op::Ne, 6, 6}, {"<>", Op::Ne, 6, 6},
            {"<", Op::Lt, 6, 7}, {">", Op::Gt, 6, 7}, {"<=", Op::Le, 6, 7}, {">=", Op::Ge, 6, 7},
            {"<<", Op::Shl, 7, 8}, {">>", Op::Shr, 7, 8},
            {"+", Op::Add, 8, 9}, {"-", Op::Sub, 8, 9},
            {"*", Op::Mul, 9, 10}, {"/", Op::Div, 9, 10}, {"%", Op::Mod, 9, 10},
        };
        for (const Entry& e : kTable)
            if (op == e.text)
            {
                code = e.op;
                // Unreal: the comparisons bind tighter than & ^ | (rules.md §5.3); C: looser, and the equalities
                // below the relations
                return unreal ? e.unreal : e.c;
            }
        return 0;
    }

    int ParseBinary(int minPower)
    {
        int left = ParseUnary();
        for (;;)
        {
            if (!_error.empty() || _token.kind != Token::Operator)
                return left;
            Op op = Op::End;
            const int power = Power(_token.text, op);
            if (power == 0 || power < minPower)
                return left;
            Next();
            const int right = ParseBinary(power + 1);  // left-associative
            Node node;
            node.kind = op == Op::JumpIfZero ? Node::LogicalAnd : op == Op::JumpIfNotZero ? Node::LogicalOr : Node::Binary;
            node.op = op;
            node.left = left;
            node.right = right;
            left = Add(node);
        }
    }

    int ParseUnary()
    {
        if (!_error.empty())
            return -1;
        if (_token.kind == Token::Operator && (_token.text == "!" || _token.text == "~" || _token.text == "-"))
        {
            const Op op = _token.text == "!" ? Op::Not : _token.text == "~" ? Op::BitNot : Op::Neg;
            Next();
            Node node;
            node.kind = Node::Unary;
            node.op = op;
            node.left = ParseUnary();
            return Add(node);
        }
        int operand = ParsePrimary();
        // a->b: M(a+b), binding at operand level
        while (_error.empty() && _token.kind == Token::Operator && _token.text == "->")
        {
            Next();
            Node sum;
            sum.kind = Node::Binary;
            sum.op = Op::Add;
            sum.left = operand;
            sum.right = ParsePrimary();
            Node mem;
            mem.kind = Node::Mem8;
            mem.left = Add(sum);
            operand = Add(mem);
        }
        return operand;
    }

    int ParsePrimary()
    {
        if (!_error.empty())
            return -1;
        const Token token = _token;
        switch (token.kind)
        {
            case Token::End: Fail("operand expected"); return -1;
            case Token::Close: Fail("unexpected ')'"); return -1;
            case Token::Operator: Fail("operand expected before '" + token.text + "'"); return -1;
            case Token::Number:
            {
                Next();
                Node node;
                node.value = token.value;
                return Add(node);
            }
            case Token::Open:
            {
                Next();
                const int inner = ParseBinary(1);
                Expect(token.column);
                return inner;
            }
            case Token::Name: break;
        }
        Next();
        std::string upper = token.text;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](char ch) { return static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); });
        uint8_t id = 0;

        // M(x), W(x): memory
        if ((upper == "M" || upper == "W") && _token.kind == Token::Open)
        {
            const size_t open = _token.column;
            Next();
            Node node;
            node.kind = upper == "M" ? Node::Mem8 : Node::Mem16;
            node.left = ParseBinary(1);
            Expect(open);
            return Add(node);
        }
        // A leading '.' forces a label over a register of the same name
        if (upper[0] != '.')
        {
            if (FindName(kReg16Names, std::size(kReg16Names), upper, id))
                return Add(Leaf(Node::Reg16, id));
            if (FindName(kReg8Names, std::size(kReg8Names), upper, id))
                return Add(Leaf(Node::Reg8, id));
            if (FindName(kVarNames, std::size(kVarNames), upper, id))
            {
                const uint8_t needs = TriggersOf(id);
                if (needs && !(needs & _trigger))
                {
                    Fail(token.text + " is not supplied by this breakpoint's trigger", token.column);
                    return -1;
                }
                return Add(Leaf(Node::Var, id));
            }
            // Flag mnemonics (design.md §4.2): C stays the register, CY is the carry
            struct Flag
            {
                const char* name;
                uint8_t mask;
                bool set;
            };
            static const Flag kFlags[] = {{"Z", 0x40, true},   {"NZ", 0x40, false}, {"CY", 0x01, true},
                                          {"NC", 0x01, false}, {"PE", 0x04, true},  {"PO", 0x04, false},
                                          {"FM", 0x80, true},  {"FP", 0x80, false}, {"FH", 0x10, true},
                                          {"FN", 0x02, true}};
            for (const Flag& flag : kFlags)
                if (upper == flag.name)
                {
                    Node mask;
                    mask.value = flag.mask;
                    Node bits;
                    bits.kind = Node::Binary;
                    bits.op = Op::And;
                    bits.left = Add(Leaf(Node::Reg8, R8_F));
                    bits.right = Add(mask);
                    Node zero;
                    Node test;
                    test.kind = Node::Binary;
                    test.op = flag.set ? Op::Ne : Op::Eq;
                    test.left = Add(bits);
                    test.right = Add(zero);
                    return Add(test);
                }
        }
        const std::string labelName = upper[0] == '.' ? token.text.substr(1) : token.text;
        if (_labels && !labelName.empty())
            if (const auto label = _labels->GetLabelByName(labelName))
            {
                _usesLabels = true;
                Node node;
                node.value = label->address;
                node.label = labelName;
                return Add(node);
            }
        Fail("unknown name '" + token.text + "'", token.column);
        return -1;
    }

    static Node Leaf(Node::Kind kind, uint8_t arg)
    {
        Node node;
        node.kind = kind;
        node.arg = arg;
        return node;
    }

    void Expect(size_t openColumn)
    {
        if (!_error.empty())
            return;
        if (_token.kind != Token::Close)
        {
            Fail("missing ')' for '(' at column " + std::to_string(openColumn));
            return;
        }
        Next();
    }

    /// endregion </Parser>

    /// region <Folding, printing, code>

    int Fold(int index)
    {
        if (index < 0)
            return index;
        Node& node = _nodes[index];
        if (node.left >= 0)
            node.left = Fold(node.left);
        if (node.right >= 0)
            node.right = Fold(node.right);
        const Node& n = _nodes[index];
        auto isConst = [&](int i) { return i >= 0 && _nodes[i].kind == Node::Const && _nodes[i].label.empty(); };
        Node folded;
        switch (n.kind)
        {
            case Node::Unary:
                if (!isConst(n.left))
                    return index;
                folded.value = n.op == Op::Neg ? -_nodes[n.left].value
                               : n.op == Op::Not ? (_nodes[n.left].value == 0 ? 1 : 0)
                                                 : ~_nodes[n.left].value;
                break;
            case Node::Binary:
                if (!isConst(n.left) || !isConst(n.right))
                    return index;
                folded.value = Binary(n.op, _nodes[n.left].value, _nodes[n.right].value);
                break;
            case Node::LogicalAnd:
            case Node::LogicalOr:
                if (!isConst(n.left) || !isConst(n.right))
                    return index;
                folded.value = n.kind == Node::LogicalAnd ? (_nodes[n.left].value && _nodes[n.right].value)
                                                          : (_nodes[n.left].value || _nodes[n.right].value);
                break;
            default: return index;
        }
        return Add(folded);
    }

    std::string Print(int index, bool top) const
    {
        const Node& n = _nodes[index];
        switch (n.kind)
        {
            case Node::Const: return n.label.empty() ? HexText(n.value) : n.label;
            case Node::Reg8: return NameOf(kReg8Names, std::size(kReg8Names), n.arg);
            case Node::Reg16: return NameOf(kReg16Names, std::size(kReg16Names), n.arg);
            case Node::Var: return NameOf(kVarNames, std::size(kVarNames), n.arg);
            case Node::Mem8: return "M(" + Print(n.left, true) + ")";
            case Node::Mem16: return "W(" + Print(n.left, true) + ")";
            case Node::Unary: return std::string(OpText(n.op)) + Print(n.left, false);
            default:
            {
                const std::string text = Print(n.left, false) + " " + OpText(n.op) + " " + Print(n.right, false);
                return top ? text : "(" + text + ")";
            }
        }
    }

    void Push(std::vector<BreakpointConditionProgram::Instruction>& code, BreakpointConditionProgram::Instruction instruction,
              int& depth, int& maxDepth, int delta) const
    {
        code.push_back(instruction);
        depth += delta;
        maxDepth = std::max(maxDepth, depth);
    }

    void Emit(int index, std::vector<BreakpointConditionProgram::Instruction>& code, int& depth, int& maxDepth) const
    {
        const Node& n = _nodes[index];
        using Instruction = BreakpointConditionProgram::Instruction;
        switch (n.kind)
        {
            case Node::Const: Push(code, {Op::Const, 0, 0, n.value}, depth, maxDepth, 1); return;
            case Node::Reg8: Push(code, {Op::Reg8, n.arg}, depth, maxDepth, 1); return;
            case Node::Reg16: Push(code, {Op::Reg16, n.arg}, depth, maxDepth, 1); return;
            case Node::Var: Push(code, {Op::Var, n.arg}, depth, maxDepth, 1); return;
            case Node::Mem8:
            case Node::Mem16:
                Emit(n.left, code, depth, maxDepth);
                Push(code, Instruction{n.kind == Node::Mem8 ? Op::Mem8 : Op::Mem16}, depth, maxDepth, 0);
                return;
            case Node::Unary:
                Emit(n.left, code, depth, maxDepth);
                Push(code, Instruction{n.op}, depth, maxDepth, 0);
                return;
            case Node::Binary:
                Emit(n.left, code, depth, maxDepth);
                Emit(n.right, code, depth, maxDepth);
                Push(code, Instruction{n.op}, depth, maxDepth, -1);
                return;
            case Node::LogicalAnd:
            case Node::LogicalOr:
            {
                // a && b: a, jump-if-zero to the end keeping it; else pop, b; then normalise to 0 / 1
                Emit(n.left, code, depth, maxDepth);
                const size_t jump = code.size();
                Push(code, Instruction{n.kind == Node::LogicalAnd ? Op::JumpIfZero : Op::JumpIfNotZero}, depth, maxDepth, -1);
                Emit(n.right, code, depth, maxDepth);
                code[jump].target = static_cast<uint16_t>(code.size());
                Push(code, Instruction{Op::Bool}, depth, maxDepth, 0);
                return;
            }
        }
    }

    /// `operand <cmp> constant` (either side), or a lone operand (non-zero)
    BreakpointConditionProgram::Fast MatchFast(int root) const
    {
        using Fast = BreakpointConditionProgram::Fast;
        Fast fast;
        auto operand = [&](int index, Fast& f) {
            const Node& n = _nodes[index];
            switch (n.kind)
            {
                case Node::Reg8: f.kind = Fast::Kind::Reg8; f.arg = n.arg; return true;
                case Node::Reg16: f.kind = Fast::Kind::Reg16; f.arg = n.arg; return true;
                case Node::Var: f.kind = Fast::Kind::Var; f.arg = n.arg; return true;
                case Node::Mem8:
                    if (_nodes[n.left].kind != Node::Const || _nodes[n.left].value < 0 || _nodes[n.left].value > 0xFFFF)
                        return false;
                    f.kind = Fast::Kind::Mem8;
                    f.address = static_cast<uint16_t>(_nodes[n.left].value);
                    return true;
                case Node::Binary:
                    // (reg8 & mask), mask a byte
                    if (n.op == Op::And && _nodes[n.left].kind == Node::Reg8 && _nodes[n.right].kind == Node::Const &&
                        _nodes[n.right].value >= 0 && _nodes[n.right].value <= 0xFF)
                    {
                        f.kind = Fast::Kind::Reg8;
                        f.arg = _nodes[n.left].arg;
                        f.mask = static_cast<uint8_t>(_nodes[n.right].value);
                        return true;
                    }
                    return false;
                default: return false;
            }
        };
        const Node& n = _nodes[root];
        if (n.kind == Node::Binary && IsCompare(n.op))
        {
            if (_nodes[n.right].kind == Node::Const && operand(n.left, fast))
            {
                fast.compare = n.op;
                fast.constant = _nodes[n.right].value;
                return fast;
            }
            fast = Fast{};
            if (_nodes[n.left].kind == Node::Const && operand(n.right, fast))
            {
                fast.compare = Mirror(n.op);
                fast.constant = _nodes[n.left].value;
                return fast;
            }
            return Fast{};
        }
        if (operand(root, fast))
        {
            fast.compare = Op::Ne;
            fast.constant = 0;
            return fast;
        }
        return Fast{};
    }

    /// endregion </Folding, printing, code>

    const std::string& _text;
    uint8_t _trigger;
    const LabelManager* _labels;
    BreakpointConditionDialect _dialect;
    size_t _pos = 0;
    Token _token;
    std::vector<Node> _nodes;
    std::string _error;
    size_t _errorColumn = 0;
    bool _usesLabels = false;
};

/// endregion </Compiler>

/// region <Program>

std::shared_ptr<const BreakpointConditionProgram> BreakpointConditionProgram::Compile(
    const std::string& text, uint8_t trigger, const LabelManager* labels, std::string& error,
    BreakpointConditionDialect dialect, const BreakpointConditionContext* dryRun)
{
    BreakpointConditionCompiler compiler(text, trigger, labels, dialect);
    std::shared_ptr<BreakpointConditionProgram> program = compiler.Run(error);
    if (!program)
        return nullptr;
    if (dryRun)
    {
        std::string why;
        if (program->Evaluate(*dryRun, &why) == BreakpointConditionResult::Error)
        {
            error = "the condition does not evaluate here: " + why;
            return nullptr;
        }
    }
    return program;
}

bool BreakpointConditionProgram::ParseDialect(const std::string& text, BreakpointConditionDialect& dialect)
{
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); });
    if (lower.empty() || lower == "unreal")
        dialect = BreakpointConditionDialect::Unreal;
    else if (lower == "c")
        dialect = BreakpointConditionDialect::C;
    else
        return false;
    return true;
}

BreakpointConditionResult BreakpointConditionProgram::Evaluate(const BreakpointConditionContext& x,
                                                               std::string* error) const noexcept
{
    if (_fast.kind == Fast::Kind::None)
        return EvaluateCode(x, error);
    int64_t v = 0;
    switch (_fast.kind)
    {
        case Fast::Kind::Reg8:
            if (!x.cpu)
                return EvaluateCode(x, error);
            v = ReadReg8(*x.cpu, _fast.arg) & _fast.mask;
            break;
        case Fast::Kind::Reg16:
            if (!x.cpu)
                return EvaluateCode(x, error);
            v = ReadReg16(*x.cpu, _fast.arg);
            break;
        case Fast::Kind::Var:
            if (!ReadVar(x, _fast.arg, v, error))
                return BreakpointConditionResult::Error;
            break;
        case Fast::Kind::Mem8:
            if (!x.memory)
                return EvaluateCode(x, error);
            v = x.memory->DirectReadFromZ80Memory(_fast.address);
            break;
        case Fast::Kind::None: break;
    }
    return Compare(_fast.compare, v, _fast.constant) ? BreakpointConditionResult::True : BreakpointConditionResult::False;
}

BreakpointConditionResult BreakpointConditionProgram::EvaluateCode(const BreakpointConditionContext& x,
                                                                   std::string* error) const noexcept
{
    int64_t stack[BreakpointConditionCompiler::kMaxStack];
    int sp = 0;
    auto fail = [error](const char* why) {
        if (error)
            *error = why;
        return BreakpointConditionResult::Error;
    };
    const Instruction* code = _code.data();
    for (size_t pc = 0;; pc++)
    {
        const Instruction& in = code[pc];
        switch (in.op)
        {
            case Op::Const: stack[sp++] = in.value; break;
            case Op::Reg8:
                if (!x.cpu)
                    return fail("no CPU");
                stack[sp++] = ReadReg8(*x.cpu, in.arg);
                break;
            case Op::Reg16:
                if (!x.cpu)
                    return fail("no CPU");
                stack[sp++] = ReadReg16(*x.cpu, in.arg);
                break;
            case Op::Var:
                if (!ReadVar(x, in.arg, stack[sp], error))
                    return BreakpointConditionResult::Error;
                sp++;
                break;
            case Op::Mem8:
            case Op::Mem16:
            {
                if (!x.memory)
                    return fail("no memory");
                // A CPU address: it wraps at 64K as the Z80's own does
                const uint16_t address = static_cast<uint16_t>(stack[sp - 1]);
                int64_t v = x.memory->DirectReadFromZ80Memory(address);
                if (in.op == Op::Mem16)
                    v |= static_cast<int64_t>(x.memory->DirectReadFromZ80Memory(static_cast<uint16_t>(address + 1))) << 8;
                stack[sp - 1] = v;
                break;
            }
            case Op::Neg: stack[sp - 1] = -stack[sp - 1]; break;
            case Op::Not: stack[sp - 1] = stack[sp - 1] == 0 ? 1 : 0; break;
            case Op::BitNot: stack[sp - 1] = ~stack[sp - 1]; break;
            case Op::JumpIfZero:
                if (stack[sp - 1] == 0)
                    pc = in.target - 1u;
                else
                    sp--;
                break;
            case Op::JumpIfNotZero:
                if (stack[sp - 1] != 0)
                    pc = in.target - 1u;
                else
                    sp--;
                break;
            case Op::Bool: stack[sp - 1] = stack[sp - 1] != 0 ? 1 : 0; break;
            case Op::End:
                return sp > 0 && stack[sp - 1] != 0 ? BreakpointConditionResult::True : BreakpointConditionResult::False;
            default:
            {
                const int64_t b = stack[--sp];
                stack[sp - 1] = Binary(in.op, stack[sp - 1], b);
                break;
            }
        }
    }
}

/// endregion </Program>
