#include "unrealasm/diagnostics.h"
#include "unrealasm/document.h"

#include <algorithm>

namespace unrealasm
{
bool HasErrors(const Diagnostics& diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) { return d.severity == Severity::Error; });
}

std::string SourceDocument::Text() const
{
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (i)
            out.push_back('\n');
        out += lines[i].text;
    }
    return out;
}

SourceDocument SourceDocument::FromText(const std::string& text, const std::string& dialect)
{
    SourceDocument document;
    document.dialect = dialect;
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t end = text.find('\n', start);
        if (end == std::string::npos)
        {
            if (start < text.size())
                document.lines.push_back({text.substr(start), {}});
            break;
        }
        document.lines.push_back({text.substr(start, end - start), {}});
        start = end + 1;
    }
    return document;
}
}  // namespace unrealasm
