// Direct native RHI regression for versioned parameter-block snapshots within and across command buffers.
#include <doctest.h>
#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include <array>

using namespace rhi;

TEST_CASE("RHI parameter block edits preserve earlier draw snapshots")
{
    DeviceDesc deviceDesc{};
    deviceDesc.deviceType = DeviceType::D3D12;
    deviceDesc.enableValidation = true;
    ComPtr<IDevice> device;
    REQUIRE(SLANG_SUCCEEDED(getRHI()->createDevice(deviceDesc, device.writeRef())));
    ComPtr<ICommandQueue> queue;
    REQUIRE(SLANG_SUCCEEDED(device->getQueue(QueueType::Graphics, queue.writeRef())));
    const char* source = R"(
        struct Parameters { float4 tint; };
        ParameterBlock<Parameters> parameters;
        // Emit a full-screen triangle; scissors distinguish snapshots without changing geometry.
        [shader("vertex")] float4 vertexMain(uint id : SV_VertexID) : SV_Position
        {
            float2 points[3] = {float2(-1,-1), float2(-1,3), float2(3,-1)};
            return float4(points[id], 0, 1);
        }
        // Read the parameter block that is deliberately edited between encoded draws.
        [shader("fragment")] float4 fragmentMain() : SV_Target { return parameters.tint; }
    )";
    auto module = device->getSlangSession()->loadModuleFromSourceString("snapshot", "snapshot.slang", source);
    REQUIRE(module);
    ComPtr<slang::IEntryPoint> vertex, fragment;
    REQUIRE(SLANG_SUCCEEDED(module->findEntryPointByName("vertexMain", vertex.writeRef())));
    REQUIRE(SLANG_SUCCEEDED(module->findEntryPointByName("fragmentMain", fragment.writeRef())));
    slang::IComponentType* entries[] = {vertex, fragment};
    ShaderProgramDesc programDesc{};
    programDesc.linkingStyle = LinkingStyle::SingleProgram;
    programDesc.slangGlobalScope = module;
    programDesc.slangEntryPoints = entries;
    programDesc.slangEntryPointCount = 2;
    ComPtr<IShaderProgram> program;
    REQUIRE(SLANG_SUCCEEDED(device->createShaderProgram(programDesc, program.writeRef())));
    ColorTargetDesc color{};
    color.format = Format::RGBA8Unorm;
    RenderPipelineDesc pipelineDesc{};
    pipelineDesc.program = program;
    pipelineDesc.targets = &color;
    pipelineDesc.targetCount = 1;
    pipelineDesc.rasterizer.cullMode = CullMode::None;
    pipelineDesc.depthStencil.depthTestEnable = false;
    pipelineDesc.depthStencil.depthWriteEnable = false;
    ComPtr<IRenderPipeline> pipeline;
    REQUIRE(SLANG_SUCCEEDED(device->createRenderPipeline(pipelineDesc, pipeline.writeRef())));
    ComPtr<IShaderObject> root;
    REQUIRE(SLANG_SUCCEEDED(device->createRootShaderObject(program, root.writeRef())));
    auto tint = ShaderCursor(root)["parameters"]["tint"];
    std::array<ComPtr<ITexture>, 2> targets;
    for (unsigned submission = 0; submission < 2; ++submission)
    {
        TextureDesc desc{};
        desc.size = {16, 8, 1};
        desc.format = color.format;
        desc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
        desc.defaultState = ResourceState::RenderTarget;
        REQUIRE(SLANG_SUCCEEDED(device->createTexture(desc, nullptr, targets[submission].writeRef())));
        ComPtr<ICommandEncoder> encoder;
        REQUIRE(SLANG_SUCCEEDED(queue->createCommandEncoder(encoder.writeRef())));
        RenderPassColorAttachment attachment{};
        attachment.view = targets[submission]->getDefaultView();
        attachment.loadOp = LoadOp::Clear;
        RenderPassDesc passDesc{};
        passDesc.colorAttachments = &attachment;
        passDesc.colorAttachmentCount = 1;
        auto pass = encoder->beginRenderPass(passDesc);
        for (unsigned half = 0; half < 2; ++half)
        {
            const std::array<float, 4> value = submission ? std::array<float, 4>{0, 0, 1, 1}
                                               : half     ? std::array<float, 4>{0, 1, 0, 1}
                                                          : std::array<float, 4>{1, 0, 0, 1};
            REQUIRE(SLANG_SUCCEEDED(tint.setData(value.data(), sizeof(value))));
            pass->bindPipeline(pipeline, root);
            RenderState state{};
            state.viewports[0] = Viewport::fromSize(16, 8);
            state.viewportCount = state.scissorRectCount = 1;
            state.scissorRects[0] = {half * 8, 0, (half + 1) * 8, 8};
            pass->setRenderState(state);
            DrawArguments draw{};
            draw.vertexCount = 3;
            pass->draw(draw);
            pass->draw(draw); // Identical version exercises the cache hit as well as the edit path.
        }
        pass->end();
        ComPtr<ICommandBuffer> commands;
        REQUIRE(SLANG_SUCCEEDED(encoder->finish(commands.writeRef())));
        REQUIRE(SLANG_SUCCEEDED(queue->submit(commands)));
    }
    REQUIRE(SLANG_SUCCEEDED(queue->waitOnHost()));
    for (unsigned submission = 0; submission < 2; ++submission)
    {
        ComPtr<ISlangBlob> pixels;
        SubresourceLayout layout{};
        REQUIRE(SLANG_SUCCEEDED(device->readTexture(targets[submission], 0, 0, pixels.writeRef(), &layout)));
        for (unsigned half = 0; half < 2; ++half)
        {
            const auto* pixel =
                static_cast<const uint8_t*>(pixels->getBufferPointer()) + 4 * layout.rowPitch + (half * 8 + 4) * 4;
            CHECK(pixel[0] == (!submission && !half ? 255 : 0));
            CHECK(pixel[1] == (!submission && half ? 255 : 0));
            CHECK(pixel[2] == (submission ? 255 : 0));
        }
    }
}
