// CPU-side opaque instance lifetime handle; only the deformation renderer knows its GPU allocation layout.
#pragma once
#include <memory>

namespace ofg {
struct DeformationGpuData;
struct DeformationStorage
{
    std::shared_ptr<DeformationGpuData> gpu;
};
} // namespace ofg
