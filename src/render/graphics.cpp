// Direct RHI scene rendering, reflected material binding, and explicit service/asset GPU ownership.
#include "render/graphics.h"
#include "render/resource-gpu-data.h"
#include "render/environment-renderer.h"
#include "render/sky-renderer.h"
#include "render/shadow-renderer.h"
#include "render/texture-renderer.h"
#include "render/deformation.h"
#include "core/engine-error.h"
#include "math/transform.h"
#include "pbr-output-shader.h"
#include <cmath>
#include <chrono>
#include <cstdio>
#include <slang-rhi/shader-cursor.h>

#include <algorithm>
#include <array>
#include <map>
#include <type_traits>

namespace ofg {
namespace {
using namespace rhi;

struct PipelineEntry
{
    Format colorFormat;
    MaterialState state;
    bool mirrored = false;
    bool fullscreen = false;
    ComPtr<IRenderPipeline> pipeline;
};

struct GraphicsState
{
    ComPtr<IDevice> device;
    ComPtr<ICommandQueue> queue;
    ComPtr<IInputLayout> inputLayout, pbrInputLayout;
    ComPtr<ITexture> depth;
    ComPtr<ITexture> hdr, display;
    std::shared_ptr<Shader> outputShader;
    std::unique_ptr<EnvironmentRenderer> environments;
    std::unique_ptr<SkyRenderer> sky;
    std::unique_ptr<ShadowRenderer> shadows;
    ComPtr<ITexture> aerialFallback;
    ComPtr<ISampler> environmentSampler;
    // Pipeline identity includes every variable fixed-function state.
    std::map<std::weak_ptr<Shader>, std::vector<PipelineEntry>, std::owner_less<std::weak_ptr<Shader>>> pipelines;
    std::vector<std::weak_ptr<MeshGpuData>> meshes;
    std::vector<std::weak_ptr<ShaderGpuData>> shaders;
    std::unique_ptr<TextureRenderer> textures;
    std::unique_ptr<DeformationRenderer> deformation;
    size_t pipelineCreations{0};
};
std::unique_ptr<GraphicsState> graphics;

// Converts an RHI failure into the application's exception-based diagnostic path.
void check(Result result, const std::string& operation)
{
    if (SLANG_FAILED(result))
    {
        throw EngineError(operation + " failed (RHI result " + std::to_string(result) + ").");
    }
}

// Includes compiler diagnostics in errors rather than discarding the original source explanation.
std::string diagnosticText(slang::IBlob* diagnostics)
{
    return diagnostics
               ? std::string(static_cast<const char*>(diagnostics->getBufferPointer()), diagnostics->getBufferSize())
               : std::string{};
}

// Checks a reflected direct field against the deliberately small public UniformValue variant.
bool uniformMatches(slang::TypeLayoutReflection* layout, const UniformValue& value)
{
    using Type = slang::TypeReflection;
    const auto kind = layout->getKind();
    const auto scalar = layout->getType()->getScalarType();
    return std::visit(
        [&](const auto& item)
        {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, float>)
            {
                return kind == Type::Kind::Scalar && scalar == Type::ScalarType::Float32;
            }
            else if constexpr (std::is_same_v<T, int32_t>)
            {
                return kind == Type::Kind::Scalar && scalar == Type::ScalarType::Int32;
            }
            else if constexpr (std::is_same_v<T, uint32_t>)
            {
                return kind == Type::Kind::Scalar && scalar == Type::ScalarType::UInt32;
            }
            else if constexpr (std::is_same_v<T, math::Mat4>)
            {
                return kind == Type::Kind::Matrix && scalar == Type::ScalarType::Float32 &&
                       layout->getType()->getRowCount() == 4 && layout->getType()->getColumnCount() == 4;
            }
            else
            {
                constexpr size_t components = std::is_same_v<T, math::Vec2> ? 2 : std::is_same_v<T, math::Vec3> ? 3 : 4;
                return kind == Type::Kind::Vector && scalar == Type::ScalarType::Float32 &&
                       layout->getType()->getElementCount() == components;
            }
        },
        value
    );
}

// Packs a column-vector matrix for the reflected Slang storage layout, including row-major declarations.
void bindMatrix(ShaderCursor cursor, const math::Mat4& value)
{
    auto layout = cursor.getTypeLayout();
    if (!layout || layout->getKind() != slang::TypeReflection::Kind::Matrix || layout->getType()->getRowCount() != 4 ||
        layout->getType()->getColumnCount() != 4 ||
        layout->getType()->getScalarType() != slang::TypeReflection::ScalarType::Float32)
    {
        throw EngineError("Transform binding requires float4x4.");
    }
    auto packed = math::packMat4(value);
    if (layout->getMatrixLayoutMode() == SLANG_MATRIX_LAYOUT_ROW_MAJOR)
    {
        for (size_t row = 0; row < 4; ++row)
        {
            for (size_t column = 0; column < 4; ++column)
            {
                packed[row * 4 + column] = value[column][row];
            }
        }
    }
    check(cursor.setData(packed.data(), sizeof(packed)), "Bind matrix");
}

// Writes scalar/vector values using explicit arrays so C++ member layout is not a shader ABI assumption.
void bindUniform(ShaderCursor cursor, const UniformValue& value)
{
    std::visit(
        [&](const auto& item)
        {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, math::Mat4>)
            {
                bindMatrix(cursor, item);
            }
            else if constexpr (std::is_same_v<T, math::Vec2>)
            {
                const float data[]{item.x, item.y};
                check(cursor.setData(data, sizeof(data)), "Bind float2");
            }
            else if constexpr (std::is_same_v<T, math::Vec3>)
            {
                const float data[]{item.x, item.y, item.z};
                check(cursor.setData(data, sizeof(data)), "Bind float3");
            }
            else if constexpr (std::is_same_v<T, math::Vec4>)
            {
                const auto data = math::packVec4(item);
                check(cursor.setData(data.data(), sizeof(data)), "Bind float4");
            }
            else
            {
                check(cursor.setData(item), "Bind scalar");
            }
        },
        value
    );
}

