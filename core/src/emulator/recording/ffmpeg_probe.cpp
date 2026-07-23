#include "ffmpeg_probe.h"
#include "../../common/subprocess.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <mutex>
#include <sstream>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

// ============================================================================
// Private helpers
// ============================================================================

#ifdef _WIN32
/// Search for a binary in winget package directories (they have version numbers in path)
/// @param binaryName e.g. "ffmpeg.exe" or "ffprobe.exe"
static std::string searchWingetPackages(const std::string& binaryName)
{
    char localAppData[MAX_PATH];
    if (GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH) == 0)
        return {};

    // Winget installs packages under %LOCALAPPDATA%/Microsoft/WinGet/Packages
    // FFmpeg packages have names like "Gyan.FFmpeg_*" with version subdirs
    std::string packagesDir = std::string(localAppData) + "\\Microsoft\\WinGet\\Packages";

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA((packagesDir + "\\*FFmpeg*").c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE)
        return {};

    std::string result;
    do
    {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;

        std::string packageDir = packagesDir + "\\" + findData.cFileName;

        // Search for bin subdirectories containing the binary
        WIN32_FIND_DATAA subFind;
        HANDLE hSubFind = FindFirstFileA((packageDir + "\\*").c_str(), &subFind);
        if (hSubFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(subFind.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    continue;
                if (subFind.cFileName[0] == '.')
                    continue;

                std::string binPath = packageDir + "\\" + subFind.cFileName + "\\bin\\" + binaryName;
                DWORD attr = GetFileAttributesA(binPath.c_str());
                if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
                {
                    result = binPath;
                    break;
                }
            } while (FindNextFileA(hSubFind, &subFind));
            FindClose(hSubFind);
        }

        if (!result.empty())
            break;
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
    return result;
}
#endif

std::vector<std::string> FFmpegProbe::getDefaultSearchPaths()
{
    std::vector<std::string> paths;

#ifdef __APPLE__
    paths = {
        "/opt/homebrew/bin",
        "/usr/local/bin",
        "/opt/local/bin",
        "/usr/bin"
    };
#elif defined(__linux__)
    paths = {
        "/usr/local/bin",
        "/usr/bin",
        "/snap/bin"
    };
#elif defined(_WIN32)
    // Windows: check common install locations
    char envBuf[MAX_PATH];

    // Program Files locations
    if (GetEnvironmentVariableA("PROGRAMFILES", envBuf, MAX_PATH) > 0)
    {
        paths.push_back(std::string(envBuf) + "\\ffmpeg\\bin");
        paths.push_back(std::string(envBuf) + "\\ffmpeg");
    }

    // Program Files (x86) for 32-bit ffmpeg on 64-bit Windows
    if (GetEnvironmentVariableA("PROGRAMFILES(X86)", envBuf, MAX_PATH) > 0)
    {
        paths.push_back(std::string(envBuf) + "\\ffmpeg\\bin");
        paths.push_back(std::string(envBuf) + "\\ffmpeg");
    }

    // MSYS2/MinGW paths (installed via pacman -S mingw-w64-x86_64-ffmpeg)
    paths.push_back("C:\\msys64\\mingw64\\bin");
    paths.push_back("C:\\msys64\\mingw32\\bin");
    paths.push_back("C:\\msys64\\usr\\bin");
    paths.push_back("C:\\msys64\\clang64\\bin");
    paths.push_back("C:\\msys64\\ucrt64\\bin");

    // Alternative MSYS2 install locations
    paths.push_back("C:\\msys2\\mingw64\\bin");
    paths.push_back("C:\\msys2\\mingw32\\bin");

    // Chocolatey default install path
    if (GetEnvironmentVariableA("ChocolateyInstall", envBuf, MAX_PATH) > 0)
    {
        paths.push_back(std::string(envBuf) + "\\bin");
    }
    paths.push_back("C:\\ProgramData\\chocolatey\\bin");

    // Scoop default install path
    if (GetEnvironmentVariableA("USERPROFILE", envBuf, MAX_PATH) > 0)
    {
        paths.push_back(std::string(envBuf) + "\\scoop\\shims");
        paths.push_back(std::string(envBuf) + "\\scoop\\apps\\ffmpeg\\current\\bin");
    }

    // Common standalone downloads
    paths.push_back("C:\\ffmpeg\\bin");
    paths.push_back("C:\\ffmpeg");

    // Current directory as last resort
    paths.push_back(".");
#endif

    return paths;
}

bool FFmpegProbe::isExecutable(const std::string& path)
{
    if (path.empty())
        return false;

#ifndef _WIN32
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    return S_ISREG(st.st_mode) && (st.st_mode & 0111) != 0;
#else
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#endif
}

