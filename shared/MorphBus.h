#pragma once

// Snapshot morph: Ouroboros shares "morph the suite towards snapshot <slot> by <amount>"
// (0 before the build into the last drop, rising through it, 1 from the drop on).
// Every suite plugin glides its parameters accordingly (SuiteProcessor).

#include <cstdint>

namespace suite
{
    struct MorphState { bool valid = false; int slot = -1; float amount = 0.0f; };

    namespace MorphBus
    {
        void publish (int slot, float amount) noexcept;   // audio thread safe
        MorphState read (int maxAgeSeconds = 2) noexcept;
        void withdraw() noexcept;
    }
}
