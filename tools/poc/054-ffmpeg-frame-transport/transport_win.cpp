// POC 054, Windows: how fast frames reach ffmpeg per transport.
// transport_win MODE WIDTH HEIGHT BYTES_PER_PIXEL FRAMES
//   MODE: npipe:<KB>  a named pipe ffmpeg opens as a file (FFmpegPipeEncoder's NamedPipe: 64 KB buffers)
//         stdin:<KB>  a named pipe as ffmpeg's standard input (QProcess: 1 MB buffers, the window recorder)
//         tcp         loopback, 8 MB send buffer, ffmpeg connects
//   BYTES_PER_PIXEL: 4 = bgra, 2 = nv12 (12 bits)
// Env: FFMPEG (path), POC_OUT (output side, default "-c copy -f null -"), POC_SRC=FILE:WxH (BGRA pictures, each
// scaled to the frame by FrameScaler, in turn), POC_WRITE_KB (bytes per WriteFile/send, default the whole frame).
// Build (x64 Native Tools prompt): cl /O2 /EHsc /std:c++20 transport_win.cpp ..\..\..\core\recording\src\common\framescaler.cpp ws2_32.lib
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../../../core/recording/src/common/framescaler.h"

static std::string Env(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

int main(int argc, char** argv)
{
    if (argc < 6)
    {
        std::fprintf(stderr, "transport_win MODE(npipe:KB|stdin:KB|tcp) W H BPP FRAMES\n");
        return 2;
    }
    const std::string modeArg = argv[1];
    const int w = std::atoi(argv[2]), h = std::atoi(argv[3]), bpp = std::atoi(argv[4]), frames = std::atoi(argv[5]);
    const std::string mode = modeArg.substr(0, modeArg.find(':'));
    const DWORD bufferKb = modeArg.find(':') == std::string::npos ? 64 : std::atoi(modeArg.c_str() + modeArg.find(':') + 1);
    const char* pixfmt = bpp == 4 ? "bgra" : "nv12";
    const size_t frameBytes = static_cast<size_t>(w) * h * (bpp == 4 ? 4 : 3) / (bpp == 4 ? 1 : 2);
    std::vector<uint8_t> frame(frameBytes);
    for (size_t i = 0; i < frameBytes; i++)
        frame[i] = static_cast<uint8_t>(i * 7);

    std::vector<std::vector<uint8_t>> sources;
    uint32_t srcW = 0, srcH = 0;
    const std::string spec = Env("POC_SRC", "");
    if (!spec.empty())
    {
        const size_t colon = spec.rfind(':');
        std::sscanf(spec.c_str() + colon + 1, "%ux%u", &srcW, &srcH);
        FILE* f = std::fopen(spec.substr(0, colon).c_str(), "rb");
        if (!f)
        {
            std::perror("POC_SRC");
            return 1;
        }
        std::vector<uint8_t> one(static_cast<size_t>(srcW) * srcH * 4);
        while (std::fread(one.data(), 1, one.size(), f) == one.size())
            sources.push_back(one);
        std::fclose(f);
    }
    const size_t chunk = std::stoul(Env("POC_WRITE_KB", "0")) * 1024;

    const std::string pipeName = "\\\\.\\pipe\\poc054-" + std::to_string(GetCurrentProcessId());
    HANDLE server = INVALID_HANDLE_VALUE, child = INVALID_HANDLE_VALUE;
    SOCKET listener = INVALID_SOCKET, sock = INVALID_SOCKET;
    std::string input;
    if (mode == "npipe" || mode == "stdin")
    {
        server = CreateNamedPipeA(pipeName.c_str(), PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE | PIPE_WAIT, 1,
                                  bufferKb * 1024, bufferKb * 1024, 0, nullptr);
        if (server == INVALID_HANDLE_VALUE)
        {
            std::fprintf(stderr, "CreateNamedPipe %lu\n", GetLastError());
            return 1;
        }
        if (mode == "stdin")
        {
            SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
            child = CreateFileA(pipeName.c_str(), GENERIC_READ, 0, &inherit, OPEN_EXISTING, 0, nullptr);
            input = "pipe:0";
        }
        else
            input = pipeName;
    }
    else if (mode == "tcp")
    {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(listener, 1) != 0)
        {
            std::fprintf(stderr, "tcp listen %d\n", WSAGetLastError());
            return 1;
        }
        int len = sizeof(a);
        getsockname(listener, reinterpret_cast<sockaddr*>(&a), &len);
        input = "tcp://127.0.0.1:" + std::to_string(ntohs(a.sin_port));
    }
    else
    {
        std::fprintf(stderr, "unknown mode\n");
        return 2;
    }

    const std::string ffmpeg = Env("FFMPEG", "ffmpeg");
    std::string command = "\"" + ffmpeg + "\" -v error -probesize 32 -analyzeduration 0 -f rawvideo -pix_fmt " + pixfmt +
                          " -s " + std::to_string(w) + "x" + std::to_string(h) + " -r 50 -i " + input + " " +
                          Env("POC_OUT", "-c copy -f null -");
    if (mode != "stdin")
        command.insert(command.find(" -v "), " -nostdin");
    STARTUPINFOA si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = mode == "stdin" ? child : GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi))
    {
        std::fprintf(stderr, "CreateProcess %lu\n", GetLastError());
        return 1;
    }
    if (child != INVALID_HANDLE_VALUE)
        CloseHandle(child);
    if (server != INVALID_HANDLE_VALUE && !ConnectNamedPipe(server, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED)
    {
        std::fprintf(stderr, "ConnectNamedPipe %lu\n", GetLastError());
        return 1;
    }
    if (listener != INVALID_SOCKET)
    {
        sock = accept(listener, nullptr, nullptr);
        int size = 8 << 20;
        setsockopt(sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&size), sizeof(size));
        int one = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
    }

    const auto t0 = std::chrono::steady_clock::now();
    bool ok = true;
    for (int i = 0; i < frames && ok; i++)
    {
        const uint8_t* data = frame.data();
        if (!sources.empty() && bpp == 4)
            FrameScaler::ScaleInto(sources[static_cast<size_t>(i) % sources.size()].data(), srcW, srcH, frame.data(),
                                   static_cast<size_t>(w) * 4, w, h, false);
        for (size_t at = 0; at < frameBytes && ok;)
        {
            const size_t n = std::min(frameBytes - at, chunk ? chunk : frameBytes);
            if (sock != INVALID_SOCKET)
            {
                const int sent = send(sock, reinterpret_cast<const char*>(data + at), static_cast<int>(std::min<size_t>(n, 1 << 30)), 0);
                ok = sent > 0;
                at += ok ? sent : 0;
            }
            else
            {
                DWORD written = 0;
                ok = WriteFile(server, data + at, static_cast<DWORD>(n), &written, nullptr) && written > 0;
                at += written;
            }
        }
    }
    const double sendS = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (sock != INVALID_SOCKET)
    {
        shutdown(sock, SD_SEND);
        closesocket(sock);
    }
    if (server != INVALID_HANDLE_VALUE)
    {
        FlushFileBuffers(server);
        CloseHandle(server);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    const double allS = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    std::printf("%-10s %5dx%-5d %s  %7.1f fps  %5.2f GB/s  (writes %.2f s, ffmpeg exit %lu%s)\n", modeArg.c_str(), w, h,
                pixfmt, frames / allS, frames * double(frameBytes) / allS / 1e9, sendS, code, ok ? "" : ", write failed");
    return 0;
}
