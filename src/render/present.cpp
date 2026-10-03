// Presents a completed offscreen image without uniforms or staging maps, so canvas acquisition cannot span a yield.
#include "render/present.h"
#include "present-shader.h"
#include <slang-rhi/shader-cursor.h>

#include <cstdio>

using namespace rhi;

Result createPresentationPipeline(IDevice* device, Format format, IRenderPipeline** outPipeline)
{
    ComPtr<slang::IBlob> diagnostics;
    auto session = device->getSlangSession();
    auto module =
        session->loadModuleFromSourceString("present", "present.slang", presentShader, diagnostics.writeRef());
    if (diagnostics)
    {
        std::fprintf(stderr, "%s\n", static_cast<const char*>(diagnostics->getBufferPointer()));
    }
    if (!module)
    {
        return SLANG_FAIL;
    }

    ComPtr<slang::IEntryPoint> vertex;
    ComPtr<slang::IEntryPoint> fragment;
    SLANG_RETURN_ON_FAIL(module->findEntryPointByName("vertexMain", vertex.writeRef()));
    SLANG_RETURN_ON_FAIL(module->findEntryPointByName("fragmentMain", fragment.writeRef()));
    slang::IComponentType* entryPoints[] = {vertex, fragment};
    ShaderProgramDesc programDesc = {};
    programDesc.linkingStyle = LinkingStyle::SingleProgram;
    programDesc.slangGlobalScope = module;
    programDesc.slangEntryPoints = entryPoints;
    programDesc.slangEntryPointCount = 2;

    ComPtr<IShaderProgram> program;
    const Result result = device->createShaderProgram(programDesc, program.writeRef(), diagnostics.writeRef());
    if (diagnostics)
    {
        std::fprintf(stderr, "%s\n", static_cast<const char*>(diagnostics->getBufferPointer()));
    }
    SLANG_RETURN_ON_FAIL(result);

    ColorTargetDesc colorTarget = {};
    colorTarget.format = format;
    RenderPipelineDesc pipelineDesc = {};
    pipelineDesc.program = program;
    pipelineDesc.targets = &colorTarget;
    pipelineDesc.targetCount = 1;
    pipelineDesc.rasterizer.cullMode = CullMode::None;
    pipelineDesc.depthStencil.depthTestEnable = false;
    pipelineDesc.depthStencil.depthWriteEnable = false;
    return device->createRenderPipeline(pipelineDesc, outPipeline);
}

Result drawPresentation(ICommandQueue* queue, IRenderPipeline* pipeline, ITexture* source, ITexture* target)
{
    ComPtr<ICommandEncoder> encoder;
    SLANG_RETURN_ON_FAIL(queue->createCommandEncoder(encoder.writeRef()));

    // A magenta clear makes incomplete presentation obvious in screenshots and integration tests.
    RenderPassColorAttachment color = {};
    color.view = target->getDefaultView();
    color.loadOp = LoadOp::Clear;
    color.clearValue[0] = 1.0f;
    color.clearValue[2] = 1.0f;
    color.clearValue[3] = 1.0f;
    RenderPassDesc passDesc = {};
    passDesc.colorAttachments = &color;
    passDesc.colorAttachmentCount = 1;
    auto pass = encoder->beginRenderPass(passDesc);
    auto root = pass->bindPipeline(pipeline);
    SLANG_RETURN_ON_FAIL(ShaderCursor(root)["sourceImage"].setBinding(source));

    const auto size = target->getDesc().size;
    RenderState state = {};
    state.viewports[0] = Viewport::fromSize(size.width, size.height);
    state.viewportCount = 1;
    state.scissorRects[0] = ScissorRect::fromSize(size.width, size.height);
    state.scissorRectCount = 1;
    pass->setRenderState(state);
    DrawArguments draw = {};
    draw.vertexCount = 3;
    pass->draw(draw);
    pass->end();

    ComPtr<ICommandBuffer> commands;
    SLANG_RETURN_ON_FAIL(encoder->finish(commands.writeRef()));
    return queue->submit(commands);
}