// Requires an exact set of supported direct material fields, checking every name and type before submission.
void bindMaterial(IShaderObject* root, const Material& material)
{
    auto cursor = ShaderCursor(root)["material"];
    if (!cursor.isValid())
    {
        throw EngineError("Shader " + material.shader()->name() + " has no material block.");
    }
    auto kind = cursor.getTypeLayout()->getKind();
    if (kind == slang::TypeReflection::Kind::ConstantBuffer || kind == slang::TypeReflection::Kind::ParameterBlock)
    {
        ShaderCursor dereferenced;
        check(cursor.getDereferenced(dereferenced), "Access material block");
        cursor = dereferenced;
    }
    auto layout = cursor.getTypeLayout();
    if (layout->getKind() != slang::TypeReflection::Kind::Struct)
    {
        throw EngineError("Shader material block must be a struct.");
    }
    for (const auto& [name, value] : material.uniforms())
    {
        auto field = cursor[name.c_str()];
        if (!field.isValid())
        {
            throw EngineError("Unknown material uniform: " + name);
        }
        if (!uniformMatches(field.getTypeLayout(), value))
        {
            throw EngineError("Unsupported or mismatched material uniform: " + name);
        }
        bindUniform(field, value);
    }
    for (unsigned int index = 0; index < layout->getFieldCount(); ++index)
    {
        const char* name = layout->getFieldByIndex(index)->getName();
        if (!material.uniforms().contains(name))
        {
            throw EngineError(std::string("Missing material uniform: ") + name);
        }
    }
}

// Validates and binds direct sampled resources, preserving the uniform-only shader contract.
void bindResources(IShaderObject* root, const Material& material)
{
    ShaderCursor cursor(root);
    for (const auto& [name, binding] : material.textures())
    {
        auto field = cursor[name.c_str()];
        auto type = field.isValid() ? field.getTypeLayout()->getType() : nullptr;
        if (!type || type->getKind() != slang::TypeReflection::Kind::Resource ||
            type->getResourceShape() != SLANG_TEXTURE_2D || type->getResourceAccess() != SLANG_RESOURCE_ACCESS_READ)
        {
            throw EngineError("Expected a read-only Texture2D binding: " + name);
        }
        auto result = type->getResourceResultType();
        auto scalar = result->getScalarType();
        if (scalar == slang::TypeReflection::ScalarType::None && result->getElementType())
        {
            scalar = result->getElementType()->getScalarType();
        }
        if (scalar != slang::TypeReflection::ScalarType::Float32)
        {
            throw EngineError("Texture requires float shader elements: " + name);
        }
        auto view = std::holds_alternative<TextureView>(binding) ? std::get<TextureView>(binding)
                                                                 : bindingTexture(binding)->defaultView();
        check(field.setBinding(graphics->textures->prepare(view)), "Bind texture " + name);
    }
    for (const auto& [name, sampler] : material.samplers())
    {
        auto field = cursor[name.c_str()];
        if (!field.isValid() || field.getTypeLayout()->getKind() != slang::TypeReflection::Kind::SamplerState)
        {
            throw EngineError("Expected a SamplerState binding: " + name);
        }
        check(field.setBinding(graphics->textures->prepare(*sampler)), "Bind sampler " + name);
    }
    auto layout = cursor.getTypeLayout();
    for (unsigned int index = 0; index < layout->getFieldCount(); ++index)
    {
        std::string name = layout->getFieldByIndex(index)->getName();
        if (name != "draw" && name != "material" &&
            !(material.renderState().pbr &&
              (name == "outdoor" || name == "shadowMaps" || name == "shadowSampler" || name == "aerialPerspective" ||
               name == "frame" || name == "filteredEnvironment" || name == "brdfLookup" ||
               name == "environmentSampler")) &&
            !material.textures().contains(name) && !material.samplers().contains(name))
        {
            throw EngineError("Missing or unsupported shader resource binding: " + name);
        }
    }
}

