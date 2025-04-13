#pragma once
#include "pch.h"

class AudioHelper_Test : public ::testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;
};

// Compare floating number arrays with precision
#define ASSERT_FLOAT_ARRAY_NEAR(expected, actual, count, max_abs_error) \
    do { \
        for (size_t i = 0; i < (count); ++i) { \
            ASSERT_NEAR((expected)[i], (actual)[i], (max_abs_error)); \
        } \
    } while (0)
