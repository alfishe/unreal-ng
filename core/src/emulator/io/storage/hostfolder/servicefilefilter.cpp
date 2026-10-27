#include "stdafx.h"

#include "servicefilefilter.h"

#include <cctype>

namespace
{
    char Lower(char c)
    {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
}  // namespace

bool MatchesWildcard(const std::string& pattern, const std::string& name)
{
    // Iterative matcher with single-star backtracking: O(n * m) worst case
    size_t p = 0;
    size_t n = 0;
    size_t starP = std::string::npos;
    size_t starN = 0;
    while (n < name.size())
    {
        if (p < pattern.size() && (pattern[p] == '?' || Lower(pattern[p]) == Lower(name[n])))
        {
            p++;
            n++;
        }
        else if (p < pattern.size() && pattern[p] == '*')
        {
            starP = p++;
            starN = n;
        }
        else if (starP != std::string::npos)
        {
            p = starP + 1;
            n = ++starN;
        }
        else
        {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*')
        p++;
    return p == pattern.size();
}

const std::vector<ServiceFileFilter::Collection>& ServiceFileFilter::DefaultCollections()
{
    static const std::vector<Collection> collections = {
        {"macos",
         {".DS_Store", "._*", ".Spotlight-V100", ".Trashes", ".fseventsd", ".TemporaryItems",
          ".DocumentRevisions-V100", ".VolumeIcon.icns", "Icon\r", ".AppleDouble", ".AppleDB", ".AppleDesktop"},
         /*osNoise=*/true},
        {"windows", {"Thumbs.db", "ehthumbs.db", "desktop.ini", "$RECYCLE.BIN", "System Volume Information", "*.lnk"},
         /*osNoise=*/true},
        {"linux", {".directory", ".Trash-*", "lost+found", "*~"}, /*osNoise=*/true},
        {"vcs", {".git", ".gitignore", ".gitattributes", ".gitmodules", ".svn", ".hg", ".hgignore"}},
        {"unreal", {".unreal-media.yaml", ".unreal-media.json", ".unreal-staging-*"}},
    };
    return collections;
}

ServiceFileFilter::ServiceFileFilter() : _collections(DefaultCollections()) {}

bool ServiceFileFilter::IsService(const std::string& name, std::string* collection) const
{
    for (const Collection& c : _collections)
    {
        for (const std::string& pattern : c.patterns)
        {
            if (MatchesWildcard(pattern, name))
            {
                if (collection)
                    *collection = c.name;
                return true;
            }
        }
    }
    return false;
}

bool ServiceFileFilter::IsOsNoiseCollection(const std::string& collection) const
{
    for (const Collection& c : _collections)
        if (c.name == collection)
            return c.osNoise;
    return false;
}
