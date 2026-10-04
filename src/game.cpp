// Explicit shared frame order and ownership; graphics calls remain outside the GPU-independent core.
#include "game.h"
#include "resources/resources.h"
#include "render/graphics.h"
#include "core/engine-error.h"
#include <cmath>

namespace ofg {
namespace {
std::unique_ptr<Scene> currentScene;
std::unique_ptr<State> rootState;
float frameSeconds{0};
bool inFrame{false};

// Resets the reentry guard even when a resource, state hook or graphics operation throws.
struct FrameGuard
{
    // Releases the current frame guard.
    ~FrameGuard() { inFrame = false; }
};
} // namespace

void Game::initialize(std::unique_ptr<Scene> scene)
{
    if (rootState || !scene)
    {
        throw EngineError("Game requires one initialization with a scene.");
    }
    rootState = std::make_unique<State>();
    currentScene = std::move(scene);
    frameSeconds = 0;
}

Scene& Game::scene()
{
    if (!currentScene)
    {
        throw EngineError("Game is not initialized.");
    }
    return *currentScene;
}

void Game::setScene(std::unique_ptr<Scene> scene)
{
    if (!rootState || !scene)
    {
        throw EngineError("Game::setScene requires initialization and a scene.");
    }
    currentScene = std::move(scene);
}

State& Game::state()
{
    if (!rootState)
    {
        throw EngineError("Game is not initialized.");
    }
    return *rootState;
}

float Game::deltaSeconds() noexcept
{
    return frameSeconds;
}

void Game::frame(float deltaSeconds, rhi::ITexture* colorTarget)
{
    if (!rootState || inFrame || !std::isfinite(deltaSeconds) || deltaSeconds < 0)
    {
        throw EngineError("Game::frame requires initialization, a finite nonnegative delta and no reentry.");
    }
    inFrame = true;
    FrameGuard guard;
    frameSeconds = deltaSeconds;
    Resources::update();
    rootState->update();
    currentScene->update();
    if (currentScene->lighting.outdoor)
    {
        advanceOutdoorLighting(*currentScene->lighting.outdoor, deltaSeconds);
    }
    if (colorTarget)
    {
        DrawList list;
        if (auto camera = currentScene->activeCamera())
        {
            const auto size = colorTarget->getDesc().size;
            list = buildDrawList(*currentScene, *camera, float(size.width) / float(size.height));
        }
        Graphics::render(list, colorTarget);
    }
}

void Game::shutdown()
{
    if (inFrame)
    {
        throw EngineError("Game::shutdown cannot destroy the executing state tree.");
    }
    rootState.reset();
    currentScene.reset();
    frameSeconds = 0;
}
} // namespace ofg
