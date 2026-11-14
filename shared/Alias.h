#pragma once

// Artist aliases: each alias has its own everything the suite learns and keeps: style log,
// recipes and signature, templates, hooks, gate patterns, role settings, Azoth presets, lead
// and master fingerprints, My sound and mix reports. The main alias uses
// <user app data>/Antioch Suite as always; another alias lives in Antioch Suite/Aliases/<name>.
// The current alias is one line in Antioch Suite/Current alias.txt, shared by every plugin
// (switch it in Ouroboros: Recipes... > Artist alias).

#include <juce_core/juce_core.h>
#include <mutex>

namespace suite::Alias
{
    inline juce::File& rootOverride() { static juce::File f; return f; }   // tests
    inline juce::File suiteRoot()
    {
        if (rootOverride() != juce::File()) return rootOverride();
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Antioch Suite");
    }
    inline juce::File currentFile() { return suiteRoot().getChildFile ("Current alias.txt"); }

    namespace detail
    {
        struct Cache { std::mutex m; juce::String name; juce::File root; double readAt = -1.0e9; };
        inline Cache& cache() { static Cache c; return c; }
    }
    /** The current alias ("" = your main name). Read from disk at most every 2 seconds (it's asked
        for by nearly every suite file path, often on the host's message thread). */
    inline juce::String current()
    {
        auto& c = detail::cache();
        const std::lock_guard<std::mutex> g (c.m);
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto root = suiteRoot();
        if (now - c.readAt > 2000.0 || root != c.root)
        {
            c.name = currentFile().loadFileAsString().trim();
            c.readAt = now;
            c.root = root;
        }
        return c.name;
    }
    inline void forgetCachedAlias() { const std::lock_guard<std::mutex> g (detail::cache().m); detail::cache().readAt = -1.0e9; }

    /** Switches every plugin to an alias ("" = main); a new name starts a new, empty alias. */
    inline bool setCurrent (const juce::String& name)
    {
        const auto clean = juce::File::createLegalFileName (name.trim());
        suiteRoot().createDirectory();
        if (clean.isNotEmpty()) suiteRoot().getChildFile ("Aliases").getChildFile (clean).createDirectory();
        const auto ok = currentFile().replaceWithText (clean);
        forgetCachedAlias();
        return ok;
    }

    /** Your aliases (besides the main one). */
    inline juce::StringArray list()
    {
        juce::StringArray names;
        for (const auto& d : suiteRoot().getChildFile ("Aliases").findChildFiles (juce::File::findDirectories, false))
            names.add (d.getFileName());
        names.sort (true);
        return names;
    }

    /** The current alias's folder, and a file or folder in it. */
    inline juce::File folder()
    {
        const auto name = current();
        return name.isEmpty() ? suiteRoot() : suiteRoot().getChildFile ("Aliases").getChildFile (juce::File::createLegalFileName (name));
    }
    inline juce::File file (const juce::String& child) { return folder().getChildFile (child); }
}
