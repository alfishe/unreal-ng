#include "platform/fileio.h"

#include <windows.h>

#include "common/filehelper.h"

namespace platform
{
    namespace
    {
        void SetError(std::string* error, const char* what)
        {
            if (error)
                *error = std::string(what) + ": Windows error " + std::to_string(GetLastError());
        }

        class WindowsAppendFile : public AppendFile
        {
        public:
            explicit WindowsAppendFile(HANDLE handle) : _handle(handle) {}
            ~WindowsAppendFile() override { Close(); }

            bool Write(const void* data, size_t size) override
            {
                const auto* p = static_cast<const uint8_t*>(data);
                while (size > 0)
                {
                    const DWORD chunk = size > 0x40000000u ? 0x40000000u : static_cast<DWORD>(size);
                    DWORD written = 0;
                    if (!WriteFile(_handle, p, chunk, &written, nullptr) || written == 0)
                        return false;
                    p += written;
                    size -= written;
                    _size += written;
                }
                return true;
            }
            bool Sync() override { return _handle != INVALID_HANDLE_VALUE && FlushFileBuffers(_handle) != 0; }
            uint64_t Size() const override { return _size; }
            void Close() override
            {
                if (_handle != INVALID_HANDLE_VALUE)
                    CloseHandle(_handle);
                _handle = INVALID_HANDLE_VALUE;
            }

        private:
            HANDLE _handle;
            uint64_t _size = 0;
        };

        class WindowsRandomAccessFile : public RandomAccessFile
        {
        public:
            WindowsRandomAccessFile(HANDLE handle, uint64_t size) : _handle(handle), _size(size) {}
            ~WindowsRandomAccessFile() override { CloseHandle(_handle); }

            uint64_t Size() const override { return _size; }
            bool ReadAt(uint64_t offset, void* out, size_t size) const override
            {
                auto* p = static_cast<uint8_t*>(out);
                while (size > 0)
                {
                    OVERLAPPED at{};
                    at.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFu);
                    at.OffsetHigh = static_cast<DWORD>(offset >> 32);
                    const DWORD chunk = size > 0x40000000u ? 0x40000000u : static_cast<DWORD>(size);
                    DWORD read = 0;
                    if (!ReadFile(_handle, p, chunk, &read, &at) || read == 0)
                        return false;
                    p += read;
                    offset += read;
                    size -= read;
                }
                return true;
            }

        private:
            HANDLE _handle;
            uint64_t _size;
        };
    }  // namespace

    std::unique_ptr<AppendFile> AppendFile::Create(const std::string& utf8Path, std::string* error)
    {
        // Readers and a later delete or rename may share the file while it is written
        HANDLE handle = CreateFileW(FileHelper::ToFsPath(utf8Path).wstring().c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            SetError(error, "cannot create");
            return nullptr;
        }
        return std::make_unique<WindowsAppendFile>(handle);
    }

    std::unique_ptr<RandomAccessFile> RandomAccessFile::Open(const std::string& utf8Path, std::string* error)
    {
        HANDLE handle = CreateFileW(FileHelper::ToFsPath(utf8Path).wstring().c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            SetError(error, "cannot open");
            return nullptr;
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(handle, &size))
        {
            SetError(error, "cannot read the size");
            CloseHandle(handle);
            return nullptr;
        }
        return std::make_unique<WindowsRandomAccessFile>(handle, static_cast<uint64_t>(size.QuadPart));
    }
}  // namespace platform