// Creates only the fixed pipeline variants required by the shader and target format.
IRenderPipeline* pipelineFor(
    const std::shared_ptr<Shader>& shader,
    IShaderProgram* program,
    Format format,
    MaterialState state = {},
    bool mirrored = false,
    bool fullscreen = false
)
{
    auto& variants = graphics->pipelines[shader];
    for (const auto& entry : variants)
    {
        if (entry.colorFormat == format && entry.state == state && entry.mirrored == mirrored &&
            entry.fullscreen == fullscreen)
        {
            return entry.pipeline;
        }
    }
    ColorTargetDesc target{};
    target.format = format;
    target.enableBlend = state.alphaMode == AlphaMode::Blend;
    target.color = {BlendFactor::SrcAlpha, BlendFactor::InvSrcAlpha, BlendOp::Add};
    target.alpha = {BlendFactor::One, BlendFactor::InvSrcAlpha, BlendOp::Add};
    RenderPipelineDesc desc{};
    desc.program = program;
    desc.inputLayout =
        fullscreen ? nullptr : (state.pbr ? graphics->pbrInputLayout.get() : graphics->inputLayout.get());
    desc.targets = &target;
    desc.targetCount = 1;
    desc.depthStencil.format = fullscreen ? Format::Undefined : Format::D32Float;
    desc.depthStencil.depthTestEnable = !fullscreen;
    desc.depthStencil.depthWriteEnable = !fullscreen && state.alphaMode != AlphaMode::Blend;
    desc.depthStencil.depthFunc = ComparisonFunc::Less;
    desc.rasterizer.cullMode = fullscreen || state.doubleSided ? CullMode::None : CullMode::Back;
    desc.rasterizer.frontFace = mirrored ? FrontFaceMode::CounterClockwise : FrontFaceMode::Clockwise;
    ComPtr<IRenderPipeline> pipeline;
    auto start = std::chrono::steady_clock::now();
    check(graphics->device->createRenderPipeline(desc, pipeline.writeRef()), "Create scene pipeline");
    double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (state.pbr || fullscreen)
    {
        std::printf(
            "OFG pipeline %s: %.2f ms, format %u, alpha %u, mirrored %u.\n",
            shader->name().c_str(),
            milliseconds,
            unsigned(format),
            unsigned(state.alphaMode),
            unsigned(mirrored)
        );
    }
    variants.push_back({format, state, mirrored, fullscreen, pipeline});
    ++graphics->pipelineCreations;
    return pipeline;
}
// Classifies lit opaque, unlit opaque and shared sorted transparency before encoding.
int stageFor(const Material& material, bool hdr)
{
    if (!hdr)
    {
        return 0;
    }
    if (material.renderState().alphaMode == AlphaMode::Blend)
    {
        return 2;
    }
    return material.renderState().unlit ? 1 : 0;
}
// Binds explicit outdoor values and valid fallback resources for ordinary studio materials.
void bindOutdoor(IShaderObject* root, const DrawList& list, uint32_t width, uint32_t height)
{
    auto cursor = ShaderCursor(root), o = cursor["outdoor"];
    const auto set = [&](const char* name, math::Vec4 value)
    {
        bindUniform(o[name], value);
    };
    auto forward = math::transformDirection(list.camera.worldFromView, {0, 0, 1});
    auto camera = list.cameraPosition;
    set("viewPositionNear", {camera.x, camera.y, camera.z, list.camera.nearDistance});
    set("viewForwardFar", {forward.x, forward.y, forward.z, 0});
    set("viewport", {float(width), float(height), 0, 0});
    set("enabledExposure", {0, 1, 0, 0});
    set("shadowOptions", {1, .1f, 0, 0});
    set("ibl", {0, 0, 1, 0});
    set("iblExposure", {1, 1, 0, 0});
    set("cloudSettings", {});
    set("windLayers", {});
    set("lightDirection", {0, 1, 0, 10000});
    math::Vec4 splits{}, texels{};
    if (list.outdoor)
    {
        const auto& settings = *list.lighting.outdoor;
        const auto& shadow = settings.shadows;
        const auto& c = settings.clouds;
        const auto& f = *list.outdoor;
        set("viewport", {float(width), float(height), float(settings.atmosphere.aerialPerspective), 0});
        set("enabledExposure", {1, f.exposureMultiplier * list.lighting.exposure, shadow.depthBias, shadow.normalBias});
        set("shadowOptions",
            {float(shadow.resolution), shadow.transition, float(shadow.enabled), float(shadow.debugView)});
        set("lightDirection",
            {f.lightDirection.x, f.lightDirection.y, f.lightDirection.z, settings.atmosphere.aerialDistance});
        set("cloudSettings", {c.coverage, c.opticalThickness, c.scale, 0});
        float wind = float(std::fmod(settings.dayCycle.elapsedSeconds * c.windSpeed, 1000000.0));
        set("windLayers", {std::cos(c.windAngle) * wind, std::sin(c.windAngle) * wind, c.altitude, c.secondAltitude});
        auto blend = graphics->environments->blend();
        set("ibl", {blend.x, blend.y, blend.z, 0});
        auto scales = graphics->environments->exposureScales(list);
        set("iblExposure", {scales.x, scales.y, 0, 0});
        for (uint32_t i = 0; i < 4; ++i)
        {
            const auto& cascade = graphics->shadows->cascades()[i];
            bindMatrix(o["shadowMatrices"][i], cascade.clipFromWorld);
            splits[i] = cascade.farDistance;
            texels[i] = cascade.texelSize;
        }
    }
    else
    {
        for (uint32_t i = 0; i < 4; ++i)
        {
            bindMatrix(o["shadowMatrices"][i], math::mat4Identity());
        }
    }
    set("splitDistances", splits);
    set("texelSizes", texels);
    check(cursor["shadowMaps"].setBinding(graphics->shadows->texture()), "Bind shadow array");
    check(cursor["shadowSampler"].setBinding(graphics->shadows->sampler()), "Bind shadow comparisons");
    check(
        cursor["aerialPerspective"].setBinding(
            list.outdoor ? graphics->sky->aerialPerspective() : graphics->aerialFallback.get()
        ),
        "Bind aerial perspective"
    );
}
// Binds an immutable lighting snapshot, packed environment filters and the BRDF lookup to a PBR draw.
void bindFrame(
    IShaderObject* root,
    const DrawList& list,
    const Material& material,
    bool display,
    const EnvironmentGpuData& env
)
{
    ShaderCursor cursor(root), frame = cursor["frame"];
    bindUniform(frame["cameraPosition"], list.cameraPosition);
    bindUniform(frame["lightCount"], list.lighting.lightCount);
    for (uint32_t i = 0; i < 4; ++i)
    {
        const auto& light = list.lighting.lights[i];
        auto field = frame["lights"][i];
        if (i < list.lighting.lightCount &&
            (light.range < 0 || light.intensity < 0 || light.innerCone < 0 || light.outerCone <= 0 ||
             light.outerCone > 1.57079633f || light.innerCone > light.outerCone || uint32_t(light.type) > 2 ||
             !std::isfinite(
                 light.intensity + light.range + light.innerCone + light.outerCone + light.position.x +
                 light.position.y + light.position.z + light.direction.x + light.direction.y + light.direction.z +
                 light.color.x + light.color.y + light.color.z
             )))
        {
            throw EngineError("Invalid punctual light.");
        }
        float length = std::sqrt(math::lengthSquared(light.direction));
        if (i < list.lighting.lightCount && length < 1e-6f)
        {
            throw EngineError("Light direction must be nonzero.");
        }
        auto direction = length > 0 ? math::mul(light.direction, 1.0f / length) : math::Vec3{0, 0, 1};
        bindUniform(
            field["positionRange"],
            math::Vec4{light.position.x, light.position.y, light.position.z, light.range}
        );
        bindUniform(field["directionType"], math::Vec4{direction.x, direction.y, direction.z, float(light.type)});
        bindUniform(field["colorIntensity"], math::Vec4{light.color.x, light.color.y, light.color.z, light.intensity});
        bindUniform(field["coneCosines"], math::Vec2{std::cos(light.innerCone), std::cos(light.outerCone)});
    }
    // Until startup capture completes, render direct lighting and sky without unrelated baked studio fill.
    bindUniform(
        frame["environmentIntensity"],
        list.outdoor && !graphics->environments->hasDynamic() ? 0.0f : list.lighting.environmentIntensity
    );
    bindUniform(frame["environmentRotation"], list.lighting.environmentRotation);
    bindUniform(frame["environmentMips"], float(env.cubes->getDesc().mipCount));
    bindUniform(frame["exposure"], list.outdoor ? 1.0f : list.lighting.exposure);
    bindUniform(frame["alphaMode"], uint32_t(material.renderState().alphaMode));
    bindUniform(frame["displayOutput"], uint32_t(display));
    bindUniform(frame["encodeSrgb"], uint32_t(0));
    bindUniform(frame["linearOutput"], uint32_t(list.lighting.linearOutput));
    bindUniform(frame["debugView"], list.lighting.debugView);
    check(cursor["filteredEnvironment"].setBinding(env.cubes), "Bind filtered IBL");
    check(cursor["brdfLookup"].setBinding(env.lookup), "Bind BRDF lookup");
    check(cursor["environmentSampler"].setBinding(graphics->environmentSampler), "Bind IBL sampler");
}
// Runs mapping/encoding into host-owned textures before the browser acquires its canvas image.
void outputPass(
    const std::shared_ptr<Shader>& shader,
    IShaderProgram* program,
    ITexture* source,
    ITexture* target,
    float exposure,
    bool linear,
    bool encode,
    uint32_t debug
)
{
    auto pipeline = pipelineFor(shader, program, target->getDesc().format, {}, false, true);
    ComPtr<IShaderObject> root;
    check(graphics->device->createRootShaderObject(program, root.writeRef()), "Create output bindings");
    ShaderCursor cursor(root);
    check(cursor["sourceImage"].setBinding(source), "Bind HDR source");
    bindUniform(cursor["outputSettings"]["exposure"], exposure);
    bindUniform(cursor["outputSettings"]["linearOutput"], uint32_t(linear));
    bindUniform(cursor["outputSettings"]["encodeSrgb"], uint32_t(encode));
    bindUniform(cursor["outputSettings"]["debugView"], debug);
    ComPtr<ICommandEncoder> encoder;
    check(graphics->queue->createCommandEncoder(encoder.writeRef()), "Create output encoder");
    RenderPassColorAttachment color{};
    color.view = target->getDefaultView();
    RenderPassDesc desc{};
    desc.colorAttachments = &color;
    desc.colorAttachmentCount = 1;
    auto pass = encoder->beginRenderPass(desc);
    pass->bindPipeline(pipeline, root);
    RenderState state{};
    auto size = target->getDesc().size;
    state.viewports[0] = Viewport::fromSize(size.width, size.height);
    state.viewportCount = 1;
    state.scissorRects[0] = ScissorRect::fromSize(size.width, size.height);
    state.scissorRectCount = 1;
    pass->setRenderState(state);
    DrawArguments draw{};
    draw.vertexCount = 3;
    pass->draw(draw);
    pass->end();
    ComPtr<ICommandBuffer> commands;
    check(encoder->finish(commands.writeRef()), "Finish output commands");
    check(graphics->queue->submit(commands), "Submit output commands");
}
} // namespace

