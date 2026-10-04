// Independent clip transport and rest-pose bindings; clips remain shared and immutable.
#pragma once

#include "scene/component.h"
#include "resources/animation.h"
#include "scene/animation-pose.h"
#include <optional>
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
    // Weight tracks require a matching morph renderer, but cannot be selected for playback yet.
    // Captures current target locals as rest pose; clears selection, pauses, resets time/speed/looping.
    void setBindings(std::vector<std::shared_ptr<Animation>> animations, std::vector<Ptr<Entity>> nodes);
    // Returns immutable clip handles without copying keyframes.
    std::span<const std::shared_ptr<Animation>> animations() const noexcept { return m_animations; }
    // Returns target observers indexed by source Model node, not runtime EntityId.
    std::span<const Ptr<Entity>> nodes() const noexcept { return m_nodes; }
    // Selects, rewinds and samples a TRS clip while preserving play/pause; failure preserves current state/pose.
    void selectAnimation(size_t index);
    // Resumes the selected clip; restarts an ended non-looping clip. Requires a selection.
    void play();
    // Holds the current pose and time.
    void pause() noexcept { m_playing = false; }
    // Pauses and samples the selected clip at zero; no selection is a no-op.
    void stop();
    // Clamps finite seconds to the selected clip and samples immediately, preserving play/pause.
    void seek(double seconds);
    // Changes end behavior without changing time or the current pose.
    void setLooping(bool looping) noexcept { m_looping = looping; }
    // Sets finite, nonnegative advancement speed; zero freezes time.
    void setPlaybackSpeed(float speed);
    // Returns the selection, or no value after construction/rebinding.
    std::optional<size_t> selectedAnimation() const noexcept { return m_selected; }
    // Returns the current double-precision time in seconds.
    double timeSeconds() const noexcept { return m_timeSeconds; }
    // Returns selected clip duration in seconds, or zero without a selection.
    double durationSeconds() const noexcept;
    // Reports whether time advancement is enabled.
    bool isPlaying() const noexcept { return m_playing; }
    // Reports whether advancement wraps at the end.
    bool isLooping() const noexcept { return m_looping; }
    // Returns the nonnegative time multiplier.
    float playbackSpeed() const noexcept { return m_speed; }

private:
    friend class Scene;
    // Creates an unbound component on its model instance root.
    explicit Animator(Entity* entity) noexcept;
    // Samples and publishes a pose without changing transport state until validation succeeds.
    void evaluate(size_t index, double seconds);
    // Advances transport and samples after state updates; Scene validates the finite nonnegative delta.
    void update(double deltaSeconds);
    std::vector<std::shared_ptr<Animation>> m_animations;
    std::vector<Ptr<Entity>> m_nodes;
    AnimationPose m_restPose;
    std::optional<size_t> m_selected;
    double m_timeSeconds = 0;
    float m_speed = 1;
    bool m_playing = false;
    bool m_looping = true;
};
} // namespace ofg
