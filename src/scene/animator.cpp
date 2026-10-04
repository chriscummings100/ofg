// Validates instance bindings and advances double-precision clip transport through the local-pose boundary.
#include "scene/animator.h"
#include "scene/entity.h"
#include "scene/mesh-renderer.h"
#include "core/engine-error.h"
#include <unordered_set>
#include <utility>
#include <algorithm>
#include <cmath>

namespace ofg {
Animator::Animator(Entity* entity) noexcept
    : Component(entity)
{
}

void Animator::setBindings(std::vector<std::shared_ptr<Animation>> animations, std::vector<Ptr<Entity>> nodes)
{
    std::unordered_set<const Entity*> uniqueNodes;
    for (const auto& node : nodes)
    {
        if (!node)
        {
            continue;
        }
        if (!uniqueNodes.insert(node.get()).second)
        {
            throw EngineError("Animator source nodes must map to distinct entities.");
        }
        const Entity* ancestor = node.get();
        while (ancestor && ancestor != entity())
        {
            ancestor = ancestor->parent();
        }
        if (!ancestor)
        {
            throw EngineError("Animator targets must belong to its root subtree.");
        }
    }
    for (const auto& animation : animations)
    {
        if (!animation || !animation->isLoaded())
        {
            throw EngineError("Animator requires non-null ready Animation resources.");
        }
        for (const auto& track : animation->desc().tracks)
        {
            if (track.node >= nodes.size())
            {
                throw EngineError("Animation target is outside the source node mapping.");
            }
            if (nodes[track.node] && track.path == AnimationPath::Weights)
            {
                const auto renderer = nodes[track.node]->meshRenderer();
                if (!renderer || !renderer->mesh() ||
                    renderer->mesh()->deformation().morphTargets.size() != track.componentsPerValue)
                {
                    throw EngineError("Animation weight target requires a MeshRenderer with matching morph targets.");
                }
            }
        }
    }
    AnimationPose rest;
    rest.localTransforms.reserve(nodes.size());
    for (const auto& node : nodes)
    {
        rest.localTransforms.push_back(node ? node->localTransform() : LocalTransform{});
    }
    m_restPose = std::move(rest);
    m_selected.reset();
    m_timeSeconds = 0;
    m_speed = 1;
    m_playing = false;
    m_looping = true;
    m_animations = std::move(animations);
    m_nodes = std::move(nodes);
}

void Animator::evaluate(size_t index, double seconds)
{
    auto pose = sampleAnimationPose(*m_animations[index], m_restPose, seconds);
    applyAnimationPose(pose, m_nodes);
}

void Animator::selectAnimation(size_t index)
{
    if (index >= m_animations.size())
    {
        throw EngineError("Animator clip index is out of range: " + std::to_string(index));
    }
    evaluate(index, 0);
    m_selected = index;
    m_timeSeconds = 0;
}

double Animator::durationSeconds() const noexcept
{
    return m_selected ? m_animations[*m_selected]->durationSeconds() : 0;
}

void Animator::seek(double seconds)
{
    if (!m_selected || !std::isfinite(seconds))
    {
        throw EngineError("Animator seek requires a selected clip and finite seconds.");
    }
    const double time = std::clamp(seconds, 0.0, durationSeconds());
    evaluate(*m_selected, time);
    m_timeSeconds = time;
}

void Animator::play()
{
    if (!m_selected)
    {
        throw EngineError("Animator play requires a selected clip.");
    }
    if (!m_looping && m_timeSeconds >= durationSeconds())
    {
        seek(0);
    }
    m_playing = true;
}

void Animator::stop()
{
    if (m_selected)
    {
        seek(0);
    }
    m_playing = false;
}

void Animator::setPlaybackSpeed(float speed)
{
    if (!std::isfinite(speed) || speed < 0)
    {
        throw EngineError("Animator playback speed must be finite and nonnegative.");
    }
    m_speed = speed;
}

void Animator::update(double deltaSeconds)
{
    if (!m_playing || deltaSeconds == 0 || m_speed == 0)
    {
        return;
    }
    const double duration = durationSeconds();
    double next = 0;
    bool ended = false;
    if (duration > 0 && m_looping)
    {
        // Reduce before multiplying, so even DBL_MAX deltas with large finite speeds cannot overflow.
        const double step = std::fmod(deltaSeconds, duration / double(m_speed)) * double(m_speed);
        next = std::fmod(m_timeSeconds + step, duration);
    }
    else if (deltaSeconds >= (duration - m_timeSeconds) / double(m_speed))
    {
        next = duration;
        ended = !m_looping;
    }
    else
    {
        next = m_timeSeconds + deltaSeconds * double(m_speed);
    }
    evaluate(*m_selected, next);
    m_timeSeconds = next;
    if (ended)
    {
        m_playing = false;
    }
}
} // namespace ofg
