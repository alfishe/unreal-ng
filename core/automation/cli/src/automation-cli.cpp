#include "automation-cli.h"

#include <stdio.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Use the platform-agnostic functions from platform_sockets.h

using namespace std::string_literals;

AutomationCLI::AutomationCLI()
    : _emulator(nullptr), _thread(nullptr), _stopThread(false), _port(8765), _serverSocket(INVALID_SOCKET)
{
    // Initialize platform sockets
    if (!initializeSockets())
    {
        std::cerr << "Failed to initialize socket library!" << std::endl;
    }

    _cliApp.footer("\nType 'help' for a list of commands or 'exit' to quit.");
    _cliApp.set_help_flag("-h,--help", "Show help and exit");

    // Initialize the EmulatorManager during construction
    EmulatorManager* emulatorManager = EmulatorManager::GetInstance();
    if (emulatorManager)
    {
        // Force a refresh of emulator instances
        auto mostRecent = emulatorManager->GetMostRecentEmulator();

        // Get all emulator IDs
        auto emulatorIds = emulatorManager->GetEmulatorIds();
    }
    else
    {
        std::cerr << "Failed to initialize EmulatorManager!" << std::endl;
    }
}

AutomationCLI::~AutomationCLI()
{
    // Destructors must not throw - wrap everything in try-catch
    try
    {
        stop();
        cleanupSockets();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception in AutomationCLI destructor: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Unknown exception in AutomationCLI destructor" << std::endl;
    }
}

bool AutomationCLI::start(uint16_t port)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (_thread)
    {
        return true;  // Already running
    }

    _port = port;
    _stopThread = false;
    _serverSocket = INVALID_SOCKET;  // Ensure socket is reset

    try
    {
        std::cout << "Starting CLI server on port " << _port << "..." << std::endl;
        _thread = std::make_unique<std::thread>(&AutomationCLI::run, this);
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to start CLI server: " << e.what() << std::endl;
        _stopThread = true;
        return false;
    }
}

void AutomationCLI::stop()
{
    // Guard: If thread is already stopped/nullptr, nothing to do.
    // This prevents double-stop race conditions when stop() is called
    // from both Automation::stop() and destructors.
    if (!_thread)
    {
        return;
    }

    std::cout << "Stopping CLI server..." << std::endl;
    std::unique_ptr<std::thread> threadToJoin;

    // First, set the stop flag to prevent new connections
    _stopThread = true;

    // Close the server socket to unblock accept()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_serverSocket != INVALID_SOCKET)
        {
            std::cout << "Closing server socket..." << std::endl;
            close(_serverSocket);
            _serverSocket = INVALID_SOCKET;
        }

        // Move the thread pointer out so we can join it outside the lock
        threadToJoin = std::move(_thread);
    }

    // Close all active client connections to unblock any threads waiting on recv()
    std::vector<SOCKET> clientSocketsCopy;
    {
        std::lock_guard<std::mutex> lock(_clientSocketsMutex);
        clientSocketsCopy = _activeClientSockets;
        _activeClientSockets.clear();
    }

    for (SOCKET clientSocket : clientSocketsCopy)
    {
        if (clientSocket != INVALID_SOCKET)
        {
            try
            {
                std::cout << "Closing client socket: " << clientSocket << std::endl;
                // Send a message to the client before closing
                std::ostringstream shutdownMsg;
                shutdownMsg << NEWLINE << "Server is shutting down. Goodbye!" << NEWLINE;
                std::string msg = shutdownMsg.str();
                send(clientSocket, msg.c_str(), msg.length(), 0);
                close(clientSocket);
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error closing client socket " << clientSocket << ": " << e.what() << std::endl;
            }
        }
    }

    // Join the thread without holding the lock
    if (threadToJoin && threadToJoin->joinable())
    {
        try
        {
            std::cout << "Joining CLI thread..." << std::endl;
            threadToJoin->join();
            std::cout << "CLI thread joined successfully" << std::endl;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error joining CLI thread: " << e.what() << std::endl;
        }
    }

    // Final cleanup of any remaining client sockets
    {
        std::lock_guard<std::mutex> lock(_clientSocketsMutex);
        for (int clientSocket : _activeClientSockets)
        {
            if (clientSocket != INVALID_SOCKET)
            {
                close(clientSocket);
            }
        }
        _activeClientSockets.clear();
    }

    // Detach client handler threads instead of joining
    // If threads are stuck waiting for resources, join() would cause deadlock
    // After setting _stopThread and closing sockets, threads will exit naturally
    {
        std::lock_guard<std::mutex> lock(_clientThreadsMutex);
        for (auto& thread : _clientThreads)
        {
            if (thread.joinable())
            {
                std::cout << "Detaching client handler thread..." << std::endl;
                thread.detach();  // Let thread exit naturally, OS will clean up on process exit
            }
        }
        _clientThreads.clear();
    }

    std::cout << "CLI server stopped" << std::endl;
}

