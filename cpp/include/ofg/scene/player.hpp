// Player scene component.
//
// Player owns the first flat-plane movement behavior for the visible player
// entity. The component reads the latest Game-stored controls during update and
// mutates its owning entity transform; it does not own rendering or camera
// state.
#pragma once

#include "ofg/scene/component.hpp"

namespace ofg {

struct SceneUpdateContext;

class Player : public Component {
public:
    // Binds this player to one scene-owned entity.
    explicit Player(Entity* entity) noexcept;

    // Returns the walking speed in world units per second.
    [[nodiscard]] float walk_speed() const noexcept;
    // Returns the fast movement speed in world units per second.
    [[nodiscard]] float fast_speed() const noexcept;
    // Replaces the walking speed after validating it is finite and non-negative.
    void set_walk_speed(float speed);
    // Returns the height used to keep the centered player box grounded.
    [[nodiscard]] float height() const noexcept;
    // Replaces the player height after validating it is finite and positive.
    void set_height(float height);
    // Returns the latest intended flat movement speed in world units per second.
    [[nodiscard]] float current_speed() const noexcept;
    // Applies player-relevant controls for one frame.
    void update(const SceneUpdateContext& context);

private:
    float m_walk_speed{3.5f};
    float m_height{1.8f};
    float m_current_speed{0.0f};
};

} // namespace ofg
