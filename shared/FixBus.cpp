#include "FixBus.h"
#include "CommandRing.h"

namespace suite::FixBus
{
    namespace
    {
        // payload: [ undo : 1 | fix : 7 | target low bits : 24 ]
        CommandRing& ring() noexcept { static CommandRing r ("antioch_suite_fixbus_v3"); return r; }
        uint32_t pack (bool undo, Fix fix, uint32_t target) noexcept
        {
            return ((uint32_t) (undo ? 1 : 0) << 31) | (((uint32_t) fix & 0x7fu) << 24) | (target & 0xffffffu);
        }
    }

    uint32_t send (Fix fix) noexcept { return ring().post (pack (false, fix, 0)); }
    uint32_t send (Fix fix, uint32_t arg) noexcept { return ring().post (pack (false, fix, arg)); }
    uint32_t sendUndo (uint32_t fixSeq) noexcept { return ring().post (pack (true, Fix::None, fixSeq)); }
    uint32_t latest() noexcept { return ring().latest(); }
    void acknowledge (uint32_t seq) noexcept { ring().acknowledge (seq); }
    int acknowledgements (uint32_t seq) noexcept { return ring().acknowledgements (seq); }
    void reset() noexcept { ring().reset(); }

    bool read (uint32_t seq, Message& out) noexcept
    {
        uint32_t p = 0;
        if (! ring().read (seq, p))
            return false;
        out.seq = seq;
        out.undo = (p >> 31) != 0;
        out.fix = (Fix) ((p >> 24) & 0x7f);
        out.target = p & 0xffffff;
        return true;
    }
}
