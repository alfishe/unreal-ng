#include "shared_memory_region.h"

#include <cstring>

#ifdef _WIN32
// Windows implementation
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

SharedMemoryRegion::~SharedMemoryRegion()
{
    close();
}

SharedMemoryRegion::SharedMemoryRegion(SharedMemoryRegion&& other) noexcept
    : _name(std::move(other._name))
    , _size(other._size)
    , _ptr(other._ptr)
    , _isOwner(other._isOwner)
#ifdef _WIN32
    , _handle(other._handle)
#else
    , _fd(other._fd)
#endif
{
    other._ptr = nullptr;
    other._size = 0;
    other._isOwner = false;
#ifdef _WIN32
    other._handle = INVALID_HANDLE_VALUE;
#else
    other._fd = -1;
#endif
}

SharedMemoryRegion& SharedMemoryRegion::operator=(SharedMemoryRegion&& other) noexcept
{
    if (this != &other)
    {
        close();
        _name = std::move(other._name);
        _size = other._size;
        _ptr = other._ptr;
        _isOwner = other._isOwner;
#ifdef _WIN32
        _handle = other._handle;
        other._handle = INVALID_HANDLE_VALUE;
#else
        _fd = other._fd;
        other._fd = -1;
#endif
        other._ptr = nullptr;
        other._size = 0;
        other._isOwner = false;
    }
    return *this;
}

#ifdef _WIN32

bool SharedMemoryRegion::create(const std::string& name, size_t size)
{
    close();

    _name = name;
    _size = size;
    _isOwner = true;

    _handle = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE | SEC_COMMIT,
        static_cast<DWORD>(size >> 32),
        static_cast<DWORD>(size & 0xFFFFFFFF),
        name.c_str());

    if (_handle == NULL)
    {
        _handle = INVALID_HANDLE_VALUE;
        reset();
        return false;
    }

    _ptr = MapViewOfFile(_handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (_ptr == NULL)
    {
        CloseHandle(_handle);
        _handle = INVALID_HANDLE_VALUE;
        reset();
        return false;
    }

    std::memset(_ptr, 0, size);
    return true;
}

bool SharedMemoryRegion::attach(const std::string& name)
{
    close();

    _name = name;
    _isOwner = false;

    _handle = OpenFileMappingA(FILE_MAP_READ, FALSE, name.c_str());
    if (_handle == NULL)
    {
        _handle = INVALID_HANDLE_VALUE;
        reset();
        return false;
    }

    // Get the size by mapping a view and querying
    MEMORY_BASIC_INFORMATION info;
    void* tempPtr = MapViewOfFile(_handle, FILE_MAP_READ, 0, 0, 0);
    if (tempPtr == NULL)
    {
        CloseHandle(_handle);
        _handle = INVALID_HANDLE_VALUE;
        reset();
        return false;
    }

    VirtualQuery(tempPtr, &info, sizeof(info));
    _size = info.RegionSize;
    _ptr = tempPtr;

    return true;
}

bool SharedMemoryRegion::resize(size_t newSize)
{
    if (!_isOwner || _name.empty())
        return false;

    // Windows: must recreate the mapping
    std::string savedName = _name;

    // Unmap current view
    if (_ptr)
    {
        UnmapViewOfFile(_ptr);
        _ptr = nullptr;
    }
    if (_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(_handle);
        _handle = INVALID_HANDLE_VALUE;
    }

    return create(savedName, newSize);
}

void SharedMemoryRegion::close()
{
    if (_ptr)
    {
        if (_isOwner)
            UnmapViewOfFile(_ptr);
        else
            UnmapViewOfFile(_ptr);
        _ptr = nullptr;
    }

    if (_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(_handle);
        _handle = INVALID_HANDLE_VALUE;
    }

    _size = 0;
    _isOwner = false;
    _name.clear();
}

#else  // POSIX (Linux, macOS)

bool SharedMemoryRegion::create(const std::string& name, size_t size)
{
    close();

    _name = name;
    _size = size;
    _isOwner = true;

    // Remove any stale shm with this name
    shm_unlink(name.c_str());

    _fd = shm_open(name.c_str(), O_CREAT | O_RDWR, 0666);
    if (_fd == -1)
    {
        reset();
        return false;
    }

    if (ftruncate(_fd, static_cast<off_t>(size)) == -1)
    {
        ::close(_fd);
        shm_unlink(name.c_str());
        _fd = -1;
        reset();
        return false;
    }

    _ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
    if (_ptr == MAP_FAILED)
    {
        _ptr = nullptr;
        ::close(_fd);
        shm_unlink(name.c_str());
        _fd = -1;
        reset();
        return false;
    }

    std::memset(_ptr, 0, size);
    return true;
}

bool SharedMemoryRegion::attach(const std::string& name)
{
    close();

    _name = name;
    _isOwner = false;

    _fd = shm_open(name.c_str(), O_RDONLY, 0);
    if (_fd == -1)
    {
        reset();
        return false;
    }

    // Get size via fstat
    struct stat st;
    if (fstat(_fd, &st) == -1)
    {
        ::close(_fd);
        _fd = -1;
        reset();
        return false;
    }
    _size = static_cast<size_t>(st.st_size);

    _ptr = mmap(nullptr, _size, PROT_READ, MAP_SHARED, _fd, 0);
    if (_ptr == MAP_FAILED)
    {
        _ptr = nullptr;
        ::close(_fd);
        _fd = -1;
        reset();
        return false;
    }

    return true;
}

bool SharedMemoryRegion::resize(size_t newSize)
{
    if (!_isOwner || _fd == -1)
        return false;

    // Unmap current
    if (_ptr)
    {
        munmap(_ptr, _size);
        _ptr = nullptr;
    }

    // Resize
    if (ftruncate(_fd, static_cast<off_t>(newSize)) == -1)
    {
        return false;
    }

    // Remap
    _ptr = mmap(nullptr, newSize, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
    if (_ptr == MAP_FAILED)
    {
        _ptr = nullptr;
        return false;
    }

    // Zero only the new portion
    if (newSize > _size)
    {
        std::memset(static_cast<char*>(_ptr) + _size, 0, newSize - _size);
    }

    _size = newSize;
    return true;
}

void SharedMemoryRegion::close()
{
    if (_ptr)
    {
        munmap(_ptr, _size);
        _ptr = nullptr;
    }

    if (_fd != -1)
    {
        ::close(_fd);

        if (_isOwner)
        {
            shm_unlink(_name.c_str());
        }

        _fd = -1;
    }

    _size = 0;
    _isOwner = false;
    _name.clear();
}

#endif  // _WIN32

void SharedMemoryRegion::reset()
{
    _name.clear();
    _size = 0;
    _ptr = nullptr;
    _isOwner = false;
}