void Graphics::initialize(rhi::IDevice* device, rhi::ICommandQueue* queue)
{
    if (graphics || !device || !queue)
    {
        throw EngineError("Graphics requires one explicit initialization with a device and queue.");
    }
    auto state = std::make_unique<GraphicsState>();
    state->device = device;
    state->queue = queue;
    state->environments = std::make_unique<EnvironmentRenderer>(device, queue);
    state->deformation = std::make_unique<DeformationRenderer>(device, queue);
    const rhi::VertexStreamDesc stream{sizeof(Vertex), rhi::InputSlotClass::PerVertex, 0};
    rhi::InputElementDesc elements[] = {
        {"POSITION", 0, rhi::Format::RGB32Float, offsetof(Vertex, position), 0},
        {"NORMAL", 0, rhi::Format::RGB32Float, offsetof(Vertex, normal), 0},
        {"TEXCOORD", 0, rhi::Format::RG32Float, offsetof(Vertex, uv), 0},
        {"TANGENT", 0, rhi::Format::RGBA32Float, offsetof(Vertex, tangent), 0},
        {"TEXCOORD", 1, rhi::Format::RG32Float, offsetof(Vertex, uv1), 0},
        {"COLOR", 0, rhi::Format::RGBA32Float, offsetof(Vertex, color), 0}
    };
    rhi::InputLayoutDesc desc{};
    desc.inputElements = elements;
    desc.inputElementCount = 6;
    desc.vertexStreams = &stream;
    desc.vertexStreamCount = 1;
    check(device->createInputLayout(desc, state->inputLayout.writeRef()), "Create mesh vertex layout");
    // The pinned Slang WGSL emitter derives locations from numeric semantic indices.
    // Preserve the existing diagnostic ABI while assigning all six PBR inputs explicitly.
    for (uint32_t i = 0; i < 6; ++i)
    {
        elements[i].semanticIndex = i;
    }
    check(device->createInputLayout(desc, state->pbrInputLayout.writeRef()), "Create PBR vertex layout");
    state->textures = std::make_unique<TextureRenderer>(device, queue);
    state->sky = std::make_unique<SkyRenderer>(device, queue);
    state->shadows = std::make_unique<ShadowRenderer>(device, queue, state->pbrInputLayout);
    rhi::TextureDesc fallback{};
    fallback.type = TextureType::Texture2DArray;
    fallback.arrayLength = 1;
    fallback.size = {1, 1, 1};
    fallback.format = Format::RGBA16Float;
    fallback.usage = TextureUsage::ShaderResource;
    uint16_t zero[4]{};
    SubresourceData fallbackPixels{zero, 8, 8};
    check(device->createTexture(fallback, &fallbackPixels, state->aerialFallback.writeRef()), "Create aerial fallback");
    state->outputShader = Shader::create("pbr-output", pbrOutputShader);
    FormatSupport support{};
    check(device->getFormatSupport(Format::RGBA16Float, &support), "Query HDR format support");
    std::printf(
        "OFG PBR device: %s, RGBA16F sampled=%u renderable=%u; portable texture/sampler budget=16/16.\n",
        device->getInfo().adapterName ? device->getInfo().adapterName : "browser-selected",
        unsigned((support & FormatSupport::ShaderSample) != FormatSupport::None),
        unsigned((support & FormatSupport::RenderTarget) != FormatSupport::None)
    );
    rhi::SamplerDesc sampler{};
    sampler.addressU = sampler.addressV = sampler.addressW = TextureAddressingMode::ClampToEdge;
    check(device->createSampler(sampler, state->environmentSampler.writeRef()), "Create IBL sampler");
    graphics = std::move(state);
}

