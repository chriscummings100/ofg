// Shared application-thread frame driver; hosts own events, graphics initialization and presentation.
#pragma once

#include "scene/scene.h"
#include "state/state.h"

namespace rhi {
class ITexture;
}

namespace ofg {
class TerrainLaboratory;
class Game
{
public:
    Game() = delete;
    // Takes a non-null scene and creates the root state; rejects double initialization.
    static void initialize(std::unique_ptr<Scene> scene);
    // Returns the current scene; requires initialization.
    static Scene& scene();
    // Replaces scene ownership immediately, including during State::update; rejects null.
    static void setScene(std::unique_ptr<Scene> scene);
    // Returns the root state used to own application modes; requires initialization.
    static State& state();
    // Returns the most recently supplied frame interval in seconds.
    static float deltaSeconds() noexcept;
    // Takes the optional terrain inspection mode after scene initialization.
    static void setTerrain(std::unique_ptr<TerrainLaboratory> terrain);
    // Returns the active terrain inspection mode, or null for other fixtures.
    static TerrainLaboratory* terrain() noexcept;
    // Updates resources, state, animations and world transforms, then renders; null target still advances updates.
    static void frame(float deltaSeconds, rhi::ITexture* colorTarget);
    // Releases state and scene without synthesizing leave hooks; may be called repeatedly outside a frame.
    static void shutdown();
};
} // namespace ofg
