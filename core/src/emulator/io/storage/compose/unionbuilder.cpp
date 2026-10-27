#include "stdafx.h"

#include "unionbuilder.h"

#include <algorithm>
#include <unordered_map>

#include "common/unicodehelper.h"

namespace
{
    /// "/A//b/" -> "/A/b"; "" -> "/"
    std::string NormalizePath(const std::string& path)
    {
        std::string out;
        size_t pos = 0;
        while (pos < path.size())
        {
            const size_t slash = path.find('/', pos);
            const std::string part = path.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
            pos = slash == std::string::npos ? path.size() : slash + 1;
            if (part.empty() || part == ".")
                continue;
            out += "/" + part;
        }
        return out.empty() ? "/" : out;
    }

    std::string Join(const std::string& dir, const std::string& name)
    {
        return dir == "/" ? "/" + name : dir + "/" + name;
    }

    struct Context
    {
        const std::vector<UnionLayer>& layers;
        UnionBuilder::KeyFunction key;
        FileTree& out;
        std::vector<std::string>* report;
        std::string* error;
        std::vector<std::string> opaque;  // normalized, of the layer being merged
        std::vector<std::string> keys;    // the key of each node of `out`, made once ("" for not yet)

        void Report(const std::string& line) const
        {
            if (report)
                report->push_back(line);
        }

        /// The key of node `index` of `out` (names never change once a node is in `out`)
        const std::string& KeyOf(uint32_t index)
        {
            if (index >= keys.size())
                keys.resize(out.NodeCount());
            std::string& k = keys[index];
            if (k.empty())
                k = key(out.Node(index).name);
            return k;
        }
    };

    bool MergeDir(Context& ctx, uint32_t dst, const FileTree& src, uint32_t from, uint16_t layer, const std::string& path)
    {
        FileTree& out = ctx.out;
        const UnionLayer& L = ctx.layers[layer];

        if (out.Node(dst).unexpanded)
        {
            // C4b: the factory reads every base directory an upper layer reaches before the merge
            if (ctx.error)
                *ctx.error = path + ": layer '" + L.name + "' merges into a base directory that was not read";
            return false;
        }
        if (std::find(ctx.opaque.begin(), ctx.opaque.end(), path) != ctx.opaque.end() && !out.Node(dst).children.empty())
        {
            for (uint32_t child : std::vector<uint32_t>(out.Node(dst).children))
                out.Detach(child);
            ctx.Report(path + ": opaque in layer '" + L.name + "', lower entries hidden");
        }

        // The entries already there (lower layers); entries this pass adds are not
        // looked up, so one layer never merges with itself
        std::unordered_map<std::string, uint32_t> existing;
        existing.reserve(out.Node(dst).children.size());
        for (uint32_t child : out.Node(dst).children)
            existing.emplace(ctx.KeyOf(child), child);

        for (uint32_t child : src.Node(from).children)
        {
            const TreeNode& s = src.Node(child);
            const std::string childPath = Join(path, s.name);
            const auto it = existing.find(ctx.key(s.name));
            if (it == existing.end())
            {
                out.CopySubtree(src, child, dst, layer);
                continue;
            }
            const uint32_t lower = it->second;
            const TreeNode& d = out.Node(lower);
            if (d.isDirectory && s.isDirectory)
            {
                if (!MergeDir(ctx, lower, src, child, layer, childPath))
                    return false;
                continue;
            }
            const std::string lowerLayer = ctx.layers[d.layer].name;
            if (L.conflict == ConflictPolicy::Error)
            {
                if (ctx.error)
                    *ctx.error = childPath + ": layer '" + L.name + "' would shadow layer '" + lowerLayer + "' (conflict: error)";
                return false;
            }
            if (L.conflict == ConflictPolicy::KeepLower)
            {
                ctx.Report(childPath + ": kept from layer '" + lowerLayer + "', layer '" + L.name + "' ignored (keep-lower)");
                continue;
            }
            out.Detach(lower);
            out.CopySubtree(src, child, dst, layer);
            ctx.Report(childPath + ": layer '" + L.name + "' shadows layer '" + lowerLayer + "'");
        }
        return true;
    }

