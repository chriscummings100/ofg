// Validates the deliberately small portable sampler policy before GPU preparation.
#include "resources/sampler.h"
#include "core/engine-error.h"

namespace ofg {
std::shared_ptr<Sampler> Sampler::create(SamplerDesc desc)
{
    for (auto filter : {desc.minFilter, desc.magFilter, desc.mipFilter})
    {
        if (filter != TextureFilter::Nearest && filter != TextureFilter::Linear)
        {
            throw EngineError("Invalid texture filter.");
        }
    }
    for (auto address : {desc.addressU, desc.addressV})
    {
        if (address != TextureAddressMode::Repeat && address != TextureAddressMode::ClampToEdge)
        {
            throw EngineError("Invalid texture address mode.");
        }
    }
    return std::shared_ptr<Sampler>(new Sampler(desc));
}
} // namespace ofg
