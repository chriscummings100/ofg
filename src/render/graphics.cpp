// Direct RHI scene rendering, reflected material binding, and explicit service/asset GPU ownership.
#include "render/graphics.h"
#include "render/resource-gpu-data.h"
#include "render/texture-renderer.h"
#include "core/engine-error.h"
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
    ComPtr<IRenderPipeline> pipeline;
};

struct GraphicsState
{
    ComPtr<IDevice> device;
    ComPtr<ICommandQueue> queue;
    ComPtr<IInputLayout> inputLayout;
    ComPtr<ITexture> depth;
    // All pipeline settings except shader identity and color format are fixed in this implementation.
    std::map<std::weak_ptr<Shader>, std::vector<PipelineEntry>, std::owner_less<std::weak_ptr<Shader>>> pipelines;
    std::vector<std::weak_ptr<MeshGpuData>> meshes;
    std::vector<std::weak_ptr<ShaderGpuData>> shaders;
    std::unique_ptr<TextureRenderer> textures;
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
        if (name != "draw" && name != "material" && !material.textures().contains(name) &&
            !material.samplers().contains(name))
        {
            throw EngineError("Missing or unsupported shader resource binding: " + name);
        }
    }
}

// Creates only the fixed pipeline variants required by the shader and target format.
IRenderPipeline* pipelineFor(const std::shared_ptr<Shader>& shader, IShaderProgram* program, Format format)
{
    auto& variants = graphics->pipelines[shader];
    for (const auto& entry : variants)
    {
        if (entry.colorFormat == format)
        {
            return entry.pipeline;
        }
    }
    ColorTargetDesc target{};
    target.format = format;
    RenderPipelineDesc desc{};
    desc.program = program;
    desc.inputLayout = graphics->inputLayout;
    desc.targets = &target;
    desc.targetCount = 1;
    desc.depthStencil.format = Format::D32Float;
    desc.depthStencil.depthTestEnable = true;
    desc.depthStencil.depthWriteEnable = true;
    desc.depthStencil.depthFunc = ComparisonFunc::Less;
    desc.rasterizer.cullMode = CullMode::None;
    ComPtr<IRenderPipeline> pipeline;
    check(graphics->device->createRenderPipeline(desc, pipeline.writeRef()), "Create scene pipeline");
    variants.push_back({format, pipeline});
    ++graphics->pipelineCreations;
    return pipeline;
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
    const rhi::VertexStreamDesc stream{sizeof(Vertex), rhi::InputSlotClass::PerVertex, 0};
    const rhi::InputElementDesc elements[] = {
        {"POSITION", 0, rhi::Format::RGB32Float, offsetof(Vertex, position), 0},
        {"NORMAL", 0, rhi::Format::RGB32Float, offsetof(Vertex, normal), 0},
        {"TEXCOORD", 0, rhi::Format::RG32Float, offsetof(Vertex, uv), 0}
    };
    rhi::InputLayoutDesc desc{};
    desc.inputElements = elements;
    desc.inputElementCount = 3;
    desc.vertexStreams = &stream;
    desc.vertexStreamCount = 1;
    check(device->createInputLayout(desc, state->inputLayout.writeRef()), "Create mesh vertex layout");
    state->textures = std::make_unique<TextureRenderer>(device, queue);
    graphics = std::move(state);
}

void Graphics::prepareMesh(Mesh& mesh)
{
    if (mesh.m_gpu && mesh.m_gpu->vertices)
    {
        return;
    }
    auto data = std::make_shared<MeshGpuData>();
    rhi::BufferDesc desc{};
    desc.size = mesh.vertices().size_bytes();
    desc.usage = rhi::BufferUsage::VertexBuffer;
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

    // Uploads and mip generation must finish encoding before the scene render pass begins.
    for (const auto& item : list.items)
    {
        if (!item.mesh || !item.material || item.subMeshIndex >= item.mesh->subMeshes().size())
        {
            throw EngineError("Draw item requires a mesh, valid submesh and material.");
        }
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

    ComPtr<ICommandEncoder> encoder;
    check(graphics->queue->createCommandEncoder(encoder.writeRef()), "Create scene encoder");
    RenderPassColorAttachment color{};
    color.view = colorTarget->getDefaultView();
    color.clearValue[0] = 0.025f;
    color.clearValue[1] = 0.035f;
    color.clearValue[2] = 0.055f;
    color.clearValue[3] = 1.0f;
    RenderPassDepthStencilAttachment depth{};
    depth.view = graphics->depth->getDefaultView();
    RenderPassDesc passDesc{};
    passDesc.colorAttachments = &color;
    passDesc.colorAttachmentCount = 1;
    passDesc.depthStencilAttachment = &depth;
    auto pass = encoder->beginRenderPass(passDesc);
    for (const auto& item : list.items)
    {
        if (!item.mesh || !item.material || item.subMeshIndex >= item.mesh->subMeshes().size())
        {
            throw EngineError("Draw item requires a mesh, valid submesh and material.");
        }
        prepareMesh(*item.mesh);
        auto shader = item.material->shader();
        prepareShader(*shader);
        auto pipeline = pipelineFor(shader, shader->m_gpu->program, target.format);
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
        pass->bindPipeline(pipeline, root);
        RenderState state{};
        state.viewports[0] = Viewport::fromSize(target.size.width, target.size.height);
        state.viewportCount = 1;
        state.scissorRects[0] = ScissorRect::fromSize(target.size.width, target.size.height);
        state.scissorRectCount = 1;
        state.vertexBuffers[0].buffer = item.mesh->m_gpu->vertices;
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
