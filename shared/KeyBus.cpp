#include "KeyBus.h"

#include <atomic>
#include <chrono>
#include <cstdint>

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
 #include <unistd.h>
#endif

namespace suite::KeyBus
{
    namespace
    {
        // Layout: [ timestamp seconds : 32 | magic : 8 | scale : 8 | root : 8 | valid : 8 ]
        struct Shared
        {
            std::atomic<uint64_t> word;
        };

        static_assert (std::atomic<uint64_t>::is_always_lock_free, "KeyBus needs lock-free 64-bit atomics");

        constexpr uint64_t magic = 0xA7;

        uint32_t nowSeconds() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<seconds> (system_clock::now().time_since_epoch()).count();
        }

        Shared* mapShared() noexcept
        {
           #if defined (_WIN32)
            auto handle = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                              (DWORD) sizeof (Shared), L"Local\\AntiochSuiteKeyBus_v1");
            if (handle == nullptr)
                return nullptr;
            auto* mem = MapViewOfFile (handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (Shared));
            return static_cast<Shared*> (mem); // mapping intentionally kept for the process lifetime
           #else
            const int fd = shm_open ("/antioch_suite_keybus_v1", O_CREAT | O_RDWR, 0600);
            if (fd < 0)
                return nullptr;
            if (ftruncate (fd, (off_t) sizeof (Shared)) != 0)
            {
                close (fd);
                return nullptr;
            }
            auto* mem = mmap (nullptr, sizeof (Shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            close (fd);
            return mem == MAP_FAILED ? nullptr : static_cast<Shared*> (mem);
           #endif
        }

        Shared& shared() noexcept
        {
            // Falls back to process-local memory if the OS refuses shared memory.
            static Shared local { 0 };
            static Shared* s = [] { auto* m = mapShared(); return m != nullptr ? m : &local; }();
            return *s;
        }
    }

    void publish (int root, int scale) noexcept
    {
        const uint64_t word = ((uint64_t) nowSeconds() << 32) | (magic << 24)
                            | ((uint64_t) (scale & 0xff) << 16) | ((uint64_t) (root & 0xff) << 8) | 1u;
        shared().word.store (word, std::memory_order_release);
    }

    void withdraw() noexcept
    {
        shared().word.store (0, std::memory_order_release);
    }

    KeyInfo read (int maxAgeSeconds) noexcept
    {
        const auto word = shared().word.load (std::memory_order_acquire);
        KeyInfo info;

        if ((word & 0xff) == 0 || ((word >> 24) & 0xff) != magic)
            return info;

        const auto stamp = (uint32_t) (word >> 32);
        const auto age = (int64_t) nowSeconds() - (int64_t) stamp;
        if (age < -2 || age > maxAgeSeconds)
            return info;

        info.valid = true;
        info.root = (int) ((word >> 8) & 0xff) % 12;
        info.scale = (int) ((word >> 16) & 0xff);
        return info;
    }
}
