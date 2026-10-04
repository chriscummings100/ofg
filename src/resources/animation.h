// Immutable model-local animation tracks; Animator components supply instance entity bindings.
#pragma once

#include "resources/resource.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace ofg {
enum class AnimationPath
{
    Translation,
    Rotation,
    Scale,
    Weights
};

enum class AnimationInterpolation
{
    Step,
    Linear,
    CubicSpline
};

struct AnimationTrack
{
    uint32_t node = 0; // Index in the originating Model, not a scene EntityId.
    AnimationPath path = AnimationPath::Translation;
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    std::vector<float> timesSeconds;
    uint32_t componentsPerValue = 3;
    // Key-major scalars; cubic keys contain incoming tangent, value, outgoing tangent groups.
    // Quaternion components are XYZW. Tangents are derivatives per second, never normalized.
    std::vector<float> values;
};

struct AnimationDesc
{
    std::string name;
    std::vector<AnimationTrack> tracks;
};

class Animation : public Resource
{
public:
    // Creates a ready uncached clip with finite data, increasing nonnegative times and valid key layouts.
    // Requires at least one track and a unique node/property pair per track. Does not alter key values.
    static std::shared_ptr<Animation> create(AnimationDesc desc);
    // Returns the immutable tracks, with targets in the originating Model's node domain.
    const AnimationDesc& desc() const noexcept { return m_desc; }
    // Returns the final key time in seconds; the clip timeline begins at zero.
    float durationSeconds() const noexcept { return m_durationSeconds; }

private:
    // Takes validated tracks and their computed end time.
    Animation(AnimationDesc desc, float durationSeconds);
    AnimationDesc m_desc;
    float m_durationSeconds = 0;
};
} // namespace ofg