void Graphics::prepareMesh(Mesh& mesh)
{
    if (mesh.m_gpu && mesh.m_gpu->vertices)
    {
        if (mesh.m_gpu->incrementalUpload && (mesh.m_gpu->uploadedVertices != mesh.vertices().size_bytes() ||
                                              mesh.m_gpu->uploadedIndices != mesh.indices().size_bytes()))
        {
            throw EngineError("A partially uploaded streaming mesh reached rendering.");
        }
        return;
    }
    auto data = std::make_shared<MeshGpuData>();
    rhi::BufferDesc desc{};
    desc.size = mesh.vertices().size_bytes();
    desc.usage = rhi::BufferUsage::VertexBuffer;
    if (!mesh.deformation().influenceSets.empty())
    {
        desc.usage |= rhi::BufferUsage::ShaderResource;
    }
    desc.defaultState = rhi::ResourceState::VertexBuffer;
    check(
        graphics->device->createBuffer(desc, mesh.vertices().data(), data->vertices.writeRef()),
        "Upload mesh vertices"
    );
    desc.size = mesh.indices().size_bytes();
    desc.usage = rhi::BufferUsage::IndexBuffer;
    desc.defaultState = rhi::ResourceState::IndexBuffer;
    check(graphics->device->createBuffer(desc, mesh.indices().data(), data->indices.writeRef()), "Upload mesh indices");
    graphics->meshes.push_back(data);
    mesh.m_gpu = std::move(data);
}

size_t Graphics::streamingMeshGpuBytes(const Mesh& mesh) noexcept
{
    return mesh.m_gpu ? mesh.vertices().size_bytes() + mesh.indices().size_bytes() : 0;
}

bool Graphics::prepareStreamingMesh(Mesh& mesh, size_t& allowance)
{
    if (!graphics)
    {
        throw EngineError("Streaming mesh preparation requires Graphics initialization.");
    }
    for (const auto& subMesh : mesh.subMeshes())
    {
        prepareShader(*subMesh.material->shader());
    }
    if (!mesh.m_gpu)
    {
        auto data = std::make_shared<MeshGpuData>();
        rhi::BufferDesc desc{};
        desc.size = mesh.vertices().size_bytes();
        desc.usage = rhi::BufferUsage::VertexBuffer | rhi::BufferUsage::CopyDestination;
        desc.defaultState = rhi::ResourceState::VertexBuffer;
        check(graphics->device->createBuffer(desc, nullptr, data->vertices.writeRef()), "Allocate streaming vertices");
        desc.size = mesh.indices().size_bytes();
        desc.usage = rhi::BufferUsage::IndexBuffer | rhi::BufferUsage::CopyDestination;
        desc.defaultState = rhi::ResourceState::IndexBuffer;
        check(graphics->device->createBuffer(desc, nullptr, data->indices.writeRef()), "Allocate streaming indices");
        data->incrementalUpload = true;
        graphics->meshes.push_back(data);
        mesh.m_gpu = std::move(data);
    }
    auto& data = *mesh.m_gpu;
    if (!data.incrementalUpload)
    {
        return true;
    }
    // Chunks are four-byte aligned for both backends. Each encoder owns its upload staging until completion.
    for (unsigned part = 0; part < 2 && allowance >= 4; ++part)
    {
        const size_t total = part == 0 ? mesh.vertices().size_bytes() : mesh.indices().size_bytes();
        size_t& uploaded = part == 0 ? data.uploadedVertices : data.uploadedIndices;
        const size_t bytes = std::min(total - uploaded, allowance & ~size_t(3));
        if (!bytes)
        {
            continue;
        }
        const auto* source = part == 0 ? reinterpret_cast<const std::byte*>(mesh.vertices().data())
                                       : reinterpret_cast<const std::byte*>(mesh.indices().data());
        rhi::ComPtr<rhi::ICommandEncoder> encoder;
        check(graphics->queue->createCommandEncoder(encoder.writeRef()), "Create streaming upload encoder");
        check(
            encoder->uploadBufferData(
                part == 0 ? data.vertices.get() : data.indices.get(),
                uploaded,
                bytes,
                source + uploaded
            ),
            "Upload streaming mesh chunk"
        );
        auto commands = encoder->finish();
        if (!commands)
        {
            throw EngineError("Finish streaming mesh upload failed.");
        }
        check(graphics->queue->submit(commands), "Submit streaming mesh upload");
        uploaded += bytes;
        allowance -= bytes;
    }
    return data.uploadedVertices == mesh.vertices().size_bytes() && data.uploadedIndices == mesh.indices().size_bytes();
}

