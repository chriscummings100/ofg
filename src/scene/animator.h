// Passive animation bindings: shared clips target observed entities beneath this component's root.
#pragma once

#include "scene/component.h"
#include "resources/animation.h"
#include <span>

namespace ofg {
class Scene;

class Animator : public Component
{
public:
    // Releases shared clips and observed targets; no playback or leave hooks are run.
    ~Animator() override = default;
    // Atomically binds ready clips to source-indexed nodes in this root's subtree.
    // Null entries represent nodes outside the selected Model scene. Clips and mapping must use the same
    // source node domain; this validates shape and membership, not name matching or retargeting.
    // Weight tracks require a target MeshRenderer with the matching morph count. Empty vectors clear bindings.
    void setBindings(std::vector<std::shared_ptr<Animation>> animations, std::vector<Ptr<Entity>> nodes);
    // Returns immutable clip handles. The component performs no playback yet.
    std::span<const std::shared_ptr<Animation>> animations() const noexcept { return m_animations; }
    // Returns target observers indexed by source Model node, not runtime EntityId.
    std::span<const Ptr<Entity>> nodes() const noexcept { return m_nodes; }

private:
    friend class Scene;
    // Creates an unbound component on its model instance root.
    explicit Animator(Entity* entity) noexcept;
    std::vector<std::shared_ptr<Animation>> m_animations;
    std::vector<Ptr<Entity>> m_nodes;
};
} // namespace ofg
