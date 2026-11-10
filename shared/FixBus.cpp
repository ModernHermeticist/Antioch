#include "FixBus.h"
#include "SharedMemory.h"

#include <atomic>

namespace suite::FixBus
{
    namespace
    {
        constexpr uint32_t ringSize = 16;

        // message word: [ seq : 32 | undo : 8 | fix : 8 | target low bits : 16 ]
        // ack word:     [ seq : 32 | count : 32 ]
        struct Shared
        {
            std::atomic<uint32_t> head;
            std::atomic<uint64_t> messages[ringSize];
            std::atomic<uint64_t> acks[ringSize];
        };

        Shared& shared() noexcept
        {
            static Shared local {};
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_fixbus_v1", sizeof (Shared)));
                return m != nullptr ? m : &local;
            }();
            return *s;
        }

        uint32_t post (bool undo, Fix fix, uint32_t target) noexcept
        {
            auto& s = shared();
            auto seq = s.head.load (std::memory_order_relaxed) + 1;
            if (seq == 0) seq = 1;
            const auto word = ((uint64_t) seq << 32) | ((uint64_t) (undo ? 1 : 0) << 24) | ((uint64_t) fix << 16) | (uint64_t) (target & 0xffffu);
            s.acks[seq % ringSize].store ((uint64_t) seq << 32, std::memory_order_relaxed);
            s.messages[seq % ringSize].store (word, std::memory_order_release);
            s.head.store (seq, std::memory_order_release);
            return seq;
        }
    }

    uint32_t send (Fix fix) noexcept { return post (false, fix, 0); }
    uint32_t sendUndo (uint32_t fixSeq) noexcept { return post (true, Fix::None, fixSeq); }
    uint32_t latest() noexcept { return shared().head.load (std::memory_order_acquire); }

    bool read (uint32_t seq, Message& out) noexcept
    {
        const auto w = shared().messages[seq % ringSize].load (std::memory_order_acquire);
        if ((uint32_t) (w >> 32) != seq)
            return false;
        out.seq = seq;
        out.undo = ((w >> 24) & 0xff) != 0;
        out.fix = (Fix) ((w >> 16) & 0xff);
        out.target = (uint32_t) (w & 0xffff);
        return true;
    }

    void acknowledge (uint32_t seq) noexcept
    {
        auto& a = shared().acks[seq % ringSize];
        auto w = a.load (std::memory_order_acquire);
        for (;;)
        {
            const auto next = (uint32_t) (w >> 32) == seq ? w + 1 : ((uint64_t) seq << 32) | 1u;
            if (a.compare_exchange_weak (w, next, std::memory_order_acq_rel))
                return;
        }
    }

    int acknowledgements (uint32_t seq) noexcept
    {
        const auto w = shared().acks[seq % ringSize].load (std::memory_order_acquire);
        return (uint32_t) (w >> 32) == seq ? (int) (w & 0xffffffffu) : 0;
    }

    void reset() noexcept
    {
        auto& s = shared();
        for (auto& m : s.messages) m.store (0);
        for (auto& a : s.acks) a.store (0);
        s.head.store (0);
    }
}
