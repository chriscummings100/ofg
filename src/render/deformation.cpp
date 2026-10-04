// Shared immutable influence upload and queue-ordered per-instance palette/vertex deformation.
#include "render/deformation.h"
#include "render/resource-gpu-data.h"
#include "core/engine-error.h"
#include "deformation-shader.h"
#include <slang-rhi/shader-cursor.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <type_traits>

namespace ofg {
struct DeformationGpuData
{
    rhi::ComPtr<rhi::IBuffer> vertices;
    rhi::ComPtr<rhi::IBuffer> palette;
};
namespace {
struct PackedInfluence
{
    uint32_t joints[4];
    float weights[4];
};
static_assert(std::is_standard_layout_v<Vertex> && sizeof(Vertex) == 72 && sizeof(float) == 4);
static_assert(offsetof(Vertex, position) == 0 && offsetof(Vertex, normal) == 12);
static_assert(offsetof(Vertex, uv) == 24 && offsetof(Vertex, tangent) == 32);
static_assert(offsetof(Vertex, uv1) == 48 && offsetof(Vertex, color) == 56);
static_assert(sizeof(PackedInfluence) == 32 && offsetof(PackedInfluence, weights) == 16);
static_assert(sizeof(std::array<float, 16>) == 64);

// Reports failed RHI operations without substituting a CPU deformation path.
void check(rhi::Result result, const char* operation)
{
    if (SLANG_FAILED(result))
    {
        throw EngineError(std::string(operation) + " failed: " + std::to_string(result));
    }
}
} // namespace

DeformationRenderer::DeformationRenderer(rhi::IDevice* device, rhi::ICommandQueue* queue)
    : m_device(device)
    , m_queue(queue)
{
}

DeformationRenderer::~DeformationRenderer()
{
    for (auto& weak : m_instances)
    {
        if (auto instance = weak.lock())
        {
            instance->gpu.reset();
        }
    }
}

void DeformationRenderer::initializePipeline()
{
    if (m_pipeline)
    {
        return;
    }
    const auto& limits = m_device->getInfo().limits;
    if (limits.maxComputeThreadsPerGroup < 64 || limits.maxComputeThreadGroupSize[0] < 64)
    {
        throw EngineError("Skinning requires compute groups of 64 threads.");
    }
    rhi::ComPtr<slang::IBlob> diagnostics;
    auto module = m_device->getSlangSession()->loadModuleFromSourceString(
        "ofg_deformation",
        "ofg-deformation.slang",
        deformationShader,
        diagnostics.writeRef()
    );
    if (!module)
    {
        throw EngineError(
            "Skinning shader: " +
            std::string(
                diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "module compilation failed"
            )
        );
    }
    rhi::ComPtr<slang::IEntryPoint> entry;
    check(module->findEntryPointByName("computeMain", entry.writeRef()), "Find skinning compute entry");
    slang::IComponentType* entries[]{entry};
    rhi::ShaderProgramDesc desc{};
    desc.linkingStyle = rhi::LinkingStyle::SingleProgram;
    desc.slangGlobalScope = module;
    desc.slangEntryPoints = entries;
    desc.slangEntryPointCount = 1;
    rhi::ComPtr<rhi::IShaderProgram> program;
    const auto result = m_device->createShaderProgram(desc, program.writeRef(), diagnostics.writeRef());
    if (SLANG_FAILED(result))
    {
        throw EngineError(
            "Skinning program: " +
            std::string(diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "linking failed")
        );
    }
    rhi::ComputePipelineDesc pipeline{};
    pipeline.program = program;
    check(m_device->createComputePipeline(pipeline, m_pipeline.writeRef()), "Create skinning pipeline");
}

void DeformationRenderer::prepare(const DeformationJob& job)
{
    if (!job.mesh || !job.skin || !job.storage || !job.mesh->m_gpu ||
        job.palette.size() != job.skin->desc().joints.size())
    {
        throw EngineError("Deformation job requires prepared mesh, skin, instance storage and a complete palette.");
    }
    const auto& sets = job.mesh->deformation().influenceSets;
    const size_t count = job.mesh->vertices().size();
    const uint64_t vertexBytes = uint64_t(count) * sizeof(Vertex);
    const uint64_t influenceBytes = uint64_t(count) * sets.size() * sizeof(PackedInfluence);
    const uint64_t paletteBytes = uint64_t(job.palette.size()) * 64;
    // RHI exposes maxBufferSize, not maxStorageBufferBindingSize. Use the guaranteed WebGPU 128 MiB
    // binding baseline on both hosts until RHI exposes the actual storage binding limit.
    const uint64_t limit = std::min<uint64_t>(m_device->getInfo().limits.maxBufferSize, 128ull * 1024 * 1024);
    if (!count || sets.empty() || vertexBytes > limit || influenceBytes > limit || paletteBytes > limit ||
        (count + 63) / 64 > m_device->getInfo().limits.maxComputeDispatchThreadGroups[0])
    {
        throw EngineError("Skinning mesh exceeds portable buffer/dispatch limits or has no influences.");
    }
    auto& mesh = *job.mesh->m_gpu;
    if (!mesh.influences)
    {
        std::vector<PackedInfluence> packed;
        packed.reserve(count * sets.size());
        for (const auto& set : sets)
        {
            for (const auto& influence : set)
            {
                packed.push_back(
                    {{influence.joints[0], influence.joints[1], influence.joints[2], influence.joints[3]},
                     {influence.weights.x, influence.weights.y, influence.weights.z, influence.weights.w}}
                );
            }
        }
        // Normalize all sets together in double precision before upload. The shader still sums/normalizes
        // the packed floats, whose rounding may leave a total slightly different from one.
        for (size_t vertex = 0; vertex < count; ++vertex)
        {
            double total = 0;
            for (size_t set = 0; set < sets.size(); ++set)
            {
                for (float weight : packed[set * count + vertex].weights)
                {
                    total += weight;
                }
            }
            if (!(total > 0))
            {
                throw EngineError("Skinning vertex has no positive weight.");
            }
            for (size_t set = 0; set < sets.size(); ++set)
            {
                for (float& weight : packed[set * count + vertex].weights)
                {
                    weight = float(double(weight) / total);
                }
            }
        }
        rhi::BufferDesc desc{};
        desc.size = influenceBytes;
        desc.usage = rhi::BufferUsage::ShaderResource;
        desc.defaultState = rhi::ResourceState::ShaderResource;
        check(m_device->createBuffer(desc, packed.data(), mesh.influences.writeRef()), "Upload skinning influences");
    }
    if (!job.storage->gpu)
    {
        auto data = std::make_shared<DeformationGpuData>();
        rhi::BufferDesc desc{};
        desc.size = vertexBytes;
        desc.usage = rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::VertexBuffer | rhi::BufferUsage::CopySource;
        desc.defaultState = rhi::ResourceState::UnorderedAccess;
        check(m_device->createBuffer(desc, nullptr, data->vertices.writeRef()), "Allocate private skinned vertices");
        desc.size = paletteBytes;
        desc.usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination;
        desc.defaultState = rhi::ResourceState::ShaderResource;
        check(m_device->createBuffer(desc, nullptr, data->palette.writeRef()), "Allocate skin palette");
        std::erase_if(
            m_instances,
            [](const auto& weak)
            {
                return weak.expired();
            }
        );
        m_instances.push_back(job.storage);
        job.storage->gpu = std::move(data);
    }
}

void DeformationRenderer::deform(std::span<const DeformationJob> jobs)
{
    if (jobs.empty())
    {
        return;
    }
    initializePipeline();
    // All creation/initial uploads finish before encoding the dependent work.
    for (const auto& job : jobs)
    {
        prepare(job);
    }
    rhi::ComPtr<rhi::ICommandEncoder> encoder;
    check(m_queue->createCommandEncoder(encoder.writeRef()), "Create deformation encoder");
    for (const auto& job : jobs)
    {
        auto& data = *job.storage->gpu;
        std::vector<std::array<float, 16>> palette;
        palette.reserve(job.palette.size());
        for (const auto& matrix : job.palette)
        {
            palette.push_back(math::packMat4(matrix));
        }
        check(encoder->uploadBufferData(data.palette, 0, palette.size() * 64, palette.data()), "Upload skin palette");
        auto pass = encoder->beginComputePass();
        auto cursor = rhi::ShaderCursor(pass->bindPipeline(m_pipeline));
        check(cursor["sourceVertices"].setBinding(job.mesh->m_gpu->vertices), "Bind skin source vertices");
        check(cursor["influences"].setBinding(job.mesh->m_gpu->influences), "Bind skin influences");
        check(cursor["palette"].setBinding(data.palette), "Bind skin palette");
        check(cursor["outputVertices"].setBinding(data.vertices), "Bind skinned output");
        pass->dispatchCompute(uint32_t((job.mesh->vertices().size() + 63) / 64), 1, 1);
        pass->end();
        encoder->setBufferState(data.vertices, rhi::ResourceState::VertexBuffer);
    }
    rhi::ComPtr<rhi::ICommandBuffer> commands;
    check(encoder->finish(commands.writeRef()), "Finish deformation commands");
    check(m_queue->submit(commands), "Submit deformation commands");
}

rhi::IBuffer* DeformationRenderer::vertices(const DeformationStorage& storage)
{
    if (!storage.gpu)
    {
        throw EngineError("Deformed vertices have not been prepared.");
    }
    return storage.gpu->vertices;
}
} // namespace ofg
