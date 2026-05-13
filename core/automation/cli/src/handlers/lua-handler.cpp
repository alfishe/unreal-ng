#include "cli-processor.h"
#include <fstream>
#include "automation.h"
#include "common/filehelper.h"

// Include actual automation module headers for method access
#if ENABLE_LUA_AUTOMATION
#include "lua/src/automation-lua.h"
#endif

#include <sstream>

/// region <Lua Command Handlers>

#if ENABLE_LUA_AUTOMATION

void CLIProcessor::HandleLua(const ClientSession& session, const std::vector<std::string>& args)
{
    if (args.empty())
    {
        ShowLuaHelp(session);
        return;
    }

    std::string subcommand = args[0];
    
    if (subcommand == "exec")
    {
        if (args.size() < 2)
        {
            session.SendResponse(ClientSession::FormatForTerminal("Usage: lua exec <code>\n"));
            return;
        }
        // Join remaining args as code
        std::ostringstream codeStream;
        for (size_t i = 1; i < args.size(); ++i)
        {
            if (i > 1) codeStream << " ";
            codeStream << args[i];
        }
        executeLuaCode(session, codeStream.str());
    }
    else if (subcommand == "file")
    {
        if (args.size() < 2)
        {
            session.SendResponse(ClientSession::FormatForTerminal("Usage: lua file <path>\n"));
            return;
        }
        executeLuaFile(session, args[1]);
    }
    else if (subcommand == "status")
    {
        showLuaStatus(session);
    }
    else if (subcommand == "stop")
    {
        stopLuaExecution(session);
    }
    else if (subcommand == "help")
    {
        ShowLuaHelp(session);
    }
    else
    {
        session.SendResponse(ClientSession::FormatForTerminal("Unknown lua subcommand: " + subcommand + NEWLINE));
        ShowLuaHelp(session);
    }
}

void CLIProcessor::executeLuaCode(const ClientSession& session, const std::string& code)
{
    auto* automation = &Automation::GetInstance();
    if (!automation)
    {
        session.SendResponse(ClientSession::FormatForTerminal("Error: Automation not available\n"));
        return;
    }

    auto* lua = automation->getLua();
    if (!lua)
    {
        session.SendResponse(ClientSession::FormatForTerminal("Error: Lua automation not available or not enabled\n"));
        session.SendResponse(ClientSession::FormatForTerminal("(Lua automation may be disabled in build configuration))\n"));
        return;
    }

    std::string errorMessage;
    std::string capturedOutput;
    bool success = lua->executeCode(code, errorMessage, capturedOutput);

    if (success)
    {
        session.SendResponse(ClientSession::FormatForTerminal("Lua code executed successfully\n"));
        if (!capturedOutput.empty())
        {
            session.SendResponse(ClientSession::FormatForTerminal("Output:\n"));
            session.SendResponse(ClientSession::FormatForTerminal(capturedOutput));
        }
    }
    else
    {
        session.SendResponse(ClientSession::FormatForTerminal("Lua execution failed:\n"));
        session.SendResponse(ClientSession::FormatForTerminal(errorMessage + NEWLINE));
    }
}

