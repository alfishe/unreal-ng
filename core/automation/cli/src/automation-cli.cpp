#include "automation-cli.h"

#include <emulator/emulatormanager.h>
#include <stdio.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace std::string_literals;

AutomationCLI::AutomationCLI() :
    _emulator(nullptr),
    _thread(nullptr),
    _stopThread(false),
    _port(8765),
    _serverSocket(INVALID_SOCKET)
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
    stop();
    
    // Cleanup platform sockets
    cleanupSockets();
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
    std::cout << "Stopping CLI server..." << std::endl;
    std::unique_ptr<std::thread> threadToJoin;

    // First, set the stop flag to prevent new connections
    _stopThread = true;

    // Close all active client connections to unblock any threads waiting on recv()
    {
        std::lock_guard<std::mutex> lock(_clientSocketsMutex);
        for (int clientSocket : _activeClientSockets)
        {
            std::cout << "Closing client socket: " << clientSocket << std::endl;
            // Send a message to the client before closing
            std::string shutdownMsg = "\nServer is shutting down. Goodbye!\n";
            send(clientSocket, shutdownMsg.c_str(), shutdownMsg.length(), 0);
            close(clientSocket);
        }
        _activeClientSockets.clear();
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_thread)
        {
            return;  // Already stopped
        }

        // Close the server socket to unblock accept()
        if (_serverSocket != INVALID_SOCKET)
        {
            std::cout << "Closing server socket: " << _serverSocket << std::endl;
            close(_serverSocket);
            _serverSocket = INVALID_SOCKET;
        }

        // Move the thread pointer out so we can join it outside the lock
        threadToJoin = std::move(_thread);
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

    std::cout << "CLI server stopped" << std::endl;
}