void AutomationCLI::run()
{
    std::cout << "CLI server thread starting..." << std::endl;

    // Initialize socket
    _serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (_serverSocket == INVALID_SOCKET)
    {
        std::cerr << "Failed to create socket: " << strerror(getLastSocketError()) << std::endl;
        return;
    }

    try
    {
        // Set SO_REUSEADDR to allow reuse of local addresses
        int opt = 1;
        if (setsockopt(_serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) < 0)
        {
            throw std::runtime_error("setsockopt(SO_REUSEADDR) failed: " + std::string(strerror(getLastSocketError())));
        }

        // Set socket to non-blocking mode using our helper function
        if (!setSocketNonBlocking(_serverSocket))
        {
            throw std::runtime_error("Failed to set non-blocking mode: " + std::string(strerror(getLastSocketError())));
        }

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        serverAddr.sin_port = htons(_port);

        if (bind(_serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
        {
            throw std::runtime_error("Failed to bind to port " + std::to_string(_port) + ": " +
                                     strerror(getLastSocketError()));
        }

        if (listen(_serverSocket, 5) == SOCKET_ERROR)
        {
            throw std::runtime_error("Failed to listen on socket: " + std::string(strerror(getLastSocketError())));
        }

        std::cout << "CLI server started on port " << _port << std::endl;

        // Main server loop
        while (!_stopThread)
        {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(_serverSocket, &readfds);

            // Set timeout to 100ms to allow checking _stopThread
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000;  // 100ms

            int activity = select(_serverSocket + 1, &readfds, nullptr, nullptr, &timeout);

            if (activity < 0 && errno != EINTR)
            {
                if (errno == EBADF)
                {
                    std::cerr << "Select error: Bad file descriptor (socket may be closed)" << std::endl;
                }
                else
                {
                    std::cerr << "select() error: " << strerror(errno) << std::endl;
                }
                break;
            }

            // Check if there's an incoming connection
            if (activity > 0 && FD_ISSET(_serverSocket, &readfds))
            {
                sockaddr_in clientAddr{};
                socklen_t clientAddrLen = sizeof(clientAddr);
                SOCKET clientSocket = accept(_serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);

                if (clientSocket != INVALID_SOCKET)
                {
                    // Add to active client sockets
                    {
                        std::lock_guard<std::mutex> lock(_clientSocketsMutex);
                        _activeClientSockets.push_back(clientSocket);
                    }

                    // Handle client in a separate thread
                    // CRITICAL: Store thread in vector for proper cleanup (don't detach!)
                    // Detached threads can be killed mid-execution on exit, causing pthread crashes
                    {
                        std::lock_guard<std::mutex> lock(_clientThreadsMutex);
                        _clientThreads.emplace_back(&AutomationCLI::handleClientConnection, this, clientSocket);
                    }
                }
                else if (!_stopThread)
                {
                    std::cerr << "accept() failed: " << strerror(getLastSocketError()) << std::endl;
                }
            }

            // Check for any disconnected clients and remove them
            // CRITICAL: Check _stopThread BEFORE iterating sockets to avoid race with stop()
            // If stop() is called, it closes all sockets. We must NOT access them after that.
            if (!_stopThread)
            {
                std::lock_guard<std::mutex> lock(_clientSocketsMutex);
                auto it = std::remove_if(_activeClientSockets.begin(), _activeClientSockets.end(), [](int sock) {
                    // First check if socket is still valid (not already closed)
                    if (sock == INVALID_SOCKET)
                    {
                        return true;  // Remove invalid sockets
                    }

                    // CRITICAL: Check if socket FD is within valid range for fd_set
                    // FD_SETSIZE is typically 1024 on Linux. Using FD_SET with a larger
                    // value triggers __fdelt_chk abort. This can happen during shutdown
                    // when many file descriptors are open.
                    if (sock >= FD_SETSIZE)
                    {
                        // Socket FD is too large for select(), treat as potentially invalid
                        // and remove it from tracking. The socket will be closed elsewhere.
                        return true;
                    }

                    fd_set set;
                    FD_ZERO(&set);
                    FD_SET(sock, &set);
                    timeval tv = {0, 0};

                    // Use select to check if socket is readable
                    int selectResult = select(sock + 1, &set, NULL, NULL, &tv);
                    if (selectResult < 0)
                    {
                        // select() failed - socket is likely closed or invalid
                        return true;
                    }

                    if (selectResult == 1 && FD_ISSET(sock, &set))
                    {
                        // Socket is readable, check if it's closed by attempting to peek
                        char dummy;
                        int recvResult = recv(sock, &dummy, 1, MSG_PEEK);
                        return recvResult == 0;  // recv returns 0 when connection is closed
                    }

                    return false;  // Socket appears to be still connected
                });

                if (it != _activeClientSockets.end())
                {
                    for (auto i = it; i != _activeClientSockets.end(); ++i)
                    {
                        if (*i != INVALID_SOCKET)
                        {
                            close(*i);
                        }
                    }

                    _activeClientSockets.erase(it, _activeClientSockets.end());
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error in CLI server: " << e.what() << std::endl;
    }

    // Clean up server socket
    if (_serverSocket != INVALID_SOCKET)
    {
        close(_serverSocket);
        _serverSocket = INVALID_SOCKET;
    }

    std::cout << "CLI server stopped" << std::endl;
}

std::unique_ptr<CLIProcessor> AutomationCLI::createProcessor()
{
    auto processor = std::make_unique<CLIProcessor>();
    processor->SetEmulator(_emulator);
    return processor;
}

void AutomationCLI::handleClientConnection(SOCKET clientSocket)
{
    // Buffer to store the current line of input
    std::string lineBuffer;
    const std::string prompt = "> ";

    // Set the socket to non-blocking mode
    if (!setSocketNonBlocking(clientSocket))
    {
        std::cerr << "Failed to set client socket to non-blocking mode" << std::endl;
        close(clientSocket);
        return;
    }

    // Add this client socket to the active connections list
    {
        std::lock_guard<std::mutex> lock(_clientSocketsMutex);
        _activeClientSockets.push_back(clientSocket);
    }

    // Configure telnet client for character-at-a-time mode with local echo
    unsigned char telnetInit[] = {
        0xFF, 0xFB, 0x01,  // IAC WILL ECHO (we'll do the echoing)
        0xFF, 0xFD, 0x01,  // IAC DO ECHO (let client echo)
        0xFF, 0xFB, 0x03,  // IAC WILL SUPPRESS GO AHEAD
        0xFF, 0xFD, 0x03,  // IAC DO SUPPRESS GO AHEAD
        0xFF, 0xFC, 0x22   // IAC WON'T LINEMODE (reject line mode)
    };
    send(clientSocket, (const char*)telnetInit, sizeof(telnetInit), 0);

    // Create a new session for this client
    ClientSession session(clientSocket);

    // Create a CLI processor for this client
    auto processor = createProcessor();

    // Force initialization of the EmulatorManager
    auto* emulatorManager = EmulatorManager::GetInstance();
    if (emulatorManager)
    {
        // Force a refresh of emulator instances
        auto mostRecent = emulatorManager->GetMostRecentEmulator();
        auto emulatorIds = emulatorManager->GetEmulatorIds();

        // Initialize the processor
        processor->InitializeProcessor();
    }

    // Send welcome message with prompt
    SendResponse(clientSocket, "Welcome to the Unreal Emulator CLI. Type 'help' for commands.", true);

    unsigned char buffer[1024];
    ssize_t bytesRead;

    while (!_stopThread)
    {
        // Clear buffer before reading
        memset(buffer, 0, sizeof(buffer));

        // Read command with timeout
        fd_set readSet;
        FD_ZERO(&readSet);

        // SAFETY: Check if socket FD is within valid range for fd_set
        // This is critical on Windows where FD_SETSIZE is only 64
        if (clientSocket >= FD_SETSIZE)
        {
            std::cerr << "Client socket FD " << clientSocket << " exceeds FD_SETSIZE (" << FD_SETSIZE
                      << "), closing connection" << std::endl;
            break;  // Exit loop and close connection gracefully
        }

        FD_SET(clientSocket, &readSet);

        struct timeval timeout;
        timeout.tv_sec = 0;        // 0 seconds
        timeout.tv_usec = 100000;  // 100ms timeout

        int selectResult = select(clientSocket + 1, &readSet, nullptr, nullptr, &timeout);

        if (selectResult == SOCKET_ERROR)
        {
            if (errno == EINTR)
                continue;  // Interrupted by signal
            if (errno == EBADF)
            {
                std::cerr << "Select error: Bad file descriptor (client socket closed)" << std::endl;
            }
            else
            {
                std::cerr << "Select error: " << strerror(getLastSocketError()) << std::endl;
            }
            break;
        }

        if (selectResult == 0)
        {
            // Timeout, check if we should continue
            continue;
        }

        // Data is available, read it
        bytesRead = recv(clientSocket, (char*)buffer, sizeof(buffer) - 1, 0);

        if (bytesRead <= 0)
        {
            if (bytesRead == 0)
            {
                std::cout << "Client closed connection" << std::endl;
                break;
            }
            else if (bytesRead == SOCKET_ERROR)
            {
                int err = getLastSocketError();
                if (err == EWOULDBLOCK || err == EAGAIN)
                {
                    // No data available yet, continue
                    continue;
                }
                std::cerr << "Recv error: " << strerror(err) << std::endl;
                break;
            }
            continue;
        }

        // Process received data
        for (int i = 0; i < bytesRead; i++)
        {
            unsigned char c = buffer[i];

            // Handle telnet commands (IAC = 0xFF)
            if (c == 0xFF && i + 1 < bytesRead)
            {
                // Skip telnet command (3 bytes: IAC VERB OPT)
                i += 2;
                continue;
            }

            // Handle backspace/delete
            if (c == 0x7F || c == 0x08)  // DEL or Backspace
            {
                if (!lineBuffer.empty())
                {
                    lineBuffer.pop_back();
                    const char* backspace = "\b \b";
                    send(clientSocket, backspace, 3, 0);
                }
                continue;
            }

            // Handle newline/return
            if (c == '\r' || c == '\n')
            {
                // Skip the matching \n if we got \r\n
                if (c == '\r' && i + 1 < bytesRead && buffer[i + 1] == '\n')
                {
                    i++;
                }

                // Echo the newline
                send(clientSocket, NEWLINE, strlen(NEWLINE), 0);

                if (!lineBuffer.empty())
                {
                    try
                    {
                        // Process the command
                        processor->ProcessCommand(session, lineBuffer);
                        lineBuffer.clear();

                        // Check if session should be closed (e.g., exit command)
                        if (session.ShouldClose())
                        {
                            std::cout << "CLI session marked for closure by command" << std::endl;
                            break;  // Exit the processing loop to close the connection
                        }

                        // Send newline and prompt for next command
                        std::string promptStr = std::string(NEWLINE) + "> ";
                        send(clientSocket, promptStr.c_str(), promptStr.length(), 0);
                    }
                    catch (const std::exception& e)
                    {
                        std::string errorMsg = "Error: " + std::string(e.what()) + NEWLINE;
                        send(clientSocket, errorMsg.c_str(), errorMsg.length(), 0);

                        // Send newline and prompt even after error
                        std::string promptStr = std::string(NEWLINE) + "> ";
                        send(clientSocket, promptStr.c_str(), promptStr.length(), 0);
                    }
                }
                else
                {
                    // Empty line, just send prompt again (no extra newline needed, already echoed)
                    send(clientSocket, "> ", 2, 0);
                }
                continue;
            }

            // Handle regular printable characters
            if (c >= 32 && c <= 126)  // Printable ASCII
            {
                lineBuffer += static_cast<char>(c);
                // Echo the character back
                char ch = static_cast<char>(c);
                send(clientSocket, &ch, 1, 0);
            }
        }

        // Check if session should be closed (e.g., exit command was processed)
        if (session.ShouldClose())
        {
            std::cout << "Closing session as requested by command" << std::endl;
            break;
        }

        // Check if socket was closed by the command handler (e.g., exit/quit)
        int error = 0;
        socklen_t len = sizeof(error);
#ifdef _WIN32
        int retval = getsockopt(clientSocket, SOL_SOCKET, SO_ERROR, (char*)&error, &len);
#else
        int retval = getsockopt(clientSocket, SOL_SOCKET, SO_ERROR, &error, &len);
#endif
        if (retval != 0 || error != 0)
        {
            std::cout << "Socket error detected, closing connection" << std::endl;
            break;
        }
    }

    // Remove socket from active sockets list before closing
    {
        std::lock_guard<std::mutex> lock(_clientSocketsMutex);
        auto it = std::find(_activeClientSockets.begin(), _activeClientSockets.end(), clientSocket);
        if (it != _activeClientSockets.end())
        {
            _activeClientSockets.erase(it);
        }
    }

    // Close the client socket
    close(clientSocket);
    std::cout << "Client connection closed" << std::endl;
}

void AutomationCLI::SendResponse(SOCKET clientSocket, const std::string& message, bool prompt,
                                 const std::string& reason) const
{
    if (clientSocket == INVALID_SOCKET)
        return;

    std::string output;

    if (prompt)
    {
        // Add a newline before the prompt
        output += NEWLINE;

        // Add the message if provided
        if (!message.empty())
        {
            output += message + NEWLINE;
        }

        // Add the prompt
        output += "> ";

        // Add the reason if provided
        if (!reason.empty())
        {
            output += " (" + reason + ")";
        }

        // Clear to end of line
        output += "\x1B[K";
    }
    else
    {
        // Just send the message as-is
        output = message;
    }

    // Send the complete output
    send(clientSocket, output.c_str(), output.length(), 0);

// Flush the socket to ensure the output is sent immediately
#ifdef _WIN32
    send(clientSocket, "", 0, 0);
#else
    send(clientSocket, "", 0, MSG_NOSIGNAL);
#endif
}
