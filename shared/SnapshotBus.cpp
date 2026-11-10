#include "SuiteProcessor.h"
#include "CommandRing.h"

namespace suite::SnapshotBus
{
    namespace
    {
        // payload: [ action : 8 | slot : 8 ]
        CommandRing& ring() noexcept { static CommandRing r ("antioch_suite_snapshotbus_v1"); return r; }
    }

    uint32_t send (SnapshotAction action, int slot) noexcept
    {
        return ring().post (((uint32_t) action << 8) | (uint32_t) (slot & 0xff));
    }

    uint32_t latest() noexcept { return ring().latest(); }
    void acknowledge (uint32_t seq) noexcept { ring().acknowledge (seq); }
    int acknowledgements (uint32_t seq) noexcept { return ring().acknowledgements (seq); }
    void reset() noexcept { ring().reset(); }

    bool read (uint32_t seq, SnapshotAction& action, int& slot) noexcept
    {
        uint32_t p = 0;
        if (! ring().read (seq, p))
            return false;
        action = (SnapshotAction) ((p >> 8) & 0xff);
        slot = (int) (p & 0xff);
        return true;
    }
}
