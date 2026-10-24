/// @file devicedatabase_test.cpp
/// @brief The device database's user layer (device-database.md §4): devices.user.json over the compiled-in
/// families, field by field for a known family (a user photo keeps the match and the controls), a new family with its
/// match, the entries edited as text, and the host verbs database / database_save / database_delete over a user
/// directory.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "_helpers/testpathhelper.h"
#include "devicedatabase.h"
#include "hostinputcontrol.h"
#include "profiles/json.h"

using namespace controllers;

namespace
{
/// The user layer is process-wide: each test leaves it empty
class DeviceDatabase_Test : public ::testing::Test
{
protected:
    void TearDown() override
    {
        std::string error;
        DeviceDatabase::SetUser("", error);
    }
};

/// An owner with only a user directory (the database verbs need no profiles)
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
        reapplied++;
    }
    std::string UserControllersDir() const override
    {
        return _dir;
    }
    int reapplied = 0;

private:
    std::string _dir;
    ProfileStore _store;
    ResolvedProfiles _resolved;
};
}  // namespace

TEST_F(DeviceDatabase_Test, UserLayer_FieldByFieldOverAKnownFamily)
{
    DeviceDatabase db = DeviceDatabase::Builtin();
    std::string error;
    ASSERT_TRUE(db.Merge(R"({"schema": 1, "families": [{"id": "xbox-elite-2", "images": "my-elite", "name": "Left"}]})",
                         error))
        << error;
    const DeviceMatch elite = db.Lookup({"bt", 0x045E, 0x0B22, 0x0517, "Xbox Wireless Controller", "iohid"});
    EXPECT_EQ(elite.family, "xbox-elite-2") << "the built-in match stays";
    EXPECT_EQ(elite.images, "my-elite");
    EXPECT_EQ(elite.name, "Left");
    EXPECT_EQ(elite.layout, "xbox");
    EXPECT_FALSE(elite.controls.empty()) << "the built-in controls stay";
    EXPECT_FALSE(db.Merge(R"({"schema": 1, "families": [{"id": "brand-new", "name": "No match"}]})", error))
        << "a new family needs its match list";
    EXPECT_NE(error.find("match"), std::string::npos) << error;
}

TEST_F(DeviceDatabase_Test, UserLayer_ActiveSwapsAndHoldersKeepTheirSnapshot)
{
    const std::shared_ptr<const DeviceDatabase> before = DeviceDatabase::Active();
    std::string error;
    ASSERT_TRUE(DeviceDatabase::SetUser(
        R"({"schema": 1, "families": [{"id": "my-stick", "name": "Arcade box", "match": [{"vid": "1234", "pid": ["5678"]}]}]})",
        error))
        << error;
    EXPECT_EQ(DeviceDatabase::Active()->Lookup({"usb", 0x1234, 0x5678, -1, "x", ""}).name, "Arcade box");
    EXPECT_EQ(before->Lookup({"usb", 0x1234, 0x5678, -1, "x", ""}).step, "unknown") << "the old snapshot is unchanged";
    EXPECT_EQ(DeviceDatabase::UserFamilies(DeviceDatabase::UserText()), std::vector<std::string>{"my-stick"});
    EXPECT_FALSE(DeviceDatabase::SetUser("{not json", error));
    EXPECT_EQ(DeviceDatabase::Active()->Lookup({"usb", 0x1234, 0x5678, -1, "x", ""}).name, "Arcade box")
        << "a refused text keeps the layer in force";
    ASSERT_TRUE(DeviceDatabase::SetUser("", error));
    EXPECT_EQ(DeviceDatabase::Active()->Lookup({"usb", 0x1234, 0x5678, -1, "x", ""}).step, "unknown");
}

TEST_F(DeviceDatabase_Test, UserFile_PutReplacesByIdAndRemove)
{
    std::string text;
    std::string error;
    JsonValue entry;
    ASSERT_TRUE(ParseJson(R"({"id": "dualsense", "images": "my-dualsense"})", entry, error));
    ASSERT_TRUE(DeviceDatabase::PutUserFamily("", entry, text, error)) << error;
    ASSERT_TRUE(ParseJson(R"({"id": "dualsense", "images": "other"})", entry, error));
    ASSERT_TRUE(DeviceDatabase::PutUserFamily(text, entry, text, error)) << error;
    EXPECT_EQ(DeviceDatabase::UserFamilies(text), std::vector<std::string>{"dualsense"}) << "replaced, not added";
    EXPECT_NE(text.find("other"), std::string::npos);
    ASSERT_TRUE(ParseJson(R"({"name": "no id"})", entry, error));
    EXPECT_FALSE(DeviceDatabase::PutUserFamily(text, entry, text, error));
    EXPECT_TRUE(DeviceDatabase::RemoveUserFamily(text, "dualsense", text, error)) << error;
    EXPECT_TRUE(DeviceDatabase::UserFamilies(text).empty());
    EXPECT_FALSE(DeviceDatabase::RemoveUserFamily(text, "dualsense", text, error));
}

TEST_F(DeviceDatabase_Test, HostVerbs_LookupSaveDelete)
{
    namespace fs = std::filesystem;
    const fs::path root = TestPathHelper::GetUniqueTestScratchPath("device-database");
    DirOwner owner(root.string());
    HostInputControl control(owner);

    HostReply reply = control.Execute({"database", {{"vidpid", "045e:0b22"}, {"bus", "bt"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.Find("match")->Find("family")->AsString(), "xbox-elite-2");
    EXPECT_EQ(reply.body.Find("match")->Find("step")->AsString(), "exact");
    EXPECT_FALSE(reply.body.Find("match")->Find("user")->AsBool());
    EXPECT_EQ(control.Execute({"database", {{"vidpid", "zz"}}}).error, "bad-request");
    EXPECT_EQ(control.Execute({"database", {{"family", "no-such"}}}).HttpStatus(), 404);

    reply = control.Execute({"database_save", {{"text", R"({"id": "xbox-elite-2", "images": "user-elite"})"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(owner.reapplied, 1);
    EXPECT_TRUE(fs::is_regular_file(root / "devices.user.json"));
    reply = control.Execute({"database", {{"family", "xbox-elite-2"}}});
    EXPECT_EQ(reply.body.Find("match")->Find("images")->AsString(), "user-elite");
    EXPECT_TRUE(reply.body.Find("match")->Find("user")->AsBool());
    EXPECT_EQ(reply.body.Find("user")->Items().size(), 1u);

    EXPECT_EQ(control.Execute({"database_save", {{"text", R"({"id": "new-one"})"}}}).error, "bad-request")
        << "a new family without a match is refused, the file unchanged";
    reply = control.Execute({"database_delete", {{"id", "xbox-elite-2"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(DeviceDatabase::Active()->ForFamily("xbox-elite-2").images, "generic-xbox-like");
    EXPECT_EQ(control.Execute({"database_delete", {{"id", "xbox-elite-2"}}}).HttpStatus(), 404);
    EXPECT_EQ(control.Execute({"database", {{"color", "x"}}}).error, "bad-request") << "an unknown option";
}
