// Shared compilation, reflected matrices and explicit raster-pass setup for lighting generation.
#include "render/lighting-pass.h"
#include "core/engine-error.h"
#include <string>
namespace ofg {
using namespace rhi;
void checkLighting(Result result, const char* operation)
{
    if (SLANG_FAILED(result))
    {
        throw EngineError(std::string(operation) + " failed: " + std::to_string(result));
    }
}
ComPtr<IShaderProgram> lightingProgram(IDevice* device, const char* source, const char* name, const char* fragment)
{
    ComPtr<slang::IBlob> diagnostics;
    auto module = device->getSlangSession()->loadModuleFromSourceString(name, name, source, diagnostics.writeRef());
    if (!module)
    {
        throw EngineError(
            std::string("Compile ") + name + ": " +
            (diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "no diagnostics")
        );
    }
    ComPtr<slang::IEntryPoint> vertex, pixel;
    checkLighting(module->findEntryPointByName("vertexMain", vertex.writeRef()), "Find lighting vertex");
    checkLighting(module->findEntryPointByName(fragment, pixel.writeRef()), "Find lighting fragment");
    slang::IComponentType* entries[]{vertex, pixel};
    ShaderProgramDesc desc{};
    desc.slangGlobalScope = module;
    desc.slangEntryPoints = entries;
    desc.slangEntryPointCount = 2;
    desc.linkingStyle = LinkingStyle::SingleProgram;
    ComPtr<IShaderProgram> program;
    auto result = device->createShaderProgram(desc, program.writeRef(), diagnostics.writeRef());
    if (SLANG_FAILED(result))
    {
        throw EngineError(
            std::string("Link ") + name + ": " +
            (diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "no diagnostics")
        );
    }
    return program;
}
ComPtr<IRenderPipeline> lightingPipeline(IDevice* device, IShaderProgram* program, Format format)
{
    ColorTargetDesc color{};
    color.format = format;
    RenderPipelineDesc desc{};
    desc.program = program;
    desc.targets = &color;
    desc.targetCount = 1;
    desc.rasterizer.cullMode = CullMode::None;
    ComPtr<IRenderPipeline> pipeline;
    checkLighting(device->createRenderPipeline(desc, pipeline.writeRef()), "Create lighting pipeline");
    return pipeline;
}
ComPtr<ITextureView> lightingView(ITexture* texture, uint32_t layer, uint32_t mip)
{
    TextureViewDesc desc{};
    desc.format = texture->getDesc().format;
    desc.subresourceRange = {layer, 1, mip, 1};
    // Pinned RHI has no view-dimension field. The revision-checked WebGPU adaptation interprets this
    // explicit marker as a single 2D attachment, preserving default cube/array sampling views.
    desc.label = "ofg-lighting-attachment";
    ComPtr<ITextureView> view;
    checkLighting(texture->createView(desc, view.writeRef()), "Create lighting slice view");
    return view;
}
void setLightingMatrix(ShaderCursor cursor, const math::Mat4& value)
{
    auto data = math::packMat4(value);
    if (cursor.getTypeLayout()->getMatrixLayoutMode() == SLANG_MATRIX_LAYOUT_ROW_MAJOR)
    {
        for (size_t r = 0; r < 4; ++r)
        {
            for (size_t c = 0; c < 4; ++c)
            {
                data[r * 4 + c] = value[c][r];
            }
        }
    }
    checkLighting(cursor.setData(data.data(), sizeof(data)), "Bind lighting matrix");
}
void drawLightingPass(
    ICommandEncoder* encoder,
    IRenderPipeline* pipeline,
    IShaderObject* root,
    ITextureView* target,
    uint32_t width,
    uint32_t height
)
{
    RenderPassColorAttachment color{};
    color.view = target;
    color.loadOp = LoadOp::Clear;
    RenderPassDesc desc{};
    desc.colorAttachments = &color;
    desc.colorAttachmentCount = 1;
    auto pass = encoder->beginRenderPass(desc);
    pass->bindPipeline(pipeline, root);
    RenderState state{};
    state.viewports[0] = Viewport::fromSize(width, height);
    state.viewportCount = 1;
    state.scissorRects[0] = ScissorRect::fromSize(width, height);
    state.scissorRectCount = 1;
    pass->setRenderState(state);
    DrawArguments args{};
    args.vertexCount = 3;
    pass->draw(args);
    pass->end();
}
void submitLighting(ICommandQueue* queue, ICommandEncoder* encoder)
{
    ComPtr<ICommandBuffer> commands;
    checkLighting(encoder->finish(commands.writeRef()), "Finish lighting commands");
    checkLighting(queue->submit(commands), "Submit lighting commands");
}
} // namespace ofg
