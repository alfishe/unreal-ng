#pragma once

#include <drogon/HttpController.h>
#include "automation.h"

namespace api
{
    namespace v1
    {
        /**
         * Interpreter Control API
         * 
         * Provides HTTP REST endpoints for remote control of Python and Lua interpreters.
         * Follows the cross-control model:
         * - ✅ CLI/WebAPI can control both Python and Lua
         * - ✅ Python can control Lua (via bindings in Phase 5)
         * - ✅ Lua can control Python (via bindings in Phase 5)
         * - ❌ Python cannot control itself
         * - ❌ Lua cannot control itself
         */
        class InterpreterAPI : public drogon::HttpController<InterpreterAPI>
        {
        public:
            InterpreterAPI() = default;

            METHOD_LIST_BEGIN
                // region Python Interpreter Control
                // Execute Python code
                ADD_METHOD_TO(InterpreterAPI::executePythonCode, "/api/v1/python/exec", drogon::Post);
                
                // Load and execute Python file
                ADD_METHOD_TO(InterpreterAPI::executePythonFile, "/api/v1/python/file", drogon::Post);
                
                // Get Python interpreter status
                ADD_METHOD_TO(InterpreterAPI::getPythonStatus, "/api/v1/python/status", drogon::Get);
                
                // Stop Python execution
                ADD_METHOD_TO(InterpreterAPI::stopPythonExecution, "/api/v1/python/stop", drogon::Post);
                // endregion Python Interpreter Control

                // region Lua Interpreter Control
                // Execute Lua code
                ADD_METHOD_TO(InterpreterAPI::executeLuaCode, "/api/v1/lua/exec", drogon::Post);
                
                // Load and execute Lua file
                ADD_METHOD_TO(InterpreterAPI::executeLuaFile, "/api/v1/lua/file", drogon::Post);
                
                // Get Lua interpreter status
                ADD_METHOD_TO(InterpreterAPI::getLuaStatus, "/api/v1/lua/status", drogon::Get);
                
                // Stop Lua execution (request - requires cooperative checking)
                ADD_METHOD_TO(InterpreterAPI::stopLuaExecution, "/api/v1/lua/stop", drogon::Post);
                // endregion Lua Interpreter Control
            METHOD_LIST_END

            // region Python Interpreter Methods
            /**
             * Execute Python code synchronously
             * 
             * POST /api/v1/python/exec
             * 
             * Request body: {"code": "print('hello')"}
             * 
             * Success response:
             * {
             *   "success": true,
             *   "message": "Python code executed successfully"
             * }
             * 
             * Error response:
             * {
             *   "success": false,
             *   "error": "NameError: name 'undefined_var' is not defined"
             * }
             */
            void executePythonCode(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

            /**
             * Load and execute Python file
             * 
             * POST /api/v1/python/file
             * 
             * Request body: {"path": "/absolute/path/to/script.py"}
             */
            void executePythonFile(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

            /**
             * Get Python interpreter status
             * 
             * GET /api/v1/python/status
             * 
             * Response:
             * {
             *   "available": true,
             *   "initialized": true,
             *   "status": "State: Running\nThread: Active\n..."
             * }
             */
            void getPythonStatus(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

            /**
             * Stop Python execution (send interrupt signal)
             * 
             * POST /api/v1/python/stop
             * 
             * Response:
             * {
             *   "success": true,
             *   "message": "Python execution interrupt signal sent"
             * }
             */
            void stopPythonExecution(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
            // endregion Python Interpreter Methods

            // region Lua Interpreter Methods
            /**
             * Execute Lua code synchronously
             * 
             * POST /api/v1/lua/exec
             * 
             * Request body: {"code": "print('hello')"}
             */
            void executeLuaCode(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

            /**
             * Load and execute Lua file
             * 
             * POST /api/v1/lua/file
             * 
             * Request body: {"path": "/absolute/path/to/script.lua"}
             */
            void executeLuaFile(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

            /**
             * Get Lua interpreter status
             * 
             * GET /api/v1/lua/status
             * 
             * Response:
             * {
             *   "available": true,
             *   "initialized": true,
             *   "status": "State: Running\nThread: Active\n..."
             * }
             */
            void getLuaStatus(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

            /**
             * Stop Lua execution (request stop - cooperative)
             * 
             * POST /api/v1/lua/stop
             * 
             * Note: Lua doesn't have async exception mechanism.
             * Scripts must cooperatively check for stop signals.
             */
            void stopLuaExecution(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;
            // endregion Lua Interpreter Methods
        };
    } // namespace v1
} // namespace api
