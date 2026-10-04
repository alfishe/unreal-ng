/// @file ttddevicetable_test.cpp
/// @brief The engine's device table (Phase 2, Step 1): one check of the
/// device set, restore order as v1's unless a device says otherwise, a set
/// version that changes with the set.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "debugger/ttd/engine/ttddevicetable.h"

using namespace ttd;

namespace
{

TTDDeviceEntry Device(PeripheralId id, std::string instance, uint32_t size = 8,
                      std::vector<TTDDeviceKey> after = {})
{
    TTDDeviceEntry e;
    e.descriptor.legacyId = id;
    e.descriptor.type = static_cast<TTDDeviceType>(id);
    e.descriptor.instance = std::move(instance);
    e.descriptor.stateSize = size;
    e.descriptor.restoreAfter = std::move(after);
    return e;
}

std::vector<std::string> Order(const TTDDeviceTable& t)
{
    std::vector<std::string> names;
    for (uint32_t i : t.RestoreOrder())
        names.push_back(t.Entries()[i].descriptor.instance);
    return names;
}

}  // namespace

TEST(TTDDeviceTable_Test, OrderIsV1sAscendingIdsWithoutDependencies)
{
    TTDDeviceTable t;
    std::string err;
    ASSERT_TRUE(t.Build({Device(PeripheralId::Tape, "tape"), Device(PeripheralId::TurboSound, "turbosound"),
                         Device(PeripheralId::BetaDisk, "betadisk")},
                        err))
        << err;
    EXPECT_EQ(Order(t), (std::vector<std::string>{"turbosound", "betadisk", "tape"}));
}

TEST(TTDDeviceTable_Test, ADeviceRestoresAfterTheOnesItNames)
{
    // A device with a lower v1 id that must load after a higher one
    TTDDeviceTable t;
    std::string err;
    ASSERT_TRUE(t.Build({Device(PeripheralId::Tape, "tape", 8, {{TTDDeviceType::Covox, "covox"}}),
                         Device(PeripheralId::Covox, "covox"), Device(PeripheralId::BetaDisk, "betadisk")},
                        err))
        << err;
    EXPECT_EQ(Order(t), (std::vector<std::string>{"betadisk", "covox", "tape"}));
}

TEST(TTDDeviceTable_Test, InstancesOfOneKindAreDistinctDevices)
{
    TTDDeviceTable t;
    std::string err;
    TTDDeviceEntry a = Device(PeripheralId::SerialPort, "uart");
    TTDDeviceEntry b = Device(PeripheralId::ZiFiLine, "zifi.uart");
    b.descriptor.type = TTDDeviceType::SerialPort;
    ASSERT_TRUE(t.Build({b, a}, err)) << err;
    EXPECT_NE(t.Find({TTDDeviceType::SerialPort, "uart"}), nullptr);
    EXPECT_NE(t.Find({TTDDeviceType::SerialPort, "zifi.uart"}), nullptr);
    EXPECT_EQ(t.Find({TTDDeviceType::SerialPort, "atm2ioesp.uart"}), nullptr);
    EXPECT_EQ(Order(t), (std::vector<std::string>{"uart", "zifi.uart"})) << "ties follow the v1 ids (24, 39)";
}

TEST(TTDDeviceTable_Test, ABadSetIsRefusedNamingTheDevice)
{
    TTDDeviceTable t;
    std::string err;
    EXPECT_FALSE(t.Build({Device(PeripheralId::Tape, "tape"), Device(PeripheralId::Tape, "tape")}, err));
    EXPECT_NE(err.find("tape"), std::string::npos) << err;
    EXPECT_NE(err.find("twice"), std::string::npos) << err;

    EXPECT_FALSE(t.Build({Device(PeripheralId::Tape, "tape", 8, {{TTDDeviceType::Covox, "covox"}})}, err));
    EXPECT_NE(err.find("does not have"), std::string::npos) << err;

    EXPECT_FALSE(t.Build({Device(PeripheralId::Tape, "tape", 8, {{TTDDeviceType::Covox, "covox"}}),
                          Device(PeripheralId::Covox, "covox", 8, {{TTDDeviceType::Tape, "tape"}})},
                         err));
    EXPECT_NE(err.find("cycle"), std::string::npos) << err;

    EXPECT_FALSE(t.Build({Device(PeripheralId::Tape, "tape", 0)}, err));
    EXPECT_NE(err.find("no state"), std::string::npos) << err;

    EXPECT_TRUE(t.Entries().empty()) << "a refused set leaves the table as it was";
}

TEST(TTDDeviceTable_Test, SetVersionChangesOnlyWithTheSet)
{
    TTDDeviceTable t;
    std::string err;
    ASSERT_TRUE(t.Build({Device(PeripheralId::Tape, "tape")}, err));
    const uint32_t v = t.SetVersion();
    ASSERT_TRUE(t.Build({Device(PeripheralId::Tape, "tape")}, err));
    EXPECT_EQ(t.SetVersion(), v) << "the same set again";
    ASSERT_TRUE(t.Build({Device(PeripheralId::Tape, "tape"), Device(PeripheralId::Covox, "covox")}, err));
    EXPECT_NE(t.SetVersion(), v) << "a device added";
}
