#include "interpreter_api.h"
#include <fstream>
#include "automation.h"
#include "common/filehelper.h"

// Include actual automation module headers for method access
#if ENABLE_PYTHON_AUTOMATION
#include "python/src/automation-python.h"
#endif
#if ENABLE_LUA_AUTOMATION
#include "lua/src/automation-lua.h"
#endif

#include <json/json.h>

using namespace api::v1;
using namespace drogon;

/// region <Python Interpreter Endpoints>

#if ENABLE_PYTHON_AUTOMATION

void InterpreterAPI::executePythonCode(const HttpRequestPtr& req,
                                       std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto* automation = &Automation::GetInstance();
    if (!automation)
    {
        Json::Value response;
        response["success"] = false;
        response["error"] = "Automation not available";
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
        return;
    }

    auto* python = automation->getPython();
    if (!python)
    {
        Json::Value response;
        response["success"] = false;
        response["error"] = "Python automation not available or not enabled";
        response["hint"] = "Python automation may be disabled in build configuration";
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k503ServiceUnavailable);
        callback(resp);
        return;
    }

    // Parse request body
    Json::Value requestBody = *req->getJsonObject();
    if (!requestBody.isMember("code") || !requestBody["code"].isString())
    {
        Json::Value response;
        response["success"] = false;
        response["error"] = "Missing or invalid 'code' field in request body";
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    std::string code = requestBody["code"].asString();
    std::string errorMessage;
    std::string capturedOutput;

    bool success = python->executeCode(code, errorMessage, capturedOutput);

    Json::Value response;
    response["success"] = success;

    if (success)
    {
        response["message"] = "Python code executed successfully";
        if (!capturedOutput.empty())
        {
            response["output"] = capturedOutput;
        }
    }
    else
    {
        response["error"] = errorMessage;
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(success ? k200OK : k500InternalServerError);
    callback(resp);
}

void InterpreterAPI::executePythonFile(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto* automation = &Automation::GetInstance();
    if (!automation || !automation->getPython())
    {
        Json::Value response;
        response["success"] = false;
        response["error"] = "Python automation not available";
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k503ServiceUnavailable);
        callback(resp);
        return;
    }

    // Parse request body
    Json::Value requestBody = *req->getJsonObject();
    if (!requestBody.isMember("path") || !requestBody["path"].isString())
    {
        Json::Value response;
        response["success"] = false;
        response["error"] = "Missing or invalid 'path' field in request body";
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    std::string path = requestBody["path"].asString();
    
    // Validate path
    std::string resolvedPath = FileHelper::AbsolutePath(path);
    if (resolvedPath.empty())
    {
        Json::Value response;
        response["success"] = false;
        response["error"] = "Invalid file path";
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    if (!FileHelper::FileExists(resolvedPath))
    {
        Json::Value response;
        response["success"] = false;
        response["error"] = "File not found: " + resolvedPath;
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    // Read file
    std::string code = [](const std::string& path) { std::ifstream f(path); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }(resolvedPath);
    if (code.empty())
    {
        Json::Value response;
        response["success"] = false;
        response["error"] = "Could not read file or file is empty";
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
        return;
    }

    // Execute
    std::string errorMessage;
    std::string capturedOutput;
    bool success = automation->getPython()->executeCode(code, errorMessage, capturedOutput);

    Json::Value response;
    response["success"] = success;
    response["path"] = resolvedPath;

    if (success)
    {
        response["message"] = "Python file executed successfully";
        if (!capturedOutput.empty())
        {
            response["output"] = capturedOutput;
        }
    }
    else
    {
        response["error"] = errorMessage;
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(success ? k200OK : k500InternalServerError);
    callback(resp);
}

void InterpreterAPI::getPythonStatus(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto* automation = &Automation::GetInstance();
    
    Json::Value response;
    
    if (!automation)
    {
        response["available"] = false;
        response["error"] = "Automation not available";
    }
    else
    {
        auto* python = automation->getPython();
        if (!python)
        {
            response["available"] = false;
            response["initialized"] = false;
            response["message"] = "Python automation not enabled or not started";
            response["hint"] = "May be disabled in build configuration";
        }
        else
        {
            response["available"] = true;
            response["initialized"] = true;
            response["status"] = python->getStatusString();
        }
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
    callback(resp);
}

void InterpreterAPI::stopPythonExecution(const HttpRequestPtr& req,
                                         std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto* automation = &Automation::GetInstance();
    if (!automation || !automation->getPython())
    {
        Json::Value response;
        response["success"] = false;
        response["error"] = "Python automation not available";
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k503ServiceUnavailable);
        callback(resp);
        return;
    }

    automation->getPython()->interruptPythonExecution();

    Json::Value response;
    response["success"] = true;
    response["message"] = "Python execution interrupt signal sent";

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
    callback(resp);
}

#else // ENABLE_PYTHON_AUTOMATION not defined

// Python automation disabled - provide helpful error messages
void InterpreterAPI::executePythonCode(const HttpRequestPtr& req,
                                       std::function<void(const HttpResponsePtr&)>&& callback) const
{
    Json::Value response;
    response["success"] = false;
    response["error"] = "Python automation is not available";
    response["reason"] = "Python automation was disabled during compilation";
    response["solution"] = "Rebuild with ENABLE_PYTHON_AUTOMATION=ON to enable";
    
    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k503ServiceUnavailable);
    callback(resp);
}

void InterpreterAPI::executePythonFile(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback) const
{
    executePythonCode(req, std::move(callback));
}

void InterpreterAPI::getPythonStatus(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback) const
{
    Json::Value response;
    response["available"] = false;
    response["initialized"] = false;
    response["reason"] = "Python automation was disabled during compilation";
    
    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
    callback(resp);
}

void InterpreterAPI::stopPythonExecution(const HttpRequestPtr& req,
                                         std::function<void(const HttpResponsePtr&)>&& callback) const
{
    executePythonCode(req, std::move(callback));
}

#endif // ENABLE_PYTHON_AUTOMATION

/// endregion </Python Interpreter Endpoints>

/// region <Lua Interpreter Endpoints>

#if ENABLE_LUA_AUTOMATION

void InterpreterAPI::executeLuaCode(const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto* automation = &Automation::GetInstance();
    if (!automation)
    {
        Json::Value response;
        response["success"] = false;
        response["error"] = "Automation not available";
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
        return;
    }

    auto* lua = automation->getLua();
    if (!lua)
    {
        Json::Value response;
        response["success"] = false;
        response["error"] = "Lua automation not available or not enabled";
        response["hint"] = "Lua automation may be disabled in build configuration";
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k503ServiceUnavailable);
        callback(resp);
        return;
    }

    // Parse request body
    Json::Value requestBody = *req->getJsonObject();
    if (!requestBody.isMember("code") || !requestBody["code"].isString())
    {
        Json::Value response;
        response["success"] = false;
        response["error"] = "Missing or invalid 'code' field in request body";
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    std::string code = requestBody["code"].asString();
    std::string errorMessage;
    std::string capturedOutput;

    bool success = lua->executeCode(code, errorMessage, capturedOutput);

    Json::Value response;
    response["success"] = success;

    if (success)
    {
        response["message"] = "Lua code executed successfully";
        if (!capturedOutput.empty())
        {
            response["output"] = capturedOutput;
        }
    }
    else
    {
        response["error"] = errorMessage;
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(success ? k200OK : k500InternalServerError);
    callback(resp);
}

void InterpreterAPI::executeLuaFile(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto* automation = &Automation::GetInstance();
    if (!automation || !automation->getLua())
    {
        Json::Value response;
        response["success"] = false;
        response["error"] = "Lua automation not available";
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k503ServiceUnavailable);
        callback(resp);
        return;
    }

    // Parse request body
    Json::Value requestBody = *req->getJsonObject();
    if (!requestBody.isMember("path") || !requestBody["path"].isString())
    {
        Json::Value response;
        response["success"] = false;
        response["error"] = "Missing or invalid 'path' field in request body";
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    std::string path = requestBody["path"].asString();
    
    // Validate path
    std::string resolvedPath = FileHelper::AbsolutePath(path);
    if (resolvedPath.empty())
    {
        Json::Value response;
        response["success"] = false;
        response["error"] = "Invalid file path";
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    if (!FileHelper::FileExists(resolvedPath))
    {
        Json::Value response;
        response["success"] = false;
        response["error"] = "File not found: " + resolvedPath;
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k404NotFound);
        callback(resp);
        return;
    }

    // Read file
    std::string code = [](const std::string& path) { std::ifstream f(path); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }(resolvedPath);
    if (code.empty())
    {
        Json::Value response;
        response["success"] = false;
        response["error"] = "Could not read file or file is empty";
        auto resp = HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
        return;
    }

    // Execute
    std::string errorMessage;
    std::string capturedOutput;
    bool success = automation->getLua()->executeCode(code, errorMessage, capturedOutput);

    Json::Value response;
    response["success"] = success;
    response["path"] = resolvedPath;

    if (success)
    {
        response["message"] = "Lua file executed successfully";
        if (!capturedOutput.empty())
        {
            response["output"] = capturedOutput;
        }
    }
    else
    {
        response["error"] = errorMessage;
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(success ? k200OK : k500InternalServerError);
    callback(resp);
}

void InterpreterAPI::getLuaStatus(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto* automation = &Automation::GetInstance();
    
    Json::Value response;
    
    if (!automation)
    {
        response["available"] = false;
        response["error"] = "Automation not available";
    }
    else
    {
        auto* lua = automation->getLua();
        if (!lua)
        {
            response["available"] = false;
            response["initialized"] = false;
            response["message"] = "Lua automation not enabled or not started";
            response["hint"] = "May be disabled in build configuration";
        }
        else
        {
            response["available"] = true;
            response["initialized"] = true;
            response["status"] = lua->getStatusString();
        }
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
    callback(resp);
}

void InterpreterAPI::stopLuaExecution(const HttpRequestPtr& req,
                                       std::function<void(const HttpResponsePtr&)>&& callback) const
{
    Json::Value response;
    response["success"] = true;
    response["message"] = "Lua stop request noted";
    response["note"] = "Lua doesn't have async exception mechanism like Python. Scripts must cooperatively check for stop signals.";

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
    callback(resp);
}

#else // ENABLE_LUA_AUTOMATION not defined

// Lua automation disabled - provide helpful error messages
void InterpreterAPI::executeLuaCode(const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback) const
{
    Json::Value response;
    response["success"] = false;
    response["error"] = "Lua automation is not available";
    response["reason"] = "Lua automation was disabled during compilation";
    response["solution"] = "Rebuild with ENABLE_LUA_AUTOMATION=ON to enable";
    
    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k503ServiceUnavailable);
    callback(resp);
}

void InterpreterAPI::executeLuaFile(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback) const
{
    executeLuaCode(req, std::move(callback));
}

void InterpreterAPI::getLuaStatus(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback) const
{
    Json::Value response;
    response["available"] = false;
    response["initialized"] = false;
    response["reason"] = "Lua automation was disabled during compilation";
    
    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
    callback(resp);
}

void InterpreterAPI::stopLuaExecution(const HttpRequestPtr& req,
                                       std::function<void(const HttpResponsePtr&)>&& callback) const
{
    executeLuaCode(req, std::move(callback));
}

#endif // ENABLE_LUA_AUTOMATION

/// endregion </Lua Interpreter Endpoints>
