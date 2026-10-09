// Shared CPU notification; sequence-based waiting cannot lose an event between queue draining and sleep.
#pragma once
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace ofg::terrain {
class StreamingWake
{
public:
    // Captures the sequence before consumers drain their independent queues.
    uint64_t sequence()
    {
        std::lock_guard lock(m_mutex);
        return m_sequence;
    }
    // Signals after publishing an event and releasing the producer's queue lock.
    void signal()
    {
        {
            std::lock_guard lock(m_mutex);
            ++m_sequence;
        }
        m_changed.notify_all();
    }
    // Waits on a worker/native teardown thread only; browser frames never block here.
    void wait(uint64_t previous)
    {
        std::unique_lock lock(m_mutex);
        m_changed.wait(
            lock,
            [&]
            {
                return m_sequence != previous;
            }
        );
    }
    // Bounded test/native teardown wait; timeout is a failure guard, never a scheduling delay.
    bool waitFor(uint64_t previous, std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(
            lock,
            timeout,
            [&]
            {
                return m_sequence != previous;
            }
        );
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    uint64_t m_sequence = 0;
};
} // namespace ofg::terrain
