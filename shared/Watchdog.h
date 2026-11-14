#pragma once

// The freeze watchdog. Everything the suite does on the host's message thread (FL's interface)
// runs inside a Watchdog::Scope naming the plugin and the job. A job that holds the thread for
// more than `thresholdMs` is written to "Freeze log.txt" in the suite folder (by the worker thread,
// never the message thread), with how long it took; one still running after a second is logged
// while it runs, so even a hang names itself.

#include "Alias.h"
#include "Worker.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <mutex>
#include <vector>

namespace suite::Watchdog
{
    constexpr double thresholdMs = 50.0, hangMs = 1000.0;

    struct Event { juce::String when, plugin, what; double ms = 0.0; bool stillRunning = false; };

    namespace detail
    {
        struct Active { const juce::String* plugin = nullptr; const char* what = nullptr; double startMs = 0.0; };

        inline std::mutex& lock() { static std::mutex m; return m; }
        inline std::vector<Event>& events() { static std::vector<Event> e; return e; }
        inline std::atomic<Active*>& current() { static std::atomic<Active*> a { nullptr }; return a; }
        inline juce::File& fileOverride() { static juce::File f; return f; }
        inline juce::File file() { return fileOverride() != juce::File() ? fileOverride() : suite::Alias::suiteRoot().getChildFile ("Freeze log.txt"); }

        inline void record (Event e)
        {
            {
                std::lock_guard<std::mutex> g (lock());
                events().push_back (e);
                if (events().size() > 200) events().erase (events().begin());
            }
            const auto line = e.when + "  " + e.plugin + ": " + e.what + (e.stillRunning ? " still running after " : " took ")
                              + juce::String (juce::roundToInt (e.ms)) + " ms\n";
            suite::Worker::post ([line]
            {
                const auto f = file();
                f.getParentDirectory().createDirectory();
                if (f.getSize() > 512 * 1024) f.replaceWithText ({});   // keeps it small
                f.appendText (line);
            });
        }

        /** Notices a job that's still running after a second (a hang) while it runs. */
        class HangWatcher : public juce::Thread
        {
        public:
            HangWatcher() : juce::Thread ("Antioch Suite watchdog") { startThread (juce::Thread::Priority::low); }
            ~HangWatcher() override { stopThread (2000); }
            void run() override
            {
                Active* reported = nullptr;
                while (! threadShouldExit())
                {
                    wait (100);
                    auto* a = current().load();
                    if (a == nullptr || a == reported) continue;
                    const auto ms = juce::Time::getMillisecondCounterHiRes() - a->startMs;
                    if (ms > hangMs)
                    {
                        reported = a;
                        record ({ juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S"), a->plugin != nullptr ? *a->plugin : juce::String(),
                                  a->what != nullptr ? a->what : "", ms, true });
                    }
                }
            }
        };
        inline void ensureWatcher() { static HangWatcher w; }
    }

    /** Times what's inside it (message thread). `what` must be a string literal. */
    class Scope
    {
    public:
        Scope (const juce::String& plugin, const char* what) : name (plugin)
        {
            detail::ensureWatcher();
            active.plugin = &name; active.what = what; active.startMs = juce::Time::getMillisecondCounterHiRes();
            previous = detail::current().exchange (&active);
        }
        ~Scope()
        {
            detail::current().store (previous);
            const auto ms = juce::Time::getMillisecondCounterHiRes() - active.startMs;
            if (ms > thresholdMs)
                detail::record ({ juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S"), name, active.what, ms, false });
        }
        Scope (const Scope&) = delete;
        Scope& operator= (const Scope&) = delete;
    private:
        juce::String name;
        detail::Active active;
        detail::Active* previous = nullptr;
    };

    /** The freezes noticed in this session (newest last). */
    inline std::vector<Event> recent() { std::lock_guard<std::mutex> g (detail::lock()); return detail::events(); }
    inline void clear() { std::lock_guard<std::mutex> g (detail::lock()); detail::events().clear(); }
    inline void setFileForTests (const juce::File& f) { detail::fileOverride() = f; }
    /** The log as text (newest first), for the search box's "Freeze log". */
    inline juce::String text()
    {
        const auto e = recent();
        juce::StringArray l;
        for (auto it = e.rbegin(); it != e.rend(); ++it)
            l.add (it->when + "  " + it->plugin + ": " + it->what + (it->stillRunning ? " still running after " : " took ") + juce::String (juce::roundToInt (it->ms)) + " ms");
        return l.isEmpty() ? juce::String ("No freezes this session: nothing in the suite has held FL's interface for more than "
                                           + juce::String (juce::roundToInt (thresholdMs)) + " ms.")
                           : l.joinIntoString ("\n") + "\n\n(Also in " + detail::file().getFullPathName() + ")";
    }
}
