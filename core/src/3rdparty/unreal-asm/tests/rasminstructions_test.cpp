// rasm's instruction layer against the rasm binary: testdata/rasm/instructions.tsv and timings.tsv
// (tools/verification/unreal-asm/checks/rasminstrtable.py) hold what rasm wrote or reported for every instruction form
// of a pool of operands, assembled alone at org #100. The test splits each form into words as rasm's preprocessor
// does, encodes it, evaluates the fixups with a small evaluator (numbers, $, characters, + - * /, parentheses) and
// writes them with ApplyFixup.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

#include "dialects/rasm/engine/rasminstructions.h"
#include "testdata.h"

namespace
{
using namespace unrealasm::rasm;

constexpr uint32_t org = 0x100;

struct Statement
{
    std::string mnemonic;
    std::vector<std::string> operands;
};

/// What rasm's preprocessor hands to an instruction function: upper case and no blanks outside quotes, operands split
/// at the commas outside parentheses and quotes, AF' after a comma as AF
Statement Split(const std::string& form)
{
    Statement s;
    size_t i = 0;
    while (i < form.size() && form[i] == ' ')
        i++;
    while (i < form.size() && form[i] != ' ' && form[i] != '\t')
        s.mnemonic += static_cast<char>(std::toupper(static_cast<unsigned char>(form[i++])));
    std::string word;
    bool any = false;
    int depth = 0;
    char quote = 0;
    for (; i < form.size(); i++)
    {
        const char c = form[i];
        if (quote)
        {
            word += c;
            if (c == '\\' && i + 1 < form.size())
                word += form[++i];
            else if (c == quote)
                quote = 0;
            continue;
        }
        if (c == ' ' || c == '\t')
            continue;
        any = true;
        if ((c == '\'' || c == '"') && !(c == '\'' && word == "AF"))
        {
            quote = c;
            word += c;
        }
        else if (c == ',' && depth == 0)
        {
            s.operands.push_back(word);
            word.clear();
        }
        else
        {
            if (c == '(')
                depth++;
            else if (c == ')')
                depth--;
            word += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    if (any)
        s.operands.push_back(word);
    for (size_t n = 1; n < s.operands.size(); n++)
        if (s.operands[n] == "AF'")
            s.operands[n] = "AF";
    return s;
}

/// A small expression evaluator for the operands of the tables: decimal (with a fraction), #hex, %binary, $, 'c',
/// + - * /, parentheses, unary minus; anything else (a register name) fails
class Evaluator
{
public:
    Evaluator(const std::string& text, double dollar) : _t(text), _dollar(dollar) {}
    std::optional<double> Run()
    {
        std::optional<double> v = Sum();
        if (!v || _p != _t.size())
            return std::nullopt;
        return v;
    }

private:
    const std::string& _t;
    double _dollar;
    size_t _p = 0;

    std::optional<double> Sum()
    {
        std::optional<double> v = Product();
        while (v && _p < _t.size() && (_t[_p] == '+' || _t[_p] == '-'))
        {
            const char op = _t[_p++];
            std::optional<double> r = Product();
            if (!r)
                return std::nullopt;
            v = op == '+' ? *v + *r : *v - *r;
        }
        return v;
    }
    std::optional<double> Product()
    {
        std::optional<double> v = Unary();
        while (v && _p < _t.size() && (_t[_p] == '*' || _t[_p] == '/'))
        {
            const char op = _t[_p++];
            std::optional<double> r = Unary();
            if (!r || (op == '/' && *r == 0))
                return std::nullopt;
            v = op == '*' ? *v * *r : *v / *r;
        }
        return v;
    }
    std::optional<double> Unary()
    {
        if (_p < _t.size() && _t[_p] == '-')
        {
            _p++;
            std::optional<double> v = Unary();
            return v ? std::optional<double>(-*v) : std::nullopt;
        }
        if (_p < _t.size() && _t[_p] == '+')
        {
            _p++;
            return Unary();
        }
        return Primary();
    }
    std::optional<double> Primary()
    {
        if (_p >= _t.size())
            return std::nullopt;
        const char c = _t[_p];
        if (c == '(')
        {
            _p++;
            std::optional<double> v = Sum();
            if (!v || _p >= _t.size() || _t[_p] != ')')
                return std::nullopt;
            _p++;
            return v;
        }
        if (c == '$')
        {
            _p++;
            return _dollar;
        }
        if (c == '\'')
        {
            size_t q = _p + 1;
            if (q < _t.size() && _t[q] == '\\')
                q++;
            if (q + 1 >= _t.size() || _t[q + 1] != '\'')
                return std::nullopt;
            _p = q + 2;
            return static_cast<unsigned char>(_t[q]);
        }
        if (c == '#' || c == '%')
        {
            const int base = c == '#' ? 16 : 2;
            _p++;
            double v = 0;
            size_t start = _p;
            while (_p < _t.size() && std::isxdigit(static_cast<unsigned char>(_t[_p])))
            {
                const int d = std::isdigit(static_cast<unsigned char>(_t[_p])) ? _t[_p] - '0' : (_t[_p] & 0xDF) - 'A' + 10;
                if (d >= base)
                    return std::nullopt;
                v = v * base + d;
                _p++;
            }
            if (_p == start)
                return std::nullopt;
            return v;
        }
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            size_t start = _p;
            while (_p < _t.size() && (std::isdigit(static_cast<unsigned char>(_t[_p])) || _t[_p] == '.'))
                _p++;
            return std::stod(_t.substr(start, _p - start));
        }
        return std::nullopt;
    }
};

std::optional<double> Evaluate(const std::string& text, double dollar)
{
    return Evaluator(text, dollar).Run();
}

/// rasm's in-place translation of an operand word as its error texts quote it: `$` -> decimal, 'c' -> #hex
std::string Translated(const std::string& text, uint32_t dollar)
{
    std::string out;
    for (size_t i = 0; i < text.size(); i++)
    {
        if (text[i] == '$')
            out += std::to_string(dollar);
        else if (text[i] == '\'' && i + 2 < text.size() && text[i + 1] != '\\' && text[i + 2] == '\'')
        {
            char buffer[8];
            std::snprintf(buffer, sizeof(buffer), "#%X", static_cast<unsigned char>(text[i + 1]));
            out += buffer;
            i += 2;
        }
        else
            out += text[i];
    }
    return out;
}

struct Assembled
{
    std::vector<uint8_t> bytes;
    std::vector<std::string> errors;     ///< from the encoder and ApplyFixup
    std::vector<std::string> warnings;
    bool unevaluated = false;            ///< the small evaluator could not compute an operand (rasm: some error)
    int nops = 0;
    int ticks = 0;
};

Assembled Assemble(const std::string& form, const EncoderOptions& options = EncoderOptions())
{
    const Statement s = Split(form);
    Assembled a;
    const Encoded e = EncodeInstruction(s.mnemonic, s.operands, options,
                                        [&](const std::string& text)
                                        {
                                            std::optional<double> v = Evaluate(text, org);
                                            if (!v)
                                                a.unevaluated = true;
                                            return v;
                                        });
    a.bytes = e.bytes;
    a.nops = e.nops;
    a.ticks = e.ticks;
    a.warnings = e.warnings;
    if (!e.error.empty())
        a.errors.push_back(e.error);
    for (const Fixup& f : e.fixups)
    {
        std::optional<double> v = Evaluate(f.expression, org + f.instructionOffset);
        if (!v)
        {
            a.unevaluated = true;
            continue;
        }
        const FixupResult r = ApplyFixup(a.bytes, f, *v, org, options, Translated(f.expression, org + f.instructionOffset));
        if (!r.error.empty())
            a.errors.push_back(r.error);
        if (!r.warning.empty())
            a.warnings.push_back(r.warning);
    }
    return a;
}

std::string Hex(const std::vector<uint8_t>& bytes)
{
    static const char digits[] = "0123456789ABCDEF";
    std::string s;
    for (uint8_t b : bytes)
    {
        s += digits[b >> 4];
        s += digits[b & 15];
    }
    return s;
}

std::vector<std::string> Columns(const std::string& line)
{
    std::vector<std::string> out;
    std::string cell;
    std::istringstream in(line);
    while (std::getline(in, cell, '\t'))
        out.push_back(cell);
    return out;
}

std::vector<std::string> SplitErrors(const std::string& text)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (true)
    {
        const size_t end = text.find(" | ", start);
        out.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            return out;
        start = end + 3;
    }
}

std::vector<std::vector<std::string>> Table(const char* name)
{
    std::vector<std::vector<std::string>> rows;
    std::istringstream in(unrealasm::testing::ReadTestText(std::string("rasm/") + name));
    std::string line;
    while (std::getline(in, line))
        if (!line.empty())
            rows.push_back(Columns(line));
    return rows;
}

TEST(RasmInstructions, EveryFormMatchesRasm)
{
    const auto rows = Table("instructions.tsv");
    ASSERT_GT(rows.size(), 10000u);
    size_t accepted = 0;
    size_t rejected = 0;
    size_t failures = 0;
    for (auto row : rows)
    {
        if (row.size() < 2)
            row.push_back(std::string());  // rasm accepted the form and wrote nothing (NOP 0)
        const std::string& form = row[0];
        const std::string& expected = row[1];
        // the documented difference: a bare RLC / RRC / SRL8 as the last statement of a file is silently empty in rasm
        // (it reads past the statement); the port rejects it as rasm does when another statement follows
        if (form == "RLC" || form == "RRC" || form == "SRL8")
        {
            EXPECT_FALSE(Assemble(form).errors.empty()) << form;
            continue;
        }
        const Assembled a = Assemble(form);
        std::string problem;
        if (!expected.empty() && expected[0] == '!')
        {
            rejected++;
            const std::vector<std::string> rasmErrors = SplitErrors(expected.substr(1));
            if (a.errors.empty() && !a.unevaluated)
                problem = "accepted (" + Hex(a.bytes) + "), rasm: " + expected;
            else if (!a.unevaluated &&
                     std::find(rasmErrors.begin(), rasmErrors.end(), a.errors.front()) == rasmErrors.end())
                problem = "error '" + a.errors.front() + "', rasm: " + expected;
        }
        else
        {
            accepted++;
            const std::string warning = row.size() > 2 && row[2][0] == '?' ? row[2].substr(1) : std::string();
            if (!a.errors.empty() || a.unevaluated)
                problem = "rejected (" + (a.errors.empty() ? std::string("unevaluated") : a.errors.front()) +
                          "), rasm: " + expected;
            else if (Hex(a.bytes) != expected)
                problem = Hex(a.bytes) + ", rasm: " + expected;
            else if ((a.warnings.empty() ? std::string() : a.warnings.front()) != warning)
                problem = "warning '" + (a.warnings.empty() ? std::string() : a.warnings.front()) + "', rasm: '" +
                          warning + "'";
        }
        if (!problem.empty() && ++failures <= 40)
            ADD_FAILURE() << form << ": " << problem;
    }
    EXPECT_EQ(failures, 0u);
    EXPECT_GT(accepted, 1000u);
    EXPECT_GT(rejected, 1000u);
}

TEST(RasmInstructions, TimingsMatchRasm)
{
    const auto rows = Table("timings.tsv");
    ASSERT_GT(rows.size(), 1000u);
    size_t failures = 0;
    auto check = [&](const std::string& form, const char* what, const TimingSum& got, const std::string& expected)
    {
        if (expected == "?")
            return;  // rasm crashes on the form (a missing argument it dereferences)
        const bool same = expected[0] == '!' ? got.error == expected.substr(1)
                                             : got.error.empty() && std::to_string(got.value) == expected;
        if (!same && ++failures <= 40)
            ADD_FAILURE() << what << "(\"" << form << "\"): " << (got.error.empty() ? std::to_string(got.value) : "!" + got.error)
                          << ", rasm: " << expected;
    };
    for (const auto& row : rows)
    {
        ASSERT_EQ(row.size(), 5u) << row[0];
        const std::string& form = row[0];
        if (row[1] != "-")
        {
            const Assembled a = Assemble(form);
            const std::string got = std::to_string(a.nops) + "/" + std::to_string(a.ticks);
            if (got != row[1] && ++failures <= 40)
                ADD_FAILURE() << "TICKER " << form << ": " << got << ", rasm: " << row[1];
        }
        check(form, "GETNOP", GetNop(form), row[2]);
        check(form, "GETTICK", GetTick(form), row[3]);
        check(form, "GETSIZE", GetSize(form), row[4]);
    }
    EXPECT_EQ(failures, 0u);
}

TEST(RasmInstructions, InstructionTiming)
{
    Timing t = InstructionTiming("LD", {"HL", "(IX+5)"});
    EXPECT_TRUE(t.known);
    EXPECT_EQ(t.nops, 10);
    EXPECT_EQ(t.ticks, 38);
    EXPECT_EQ(t.size, 6);
    t = InstructionTiming("NOP", {"#10"});
    EXPECT_TRUE(t.known);
    EXPECT_EQ(t.nops, 16);
    EXPECT_EQ(t.size, 16);
    EXPECT_FALSE(InstructionTiming("NOP", {"X"}).known);
    EXPECT_FALSE(InstructionTiming("LD", {"A"}).known);
    EXPECT_EQ(GetNop("ld\ta,b").value, 1);  // tabs are blanks (rasm 1, not in timings.tsv: a TSV cell)
    EXPECT_EQ(Duration("ld a,b:ld (ix+1),a", false).value, 6);
    EXPECT_EQ(Duration("ld a,b:ld (ix+1),a", true).value, 23);
}

TEST(RasmInstructions, SuperInstructions)
{
    EXPECT_EQ(Hex(Assemble("push bc,de,hl,ix").bytes), "C5D5E5DDE5");
    EXPECT_EQ(Hex(Assemble("inc a,b,(ix+3)").bytes), "3C04DD3403");
    EXPECT_EQ(Hex(Assemble("ld hl,(ix+5)").bytes), "DD6606DD6E05");
    EXPECT_EQ(Hex(Assemble("ld (iy-2),de").bytes), "FD72FFFD73FE");
    EXPECT_EQ(Hex(Assemble("ld bc,de").bytes), "424B");
    EXPECT_EQ(Hex(Assemble("ld hl,sp").bytes), "210000" "39");
    EXPECT_EQ(Hex(Assemble("srl hl").bytes), "CB3CCB1D");
    EXPECT_EQ(Hex(Assemble("rlc de").bytes), "CB22CB13CB1ACB02");
    EXPECT_EQ(Hex(Assemble("srl8 ix").bytes), "DD6CDD2600");
    EXPECT_EQ(Hex(Assemble("rst nc,#38").bytes), "30FF");
    EXPECT_EQ(Hex(Assemble("nop 3").bytes), "000000");
    EXPECT_EQ(Hex(Assemble("ldi 2").bytes), "EDA0EDA0");
    EXPECT_EQ(Hex(Assemble("exa").bytes), "08");
    EXPECT_EQ(Hex(Assemble("sl1 b").bytes), "CB30");
    EXPECT_EQ(Hex(Assemble("set 3,(ix+1),a").bytes), "DDCB01DF");
}

TEST(RasmInstructions, RejectedForms)
{
    // rasm's AUTOTEST_INSTRMUSTFAILED: every statement of it is an error
    for (const char* form : {"ld a,b,c", "ldi a",  "ldir bc", "exx hl,de", "ex bc,hl", "ex af,bc", "ex hl,hl",
                             "neg b",    "push b", "push",    "pop c",     "sub ix",   "add ix",   "add",
                             "di 2",     "ld i,c", "rl",      "rlca a",    "sla",      "adc ix",   "adc b,a",
                             "xor 12,13", "xor b,1", "or",    "and b,1",   "inc",      "dec"})
    {
        const Assembled a = Assemble(form);
        EXPECT_TRUE(!a.errors.empty() || a.unevaluated) << form;
    }
    EXPECT_EQ(Assemble("ld a,(ix+200)").errors.front(),
              "offset modifier (IX+200) must be between -128 and 127 inclusive : current value = 200");
    EXPECT_EQ(Assemble("jr 0").errors.front(), "relative offset -258 too far [0]");
    EXPECT_EQ(Assemble("im 3").errors.front(), "IM 0,1 or 2 only");
    EXPECT_EQ(Assemble("ld a,300").warnings.front(), "truncating value #12C to #2C");
    EncoderOptions quiet;
    quiet.noWarning = true;
    EXPECT_TRUE(Assemble("ld a,300", quiet).warnings.empty());
}

TEST(RasmInstructions, FixupOffsetsAndExpressions)
{
    const Encoded e = EncodeInstruction("LD", {"HL", "(IY-3)"}, EncoderOptions(), nullptr);
    ASSERT_EQ(e.fixups.size(), 2u);
    EXPECT_EQ(e.fixups[0].kind, FixupKind::IV81);
    EXPECT_EQ(e.fixups[0].offset, 2u);
    EXPECT_EQ(e.fixups[0].expression, "(0-3)");
    EXPECT_EQ(e.fixups[0].extra, 0xFD);
    EXPECT_EQ(e.fixups[1].kind, FixupKind::IV8);
    EXPECT_EQ(e.fixups[1].offset, 5u);
    EXPECT_EQ(e.fixups[1].instructionOffset, 0u);
    EncoderOptions loop;
    loop.insideLoopOrMacro = true;
    EXPECT_EQ(EncodeInstruction("LD", {"HL", "(IY-3)"}, loop, nullptr).fixups[1].instructionOffset, 3u);

    const Encoded b = EncodeInstruction("SET", {"5", "(IX+1)"}, EncoderOptions(), nullptr);
    ASSERT_EQ(b.fixups.size(), 2u);
    EXPECT_EQ(b.fixups[1].kind, FixupKind::BitResSet);
    EXPECT_EQ(b.fixups[1].offset, 3u);
    EXPECT_EQ(b.fixups[1].instructionOffset, 3u);
    EXPECT_EQ(b.fixups[1].extra, 0xC6);
    EXPECT_TRUE(IsInstruction("SRL8"));
    EXPECT_TRUE(IsInstruction("OTI"));
    EXPECT_FALSE(IsInstruction("DEFB"));
    EXPECT_FALSE(IsInstruction("API_SEND"));
}
}  // namespace
