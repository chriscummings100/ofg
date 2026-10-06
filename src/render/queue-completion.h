// Actual GPU completion markers: native fences and browser queue callbacks, without frame-time waits.
#pragma once

#include <slang-rhi.h>
#include <memory>

namespace ofg {
class QueueCompletion
{
public:
    // Retains the graphics queue; browser callbacks own only shared completion state.
    QueueCompletion(rhi::IDevice* device, rhi::ICommandQueue* queue);
    // Releases application ownership without invalidating outstanding browser callback state.
    ~QueueCompletion();
    QueueCompletion(const QueueCompletion&) = delete;
    QueueCompletion& operator=(const QueueCompletion&) = delete;
    // Marks all previously submitted queue work and returns its monotonically increasing serial.
    uint64_t mark();
    // Returns actual completed work without waiting; throws on asynchronous backend failure.
    uint64_t completed() const;

private:
    struct State;
    std::shared_ptr<State> m_state;
    rhi::ComPtr<rhi::ICommandQueue> m_queue;
    rhi::ComPtr<rhi::IFence> m_fence;
    uint64_t m_serial = 0;
};
} // namespace ofg
