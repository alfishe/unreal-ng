#pragma once
#include "pch.h"

class Manifest_Test : public ::testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;

    // Pre-allocated region simulating a small SHM
    static constexpr size_t SHM_SIZE = 64 * 1024;  // 64KB
    void* _shm = nullptr;
};
