// Two bounded I/O workers implement disk/IndexedDB lookup and HTTP misses without occupying mesh workers.
#include "terrain/terrain-data-cache.h"
#include "platform/http-client.h"
#include "core/engine-error.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <sstream>
#include <iomanip>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>
#else
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace ofg::terrain {
namespace {
#ifdef __EMSCRIPTEN__
struct StorageOperation
{
    std::string key, error;
    std::vector<std::byte> bytes;
    std::mutex mutex;
    std::condition_variable changed;
    bool write = false, finished = false;
};

// Copy read bytes before JavaScript releases them and notify only the dedicated I/O worker.
extern "C" EMSCRIPTEN_KEEPALIVE void ofgStorageFinished(
    uintptr_t pointer,
    const std::byte* bytes,
    size_t length,
    const char* error
)
{
    auto* operation = reinterpret_cast<StorageOperation*>(pointer);
    {
        std::lock_guard lock(operation->mutex);
        try
        {
            if (error)
                operation->error = error;
            else if (length)
                operation->bytes.assign(bytes, bytes + length);
        } catch (const std::exception& failure)
        {
            operation->error = failure.what();
        }
        operation->finished = true;
    }
    operation->changed.notify_one();
}

// IndexedDB transactions expose only complete byte arrays; quota failure is reported to the cache as a warning.
// clang-format off
EM_JS(void, browserStorage, (uintptr_t operation, const char* key, bool write, const std::byte* bytes, size_t length), {
    const name = UTF8ToString(key);
    const saved = write ? HEAPU8.slice(bytes, bytes + length) : null;
    let finished = false;
    const finish = (value, error) =>
    {
        if (finished)
            return;
        finished = true;
        let pointer = 0, message = 0;
        if (value && value.length)
        {
            pointer = _malloc(value.length);
            if (pointer)
                HEAPU8.set(value, pointer);
            else
                error = 'IndexedDB allocation failed';
        }
        if (error)
            message = stringToNewUTF8(String(error));
        _ofgStorageFinished(operation, pointer, pointer ? value.length : 0, message);
        if (pointer)
            _free(pointer);
        if (message)
            _free(message);
    };
    try
    {
        const open = indexedDB.open('ofg-terrain-v1', 1);
        open.onupgradeneeded = () => open.result.createObjectStore('tiles');
        open.onerror = () => finish(null, open.error);
        open.onblocked = () => finish(null, 'Terrain IndexedDB upgrade blocked');
        open.onsuccess = () =>
        {
            const db = open.result;
            if (finished)
            {
                db.close();
                return;
            }
            let value = null;
            const tx = db.transaction('tiles', write ? 'readwrite' : 'readonly');
            tx.onabort = () =>
            {
                db.close();
                finish(null, tx.error || 'Terrain transaction aborted');
            };
            tx.oncomplete = () =>
            {
                db.close();
                finish(value, null);
            };
            const request = write ? tx.objectStore('tiles').put(saved, name) : tx.objectStore('tiles').get(name);
            request.onsuccess = () =>
            {
                if (!write && request.result)
                {
                    if (!(request.result instanceof Uint8Array) || request.result.length > 2097152)
                    {
                        tx.abort();
                        return;
                    }
                    value = request.result;
                }
            };
        };
    } catch (error)
    {
        finish(null, error);
    }
});
// clang-format on

// Runs only on the browser main runtime; the operation remains alive on its I/O worker's stack.
void startStorage(void* pointer)
{
    auto* operation = static_cast<StorageOperation*>(pointer);
    browserStorage(
        uintptr_t(operation),
        operation->key.c_str(),
        operation->write,
        operation->bytes.data(),
        operation->bytes.size()
    );
}

// Block a dedicated I/O worker on an asynchronous IndexedDB operation; mesh/UI/coordinator threads never wait here.
std::vector<std::byte> storage(const std::string& key, const std::vector<std::byte>* write)
{
    StorageOperation operation;
    operation.key = key;
    operation.write = write != nullptr;
    if (write)
        operation.bytes = *write;
    if (!emscripten_proxy_async(
            emscripten_proxy_get_system_queue(),
            emscripten_main_runtime_thread_id(),
            startStorage,
            &operation
        ))
        throw EngineError("Cannot proxy terrain storage operation.");
    std::unique_lock lock(operation.mutex);
    operation.changed.wait(
        lock,
        [&]
        {
            return operation.finished;
        }
    );
    if (!operation.error.empty())
        throw EngineError(operation.error);
    return std::move(operation.bytes);
}
#else
// Hash only the filename; the full content key stored inside the file verifies collisions and source identity.
std::filesystem::path cachePath(const std::string& directory, std::string_view key)
{
    uint64_t hash = 14695981039346656037ull;
    for (const auto character : key)
        hash = (hash ^ uint8_t(character)) * 1099511628211ull;
    std::ostringstream name;
    name << std::hex << std::setw(16) << std::setfill('0') << hash << ".tile";
    return std::filesystem::u8path(directory) / name.str();
}

// Read a bounded local entry, checking its complete key before any binary tile decoding.
std::vector<std::byte> readDisk(const std::string& directory, const std::string& key)
{
    const auto path = cachePath(directory, key);
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        if (std::filesystem::exists(path))
            throw EngineError("Cannot read terrain cache entry.");
        return {};
    }
    const auto length = input.tellg();
    if (length < 0 || uint64_t(length) > terrainResponseLimit + 8192)
        throw EngineError("Invalid terrain cache size.");
    input.seekg(0);
    std::string identity;
    std::getline(input, identity);
    if (identity != key)
        return {};
    const auto remaining = size_t(length) - identity.size() - 1;
    if (remaining > terrainResponseLimit)
        throw EngineError("Terrain cache entry exceeds byte limit.");
    std::vector<std::byte> bytes(remaining);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
        throw EngineError("Incomplete terrain cache entry.");
    return bytes;
}

// Atomically replace one complete local entry; failed writes leave the previous entry intact.
void writeDisk(const std::string& directory, const std::string& key, const std::vector<std::byte>& bytes)
{
    const auto path = cachePath(directory, key);
    std::filesystem::create_directories(path.parent_path());
    static std::atomic<uint64_t> sequence{0};
    auto temporary = path;
    temporary += "." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(sequence++) + ".tmp";
    try
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << key << '\n';
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        output.flush();
        if (!output)
            throw EngineError("Terrain cache write failed.");
        output.close();
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw EngineError("Cannot publish terrain cache entry.");
    } catch (...)
    {
        std::error_code error;
        std::filesystem::remove(temporary, error);
        throw;
    }
}
#endif
} // namespace

