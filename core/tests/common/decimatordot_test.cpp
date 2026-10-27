#include "pch.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "common/sound/filters/decimatordot.h"
#include "common/sound/filters/filter_decimator.h"

/// DecimatorDot kernels and FilterDecimator::getOutputs: every path returns the bits of the scalar reference order
/// (docs/inprogress/2026-10-03-zx-multisound/ym-decimator-prototype.md)
namespace
{
bool SameBits(double a, double b)
{
    uint64_t x = 0;
    uint64_t y = 0;
    std::memcpy(&x, &a, 8);
    std::memcpy(&y, &b, 8);
    return x == y;
}

template <size_t K>
void CheckKernel(std::mt19937_64& rng, size_t length)
{
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<double> data[K];
    const double* x[K];
    for (size_t k = 0; k < K; k++)
    {
        data[k].resize(length);
        for (double& v : data[k])
            v = dist(rng);
        x[k] = data[k].data();
    }
    std::vector<double> a(length);
    std::vector<double> b(length);
    for (size_t i = 0; i < length; i++)
    {
        a[i] = dist(rng);
        b[i] = dist(rng);
    }
    double sa[K], sb[K], va[K], vb[K];
    DecimatorDot::Scalar<K>(x, a.data(), b.data(), length, sa, sb);
    DecimatorDot::Simd<K>(x, a.data(), b.data(), length, va, vb);
    for (size_t k = 0; k < K; k++)
    {
        EXPECT_TRUE(SameBits(sa[k], va[k])) << "K " << K << " length " << length << " stream " << k;
        EXPECT_TRUE(SameBits(sb[k], vb[k])) << "K " << K << " length " << length << " stream " << k;
    }
}
}  // namespace

class DecimatorDot_Test : public ::testing::Test
{
};

TEST_F(DecimatorDot_Test, SimdKernelReturnsTheScalarBits)
{
    std::mt19937_64 rng(7);
    for (size_t length : {size_t{1}, size_t{3}, size_t{4}, size_t{5}, size_t{8}, size_t{97}, size_t{193}, size_t{385}})
    {
        for (int round = 0; round < 20; round++)
        {
            CheckKernel<1>(rng, length);
            CheckKernel<2>(rng, length);
            CheckKernel<3>(rng, length);
            CheckKernel<4>(rng, length);
        }
    }
}

TEST_F(DecimatorDot_Test, GetOutputsReturnsTheBitsOfOneGetOutputEach)
{
    // The ZX-MultiSound layout: six SSG decimators at 218.75 kHz (a master and five slaves), two FM slaves at
    // 437.5 kHz, plus a standalone pair fed in lockstep (the TSFM / TurboSound layout). Two identical sets: one
    // evaluated by getOutput() each, the other by one getOutputs() call
    struct Set
    {
        FilterDecimator ssg[6];
        FilterDecimator fm[2];
        FilterDecimator standalone[2];
        Set()
        {
            for (int i = 0; i < 6; i++)
            {
                ssg[i].configure(44100.0, FilterDecimator::Quality::Reference, false, 218750.0);
                if (i > 0)
                    ssg[i].attachMaster(&ssg[0]);
            }
            for (auto& d : fm)
            {
                d.configure(44100.0, FilterDecimator::Quality::Reference, false, 437500.0);
                d.attachMaster(&ssg[0]);
            }
            for (auto& d : standalone)
                d.configure(44100.0, FilterDecimator::Quality::Reference, false, 218750.0);
        }
    };
    Set one;
    Set many;
    std::mt19937_64 rng(11);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    size_t compared = 0;
    for (int n = 0; n < 3000; n++)
    {
        while (!one.ssg[0].hasOutput())
        {
            for (int half = 0; half < 2; half++)
            {
                for (int i = 0; i < 2; i++)
                {
                    const double v = dist(rng);
                    one.fm[i].feedSample(v);
                    many.fm[i].feedSample(v);
                }
            }
            for (int i = 0; i < 6; i++)
            {
                const double v = (n % 7 == 0) ? 0.25 : dist(rng);   // constant stretches too
                one.ssg[i].feedSample(v);
                many.ssg[i].feedSample(v);
            }
            for (int i = 0; i < 2; i++)
            {
                const double v = dist(rng);
                one.standalone[i].feedSample(v);
                many.standalone[i].feedSample(v);
            }
        }
        ASSERT_TRUE(many.ssg[0].hasOutput());

        // Slaves first, the master last, as the pair renders
        double expected[10];
        size_t k = 0;
        for (int i = 1; i < 6; i++)
            expected[k++] = one.ssg[i].getOutput();
        expected[k++] = one.ssg[0].getOutput();
        expected[k++] = one.fm[0].getOutput();
        expected[k++] = one.fm[1].getOutput();
        expected[k++] = one.standalone[0].getOutput();
        expected[k++] = one.standalone[1].getOutput();

        FilterDecimator* list[10] = {&many.ssg[1], &many.ssg[2],       &many.ssg[3],       &many.ssg[4],
                                     &many.ssg[5], &many.ssg[0],       &many.fm[0],        &many.fm[1],
                                     &many.standalone[0], &many.standalone[1]};
        double got[10];
        FilterDecimator::getOutputs(list, 10, got);
        for (size_t i = 0; i < 10; i++)
        {
            ASSERT_TRUE(SameBits(expected[i], got[i])) << "output " << n << " stream " << i;
            compared++;
        }
        ASSERT_EQ(one.ssg[0].phase(), many.ssg[0].phase());
        ASSERT_EQ(one.standalone[1].phase(), many.standalone[1].phase());
    }
    EXPECT_EQ(compared, 30000u);
}
