// Static application-thread loading service; the lookup dictionary never owns assets.
#pragma once

#include "core/engine-error.h"
#include "resources/resource.h"

#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace ofg {

class Resources
{
public:
    Resources() = delete;

    // Returns a live cached asset or a new pending one; loading starts on update().
    // T derives from Resource and is constructible from a string key.
    template<typename T>
    [[nodiscard]] static std::shared_ptr<T> loadResourceAsync(const std::string& key)
    {
        static_assert(std::is_base_of_v<Resource, T>, "Resources requires a Resource-derived type.");
        if (key.empty())
        {
            throw EngineError("Resource keys must not be empty.");
        }

        auto found = s_resources.find(key);
        if (found != s_resources.end())
        {
            if (auto existing = found->second.lock())
            {
                auto typed = std::dynamic_pointer_cast<T>(existing);
                if (!typed)
                {
                    throw EngineError("Resource key already belongs to a different type: " + key);
                }
                return typed;
            }
        }

        auto resource = std::make_shared<T>(key);
        s_resources[key] = resource;
        return resource;
    }

#ifndef __EMSCRIPTEN__
    // Native tools only: pumps the same scheduler until this resource succeeds or fails.
    // Loading steps must make progress through update(); do not wait on an unpumped host loop.
    template<typename T>
    [[nodiscard]] static std::shared_ptr<T> loadResource(const std::string& key)
    {
        if (s_updating)
        {
            throw EngineError("Blocking resource loads cannot run inside a loading step.");
        }
        auto resource = loadResourceAsync<T>(key);
        while (!resource->isFinished())
        {
            update();
        }
        return resource;
    }
#endif

    // Advances each pending asset once and removes expired cache entries; call once per frame.
    // Dependencies requested during a step begin on the next update. Reentry is rejected.
    static void update();

private:
    static std::unordered_map<std::string, std::weak_ptr<Resource>> s_resources;
    static bool s_updating;
};

} // namespace ofg
