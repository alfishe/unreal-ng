// POC 054: frames into ffmpeg through a FIFO / a bigger FIFO / a unix socket / TCP loopback. Prints frames per second
// ingested by `ffmpeg -f rawvideo ... -c copy -f null -`.
// transport MODE WIDTH HEIGHT BYTES_PER_PIXEL FRAMES   (MODE: fifo | fifobig | unix | tcp)
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
extern char** environ;

static bool WriteAll(int fd, const uint8_t* p, size_t n)
{
    while (n)
    {
        const ssize_t w = ::write(fd, p, n);
        if (w < 0)
        {
            if (errno == EINTR) continue;
            return false;
        }
        p += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

static void BigBuffers(int s)
{
    // Ask for 8 MB and keep what the kernel grants (macOS kern.ipc.maxsockbuf, Linux net.core.wmem_max)
    for (int size = 8 << 20; size >= 64 << 10; size /= 2)
        if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) == 0)
            break;
}

int main(int argc, char** argv)
{
    if (argc < 6) { std::fprintf(stderr, "transport MODE W H BPP FRAMES\n"); return 2; }
    const std::string mode = argv[1];
    const int w = std::atoi(argv[2]), h = std::atoi(argv[3]), bpp = std::atoi(argv[4]), frames = std::atoi(argv[5]);
    const char* pixfmt = bpp == 4 ? "rgba" : "nv12";
    const size_t frameBytes = static_cast<size_t>(w) * h * (bpp == 4 ? 4 : 3) / (bpp == 4 ? 1 : 2);
    std::vector<uint8_t> frame(frameBytes);
    for (size_t i = 0; i < frameBytes; i++) frame[i] = static_cast<uint8_t>(i * 7);
    signal(SIGPIPE, SIG_IGN);

    // Relative to the working directory (run from the repo scratch/): a unix socket path must stay short
    const std::string base = "poc054-" + std::to_string(getpid());
    std::string url;
    int listenFd = -1;
    if (mode == "fifo" || mode == "fifobig")
    {
        ::unlink(base.c_str());
        if (mkfifo(base.c_str(), 0600) != 0) { perror("mkfifo"); return 1; }
        url = base;
    }
    else if (mode == "unix")
    {
        listenFd = socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un a{};
        a.sun_family = AF_UNIX;
        std::snprintf(a.sun_path, sizeof(a.sun_path), "%s.sock", base.c_str());
        ::unlink(a.sun_path);
        if (bind(listenFd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(listenFd, 1) != 0) { perror("unix"); return 1; }
        url = std::string("unix:") + a.sun_path;
    }
    else if (mode == "tcp")
    {
        listenFd = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (bind(listenFd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(listenFd, 1) != 0) { perror("tcp"); return 1; }
        socklen_t len = sizeof(a);
        getsockname(listenFd, reinterpret_cast<sockaddr*>(&a), &len);
        url = "tcp://127.0.0.1:" + std::to_string(ntohs(a.sin_port));
    }
    else { std::fprintf(stderr, "unknown mode\n"); return 2; }

    const std::string size = std::to_string(w) + "x" + std::to_string(h);
    const char* args[] = {"ffmpeg", "-nostdin", "-v", "error", "-probesize", "32", "-analyzeduration", "0", "-f", "rawvideo",
                          "-pix_fmt", pixfmt, "-s", size.c_str(), "-r", "50", "-i", url.c_str(), "-c", "copy", "-f", "null", "-", nullptr};
    pid_t pid;
    if (posix_spawnp(&pid, "ffmpeg", nullptr, nullptr, const_cast<char**>(args), environ) != 0) { perror("spawn"); return 1; }

    int fd = -1;
    if (listenFd >= 0)
    {
        fd = accept(listenFd, nullptr, nullptr);
        BigBuffers(fd);
        if (mode == "tcp") { int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)); }
    }
    else
        fd = ::open(base.c_str(), O_WRONLY);  // blocks until ffmpeg opens it
    int granted = 0;
#ifdef F_SETPIPE_SZ
    if (mode == "fifobig")
    {
        FILE* f = std::fopen("/proc/sys/fs/pipe-max-size", "r");
        int maxSize = 1 << 20;
        if (f) { if (std::fscanf(f, "%d", &maxSize) != 1) maxSize = 1 << 20; std::fclose(f); }
        granted = fcntl(fd, F_SETPIPE_SZ, maxSize);
    }
    if (mode == "fifo" || mode == "fifobig") granted = fcntl(fd, F_GETPIPE_SZ);
#endif
    if (listenFd >= 0) { socklen_t l = sizeof(granted); getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &granted, &l); }

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; i++)
        if (!WriteAll(fd, frame.data(), frameBytes)) { perror("write"); break; }
    const double sendS = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    ::close(fd);
    int status = 0;
    waitpid(pid, &status, 0);
    const double allS = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (listenFd >= 0) ::close(listenFd);
    ::unlink(base.c_str());
    ::unlink((base + ".sock").c_str());
    std::printf("%-8s %5dx%-5d %s  buffer %8d  %7.1f fps  %5.2f GB/s  (writes %.2f s, ffmpeg exit %d)\n", mode.c_str(), w, h,
                pixfmt, granted, frames / allS, frames * double(frameBytes) / allS / 1e9, sendS, WEXITSTATUS(status));
}
