// eve-emu - matrix commands (spec §7.5).
//
// The coprocessor keeps its current matrix as the bitmap transform itself (screen ->
// bitmap, 16.16), so CMD_SCALE(2, 2) halves A and E: "to zoom a bitmap 2X" [PG §5.49].
// Each command composes with the current matrix the way OpenGL does (current = current
// x new, for the object -> screen direction), which for the inverse kept here means
// new^-1 x current. Order, angle steps and rounding match the BT8XX reference (golden
// cases matrix-*, cmd-matrix).
#include "eve-copro.h"

namespace EveLib
{

namespace
{

constexpr uint32_t kFixedBits = 16;                    // 16.16
constexpr int64_t kFixedUnit = int64_t{1} << kFixedBits;
constexpr uint32_t kTransformShift = 8;                // 16.16 -> 8.8 / 15.8
constexpr int64_t kTransformHalf = int64_t{1} << (kTransformShift - 1);
constexpr uint32_t kMask17 = 0x1FFFF, kMask24 = 0xFFFFFF;
constexpr uint32_t kOpTransformA = 0x15;               // BITMAP_TRANSFORM_A ... F are 0x15 ... 0x1A
constexpr uint32_t kOpcodeShiftDl = 24;
constexpr uint32_t kTurn = 65536;                      // CMD_ROTATE angle unit: 1/65536 turn
constexpr double kPi = 3.14159265358979323846;
constexpr int kSineTerms = 12;

enum Coefficient { kA = 0, kB = 1, kC = 2, kD = 3, kE = 4, kF = 5 };

int32_t Saturate32(int64_t v)
{
    if (v > INT32_MAX)
        return INT32_MAX;
    if (v < INT32_MIN)
        return INT32_MIN;
    return static_cast<int32_t>(v);
}

// a x b in 16.16, rounded down (BT8XX, golden case matrix-combined).
int64_t FixedMul(int64_t a, int64_t b)
{
    const int64_t p = a * b;
    return p >> kFixedBits;
}

// a / b in 16.16, truncated toward zero.
int64_t FixedDiv(int64_t a, int64_t b)
{
    return b == 0 ? 0 : (a * kFixedUnit) / b;
}

// sin of an angle in [0, pi/2] by its Taylor series: only +, -, x, / of IEEE doubles, so
// the result is the same on every platform (no libm).
double Sine(double x)
{
    double term = x;
    double sum = x;
    const double x2 = x * x;
    for (int n = 1; n < kSineTerms; ++n)
    {
        term = -term * x2 / static_cast<double>((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

// sin and cos of a CMD_ROTATE angle, in 16.16 as the coprocessor ROM makes them (BT8XX,
// golden case matrix-rotate): the angle in steps of 1/1024 turn, each value
// 2 x trunc(32767 x sin), so 1.0 is 65534.
void SinCos(uint32_t angle, int64_t& sine, int64_t& cosine)
{
    constexpr uint32_t kAngleStepMask = ~0x3Fu;
    constexpr double kSineScale = 32767;
    constexpr int64_t kSineFactor = 2;
    const uint32_t quarter = kTurn / 4;
    const uint32_t a = (angle & kAngleStepMask) % kTurn;
    const uint32_t q = a / quarter;
    const double r = static_cast<double>(a % quarter) * (2 * kPi) / kTurn;
    const double s = Sine(r);
    const double c = Sine(kPi / 2 - r);
    double sv = 0, cv = 0;
    switch (q)
    {
    case 0: sv = s; cv = c; break;
    case 1: sv = c; cv = -s; break;
    case 2: sv = -s; cv = -c; break;
    default: sv = -c; cv = s; break;
    }
    sine = kSineFactor * static_cast<int64_t>(sv * kSineScale);
    cosine = kSineFactor * static_cast<int64_t>(cv * kSineScale);
}

// m = n x m for the 2x3 affine matrices (n given as its six coefficients).
void PreMultiply(int32_t* m, int64_t na, int64_t nb, int64_t nc, int64_t nd, int64_t ne, int64_t nf)
{
    const int64_t a = m[kA], b = m[kB], c = m[kC], d = m[kD], e = m[kE], f = m[kF];
    m[kA] = Saturate32(FixedMul(na, a) + FixedMul(nb, d));
    m[kB] = Saturate32(FixedMul(na, b) + FixedMul(nb, e));
    m[kC] = Saturate32(FixedMul(na, c) + FixedMul(nb, f) + nc);
    m[kD] = Saturate32(FixedMul(nd, a) + FixedMul(ne, d));
    m[kE] = Saturate32(FixedMul(nd, b) + FixedMul(ne, e));
    m[kF] = Saturate32(FixedMul(nd, c) + FixedMul(ne, f) + nf);
}

uint32_t TransformWord(uint32_t index, int32_t value16)
{
    const int64_t v = kMatrixRoundToNearest ? (static_cast<int64_t>(value16) + kTransformHalf) >> kTransformShift
                                            : static_cast<int64_t>(value16) >> kTransformShift;
    const uint32_t mask = (index == kC || index == kF) ? kMask24 : kMask17;
    return ((kOpTransformA + index) << kOpcodeShiftDl) | (static_cast<uint32_t>(v) & mask);
}

} // namespace

bool MatrixBegin(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    int32_t* m = c.matrix;
    switch (c.command)
    {
    case kCmdLoadidentity:
        for (uint32_t i = 0; i < kMatrixSize; ++i)
            m[i] = 0;
        m[kA] = kFixedOne;
        m[kE] = kFixedOne;
        return true;
    case kCmdTranslate:
    {
        // Inverse of a translation by (tx, ty).
        const int64_t tx = static_cast<int32_t>(c.params[0]);
        const int64_t ty = static_cast<int32_t>(c.params[1]);
        PreMultiply(m, kFixedUnit, 0, -tx, 0, kFixedUnit, -ty);
        return true;
    }
    case kCmdScale:
    {
        const int64_t sx = static_cast<int32_t>(c.params[0]);
        const int64_t sy = static_cast<int32_t>(c.params[1]);
        PreMultiply(m, FixedDiv(kFixedUnit, sx), 0, 0, 0, FixedDiv(kFixedUnit, sy), 0);
        return true;
    }
    case kCmdRotate:
    {
        // Clockwise on the screen (y down); its inverse turns back.
        int64_t s = 0, k = 0;
        SinCos(c.params[0], s, k);
        PreMultiply(m, k, s, 0, -s, k, 0);
        return true;
    }
    case kCmdSetmatrix:
        // F first, A last (golden case cmd-matrix).
        for (uint32_t i = kMatrixSize; i-- > 0;)
            if (!QueueDlWord(chip, TransformWord(i, m[i])))
                return false;
        return false; // emitted as its cost elapses
    case kCmdGetmatrix:
        for (uint32_t i = 0; i < kMatrixSize; ++i)
            WriteRingResult(chip, i, static_cast<uint32_t>(m[i]));
        return true;
    default:
        return true;
    }
}

} // namespace EveLib