void Graphics::prepareShader(Shader& shader)
{
    if (shader.m_gpu && shader.m_gpu->program)
    {
        return;
    }
    rhi::ComPtr<slang::IBlob> diagnostics;
    // Slang caches modules by name within a session. Distinct Shader objects may use the same diagnostic filename.
    static uint64_t nextModuleId = 0;
    const std::string moduleName = "ofg_mesh_" + std::to_string(nextModuleId++);
    auto module = graphics->device->getSlangSession()->loadModuleFromSourceString(
        moduleName.c_str(),
        (moduleName + "-" + shader.name()).c_str(),
        shader.source().c_str(),
        diagnostics.writeRef()
    );
    if (!module)
    {
        throw EngineError("Compile " + shader.name() + ": " + diagnosticText(diagnostics));
    }
    rhi::ComPtr<slang::IEntryPoint> vertex, fragment;
    check(module->findEntryPointByName(shader.vertexEntry().c_str(), vertex.writeRef()), "Find vertex entry");
    check(module->findEntryPointByName(shader.fragmentEntry().c_str(), fragment.writeRef()), "Find fragment entry");
    slang::IComponentType* entries[]{vertex, fragment};
    rhi::ShaderProgramDesc desc{};
    desc.linkingStyle = rhi::LinkingStyle::SingleProgram;
    desc.slangGlobalScope = module;
    desc.slangEntryPoints = entries;
    desc.slangEntryPointCount = 2;
    auto data = std::make_shared<ShaderGpuData>();
    const auto result = graphics->device->createShaderProgram(desc, data->program.writeRef(), diagnostics.writeRef());
    check(result, "Create shader " + shader.name() + ": " + diagnosticText(diagnostics));
    if (shader.name().starts_with("pbr-"))
    {
        ComPtr<IShaderObject> root;
        check(graphics->device->createRootShaderObject(data->program, root.writeRef()), "Reflect PBR resources");
        auto layout = ShaderCursor(root).getTypeLayout();
        unsigned textures = 0, samplers = 0;
        for (unsigned i = 0; i < layout->getFieldCount(); ++i)
        {
            auto kind = layout->getFieldByIndex(i)->getTypeLayout()->getKind();
            textures += kind == slang::TypeReflection::Kind::Resource;
            samplers += kind == slang::TypeReflection::Kind::SamplerState;
        }
        if (textures > 16 || samplers > 16)
        {
            throw EngineError("Reflected PBR layout exceeds portable 16/16 resource budget.");
        }
        std::printf("OFG layout %s: %u sampled textures, %u samplers.\n", shader.name().c_str(), textures, samplers);
    }
    graphics->shaders.push_back(data);
    shader.m_gpu = std::move(data);
}


