#include "unrealasm/symbols/bundles.h"

#include <algorithm>
#include <cctype>

#include "symbols/io/json.h"

namespace unrealasm::symbols
{
namespace
{
std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool Strings(const json::Value* value, std::vector<std::string>& out, bool lower)
{
    if (!value)
        return true;
    if (value->type == json::Value::Type::String)
    {
        out.push_back(lower ? Lower(value->string) : value->string);
        return true;
    }
    if (value->type != json::Value::Type::Array)
        return false;
    for (const json::Value& item : value->array)
    {
        if (item.type != json::Value::Type::String)
            return false;
        out.push_back(lower ? Lower(item.string) : item.string);
    }
    return true;
}

std::string Text(const json::Value* value)
{
    return value && value->type == json::Value::Type::String ? value->string : std::string();
}
}  // namespace

bool ParseManifest(std::string_view text, BundleManifest& out, std::string& error)
{
    json::Value root;
    size_t offset = 0;
    if (!json::Parse(text, root, error, offset))
    {
        error = "manifest: " + error + " at byte " + std::to_string(offset);
        return false;
    }
    if (Text(root.Get("format")) != "unreal-symbols-manifest")
    {
        error = "manifest: \"format\" is not \"unreal-symbols-manifest\"";
        return false;
    }
    const json::Value* bundles = root.Get("bundles");
    if (!bundles || bundles->type != json::Value::Type::Array)
    {
        error = "manifest: no \"bundles\" array";
        return false;
    }
    out.bundles.clear();
    for (const json::Value& item : bundles->array)
    {
        Bundle bundle;
        bundle.id = Text(item.Get("id"));
        bundle.title = Text(item.Get("title"));
        bundle.file = Text(item.Get("file"));
        bundle.space = Text(item.Get("space"));
        bundle.note = Text(item.Get("note"));
        const json::Value* match = item.Get("match");
        if (bundle.id.empty() || bundle.file.empty() || !match)
        {
            error = "manifest: a bundle needs id, file and match (" + (bundle.id.empty() ? std::string("no id") : bundle.id) + ")";
            return false;
        }
        if (!Strings(match->Get("page_sha256"), bundle.pageSha256, true) || bundle.pageSha256.empty())
        {
            error = "manifest: bundle " + bundle.id + " has no page_sha256";
            return false;
        }
        if (const json::Value* page = match->Get("page"))
        {
            if (page->type != json::Value::Type::Integer || page->integer < 0 || page->integer > 255)
            {
                error = "manifest: bundle " + bundle.id + ": page is no ROM page number";
                return false;
            }
            bundle.page = static_cast<int>(page->integer);
        }
        if (!Strings(item.Get("except"), bundle.except, false))
        {
            error = "manifest: bundle " + bundle.id + ": except is no list of names";
            return false;
        }
        out.bundles.push_back(std::move(bundle));
    }
    return true;
}

std::vector<BundleHit> MatchBundles(const BundleManifest& manifest, const std::vector<std::string>& pages)
{
    std::vector<BundleHit> hits;
    for (const Bundle& bundle : manifest.bundles)
    {
        std::vector<int> matched;
        for (size_t i = 0; i < pages.size(); i++)
        {
            if (bundle.page >= 0 && static_cast<int>(i) != bundle.page)
                continue;
            if (!pages[i].empty() && std::find(bundle.pageSha256.begin(), bundle.pageSha256.end(), Lower(pages[i])) != bundle.pageSha256.end())
                matched.push_back(static_cast<int>(i));
        }
        if (matched.empty())
            continue;
        const bool perPage = bundle.space == "rom";
        if (!perPage)
            matched.erase(matched.begin() + 1, matched.end());
        for (int page : matched)
        {
            BundleHit hit;
            hit.bundle = &bundle;
            hit.page = page;
            hit.space = perPage ? "rom" + std::to_string(page) : bundle.space;
            hit.set = "bundle:" + bundle.id + (perPage && matched.size() > 1 ? ":rom" + std::to_string(page) : std::string());
            hits.push_back(std::move(hit));
        }
    }
    return hits;
}
}  // namespace unrealasm::symbols