struct TerrainDataCache::State
{
    struct Task
    {
        RequestId id;
        NodeAddress node;
        std::shared_ptr<const TerrainManifest> manifest;
        bool skip = false;
        std::atomic<bool> cancelled{false};
    };
    std::string baseUrl, directory;
    std::mutex mutex;
    std::condition_variable changed, finished;
    std::map<RequestId, std::shared_ptr<Task>> tasks;
    std::deque<std::shared_ptr<Task>> queued;
    std::vector<TerrainDataResult> results;
    std::array<std::thread, 2> threads;
    std::shared_ptr<StreamingWake> ioWake = std::make_shared<StreamingWake>(), wake;
    bool stopping = false;

    // Perform the straightforward cache algorithm while retaining cancellation and immutable identity.
    TerrainDataResult acquire(const Task& task)
    {
        TerrainDataResult result;
        result.id = task.id;
        result.bypassed = task.skip;
        if (task.cancelled)
        {
            result.cancelled = true;
            return result;
        }
        const auto url = baseUrl + terrainTilePath(*task.manifest, task.node);
        if (!task.skip)
        {
            try
            {
#ifdef __EMSCRIPTEN__
                auto bytes = storage(url, nullptr);
#else
                auto bytes = readDisk(directory, url);
#endif
                if (!bytes.empty())
                {
                    result.tile = decodeTerrainTile(bytes, *task.manifest, task.node);
                    result.hit = true;
                    return result;
                }
            } catch (const std::exception& error)
            {
                result.warning = error.what();
            }
        }
        if (task.cancelled)
        {
            result.cancelled = true;
            return result;
        }
        auto request = HttpRequest::start(url, terrainResponseLimit, task.skip, {}, ioWake);
        HttpResponse response;
        for (;;)
        {
            const auto sequence = ioWake->sequence();
            if (task.cancelled)
            {
                request->cancel();
                result.cancelled = true;
                return result;
            }
            auto completed = request->take();
            if (completed)
            {
                response = std::move(*completed);
                break;
            }
            ioWake->wait(sequence);
        }
        if (!response.error.empty())
            throw EngineError(response.error);
        if (response.status != 200)
            throw EngineError("Terrain HTTP status " + std::to_string(response.status));
        result.tile = decodeTerrainTile(response.bytes, *task.manifest, task.node);
        if (!task.skip && !task.cancelled)
        {
            try
            {
#ifdef __EMSCRIPTEN__
                storage(url, &response.bytes);
#else
                writeDisk(directory, url, response.bytes);
#endif
            } catch (const std::exception& error)
            {
                result.warning = error.what();
            }
        }
        return result;
    }

