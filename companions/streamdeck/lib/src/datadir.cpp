#include "sdb/datadir.h"

#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>

#include <vector>
#else
#include <unistd.h>

#include <array>
#endif

namespace sdb::data
{

namespace fs = std::filesystem;

namespace
{
// how many folders above the executable a source tree's data/ is looked for (bin -> build dir -> repo: 2)
constexpr int kTreeLevels = 4;

std::mutex g_mutex;
std::optional<fs::path> g_root;  // looked up (or set) once
fs::path g_userRoot;             // SetUserRoot (tests); empty = the home directory's .unreal-ng

bool IsDir(const fs::path& p)
{
    std::error_code ec;
    return !p.empty() && fs::is_directory(p, ec);
}
}  // namespace

std::vector<fs::path> Candidates(const fs::path& exeDir, const std::string& env, const fs::path& sourceData)
{
    std::vector<fs::path> out;
    auto add = [&](const fs::path& p) {
        if (p.empty())
            return;
        const fs::path n = p.lexically_normal();
        for (const auto& o : out)
            if (o == n)
                return;
        out.push_back(n);
    };
    if (!env.empty())
        add(fs::path(env));
    if (!exeDir.empty())
    {
        // app bundles round the executable, innermost first: X.app/Contents/MacOS -> X.app/Contents/Resources
        fs::path outermostApp;
        for (fs::path p = exeDir; !p.empty() && p != p.root_path(); p = p.parent_path())
            if (p.filename() == "Contents" && p.parent_path().extension() == ".app")
            {
                add(p / "Resources");
                outermostApp = p.parent_path();
            }
        add(exeDir);
        if (!outermostApp.empty())
            add(outermostApp.parent_path());
        add(exeDir.parent_path() / "share" / "unreal-ng");
        fs::path p = exeDir;
        for (int i = 0; i <= kTreeLevels && !p.empty(); ++i, p = p.parent_path())
        {
            add(p / "data");
            if (p == p.root_path())
                break;
        }
    }
    add(sourceData);
    return out;
}

fs::path Find(const std::vector<fs::path>& candidates)
{
    for (const auto& c : candidates)
    {
        std::error_code ec;
        if (fs::is_directory(c / "streamdeck", ec))
            return c;
    }
    return {};
}

fs::path ExecutableDir()
{
    fs::path exe;
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (n == 0 || n == buffer.size())
        return {};
    buffer.resize(n);
    exe = fs::path(buffer);
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        return {};
    exe = fs::path(buffer.data());
#else
    std::array<char, 4096> buffer{};
    const ssize_t n = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (n <= 0)
        return {};
    exe = fs::path(std::string(buffer.data(), static_cast<size_t>(n)));
#endif
    std::error_code ec;
    const fs::path real = fs::weakly_canonical(exe, ec);  // a link in bin/ (the DEB), the DMG root's studio link
    return (ec ? exe : real).parent_path();
}

fs::path Root()
{
    std::lock_guard lock(g_mutex);
    if (!g_root)
    {
        const char* env = std::getenv(kEnv);
#ifdef SDB_SOURCE_DATA_DIR
        const fs::path source = SDB_SOURCE_DATA_DIR;
#else
        const fs::path source;
#endif
        g_root = Find(Candidates(ExecutableDir(), env ? env : "", source));
    }
    return *g_root;
}

void SetRoot(const fs::path& root)
{
    std::lock_guard lock(g_mutex);
    if (root.empty())
        g_root.reset();
    else
        g_root = root;
}

fs::path TestdataRoot()
{
    const fs::path root = Root();
    if (!root.empty() && IsDir(root.parent_path() / "testdata"))
        return (root.parent_path() / "testdata").lexically_normal();
#ifdef SDB_SOURCE_DATA_DIR
    const fs::path source = fs::path(SDB_SOURCE_DATA_DIR).parent_path() / "testdata";
    if (IsDir(source))
        return source.lexically_normal();
#endif
    return {};
}

fs::path UserRoot()
{
    {
        std::lock_guard lock(g_mutex);
        if (!g_userRoot.empty())
            return g_userRoot;
    }
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (!home || !*home)
        return {};
    return fs::path(home) / ".unreal-ng";
}

void SetUserRoot(const fs::path& root)
{
    std::lock_guard lock(g_mutex);
    g_userRoot = root;
}

const std::vector<std::string>& PathRoots()
{
    static const std::vector<std::string> roots{"data", "icons", "testdata", "user", "scenario"};
    return roots;
}

std::string RootOf(const std::string& path)
{
    if (path.size() < 2 || path[0] != '$')
        return {};
    const size_t end = path.find_first_of("/\\", 1);
    return path.substr(1, end == std::string::npos ? std::string::npos : end - 1);
}

std::string Resolve(const std::string& path, const fs::path& scenarioDir)
{
    const std::string root = RootOf(path);
    if (root.empty())
    {
        if (path.empty() || scenarioDir.empty() || path[0] == '$')
            return path;
        const fs::path p(path);
        if (p.is_absolute() || p.has_root_name() || p.has_root_directory())
            return path;
        return (scenarioDir / p).lexically_normal().generic_string();
    }
    fs::path base;
    if (root == "data")
        base = Root();
    else if (root == "icons")
        base = Root().empty() ? fs::path() : Root() / "icons";
    else if (root == "testdata")
        base = TestdataRoot();
    else if (root == "user")
        base = UserRoot();
    else if (root == "scenario")
        base = scenarioDir;
    else
        return path;  // "$database/...": not a root
    if (base.empty())
        return path;
    const std::string rest = path.size() > root.size() + 2 ? path.substr(root.size() + 2) : std::string();
    fs::path p = base;
    if (!rest.empty())
        p /= fs::path(rest);
    return p.lexically_normal().generic_string();
}

}  // namespace sdb::data
