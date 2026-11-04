#pragma once

/// @file ttdbookmarkexport.h
/// @brief Agent bookmarks written up (docs/inprogress/2026-11-03-ttd-agent-bookmarks/design.md): the bookmarks with
/// their notes, tags, context and watched bytes as a WALKTHROUGH section (Markdown) or as the comment block of a game
/// profile's header (YAML comments). The JSON form is the `bookmarks` reply itself (ttdcontrol.cpp).

#include <cstdint>
#include <string>
#include <vector>

#include "ttdbookmarks.h"

namespace ttd
{

enum class TTDBookmarkExportFormat : uint8_t
{
    Markdown,   ///< "## <title>" then one "### <label>" block per bookmark
    Profile,    ///< "# " comment lines wrapped at 120 columns, for a game profile's header
    Json        ///< the bookmarks reply as JSON text
};

/// @brief "markdown", "md", "profile", "yaml", "json"; false for anything else.
bool TTDBookmarkExportFormatByName(const std::string& name, TTDBookmarkExportFormat& format);

/// @brief What the export says about the session around the bookmarks.
struct TTDBookmarkExportInfo
{
    std::string title;          ///< The game or the task (empty: "TTD bookmarks")
    std::string machine;        ///< The model's short name
    TTDTimePoint first{};       ///< The history's first and last moments
    TTDTimePoint last{};
    std::string tag;            ///< The tag the list was filtered by (empty: all)
};

/// @brief "f<frame>+<T>".
std::string TTDBookmarkTimeText(const TTDTimePoint& t);

/// @brief The context as one line: "PC #8A14 (RAM 2) ld (hl),a; SP #FF4A AF #0344 ... IM 1 EI 7FFD #10".
std::string TTDBookmarkContextText(const TTDBookmarkContext& context);

/// @brief The watched ranges as "#A130=03, #A140..#A143=00 01 02 03".
std::string TTDBookmarkWatchText(const std::vector<TTDBookmarkWatch>& watches);

/// @brief Markdown or profile text for @p bookmarks (already filtered and in time order).
std::string TTDBookmarksExportText(const std::vector<TTDBookmark>& bookmarks, TTDBookmarkExportFormat format,
                                   const TTDBookmarkExportInfo& info);

}  // namespace ttd
