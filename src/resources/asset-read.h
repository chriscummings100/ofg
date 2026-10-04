// Private cancellable native-file/browser-fetch reader shared by concrete resource loaders.
#pragma once
#include "core/engine-error.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <span>
#include <string>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#endif

namespace ofg {
inline constexpr size_t maxEncodedBytes = 64 * 1024 * 1024;
// Owns one platform read. Callbacks only record completion; decode stays on the resource scheduler.
struct AssetRead
{
    // Creates one unstarted request; callback context must never move or be copied.
    AssetRead() = default;
    AssetRead(const AssetRead&) = delete;
    AssetRead& operator=(const AssetRead&) = delete;
#ifdef __EMSCRIPTEN__
    emscripten_fetch_t* fetch = nullptr;
    bool finished = false;
    bool succeeded = false;
    bool cancelling = false;
    // Records completion without allocation or exceptions crossing the C callback boundary.
    static void complete(emscripten_fetch_t* request)
    {
        auto& load = *static_cast<AssetRead*>(request->userData);
        if (!load.cancelling)
        {
            load.finished = true;
            load.succeeded = request->status >= 200 && request->status < 300;
        }
    }
    // Closes pending or completed I/O; close can synchronously invoke complete().
    ~AssetRead()
    {
        cancelling = true;
        if (fetch)
        {
            emscripten_fetch_close(fetch);
        }
    }
#else
    std::vector<std::byte> bytes;
    std::ifstream file;
    size_t read = 0;
#endif

    // Starts I/O once; the owning resource must retain this state until cancellation/completion.
    void start(const std::string& path)
    {
#ifdef __EMSCRIPTEN__
        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);
        std::strcpy(attr.requestMethod, "GET");
        attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_REPLACE;
        attr.timeoutMSecs = 30000;
        attr.userData = this;
        attr.onsuccess = complete;
        attr.onerror = complete;
        fetch = emscripten_fetch(&attr, path.c_str());
        if (!fetch)
        {
            throw EngineError("Could not start asset fetch.");
        }
#else
        file.open(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
        if (!file)
        {
            throw EngineError("Could not open asset file.");
        }
        const auto size = file.tellg();
        if (size <= 0 || uint64_t(size) > maxEncodedBytes)
        {
            throw EngineError("Invalid asset file size (limit 64 MiB).");
        }
        bytes.resize(size_t(size));
        file.seekg(0);
#endif
    }

    // Advances at most 256 KiB of native reading, or polls the browser callback result.
    bool poll()
    {
#ifdef __EMSCRIPTEN__
        if (!finished)
        {
            return false;
        }
        if (!succeeded)
        {
            throw EngineError("Asset fetch failed, HTTP " + std::to_string(fetch->status));
        }
        if (!fetch->numBytes || fetch->numBytes > maxEncodedBytes)
        {
            throw EngineError("Invalid asset response size (limit 64 MiB).");
        }
#else
        if (read < bytes.size())
        {
            const auto count = std::min(size_t(256 * 1024), bytes.size() - read);
            if (!file.read(reinterpret_cast<char*>(bytes.data() + read), count))
            {
                throw EngineError("Asset file read failed.");
            }
            read += count;
            if (read == bytes.size())
            {
                file.close();
            }
            return false;
        }
#endif
        return true;
    }

    // Borrows completed bytes until this request state is destroyed; poll() must have succeeded.
    std::span<const std::byte> data() const
    {
#ifdef __EMSCRIPTEN__
        return {reinterpret_cast<const std::byte*>(fetch->data), size_t(fetch->numBytes)};
#else
        return bytes;
#endif
    }
};

} // namespace ofg
