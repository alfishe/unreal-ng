/// @file sigpoc.cpp
/// @brief POC 034 (P-12): core/swsignature over a real collection. Reads the vtrd.in ZIPs in place (members inflated in
/// memory), computes the signatures, makes the variants of each title with the core's writers (re-pack, rename, saves)
/// and measures recall, false matches and speed. See ../README.md for the charter.
///
/// signature-poc --out <dir> [--repack-every N] [--limit N] [--cross-check] [--snapshots <dir>]... <corpus dir>...
/// signature-poc --out <dir> --speed N <corpus dir>...   (N media in memory: SHA-256 alone, Compute on 1 and all threads)

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "3rdparty/digestpp/digestpp.hpp"
#include "3rdparty/miniz/miniz.h"
#include "loaders/tape/loader_tape.h"
#include "loaders/tape/writer_tap.h"
#include "loaders/tape/writer_tzx.h"
#include "match.h"
#include "swsignature.h"
#include "unrealdisk/diskimage.h"
#include "unrealdisk/floppyformats.h"
#include "unrealdisk/trdosfs.h"

namespace fs = std::filesystem;
using namespace unrealng::swsignature;

namespace
{

using Clock = std::chrono::steady_clock;

double Seconds(Clock::time_point since)
{
    return std::chrono::duration<double>(Clock::now() - since).count();
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string Extension(const std::string& name)
{
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? std::string() : Lower(name.substr(dot + 1));
}

std::vector<uint8_t> ReadAll(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

uint16_t U16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t U32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

struct ZipMember
{
    std::string name;
    std::vector<uint8_t> data;
};

/// The members of a ZIP whose extension @p wanted takes, inflated in memory (stored and deflate; no ZIP64)
bool ReadZip(const std::vector<uint8_t>& zip, const std::function<bool(const std::string&)>& wanted,
             std::vector<ZipMember>& out, std::string& error)
{
    if (zip.size() < 22)
    {
        error = "too small";
        return false;
    }
    size_t eocd = std::string::npos;
    const size_t lowest = zip.size() > 65557 ? zip.size() - 65557 : 0;
    for (size_t i = zip.size() - 22 + 1; i-- > lowest;)
        if (U32(&zip[i]) == 0x06054b50)
        {
            eocd = i;
            break;
        }
    if (eocd == std::string::npos)
    {
        error = "no end of central directory";
        return false;
    }
    const uint16_t count = U16(&zip[eocd + 10]);
    size_t at = U32(&zip[eocd + 16]);
    for (uint16_t n = 0; n < count; n++)
    {
        if (at + 46 > zip.size() || U32(&zip[at]) != 0x02014b50)
        {
            error = "bad central directory";
            return false;
        }
        const uint16_t method = U16(&zip[at + 10]);
        const uint32_t packed = U32(&zip[at + 20]);
        const uint32_t size = U32(&zip[at + 24]);
        const uint16_t nameLength = U16(&zip[at + 28]);
        const uint16_t extraLength = U16(&zip[at + 30]);
        const uint16_t commentLength = U16(&zip[at + 32]);
        const uint32_t local = U32(&zip[at + 42]);
        if (at + 46 + nameLength > zip.size())
        {
            error = "bad name";
            return false;
        }
        std::string name(reinterpret_cast<const char*>(&zip[at + 46]), nameLength);
        at += 46 + nameLength + extraLength + commentLength;
        if (name.empty() || name.back() == '/' || !wanted(name) || size == 0 || size > 8u * 1024 * 1024)
            continue;
        if (local + 30 > zip.size() || U32(&zip[local]) != 0x04034b50)
            continue;
        const size_t data = local + 30 + U16(&zip[local + 26]) + U16(&zip[local + 28]);
        if (data + packed > zip.size())
            continue;
        ZipMember member;
        member.name = std::move(name);
        member.data.resize(size);
        if (method == 0 && packed == size)
            std::memcpy(member.data.data(), &zip[data], size);
        else if (method == 8)
        {
            const size_t got = tinfl_decompress_mem_to_mem(member.data.data(), size, &zip[data], packed, 0);
            if (got != size)
                continue;
        }
        else
            continue;
        out.push_back(std::move(member));
    }
    return true;
}

bool Interesting(const std::string& name)
{
    static const std::set<std::string> extensions = {"trd", "scl", "fdi", "udi", "td0", "tap", "tzx",
                                                     "z80", "sna", "szx"};
    const std::string ext = Extension(name);
    // hobeta: $B, $C, ... (one file of a TR-DOS disk)
    return extensions.count(ext) != 0 || (ext.size() == 2 && ext[0] == '$');
}

/// One title of the corpus: a medium in a vtrd release
struct Item
{
    std::string release;   ///< the ZIP, relative to the corpus root
    std::string category;  ///< gamez, full_ver, ...
    std::string member;
    Signatures sig;
};

std::string Title(const std::string& release)
{
    // The ZIP's stem: different ZIPs are different releases
    return Lower(fs::path(release).stem().string());
}

struct Counter
{
    size_t tried = 0;
    size_t ok = 0;
    void Add(bool good)
    {
        tried++;
        if (good)
            ok++;
    }
    double Share() const
    {
        return tried == 0 ? 0.0 : 100.0 * static_cast<double>(ok) / static_cast<double>(tried);
    }
};

struct Run
{
    fs::path out;
    fs::path tmp;
    size_t repackEvery = 1;
    size_t limit = 0;
    std::vector<fs::path> corpora;
    std::vector<fs::path> snapshotDirs;

    std::vector<Item> items;
    size_t zips = 0, zipErrors = 0, members = 0, bytes = 0;
    double computeSeconds = 0.0;
    size_t speed = 0;                   ///< --speed N: the speed run only
    bool crossCheck = false;            ///< every disk also through the DiskImage loaders
    size_t crossDisks = 0, crossDiffer = 0;
    double crossSeconds = 0.0;          ///< the DiskImage path's time for the same disks
    double crossFastSeconds = 0.0;      ///< the default path's time for those disks
    std::vector<std::string> crossExamples;
    std::map<std::string, size_t> byFormat;
    std::map<std::string, size_t> noContent;
    std::map<std::string, Counter> repack;    ///< "scl->trd", "tap->tzx", "rename", "label"
    std::map<std::string, Counter> saved;     ///< "add-file", "rewrite-data", "rewrite-code"
    std::map<std::string, Counter> savedByFiles;  ///< add-file recall by the file count bucket
    std::map<std::string, Counter> savedG, savedH;
    size_t ruleDisagree = 0;  ///< the library's catalog rule against the prototype H (expected 0)
    std::vector<std::string> disagreeExamples;  ///< the catalog rules, by "<what>, <bucket>"
    std::vector<std::string> repackFailures;
    FILE* tsv = nullptr;
};

/// The disk as @p name (its extension picks the format), read back
bool SaveAs(Run& run, unrealdisk::DiskImage& disk, const std::string& name, std::vector<uint8_t>& out)
{
    const fs::path path = run.tmp / name;
    const unrealdisk::FloppySaveResult saved = unrealdisk::FloppyFormats::Save({}, disk, path.string(), false);
    if (!saved.saved)
        return false;
    out = ReadAll(path);
    std::error_code ec;
    fs::remove(path, ec);
    return !out.empty();
}

bool Of(const std::vector<uint8_t>& bytes, const std::string& name, Signatures& out)
{
    std::string error;
    return Compute(bytes.data(), bytes.size(), name, out, error);
}

/// Prototype of the catalog rule: a file's catalog identity (name, type, start, length; no data) next to its
/// content token. G: every reference entry in the candidate's catalog, at most one reference file changed, at most
/// max(2, n) entries added. H: G and at least one reference file unchanged
struct CatalogSet
{
    std::set<std::string> names;    ///< "name|type|start|length"
    std::set<std::string> content;  ///< the c:/d: tokens
};

/// The c: / d: tokens only: the Jaccard rule of the spec (the library falls back to it without n: tokens)
std::vector<std::string> ContentTokens(const Signatures& sig)
{
    std::vector<std::string> tokens;
    for (const std::string& token : sig.FileSet())
        if (!IsCatalogToken(token))
            tokens.push_back(token);
    return tokens;
}

CatalogSet CatalogOf(const Signatures& sig)
{
    CatalogSet set;
    for (const DiskFile& file : sig.files)
        set.names.insert(file.name + "|" + std::string(1, file.type) + "|" + std::to_string(file.start) + "|" +
                         std::to_string(file.length));
    const std::vector<std::string> tokens = ContentTokens(sig);
    set.content.insert(tokens.begin(), tokens.end());
    return set;
}

struct CatalogScore
{
    bool g = false;
    bool h = false;
};

CatalogScore CompareCatalogs(const CatalogSet& reference, const CatalogSet& candidate)
{
    CatalogScore score;
    if (reference.names.empty())
        return score;
    for (const std::string& name : reference.names)
        if (candidate.names.count(name) == 0)
            return score;
    size_t missing = 0;
    for (const std::string& token : reference.content)
        if (candidate.content.count(token) == 0)
            missing++;
    const size_t added = candidate.names.size() - reference.names.size();
    score.g = missing <= 1 && added <= std::max<size_t>(2, reference.names.size());
    score.h = score.g && missing < reference.content.size();
    return score;
}

std::string Bucket(size_t files)
{
    if (files <= 1)
        return "1 file";
    if (files <= 2)
        return "2 files";
    if (files <= 4)
        return "3-4 files";
    if (files <= 8)
        return "5-8 files";
    return "9+ files";
}

void DiskVariants(Run& run, const Item& item, const std::vector<uint8_t>& bytes)
{
    std::unique_ptr<unrealdisk::DiskImage> disk;
    std::string format;
    if (!unrealdisk::FloppyFormats::LoadData({}, bytes.data(), bytes.size(), item.member, disk, format).Ok() || !disk)
        return;
    const std::string from = item.sig.format;
    // Re-pack: every other format the core writes
    for (const char* to : {"trd", "scl", "fdi", "udi"})
    {
        if (from == to)
            continue;
        std::vector<uint8_t> other;
        Signatures sig;
        const std::string key = from + "->" + to;
        if (!SaveAs(run, *disk, std::string("repack.") + to, other))
        {
            run.repack[key + " (not writable)"].Add(true);
            continue;
        }
        const bool good = Of(other, std::string("renamed copy.") + to, sig) && sig.content == item.sig.content;
        run.repack[key].Add(good);
        if (!good && run.repackFailures.size() < 40)
            run.repackFailures.push_back(item.release + " / " + item.member + ": " + key + " gives " + sig.content);
    }
    // Rename: the file and the disk label (TRD bytes, label at sector 9 + #F5)
    {
        Signatures sig;
        run.repack["rename file"].Add(Of(bytes, "Some Other Name." + from, sig) && sig.content == item.sig.content);
        std::vector<uint8_t> trd;
        if (SaveAs(run, *disk, "label.trd", trd) && trd.size() >= 0x900)
        {
            for (size_t i = 0; i < 8; i++)
                trd[0x8F5 + i] = static_cast<uint8_t>("RENAMED!"[i]);
            run.repack["rename label"].Add(Of(trd, "label.trd", sig) && sig.content == item.sig.content);
        }
    }
    // Saved to: what a game does to its disk
    unrealdisk::TrdosFileSystem trdos(*disk);
    std::vector<unrealdisk::TrdosFile> files;
    std::string error;
    if (!trdos.List(files, error))
        return;
    const std::string bucket = Bucket(item.sig.files.size());
    const CatalogSet reference = CatalogOf(item.sig);
    auto record = [&](const std::string& what, const Signatures& sig) {
        const bool equal = sig.content == item.sig.content;
        const bool a = equal || CompareFileSets(ContentTokens(item.sig), ContentTokens(sig)).matches;
        const MatchResult match = Compare(item.sig, sig);  // the library: the catalog rule
        CatalogScore catalog = CompareCatalogs(reference, CatalogOf(sig));
        catalog.g = catalog.g || equal;
        const bool library = match.level == MatchLevel::Fuzzy || match.level == MatchLevel::Content;
        if (library != (catalog.h || equal))
        {
            run.ruleDisagree++;
            if (run.disagreeExamples.size() < 20)
                run.disagreeExamples.push_back(item.release + " [" + item.member + "] " + what + ": library " +
                                               (library ? "yes" : "no") + " (" + match.reason + ")");
        }
        catalog.h = library;
        run.saved[what].Add(a);
        run.savedByFiles[what + ", " + bucket].Add(a);
        run.savedG[what].Add(catalog.g);
        run.savedG[what + ", " + bucket].Add(catalog.g);
        run.savedH[what].Add(catalog.h);
        run.savedH[what + ", " + bucket].Add(catalog.h);
    };
    auto fresh = [&]() {
        std::unique_ptr<unrealdisk::DiskImage> copy;
        std::string f;
        unrealdisk::FloppyFormats::LoadData({}, bytes.data(), bytes.size(), item.member, copy, f);
        return copy;
    };
    // Add a file: a high-score table as data, then as code (SAVE "HISCORE" CODE, the usual way)
    for (const char type : {'D', 'C'})
    {
        std::unique_ptr<unrealdisk::DiskImage> copy = fresh();
        unrealdisk::TrdosFileSystem fs2(*copy);
        std::vector<uint8_t> hiscore(200);
        for (size_t i = 0; i < hiscore.size(); i++)
            hiscore[i] = static_cast<uint8_t>(i * 7 + item.sig.files.size());
        unrealdisk::TrdosFileSystem::WriteOptions options;
        options.start = type == 'C' ? 0xC000 : 0;
        std::vector<uint8_t> trd;
        Signatures sig;
        if (fs2.Write("HISCORE", type, hiscore, options, error) && SaveAs(run, *copy, "saved.trd", trd) &&
            Of(trd, "saved.trd", sig))
            record(type == 'C' ? "add a code file" : "add a data file", sig);
    }
    // Rewrite a file in place (one byte changed; TR-DOS deletes the old entry and writes the new one): the first data
    // file, then the first code file
    for (const bool code : {false, true})
    {
        const auto it = std::find_if(files.begin(), files.end(), [code](const unrealdisk::TrdosFile& file) {
            return ((file.type == 'B' || file.type == 'C') == code) && file.length > 0;
        });
        if (it == files.end())
            continue;
        std::unique_ptr<unrealdisk::DiskImage> copy = fresh();
        unrealdisk::TrdosFileSystem fs2(*copy);
        std::vector<uint8_t> data;
        if (!fs2.Read(*it, data, nullptr, error) || data.empty())
            continue;
        data[data.size() / 2] ^= 0x5A;
        unrealdisk::TrdosFileSystem::WriteOptions options;
        options.start = it->start;
        std::vector<uint8_t> trd;
        Signatures sig;
        if (!fs2.Write(it->TrimmedName(), it->type, data, options, error) || !SaveAs(run, *copy, "rewrite.trd", trd) ||
            !Of(trd, "rewrite.trd", sig))
            continue;
        record(code ? "rewrite a code file" : "rewrite a data file", sig);
    }
}

void TapeVariants(Run& run, const Item& item, const std::vector<uint8_t>& bytes)
{
    LoaderTapeBase* loader = TapeLoaderRegistry::Instance().Select(std::span<const uint8_t>(bytes), item.member);
    if (loader == nullptr)
        return;
    const TapeImage image = loader->Load(std::span<const uint8_t>(bytes), item.member);
    Signatures sig;
    const std::vector<uint8_t> tzx = TzxArchiveWriter::Write(image);
    run.repack[item.sig.format + "->tzx"].Add(Of(tzx, "renamed.tzx", sig) && sig.content == item.sig.content);
    const std::vector<uint8_t> tap = TapArchiveWriter::Write(image);
    if (!tap.empty())
        run.repack[item.sig.format + "->tap"].Add(Of(tap, "renamed.tap", sig) && sig.content == item.sig.content);
    run.repack["rename file"].Add(Of(bytes, "Some Other Name." + item.sig.format, sig) && sig.content == item.sig.content);
}

void Walk(Run& run, const fs::path& root)
{
    std::vector<fs::path> zips;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end;
         it.increment(ec))
    {
        if (ec)
            break;
        if (it->is_regular_file(ec) && Extension(it->path().filename().string()) == "zip")
            zips.push_back(it->path());
    }
    std::sort(zips.begin(), zips.end());
    for (const fs::path& path : zips)
    {
        if (run.limit != 0 && run.zips >= run.limit)
            break;
        run.zips++;
        const std::vector<uint8_t> zip = ReadAll(path);
        std::vector<ZipMember> members;
        std::string error;
        if (!ReadZip(zip, Interesting, members, error))
        {
            run.zipErrors++;
            continue;
        }
        const std::string release = fs::relative(path, root.parent_path(), ec).generic_string();
        for (const ZipMember& member : members)
        {
            Item item;
            item.release = release;
            item.category = root.filename().string();
            item.member = member.name;
            const auto start = Clock::now();
            std::string why;
            const bool ok = Compute(member.data.data(), member.data.size(), member.name, item.sig, why);
            const double took = Seconds(start);
            run.computeSeconds += took;
            run.members++;
            run.bytes += member.data.size();
            if (!ok)
                continue;
            if (run.crossCheck && item.sig.kind == MediaKind::Disk)
            {
                Options viaDiskImage;
                viaDiskImage.sectorImages = false;
                Signatures slow;
                const auto slowStart = Clock::now();
                Compute(member.data.data(), member.data.size(), member.name, slow, why, viaDiskImage);
                run.crossSeconds += Seconds(slowStart);
                run.crossFastSeconds += took;
                run.crossDisks++;
                if (slow.content != item.sig.content || slow.FileSet() != item.sig.FileSet())
                {
                    run.crossDiffer++;
                    if (run.crossExamples.size() < 20)
                        run.crossExamples.push_back(release + " / " + member.name);
                }
            }
            run.byFormat[ToText(item.sig.kind) + std::string(" ") + item.sig.format]++;
            if (item.sig.content.empty())
                run.noContent[ToText(item.sig.kind) + std::string(" ") + item.sig.format]++;
            std::fprintf(run.tsv, "%s\t%s\t%s\t%s\t%zu\t%zu\t%s\t%s\n", item.release.c_str(), item.member.c_str(),
                         ToText(item.sig.kind), item.sig.format.c_str(), item.sig.size, item.sig.files.size(),
                         item.sig.sha256.c_str(), item.sig.content.c_str());
            const bool variants = run.repackEvery <= 1 || run.items.size() % run.repackEvery == 0;
            if (variants && !item.sig.content.empty())
            {
                if (item.sig.kind == MediaKind::Disk && item.sig.format != "hobeta")
                    DiskVariants(run, item, member.data);
                else if (item.sig.kind == MediaKind::Tape)
                    TapeVariants(run, item, member.data);
            }
            item.sig.blocks.clear();  // keep the memory down: the files and the content stay
            item.sig.pages.clear();
            run.items.push_back(std::move(item));
        }
        if (run.zips % 2000 == 0)
            std::fprintf(stderr, "  %zu zips, %zu media\n", run.zips, run.items.size());
    }
}

/// The candidate false matches: exact content shared by two releases, fuzzy matches between two releases, per variant
/// of the rule
void FalseMatches(Run& run, FILE* report)
{
    // Exact content across releases
    std::map<std::string, std::set<std::string>> byContent;
    for (const Item& item : run.items)
        if (!item.sig.content.empty())
            byContent[item.sig.content].insert(Title(item.release));
    size_t shared = 0, sharedItems = 0;
    std::vector<std::string> sharedSamples;
    for (const auto& [content, titles] : byContent)
        if (titles.size() > 1)
        {
            shared++;
            sharedItems += titles.size();
            if (sharedSamples.size() < 30)
            {
                std::string line = content.substr(0, 28) + ":";
                for (const std::string& t : titles)
                    line += " " + t;
                sharedSamples.push_back(line);
            }
        }
    size_t distinct = byContent.size();
    std::fprintf(report, "\n## Exact content across releases\n\ndistinct content signatures: %zu; shared by >1 release: %zu "
                         "(%zu releases involved)\n",
                 distinct, shared, sharedItems);
    for (const std::string& s : sharedSamples)
        std::fprintf(report, "  %s\n", s.c_str());

    // Fuzzy: one disk per content (duplicates are exact, counted above), index by token
    std::vector<const Item*> disks;
    std::set<std::string> seen;
    for (const Item& item : run.items)
        if (item.sig.kind == MediaKind::Disk && !item.sig.files.empty() && seen.insert(item.sig.content).second)
            disks.push_back(&item);
    std::vector<std::vector<std::string>> sets(disks.size());
    std::map<std::string, std::vector<size_t>> byToken;
    for (size_t i = 0; i < disks.size(); i++)
    {
        sets[i] = ContentTokens(disks[i]->sig);
        for (const std::string& token : std::set<std::string>(sets[i].begin(), sets[i].end()))
            byToken[token].push_back(i);
    }
    struct Variant
    {
        const char* name;
        size_t pairs = 0;
        std::set<size_t> items;
        std::vector<std::string> samples;
    };
    Variant variants[] = {{"A: Jaccard >= 0.8, no code file missing (spec)"},
                          {"B: Jaccard >= 0.8 only"},
                          {"C: A and the reference has >= 3 files"}};
    size_t candidatePairs = 0;
    for (size_t i = 0; i < disks.size(); i++)
    {
        std::set<size_t> others;
        for (const std::string& token : std::set<std::string>(sets[i].begin(), sets[i].end()))
            for (size_t j : byToken[token])
                if (j != i)
                    others.insert(j);
        for (size_t j : others)
        {
            if (Title(disks[i]->release) == Title(disks[j]->release))
                continue;  // the disks of one release
            candidatePairs++;
            const FileSetScore score = CompareFileSets(sets[i], sets[j]);
            const bool b = score.jaccard >= kFuzzyJaccard;
            const bool a = score.matches;
            const bool c = a && sets[i].size() >= 3;
            const bool hits[] = {a, b, c};
            for (int v = 0; v < 3; v++)
            {
                if (!hits[v])
                    continue;
                variants[v].pairs++;
                variants[v].items.insert(i);
                if (variants[v].samples.size() < 60)
                {
                    char line[1024];
                    std::snprintf(line, sizeof line, "%s [%s] ~ %s [%s]: J %.2f, %zu common, %zu+%zu files",
                                  disks[i]->release.c_str(), disks[i]->member.c_str(), disks[j]->release.c_str(),
                                  disks[j]->member.c_str(), score.jaccard, score.common, sets[i].size(), sets[j].size());
                    variants[v].samples.push_back(line);
                }
            }
        }
    }
    const double pairs = static_cast<double>(disks.size()) * static_cast<double>(disks.size() - 1);
    std::fprintf(report, "\n## Fuzzy matches between releases\n\ndisks (distinct content): %zu; ordered pairs: %.0f; pairs "
                         "sharing a file: %zu\n",
                 disks.size(), pairs, candidatePairs);
    for (const Variant& v : variants)
    {
        std::fprintf(report, "\n### %s\n\npairs: %zu (%.5f %% of all pairs); disks matching another release: %zu (%.3f %%)\n",
                     v.name, v.pairs, pairs > 0 ? 100.0 * static_cast<double>(v.pairs) / pairs : 0.0, v.items.size(),
                     disks.empty() ? 0.0 : 100.0 * static_cast<double>(v.items.size()) / static_cast<double>(disks.size()));
        for (const std::string& s : v.samples)
            std::fprintf(report, "  %s\n", s.c_str());
    }

    // The catalog rules: candidates hold the reference's rarest catalog entry
    std::vector<CatalogSet> catalogs(disks.size());
    std::map<std::string, std::vector<size_t>> byName;
    for (size_t i = 0; i < disks.size(); i++)
    {
        catalogs[i] = CatalogOf(disks[i]->sig);
        for (const std::string& name : catalogs[i].names)
            byName[name].push_back(i);
    }
    Variant catalogVariants[] = {{"G: catalog rule (every entry kept, <= 1 file changed, <= max(2, n) added)"},
                                 {"H: G and >= 1 file unchanged (the library's catalog rule)"}};
    std::map<std::string, std::pair<size_t, size_t>> gByBucket;  ///< references, matching another release
    for (size_t i = 0; i < disks.size(); i++)
    {
        const std::string* rarest = nullptr;
        for (const std::string& name : catalogs[i].names)
            if (rarest == nullptr || byName[name].size() < byName[*rarest].size())
                rarest = &name;
        if (rarest == nullptr)
            continue;
        bool any = false;
        for (size_t j : byName[*rarest])
        {
            if (j == i || Title(disks[i]->release) == Title(disks[j]->release))
                continue;
            CatalogScore score = CompareCatalogs(catalogs[i], catalogs[j]);
            const bool library = CompareFileSets(disks[i]->sig.FileSet(), disks[j]->sig.FileSet()).matches;
            if (library != score.h)
            {
                run.ruleDisagree++;
                if (run.disagreeExamples.size() < 20)
                    run.disagreeExamples.push_back(disks[i]->release + " [" + disks[i]->member + "] ~ " + disks[j]->release +
                                                   " [" + disks[j]->member + "]: library " + (library ? "yes" : "no") +
                                                   ", prototype " + (score.h ? "yes" : "no"));
            }
            score.h = library;
            const bool hits[] = {score.g, score.h};
            for (int v = 0; v < 2; v++)
            {
                if (!hits[v])
                    continue;
                if (v == 0)
                    any = true;
                catalogVariants[v].pairs++;
                catalogVariants[v].items.insert(i);
                if (catalogVariants[v].samples.size() < 250)
                {
                    char line[1024];
                    std::snprintf(line, sizeof line, "%s [%s] ~ %s [%s]: %zu+%zu files", disks[i]->release.c_str(),
                                  disks[i]->member.c_str(), disks[j]->release.c_str(), disks[j]->member.c_str(),
                                  catalogs[i].names.size(), catalogs[j].names.size());
                    catalogVariants[v].samples.push_back(line);
                }
            }
        }
        auto& bucket = gByBucket[Bucket(catalogs[i].names.size())];
        bucket.first++;
        if (any)
            bucket.second++;
    }
    for (const Variant& v : catalogVariants)
    {
        std::fprintf(report, "\n### %s\n\npairs: %zu (%.5f %% of all pairs); disks matching another release: %zu (%.3f %%)\n",
                     v.name, v.pairs, pairs > 0 ? 100.0 * static_cast<double>(v.pairs) / pairs : 0.0, v.items.size(),
                     disks.empty() ? 0.0 : 100.0 * static_cast<double>(v.items.size()) / static_cast<double>(disks.size()));
        if (&v == &catalogVariants[0])
            for (const auto& [bucket, counts] : gByBucket)
                std::fprintf(report, "  %-10s references %6zu, matching another release %5zu (%.3f %%)\n", bucket.c_str(),
                             counts.first, counts.second,
                             counts.first ? 100.0 * static_cast<double>(counts.second) / static_cast<double>(counts.first) : 0.0);
        for (const std::string& s : v.samples)
            std::fprintf(report, "  %s\n", s.c_str());
    }
}

void Snapshots(Run& run, FILE* report)
{
    std::vector<std::pair<std::string, Signatures>> snaps;
    double seconds = 0.0;
    // Directory by directory: an unreadable one is skipped, not the end of the walk
    std::vector<fs::path> files;
    std::vector<fs::path> pending(run.snapshotDirs.begin(), run.snapshotDirs.end());
    while (!pending.empty())
    {
        const fs::path dir = pending.back();
        pending.pop_back();
        std::error_code ec;
        for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
             it.increment(ec))
        {
            std::error_code type;
            if (it->is_directory(type) && !it->is_symlink(type))
            {
                if (it->path().filename() != "vtrd")
                    pending.push_back(it->path());
                continue;
            }
            const std::string ext = Extension(it->path().filename().string());
            if (it->is_regular_file(type) && (ext == "z80" || ext == "sna" || ext == "szx"))
                files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    size_t noPages = 0;
    for (const fs::path& path : files)
    {
        const std::vector<uint8_t> bytes = ReadAll(path);
        Signatures sig;
        std::string error;
        const auto start = Clock::now();
        const bool ok = Compute(bytes.data(), bytes.size(), path.filename().string(), sig, error);
        seconds += Seconds(start);
        if (ok && !sig.pages.empty())
            snaps.emplace_back(path.string(), std::move(sig));
        else
            noPages++;
    }
    std::fprintf(report, "\n## Snapshots (memory signature)\n\nsnapshot files %zu, without memory pages %zu (no logical RAM "
                         "banks, unreadable)\n",
                 files.size(), noPages);
    std::fprintf(report, "snapshots with memory pages: %zu (%.1f ms each)\n", snaps.size(),
                 snaps.empty() ? 0.0 : 1000.0 * seconds / static_cast<double>(snaps.size()));
    // The oracle pairs: <game>-300 and <game>-end
    std::map<std::string, std::vector<size_t>> games;
    for (size_t i = 0; i < snaps.size(); i++)
    {
        const std::string stem = fs::path(snaps[i].first).stem().string();
        const size_t dash = stem.rfind('-');
        if (snaps[i].first.find("rzx/oracle") != std::string::npos && dash != std::string::npos)
            games[stem.substr(0, dash)].push_back(i);
    }
    const double thresholds[] = {0.6, 0.7, 0.8, 0.9};
    size_t sameAt[4] = {0, 0, 0, 0}, sameGames = 0, differentAt[4] = {0, 0, 0, 0};
    for (const auto& [game, ids] : games)
        if (ids.size() == 2)
        {
            const double share = MemoryShare(snaps[ids[0]].second.pages, snaps[ids[1]].second.pages);
            sameGames++;
            for (int t = 0; t < 4; t++)
                if (share >= thresholds[t])
                    sameAt[t]++;
            std::fprintf(report, "  same game %-14s: %.1f %% equal pages -> %s\n", game.c_str(), 100.0 * share,
                         share >= kMemoryShare ? "match" : "no match");
        }
    // Every pair of different files: the false matches
    size_t pairs = 0, matches = 0;
    double best = 0.0;
    std::string bestPair;
    std::vector<std::string> samples;
    for (size_t i = 0; i < snaps.size(); i++)
        for (size_t j = i + 1; j < snaps.size(); j++)
        {
            const std::string a = fs::path(snaps[i].first).stem().string();
            const std::string b = fs::path(snaps[j].first).stem().string();
            if (a.substr(0, a.rfind('-')) == b.substr(0, b.rfind('-')))
                continue;
            if (snaps[i].second.sha256 == snaps[j].second.sha256)
                continue;  // one file twice
            pairs++;
            const double share = MemoryShare(snaps[i].second.pages, snaps[j].second.pages);
            for (int t = 0; t < 4; t++)
                if (share >= thresholds[t])
                    differentAt[t]++;
            if (share >= kMemoryShare)
            {
                matches++;
                if (samples.size() < 20)
                    samples.push_back(snaps[i].first + " ~ " + snaps[j].first + ": " + std::to_string(share));
            }
            else if (share > best)
            {
                best = share;
                bestPair = a + " ~ " + b;
            }
        }
    std::fprintf(report, "  pairs of different files: %zu; matching >= %.0f %%: %zu; best non-match %.1f %% (%s)\n", pairs,
                 100.0 * kMemoryShare, matches, 100.0 * best, bestPair.c_str());
    for (const std::string& s : samples)
        std::fprintf(report, "    %s\n", s.c_str());
    for (int t = 0; t < 4; t++)
        std::fprintf(report, "  threshold %.0f %%: same game %zu / %zu, different pairs %zu / %zu\n", 100.0 * thresholds[t],
                     sameAt[t], sameGames, differentAt[t], pairs);
}

void Report(Run& run, double total)
{
    FILE* report = std::fopen((run.out / "summary.md").string().c_str(), "w");
    if (report == nullptr)
        report = stdout;
    std::fprintf(report, "# POC 034 run\n\nzips %zu (unreadable %zu), media %zu, %.1f MB, signatures %.2f s: %.0f files/s, "
                         "%.0f MB/s; the whole run %.0f s\n",
                 run.zips, run.zipErrors, run.members, static_cast<double>(run.bytes) / 1e6, run.computeSeconds,
                 run.computeSeconds > 0 ? static_cast<double>(run.members) / run.computeSeconds : 0.0,
                 run.computeSeconds > 0 ? static_cast<double>(run.bytes) / 1e6 / run.computeSeconds : 0.0, total);
    if (run.crossCheck)
    {
        std::fprintf(report, "\n## Sector path vs DiskImage loaders\n\ndisks %zu, results differ %zu; default path %.3f ms "
                             "a disk, DiskImage path %.3f ms a disk\n",
                     run.crossDisks, run.crossDiffer,
                     run.crossDisks ? 1000.0 * run.crossFastSeconds / static_cast<double>(run.crossDisks) : 0.0,
                     run.crossDisks ? 1000.0 * run.crossSeconds / static_cast<double>(run.crossDisks) : 0.0);
        for (const std::string& example : run.crossExamples)
            std::fprintf(report, "  differs: %s\n", example.c_str());
    }
    std::fprintf(report, "\n## Formats\n\n");
    for (const auto& [format, count] : run.byFormat)
        std::fprintf(report, "  %-16s %6zu   without content signature: %zu\n", format.c_str(), count,
                     run.noContent.count(format) ? run.noContent[format] : 0);
    std::fprintf(report, "\n## Re-pack and rename (content equal)\n\n");
    Counter all;
    for (const auto& [key, counter] : run.repack)
    {
        std::fprintf(report, "  %-26s %6zu / %6zu  %.2f %%\n", key.c_str(), counter.ok, counter.tried, counter.Share());
        if (key.find("not writable") == std::string::npos)
        {
            all.tried += counter.tried;
            all.ok += counter.ok;
        }
    }
    std::fprintf(report, "  %-26s %6zu / %6zu  %.2f %%\n", "all", all.ok, all.tried, all.Share());
    for (const std::string& failure : run.repackFailures)
        std::fprintf(report, "    fail: %s\n", failure.c_str());
    std::fprintf(report, "\n## Saved to\n\nA = the spec fuzzy rule (Jaccard >= 0.8, no code file missing); G = "
                         "every reference entry kept, <= 1 file changed, <= max(2, n) added; H = G and >= 1 file "
                         "unchanged (the library's catalog rule)\n\n");
    std::fprintf(report, "  %-34s %16s %16s %16s\n", "", "A", "G", "H");
    auto row = [&](const std::string& key, const char* indent) {
        const Counter& a = run.saved[key];
        const Counter& g = run.savedG[key];
        const Counter& h = run.savedH[key];
        std::fprintf(report, "%s%-34s %6zu/%-6zu %5.1f%% %6zu/%-6zu %5.1f%% %6zu/%-6zu %5.1f%%\n", indent, key.c_str(), a.ok,
                     a.tried, a.Share(), g.ok, g.tried, g.Share(), h.ok, h.tried, h.Share());
    };
    Counter allA, allG, allH;
    for (const auto& [key, counter] : std::map<std::string, Counter>(run.saved))
    {
        row(key, "  ");
        allA.tried += counter.tried;
        allA.ok += counter.ok;
        allG.tried += run.savedG[key].tried;
        allG.ok += run.savedG[key].ok;
        allH.tried += run.savedH[key].tried;
        allH.ok += run.savedH[key].ok;
    }
    std::fprintf(report, "  %-34s %6zu/%-6zu %5.1f%% %6zu/%-6zu %5.1f%% %6zu/%-6zu %5.1f%%\n", "all", allA.ok, allA.tried,
                 allA.Share(), allG.ok, allG.tried, allG.Share(), allH.ok, allH.tried, allH.Share());
    std::fprintf(report, "\n");
    for (const auto& [key, counter] : std::map<std::string, Counter>(run.savedByFiles))
    {
        run.saved[key] = counter;
        row(key, "    ");
    }
    FalseMatches(run, report);
    std::fprintf(report, "\nthe library's catalog rule against the prototype H: %zu disagreements\n", run.ruleDisagree);
    for (const std::string& example : run.disagreeExamples)
        std::fprintf(report, "  %s\n", example.c_str());
    Snapshots(run, report);
    if (report != stdout)
        std::fclose(report);
}

/// --speed: N media read into memory first, then timed without I/O: SHA-256 alone (the floor), Compute on one thread,
/// Compute on every hardware thread (media are independent)
void Speed(Run& run)
{
    std::vector<ZipMember> media;
    size_t bytes = 0;
    for (const fs::path& root : run.corpora)
    {
        std::vector<fs::path> zips;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             it != end && media.size() < run.speed; it.increment(ec))
            if (!ec && it->is_regular_file(ec) && Extension(it->path().filename().string()) == "zip")
                zips.push_back(it->path());
        std::sort(zips.begin(), zips.end());
        for (const fs::path& path : zips)
        {
            std::vector<ZipMember> members;
            std::string error;
            if (!ReadZip(ReadAll(path), Interesting, members, error))
                continue;
            for (ZipMember& member : members)
                if (media.size() < run.speed)
                {
                    bytes += member.data.size();
                    media.push_back(std::move(member));
                }
            if (media.size() >= run.speed)
                break;
        }
    }
    const double mb = static_cast<double>(bytes) / 1e6;
    FILE* report = std::fopen((run.out / "speed.md").string().c_str(), "w");
    if (report == nullptr)
        report = stdout;
    std::fprintf(report, "# POC 034 speed\n\nmedia %zu, %.1f MB in memory (%.1f KB each)\n\n", media.size(), mb,
                 media.empty() ? 0.0 : 1000.0 * mb / static_cast<double>(media.size()));
    for (int round = 0; round < 3; round++)
    {
        auto start = Clock::now();
        size_t sink = 0;
        for (const ZipMember& member : media)
            sink += digestpp::sha256().absorb(member.data.data(), member.data.size()).hexdigest().size();
        const double sha = Seconds(start);

        start = Clock::now();
        for (const ZipMember& member : media)
        {
            Signatures sig;
            std::string error;
            Compute(member.data.data(), member.data.size(), member.name, sig, error);
            sink += sig.content.size();
        }
        const double one = Seconds(start);

        const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
        start = Clock::now();
        std::vector<std::thread> pool;
        std::vector<size_t> sinks(threads, 0);
        for (unsigned t = 0; t < threads; t++)
            pool.emplace_back([&, t]() {
                for (size_t i = t; i < media.size(); i += threads)
                {
                    Signatures sig;
                    std::string error;
                    Compute(media[i].data.data(), media[i].data.size(), media[i].name, sig, error);
                    sinks[t] += sig.content.size();
                }
            });
        for (std::thread& thread : pool)
            thread.join();
        const double all = Seconds(start);
        for (size_t v : sinks)
            sink += v;
        const double n = static_cast<double>(media.size());
        std::fprintf(report, "round %d: SHA-256 of the files alone %.0f MB/s (%.0f files/s); Compute, 1 thread %.0f files/s "
                             "(%.0f MB/s); Compute, %u threads %.0f files/s [%zu]\n",
                     round, mb / sha, n / sha, n / one, mb / one, threads, n / all, sink % 10);
    }
    if (report != stdout)
        std::fclose(report);
}

}  // namespace

int main(int argc, char** argv)
{
    Run run;
    for (int i = 1; i < argc; i++)
    {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc)
            run.out = argv[++i];
        else if (arg == "--repack-every" && i + 1 < argc)
            run.repackEvery = std::stoul(argv[++i]);
        else if (arg == "--speed" && i + 1 < argc)
            run.speed = std::stoul(argv[++i]);
        else if (arg == "--cross-check")
            run.crossCheck = true;
        else if (arg == "--limit" && i + 1 < argc)
            run.limit = std::stoul(argv[++i]);
        else if (arg == "--snapshots" && i + 1 < argc)
            run.snapshotDirs.emplace_back(argv[++i]);
        else
            run.corpora.emplace_back(arg);
    }
    if (run.out.empty() || run.corpora.empty())
    {
        std::fprintf(stderr, "signature-poc --out <dir> [--repack-every N] [--limit N] [--cross-check] [--snapshots <dir>]... <corpus dir>...\n");
        return 2;
    }
    if (run.corpora.size() == 1 && fs::is_regular_file(run.corpora.front()))
    {
        // One file: both disk paths side by side (the cross-check's differences)
        const std::vector<uint8_t> bytes = ReadAll(run.corpora.front());
        const std::string name = run.corpora.front().filename().string();
        for (const bool sectorImages : {true, false})
        {
            Options options;
            options.sectorImages = sectorImages;
            Signatures sig;
            std::string error;
            Compute(bytes.data(), bytes.size(), name, sig, error, options);
            std::printf("%s: %s %s files %zu content %s\n", sectorImages ? "sector path" : "DiskImage  ", ToText(sig.kind),
                        sig.format.c_str(), sig.files.size(), sig.content.c_str());
            for (const std::string& warning : sig.warnings)
                std::printf("  warning: %s\n", warning.c_str());
            for (const DiskFile& file : sig.files)
                std::printf("  %-8s %c %5u %5u %3u %s\n", file.name.c_str(), file.type, file.start, file.length, file.sectors,
                            file.dataSha256.substr(0, 16).c_str());
        }
        // With --out: the disk re-packed to every writable format, each copy's signature
        std::unique_ptr<unrealdisk::DiskImage> disk;
        std::string format;
        if (!run.out.empty() && unrealdisk::FloppyFormats::LoadData({}, bytes.data(), bytes.size(), name, disk, format).Ok() &&
            disk)
        {
            fs::create_directories(run.out);
            for (const char* ext : {"trd", "scl", "fdi", "udi"})
            {
                const fs::path path = run.out / (fs::path(name).stem().string() + "-repack." + ext);
                const unrealdisk::FloppySaveResult saved = unrealdisk::FloppyFormats::Save({}, *disk, path.string(), false);
                Signatures sig;
                std::string error;
                const std::vector<uint8_t> copy = saved.saved ? ReadAll(saved.savedPath) : std::vector<uint8_t>{};
                if (saved.saved)
                    Compute(copy.data(), copy.size(), saved.savedPath, sig, error);
                std::printf("%s -> %s: %s%s files %zu content %s\n", format.c_str(), ext, saved.saved ? "saved" : "refused",
                            saved.reason.empty() ? "" : (" (" + saved.reason + ")").c_str(), sig.files.size(),
                            sig.content.c_str());
                for (const std::string& warning : sig.warnings)
                    std::printf("  warning: %s\n", warning.c_str());
            }
        }
        return 0;
    }
    if (run.speed > 0)
    {
        fs::create_directories(run.out);
        Speed(run);
        std::fprintf(stderr, "done: %s\n", (run.out / "speed.md").string().c_str());
        return 0;
    }
    run.tmp = run.out / "tmp";
    fs::create_directories(run.tmp);
    run.tsv = std::fopen((run.out / "media.tsv").string().c_str(), "w");
    if (run.tsv == nullptr)
        return 1;
    std::fprintf(run.tsv, "release\tmember\tkind\tformat\tsize\tfiles\tsha256\tcontent\n");
    const auto start = Clock::now();
    for (const fs::path& corpus : run.corpora)
    {
        std::fprintf(stderr, "%s\n", corpus.string().c_str());
        Walk(run, corpus);
    }
    std::fclose(run.tsv);
    Report(run, Seconds(start));
    std::fprintf(stderr, "done: %s\n", (run.out / "summary.md").string().c_str());
    return 0;
}
