#include <gtest/gtest.h>
#include "loaders/snapshot/uns/loader_uns.h"
#include "loaders/snapshot/uns/serializers/yamlsnapshotserializer.h"
#include <cstdio>
#include <fstream>

TEST(LoaderUNSTest, LoadMinimal)
{
    // Create a minimal YAML snapshot file
    const char* tmpfile = "loader_uns_test.yaml";
    std::ofstream out(tmpfile);
    out << "metadata:\n  manifest_version: '1.8'\n  emulator_id: 'UnrealSpeccy-NG'\n  emulator_version: '0.40.0'\n  host_platform: 'macOS'\n  emulated_platform: 'ZX Spectrum 128K'\n  save_time: '2024-06-21T12:00:00Z'\n  description: 'Test snapshot'\nmachine:\n  model: PENTAGON\n  ram_size_kb: 128\n";
    out.close();
    // LoaderUNS expects EmulatorContext*, but we use nullptr for this test
    LoaderUNS loader(nullptr, tmpfile);
    ASSERT_TRUE(loader.load());
    // Check that DTO is populated (LoaderUNS should expose the DTO or provide accessors)
    // For this test, we assume LoaderUNS has a getDTO() or similar (pseudo-code):
    // const SnapshotDTO& dto = loader.getDTO();
    // EXPECT_EQ(dto.metadata.manifest_version, "1.8");
    // Remove temp file
    std::remove(tmpfile);
    // Note: If LoaderUNS does not expose the DTO, this test should be extended when it does.
} 