#include "FixBus.h"
#include "CommandRing.h"

namespace suite::FixBus
{
    namespace
    {
        // payload: [ undo : 8 | fix : 8 | target low bits : 16 ]
        CommandRing& ring() noexcept { static CommandRing r ("antioch_suite_fixbus_v2"); return r; }
        uint32_t pack (bool undo, Fix fix, uint32_t target) noexcept
        {
            return ((uint32_t) (undo ? 1 : 0) << 24) | ((uint32_t) fix << 16) | (target & 0xffffu);
        }
    }

    uint32_t send (Fix fix) noexcept { return ring().post (pack (false, fix, 0)); }
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
        out.undo = ((p >> 24) & 0xff) != 0;
        out.fix = (Fix) ((p >> 16) & 0xff);
        out.target = p & 0xffff;
        return true;
    }
}