    // Drain bounded acquisition jobs on a dedicated I/O thread, publishing exactly one outcome per request.
    void run()
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
                if (queued.empty() && stopping)
                    return;
                task = queued.front();
                queued.pop_front();
            }
            TerrainDataResult result;
            result.id = task->id;
            try
            {
                result = acquire(*task);
            } catch (const std::exception& error)
            {
                result.error = error.what();
            }
            if (task->cancelled)
            {
                result.tile.reset();
                result.cancelled = true;
                result.error.clear();
            }
            {
                std::lock_guard lock(mutex);
                results.push_back(std::move(result));
            }
            finished.notify_one();
            if (wake)
                wake->signal();
        }
    }
};

TerrainDataCache::TerrainDataCache(std::string baseUrl, std::string directory, std::shared_ptr<StreamingWake> wake)
    : m_state(std::make_shared<State>())
{
    while (!baseUrl.empty() && baseUrl.back() == '/')
        baseUrl.pop_back();
    if (baseUrl.ends_with("/v1"))
        baseUrl.resize(baseUrl.size() - 3);
    if (baseUrl.find_first_of("\r\n") != std::string::npos)
        throw EngineError("Invalid terrain service URL.");
    m_state->baseUrl = std::move(baseUrl);
    m_state->directory = std::move(directory);
    m_state->wake = std::move(wake);
    try
    {
        for (auto& thread : m_state->threads)
        {
            thread = std::thread(
                [state = m_state]
                {
                    state->run();
                }
            );
#ifdef __EMSCRIPTEN__
            thread.detach();
#endif
        }
    } catch (...)
    {
        {
            std::lock_guard lock(m_state->mutex);
            m_state->stopping = true;
        }
        m_state->changed.notify_all();
        for (auto& thread : m_state->threads)
            if (thread.joinable())
                thread.join();
        throw;
    }
}

TerrainDataCache::~TerrainDataCache()
{
    {
        std::lock_guard lock(m_state->mutex);
        m_state->stopping = true;
        for (auto& [id, task] : m_state->tasks)
            task->cancelled = true;
    }
    m_state->changed.notify_all();
    m_state->ioWake->signal();
    for (auto& thread : m_state->threads)
        if (thread.joinable())
            thread.join();
}

void TerrainDataCache::request(
    RequestId id,
    std::shared_ptr<const TerrainManifest> manifest,
    NodeAddress node,
    bool skipCache
)
{
    if (!manifest)
        throw EngineError("Terrain acquisition requires a pinned manifest.");
    auto task = std::make_shared<State::Task>();
    task->id = id;
    task->manifest = std::move(manifest);
    task->node = terrainTileAddress(node, task->manifest->terminalDepth);
    task->skip = skipCache;
    {
        std::lock_guard lock(m_state->mutex);
        if (m_state->stopping || m_state->tasks.size() >= 64 || m_state->tasks.contains(id))
            throw EngineError("Terrain cache queue is full, stopping or has a duplicate request.");
        m_state->tasks.emplace(id, task);
        try
        {
            m_state->queued.push_back(task);
        } catch (...)
        {
            m_state->tasks.erase(id);
            throw;
        }
    }
    m_state->changed.notify_one();
}

void TerrainDataCache::cancel(RequestId id)
{
    {
        std::lock_guard lock(m_state->mutex);
        if (const auto found = m_state->tasks.find(id); found != m_state->tasks.end())
            found->second->cancelled = true;
    }
    m_state->ioWake->signal();
}

std::vector<TerrainDataResult> TerrainDataCache::takeResults()
{
    std::lock_guard lock(m_state->mutex);
    std::vector<TerrainDataResult> results;
    results.swap(m_state->results);
    for (auto& result : results)
    {
        const auto found = m_state->tasks.find(result.id);
        if (found != m_state->tasks.end() && found->second->cancelled)
        {
            result.tile.reset();
            result.cancelled = true;
            result.error.clear();
        }
        m_state->tasks.erase(result.id);
    }
    return results;
}

bool TerrainDataCache::waitForResult(std::chrono::milliseconds timeout)
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
