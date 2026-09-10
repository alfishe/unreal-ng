#pragma once

// Shared fixtures for the DeZog ZEsarUX debug adapter tests (the zrcp::
// Server surface DeZog connects to).
//
// TestDezogZesaruxClient is a minimal blocking line client that talks the ZRCP text
// protocol exactly like DeZog's ZesaruxSocket: commands are '\n'-terminated
// lines and every response is consumed up to the "command...> " prompt. Like
// DeZog, the prompt must sit on a line of its own (preceded by '\n') - an
// answer is otherwise incomplete and DeZog times out.
//
// DezogZesaruxFixture boots the server on an ephemeral port next to the live
// emulator from DezogEmulatorFixture, with the production pause-notifier
// wiring (adapter stop events -> server run-waiter).

#include "../dezog/dezogtestfixture.h"

#include "platform-sockets.h"
#include "zrcpserver.h"

#include <cstring>
#include <string>
#include <vector>

class TestDezogZesaruxClient
{
public:
    ~TestDezogZesaruxClient() { disconnect(); }

    bool connect(uint16_t port)
    {
        initializeSockets();
        _sock = socket(AF_INET, SOCK_STREAM, 0);
        if (_sock == INVALID_SOCKET)
            return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (::connect(_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        {
            closeSocket(_sock);
            _sock = INVALID_SOCKET;
            return false;
        }
        return true;
    }

    void disconnect()
    {
        if (_sock != INVALID_SOCKET)
        {
            closeSocket(_sock);
            _sock = INVALID_SOCKET;
        }
    }

    // Sends one command line and returns everything the server emits before
    // the prompt (lines still '\n'-terminated, prompt stripped).
    std::string command(const std::string& line, int timeoutMs = 3000)
    {
        sendLine(line);
        return readUntilPrompt(timeoutMs);
    }

    void sendLine(const std::string& line)
    {
        const std::string data = line + "\n";
        size_t sent = 0;
        while (sent < data.size())
        {
            ssize_t n = send(_sock, data.data() + sent, data.size() - sent, 0);
            ASSERT_GT(n, 0);
            sent += static_cast<size_t>(n);
        }
    }

    // DeZog's pause(): a bare newline interrupts a running "run"
    void sendBlank() { sendLine(""); }

    // Reads exactly one '\n'-terminated line (newline stripped)
    std::string readLine(int timeoutMs = 3000)
    {
        while (true)
        {
            size_t newline = _buffer.find('\n');
            if (newline != std::string::npos)
            {
                std::string line = _buffer.substr(0, newline);
                _buffer.erase(0, newline + 1);
                return line;
            }
            char chunk[1024];
            if (!fillChunk(chunk, sizeof(chunk), timeoutMs))
                return {};
        }
    }

    // Accumulates until the stream ends with '\n' + prompt - DeZog's exact
    // acceptance rule (zesaruxsocket.ts splits the chunk on '\n' and requires
    // the prompt to be the last split line). "log> " lines are stripped from
    // the chunk BEFORE that check, exactly like DeZog (they are collected in
    // logs). Returns the content before the final newline ("" when only the
    // prompt line arrived).
    std::string readUntilPrompt(int timeoutMs = 3000, bool* timedOut = nullptr)
    {
        const std::string promptEnd = std::string("\n") + zrcp::PROMPT;
        while (true)
        {
            stripLogLines();
            if (_buffer.size() >= promptEnd.size() &&
                _buffer.compare(_buffer.size() - promptEnd.size(), promptEnd.size(), promptEnd) == 0)
                break;
            char chunk[1024];
            if (!fillChunk(chunk, sizeof(chunk), timeoutMs))
            {
                if (timedOut)
                    *timedOut = true;
                return {};
            }
        }
        _buffer.erase(_buffer.size() - promptEnd.size(), promptEnd.size());
        std::string response = _buffer;
        _buffer.clear();
        // Responses are '\n'-terminated lines followed by the prompt: strip
        // the final newline so callers compare whole lines
        if (!response.empty() && response.back() == '\n')
            response.pop_back();
        return response;
    }

    // True once the server closed the connection (EOF or reset)
    bool socketClosed()
    {
        char byte;
        const ssize_t n = recv(_sock, &byte, 1, 0);
        return n <= 0;
    }

    // "log> " lines stripped from responses (DeZog forwards them to the UI
    // instead of handing them to the command handler)
    std::vector<std::string> logs;

private:
    // DeZog's log handling: a "log> " line at a line start is removed from the
    // chunk (and would be forwarded to the UI); it does not count as answer
    // data for the prompt check.
    void stripLogLines()
    {
        size_t searchFrom = 0;
        while (true)
        {
            const size_t start = _buffer.find("log> ", searchFrom);
            if (start == std::string::npos)
                return;
            if (start > 0 && _buffer[start - 1] != '\n')
            {
                searchFrom = start + 5;
                continue;
            }
            const size_t end = _buffer.find('\n', start);
            if (end == std::string::npos)
                return;  // incomplete log line: wait for more data
            logs.push_back(_buffer.substr(start + 5, end - start - 5));
            _buffer.erase(start, end - start + 1);
            searchFrom = start;
        }
    }

    bool fillChunk(char* chunk, size_t size, int timeoutMs)
    {
        const int ready = waitForSocketRead(_sock, timeoutMs);
        if (ready <= 0)
            return false;
        const ssize_t n = recv(_sock, chunk, static_cast<int>(size), 0);
        if (n <= 0)
            return false;
        _buffer.append(chunk, chunk + n);
        return true;
    }

    SOCKET _sock = INVALID_SOCKET;
    std::string _buffer;
};

class DezogZesaruxFixture : public DezogEmulatorFixture
{
protected:
    void SetUp() override
    {
        DezogEmulatorFixture::SetUp();

        zrcp::ServerConfig config;
        config.port = 0;  // ephemeral
        config.serverName = "Unreal-NG-Test";
        _server = std::make_unique<zrcp::Server>(_adapter.get(), config);
        _adapter->setPauseNotifier([this](dzrp::BreakReason reason, uint16_t addr, uint8_t bank) {
            _server->notifyPause(reason, addr, bank);
        });
        ASSERT_TRUE(_server->start());
        ASSERT_NE(_server->getPort(), 0);

        ASSERT_TRUE(_client.connect(_server->getPort()));

        // Welcome banner + first prompt
        const std::string banner = _client.readUntilPrompt();
        ASSERT_NE(banner.find("Welcome to Unreal-NG-Test"), std::string::npos);
    }

    void TearDown() override
    {
        _client.disconnect();
        if (_adapter)
            _adapter->setPauseNotifier(nullptr);
        if (_server)
            _server->stop();
        _server.reset();
        DezogEmulatorFixture::TearDown();
    }

    // The DeZog init sequence up to (and including) enter-cpu-step
    void initSession()
    {
        EXPECT_EQ(_client.command("close-all-menus"), "");
        EXPECT_EQ(_client.command("get-version"), zrcp::SERVER_VERSION);
        EXPECT_EQ(_client.command("set-debug-settings 0"), "");
        _client.logs.clear();
        EXPECT_EQ(_client.command("hard-reset-cpu"), "");
        ASSERT_EQ(_client.logs.size(), 1u);
        EXPECT_NE(_client.logs[0].find("hard-reset-cpu"), std::string::npos);
        EXPECT_EQ(_client.command("enter-cpu-step"), "");
    }

    std::unique_ptr<zrcp::Server> _server;
    TestDezogZesaruxClient _client;
};
