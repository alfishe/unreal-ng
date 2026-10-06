#pragma once

// Reading the library's own test data (testdata/, decision D-14)

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace unrealasm::testing
{
inline std::string TestDataPath(const std::string& relative)
{
    return std::string(UNREAL_ASM_TESTDATA_DIR) + "/" + relative;
}

inline std::vector<uint8_t> ReadTestData(const std::string& relative)
{
    std::ifstream in(TestDataPath(relative), std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

inline std::string ReadTestText(const std::string& relative)
{
    const std::vector<uint8_t> bytes = ReadTestData(relative);
    return std::string(bytes.begin(), bytes.end());
}
}  // namespace unrealasm::testing
