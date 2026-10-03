// Cooperative loading with weak scheduling and no global owning resource registry.
#include "resources/resources.h"

#include <vector>

namespace ofg {

std::unordered_map<std::string, std::weak_ptr<Resource>> Resources::s_resources;
bool Resources::s_updating = false;

void Resources::update()
{
    if (s_updating)
    {
        throw EngineError("Resources::update cannot run recursively.");
    }

    // Keep the guard exception-safe without introducing a general scope-guard helper.
    struct UpdateGuard
    {
        bool& updating;
        // Marks the scheduler active for this scope.
        explicit UpdateGuard(bool& flag)
            : updating(flag)
        {
            updating = true;
        }
        // Restores scheduler eligibility on every exit path.
        ~UpdateGuard() { updating = false; }
    } guard(s_updating);

    // A load step may insert dependency keys and rehash the dictionary. Iterate a snapshot.
    // Keep it weak so abandoning another pending resource does not extend that asset's lifetime.
    std::vector<std::weak_ptr<Resource>> pending;
    for (auto iterator = s_resources.begin(); iterator != s_resources.end();)
    {
        if (auto resource = iterator->second.lock())
        {
            if (!resource->isFinished())
            {
                pending.push_back(resource);
            }
            ++iterator;
        }
        else
        {
            iterator = s_resources.erase(iterator);
        }
    }

    for (const auto& weak : pending)
    {
        if (auto resource = weak.lock())
        {
            resource->advanceLoading();
        }
    }
}

} // namespace ofg
