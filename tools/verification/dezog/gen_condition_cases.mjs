#!/usr/bin/env node
// Generates testdata/debugger/conditions/dezog-cases.tsv: random DeZog breakpoint conditions with random registers
// and the truth DeZog gives them (dzrpremote.ts checkConditionAndLog -> Utility.evalExpression), so core-tests can
// check dzrp::translateCondition against real JavaScript (core/tests/automation/dezog/dezogcondition_test.cpp).
//
// The token replacement and parseValue below are DeZog's (src/misc/utility.ts, src/labels/labels.ts
// getNumberFromString64k) with no labels loaded. Line: condition TAB 15 hex registers TAB 1 / 0 / E (an error).
//
//   node tools/verification/dezog/gen_condition_cases.mjs > testdata/debugger/conditions/dezog-cases.tsv

const REGISTERS = ['PC', 'SP', 'AF', 'BC', 'DE', 'HL', 'IX', 'IY', "AF'", "BC'", "DE'", "HL'", 'IR', 'IM',
    'F', 'A', 'C', 'B', 'E', 'D', 'L', 'H', 'IXL', 'IXH', 'IYL', 'IYH',
    "F'", "A'", "C'", "B'", "E'", "D'", "L'", "H'", 'R', 'I'];

// A small deterministic generator: the file must not change between runs
let seed = 0x5EED1234;
function rnd(n) {
    seed = (Math.imul(seed, 1103515245) + 12345) >>> 0;
    return ((seed >>> 8) % n);
}
function pick(list) { return list[rnd(list.length)]; }

// utility.ts parseValue (verbatim logic)
function parseValue(valueString) {
    const match = /^\s*((0x|\$)([0-9a-f]+)([^0-9a-f]*))?(([0-9a-f]+)h(.*))?(([01]+)b(.*))?(_([szhnpc]+)([^szhnpc])*)?((-?\d+)(\D*))?('([\S ]+)')?/i.exec(valueString);
    if (!match) return NaN;
    const ghex = match[3], ghex_empty = match[4], ghexh = match[6], ghexh_empty = match[7];
    const gbit = match[9], gbit_empty = match[10];
    let gflags = match[12]; const gflags_empty = match[13];
    const gdec = match[15], gdec_empty = match[16], gchar = match[18];
    if (ghex) { if (ghex_empty) return NaN; return parseInt(ghex, 16); }
    if (ghexh) { if (ghexh_empty) return NaN; return parseInt(ghexh, 16); }
    if (gdec) { if (gdec_empty) return NaN; return parseInt(gdec, 10); }
    if (gbit) { if (gbit_empty) return NaN; return parseInt(gbit, 2); }
    if (gflags) {
        if (gflags_empty) return NaN;
        gflags = gflags.toLowerCase();
        let flags = 0;
        if (gflags.includes('s')) flags |= 0x80;
        if (gflags.includes('z')) flags |= 0x40;
        if (gflags.includes('h')) flags |= 0x10;
        if (gflags.includes('p')) flags |= 0x04;
        if (gflags.includes('n')) flags |= 0x02;
        if (gflags.includes('c')) flags |= 0x01;
        return flags;
    }
    if (gchar) { if (gchar.length < 1) return NaN; return gchar.charCodeAt(0); }
    return NaN;
}

// labels.ts getNumberFromString64k without labels
function numberFromString64k(text) {
    if (text.startsWith('_')) return NaN;
    const result = parseValue(text);
    if (isNaN(result)) return result;
    return result & 0xFFFF;
}

function registerValue(name, r) {
    const n = name.toUpperCase();
    const pairs = {PC: r.pc, SP: r.sp, AF: r.af, BC: r.bc, DE: r.de, HL: r.hl, IX: r.ix, IY: r.iy,
        "AF'": r.af2, "BC'": r.bc2, "DE'": r.de2, "HL'": r.hl2, IR: (r.i << 8) | r.r, IM: r.im, R: r.r, I: r.i};
    if (n in pairs) return pairs[n];
    const halves = {A: ['af', 8], F: ['af', 0], B: ['bc', 8], C: ['bc', 0], D: ['de', 8], E: ['de', 0], H: ['hl', 8],
        L: ['hl', 0], IXH: ['ix', 8], IXL: ['ix', 0], IYH: ['iy', 8], IYL: ['iy', 0]};
    const shadow = n.endsWith("'");
    const base = shadow ? n.slice(0, -1) : n;
    if (base in halves && (!shadow || base.length === 1)) {
        const [pair, shift] = halves[base];
        return (r[shadow ? pair + '2' : pair] >> shift) & 0xFF;
    }
    return NaN;
}

