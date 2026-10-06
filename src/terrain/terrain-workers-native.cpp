// Two native worker threads perform only CPU meshing; the short queue lock never encloses generation.
#include "terrain/terrain-workers.h"
#include "core/engine-error.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace ofg::terrain {
struct TerrainWorkers::State
{
    struct Task
    {
        BuildRequest request;
        GeneratorSettings generator;
        std::atomic<bool> cancelled{false};
        bool held = false, fail = false;
    };
    std::mutex mutex;
    std::condition_variable changed, finished;
    bool stopping = false;
    std::map<RequestId, std::shared_ptr<Task>> tasks;
    std::deque<std::shared_ptr<Task>> queued;
    std::vector<WorkerResult> results;
    std::array<std::thread, 2> threads;

    // Waits for runnable work, generates outside the queue lock and publishes one terminal result.
    void run(uint32_t worker)
    {
        for (;;)
        {
            std::shared_ptr<Task> task;
            {
                std::unique_lock lock(mutex);
                changed.wait(
                    lock,
                    [&]
                    {
                        return stopping || !queued.empty();
                    }
                );
                if (stopping && queued.empty())
                {
                    return;
                }
                task = std::move(queued.front());
                queued.pop_front();
                changed.wait(
                    lock,
                    [&]
                    {
                        return stopping || task->cancelled || !task->held;
                    }
                );
            }
            WorkerResult result;
            result.request = task->request;
            result.worker = worker;
            try
            {
                if (!task->cancelled)
                {
                    if (task->fail)
                    {
                        throw EngineError("Injected terrain generation failure.");
                    }
                    TerrainMesher mesher(
                        task->request.key.address,
                        task->request.key.transitionFaces,
                        task->generator,
                        task->request.byteLimit
                    );
                    while (!task->cancelled && !mesher.step(256))
                    {
                    }
                    if (!task->cancelled)
                    {
                        result.geometry = mesher.takeGeometry();
                    }
                }
                if (task->cancelled)
                {
                    result.outcome = WorkerOutcome::Cancelled;
                }
            } catch (const std::exception& error)
            {
                result.outcome = WorkerOutcome::Failed;
                result.error = error.what();
            }
            {
                std::lock_guard lock(mutex);
                results.push_back(std::move(result));
            }
            finished.notify_one();
        }
    }
};

TerrainWorkers::TerrainWorkers()
    : m_state(std::make_unique<State>())
{
    try
    {
        for (uint32_t i = 0; i < m_state->threads.size(); ++i)
        {
            m_state->threads[i] = std::thread(
                [state = m_state.get(), i]
                {
                    state->run(i);
                }
            );
        }
    } catch (...)
    {
        {
            std::lock_guard lock(m_state->mutex);
            m_state->stopping = true;
        }
        m_state->changed.notify_all();
        for (auto& thread : m_state->threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        throw;
    }
}

TerrainWorkers::~TerrainWorkers()
{
    {
        std::lock_guard lock(m_state->mutex);
        m_state->stopping = true;
        for (auto& [id, task] : m_state->tasks)
        {
            task->cancelled = true;
        }
    }
    m_state->changed.notify_all();
    for (auto& thread : m_state->threads)
    {
        thread.join();
    }
}

void TerrainWorkers::submit(BuildRequest request, GeneratorSettings generator, bool hold, bool fail)
{
    auto task = std::make_shared<State::Task>();
    task->request = request;
    task->generator = generator;
    task->held = hold;
    task->fail = fail;
    {
        std::lock_guard lock(m_state->mutex);
        if (m_state->stopping || m_state->tasks.size() >= 64 || m_state->tasks.contains(request.id))
        {
            throw EngineError("Terrain worker queue is full, stopping or received a duplicate request.");
        }
        m_state->tasks.emplace(request.id, task);
        try
        {
            m_state->queued.push_back(task);
        } catch (...)
        {
            m_state->tasks.erase(request.id);
            throw;
        }
    }
    m_state->changed.notify_one();
}

void TerrainWorkers::cancel(RequestId id)
{
    {
        std::lock_guard lock(m_state->mutex);
        const auto task = m_state->tasks.find(id);
        if (task != m_state->tasks.end())
        {
            task->second->cancelled = true;
        }
        else
        {
            WorkerResult result;
            result.request.id = id;
            result.outcome = WorkerOutcome::Cancelled;
            m_state->results.push_back(std::move(result));
        }
    }
    m_state->changed.notify_all();
    m_state->finished.notify_one();
}

void TerrainWorkers::releaseHeld()
{
    {
        std::lock_guard lock(m_state->mutex);
        for (auto& [id, task] : m_state->tasks)
        {
            task->held = false;
        }
    }
    m_state->changed.notify_all();
}

std::vector<WorkerResult> TerrainWorkers::takeResults()
{
    std::vector<WorkerResult> results;
    {
        std::lock_guard lock(m_state->mutex);
        results.swap(m_state->results);
        for (const auto& result : results)
        {
            m_state->tasks.erase(result.request.id);
        }
    }
    return results;
}

bool TerrainWorkers::waitForResult(std::chrono::milliseconds timeout)
{
    std::unique_lock lock(m_state->mutex);
    return m_state->finished.wait_for(
        lock,
        timeout,
        [&]
        {
            return !m_state->results.empty();
        }
    );
}
} // namespace ofg::terrain
