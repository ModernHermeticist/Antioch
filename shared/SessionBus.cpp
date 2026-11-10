#include "Session.h"
#include "CommandRing.h"

namespace suite::SessionBus
{
    namespace
    {
        // payload: [ style : 8 | root : 8 | scale : 8 ]
        CommandRing& ring() noexcept { static CommandRing r ("antioch_suite_sessionbus_v1"); return r; }
    }

    uint32_t send (const SessionRequest& r) noexcept
    {
        return ring().post (((uint32_t) (r.style & 0xff) << 16) | ((uint32_t) (r.root & 0xff) << 8) | (uint32_t) (r.scale & 0xff));
    }
    uint32_t latest() noexcept { return ring().latest(); }
    void acknowledge (uint32_t seq) noexcept { ring().acknowledge (seq); }
    int acknowledgements (uint32_t seq) noexcept { return ring().acknowledgements (seq); }
    void reset() noexcept { ring().reset(); }

    bool read (uint32_t seq, SessionRequest& out) noexcept
    {
        uint32_t p = 0;
        if (! ring().read (seq, p)) return false;
        out.style = (int) ((p >> 16) & 0xff);
        out.root = (int) ((p >> 8) & 0xff);
        out.scale = (int) (p & 0xff);
        return true;
    }
}
