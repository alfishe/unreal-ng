#pragma once

/// @file chdtesthelper.h
/// @brief Shared pieces of the CHD tests: the fixtures in testdata/media/chd/
/// (made by tools/chd/make-test-fixtures.py with chdman 0.289) and file helpers.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"

namespace chdtest
{
    constexpr uint32_t kHunk = 4096;
    constexpr uint32_t kHunks = 64;

    inline std::string Fixture(const std::string& name)
    {
        return TestPathHelper::GetTestDataPath("media/chd/" + name);
    }

    inline std::vector<uint8_t> ReadFile(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    inline void WriteFile(const std::string& path, const std::vector<uint8_t>& bytes)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    /// The source disk of every fixture
    inline std::vector<uint8_t> MixedImage()
    {
        return ReadFile(Fixture("mixed.img"));
    }

    /// The disk child-of-default.chd holds: mixed.img with 16 bytes of hunk 5 changed and hunk 30 zeroed
    inline std::vector<uint8_t> ChildImage()
    {
        std::vector<uint8_t> image = MixedImage();
        if (image.size() != kHunk * kHunks)
            return image;
        const char text[] = "CHILD CHANGED IT";
        std::copy(text, text + 16, image.begin() + 5 * kHunk);
        std::fill(image.begin() + 30 * kHunk, image.begin() + 31 * kHunk, 0);
        return image;
    }
}  // namespace chdtest