std::string FFmpegProbe::searchPath(const std::string& binaryName)
{
    // Search PATH environment variable
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv)
        return {};

    std::string pathStr(pathEnv);
    std::stringstream ss(pathStr);
    std::string dir;

#ifndef _WIN32
    const char sep = ':';
    const std::string ext = "";
#else
    const char sep = ';';
    const std::string ext = ".exe";
#endif

    while (std::getline(ss, dir, sep))
    {
        if (dir.empty())
            continue;

        std::string fullPath = dir;
        if (fullPath.back() != '/' && fullPath.back() != '\\')
            fullPath += '/';
        fullPath += binaryName + ext;

        if (isExecutable(fullPath))
            return fullPath;
    }

    return {};
}

std::string FFmpegProbe::runCommand(const std::string& command, const std::vector<std::string>& args, int timeoutMs)
{
    Subprocess proc;
    if (!proc.spawn(command, args))
        return {};

    // Close stdin immediately (we're just reading output)
    proc.closeStdin();

    // Wait for completion
    if (proc.waitForFinished(timeoutMs) < 0)
    {
        proc.terminate();
        return {};
    }

    // ffmpeg splits its output: the banner/version goes to stderr, while
    // list queries (-encoders, -formats, ...) go to stdout. Return both.
    return proc.readAllStdout() + proc.readAllStderr();
}

// ============================================================================
// Public API
// ============================================================================

std::string FFmpegProbe::findFFmpeg()
{
#ifndef _WIN32
    const std::string binaryName = "ffmpeg";
#else
    const std::string binaryName = "ffmpeg.exe";
#endif

    // 1. Search PATH first
    std::string found = searchPath(binaryName);
    if (!found.empty())
        return found;

    // 2. Search platform-specific default paths
    auto paths = getDefaultSearchPaths();
    for (const auto& dir : paths)
    {
        std::string fullPath = dir;
        if (!fullPath.empty() && fullPath.back() != '/' && fullPath.back() != '\\')
            fullPath += '/';
        fullPath += binaryName;

        if (isExecutable(fullPath))
            return fullPath;
    }

#ifdef _WIN32
    // 3. Windows: search winget package directories (version numbers in path)
    found = searchWingetPackages(binaryName);
    if (!found.empty())
        return found;
#endif

    return {};
}

std::string FFmpegProbe::findFFprobe()
{
#ifndef _WIN32
    const std::string binaryName = "ffprobe";
#else
    const std::string binaryName = "ffprobe.exe";
#endif

    // 1. Search PATH first
    std::string found = searchPath(binaryName);
    if (!found.empty())
        return found;

    // 2. Search platform-specific default paths
    auto paths = getDefaultSearchPaths();
    for (const auto& dir : paths)
    {
        std::string fullPath = dir;
        if (!fullPath.empty() && fullPath.back() != '/' && fullPath.back() != '\\')
            fullPath += '/';
        fullPath += binaryName;

        if (isExecutable(fullPath))
            return fullPath;
    }

#ifdef _WIN32
    // 3. Windows: search winget package directories (version numbers in path)
    found = searchWingetPackages(binaryName);
    if (!found.empty())
        return found;
#endif

    return {};
}

bool FFmpegProbe::isAvailable(const std::string& path)
{
    std::string ffmpegPath = path.empty() ? findFFmpeg() : path;
    if (ffmpegPath.empty())
        return false;

    // Verify it's actually ffmpeg by checking version output
    std::string output = runCommand(ffmpegPath, {"-version"}, 3000);
    return output.find("ffmpeg") != std::string::npos;
}

std::string FFmpegProbe::getVersion(const std::string& path)
{
    std::string ffmpegPath = path.empty() ? findFFmpeg() : path;
    if (ffmpegPath.empty())
        return "Not found";

    std::string output = runCommand(ffmpegPath, {"-version"}, 3000);

    // Extract first line (version line)
    size_t newlinePos = output.find('\n');
    if (newlinePos != std::string::npos)
        return output.substr(0, newlinePos);

    return output;
}

FFmpegProbe::HWAccelInfo FFmpegProbe::detectHWAccel(const std::string& ffmpegPath)
{
    HWAccelInfo info;

    std::string path = ffmpegPath.empty() ? findFFmpeg() : ffmpegPath;
    if (path.empty())
        return info;

    // Query available encoders
    auto encoders = getAvailableEncoders(path);

    for (const auto& encoder : encoders)
    {
        if (encoder.find("videotoolbox") != std::string::npos)
            info.videoToolbox = true;
        else if (encoder.find("nvenc") != std::string::npos)
            info.nvenc = true;
        else if (encoder.find("qsv") != std::string::npos)
            info.qsv = true;
        else if (encoder.find("vaapi") != std::string::npos)
            info.vaapi = true;
        else if (encoder.find("amf") != std::string::npos)
            info.amf = true;
    }

    return info;
}

