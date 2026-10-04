// GPU-independent local animation poses; sampling and entity publication are separate operations.
#pragma once

#include "resources/animation.h"
#include "scene/entity.h"
#include "core/ptr.h"
#include <span>

namespace ofg {
struct AnimationPose
{
    std::vector<LocalTransform> localTransforms; // Indexed by the clip's source node domain.
};

// Samples immutable TRS tracks over a copy of the target rest pose, clamping each track to its key range.
// Time must be finite. Rejects weights, out-of-domain targets and non-finite/zero evaluated rotations.
// Does not access entities or produce matrices; callers may blend or modify the returned local pose.
AnimationPose sampleAnimationPose(const Animation& animation, const AnimationPose& restPose, double seconds);

// Validates the entire pose before publishing local transforms. Null mapping entries denote excluded model nodes.
// The caller owns mapping validity/lifetime; pose and mapping must have the same source-node domain.
void applyAnimationPose(const AnimationPose& pose, std::span<const Ptr<Entity>> nodes);
} // namespace ofg