void CLIProcessor::executeLuaFile(const ClientSession& session, const std::string& path)
{
    // Validate and resolve path
    std::string resolvedPath = FileHelper::AbsolutePath(path);
    if (resolvedPath.empty())
    {
        session.SendResponse(ClientSession::FormatForTerminal("Error: Invalid file path\n"));
        return;
    }

    // Check file exists
    if (!FileHelper::FileExists(resolvedPath))
    {
        session.SendResponse(ClientSession::FormatForTerminal("Error: File not found: " + resolvedPath + NEWLINE));
        return;
    }

    // Check file extension
    // Check .lua extension (C++17 compatible)
    if (resolvedPath.length() < 4 || resolvedPath.substr(resolvedPath.length() - 4) != ".lua")
    {
        session.SendResponse(ClientSession::FormatForTerminal("Warning: File does not have .lua extension: " + resolvedPath + NEWLINE));
    }

    // Read file content
    size_t fileSize = FileHelper::GetFileSize(resolvedPath);
    if (fileSize == 0 || fileSize == static_cast<size_t>(-1))
    {
        session.SendResponse(ClientSession::FormatForTerminal("Error: Could not read file or file is empty: " + resolvedPath + NEWLINE));
        return;
    }
    
    std::vector<uint8_t> buffer(fileSize);
    size_t bytesRead = FileHelper::ReadFileToBuffer(resolvedPath, buffer.data(), fileSize);
    if (bytesRead != fileSize)
    {
        session.SendResponse(ClientSession::FormatForTerminal("Error: Failed to read file: " + resolvedPath + NEWLINE));
        return;
    }
    
    std::string code(buffer.begin(), buffer.end());

    session.SendResponse(ClientSession::FormatForTerminal("Executing Lua file: " + resolvedPath + NEWLINE));
    
    // Execute
    executeLuaCode(session, code);
}

void CLIProcessor::showLuaStatus(const ClientSession& session)
{
    auto* automation = &Automation::GetInstance();
    if (!automation)
    {
        session.SendResponse(ClientSession::FormatForTerminal("Error: Automation not available\n"));
        return;
    }

    auto* lua = automation->getLua();
    if (!lua)
    {
        session.SendResponse(ClientSession::FormatForTerminal("Lua Interpreter Status\n"));
        session.SendResponse(ClientSession::FormatForTerminal("=======================\n"));
        session.SendResponse(ClientSession::FormatForTerminal("State: Not Available\n"));
        session.SendResponse(ClientSession::FormatForTerminal("Lua automation is not enabled or not started\n"));
        session.SendResponse(ClientSession::FormatForTerminal("(May be disabled in build configuration)\n"));
        return;
    }

    std::ostringstream oss;
    oss << "Lua Interpreter Status\n";
    oss << "=======================\n";
    oss << lua->getStatusString();
    
    session.SendResponse(ClientSession::FormatForTerminal(oss.str()));
}

void CLIProcessor::stopLuaExecution(const ClientSession& session)
{
    // Lua doesn't have async exception mechanism like Python
    // Scripts must cooperatively check for stop signals
    session.SendResponse(ClientSession::FormatForTerminal("Note: Lua stop request sent, but requires cooperative script checking\n"));
    session.SendResponse(ClientSession::FormatForTerminal("(Lua scripts must check stop flag periodically)\n"));
}

void CLIProcessor::ShowLuaHelp(const ClientSession& session)
{
    std::ostringstream oss;
    oss << "Lua Interpreter Control Commands:" << NEWLINE;
    oss << "  lua exec <code>   - Execute Lua code string" << NEWLINE;
    oss << "  lua file <path>   - Load and execute Lua file" << NEWLINE;
    oss << "  lua status        - Show interpreter status" << NEWLINE;
    oss << "  lua stop          - Request Lua execution stop" << NEWLINE;
    oss << "  lua help          - Show this help message" << NEWLINE;
    oss << NEWLINE;
    oss << "Examples:" << NEWLINE;
    oss << "  lua exec \"print('Hello from Lua')\"" << NEWLINE;
    oss << "  lua file /path/to/script.lua" << NEWLINE;
    oss << "  lua status" << NEWLINE;
    
    session.SendResponse(ClientSession::FormatForTerminal(oss.str()));
}

#else
// Lua automation is disabled

void CLIProcessor::HandleLua(const ClientSession& session, const std::vector<std::string>& args)
{
    std::ostringstream oss;
    oss << "Error: Lua automation is not available" << NEWLINE;
    oss << "Lua automation was disabled during compilation" << NEWLINE;
    oss << "Rebuild with ENABLE_LUA_AUTOMATION=ON to enable" << NEWLINE;

    session.SendResponse(ClientSession::FormatForTerminal(oss.str());
}

#endif // ENABLE_LUA_AUTOMATION

/// endregion </Lua Command Handlers>
