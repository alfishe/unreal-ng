// unreal-asm-tests entry point (own main: links only gtest, which every build of it provides, MinGW included)

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
