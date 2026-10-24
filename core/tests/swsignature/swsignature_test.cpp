/// @file swsignature_test.cpp
/// @brief Media signatures (input-and-profiles.md §4): the exact hash, the tape content over TAP ↔ TZX, the TR-DOS file
/// set over SCL ↔ TRD ↔ FDI ↔ UDI and renames, the fuzzy rule after saves, the memory signature of two snapshots of one
/// game, the match rules of match.h and the surfaces' SignatureControl.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>

#include "_helpers/testpathhelper.h"
#include "loaders/tape/loader_tape.h"
#include "loaders/tape/writer_tzx.h"
#include "match.h"
#include "signaturecontrol.h"
#include "swsignature.h"
#include "unrealdisk/diskimage.h"
#include "unrealdisk/floppyformats.h"
#include "unrealdisk/trdosfs.h"

using namespace unrealng::swsignature;

namespace
{
std::vector<uint8_t> ReadAll(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

Signatures Of(const std::vector<uint8_t>& bytes, const std::string& name)
{
    Signatures out;
    std::string error;
    EXPECT_TRUE(Compute(bytes.data(), bytes.size(), name, out, error)) << error;
    return out;
}

/// The SCL fixture onto a disk image
std::unique_ptr<unrealdisk::DiskImage> LoadDisk(const std::vector<uint8_t>& bytes, const std::string& name)
{
    std::unique_ptr<unrealdisk::DiskImage> disk;
    std::string format;
    EXPECT_TRUE(unrealdisk::FloppyFormats::LoadData({}, bytes.data(), bytes.size(), name, disk, format).Ok());
    return disk;
}

/// @p disk written as @p name (its extension picks the format) and read back
std::vector<uint8_t> SaveAs(unrealdisk::DiskImage& disk, const std::string& name)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath(name);
    const unrealdisk::FloppySaveResult saved = unrealdisk::FloppyFormats::Save({}, disk, path, false);
    EXPECT_TRUE(saved.saved) << saved.reason;
    std::vector<uint8_t> bytes = ReadAll(path);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return bytes;
}

const std::string kScl = "loaders/scl/eyeache2.scl";
const std::string kTap = "loaders/tap/aydetect.tap";
}  // namespace

