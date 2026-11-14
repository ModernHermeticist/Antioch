#pragma once

// One background thread for the suite's slow work (file writes, the label-ready evaluation,
// the director's plan), so nothing heavy runs on the host's message thread, which is FL's whole
// interface. Jobs run one at a time, in order. A job must not touch the plugin directly: it gets
// its own copies (or a shared_ptr) and hands its result back for the message thread to pick up.

#include <juce_core/juce_core.h>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

namespace suite::Worker
{
    namespace detail
    {
        class Thread : public juce::Thread
        {
        public:
            Thread() : juce::Thread ("Antioch Suite worker") { startThread (juce::Thread::Priority::low); }
            ~Thread() override
            {
                signalThreadShouldExit();
                { std::lock_guard<std::mutex> g (m); }
                cv.notify_all();
                stopThread (4000);
            }

            void post (std::function<void()> job)
            {
                { std::lock_guard<std::mutex> g (m); jobs.push_back (std::move (job)); }
                cv.notify_one();
            }

            /** Waits until every job posted so far has run (tests; never on the message thread in a host). */
            void finish()
            {
                std::unique_lock<std::mutex> l (m);
                idle.wait_for (l, std::chrono::seconds (20), [this] { return jobs.empty() && ! running; });
            }

            bool inlineMode = false;   // tests: run jobs straight away on the caller's thread

        private:
            void run() override
            {
                while (! threadShouldExit())
                {
                    std::function<void()> job;
                    {
                        std::unique_lock<std::mutex> l (m);
                        cv.wait_for (l, std::chrono::milliseconds (200), [this] { return ! jobs.empty() || threadShouldExit(); });
                        if (jobs.empty()) { idle.notify_all(); continue; }
                        job = std::move (jobs.front());
                        jobs.pop_front();
                        running = true;
                    }
                    if (job) job();
                    {
                        std::lock_guard<std::mutex> g (m);
                        running = false;
                    }
                    idle.notify_all();
                }
            }

            std::mutex m;
            std::condition_variable cv, idle;
            std::deque<std::function<void()>> jobs;
            bool running = false;
        };

        inline Thread& thread() { static Thread t; return t; }
    }

    /** Runs `job` on the suite's background thread. */
    inline void post (std::function<void()> job)
    {
        auto& t = detail::thread();
        if (t.inlineMode) { if (job) job(); return; }
        t.post (std::move (job));
    }

    /** Waits for every posted job (tests). */
    inline void finish() { detail::thread().finish(); }
}
