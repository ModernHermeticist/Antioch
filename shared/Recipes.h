#pragma once

// Recipes: the whole suite's settings saved under a name, for any project.
// Ouroboros sends "save recipe <name>" / "load recipe <name>"; every suite plugin writes
// or reads its own settings in the recipes folder, matched by plugin and track name (or,
// when a plugin is only used once in the recipe, by plugin alone).

#include <juce_core/juce_core.h>
#include <cstdint>

namespace suite
{
    enum class RecipeAction : uint8_t { Save = 1, Load = 2 };

    namespace RecipeBus
    {
        uint32_t send (RecipeAction, const juce::String& name) noexcept;
        uint32_t latest() noexcept;
        bool read (uint32_t seq, RecipeAction& action, juce::String& name) noexcept;
        void acknowledge (uint32_t seq) noexcept;
        int acknowledgements (uint32_t seq) noexcept;
        void reset() noexcept;
    }

    namespace Recipes
    {
        /** <user app data>/Antioch Suite/Recipes (tests can redirect it). */
        juce::File folder();
        void setFolderForTests (const juce::File&);
        juce::StringArray list();
        juce::File fileFor (const juce::String& recipe, const juce::String& plugin, const juce::String& track);
        /** The file for this plugin in a recipe: same track name first, else the only one for this plugin. */
        juce::File find (const juce::String& recipe, const juce::String& plugin, const juce::String& track);
    }
}
