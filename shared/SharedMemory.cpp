#include "SharedMemory.h"
#include <string>

#if defined (_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace suite
{
    void* openSharedBlock (const char* name, std::size_t bytes) noexcept
    {
       #if defined (_WIN32)
        const std::string full = std::string ("Local\\") + name;
        const std::wstring wide (full.begin(), full.end());
        auto handle = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD) bytes, wide.c_str());
        if (handle == nullptr)
            return nullptr;
        return MapViewOfFile (handle, FILE_MAP_ALL_ACCESS, 0, 0, bytes); // handle kept open for the process lifetime
       #else
        const std::string full = std::string ("/") + name;
        const int fd = shm_open (full.c_str(), O_CREAT | O_RDWR, 0600);
        if (fd < 0)
            return nullptr;
        struct stat st {};
        // Only grow the block; never truncate one another process already sized.
        if (fstat (fd, &st) != 0 || ((std::size_t) st.st_size < bytes && ftruncate (fd, (off_t) bytes) != 0))
        {
            close (fd);
            return nullptr;
        }
        auto* mem = mmap (nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close (fd);
        return mem == MAP_FAILED ? nullptr : mem;
       #endif
    }
}
