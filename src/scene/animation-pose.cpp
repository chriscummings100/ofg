// glTF STEP, shortest-path linear rotation and time-scaled cubic Hermite sampling in local TRS space.
#include "scene/animation-pose.h"
#include "core/engine-error.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace ofg {
namespace {
using Value = std::array<double, 4>;

// Normalizes in double precision so finite float keys/tangents cannot overflow the length calculation.
Value rotation(Value value)
{
    const double length = std::hypot(std::hypot(value[0], value[1]), std::hypot(value[2], value[3]));
    if (!std::isfinite(length) || length == 0)
    {
        throw EngineError("Evaluated animation rotation is zero or non-finite.");
    }
    for (auto& component : value)
    {
        component /= length;
    }
    return value;
}

// Reads one key group (incoming tangent, value or outgoing tangent for cubic tracks).
Value keyValue(const AnimationTrack& track, size_t key, size_t group)
{
    const size_t groups = track.interpolation == AnimationInterpolation::CubicSpline ? 3 : 1;
    const size_t offset = (key * groups + group) * track.componentsPerValue;
    Value value{};
    for (size_t component = 0; component < track.componentsPerValue; ++component)
    {
        value[component] = track.values[offset + component];
    }
    return value;
}

// Interpolates a single validated resource track; keys outside the channel's range hold their endpoint value.
Value sampleTrack(const AnimationTrack& track, double seconds)
{
    const auto& times = track.timesSeconds;
    const bool cubic = track.interpolation == AnimationInterpolation::CubicSpline;
    const size_t valueGroup = cubic ? 1 : 0;
    if (seconds <= times.front())
    {
        return keyValue(track, 0, valueGroup);
    }
    if (seconds >= times.back())
    {
        return keyValue(track, times.size() - 1, valueGroup);
    }
    const size_t right = std::upper_bound(times.begin(), times.end(), seconds) - times.begin();
    const size_t left = right - 1;
    auto a = keyValue(track, left, valueGroup);
    if (track.interpolation == AnimationInterpolation::Step)
    {
        return a;
    }
    auto b = keyValue(track, right, valueGroup);
    const double interval = double(times[right]) - times[left];
    const double t = (seconds - times[left]) / interval;
    Value result{};
    if (cubic)
    {
        const auto outgoing = keyValue(track, left, 2);
        const auto incoming = keyValue(track, right, 0);
        // glTF tangents are derivatives per second, hence the interval factors in the Hermite basis.
        const double t2 = t * t, t3 = t2 * t;
        for (size_t component = 0; component < track.componentsPerValue; ++component)
        {
            result[component] = (2 * t3 - 3 * t2 + 1) * a[component] +
                                (t3 - 2 * t2 + t) * interval * outgoing[component] + (-2 * t3 + 3 * t2) * b[component] +
                                (t3 - t2) * interval * incoming[component];
        }
        return result; // Cubic quaternions are normalized afterward; do not independently flip their signs.
    }

    double weightA = 1 - t, weightB = t;
    if (track.path == AnimationPath::Rotation)
    {
        a = rotation(a);
        b = rotation(b);
        double dot = 0;
        for (size_t component = 0; component < 4; ++component)
        {
            dot += a[component] * b[component];
        }
        if (dot < 0)
        {
            for (auto& component : b)
            {
                component = -component;
            }
            dot = -dot;
        }
        // Near-parallel quaternions use the continuous normalized-linear limit of slerp.
        if (dot < .9995)
        {
            const double angle = std::acos(std::clamp(dot, 0.0, 1.0));
            weightA = std::sin((1 - t) * angle) / std::sin(angle);
            weightB = std::sin(t * angle) / std::sin(angle);
        }
    }
    for (size_t component = 0; component < track.componentsPerValue; ++component)
    {
        result[component] = weightA * a[component] + weightB * b[component];
    }
    return result;
}

// Checks a public pose before any entity writes, including edits by future blending or IK callers.
void validatePose(const AnimationPose& pose)
{
    for (const auto& transform : pose.localTransforms)
    {
        for (float value :
             {transform.position.x,
              transform.position.y,
              transform.position.z,
              transform.scale.x,
              transform.scale.y,
              transform.scale.z})
        {
            if (!std::isfinite(value))
            {
                throw EngineError("Animation pose position and scale must be finite.");
            }
        }
        std::string error;
        if (!math::normalize(transform.rotation, error))
        {
            throw EngineError("Animation pose: " + error);
        }
    }
}
} // namespace

AnimationPose sampleAnimationPose(const Animation& animation, const AnimationPose& restPose, double seconds)
{
    if (!std::isfinite(seconds))
    {
        throw EngineError("Animation sample time must be finite.");
    }
    AnimationPose result = restPose;
    for (const auto& track : animation.desc().tracks)
    {
        try
        {
            if (track.path == AnimationPath::Weights)
            {
                throw EngineError("Morph weight playback is not supported.");
            }
            if (track.node >= result.localTransforms.size())
            {
                throw EngineError("Animation target is outside the pose source-node domain.");
            }
            auto value = sampleTrack(track, seconds);
            auto& transform = result.localTransforms[track.node];
            if (track.path == AnimationPath::Rotation)
            {
                value = rotation(value);
                transform.rotation = {float(value[0]), float(value[1]), float(value[2]), float(value[3])};
            }
            else
            {
                const math::Vec3 vector{float(value[0]), float(value[1]), float(value[2])};
                if (track.path == AnimationPath::Translation)
                {
                    transform.position = vector;
                }
                else
                {
                    transform.scale = vector;
                }
            }
        } catch (const EngineError& error)
        {
            throw EngineError(
                "Animation '" + animation.desc().name + "' node " + std::to_string(track.node) + ": " + error.what()
            );
        }
    }
    validatePose(result);
    return result;
}

void applyAnimationPose(const AnimationPose& pose, std::span<const Ptr<Entity>> nodes)
{
    if (pose.localTransforms.size() != nodes.size())
    {
        throw EngineError("Animation pose and target mapping must have the same source-node domain.");
    }
    validatePose(pose);
    for (size_t index = 0; index < nodes.size(); ++index)
    {
        if (nodes[index])
        {
            nodes[index]->setLocalTransform(pose.localTransforms[index]);
        }
    }
}
} // namespace ofg
