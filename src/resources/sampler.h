// Immutable shared sampling policy; private GPU state is prepared by the graphics owner.
#pragma once
#include "resources/resource.h"
#include <memory>

namespace ofg {
enum class TextureFilter
{
    Nearest,
    Linear
};
enum class TextureAddressMode
{
    Repeat,
    ClampToEdge,
    MirroredRepeat
};
struct SamplerDesc
{
    TextureFilter minFilter = TextureFilter::Linear;
    TextureFilter magFilter = TextureFilter::Linear;
    TextureFilter mipFilter = TextureFilter::Linear;
    TextureAddressMode addressU = TextureAddressMode::Repeat;
    TextureAddressMode addressV = TextureAddressMode::Repeat;
    bool useMipmaps = true;
};
struct SamplerGpuData;
class TextureRenderer;
class Sampler : public Resource
{
public:
    // Creates a validated ready sampler; settings cannot subsequently change.
    static std::shared_ptr<Sampler> create(SamplerDesc desc = {});
    // Returns immutable sampling settings.
    const SamplerDesc& desc() const noexcept { return m_desc; }

private:
    friend class TextureRenderer;
    // Stores validated settings without allocating GPU resources.
    explicit Sampler(SamplerDesc desc)
        : m_desc(desc)
    {
    }
    SamplerDesc m_desc;
    std::shared_ptr<SamplerGpuData> m_gpu;
};
} // namespace ofg
