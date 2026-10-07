// Four explicit depth passes with conservative caster clipping and material-equivalent mask evaluation.
#include "terrain/terrain-profile.h"
#include "render/shadow-renderer.h"
#include "render/lighting-pass.h"
#include "shadow-shader.h"
#include "math/transform.h"
#include "core/engine-error.h"
#include <cassert>
#include <algorithm>
namespace ofg {
using namespace rhi;
// Material owns these bindings; cursors observe children retained by root. No pass resource is retained.
struct ShadowMaterialGpuData
{
    ComPtr<IShaderObject> root, unboundPass;
    ShaderCursor pass, worldFromLocal;
};

ShadowRenderer::ShadowRenderer(IDevice* device, ICommandQueue* queue, IInputLayout* layout)
    : m_device(device)
    , m_queue(queue)
    , m_layout(layout)
{
    ComPtr<slang::IBlob> diagnostics;
    m_passModule = device->getSlangSession()->loadModuleFromSourceString(
        "ofg_shadow_pass",
        "ofg_shadow_pass.slang",
        shadowPassShader,
        diagnostics.writeRef()
    );
    if (!m_passModule)
    {
        throw EngineError(
            std::string("Compile shadow pass: ") +
            (diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "no diagnostics")
        );
    }
    m_passType = m_passModule->getLayout()->findTypeByName("ShadowPassParameters");
    assert(m_passType);
    resize(1);
    rhi::SamplerDesc desc{};
    desc.reductionOp = TextureReductionOp::Comparison;
    desc.comparisonFunc = ComparisonFunc::LessEqual;
    desc.addressU = desc.addressV = desc.addressW = TextureAddressingMode::ClampToEdge;
    checkLighting(device->createSampler(desc, m_sampler.writeRef()), "Create shadow comparison sampler");
}
ShadowRenderer::~ShadowRenderer()
{
    for (auto& weak : m_materials)
    {
        if (auto data = weak.lock())
        {
            *data = ShadowMaterialGpuData{};
        }
    }
}

void ShadowRenderer::prepareMaterial(const ShadowDraw& draw)
{
    auto& material = *draw.item->material;
    if (material.m_shadowGpu && material.m_shadowGpu->root)
    {
        return;
    }
    OFG_TERRAIN_SCOPE(ShadowRenderer_prepareMaterial);
    auto data = std::make_shared<ShadowMaterialGpuData>();
    checkLighting(
        m_device->createRootShaderObject(m_programs[draw.alphaTexture != nullptr], data->root.writeRef()),
        "Create shadow material bindings"
    );
    ShaderCursor root(data->root);
    data->pass = root["shadowPass"];
    data->unboundPass = data->pass.getDereferenced().m_baseObject;
    data->worldFromLocal = root["shadowDraw"]["worldFromLocal"];
    // The production shader explicitly declares column-major float4x4, matching packMat4.
    assert(data->worldFromLocal.getTypeLayout()->getMatrixLayoutMode() == SLANG_MATRIX_LAYOUT_COLUMN_MAJOR);
    auto properties = root["shadowMaterial"];
    const auto& values = material.uniforms();
    float base = 1, cutoff = .5f;
    uint32_t uv = 0;
    math::Vec3 x{1, 0, 0}, y{0, 1, 0};
    if (material.renderState().pbr)
    {
        base = std::get<math::Vec4>(values.at("baseColor")).w;
        cutoff = std::get<float>(values.at("alphaCutoff"));
        if (draw.alphaTexture)
        {
            x = std::get<math::Vec3>(values.at("baseColorTransformX"));
            y = std::get<math::Vec3>(values.at("baseColorTransformY"));
            uv = std::get<uint32_t>(values.at("baseColorUvSet"));
        }
    }
    const float alpha[]{base, cutoff, float(uv), float(material.renderState().alphaMode == AlphaMode::Mask)};
    const float tx[]{x.x, x.y, x.z, 0}, ty[]{y.x, y.y, y.z, 0};
    checkLighting(properties["alpha"].setData(alpha, sizeof(alpha)), "Bind shadow alpha");
    checkLighting(properties["transformX"].setData(tx, sizeof(tx)), "Bind shadow UV X");
    checkLighting(properties["transformY"].setData(ty, sizeof(ty)), "Bind shadow UV Y");
    if (draw.alphaTexture)
    {
        checkLighting(properties["alphaTexture"].setBinding(draw.alphaTexture), "Bind shadow alpha image");
        checkLighting(properties["alphaSampler"].setBinding(draw.alphaSampler), "Bind shadow alpha sampler");
    }
    checkLighting(properties.getDereferenced().m_baseObject->finalize(), "Finalize shadow material");
    m_materials.push_back(data);
    material.m_shadowGpu = std::move(data);
}

void ShadowRenderer::resize(uint32_t resolution)
{
    rhi::TextureDesc desc{};
    desc.type = TextureType::Texture2DArray;
    desc.arrayLength = 4;
    desc.size = {resolution, resolution, 1};
    desc.format = Format::D32Float;
    desc.usage = TextureUsage::DepthStencil | TextureUsage::ShaderResource | TextureUsage::CopySource;
    desc.defaultState = rhi::ResourceState::DepthWrite;
    ComPtr<ITexture> texture;
    checkLighting(m_device->createTexture(desc, nullptr, texture.writeRef()), "Create shadow depth array");
    m_depth = texture;
}
IRenderPipeline* ShadowRenderer::pipeline(bool texture, bool doubleSided, bool mirrored)
{
    uint32_t key = uint32_t(texture) * 4 + uint32_t(doubleSided) * 2 + uint32_t(mirrored);
    if (m_pipelines[key])
    {
        return m_pipelines[key];
    }
    if (!m_programs[texture])
    {
        std::string source = (texture ? "#define SHADOW_ALPHA_TEXTURE 1\n" : "") + std::string(shadowShader);
        m_programs[texture] =
            lightingProgram(m_device, source.c_str(), texture ? "ofg_shadow_mask" : "ofg_shadow_depth");
    }
    RenderPipelineDesc desc{};
    desc.program = m_programs[texture];
    desc.inputLayout = m_layout;
    desc.depthStencil.format = Format::D32Float;
    desc.depthStencil.depthTestEnable = true;
    desc.depthStencil.depthWriteEnable = true;
    desc.depthStencil.depthFunc = ComparisonFunc::Less;
    desc.rasterizer.cullMode = doubleSided ? CullMode::None : CullMode::Back;
    desc.rasterizer.frontFace = mirrored ? FrontFaceMode::CounterClockwise : FrontFaceMode::Clockwise;
    desc.rasterizer.depthBias = 1;
    desc.rasterizer.slopeScaledDepthBias = 1;
    checkLighting(m_device->createRenderPipeline(desc, m_pipelines[key].writeRef()), "Create shadow pipeline");
    return m_pipelines[key];
}
void ShadowRenderer::render(const DrawList& frame, std::span<const ShadowDraw> draws)
{
    OFG_TERRAIN_SCOPE(ShadowRenderer_render);
    const auto& settings = frame.lighting.outdoor->shadows;
    if (!settings.enabled)
    {
        return;
    }
    if (m_depth->getDesc().size.width != settings.resolution)
    {
        resize(settings.resolution);
    }
    std::erase_if(
        m_materials,
        [](const auto& entry)
        {
            return entry.expired();
        }
    );
    std::vector<Bounds> bounds;
    bounds.reserve(draws.size());
    for (const auto& draw : draws)
    {
        bounds.push_back(worldBounds(draw.item->mesh->bounds(), draw.item->worldFromLocal));
    }
    m_cascades = buildShadowCascades(frame.camera, frame.outdoor->lightDirection, settings, bounds);
    // Pipeline creation and texture preparation are completed before beginning any depth pass.
    for (const auto& draw : draws)
    {
        const auto& w = draw.item->worldFromLocal;
        bool mirrored =
            math::dot(math::cross({w[0].x, w[0].y, w[0].z}, {w[1].x, w[1].y, w[1].z}), {w[2].x, w[2].y, w[2].z}) < 0;
        pipeline(draw.alphaTexture != nullptr, draw.item->material->renderState().doubleSided, mirrored);
        prepareMaterial(draw);
    }
    ComPtr<ICommandEncoder> encoder;
    checkLighting(m_queue->createCommandEncoder(encoder.writeRef()), "Create shadow encoder");
    for (uint32_t layer = 0; layer < 4; ++layer)
    {
        ComPtr<IShaderObject> globals;
        checkLighting(
            m_device->createShaderObject(m_passType, ShaderObjectContainerType::None, globals.writeRef()),
            "Create shadow pass globals"
        );
        setLightingMatrix(ShaderCursor(globals)["clipFromWorld"], m_cascades[layer].clipFromWorld);
        checkLighting(globals->finalize(), "Finalize shadow pass globals");
        auto view = lightingView(m_depth, layer);
        RenderPassDepthStencilAttachment depth{};
        depth.view = view;
        depth.depthLoadOp = LoadOp::Clear;
        depth.depthClearValue = 1;
        RenderPassDesc desc{};
        desc.depthStencilAttachment = &depth;
        auto pass = encoder->beginRenderPass(desc);
        for (const auto& draw : draws)
        {
            const auto& item = *draw.item;
            auto clip = math::mul(m_cascades[layer].clipFromWorld, item.worldFromLocal);
            // Rest bounds cannot reject a caster whose vertices were moved by skinning.
            if (!item.deformationJob && !boundsVisible(item.mesh->bounds(), clip))
            {
                continue;
            }
            const auto& w = item.worldFromLocal;
            bool mirrored =
                math::dot(math::cross({w[0].x, w[0].y, w[0].z}, {w[1].x, w[1].y, w[1].z}), {w[2].x, w[2].y, w[2].z}) <
                0;
            auto p = pipeline(draw.alphaTexture != nullptr, item.material->renderState().doubleSided, mirrored);
            auto& bindings = *item.material->m_shadowGpu;
            auto world = math::packMat4(item.worldFromLocal);
            checkLighting(bindings.worldFromLocal.setData(world.data(), sizeof(world)), "Bind shadow instance");
            checkLighting(bindings.pass.setObject(globals), "Bind shadow pass globals");
            pass->bindPipeline(p, bindings.root);
            RenderState state{};
            state.viewports[0] = Viewport::fromSize(settings.resolution, settings.resolution);
            state.viewportCount = 1;
            state.scissorRects[0] = ScissorRect::fromSize(settings.resolution, settings.resolution);
            state.scissorRectCount = 1;
            state.vertexBuffers[0].buffer = draw.vertices;
            state.vertexBufferCount = 1;
            state.indexBuffer.buffer = draw.indices;
            state.indexFormat = IndexFormat::Uint32;
            pass->setRenderState(state);
            const auto& part = item.mesh->subMeshes()[item.subMeshIndex];
            DrawArguments args{};
            args.vertexCount = part.indexCount;
            args.startIndexLocation = part.firstIndex;
            pass->drawIndexed(args);
            // RHI snapshots at draw; restore the original child because setObject disallows null.
            checkLighting(bindings.pass.setObject(bindings.unboundPass), "Release shadow pass globals");
        }
        pass->end();
    }
    submitLighting(m_queue, encoder);
}
} // namespace ofg
