// Shared asynchronous model inspection mode; owns the pending resource and its scene observer record.
#pragma once
#include "scene/scene.h"

namespace ofg {
// Creates the camera and lighting used for undeformed model inspection, before asset loading finishes.
std::unique_ptr<Scene> createModelFixtureScene();

class ModelFixture
{
public:
    // Requests one model through the cooperative resource scheduler.
    explicit ModelFixture(const std::string& path);
    // Instantiates once after loading; throws the asset diagnostic on failure. Call before rendering.
    void update(Scene& scene);
    // Reports successful instantiation, rather than merely completed I/O.
    bool ready() const noexcept { return bool(m_instance.root); }
    // Describes pending/loaded state, counts and the intentionally undeformed preview.
    const std::string& status() const noexcept { return m_status; }

private:
    std::shared_ptr<Model> m_model;
    ModelInstance m_instance;
    std::string m_status = "Loading model...";
};
} // namespace ofg