void Graphics::render(const DrawList& list, rhi::ITexture* colorTarget)
{
    using namespace rhi;
    if (!graphics || !colorTarget)
    {
        throw EngineError("Graphics::render requires initialization and a color target.");
    }
    if (list.outdoor && (!list.lighting.hdr || !list.lighting.outdoor || !list.lighting.environment))
    {
        throw EngineError("Outdoor rendering requires HDR, outdoor settings and an environment BRDF lookup.");
    }
    const auto& target = colorTarget->getDesc();
    if (target.type != TextureType::Texture2D || target.size.width == 0 || target.size.height == 0 ||
        target.sampleCount != 1 || target.arrayLength != 1 ||
        (target.usage & TextureUsage::RenderTarget) == TextureUsage::None)
    {
        throw EngineError("Scene target must be a nonempty single-sample 2D render target.");
    }
    std::erase_if(
        graphics->pipelines,
        [](const auto& entry)
        {
            return entry.first.expired();
        }
    );
    std::erase_if(
        graphics->meshes,
        [](const auto& entry)
        {
            return entry.expired();
        }
    );
    std::erase_if(
        graphics->shaders,
        [](const auto& entry)
        {
            return entry.expired();
        }
    );

    if (!graphics->depth || graphics->depth->getDesc().size.width != target.size.width ||
        graphics->depth->getDesc().size.height != target.size.height)
    {
        rhi::TextureDesc desc{};
        desc.type = TextureType::Texture2D;
        desc.format = Format::D32Float;
        desc.size = target.size;
        desc.usage = TextureUsage::DepthStencil;
        desc.defaultState = rhi::ResourceState::DepthWrite;
        ComPtr<ITexture> depth;
        check(graphics->device->createTexture(desc, nullptr, depth.writeRef()), "Create scene depth");
        graphics->depth = depth;
    }

    for (const auto& job : list.deformations)
    {
        if (!job.mesh)
        {
            throw EngineError("Deformation job requires a mesh.");
        }
        prepareMesh(*job.mesh);
    }
    graphics->deformation->deform(list.deformations);

    // Scene and shadow passes consume the same per-instance output from this extraction snapshot.
    const auto drawVertices = [&](const DrawItem& item) -> IBuffer*
    {
        if (!item.deformationJob)
        {
            return item.mesh->m_gpu->vertices;
        }
        if (*item.deformationJob >= list.deformations.size() ||
            list.deformations[*item.deformationJob].mesh != item.mesh)
        {
            throw EngineError("Draw item references an invalid deformation job.");
        }
        return DeformationRenderer::vertices(*list.deformations[*item.deformationJob].storage);
    };

    // Uploads and mip generation must finish encoding before the scene render pass begins.
    for (const auto& item : list.items)
    {
        if (!item.mesh || !item.material || item.subMeshIndex >= item.mesh->subMeshes().size())
        {
            throw EngineError("Draw item requires a mesh, valid submesh and material.");
        }
        prepareMesh(*item.mesh);
        prepareShader(*item.material->shader());
        for (const auto& [name, binding] : item.material->textures())
        {
            auto texture = bindingTexture(binding);
            if (!texture->isLoaded())
            {
                throw EngineError("Draw texture is not ready: " + name + " " + texture->error());
            }
            auto view =
                std::holds_alternative<TextureView>(binding) ? std::get<TextureView>(binding) : texture->defaultView();
            graphics->textures->prepare(view);
        }
        for (const auto& [name, sampler] : item.material->samplers())
        {
            graphics->textures->prepare(*sampler);
        }
    }

    if (list.lighting.lightCount > 4 || !std::isfinite(list.lighting.exposure) || list.lighting.exposure < 0 ||
        !std::isfinite(list.lighting.environmentIntensity) || list.lighting.environmentIntensity < 0 ||
        !std::isfinite(list.lighting.environmentRotation))
    {
        throw EngineError("Invalid scene lighting settings.");
    }
    if (list.lighting.environment)
    {
        graphics->environments->prepare(*list.lighting.environment);
    }
    for (const auto& item : list.items)
    {
        if (item.material->renderState().pbr && (!list.lighting.hdr || !list.lighting.environment))
        {
            throw EngineError("PBR requires an HDR scene and an explicit baked environment (intensity may be zero).");
        }
    }
    if (list.lighting.hdr)
    {
        prepareShader(*graphics->outputShader);
        if (!graphics->hdr || graphics->hdr->getDesc().size.width != target.size.width ||
            graphics->hdr->getDesc().size.height != target.size.height)
        {
            rhi::TextureDesc desc{};
            desc.size = target.size;
            desc.format = Format::RGBA16Float;
            desc.usage = TextureUsage::RenderTarget | TextureUsage::ShaderResource | TextureUsage::CopySource;
            desc.defaultState = rhi::ResourceState::RenderTarget;
            ComPtr<ITexture> hdr, display;
            check(graphics->device->createTexture(desc, nullptr, hdr.writeRef()), "Create HDR scene");
            check(
                graphics->device->createTexture(desc, nullptr, display.writeRef()),
                "Create display-linear composition"
            );
            graphics->hdr = hdr;
            graphics->display = display;
        }
    }
    if (list.outdoor)
    {
        graphics->sky->update(list);
        graphics->environments->update(list, *graphics->sky);
        std::vector<ShadowDraw> shadowDraws;
        for (const auto& item : list.shadowCasters)
        {
            prepareMesh(*item.mesh);
            ShadowDraw draw{&item, drawVertices(item), item.mesh->m_gpu->indices};
            if (item.material->renderState().alphaMode == AlphaMode::Mask &&
                item.material->textures().contains("baseColorTexture"))
            {
                const auto& binding = item.material->textures().at("baseColorTexture");
                auto view = std::holds_alternative<TextureView>(binding) ? std::get<TextureView>(binding)
                                                                         : bindingTexture(binding)->defaultView();
                draw.alphaTexture = graphics->textures->prepare(view);
                draw.alphaSampler = graphics->textures->prepare(*item.material->samplers().at("baseColorSampler"));
            }
            shadowDraws.push_back(draw);
        }
        graphics->shadows->render(list, shadowDraws);
        graphics->sky->drawBackground(list, graphics->hdr);
    }
    std::vector<const DrawItem*> items;
    for (const auto& item : list.items)
    {
        items.push_back(&item);
    }
    // Stable back-to-front object sorting; intersecting transparent geometry remains a known limitation.
    std::stable_sort(
        items.begin(),
        items.end(),
        [&](const auto* a, const auto* b)
        {
            int sa = stageFor(*a->material, list.lighting.hdr), sb = stageFor(*b->material, list.lighting.hdr);
            if (sa != sb)
            {
                return sa < sb;
            }
            bool aBlend = a->material->renderState().alphaMode == AlphaMode::Blend;
            bool bBlend = b->material->renderState().alphaMode == AlphaMode::Blend;
            if (aBlend != bBlend)
            {
                return !aBlend;
            }
            if (!aBlend)
            {
                return false;
            }
            auto distance = [&](const DrawItem* item)
            {
                auto p = item->worldFromLocal[3];
                return math::lengthSquared(math::sub(math::Vec3{p.x, p.y, p.z}, list.cameraPosition));
            };
            return distance(a) > distance(b);
        }
    );
    const int stages = list.lighting.hdr ? 3 : 1;
    for (int stage = 0; stage < stages; ++stage)
    {
        if (stage == 1)
        {
            outputPass(
                graphics->outputShader,
                graphics->outputShader->m_gpu->program,
                graphics->hdr,
                graphics->display,
                list.outdoor ? 1.0f : list.lighting.exposure,
                list.lighting.linearOutput,
                false,
                list.lighting.debugView
            );
        }
        ITexture* attachment =
            list.lighting.hdr ? (stage == 0 ? graphics->hdr.get() : graphics->display.get()) : colorTarget;
        ComPtr<ICommandEncoder> encoder;
        check(graphics->queue->createCommandEncoder(encoder.writeRef()), "Create scene encoder");
        RenderPassColorAttachment color{};
        color.view = attachment->getDefaultView();
        color.loadOp = stage == 0 && !list.outdoor ? LoadOp::Clear : LoadOp::Load;
        color.clearValue[0] = 0.025f;
        color.clearValue[1] = 0.035f;
        color.clearValue[2] = 0.055f;
        color.clearValue[3] = 1;
        RenderPassDepthStencilAttachment depth{};
        depth.view = graphics->depth->getDefaultView();
        depth.depthLoadOp = stage == 0 ? LoadOp::Clear : LoadOp::Load;
        RenderPassDesc passDesc{};
        passDesc.colorAttachments = &color;
        passDesc.colorAttachmentCount = 1;
        passDesc.depthStencilAttachment = &depth;
        auto pass = encoder->beginRenderPass(passDesc);
        for (const auto* itemPointer : items)
        {
            const auto& item = *itemPointer;
            if (stage != stageFor(*item.material, list.lighting.hdr))
            {
                continue;
            }
            auto shader = item.material->shader();
            const auto& world = item.worldFromLocal;
            math::Vec3 x{world[0].x, world[0].y, world[0].z}, y{world[1].x, world[1].y, world[1].z},
                z{world[2].x, world[2].y, world[2].z};
            bool mirrored = math::dot(math::cross(x, y), z) < 0;
            auto pipeline = pipelineFor(
                shader,
                shader->m_gpu->program,
                attachment->getDesc().format,
                item.material->renderState(),
                mirrored
            );
            // Each draw gets independent uniform storage; later material edits cannot overwrite encoded draws.
            ComPtr<IShaderObject> root;
            check(
                graphics->device->createRootShaderObject(shader->m_gpu->program, root.writeRef()),
                "Create draw bindings"
            );
            bindMaterial(root, *item.material);
            bindResources(root, *item.material);
            auto transform = ShaderCursor(root)["draw"]["clipFromLocal"];
            if (!transform.isValid())
            {
                throw EngineError("Shader requires draw.clipFromLocal float4x4.");
            }
            bindMatrix(transform, math::mul(list.clipFromWorld, item.worldFromLocal));
            if (item.material->renderState().pbr)
            {
                std::string error;
                auto inverse = math::inverseAffine(item.worldFromLocal, error);
                if (!inverse)
                {
                    throw EngineError("PBR normal transform: " + error);
                }
                math::Mat4 normal;
                for (size_t c = 0; c < 4; ++c)
                {
                    for (size_t r = 0; r < 4; ++r)
                    {
                        normal[c][r] = (*inverse)[r][c];
                    }
                }
                bindMatrix(ShaderCursor(root)["draw"]["worldFromLocal"], item.worldFromLocal);
                bindMatrix(ShaderCursor(root)["draw"]["normalFromLocal"], normal);
                bindUniform(ShaderCursor(root)["draw"]["orientation"], mirrored ? -1.0f : 1.0f);
                bindFrame(
                    root,
                    list,
                    *item.material,
                    stage != 0,
                    list.outdoor && graphics->environments->hasDynamic() ? graphics->environments->dynamic()
                                                                         : *list.lighting.environment->m_gpu
                );
                bindOutdoor(root, list, target.size.width, target.size.height);
            }
            pass->bindPipeline(pipeline, root);
            RenderState state{};
            state.viewports[0] = Viewport::fromSize(target.size.width, target.size.height);
            state.viewportCount = 1;
            state.scissorRects[0] = ScissorRect::fromSize(target.size.width, target.size.height);
            state.scissorRectCount = 1;
            state.vertexBuffers[0].buffer = drawVertices(item);
            state.vertexBufferCount = 1;
            state.indexBuffer.buffer = item.mesh->m_gpu->indices;
            state.indexFormat = IndexFormat::Uint32;
            pass->setRenderState(state);
            const auto& part = item.mesh->subMeshes()[item.subMeshIndex];
            DrawArguments arguments{};
            arguments.vertexCount = part.indexCount;
            arguments.startIndexLocation = part.firstIndex;
            pass->drawIndexed(arguments);
        }

        pass->end();
        ComPtr<ICommandBuffer> commands;
        check(encoder->finish(commands.writeRef()), "Finish scene commands");
        check(graphics->queue->submit(commands), "Submit scene commands");
    }
    if (list.lighting.hdr)
    {
        bool srgb = target.format == Format::RGBA8UnormSrgb || target.format == Format::BGRA8UnormSrgb;
        outputPass(
            graphics->outputShader,
            graphics->outputShader->m_gpu->program,
            graphics->display,
            colorTarget,
            1,
            true,
            !srgb && (target.format == Format::RGBA8Unorm || target.format == Format::BGRA8Unorm),
            1
        );
    }
}