void AutomationCLI::run()
{
    // Initialize socket
    _serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (_serverSocket == INVALID_SOCKET)
    {
        std::cerr << "Failed to create socket: " << strerror(getLastSocketError()) << std::endl;
        return;
    }

    // Set SO_REUSEADDR to allow reuse of local addresses
    int opt = 1;
    if (setsockopt(_serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) < 0)
    {
        std::cerr << "setsockopt(SO_REUSEADDR) failed: " << strerror(getLastSocketError()) << std::endl;
        close(_serverSocket);
        _serverSocket = INVALID_SOCKET;
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serverAddr.sin_port = htons(_port);

    if (bind(_serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cerr << "Failed to bind to port " << _port << ": " << strerror(getLastSocketError()) << std::endl;
        close(_serverSocket);
        _serverSocket = INVALID_SOCKET;
        return;
    }

    if (listen(_serverSocket, 5) == SOCKET_ERROR)
    {
        std::cerr << "Failed to listen on socket: " << strerror(getLastSocketError()) << std::endl;
        close(_serverSocket);
        _serverSocket = INVALID_SOCKET;
        return;
    }

    std::cout << "=== CLI Server ===" << std::endl;
    std::cout << "Status:  Listening on port " << _port << std::endl;
    std::cout << "Connect: telnet localhost " << _port << std::endl;
    std::cout << "==================" << std::endl;

    while (!_stopThread)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(_serverSocket, &readfds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int activity = select(_serverSocket + 1, &readfds, nullptr, nullptr, &tv);

        // Check if we should stop
        if (_stopThread)
        {
            break;
        }

        if (activity < 0)
        {
            int errorCode = getLastSocketError();
#ifdef _WIN32
            if (errorCode == WSAEINTR)
#else
            if (errorCode == EINTR)
#endif
            {
                // Interrupted system call, just retry
                continue;
            }
            std::cerr << "select error: " << strerror(errorCode) << std::endl;
            break;
        }

        if (activity == 0)
        {
            // Timeout, check stop flag and continue
            continue;
        }

        if (FD_ISSET(_serverSocket, &readfds))
        {
            // Accept a new client connection
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            SOCKET clientSocket = accept(_serverSocket, (struct sockaddr*)&clientAddr, &clientLen);

            if (clientSocket == INVALID_SOCKET)
            {
                std::cerr << "Failed to accept connection: " << strerror(getLastSocketError()) << std::endl;
                continue;
            }

            std::cout << "Client connected from " << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port)
                      << std::endl;

            // Handle the client connection in a separate method
            // This method will handle all command processing until the client disconnects
            handleClientConnection(clientSocket);
        }
    }

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
    std::cout << "New client connection established on socket: " << clientSocket << std::endl;

    // Add this client socket to the active connections list
    {
        std::lock_guard<std::mutex> lock(_clientSocketsMutex);
    }

    // Set socket to non-blocking mode
    setSocketNonBlocking(clientSocket);

    // Configure telnet client for character-at-a-time mode with local echo
    // This works better with Windows telnet client
    unsigned char telnetInit[] =
    {
        0xFF, 0xFD, 0x22,  // IAC DO LINEMODE (we'll reject this)
        0xFF, 0xFB, 0x01,  // IAC WILL ECHO
        0xFF, 0xFD, 0x01,  // IAC DO ECHO
        0xFF, 0xFB, 0x03,  // IAC WILL SUPPRESS GO AHEAD
        0xFF, 0xFD, 0x03,  // IAC DO SUPPRESS GO AHEAD
        0xFF, 0xFC, 0x22,  // IAC WON'T LINEMODE (reject line mode)
        0xFF, 0xFC, 0x01   // IAC WON'T ECHO (let client handle echo)
    };
    send(clientSocket, (const char*)telnetInit, sizeof(telnetInit), 0);
    
    // Send WILL SUPPRESS GO AHEAD again to ensure it's set
    unsigned char willSuppressGA[] = {0xFF, 0xFB, 0x03};
    send(clientSocket, (const char*)willSuppressGA, sizeof(willSuppressGA), 0);

    // Create a new session for this client
    ClientSession session(clientSocket);

    // Create a CLI processor for this client
    auto processor = createProcessor();

    // Force initialization of the EmulatorManager BEFORE sending welcome message
    auto* emulatorManager = EmulatorManager::GetInstance();
    if (emulatorManager)
    {
        // Force a refresh of emulator instances
        auto mostRecent = emulatorManager->GetMostRecentEmulator();

        // Get all emulator IDs
        auto emulatorIds = emulatorManager->GetEmulatorIds();

        // Initialize the processor with a dummy command but don't send it to the client
        processor->InitializeProcessor();

        // Send welcome message AFTER EmulatorManager is fully initialized
        #ifdef _WIN32
            const char* newline = "\r\n";
        #else
            const char* newline = "\n";
        #endif
        
        std::string welcomeMsg = "Welcome to the Unreal Emulator CLI. Type 'help' for commands." + std::string(newline);
        ssize_t bytesSent = send(clientSocket, welcomeMsg.c_str(), welcomeMsg.length(), 0);

        // Send initial prompt
        sendPrompt(clientSocket, "initial welcome");
    }
    else
    {
        std::cout << "Failed to initialize EmulatorManager!" << std::endl;

        // Send error message
        std::string errorMsg = "Error: Failed to initialize emulator. Please try again later.\n";
        send(clientSocket, errorMsg.c_str(), errorMsg.length(), 0);
        return;
    }

    // Set socket to non-blocking mode to avoid hanging
    setSocketNonBlocking(clientSocket);

    char buffer[1024];
    ssize_t bytesRead;

    while (true)
    {
        // Clear buffer before reading
        memset(buffer, 0, sizeof(buffer));

        // Read command with timeout
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(clientSocket, &readSet);

        struct timeval timeout;
        timeout.tv_sec = 1;  // 1 second timeout
        timeout.tv_usec = 0;

        int selectResult = select(clientSocket + 1, &readSet, NULL, NULL, &timeout);

        if (selectResult == SOCKET_ERROR)
        {
            std::cerr << "Select error: " << strerror(getLastSocketError()) << std::endl;
            break;
        }

        if (selectResult == 0)
        {
            // Timeout, no data available
            continue;
        }

        // Data is available, read it
        bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

        if (bytesRead <= 0)
        {
            if (bytesRead == 0)
            {
                std::cout << "Client closed connection" << std::endl;
            }
            else
            {
                std::cerr << "Recv error: " << strerror(getLastSocketError()) << std::endl;
            }
            break;
        }

        // Process telnet protocol commands (IAC = 0xFF)
        bool processedTelnetCommand = false;
        for (int i = 0; i < bytesRead; )
        {
            if ((unsigned char)buffer[i] == 0xFF)  // IAC
            {
                if (i + 1 >= bytesRead) break;  // Not enough data yet
                
                unsigned char verb = (unsigned char)buffer[i+1];
                
                if (verb == 0xFF)  // Literal 0xFF
                {
                    // Remove one of the 0xFFs
                    memmove(buffer + i, buffer + i + 1, bytesRead - i - 1);
                    bytesRead--;
                    i++;
                    continue;
                }
                else if (verb == 0xFD)  // DO
                {
                    // Handle DO commands
                    if (i + 2 < bytesRead)
                    {
                        unsigned char option = (unsigned char)buffer[i+2];
                        unsigned char response[3] = {0xFF, 0xFC, option}; // WON'T
                        
                        // Special handling for certain options
                        if (option == 0x01) // ECHO
                            response[1] = 0xFB; // WILL
                        else if (option == 0x03) // SUPPRESS GO AHEAD
                            response[1] = 0xFB; // WILL
                            
                        send(clientSocket, (const char*)response, 3, 0);
                    }
                    i += 3;
                    processedTelnetCommand = true;
                    continue;
                }
                else if (verb == 0xFB)  // WILL
                {
                    // Handle WILL commands
                    if (i + 2 < bytesRead)
                    {
                        unsigned char option = (unsigned char)buffer[i+2];
                        unsigned char response[3] = {0xFF, 0xFE, option}; // DON'T
                        
                        // Special handling for certain options
                        if (option == 0x01) // ECHO
                            response[1] = 0xFD; // DO
                        else if (option == 0x03) // SUPPRESS GO AHEAD
                            response[1] = 0xFD; // DO
                            
                        send(clientSocket, (const char*)response, 3, 0);
                    }
                    i += 3;
                    processedTelnetCommand = true;
                    continue;
                }
                else if (verb == 0xFE || verb == 0xFC)  // DON'T or WON'T
                {
                    // Skip these commands
                    i += 3;
                    processedTelnetCommand = true;
                    continue;
                }
                else
                {
                    // Skip unknown command
                    i += 2;
                    continue;
                }
            }
            i++;
        }

        if (processedTelnetCommand || bytesRead == 0)
        {
            // If we processed telnet commands, continue to next read
            continue;
        }

        buffer[bytesRead] = '\0';

        // Remove trailing newline if present
        if (buffer[bytesRead - 1] == '\n')
            buffer[bytesRead - 1] = '\0';
        if (bytesRead > 1 && buffer[bytesRead - 2] == '\r')
            buffer[bytesRead - 2] = '\0';

        std::string command(buffer);

        if (command.empty())
        {
            send(clientSocket, "> ", 2, 0);
            continue;
        }

        // Process the command and capture any output
        try
        {
            processor->ProcessCommand(session, command);
        }
        catch (const std::exception& e)
        {
            std::string errorMsg = "Error processing command: " + std::string(e.what()) + "\n";
            send(clientSocket, errorMsg.c_str(), errorMsg.length(), 0);

            // Send a prompt after the error message
            if (command != "exit" && command != "quit")
            {
                sendPrompt(clientSocket, "after error");
            }
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

        // Send prompt for next command
        sendPrompt(clientSocket, "next command");
    }

    // Remove this client socket from the active connections list
    {
        std::lock_guard<std::mutex> lock(_clientSocketsMutex);
        _activeClientSockets.erase(std::remove(_activeClientSockets.begin(), _activeClientSockets.end(), clientSocket),
                                   _activeClientSockets.end());
    }

    // Close the client socket
    close(clientSocket);
    std::cout << "Client connection closed" << std::endl;
}

// Send a command prompt to the client
void AutomationCLI::sendPrompt(SOCKET clientSocket, const std::string& reason)
{
    #ifdef _WIN32
        // On Windows, ensure we're at the start of the line
        std::string cr = "\r";
        send(clientSocket, cr.c_str(), cr.length(), 0);
    #endif
    
    std::string prompt = "> ";
    ssize_t promptBytes = send(clientSocket, prompt.c_str(), prompt.length(), 0);

    // Flush the output to ensure it's sent immediately
    #ifdef _WIN32
        // On Windows, we need to flush the socket buffer
        // This is a no-op on most systems but helps with Windows telnet
        send(clientSocket, "", 0, 0);
    #endif

    if (!reason.empty())
    {
        // std::cout << "Sent prompt: " << promptBytes << " bytes (" << reason << ")" << std::endl;
    }
    else
    {
        // std::cout << "Sent prompt: " << promptBytes << " bytes" << std::endl;
    }
}
