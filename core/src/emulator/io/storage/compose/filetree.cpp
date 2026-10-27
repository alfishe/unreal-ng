#include "stdafx.h"

#include "filetree.h"

#include <algorithm>
#include <utility>

FileTree::FileTree()
{
    TreeNode root;
    root.isDirectory = true;
    _nodes.push_back(std::move(root));
}

uint32_t FileTree::Add(uint32_t parent, TreeNode node)
{
    const uint32_t index = static_cast<uint32_t>(_nodes.size());
    node.parent = parent;
    _nodes.push_back(std::move(node));
    _nodes[parent].children.push_back(index);
    return index;
}

uint32_t FileTree::Child(uint32_t dir, std::string_view name) const
{
    for (uint32_t child : _nodes[dir].children)
    {
        if (_nodes[child].name == name)
            return child;
    }
    return kNone;
}

uint32_t FileTree::Find(std::string_view path) const
{
    uint32_t at = kRoot;
    size_t pos = 0;
    while (pos < path.size())
    {
        const size_t slash = path.find('/', pos);
        const std::string_view part = path.substr(pos, slash == std::string_view::npos ? std::string_view::npos : slash - pos);
        pos = slash == std::string_view::npos ? path.size() : slash + 1;
        if (part.empty() || part == ".")
            continue;
        if (!_nodes[at].isDirectory)
            return kNone;
        at = Child(at, part);
        if (at == kNone)
            return kNone;
    }
    return at;
}

std::string FileTree::PathOf(uint32_t index) const
{
    if (index == kRoot)
        return "/";
    std::vector<uint32_t> chain;
    for (uint32_t at = index; at != kRoot; at = _nodes[at].parent)
        chain.push_back(at);
    std::string path;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        path += "/" + _nodes[*it].name;
    return path;
}

void FileTree::Detach(uint32_t index)
{
    if (index == kRoot)
        return;
    auto& siblings = _nodes[_nodes[index].parent].children;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), index), siblings.end());
}

uint32_t FileTree::CopySubtree(const FileTree& source, uint32_t from, uint32_t parent, uint16_t layer)
{
    const TreeNode& node = source.Node(from);
    TreeNode copy;
    copy.name = node.name;
    copy.data = node.data;
    copy.mtimeUtc = node.mtimeUtc;
    copy.layer = layer;
    copy.attributes = node.attributes;
    copy.isDirectory = node.isDirectory;
    copy.unexpanded = node.unexpanded;
    copy.baseCluster = node.baseCluster;
    if (copy.data.storage == FileData::Storage::DeviceExtents)
    {
        copy.data.firstExtent = static_cast<uint32_t>(_extents.size());
        const auto first = source.Extents().begin() + node.data.firstExtent;
        _extents.insert(_extents.end(), first, first + node.data.extentCount);
    }
    const uint32_t index = Add(parent, std::move(copy));
    for (uint32_t child : node.children)
        CopySubtree(source, child, index, layer);
    return index;
}