// utility.ts replaceVarsWithValues + evalExpression
function dezogTruth(expr, r) {
    try {
        const replaced = expr.replace(/(0x[a-fA-F0-9]+\b|(?<![\w@])[@a-zA-Z_\.][a-zA-Z0-9_\.]*'?|[\$][0-9a-fA-F]+\b|[a-fA-F0-9]+h\b|[01]+b\b|\d+\b|'[\S ]+')/g, (match, p1) => {
            let res;
            if (REGISTERS.includes(p1.toUpperCase())) {
                const v = registerValue(p1, r);
                if (!isNaN(v)) res = v;
            }
            if (isNaN(res)) {
                let lbl = p1;
                if (lbl.startsWith('@')) lbl = lbl.substring(1);
                res = numberFromString64k(lbl);
                if (isNaN(res)) res = p1;
            }
            return res.toString();
        });
        let result = (0, eval)(replaced);
        if (typeof result == 'boolean') result = result ? 1 : 0;
        return result != 0 ? '1' : '0';
    } catch {
        return 'E';
    }
}

function number() {
    const v = pick([0, 1, 2, 3, 5, 7, 8, 15, 16, 18, 31, 32, 100, 127, 128, 255, 256, 0x1234, 0x4000, 0x7FFF, 0x8000,
        0xFFFF, 0x10000, 0x18000, 1 + rnd(0xFFFF)]);
    switch (rnd(9)) {
        case 0: return '0x' + v.toString(16);
        case 1: return '$' + v.toString(16).toUpperCase();
        case 2: return '0' + v.toString(16) + 'h';
        case 3: return v.toString(2) + 'b';
        case 4: return pick(['0X1F', '1F', 'FFh', '12H', "'A'", 'LBL', 'true', '_z', '07', '1e3']);  // the odd ones
        default: return String(v);
    }
}

function register() {
    const name = pick(REGISTERS);
    return rnd(4) === 0 ? name.toLowerCase() : name;
}

const BINARY = ['+', '-', '*', '%', '/', '<<', '>>', '>>>', '&', '|', '^', '&&', '||', '==', '!=', '===', '!==',
    '<', '<=', '>', '>=', '**'];
const COMPARE = ['==', '!=', '===', '!==', '<', '<=', '>', '>='];

function expression(depth) {
    if (depth <= 0 || rnd(3) === 0)
        return rnd(3) === 0 ? number() : register();
    switch (rnd(6)) {
        case 0: return pick(['!', '~', '-', '+']) + expression(depth - 1);
        case 1: return '(' + expression(depth - 1) + ')';
        default: return expression(depth - 1) + ' ' + pick(BINARY) + ' ' + expression(depth - 1);
    }
}

function condition() {
    switch (rnd(4)) {
        case 0: return expression(3);
        case 1: return expression(2) + ' ' + pick(['&&', '||']) + ' ' + register() + ' ' + pick(COMPARE) + ' ' + number();
        default: return expression(2) + ' ' + pick(COMPARE) + ' ' + expression(2);
    }
}

function registers() {
    const w = () => rnd(0x10000);
    const r = {pc: w(), sp: w(), af: w(), bc: w(), de: w(), hl: w(), ix: w(), iy: w(), af2: w(), bc2: w(), de2: w(),
        hl2: w(), i: rnd(256), r: rnd(256), im: rnd(3)};
    // small values often enough for the comparisons with small numbers to hold
    if (rnd(2) === 0) { r.af &= 0x0F0F; r.bc &= 0x0303; r.hl &= 0x00FF; }
    return r;
}

const out = ['# condition\tPC SP AF BC DE HL IX IY AF\' BC\' DE\' HL\' I R IM (hex)\tDeZog truth: 1 / 0 / E (error)'];
for (let i = 0; i < 3000; i++) {
    const c = condition();
    const r = registers();
    const regs = [r.pc, r.sp, r.af, r.bc, r.de, r.hl, r.ix, r.iy, r.af2, r.bc2, r.de2, r.hl2, r.i, r.r, r.im]
        .map(v => v.toString(16).toUpperCase()).join(' ');
    out.push(c + '\t' + regs + '\t' + dezogTruth(c, r));
}
process.stdout.write(out.join('\n') + '\n');
