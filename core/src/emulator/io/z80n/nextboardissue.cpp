#include "stdafx.h"

#include "nextboardissue.h"

#include <cctype>

namespace
{
struct IssueName
{
    const char* name;
    const char* alias;
    const char* title;
    NextBoardIssue issue;
};
constexpr IssueName kIssueNames[] = {
    {"issue2", "ks1", "Issue 2 (KS1)", NextBoardIssue::Issue2},
    {"issue4", "ks2", "Issue 4 (KS2)", NextBoardIssue::Issue4},
    {"issue5", "ks3", "Issue 5 (KS3)", NextBoardIssue::Issue5},
};
}  // namespace

bool NextBoardIssueFromName(const std::string& name, NextBoardIssue& issue)
{
    std::string lower;
    for (char c : name)
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const IssueName& n : kIssueNames)
        if (lower == n.name || lower == n.alias)
        {
            issue = n.issue;
            return true;
        }
    return false;
}

const char* NextBoardIssueName(NextBoardIssue issue)
{
    for (const IssueName& n : kIssueNames)
        if (n.issue == issue)
            return n.name;
    return "issue4";
}

const char* NextBoardIssueTitle(NextBoardIssue issue)
{
    for (const IssueName& n : kIssueNames)
        if (n.issue == issue)
            return n.title;
    return "Issue 4 (KS2)";
}

uint32_t NextSramBytes(NextBoardIssue issue, unsigned ramKb)
{
    constexpr uint32_t kMb = 0x100000;
    switch (issue)
    {
        case NextBoardIssue::Issue5: return 4 * kMb;
        case NextBoardIssue::Issue2: return ramKb > 1024 ? 2 * kMb : kMb;
        case NextBoardIssue::Issue4:
        default: return 2 * kMb;
    }
}
