// Direct clip-source name mapping to an existing model instance; deliberately does not retarget transforms.
#pragma once
#include "scene/model-instance.h"

namespace ofg {
// Requires unique nonempty names where present and matching parent relationships for mapped nodes.
// Every animated source node must resolve; unmatched unanimated source nodes remain null. No keyframes are copied.
std::vector<Ptr<Entity>> mapAnimationNodesByName(const ModelData& source, const ModelInstance& target);
} // namespace ofg
