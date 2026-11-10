#pragma once

#include <cstddef>

namespace suite
{
    /** Maps a small named block of memory shared by every plugin instance on this machine
        (across plugin DLLs and bridged processes). The block starts zero-filled, stays
        mapped for the life of the process, and is never freed. Returns nullptr on failure. */
    void* openSharedBlock (const char* name, std::size_t bytes) noexcept;
}
