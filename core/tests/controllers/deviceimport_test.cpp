/// @file deviceimport_test.cpp
/// @brief Other projects' mappings as user device entries (device-database.md §3, FR-7): the database_import verb
/// (the parsers: devicemapping_test.cpp) merges its family over a user photo of the same device and saves it.

#include <gtest/gtest.h>

#include <filesystem>

#include "_helpers/testpathhelper.h"
#include "devicedatabase.h"
#include "hid/hidpad.h"
#include "hostinputcontrol.h"

using namespace controllers;

namespace
{
class DirOwner : public IHostInputOwner
{
public:
    explicit DirOwner(std::string dir) : _dir(std::move(dir))
    {
    }
    ProfileStore& Profiles() override
    {
        return _store;
    }
    ProfileContext Context() const override
    {
        return {};
    }
    const ResolvedProfiles& Resolved() const override
    {
        return _resolved;
    }
    std::string UserProfileDir() const override
    {
        return _dir + "/profiles";
    }
    std::string ManualProfile() const override
    {
        return {};
    }
    void SetManualProfile(const std::string&) override
    {
    }
    void Reapply() override
    {
    }
    std::string UserControllersDir() const override
    {
        return _dir;
    }

private:
    std::string _dir;
    ProfileStore _store;
    ResolvedProfiles _resolved;
};

class DeviceImport_Test : public ::testing::Test
{
protected:
    void TearDown() override
    {
        std::string error;
        DeviceDatabase::SetUser("", error);
    }
};

// 8BitDo SN30 Pro over USB as SDL writes it: bus 0003, vendor 2dc8, product 6101
const char* kSn30 =
    "03000000c82d00000161000000010000,8BitDo SN30 Pro,a:b1,b:b0,back:b10,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,"
    "dpup:h0.1,leftshoulder:b6,leftstick:b13,lefttrigger:b8,leftx:a0,lefty:a1,rightshoulder:b7,rightstick:b14,"
    "righttrigger:b9,rightx:a2,righty:a3,start:b11,x:b4,y:b3,misc2:b15,platform:Mac OS X,";
}  // namespace

TEST_F(DeviceImport_Test, ImportVerb_SavesMergedOverAUserPhotoAndTheLookupUsesIt)
{
    namespace fs = std::filesystem;
    const fs::path root = TestPathHelper::GetUniqueTestScratchPath("device-import");
    // The path is per process, so --gtest_repeat meets the previous pass's files: start and end clean
    std::error_code removeError;
    fs::remove_all(root, removeError);
    struct Cleanup
    {
        fs::path dir;
        ~Cleanup()
        {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    } cleanup{root};
    DirOwner owner(root.string());
    HostInputControl control(owner);

    HostReply reply = control.Execute({"database_import", {{"text", kSn30}, {"save", "false"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_FALSE(fs::exists(root / "devices.user.json")) << "save=false only answers the entry";
    EXPECT_EQ(reply.body.Find("entry")->Find("id")->AsString(), "user-2dc86101");

    // The hotspot editor saved a photo for the pad first: the import keeps its image set
    ASSERT_TRUE(control
                    .Execute({"database_save",
                              {{"text", R"({"id": "user-2dc86101", "name": "My SN30", "images": "user-2dc86101",
                                 "match": [{"vid": "2dc8", "pid": ["6101"]}]})"}}})
                    .Ok());
    reply = control.Execute({"database_import", {{"text", kSn30}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_NE(reply.message.find("saved as user-2dc86101"), std::string::npos) << reply.message;
    EXPECT_TRUE(fs::exists(root / "devices.user.json"));
    DeviceIdentity identity;
    identity.bus = "usb";
    identity.vendor = 0x2dc8;
    identity.product = 0x6101;
    const DeviceMatch match = DeviceDatabase::Active()->Lookup(identity);
    EXPECT_EQ(match.family, "user-2dc86101");
    EXPECT_EQ(match.images, "user-2dc86101") << "the photo stays";
    EXPECT_EQ(match.name, "8BitDo SN30 Pro") << "the import names what it maps";
    ASSERT_EQ(match.map.count("south"), 1u);
    EXPECT_EQ(match.map.at("south"), "button.1") << "a:b1";
    EXPECT_EQ(control.Execute({"database_import", {{"text", "nonsense"}}}).error, "bad-request");
}
