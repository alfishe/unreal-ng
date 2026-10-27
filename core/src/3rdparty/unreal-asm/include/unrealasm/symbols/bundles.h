#pragma once

// Symbol bundles (symbols/tdd.md §7): label files shipped with the emulator for known ROMs and system variables, and the
// manifest that says which ROM page each belongs to. A bundle applies to a machine when one of its ROM pages hashes
// (SHA-256) as the bundle says; the emulator imports it as a set of origin "bundle" below every loaded file.
//
//   { "format": "unreal-symbols-manifest", "version": 1,
//     "bundles": [
//       { "id": "rom:48k", "title": "...", "file": "48k_rom.map",
//         "match": { "page_sha256": ["d55d..."] }, "space": "rom" },
//       { "id": "sysvars:48k", "file": "48k_variables.map", "match": { "page_sha256": ["d55d...", "8d93..."] },
//         "space": "cpu:main" },
//       { "id": "rom:sprinter-3.04-p0", "file": "sprinter/bios304-p0-drivers.map",
//         "match": { "page": 0, "page_sha256": ["..."] } } ] }
//
// "space": "rom" = the page that matched (rom<n>); another space spelling = every record without a page of its own goes
// there; absent = the records' own spaces. "page": only that ROM page is compared. "except": names left out.

#include <string>
#include <string_view>
#include <vector>

namespace unrealasm::symbols
{
struct Bundle
{
    std::string id;
    std::string title;
    std::string file;                       ///< relative to the manifest's folder
    std::vector<std::string> pageSha256;    ///< lower-case hex digests of a 16 KB ROM page
    int page = -1;                          ///< compare only this ROM page; -1 = any
    std::string space;                      ///< "rom" = the matched page; a space spelling; "" = the records' own
    std::vector<std::string> except;        ///< names left out
    std::string note;
};

struct BundleManifest
{
    std::vector<Bundle> bundles;
};

/// false with the reason when the text is no manifest
bool ParseManifest(std::string_view text, BundleManifest& out, std::string& error);

struct BundleHit
{
    const Bundle* bundle = nullptr;
    int page = -1;              ///< the ROM page that matched
    std::string space;          ///< the space its records without a page go to ("" = their own): rom<page> for "rom"
    std::string set;            ///< the set id: "bundle:<id>", with ":rom<page>" when the bundle matched several pages
};

/// The bundles a machine takes whose ROM pages hash as `pages` (index = page number, "" = no page): a bundle with
/// space "rom" once per matching page, any other once (its first matching page)
std::vector<BundleHit> MatchBundles(const BundleManifest& manifest, const std::vector<std::string>& pages);
}  // namespace unrealasm::symbols
