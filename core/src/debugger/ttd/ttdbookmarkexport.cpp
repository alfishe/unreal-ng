/// @file ttdbookmarkexport.cpp
/// @brief Agent bookmarks as a WALKTHROUGH section or a game profile's header comment (ttdbookmarkexport.h).

#include "ttdbookmarkexport.h"

#include <cstdio>
#include <sstream>

namespace ttd
{

namespace
{

std::string Hex(uint32_t v, int digits)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%0*X", digits, v);
    return buf;
}

std::string CodePageText(const TTDBookmarkContext& c)
{
    if (c.codePage == 0xFFFF)
        return "";
    switch (c.codeKind)
    {
        case 0: return "ROM " + std::to_string(c.codePage);
        case 1: return "RAM " + std::to_string(c.codePage);
        case 2: return "cache " + std::to_string(c.codePage);
        default: return "page " + std::to_string(c.codePage);
    }
}

/// Words of @p text wrapped into lines of at most @p width columns: the first line starts with @p first, the rest
/// (and every line of a later paragraph) with @p next
void Wrap(std::ostringstream& out, const std::string& text, const std::string& first, const std::string& next,
          size_t width)
{
    std::istringstream paragraphs(text);
    std::string paragraph;
    std::string prefix = first;
    while (std::getline(paragraphs, paragraph))
    {
        std::istringstream words(paragraph);
        std::string word;
        std::string line = prefix;
        bool empty = true;
        while (words >> word)
        {
            if (!empty && line.size() + 1 + word.size() > width)
            {
                out << line << "\n";
                line = next;
                empty = true;
            }
            line += (empty ? "" : " ") + word;
            empty = false;
        }
        out << (empty ? line.substr(0, line.find_last_not_of(' ') + 1) : line) << "\n";
        prefix = next;
    }
}

}  // namespace

bool TTDBookmarkExportFormatByName(const std::string& name, TTDBookmarkExportFormat& format)
{
    if (name == "markdown" || name == "md")
        format = TTDBookmarkExportFormat::Markdown;
    else if (name == "profile" || name == "yaml")
        format = TTDBookmarkExportFormat::Profile;
    else if (name == "json")
        format = TTDBookmarkExportFormat::Json;
    else
        return false;
    return true;
}

std::string TTDBookmarkTimeText(const TTDTimePoint& t)
{
    return "f" + std::to_string(t.frame) + "+" + std::to_string(t.tInFrame);
}

std::string TTDBookmarkContextText(const TTDBookmarkContext& c)
{
    if (!c.valid)
        return "";
    std::string text = "PC " + Hex(c.pc, 4);
    const std::string page = CodePageText(c);
    if (!page.empty())
        text += " (" + page + ")";
    if (!c.instruction.empty())
        text += " " + c.instruction;
    text += "; SP " + Hex(c.sp, 4) + " AF " + Hex(c.af, 4) + " BC " + Hex(c.bc, 4) + " DE " + Hex(c.de, 4) + " HL " +
            Hex(c.hl, 4) + " IX " + Hex(c.ix, 4) + " IY " + Hex(c.iy, 4) + " IM " + std::to_string(c.im) +
            (c.iff1 ? " EI" : " DI");
    if (c.p7FFD)
        text += " 7FFD " + Hex(c.p7FFD, 2);
    return text;
}

std::string TTDBookmarkWatchText(const std::vector<TTDBookmarkWatch>& watches)
{
    std::string text;
    for (const TTDBookmarkWatch& w : watches)
    {
        if (!text.empty())
            text += ", ";
        text += Hex(w.address, 4);
        if (w.length > 1)
            text += ".." + Hex(static_cast<uint16_t>(w.address + w.length - 1), 4);
        text += "=";
        if (w.bytes.empty())
            text += "?";
        for (size_t i = 0; i < w.bytes.size(); ++i)
        {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02X", w.bytes[i]);
            text += (i ? " " : "") + std::string(buf);
        }
    }
    return text;
}

std::string TTDBookmarksExportText(const std::vector<TTDBookmark>& bookmarks, TTDBookmarkExportFormat format,
                                   const TTDBookmarkExportInfo& info)
{
    std::ostringstream out;
    const std::string title = info.title.empty() ? "TTD bookmarks" : info.title;
    std::string about = std::to_string(bookmarks.size()) + " bookmark(s)";
    if (!info.tag.empty())
        about += " tagged " + info.tag;
    about += ", " + (info.machine.empty() ? std::string("the machine") : info.machine) + " history " +
             TTDBookmarkTimeText(info.first) + " .. " + TTDBookmarkTimeText(info.last);

    if (format == TTDBookmarkExportFormat::Markdown)
    {
        // GitHub reads $..$ as math: hex addresses stay in code spans (the reverse-cases docs check)
        out << "## " << title << "\n\n" << about << ".\n";
        for (const TTDBookmark& b : bookmarks)
        {
            out << "\n### " << b.label << " (`" << TTDBookmarkTimeText(b.time) << "`)\n\n";
            if (!b.note.empty())
                out << b.note << (b.note.back() == '\n' ? "" : "\n") << "\n";
            const std::string context = TTDBookmarkContextText(b.context);
            if (!context.empty())
                out << "- **At:** `" << context << "`\n";
            if (!b.watches.empty())
                out << "- **Watched:** `" << TTDBookmarkWatchText(b.watches) << "`\n";
            if (!b.tags.empty())
            {
                out << "- **Tags:**";
                for (const std::string& tag : b.tags)
                    out << " `" << tag << "`";
                out << "\n";
            }
            out << "- **Seek:** `time_travel {\"action\":\"seek_bookmark\",\"label\":\"" << b.label << "\"}`\n";
        }
        return out.str();
    }

    // Profile: comment lines for the header of a game profile (data/software/*/*.yaml), 120 columns
    Wrap(out, title + ": found with TTD (" + about + ").", "# ", "# ", 120);
    for (const TTDBookmark& b : bookmarks)
    {
        std::string line = "- " + b.label + " " + TTDBookmarkTimeText(b.time);
        if (b.context.valid)
        {
            line += ", PC " + Hex(b.context.pc, 4);
            if (!b.context.instruction.empty())
                line += " " + b.context.instruction;
        }
        if (!b.watches.empty())
            line += ", " + TTDBookmarkWatchText(b.watches);
        if (!b.note.empty())
            line += ": " + b.note;
        Wrap(out, line, "# ", "#   ", 120);
    }
    return out.str();
}

}  // namespace ttd
