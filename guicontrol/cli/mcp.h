#pragma once

#include <QString>

namespace guicontrol::cli
{

/// Serves MCP on stdin / stdout until stdin closes
int RunMcp(const QString& version);

}  // namespace guicontrol::cli
