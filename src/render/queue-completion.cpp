// WebGPU's pinned RHI fence signals submission; use the backend's real work-done callback for retirement.
#include "render/queue-completion.h"
#include "core/engine-error.h"

#include <atomic>
#ifdef __EMSCRIPTEN__
#include <webgpu/webgpu.h>
#endif

namespace ofg {
struct QueueCompletion::State
{
    std::atomic<uint64_t> completed{0};
    std::atomic<bool> failed{false};
};

QueueCompletion::QueueCompletion(rhi::IDevice* device, rhi::ICommandQueue* queue)
    : m_state(std::make_shared<State>())
    , m_queue(queue)
{
    if (!device || !queue)
    {
        throw EngineError("Queue completion requires a device and queue.");
    }
#ifndef __EMSCRIPTEN__
    if (SLANG_FAILED(device->createFence({}, m_fence.writeRef())))
    {
        throw EngineError("Create terrain completion fence failed.");
    }
#endif
}

QueueCompletion::~QueueCompletion() = default;

uint64_t QueueCompletion::mark()
{
    const uint64_t serial = ++m_serial;
#ifdef __EMSCRIPTEN__
    rhi::NativeHandle handle;
    if (SLANG_FAILED(m_queue->getNativeHandle(&handle)) || handle.type != rhi::NativeHandleType::WGPUQueue)
    {
        throw EngineError("WebGPU queue handle is unavailable for actual completion tracking.");
    }
    struct Callback
    {
        std::shared_ptr<State> state;
        uint64_t serial;
    };
    auto callback = std::make_unique<Callback>(Callback{m_state, serial});
    WGPUQueueWorkDoneCallbackInfo info{};
    info.mode = WGPUCallbackMode_AllowSpontaneous;
    info.userdata1 = callback.get();
    // The callback never accesses a terrain instance, scene or discarded QueueCompletion object.
    info.callback = [](WGPUQueueWorkDoneStatus status, WGPUStringView, void* userdata, void*)
    {
        std::unique_ptr<Callback> value(static_cast<Callback*>(userdata));
        if (status != WGPUQueueWorkDoneStatus_Success)
        {
            value->state->failed = true;
            return;
        }
        auto previous = value->state->completed.load();
        while (previous < value->serial && !value->state->completed.compare_exchange_weak(previous, value->serial))
        {
        }
    };
    wgpuQueueOnSubmittedWorkDone(reinterpret_cast<WGPUQueue>(handle.value), info);
    callback.release();
#else
    rhi::IFence* fence = m_fence.get();
    rhi::SubmitDesc submit;
    submit.signalFences = &fence;
    submit.signalFenceValues = &serial;
    submit.signalFenceCount = 1;
    if (SLANG_FAILED(m_queue->submit(submit)))
    {
        throw EngineError("Submit terrain completion fence failed.");
    }
#endif
    return serial;
}

uint64_t QueueCompletion::completed() const
{
#ifdef __EMSCRIPTEN__
    if (m_state->failed)
    {
        throw EngineError("WebGPU queue reported a terrain completion failure.");
    }
    return m_state->completed.load();
#else
    uint64_t value = 0;
    if (SLANG_FAILED(m_fence->getCurrentValue(&value)))
    {
        throw EngineError("Read terrain completion fence failed.");
    }
    return value;
#endif
}
} // namespace ofg
