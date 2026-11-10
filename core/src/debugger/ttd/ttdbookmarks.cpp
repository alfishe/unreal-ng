#include "ttdbookmarks.h"

#include <algorithm>
#include <cctype>
#include <cstddef>

namespace ttd {

bool TTDBookmark::HasTag(const std::string& tag) const
{
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

bool ValidateBookmark(const TTDBookmark& bookmark, std::string* err)
{
    auto fail = [err](const std::string& why) {
        if (err)
            *err = why;
        return false;
    };
    if (bookmark.label.empty())
        return fail("bookmark label must not be empty");
    if (bookmark.label.size() > kMaxBookmarkLabelLength)
        return fail("bookmark label longer than " + std::to_string(kMaxBookmarkLabelLength) + " chars");
    if (bookmark.note.size() > kMaxBookmarkNoteLength)
        return fail("bookmark note longer than " + std::to_string(kMaxBookmarkNoteLength) + " chars");
    if (bookmark.tags.size() > kMaxBookmarkTags)
        return fail("more than " + std::to_string(kMaxBookmarkTags) + " tags");
    for (size_t i = 0; i < bookmark.tags.size(); ++i)
    {
        const std::string& tag = bookmark.tags[i];
        if (tag.empty() || tag.size() > kMaxBookmarkTagLength)
            return fail("tag '" + tag + "': 1.." + std::to_string(kMaxBookmarkTagLength) + " chars");
        for (const char c : tag)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.')
                return fail("tag '" + tag + "': only letters, digits, '_', '-', '.'");
        if (std::find(bookmark.tags.begin(), bookmark.tags.begin() + static_cast<std::ptrdiff_t>(i), tag) !=
            bookmark.tags.begin() + static_cast<std::ptrdiff_t>(i))
            return fail("tag '" + tag + "' given twice");
    }
    if (bookmark.watches.size() > kMaxBookmarkWatches)
        return fail("more than " + std::to_string(kMaxBookmarkWatches) + " watched ranges");
    for (const TTDBookmarkWatch& w : bookmark.watches)
        if (w.length == 0 || w.length > kMaxBookmarkWatchLength || (!w.bytes.empty() && w.bytes.size() != w.length))
            return fail("a watched range is 1.." + std::to_string(kMaxBookmarkWatchLength) + " bytes");
    if (bookmark.context.instruction.size() > kMaxBookmarkInstructionLength)
        return fail("the context's instruction text is too long");
    return true;
}

bool TTDBookmarkJournal::Add(const TTDBookmark& bookmark, std::string* err)
{
    // Validation before the lock: none of it touches shared state, and a
    // rejected Add leaves the journal untouched either way.
    if (!ValidateBookmark(bookmark, err))
        return false;

    std::lock_guard<std::mutex> lk(_mutex);

    for (const auto& existing : _bookmarks)
    {
        if (existing.label == bookmark.label)
        {
            if (err)
                *err = "bookmark label '" + bookmark.label + "' already exists at frame " +
                       std::to_string(existing.time.frame);
            return false;
        }
    }

    // Keep the time-sorted invariant. upper_bound (not lower_bound) places a
    // same-time bookmark AFTER existing ones, so Snapshot order matches
    // insertion order within a frame — the order a reader sees is the order
    // the user created them in.
    auto it = std::upper_bound(_bookmarks.begin(), _bookmarks.end(), bookmark.time,
                               [](const TTDTimePoint& t, const TTDBookmark& b) { return t < b.time; });
    _bookmarks.insert(it, bookmark);
    return true;
}

std::vector<TTDBookmark> TTDBookmarkJournal::Snapshot() const
{
    std::lock_guard<std::mutex> lk(_mutex);
    return _bookmarks;
}

bool TTDBookmarkJournal::Find(const std::string& label, TTDBookmark& out) const
{
    std::lock_guard<std::mutex> lk(_mutex);
    for (const auto& b : _bookmarks)
    {
        if (b.label == label)
        {
            out = b;
            return true;
        }
    }
    return false;
}

bool TTDBookmarkJournal::Edit(const std::string& label, const std::function<void(TTDBookmark&)>& edit, std::string* err)
{
    std::lock_guard<std::mutex> lk(_mutex);
    auto it = std::find_if(_bookmarks.begin(), _bookmarks.end(), [&](const TTDBookmark& b) { return b.label == label; });
    if (it == _bookmarks.end())
    {
        if (err)
            *err = "unknown bookmark label: " + label;
        return false;
    }
    TTDBookmark changed = *it;
    edit(changed);
    changed.time = it->time;   // an edit never moves a bookmark
    if (!ValidateBookmark(changed, err))
        return false;
    if (changed.label != label)
        for (const TTDBookmark& other : _bookmarks)
            if (other.label == changed.label)
            {
                if (err)
                    *err = "bookmark label '" + changed.label + "' already exists at frame " +
                           std::to_string(other.time.frame);
                return false;
            }
    *it = std::move(changed);
    return true;
}

bool TTDBookmarkJournal::Remove(const std::string& label)
{
    std::lock_guard<std::mutex> lk(_mutex);
    auto it = std::find_if(_bookmarks.begin(), _bookmarks.end(),
                           [&](const TTDBookmark& b) { return b.label == label; });
    if (it == _bookmarks.end())
        return false;
    _bookmarks.erase(it);
    return true;
}

size_t TTDBookmarkJournal::RemoveOnBranches(const std::vector<uint32_t>& branches)
{
    std::lock_guard<std::mutex> lk(_mutex);
    const size_t before = _bookmarks.size();
    _bookmarks.erase(std::remove_if(_bookmarks.begin(), _bookmarks.end(),
                                    [&](const TTDBookmark& b) {
                                        return std::find(branches.begin(), branches.end(), b.branch) != branches.end();
                                    }),
                     _bookmarks.end());
    return before - _bookmarks.size();
}

void TTDBookmarkJournal::DropBefore(const TTDTimePoint& t)
{
    std::lock_guard<std::mutex> lk(_mutex);
    _bookmarks.erase(std::remove_if(_bookmarks.begin(), _bookmarks.end(), [&](const TTDBookmark& b) { return b.time < t; }),
                     _bookmarks.end());
}

void TTDBookmarkJournal::DropAfter(const TTDTimePoint& t)
{
    std::lock_guard<std::mutex> lk(_mutex);

    // Same shape as TTDExternalEventJournal::DropAfter: erase every bookmark
    // with time > t. Bookmarks exactly at t are kept — they point at the
    // resume point itself, which stays valid.
    auto it = std::find_if(_bookmarks.begin(), _bookmarks.end(),
                           [&](const TTDBookmark& b) { return t < b.time; });
    _bookmarks.erase(it, _bookmarks.end());
}

void TTDBookmarkJournal::Clear()
{
    std::lock_guard<std::mutex> lk(_mutex);
    _bookmarks.clear();
}

size_t TTDBookmarkJournal::Size() const
{
    std::lock_guard<std::mutex> lk(_mutex);
    return _bookmarks.size();
}

bool TTDBookmarkJournal::IsEmpty() const
{
    std::lock_guard<std::mutex> lk(_mutex);
    return _bookmarks.empty();
}

} // namespace ttd
