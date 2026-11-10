#include "CommandRing.h"
#include "SharedMemory.h"

#include <atomic>

namespace suite
{
    namespace { constexpr uint32_t ringSize = 16; }

    // message word: [ seq : 32 | payload : 32 ]   ack word: [ seq : 32 | count : 32 ]
    struct CommandRing::Shared
    {
        std::atomic<uint32_t> head;
        std::atomic<uint64_t> messages[ringSize];
        std::atomic<uint64_t> acks[ringSize];
    };

    CommandRing::CommandRing (const char* sharedName) noexcept
    {
        auto* m = static_cast<Shared*> (openSharedBlock (sharedName, sizeof (Shared)));
        if (m == nullptr)
        {
            m = new Shared {};   // shared memory unavailable: works within this binary only (never freed)
        }
        shared = m;
    }

    uint32_t CommandRing::post (uint32_t payload) noexcept
    {
        auto seq = shared->head.load (std::memory_order_relaxed) + 1;
        if (seq == 0) seq = 1;
        shared->acks[seq % ringSize].store ((uint64_t) seq << 32, std::memory_order_relaxed);
        shared->messages[seq % ringSize].store (((uint64_t) seq << 32) | payload, std::memory_order_release);
        shared->head.store (seq, std::memory_order_release);
        return seq;
    }

    uint32_t CommandRing::latest() const noexcept { return shared->head.load (std::memory_order_acquire); }

    bool CommandRing::read (uint32_t seq, uint32_t& payload) const noexcept
    {
        const auto w = shared->messages[seq % ringSize].load (std::memory_order_acquire);
        if ((uint32_t) (w >> 32) != seq)
            return false;
        payload = (uint32_t) (w & 0xffffffffu);
        return true;
    }

    void CommandRing::acknowledge (uint32_t seq) noexcept
    {
        auto& a = shared->acks[seq % ringSize];
        auto w = a.load (std::memory_order_acquire);
        for (;;)
        {
            const auto next = (uint32_t) (w >> 32) == seq ? w + 1 : (((uint64_t) seq << 32) | 1u);
            if (a.compare_exchange_weak (w, next, std::memory_order_acq_rel))
                return;
        }
    }

    int CommandRing::acknowledgements (uint32_t seq) const noexcept
    {
        const auto w = shared->acks[seq % ringSize].load (std::memory_order_acquire);
        return (uint32_t) (w >> 32) == seq ? (int) (w & 0xffffffffu) : 0;
    }

    void CommandRing::reset() noexcept
    {
        for (auto& m : shared->messages) m.store (0);
        for (auto& a : shared->acks) a.store (0);
        shared->head.store (0);
    }
}
