#include "dialects/zasm/zasmcompound.h"

#include <algorithm>

#include "dialects/common/z80.h"

namespace unrealasm::dialects
{
namespace
{
using ir::Expr;
using ir::Operand;
using ir::Statement;

Operand Reg(const std::string& name)
{
    Operand o;
    o.kind = Operand::Kind::Register;
    o.text = name;
    return o;
}

Operand Ind(const std::string& name)
{
    Operand o;
    o.kind = Operand::Kind::Indirect;
    o.text = name;
    return o;
}

Statement Ins(const std::string& mnemonic, std::vector<Operand> operands)
{
    Statement s;
    s.kind = Statement::Kind::Instruction;
    s.mnemonic = mnemonic;
    s.operands = std::move(operands);
    return s;
}

/// The halves of a register pair: bc -> b c, ix -> ixh ixl
bool Halves(const std::string& pair, std::string& hi, std::string& lo)
{
    if (pair == "bc") { hi = "b"; lo = "c"; return true; }
    if (pair == "de") { hi = "d"; lo = "e"; return true; }
    if (pair == "hl") { hi = "h"; lo = "l"; return true; }
    if (pair == "ix") { hi = "ixh"; lo = "ixl"; return true; }
    if (pair == "iy") { hi = "iyh"; lo = "iyl"; return true; }
    return false;
}

bool IsPair(const Operand& o)
{
    std::string hi, lo;
    return o.kind == Operand::Kind::Register && Halves(o.text, hi, lo);
}

enum class Step { None, Post, Pre };

/// (HL++) / (--HL) and the same for BC and DE: a memory operand whose text could not be an expression
Step Increment(const Operand& o, std::string& reg)
{
    if (o.kind != Operand::Kind::Memory)
        return Step::None;
    // (--hl) reads as -(-hl): a name under two negations
    if (o.expr.kind == Expr::Kind::Unary && o.expr.op == ir::Op::Negate && o.expr.args[0].kind == Expr::Kind::Unary &&
        o.expr.args[0].op == ir::Op::Negate && o.expr.args[0].args[0].kind == Expr::Kind::Symbol)
    {
        const std::string name = z80::Lower(o.expr.args[0].args[0].text);
        if (name == "hl" || name == "bc" || name == "de")
        {
            reg = name;
            return Step::Pre;
        }
    }
    if (o.expr.kind != Expr::Kind::Raw)
        return Step::None;
    std::string t = z80::Lower(o.expr.text);
    t.erase(std::remove(t.begin(), t.end(), ' '), t.end());
    for (const char* r : {"hl", "bc", "de"})
    {
        if (t == std::string(r) + "++")
        {
            reg = r;
            return Step::Post;
        }
        if (t == std::string("--") + r)
        {
            reg = r;
            return Step::Pre;
        }
    }
    return Step::None;
}

Operand IndexedPlus(const Operand& o, int n)
{
    Operand r = o;
    r.expr = Expr::Binary(ir::Op::Add, o.expr, Expr::Number(n));
    return r;
}

bool PlainMemory(const Operand& o, const std::string& reg)
{
    return o.kind == Operand::Kind::Indirect && o.text == reg;
}
}  // namespace

bool ExpandZasmCompound(const Statement& s, std::vector<Statement>& out)
{
    const std::string& m = s.mnemonic;
    const auto& ops = s.operands;
    std::string hi, lo, reg, rhi, rlo;

    // LD rr,rr'
    if (m == "ld" && ops.size() == 2 && IsPair(ops[0]) && IsPair(ops[1]) && ops[0].text != "sp")
    {
        Halves(ops[0].text, hi, lo);
        Halves(ops[1].text, rhi, rlo);
        out = {Ins("ld", {Reg(hi), Reg(rhi)}), Ins("ld", {Reg(lo), Reg(rlo)})};
        return true;
    }
    // LD rr,(hl) / (hl++) / (--hl) / (ix+d), and the stores
    for (int store = 0; store < 2; ++store)
    {
        if (m != "ld" || ops.size() != 2)
            break;
        const Operand& pairOp = ops[store ? 1 : 0];
        const Operand& mem = ops[store ? 0 : 1];
        if (!IsPair(pairOp) || pairOp.text == "ix" || pairOp.text == "iy" || !Halves(pairOp.text, hi, lo))
            continue;
        const auto load = [&](const std::string& half, const Operand& at) {
            return store ? Ins("ld", {at, Reg(half)}) : Ins("ld", {Reg(half), at});
        };
        if (mem.kind == Operand::Kind::Indexed)
        {
            out = {load(lo, mem), load(hi, IndexedPlus(mem, 1))};
            return true;
        }
        const Step step = Increment(mem, reg);
        const bool plain = PlainMemory(mem, "hl");
        if (plain || step != Step::None)
        {
            if (!plain && reg != "hl")
                continue;
            if (pairOp.text == "hl")
                continue;   // LD HL,(HL) is no such compound
            const Operand at = Ind("hl");
            if (step == Step::Pre)
                out = {Ins("dec", {Reg("hl")}), load(hi, at), Ins("dec", {Reg("hl")}), load(lo, at)};
            else
                out = {load(lo, at), Ins("inc", {Reg("hl")}), load(hi, at), Ins(step == Step::Post ? "inc" : "dec", {Reg("hl")})};
            return true;
        }
    }
    // RR BC, SRL HL ...: two 8-bit rotates
    if (ops.size() == 1 && IsPair(ops[0]) && (ops[0].text == "bc" || ops[0].text == "de" || ops[0].text == "hl"))
    {
        Halves(ops[0].text, hi, lo);
        if (m == "rr" || m == "sra" || m == "srl")
        {
            out = {Ins(m == "rr" ? "rr" : m, {Reg(hi)}), Ins("rr", {Reg(lo)})};
            return true;
        }
        if (m == "rl" || m == "sla" || m == "sll" || m == "sli")
        {
            out = {Ins(m == "rl" ? "rl" : m, {Reg(lo)}), Ins("rl", {Reg(hi)})};
            return true;
        }
    }
    // One operand written (hl++) / (--hl) / (bc++) ...: the instruction on (rr) with the step around it
    for (size_t k = 0; k < ops.size(); ++k)
    {
        const Step step = Increment(ops[k], reg);
        if (step == Step::None)
            continue;
        Statement plain = s;
        plain.operands[k] = Ind(reg);
        if (step == Step::Pre)
            out = {Ins("dec", {Reg(reg)}), plain};
        else
            out = {plain, Ins("inc", {Reg(reg)})};
        return true;
    }
    return false;
}
}  // namespace unrealasm::dialects
