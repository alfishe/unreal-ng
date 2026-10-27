/// @brief CLI `recent`: the files opened lately (RecentFiles), the list GET /api/v1/recent, Lua / Python
/// recent_files(), MCP media action recent and the readout media.recent show.
///
///   recent [all|snapshot|tape|disk|rzx|image|cd] [--json]   the list, newest first, with each kind's index
///   recent clear [kind]                                      forget them
///
/// Opening one is the catalogue action: `action run media.open_recent index=0 kind=tape`.

#include "cli-processor.h"

#include <emulator/media/recentfiles.h>
#include <emulator/state/statenodejson.h>

#include <sstream>

void CLIProcessor::HandleRecent(const ClientSession& session, const std::vector<std::string>& args)
{
    bool json = false;
    std::vector<std::string> words;
    for (const std::string& arg : args)
    {
        if (arg == "--json")
            json = true;
        else
            words.push_back(arg);
    }
    const bool clear = !words.empty() && words[0] == "clear";
    if (clear)
        words.erase(words.begin());
    const std::string kind = words.empty() ? std::string("all") : words[0];
    if (kind != "all" && !RecentFiles::IsKind(kind))
    {
        session.SendResponse("Usage: recent [all|snapshot|tape|disk|rzx|image|cd] [--json] | recent clear [kind]" +
                             std::string(NEWLINE) + "  open one: action run media.open_recent index=N kind=K" +
                             NEWLINE);
        return;
    }
    RecentFiles& files = RecentFiles::Instance();
    if (clear)
    {
        files.Clear(kind);
        session.SendResponse("Recent " + (kind == "all" ? std::string("files") : kind + " files") + " cleared" +
                             NEWLINE);
        return;
    }
    if (json)
    {
        session.SendResponse(StateNodeToJsonText(files.ToValue(kind)) + NEWLINE);
        return;
    }
    std::ostringstream out;
    const std::vector<RecentFiles::Entry> list = files.List(kind);
    if (list.empty())
    {
        out << "No recent " << (kind == "all" ? std::string("files") : kind + " files") << NEWLINE;
        session.SendResponse(out.str());
        return;
    }
    out << "Recent " << (kind == "all" ? std::string("files") : kind + " files") << " (newest first; open: action run "
        << "media.open_recent index=N" << (kind == "all" ? "" : " kind=" + kind) << "):" << NEWLINE;
    size_t index = 0;
    for (const RecentFiles::Entry& entry : list)
        out << "  " << index++ << "  " << entry.kind << "  " << entry.name << "  " << entry.path << NEWLINE;
    session.SendResponse(out.str());
}