std::vector<std::string> FFmpegProbe::getAvailableEncoders(const std::string& ffmpegPath)
{
    std::vector<std::string> encoders;

    std::string path = ffmpegPath.empty() ? findFFmpeg() : ffmpegPath;
    if (path.empty())
        return encoders;

    // Cache per binary path — availability checks are called repeatedly and
    // each probe spawns an ffmpeg process
    static std::mutex cacheMutex;
    static std::map<std::string, std::vector<std::string>> cache;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = cache.find(path);
        if (it != cache.end())
            return it->second;
    }

    std::string output = runCommand(path, {"-encoders"}, 5000);

    // Parse encoder list
    // Format: " V..... h264_videotoolbox  VideoToolbox H.264 encoder"
    //         " A..... aac  AAC (Advanced Audio Coding)"
    std::stringstream ss(output);
    std::string line;

    while (std::getline(ss, line))
    {
        // Skip header lines
        if (line.empty() || (line[0] == ' ' && line.find("-----") != std::string::npos))
            continue;

        // Find encoder name after the flags (6 chars: " V.... ")
        // The format is: "FLAGS encoder_name description"
        if (line.size() > 8 && line[0] == ' ')
        {
            // Find the encoder name
            size_t nameStart = 8; // After " V.... "
            size_t nameEnd = line.find(' ', nameStart);

            if (nameEnd != std::string::npos)
            {
                std::string name = line.substr(nameStart, nameEnd - nameStart);
                // Trim whitespace
                name.erase(name.find_last_not_of(" \t\r\n") + 1);

                if (!name.empty())
                    encoders.push_back(name);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cache[path] = encoders;
    }

    return encoders;
}

bool FFmpegProbe::isEncoderAvailable(const std::string& encoder, const std::string& ffmpegPath)
{
    auto encoders = getAvailableEncoders(ffmpegPath.empty() ? findFFmpeg() : ffmpegPath);

    return std::find(encoders.begin(), encoders.end(), encoder) != encoders.end();
}

std::string FFmpegProbe::getBestHWEncoder(const std::string& ffmpegPath)
{
    HWAccelInfo hw = detectHWAccel(ffmpegPath);

#ifdef __APPLE__
    if (hw.videoToolbox)
        return "h264_videotoolbox";
#elif defined(_WIN32)
    if (hw.nvenc)
        return "h264_nvenc";
    if (hw.qsv)
        return "h264_qsv";
    if (hw.amf)
        return "h264_amf";
#elif defined(__linux__)
    if (hw.vaapi)
        return "h264_vaapi";
    if (hw.nvenc)
        return "h264_nvenc";
#endif

    // No hardware encoder available
    return {};
}

std::string FFmpegProbe::getCapabilityReport(const std::string& ffmpegPath)
{
    std::string path = ffmpegPath.empty() ? findFFmpeg() : ffmpegPath;

    std::stringstream report;

    if (path.empty())
    {
        report << "FFmpeg: NOT FOUND\n";
        report << "Install ffmpeg or set path manually.\n";
        report << "  macOS:   brew install ffmpeg\n";
        report << "  Ubuntu:  sudo apt install ffmpeg\n";
        report << "  Windows: winget install ffmpeg\n";
        report << "           or: choco install ffmpeg\n";
        report << "           or: pacman -S mingw-w64-x86_64-ffmpeg (MSYS2)\n";
        report << "           or: https://ffmpeg.org/download.html\n";
        return report.str();
    }

    report << "FFmpeg: " << getVersion(path) << "\n";
    report << "Path: " << path << "\n";

    HWAccelInfo hw = detectHWAccel(path);
    report << "Hardware acceleration:\n";
    report << "  VideoToolbox: " << (hw.videoToolbox ? "YES" : "no") << "\n";
    report << "  NVENC:        " << (hw.nvenc ? "YES" : "no") << "\n";
    report << "  QuickSync:    " << (hw.qsv ? "YES" : "no") << "\n";
    report << "  VA-API:       " << (hw.vaapi ? "YES" : "no") << "\n";
    report << "  AMF:          " << (hw.amf ? "YES" : "no") << "\n";

    std::string bestHw = getBestHWEncoder(path);
    if (!bestHw.empty())
        report << "Best HW encoder: " << bestHw << "\n";
    else
        report << "Best HW encoder: (none — will use software)\n";

    return report.str();
}
