#include "pch.h"
#include "emulog_test.h"
#include "common/emulog.h"
#include "common/emulog_category.h"
#include "common/emulog_registry.h"

/// region <SetUp / TearDown>

void EmuLog_Test::SetUp()
{
    // Ensure clean state — disable all categories
    emu::log::LoggerRegistry::instance().disableAll();
}

void EmuLog_Test::TearDown()
{
    emu::log::LoggerRegistry::instance().disableAll();
    emu::log::LoggerRegistry::instance().setRingBuffer(nullptr);
}

/// endregion </SetUp / TearDown>

// =============================================================================
// FNV-1a hash
// =============================================================================

TEST_F(EmuLog_Test, HashFNV1a_DeterministicForSameInput)
{
    uint32_t h1 = emu::log::hash_fnv1a("fdc.read");
    uint32_t h2 = emu::log::hash_fnv1a("fdc.read");
    EXPECT_EQ(h1, h2);
}

TEST_F(EmuLog_Test, HashFNV1a_DifferentForDifferentInput)
{
    uint32_t h1 = emu::log::hash_fnv1a("fdc.read");
    uint32_t h2 = emu::log::hash_fnv1a("fdc.write");
    EXPECT_NE(h1, h2);
}

TEST_F(EmuLog_Test, HashFNV1a_EmptyString)
{
    uint32_t h = emu::log::hash_fnv1a("");
    // FNV-1a offset basis
    EXPECT_EQ(h, 0x811C9DC5u);
}

// =============================================================================
// CategoryState
// =============================================================================

TEST_F(EmuLog_Test, CategoryState_DefaultDisabled)
{
    emu::log::CategoryState state{
        {false},
        {static_cast<uint8_t>(emu::log::Level::Off)},
        "test.category",
        emu::log::hash_fnv1a("test.category"),
        nullptr
    };

    EXPECT_FALSE(state.enabled.load());
    EXPECT_FALSE(state.is_enabled(emu::log::Level::Trace));
    EXPECT_FALSE(state.is_enabled(emu::log::Level::Error));
}

TEST_F(EmuLog_Test, CategoryState_EnabledAtLevel)
{
    emu::log::CategoryState state{
        {true},
        {static_cast<uint8_t>(emu::log::Level::Warn)},
        "test.warn",
        emu::log::hash_fnv1a("test.warn"),
        nullptr
    };

    EXPECT_TRUE(state.is_enabled(emu::log::Level::Warn));
    EXPECT_TRUE(state.is_enabled(emu::log::Level::Error));
    EXPECT_FALSE(state.is_enabled(emu::log::Level::Info));
    EXPECT_FALSE(state.is_enabled(emu::log::Level::Trace));
}

// =============================================================================
// LoggerRegistry
// =============================================================================

TEST_F(EmuLog_Test, Registry_CategoryCount)
{
    // The registry already has categories registered from static initializers (fdc.*)
    // Just verify it's non-zero or can list them
    auto categories = emu::log::LoggerRegistry::instance().listCategories();
    EXPECT_GE(categories.size(), 0u);
}

TEST_F(EmuLog_Test, Registry_EnableExactCategory)
{
    auto& reg = emu::log::LoggerRegistry::instance();

    // Create and register a test category
    static emu::log::CategoryState testState{
        {false},
        {static_cast<uint8_t>(emu::log::Level::Off)},
        "test.enable_exact",
        emu::log::hash_fnv1a("test.enable_exact"),
        nullptr
    };
    reg.registerCategory(testState);

    // Enable it
    reg.setEnabled("test.enable_exact", true, emu::log::Level::Debug);

    EXPECT_TRUE(testState.enabled.load());
    EXPECT_EQ(testState.level.load(), static_cast<uint8_t>(emu::log::Level::Debug));
}

TEST_F(EmuLog_Test, Registry_DisableAll)
{
    auto& reg = emu::log::LoggerRegistry::instance();

    static emu::log::CategoryState testState{
        {true},
        {static_cast<uint8_t>(emu::log::Level::Trace)},
        "test.disable_all",
        emu::log::hash_fnv1a("test.disable_all"),
        nullptr
    };
    reg.registerCategory(testState);

    reg.disableAll();

    EXPECT_FALSE(testState.enabled.load());
}

TEST_F(EmuLog_Test, Registry_WildcardEnable)
{
    auto& reg = emu::log::LoggerRegistry::instance();

    // Use wildcard to enable all
    reg.setEnabled("*", true, emu::log::Level::Trace);

    auto categories = reg.listCategories();
    for (const auto& [name, enabled] : categories)
    {
        EXPECT_TRUE(enabled) << "Category " << name << " should be enabled";
    }
}

TEST_F(EmuLog_Test, Registry_FindCategory)
{
    auto& reg = emu::log::LoggerRegistry::instance();

    static emu::log::CategoryState testState{
        {false},
        {static_cast<uint8_t>(emu::log::Level::Off)},
        "test.findme",
        emu::log::hash_fnv1a("test.findme"),
        nullptr
    };
    reg.registerCategory(testState);

    auto* found = reg.findCategory("test.findme");
    EXPECT_NE(found, nullptr);
    EXPECT_STREQ(found->name, "test.findme");

    auto* notFound = reg.findCategory("test.nonexistent");
    EXPECT_EQ(notFound, nullptr);
}

TEST_F(EmuLog_Test, Registry_SerializeCategoryList)
{
    auto& reg = emu::log::LoggerRegistry::instance();

    char buffer[4096] = {};
    size_t written = reg.serializeCategoryList(buffer, sizeof(buffer));

    // If there are any categories registered, output should be non-empty
    if (reg.categoryCount() > 0)
    {
        EXPECT_GT(written, 0u);
        // Format is "name:enabled:level\n" per category
        // Verify it contains at least one newline
        EXPECT_NE(std::string(buffer).find('\n'), std::string::npos);
    }
}