void Graphics::shutdown() noexcept
{
    if (!graphics)
    {
        return;
    }
    graphics->pipelines.clear();
    for (auto& weak : graphics->meshes)
    {
        if (auto data = weak.lock())
        {
            data->vertices.setNull();
            data->influences.setNull();
            data->indices.setNull();
        }
    }
    for (auto& weak : graphics->shaders)
    {
        if (auto data = weak.lock())
        {
            data->program.setNull();
        }
    }
    graphics.reset();
}

OutdoorDiagnostics Graphics::outdoorDiagnostics() noexcept
{
    OutdoorDiagnostics result;
    if (!graphics)
    {
        return result;
    }
    result.environmentSteps = graphics->environments->completedSteps();
    result.environmentPasses = graphics->environments->lastPassCount();
    result.environmentPublications = graphics->environments->publicationCount();
    if (!graphics->environments->hasDynamic())
    {
        return result;
    }
    result.ready = true;
    result.environmentAgeSeconds = graphics->environments->ageSeconds();
    result.environmentBlend = graphics->environments->blend().z;
    result.shadowResolution = graphics->shadows->texture()->getDesc().size.width;
    for (size_t i = 0; i < 4; ++i)
    {
        result.cascadeDistances[i] = graphics->shadows->cascades()[i].farDistance;
    }
    return result;
}

size_t Graphics::pipelineCreationCount() noexcept
{
    return graphics ? graphics->pipelineCreations : 0;
}

size_t Graphics::pipelineCount() noexcept
{
    size_t count = 0;
    if (graphics)
    {
        for (const auto& entry : graphics->pipelines)
        {
            count += entry.second.size();
        }
    }
    return count;
}
} // namespace ofg
