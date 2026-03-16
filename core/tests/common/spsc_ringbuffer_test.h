#pragma once
#include "pch.h"

class SPSCRingBuffer_Test : public ::testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;

    // Pre-allocated memory for ring buffer tests
    static constexpr size_t TEST_CAPACITY = 4096;  // Power of 2
    void* _memory = nullptr;
};