    /// The directory at `mount`, created (owned by `layer`) where missing; a
    /// file in the way is replaced (or fails the build for conflict: error)
    uint32_t EnsurePath(Context& ctx, const std::string& mount, uint16_t layer, int64_t mtime)
    {
        FileTree& out = ctx.out;
        const UnionLayer& L = ctx.layers[layer];
        uint32_t at = FileTree::kRoot;
        std::string path = "/";
        size_t pos = 1;
        while (pos < mount.size())
        {
            const size_t slash = mount.find('/', pos);
            const std::string part = mount.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
            pos = slash == std::string::npos ? mount.size() : slash + 1;
            path = Join(path, part);

            uint32_t next = FileTree::kNone;
            const std::string key = ctx.key(part);
            for (uint32_t child : out.Node(at).children)
            {
                if (ctx.KeyOf(child) == key)
                    next = child;
            }
            if (next != FileTree::kNone && !out.Node(next).isDirectory)
            {
                if (L.conflict == ConflictPolicy::Error)
                {
                    if (ctx.error)
                        *ctx.error = path + ": layer '" + L.name + "' mounts over a file (conflict: error)";
                    return FileTree::kNone;
                }
                ctx.Report(path + ": file replaced by the mount point of layer '" + L.name + "'");
                out.Detach(next);
                next = FileTree::kNone;
            }
            if (next == FileTree::kNone)
            {
                TreeNode dir;
                dir.name = part;
                dir.isDirectory = true;
                dir.layer = layer;
                dir.mtimeUtc = mtime;
                next = out.Add(at, std::move(dir));
            }
            at = next;
        }
        return at;
    }

    void SortTree(FileTree& tree, uint32_t dir)
    {
        UnionBuilder::SortChildren(tree, dir);
        for (uint32_t child : tree.Node(dir).children)
        {
            if (tree.Node(child).isDirectory)
                SortTree(tree, child);
        }
    }
}  // namespace

std::string UnionBuilder::FatKey(const std::string& name)
{
    // ASCII names (nearly all): upper-cased in place, no decoding (a build calls this per entry and layer)
    if (std::all_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; }))
    {
        size_t end = name.size();
        while (end > 0 && (name[end - 1] == '.' || name[end - 1] == ' '))
            end--;
        std::string key(name, 0, end);
        for (char& c : key)
        {
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
        }
        return key;
    }
    std::u32string points = UnicodeHelper::DecodeUtf8(name);
    while (!points.empty() && (points.back() == U'.' || points.back() == U' '))
        points.pop_back();
    for (char32_t& c : points)
        c = UnicodeHelper::ToUpper(c);
    return UnicodeHelper::EncodeUtf8(points);
}

void UnionBuilder::SortChildren(FileTree& tree, uint32_t dir)
{
    std::vector<uint32_t>& children = tree.Node(dir).children;
    std::stable_sort(children.begin(), children.end(), [&tree](uint32_t a, uint32_t b) {
        const TreeNode& x = tree.Node(a);
        const TreeNode& y = tree.Node(b);
        if (x.isDirectory != y.isDirectory)
            return x.isDirectory;
        return x.name < y.name;
    });
}

bool UnionBuilder::Merge(const std::vector<UnionLayer>& layers, KeyFunction key, FileTree& out,
                         std::vector<std::string>* report, std::string* error)
{
    Context ctx{layers, key, out, report, error, {}, {}};
    size_t nodes = out.NodeCount();
    for (const UnionLayer& layer : layers)
        nodes += layer.tree->NodeCount();
    ctx.keys.reserve(nodes);  // at most every layer's nodes end up in `out`: the key cache grows without moving
    for (size_t i = 0; i < layers.size(); i++)
    {
        const UnionLayer& L = layers[i];
        const uint16_t layer = static_cast<uint16_t>(i);
        const std::string mount = NormalizePath(L.mount);
        const int64_t rootTime = L.tree->Node(FileTree::kRoot).mtimeUtc;

        for (const std::string& w : L.whiteout)
        {
            const std::string path = NormalizePath(w);
            const uint32_t node = out.Find(path);
            if (node != FileTree::kNone && node != FileTree::kRoot)
            {
                out.Detach(node);
                ctx.Report(path + ": whiteout in layer '" + L.name + "'");
            }
        }

        const uint32_t anchor = EnsurePath(ctx, mount, layer, rootTime);
        if (anchor == FileTree::kNone)
            return false;
        if (i == 0 && anchor == FileTree::kRoot)
            out.Node(FileTree::kRoot).mtimeUtc = rootTime;  // the volume label's time

        ctx.opaque.clear();
        for (const std::string& o : L.opaque)
            ctx.opaque.push_back(NormalizePath(o));
        if (!MergeDir(ctx, anchor, *L.tree, FileTree::kRoot, layer, mount))
            return false;
    }
    SortTree(out, FileTree::kRoot);
    return true;
}
