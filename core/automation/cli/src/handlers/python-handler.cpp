#include "cli-processor.h"
#include <fstream>
#include "automation.h"
#include "common/filehelper.h"

// Include actual automation module headers for method access
#if ENABLE_PYTHON_AUTOMATION
#include "python/src/automation-python.h"
#endif

#include <sstream>

/// region <Python Command Handlers>

#if ENABLE_PYTHON_AUTOMATION

void CLIProcessor::HandlePython(const ClientSession& session, const std::vector<std::string>& args)
{
    if (args.empty())
    {
        ShowPythonHelp(session);
        return;
    }

    std::string subcommand = args[0];
    
    if (subcommand == "exec")
    {
        if (args.size() < 2)
        {
            session.SendResponse(ClientSession::FormatForTerminal("Usage: python exec <code>\n"));
            return;
        }
        // Join remaining args as code
        std::ostringstream codeStream;
        for (size_t i = 1; i < args.size(); ++i)
        {
            if (i > 1) codeStream << " ";
            codeStream << args[i];
        }
        executePythonCode(session, codeStream.str());
    }
    else if (subcommand == "file")
    {
        if (args.size() < 2)
        {
            session.SendResponse(ClientSession::FormatForTerminal("Usage: python file <path>\n"));
            return;
        }
        executePythonFile(session, args[1]);
    }
    else if (subcommand == "status")
    {
        showPythonStatus(session);
    }
    else if (subcommand == "stop")
    {
        stopPythonExecution(session);
    }
    else if (subcommand == "help")
    {
        ShowPythonHelp(session);
    }
    else
    {
        session.SendResponse(ClientSession::FormatForTerminal("Unknown python subcommand: " + subcommand + NEWLINE));
        ShowPythonHelp(session);
    }
}

void CLIProcessor::executePythonCode(const ClientSession& session, const std::string& code)
{
    auto* automation = &Automation::GetInstance();
    if (!automation)
    {
        session.SendResponse(ClientSession::FormatForTerminal("Error: Automation not available\n"));
        return;
    }

    auto* python = automation->getPython();
    if (!python)
    {
        session.SendResponse(ClientSession::FormatForTerminal("Error: Python automation not available or not enabled\n"));
        session.SendResponse(ClientSession::FormatForTerminal("(Python automation may be disabled in build configuration)\n"));
        return;
    }

    std::string errorMessage;
    std::string capturedOutput;
    bool success = python->executeCode(code, errorMessage, capturedOutput);

    if (success)
    {
        session.SendResponse(ClientSession::FormatForTerminal("Python code executed successfully\n"));
        if (!capturedOutput.empty())
        {
            session.SendResponse(ClientSession::FormatForTerminal("Output:\n"));
            session.SendResponse(ClientSession::FormatForTerminal(capturedOutput));
        }
    }
    else
    {
        session.SendResponse(ClientSession::FormatForTerminal("Python execution failed:\n"));
        session.SendResponse(ClientSession::FormatForTerminal(errorMessage + NEWLINE));
    }
}

void CLIProcessor::executePythonFile(const ClientSession& session, const std::string& path)
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
    // Check .py extension (C++17 compatible)
    if (resolvedPath.length() < 3 || resolvedPath.substr(resolvedPath.length() - 3) != ".py")
    {
        session.SendResponse(ClientSession::FormatForTerminal("Warning: File does not have .py extension: " + resolvedPath + NEWLINE));
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

    session.SendResponse(ClientSession::FormatForTerminal("Executing Python file: " + resolvedPath + NEWLINE));
    
    // Execute
    executePythonCode(session, code);
}

void CLIProcessor::showPythonStatus(const ClientSession& session)
{
    auto* automation = &Automation::GetInstance();
    if (!automation)
    {
        session.SendResponse(ClientSession::FormatForTerminal("Error: Automation not available\n"));
        return;
    }

    auto* python = automation->getPython();
    if (!python)
    {
        session.SendResponse(ClientSession::FormatForTerminal("Python Interpreter Status\n"));
        session.SendResponse(ClientSession::FormatForTerminal("=========================\n"));
        session.SendResponse(ClientSession::FormatForTerminal("State: Not Available\n"));
        session.SendResponse(ClientSession::FormatForTerminal("Python automation is not enabled or not started\n"));
        session.SendResponse(ClientSession::FormatForTerminal("(May be disabled in build configuration)\n"));
        return;
    }

    std::ostringstream oss;
    oss << "Python Interpreter Status\n";
    oss << "=========================\n";
    oss << python->getStatusString();
    
    session.SendResponse(ClientSession::FormatForTerminal(oss.str()));
}

void CLIProcessor::stopPythonExecution(const ClientSession& session)
{
    auto* automation = &Automation::GetInstance();
    if (!automation)
    {
        session.SendResponse(ClientSession::FormatForTerminal("Error: Automation not available\n"));
        return;
    }

    auto* python = automation->getPython();
    if (!python)
    {
        session.SendResponse(ClientSession::FormatForTerminal("Error: Python automation not available\n"));
        return;
    }

    python->interruptPythonExecution();
    session.SendResponse(ClientSession::FormatForTerminal("Python execution interrupt signal sent\n"));
}

void CLIProcessor::ShowPythonHelp(const ClientSession& session)
{
    std::ostringstream oss;
    oss << "Python Interpreter Control Commands:" << NEWLINE;
    oss << "  python exec <code>   - Execute Python code string" << NEWLINE;
    oss << "  python file <path>   - Load and execute Python file" << NEWLINE;
    oss << "  python status        - Show interpreter status" << NEWLINE;
    oss << "  python stop          - Interrupt Python execution" << NEWLINE;
    oss << "  python help          - Show this help message" << NEWLINE;
    oss << NEWLINE;
    oss << "Aliases: py (shorthand for python)" << NEWLINE;
    oss << NEWLINE;
    oss << "Examples:" << NEWLINE;
    oss << "  python exec \"print('Hello from Python')\"" << NEWLINE;
    oss << "  python file /path/to/script.py" << NEWLINE;
    oss << "  py status" << NEWLINE;
    
    session.SendResponse(ClientSession::FormatForTerminal(oss.str()));
}

#else
// Python automation is disabled

void CLIProcessor::HandlePython(const ClientSession& session, const std::vector<std::string>& args)
{
    std::ostringstream oss;
    oss << "Error: Python automation is not available" << NEWLINE;
    oss << "Python automation was disabled during compilation" << NEWLINE;
    oss << "Rebuild with ENABLE_PYTHON_AUTOMATION=ON to enable" << NEWLINE;

    session.SendResponse(ClientSession::FormatForTerminal(oss.str()));
}

#endif // ENABLE_PYTHON_AUTOMATION

/// endregion </Python Command Handlers>
