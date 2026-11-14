#pragma once

// The freeze watchdog. Everything the suite does on the host's message thread (FL's interface)
// runs inside a Watchdog::Scope naming the plugin and the job. A job that holds the thread for
// more than `thresholdMs` is written to "Freeze log.txt" in the suite folder (by the worker thread,
// never the message thread), with how long it took; one still running after a second is logged
// while it runs, so even a hang names itself.

#include "Alias.h"
#include "Worker.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
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
        /** The last job that finished (what ran just before a block that no job owns). */
        inline std::mutex& lastLock() { static std::mutex m; return m; }
        inline juce::String& lastJob() { static juce::String s; return s; }
        inline std::atomic<double>& lastPong() { static std::atomic<double> t { 0.0 }; return t; }
        inline std::atomic<bool>& pingPending() { static std::atomic<bool> b { false }; return b; }
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

        /** The heartbeat: FL's interface (the message thread) is pinged every 100 ms; when it doesn't
            answer for over 300 ms, the block is logged with whatever suite job was running (or ran
            last), so a freeze with no slow suite job still shows up, and says it wasn't the suite's. */
        inline void heartbeat()
        {
            if (suite::Worker::detail::thread().inlineMode) return;   // tests: no message loop
            auto* mm = juce::MessageManager::getInstanceWithoutCreating();
            if (mm == nullptr) return;
            const auto now = juce::Time::getMillisecondCounterHiRes();
            static double blockedSince = 0.0;
            static bool reported = false;
            if (pingPending().load())
            {
                const auto waited = now - lastPong().load();
                if (waited > 300.0 && ! reported)
                {
                    reported = true;
                    blockedSince = lastPong().load();
                    juce::String job;
                    if (auto* a = current().load(); a != nullptr && a->what != nullptr) job = (a->plugin != nullptr ? *a->plugin : juce::String()) + ": " + a->what + " (running)";
                    else { std::lock_guard<std::mutex> g (lastLock()); job = lastJob().isNotEmpty() ? lastJob() + " (finished just before; the block itself isn't a suite job)" : juce::String ("no suite job (FL or another plugin)"); }
                    record ({ juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S"), "FL's interface", "blocked: " + job, waited, true });
                }
                return;
            }
            if (reported) { reported = false; juce::ignoreUnused (blockedSince); }
            pingPending().store (true);
            lastPong().store (now);
            mm->callAsync ([] { lastPong().store (juce::Time::getMillisecondCounterHiRes()); pingPending().store (false); });
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
                    heartbeat();
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
            { std::lock_guard<std::mutex> g (detail::lastLock()); detail::lastJob() = name + ": " + active.what; }
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
        // the file has every plugin's (each plugin is its own DLL with its own copy of this)
        if (const auto f = detail::file(); f.existsAsFile() && f.getSize() > 0)
        {
            auto lines = juce::StringArray::fromLines (f.loadFileAsString().trim());
            lines.removeEmptyStrings();
            juce::StringArray newest;
            for (int i = lines.size() - 1; i >= 0 && newest.size() < 60; --i) newest.add (lines[i]);
            return newest.joinIntoString ("\n") + "\n\n(" + f.getFullPathName() + ")";
        }
        const auto e = recent();
        juce::StringArray l;
        for (auto it = e.rbegin(); it != e.rend(); ++it)
            l.add (it->when + "  " + it->plugin + ": " + it->what + (it->stillRunning ? " still running after " : " took ") + juce::String (juce::roundToInt (it->ms)) + " ms");
        return l.isEmpty() ? juce::String ("No freezes this session: nothing in the suite has held FL's interface for more than "
                                           + juce::String (juce::roundToInt (thresholdMs)) + " ms.")
                           : l.joinIntoString ("\n") + "\n\n(Also in " + detail::file().getFullPathName() + ")";
    }
}
