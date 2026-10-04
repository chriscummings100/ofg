// Retains an imported Model while observing one scene-owned instance of its selected hierarchy.
#pragma once
#include "resources/model.h"
#include "scene/animator.h"

namespace ofg {
struct ModelInstance
{
    std::shared_ptr<Model> model;
    Ptr<Entity> root; // Synthetic placement root; releasing this record does not remove it from Scene.
    Ptr<Animator> animator;
    std::vector<Ptr<Entity>> nodes; // Source node indices, null outside the selected model scene.
};
} // namespace ofg
