#include "Alias.h"
#include "Recipes.h"
#include "CommandRing.h"
#include "SharedMemory.h"

#include <atomic>
#include <cstring>

namespace suite
{
    namespace
    {
        constexpr int ringSize = 16, nameBytes = 64;
        struct Names { char names[ringSize][nameBytes]; };

        CommandRing& ring() noexcept { static CommandRing r ("antioch_suite_recipebus_v1"); return r; }
        Names& names() noexcept
        {
            static Names* n = []
            {
                auto* m = static_cast<Names*> (openSharedBlock ("antioch_suite_recipenames_v1", sizeof (Names)));
                return m != nullptr ? m : new Names {};
            }();
            return *n;
        }
        juce::File& folderOverride() { static juce::File f; return f; }
    }

    uint32_t RecipeBus::send (RecipeAction action, const juce::String& name) noexcept
    {
        const auto next = ring().latest() + 1 == 0 ? 1u : ring().latest() + 1;
        auto& slot = names().names[next % ringSize];
        std::memset (slot, 0, nameBytes);
        name.substring (0, 60).copyToUTF8 (slot, nameBytes - 1);
        std::atomic_thread_fence (std::memory_order_release);
        return ring().post ((uint32_t) action);
    }
    uint32_t RecipeBus::latest() noexcept { return ring().latest(); }
    void RecipeBus::acknowledge (uint32_t seq) noexcept { ring().acknowledge (seq); }
    int RecipeBus::acknowledgements (uint32_t seq) noexcept { return ring().acknowledgements (seq); }
    void RecipeBus::reset() noexcept { ring().reset(); }

    bool RecipeBus::read (uint32_t seq, RecipeAction& action, juce::String& name) noexcept
    {
        uint32_t p = 0;
        if (! ring().read (seq, p)) return false;
        std::atomic_thread_fence (std::memory_order_acquire);
        const auto& slot = names().names[seq % ringSize];
        name = juce::String::fromUTF8 (slot, (int) strnlen (slot, nameBytes));
        action = (RecipeAction) (p & 0xff);
        return name.isNotEmpty();
    }

    juce::File Recipes::folder()
    {
        if (folderOverride() != juce::File()) return folderOverride();
        return suite::Alias::file ("Recipes");
    }
    void Recipes::setFolderForTests (const juce::File& f) { folderOverride() = f; }

    juce::StringArray Recipes::list()
    {
        juce::StringArray out;
        for (const auto& d : folder().findChildFiles (juce::File::findDirectories, false))
            out.add (d.getFileName());
        out.sort (true);
        return out;
    }

    juce::File Recipes::fileFor (const juce::String& recipe, const juce::String& plugin, const juce::String& track)
    {
        return folder().getChildFile (juce::File::createLegalFileName (recipe))
                       .getChildFile (juce::File::createLegalFileName (plugin + " - " + (track.isNotEmpty() ? track : juce::String ("(no track name)")) + ".state"));
    }

    juce::File Recipes::find (const juce::String& recipe, const juce::String& plugin, const juce::String& track)
    {
        const auto exact = fileFor (recipe, plugin, track);
        if (exact.existsAsFile()) return exact;
        const auto dir = folder().getChildFile (juce::File::createLegalFileName (recipe));
        const auto mine = dir.findChildFiles (juce::File::findFiles, false, juce::File::createLegalFileName (plugin) + " - *.state");
        return mine.size() == 1 ? mine[0] : juce::File();
    }
}
