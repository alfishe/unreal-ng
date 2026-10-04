#include "platform/fileio.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace platform
{
    namespace
    {
        void SetError(std::string* error, const char* what)
        {
            if (error)
                *error = std::string(what) + ": " + std::strerror(errno);
        }

        class PosixAppendFile : public AppendFile
        {
        public:
            explicit PosixAppendFile(int fd) : _fd(fd) {}
            ~PosixAppendFile() override { Close(); }

            bool Write(const void* data, size_t size) override
            {
                const auto* p = static_cast<const uint8_t*>(data);
                while (size > 0)
                {
                    const ssize_t n = ::write(_fd, p, size);
                    if (n < 0 && errno == EINTR)
                        continue;
                    if (n <= 0)
                        return false;
                    p += n;
                    size -= static_cast<size_t>(n);
                    _size += static_cast<uint64_t>(n);
                }
                return true;
            }
            bool Sync() override { return _fd >= 0 && ::fsync(_fd) == 0; }
            uint64_t Size() const override { return _size; }
            void Close() override
            {
                if (_fd >= 0)
                    ::close(_fd);
                _fd = -1;
            }

        private:
            int _fd;
            uint64_t _size = 0;
        };

        class PosixRandomAccessFile : public RandomAccessFile
        {
        public:
            PosixRandomAccessFile(int fd, uint64_t size) : _fd(fd), _size(size) {}
            ~PosixRandomAccessFile() override { ::close(_fd); }

            uint64_t Size() const override { return _size; }
            bool ReadAt(uint64_t offset, void* out, size_t size) const override
            {
                auto* p = static_cast<uint8_t*>(out);
                while (size > 0)
                {
                    const ssize_t n = ::pread(_fd, p, size, static_cast<off_t>(offset));
                    if (n < 0 && errno == EINTR)
                        continue;
                    if (n <= 0)
                        return false;
                    p += n;
                    offset += static_cast<uint64_t>(n);
                    size -= static_cast<size_t>(n);
                }
                return true;
            }

        private:
            int _fd;
            uint64_t _size;
        };
    }  // namespace

    std::unique_ptr<AppendFile> AppendFile::Create(const std::string& utf8Path, std::string* error)
    {
        const int fd = ::open(utf8Path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (fd < 0)
        {
            SetError(error, "cannot create");
            return nullptr;
        }
        return std::make_unique<PosixAppendFile>(fd);
    }

    std::unique_ptr<RandomAccessFile> RandomAccessFile::Open(const std::string& utf8Path, std::string* error)
    {
        const int fd = ::open(utf8Path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0)
        {
            SetError(error, "cannot open");
            return nullptr;
        }
        struct stat st{};
        if (::fstat(fd, &st) != 0)
        {
            SetError(error, "cannot stat");
            ::close(fd);
            return nullptr;
        }
        return std::make_unique<PosixRandomAccessFile>(fd, static_cast<uint64_t>(st.st_size));
    }
}  // namespace platform