TEST(Signature_Test, ExactHashAndUnknownFormat)
{
    const std::vector<uint8_t> bytes = {'h', 'e', 'l', 'l', 'o'};
    const Signatures sig = Of(bytes, "readme.txt");
    EXPECT_EQ(sig.sha256, "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
    EXPECT_EQ(sig.kind, MediaKind::Unknown);
    EXPECT_TRUE(sig.content.empty());
    ASSERT_EQ(sig.Keys().size(), 1u);
    EXPECT_EQ(sig.Keys()[0], std::string(kFilePrefix) + sig.sha256);

    Signatures empty;
    std::string error;
    EXPECT_FALSE(Compute(nullptr, 0, "x.tap", empty, error));
    EXPECT_FALSE(error.empty());
}

TEST(Signature_Test, ExternalHashesOnRequest)
{
    const std::vector<uint8_t> bytes = {'a', 'b', 'c'};
    Signatures sig;
    std::string error;
    Options options;
    options.externalHashes = true;
    ASSERT_TRUE(Compute(bytes.data(), bytes.size(), "abc.bin", sig, error, options));
    EXPECT_EQ(sig.md5, "900150983cd24fb0d6963f7d28e17f72");
    EXPECT_EQ(sig.sha1, "a9993e364706816aba3e25717850c26c9cd0d89d");
}

TEST(Signature_Test, TapeContentSurvivesTapToTzx)
{
    const std::vector<uint8_t> tap = ReadAll(TestPathHelper::GetTestDataPath(kTap));
    ASSERT_FALSE(tap.empty());
    const Signatures fromTap = Of(tap, "aydetect.tap");
    ASSERT_EQ(fromTap.kind, MediaKind::Tape);
    ASSERT_EQ(fromTap.content.rfind(kTapePrefix, 0), 0u) << fromTap.content;
    ASSERT_FALSE(fromTap.blocks.empty());

    // The same blocks as a TZX, renamed: the file differs, the content does not
    LoaderTapeBase* loader = TapeLoaderRegistry::Instance().Select(std::span<const uint8_t>(tap), "aydetect.tap");
    ASSERT_NE(loader, nullptr);
    const TapeImage image = loader->Load(std::span<const uint8_t>(tap), "aydetect.tap");
    const std::vector<uint8_t> tzx = TzxArchiveWriter::Write(image);
    const Signatures fromTzx = Of(tzx, "AY Detect (1990).tzx");
    EXPECT_EQ(fromTzx.format, "tzx");
    EXPECT_NE(fromTzx.sha256, fromTap.sha256);
    EXPECT_EQ(fromTzx.content, fromTap.content);
    ASSERT_EQ(fromTzx.blocks.size(), fromTap.blocks.size());
    for (size_t i = 0; i < fromTap.blocks.size(); i++)
        EXPECT_EQ(fromTzx.blocks[i].sha256, fromTap.blocks[i].sha256);
    EXPECT_EQ(Compare(fromTap, fromTzx).level, MatchLevel::Content);

    // One payload byte changed: another content
    std::vector<uint8_t> changed = tap;
    changed[tap.size() - 2] ^= 0x01;
    EXPECT_NE(Of(changed, "aydetect.tap").content, fromTap.content);
}

TEST(Signature_Test, TrdosFileSetSurvivesRepackAndRename)
{
    const std::vector<uint8_t> scl = ReadAll(TestPathHelper::GetTestDataPath(kScl));
    const Signatures fromScl = Of(scl, "eyeache2.scl");
    ASSERT_EQ(fromScl.kind, MediaKind::Disk);
    ASSERT_EQ(fromScl.content.rfind(kTrdosPrefix, 0), 0u) << fromScl.content;
    ASSERT_FALSE(fromScl.files.empty());

    std::unique_ptr<unrealdisk::DiskImage> disk = LoadDisk(scl, "eyeache2.scl");
    ASSERT_TRUE(disk);
    for (const char* name : {"EYEACHE.TRD", "eye ache 2 (renamed).fdi", "copy.udi"})
    {
        const Signatures other = Of(SaveAs(*disk, name), name);
        EXPECT_NE(other.sha256, fromScl.sha256) << name;
        EXPECT_EQ(other.content, fromScl.content) << name;
        EXPECT_EQ(other.FileSet(), fromScl.FileSet()) << name;
        EXPECT_EQ(Compare(fromScl, other).level, MatchLevel::Content) << name;
    }
}

/// The sector-image path (TRD, SCL from their bytes) gives what the core's DiskImage loaders give
void ExpectSameAsDiskImage(const std::vector<uint8_t>& bytes, const std::string& name)
{
    Signatures fast;
    Signatures slow;
    Options viaDiskImage;
    viaDiskImage.sectorImages = false;
    std::string error;
    ASSERT_TRUE(Compute(bytes.data(), bytes.size(), name, fast, error)) << error;
    ASSERT_TRUE(Compute(bytes.data(), bytes.size(), name, slow, error, viaDiskImage)) << error;
    EXPECT_EQ(fast.format, slow.format) << name;
    EXPECT_EQ(fast.content, slow.content) << name;
    ASSERT_EQ(fast.files.size(), slow.files.size()) << name;
    for (size_t i = 0; i < fast.files.size(); i++)
    {
        EXPECT_EQ(fast.files[i].name, slow.files[i].name) << name;
        EXPECT_EQ(fast.files[i].dataLength, slow.files[i].dataLength) << name << " " << fast.files[i].name;
        EXPECT_EQ(fast.files[i].entrySha256, slow.files[i].entrySha256) << name << " " << fast.files[i].name;
    }
}

TEST(Signature_Test, SectorImagesReadAsTheDiskImageLoaders)
{
    const std::vector<uint8_t> scl = ReadAll(TestPathHelper::GetTestDataPath(kScl));
    ExpectSameAsDiskImage(scl, "eyeache2.scl");
    std::unique_ptr<unrealdisk::DiskImage> disk = LoadDisk(scl, "eyeache2.scl");
    ASSERT_TRUE(disk);
    ExpectSameAsDiskImage(SaveAs(*disk, "plain.trd"), "plain.trd");
    // A deleted entry stays in the catalog: both paths leave it out
    const Signatures reference = Of(scl, "eyeache2.scl");
    unrealdisk::TrdosFileSystem fs(*disk);
    std::string error;
    ASSERT_TRUE(fs.Delete(reference.files.front().name, reference.files.front().type, error)) << error;
    const std::vector<uint8_t> deleted = SaveAs(*disk, "deleted.trd");
    ExpectSameAsDiskImage(deleted, "deleted.trd");
    EXPECT_EQ(Of(deleted, "deleted.trd").files.size(), reference.files.size() - 1);
    // A TRD cut short: the loader pads it, the sector path hands it over
    std::vector<uint8_t> cut = SaveAs(*disk, "cut.trd");
    cut.resize(16 * 256 * 3);
    ExpectSameAsDiskImage(cut, "cut.trd");
    // A broken SCL sum: neither reads it as a disk
    std::vector<uint8_t> badSum = scl;
    badSum.back() ^= 0x55;
    ExpectSameAsDiskImage(badSum, "bad.scl");
}

TEST(Signature_Test, FuzzyMatchesASavedToDiskOnly)
{
    const std::vector<uint8_t> scl = ReadAll(TestPathHelper::GetTestDataPath(kScl));
    const Signatures reference = Of(scl, "eyeache2.scl");
    std::unique_ptr<unrealdisk::DiskImage> disk = LoadDisk(scl, "eyeache2.scl");
    ASSERT_TRUE(disk);
    ASSERT_GE(reference.files.size(), 4u) << "the fixture needs a few files for one added to stay >= 0.8";

    // A high-score file the game saved: another file set, the same title
    unrealdisk::TrdosFileSystem fs(*disk);
    std::string error;
    ASSERT_TRUE(fs.Write("HISCORE", 'D', std::vector<uint8_t>(200, 0x42), {}, error)) << error;
    const Signatures saved = Of(SaveAs(*disk, "saved.trd"), "saved.trd");
    EXPECT_NE(saved.content, reference.content);
    const MatchResult match = Compare(reference, saved);
    EXPECT_EQ(match.level, MatchLevel::Fuzzy) << match.reason;
    EXPECT_EQ(match.reason, "file set: every entry kept, 0 of " + std::to_string(reference.files.size()) +
                                " files changed, 1 added");

    // The game rewrote a code file in place (TR-DOS deletes the old entry, writes the new one): one file changed
    const DiskFile* code = nullptr;
    for (const DiskFile& file : reference.files)
        if (file.code)
            code = &file;
    ASSERT_NE(code, nullptr);
    {
        std::unique_ptr<unrealdisk::DiskImage> copy = LoadDisk(scl, "eyeache2.scl");
        unrealdisk::TrdosFileSystem rewrite(*copy);
        unrealdisk::TrdosFile entry;
        ASSERT_TRUE(rewrite.Find(code->name, code->type, entry, error)) << error;
        std::vector<uint8_t> data;
        ASSERT_TRUE(rewrite.Read(entry, data, nullptr, error)) << error;
        data[data.size() / 2] ^= 0x5A;
        unrealdisk::TrdosFileSystem::WriteOptions options;
        options.start = entry.start;
        ASSERT_TRUE(rewrite.Write(code->name, code->type, data, options, error)) << error;
        const MatchResult rewritten = Compare(reference, Of(SaveAs(*copy, "rewritten.trd"), "rewritten.trd"));
        EXPECT_EQ(rewritten.level, MatchLevel::Fuzzy) << rewritten.reason;
    }

    // A code file gone: no match
    ASSERT_TRUE(fs.Delete(code->name, code->type, error)) << error;
    const Signatures broken = Of(SaveAs(*disk, "broken.trd"), "broken.trd");
    EXPECT_EQ(Compare(reference, broken).level, MatchLevel::None);
}

TEST(Signature_Test, FileSetRule)
{
    const std::vector<std::string> reference = {"c:0000000000000001", "c:0000000000000002", "d:0000000000000003",
                                                "d:0000000000000004"};
    // One data file added: 4 / 5
    FileSetScore score = CompareFileSets(reference, {"c:0000000000000001", "c:0000000000000002", "d:0000000000000003",
                                                     "d:0000000000000004", "d:00000000000000ff"});
    EXPECT_TRUE(score.matches);
    EXPECT_DOUBLE_EQ(score.jaccard, 0.8);
    // A data file replaced: 3 / 5 is below the threshold
    score = CompareFileSets(reference, {"c:0000000000000001", "c:0000000000000002", "d:0000000000000003",
                                        "d:00000000000000ff"});
    EXPECT_FALSE(score.matches);
    // A code file missing
    score = CompareFileSets(reference, {"c:0000000000000001", "d:0000000000000003", "d:0000000000000004"});
    EXPECT_EQ(score.codeMissing, 1u);
    EXPECT_FALSE(score.matches);
    EXPECT_FALSE(CompareFileSets({}, {}).matches);

    // The catalog rule: the reference has entry tokens
    const std::vector<std::string> catalog = {"n:00000000000000e1", "n:00000000000000e2", "n:00000000000000e3",
                                              "c:0000000000000001", "c:0000000000000002", "d:0000000000000003"};
    score = CompareFileSets(catalog, {"n:00000000000000e1", "n:00000000000000e2", "n:00000000000000e3",
                                      "c:0000000000000001", "c:00000000000000f2", "d:0000000000000003"});
    EXPECT_TRUE(score.catalogRule);
    EXPECT_TRUE(score.matches) << "a code file rewritten in place";
    EXPECT_EQ(score.changed, 1u);
    score = CompareFileSets(catalog, {"n:00000000000000e1", "n:00000000000000e2", "n:00000000000000e3",
                                      "c:0000000000000001", "c:00000000000000f2", "d:00000000000000f3"});
    EXPECT_FALSE(score.matches) << "two files changed";
    score = CompareFileSets(catalog, {"n:00000000000000e1", "n:00000000000000e3", "c:0000000000000001",
                                      "d:0000000000000003"});
    EXPECT_EQ(score.entriesMissing, 1u);
    EXPECT_FALSE(score.matches) << "an entry gone";
    std::vector<std::string> many = catalog;
    for (const char* extra : {"n:00000000000000a1", "n:00000000000000a2", "n:00000000000000a3", "n:00000000000000a4"})
        many.push_back(extra);
    score = CompareFileSets(catalog, many);
    EXPECT_EQ(score.added, 4u);
    EXPECT_FALSE(score.matches) << "more than max(2, 3) entries added: the title inside a compilation";
    // One file: its data other is another program with a loader of the same name and length
    EXPECT_FALSE(CompareFileSets({"n:00000000000000e1", "c:0000000000000001"}, {"n:00000000000000e1", "c:0000000000000009"})
                     .matches);
    EXPECT_TRUE(CompareFileSets({"n:00000000000000e1", "c:0000000000000001"},
                                {"n:00000000000000e1", "c:0000000000000001", "n:00000000000000e2", "d:0000000000000002"})
                    .matches);

    EXPECT_TRUE(IsFileSetToken("n:0123456789abcdef"));
    EXPECT_TRUE(IsFileSetToken("c:0123456789abcdef"));
    EXPECT_FALSE(IsFileSetToken("x:0123456789abcdef"));
    EXPECT_FALSE(IsFileSetToken("c:0123"));
}

TEST(Signature_Test, MemorySignatureOfOneGameAtTwoMoments)
{
    const Signatures start =
        Of(ReadAll(TestPathHelper::GetTestDataPath("loaders/rzx/oracle/dargonscrypt-300.z80")), "dc-300.z80");
    const Signatures later =
        Of(ReadAll(TestPathHelper::GetTestDataPath("loaders/rzx/oracle/dargonscrypt-end.z80")), "dc-end.z80");
    const Signatures other =
        Of(ReadAll(TestPathHelper::GetTestDataPath("loaders/rzx/oracle/garfield-300.z80")), "garfield.z80");
    ASSERT_EQ(start.kind, MediaKind::Snapshot);
    ASSERT_EQ(start.content.rfind(kMemoryPrefix, 0), 0u);
    // Every bank the file holds (48K: three, 128K: eight) without the screen, the stack page and uniform pages
    const size_t pages = start.pages.size();
    EXPECT_GE(pages, kMemoryMinPages);
    EXPECT_LE(pages, 8u * 64u - 0x1Bu - 1u) << start.machineHint;
    const MatchResult same = Compare(start, later);
    const MatchResult different = Compare(start, other);
    EXPECT_GT(same.score, different.score) << same.reason << " / " << different.reason;
    EXPECT_EQ(different.level, MatchLevel::None) << different.reason;
}

TEST(Signature_Test, ControlVerbs)
{
    SignatureControl control;
    const std::string scl = TestPathHelper::GetTestDataPath(kScl);
    SignatureReply reply = control.Execute({"file", {{"path", scl}, {"details", ""}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    const StateNode* signature = reply.body.find("signature");
    ASSERT_NE(signature, nullptr);
    ASSERT_NE(signature->find("files"), nullptr);
    EXPECT_GT(signature->find("files")->size(), 0u);
    EXPECT_EQ(signature->find("kind")->s, "disk");

    reply = control.Execute({"compare", {{"path", scl}, {"with", scl}}});
    ASSERT_TRUE(reply.Ok());
    EXPECT_EQ(reply.body.find("match")->find("level")->s, "exact");

    reply = control.Execute({"file", {{"path", scl + ".missing"}}});
    EXPECT_EQ(reply.error, "not-found");
    EXPECT_EQ(reply.HttpStatus(), 404);
    EXPECT_EQ(control.Execute({"media", {}}).error, "unavailable");
    EXPECT_EQ(control.Execute({"nope", {}}).error, "bad-request");
}
