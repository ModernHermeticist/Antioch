#pragma once

// A small lock-free ring of commands in named shared memory: one sender posts
// 32-bit payloads with increasing sequence numbers; any number of plugins read
// them and acknowledge. Used by FixBus and SnapshotBus.

#include <cstdint>

namespace suite
{
    class CommandRing
    {
    public:
        explicit CommandRing (const char* sharedName) noexcept;

        uint32_t post (uint32_t payload) noexcept;          // returns the sequence number
        uint32_t latest() const noexcept;
        bool read (uint32_t seq, uint32_t& payload) const noexcept;   // false if overwritten
        void acknowledge (uint32_t seq) noexcept;
        int acknowledgements (uint32_t seq) const noexcept;
        void reset() noexcept;

    private:
        struct Shared;
        Shared* shared;
    };
}
