// Animation key-layout and timing validation without playback, scene or graphics dependencies.
#include "resources/animation.h"
#include "core/engine-error.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace ofg {
namespace {
// Validates one channel independently of its eventual Model/scene target and returns its last key time.
float validateTrack(const AnimationTrack& track)
{
    uint32_t expectedComponents = 0;
    switch (track.path)
    {
    case AnimationPath::Translation:
    case AnimationPath::Scale:
        expectedComponents = 3;
        break;
    case AnimationPath::Rotation:
        expectedComponents = 4;
        break;
    case AnimationPath::Weights:
        expectedComponents = track.componentsPerValue;
        break;
    default:
        throw EngineError("Unknown animation path.");
    }
    if (!expectedComponents || track.componentsPerValue != expectedComponents)
    {
        throw EngineError("Animation value width does not match its target property.");
    }

    size_t groupsPerKey = 1;
    switch (track.interpolation)
    {
    case AnimationInterpolation::Step:
    case AnimationInterpolation::Linear:
        break;
    case AnimationInterpolation::CubicSpline:
        groupsPerKey = 3;
        break;
    default:
        throw EngineError("Unknown animation interpolation.");
    }
    if (track.timesSeconds.empty() || (groupsPerKey == 3 && track.timesSeconds.size() < 2))
    {
        throw EngineError("Animation requires keys; cubic interpolation requires at least two.");
    }
    float previousTime = -1;
    for (float time : track.timesSeconds)
    {
        if (!std::isfinite(time) || time < 0 || time <= previousTime)
        {
            throw EngineError("Animation times must be finite, nonnegative and strictly increasing.");
        }
        previousTime = time;
    }

    // Divide rather than multiply untrusted counts; this also handles 32-bit browser size_t.
    const size_t width = track.componentsPerValue;
    if (track.values.size() % width != 0 || (track.values.size() / width) % groupsPerKey != 0 ||
        track.values.size() / width / groupsPerKey != track.timesSeconds.size())
    {
        throw EngineError("Animation output count does not match key times, value width and interpolation.");
    }
    for (float value : track.values)
    {
        if (!std::isfinite(value))
        {
            throw EngineError("Animation outputs and cubic tangents must be finite.");
        }
    }
    if (track.path == AnimationPath::Rotation)
    {
        // A zero quaternion cannot describe a rotation. Keep signs, magnitudes and derivative tangents intact.
        for (size_t key = 0; key < track.timesSeconds.size(); ++key)
        {
            const size_t valueOffset = (key * groupsPerKey + (groupsPerKey == 3 ? 1 : 0)) * width;
            bool nonzero = false;
            for (size_t component = 0; component < width; ++component)
            {
                nonzero = nonzero || track.values[valueOffset + component] != 0;
            }
            if (!nonzero)
            {
                throw EngineError("Animation rotation key cannot be a zero quaternion.");
            }
        }
    }
    return track.timesSeconds.back();
}
} // namespace

Animation::Animation(AnimationDesc desc, float durationSeconds)
    : m_desc(std::move(desc))
    , m_durationSeconds(durationSeconds)
{
}

std::shared_ptr<Animation> Animation::create(AnimationDesc desc)
{
    if (desc.tracks.empty())
    {
        throw EngineError("Animation requires at least one track.");
    }
    std::set<std::pair<uint32_t, AnimationPath>> targets;
    float duration = 0;
    for (size_t i = 0; i < desc.tracks.size(); ++i)
    {
        const auto& track = desc.tracks[i];
        try
        {
            if (!targets.emplace(track.node, track.path).second)
            {
                throw EngineError("Duplicate animation node/property target.");
            }
            duration = std::max(duration, validateTrack(track));
        } catch (const EngineError& error)
        {
            throw EngineError("Animation '" + desc.name + "' track " + std::to_string(i) + ": " + error.what());
        }
    }
    return std::shared_ptr<Animation>(new Animation(std::move(desc), duration));
}
} // namespace ofg
