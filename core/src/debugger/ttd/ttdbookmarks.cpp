#include "ttdbookmarks.h"

#include <algorithm>

namespace ttd {

bool TTDBookmarkJournal::Add(const TTDBookmark& bookmark, std::string* err)
{
    // Validation before the lock: none of it touches shared state, and a
    // rejected Add leaves the journal untouched either way.
    if (bookmark.label.empty())
    {
        if (err) *err = "bookmark label must not be empty";
        return false;
    }
    if (bookmark.label.size() > kMaxBookmarkLabelLength)
    {
        if (err)
            *err = "bookmark label longer than " +
                   std::to_string(kMaxBookmarkLabelLength) + " chars";
        return false;
    }

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
