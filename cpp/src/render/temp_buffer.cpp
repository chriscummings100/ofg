// Static reusable temporary render-target system implementation.
#include "ofg/render/temp_buffer.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/gpu/common.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ofg {
namespace {

constexpr std::uint64_t _stale_frame_count = 10;
constexpr WGPUTextureUsage _supported_temp_usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
                                                   WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;

struct TempBufferEntry {
    std::uint32_t m_id{0};
    std::uint32_t m_generation{0};
    TempBufferDesc m_desc;
    WGPUTexture m_texture{nullptr};
    WGPUTextureView m_view{nullptr};
    std::uint64_t m_byte_size{0};
    std::uint64_t m_last_used_frame{0};
    std::uint64_t m_last_returned_frame{0};
    bool m_active{false};
};

// Returns the byte size for formats supported by temporary render targets.
std::uint32_t bytes_per_pixel(WGPUTextureFormat format) {
    switch (format) {
    case WGPUTextureFormat_RGBA16Float:
        return 8;
    case WGPUTextureFormat_RGBA8Unorm:
    case WGPUTextureFormat_RGBA8UnormSrgb:
    case WGPUTextureFormat_BGRA8Unorm:
    case WGPUTextureFormat_BGRA8UnormSrgb:
        return 4;
    default:
        break;
    }
    throw EngineError("TempBuffer does not support texture format " + gpu::texture_format_name(format) + ".");
}

// Computes a conservative byte count for a temporary texture descriptor.
std::uint64_t temp_buffer_byte_size(const TempBufferDesc& desc) {
    return static_cast<std::uint64_t>(desc.m_width) * static_cast<std::uint64_t>(desc.m_height) *
           static_cast<std::uint64_t>(bytes_per_pixel(desc.m_format));
}

// Builds a readable WebGPU label for a temp texture or view.
std::string make_label(std::string_view debug_label, const TempBufferDesc& desc, std::string_view suffix) {
    std::ostringstream out;
    out << "OFG temp buffer";
    if (!debug_label.empty()) {
        out << " " << debug_label;
    }
    out << " " << desc.m_width << "x" << desc.m_height << " " << suffix;
    return out.str();
}

// Releases or destroys one stored WebGPU texture/view pair.
void release_entry_resources(TempBufferEntry& entry, bool destroy_texture) noexcept {
    if (entry.m_view != nullptr) {
        wgpuTextureViewRelease(entry.m_view);
        entry.m_view = nullptr;
    }
    if (entry.m_texture != nullptr) {
        if (destroy_texture) {
            wgpuTextureDestroy(entry.m_texture);
        }
        wgpuTextureRelease(entry.m_texture);
        entry.m_texture = nullptr;
    }
    entry.m_active = false;
}

} // namespace

class TempBufferStore {
public:
    explicit TempBufferStore(GpuContext gpu) : m_gpu(std::move(gpu)) {}
    TempBufferStore(const TempBufferStore&) = delete;
    TempBufferStore& operator=(const TempBufferStore&) = delete;
    TempBufferStore(TempBufferStore&&) = delete;
    TempBufferStore& operator=(TempBufferStore&&) = delete;
    ~TempBufferStore();

    // Starts a frame and prunes reusable entries that aged out.
    void begin_frame();
    // Returns or creates a descriptor-matched temporary render target.
    TempBufferRef get(const TempBufferDesc& desc, std::string_view debug_label);
    // Returns one active entry to the reusable set.
    void release(TempBufferRef& buffer) noexcept;
    // Returns all active entries to the reusable set.
    void end_frame();
    // Releases all texture resources and prevents further frame use.
    bool release_all() noexcept;
    // Reports current and cumulative memory state.
    TempBufferStats stats() const noexcept;
    // Reports texture and view creation counters.
    RendererCounters counters() const noexcept;
    // Checks whether a public handle still refers to an active entry.
    bool is_ref_active(const TempBufferRef& buffer) const noexcept;

private:
    // Throws if the store has been lifecycle-released.
    void require_not_released(const char* operation) const;
    // Returns an active entry to the reusable set.
    void return_entry(TempBufferEntry& entry) noexcept;
    // Finds a matching reusable entry.
    [[nodiscard]] TempBufferEntry* find_reusable_entry(const TempBufferDesc& desc) noexcept;
    // Creates and stores a new temp texture/view pair.
    [[nodiscard]] TempBufferEntry& create_entry(const TempBufferDesc& desc, std::string_view debug_label);
    // Removes stale reusable entries.
    void prune_stale_entries() noexcept;
    // Recomputes active/reusable byte and count diagnostics.
    void refresh_live_stats() noexcept;
    // Updates peak allocated bytes after creation or discard.
    void update_peak_bytes() noexcept;
    // Finds an entry by public id.
    [[nodiscard]] TempBufferEntry* find_entry(std::uint32_t id) noexcept;
    // Finds an entry by public id.
    [[nodiscard]] const TempBufferEntry* find_entry(std::uint32_t id) const noexcept;

    GpuContext m_gpu;
    std::vector<TempBufferEntry> m_entries;
    std::uint64_t m_frame_index{0};
    std::uint32_t m_next_id{1};
    bool m_in_frame{false};
    bool m_released{false};
    RendererCounters m_counters;
    TempBufferStats m_stats;
};

namespace {

std::unique_ptr<TempBufferStore> _temp_buffers;

// Returns the live temp-buffer store or throws a lifecycle error.
TempBufferStore& require_temp_buffers(const char* operation) {
    if (_temp_buffers == nullptr) {
        throw EngineError(std::string(operation) + " requires TempBuffer::create first.");
    }
    return *_temp_buffers;
}

} // namespace

TempBufferStore::~TempBufferStore() {
    release_all();
}

// Starts a frame and prunes reusable entries that aged out.
void TempBufferStore::begin_frame() {
    require_not_released("TempBuffer::begin_frame");
    if (m_in_frame) {
        throw EngineError("TempBuffer::begin_frame cannot be called while a temp-buffer frame is active.");
    }
    m_frame_index += 1;
    prune_stale_entries();
    refresh_live_stats();
    m_in_frame = true;
}

// Returns or creates a descriptor-matched temporary render target.
TempBufferRef TempBufferStore::get(const TempBufferDesc& desc, std::string_view debug_label) {
    require_not_released("TempBuffer::get");
    if (!m_in_frame) {
        throw EngineError("TempBuffer::get requires TempBuffer::begin_frame first.");
    }
    validate_temp_buffer_desc(desc);

    TempBufferEntry* entry = find_reusable_entry(desc);
    if (entry == nullptr) {
        entry = &create_entry(desc, debug_label);
    } else {
        m_stats.m_reused_count += 1;
    }

    entry->m_active = true;
    entry->m_last_used_frame = m_frame_index;
    entry->m_generation += 1;
    if (entry->m_generation == 0) {
        entry->m_generation = 1;
    }
    refresh_live_stats();
    return TempBufferRef(entry->m_id, entry->m_generation, entry->m_view, desc.m_width, desc.m_height, desc.m_format);
}

// Returns one active entry to the reusable set.
void TempBufferStore::release(TempBufferRef& buffer) noexcept {
    TempBufferEntry* entry = find_entry(buffer.m_id);
    if (entry != nullptr && entry->m_generation == buffer.m_generation && entry->m_active) {
        return_entry(*entry);
        m_stats.m_early_release_count += 1;
        refresh_live_stats();
    }
    buffer.invalidate();
}

// Returns all active entries to the reusable set.
void TempBufferStore::end_frame() {
    require_not_released("TempBuffer::end_frame");
    if (!m_in_frame) {
        throw EngineError("TempBuffer::end_frame requires an active temp-buffer frame.");
    }

    std::uint64_t returned_count = 0;
    for (TempBufferEntry& entry : m_entries) {
        if (entry.m_active) {
            return_entry(entry);
            returned_count += 1;
        }
    }
    m_stats.m_end_frame_return_count += returned_count;
    m_in_frame = false;
    refresh_live_stats();
}

// Releases all texture resources and prevents further frame use.
bool TempBufferStore::release_all() noexcept {
    for (TempBufferEntry& entry : m_entries) {
        release_entry_resources(entry, true);
    }
    m_entries.clear();
    m_gpu = GpuContext{};
    m_in_frame = false;
    m_released = true;
    refresh_live_stats();
    return true;
}

// Reports current and cumulative memory state.
TempBufferStats TempBufferStore::stats() const noexcept {
    return m_stats;
}

// Reports texture and view creation counters.
RendererCounters TempBufferStore::counters() const noexcept {
    return m_counters;
}

// Checks whether a public handle still refers to an active entry.
bool TempBufferStore::is_ref_active(const TempBufferRef& buffer) const noexcept {
    const TempBufferEntry* entry = find_entry(buffer.m_id);
    return entry != nullptr && entry->m_generation == buffer.m_generation && entry->m_active &&
           entry->m_view == buffer.m_view;
}

// Throws if the store has been lifecycle-released.
void TempBufferStore::require_not_released(const char* operation) const {
    if (m_released) {
        throw EngineError(std::string(operation) + " cannot run after TempBuffer::release.");
    }
}

// Returns an active entry to the reusable set.
void TempBufferStore::return_entry(TempBufferEntry& entry) noexcept {
    entry.m_active = false;
    entry.m_last_returned_frame = m_frame_index;
}

// Finds a matching reusable entry.
TempBufferEntry* TempBufferStore::find_reusable_entry(const TempBufferDesc& desc) noexcept {
    for (TempBufferEntry& entry : m_entries) {
        if (!entry.m_active && entry.m_desc == desc) {
            return &entry;
        }
    }
    return nullptr;
}

// Creates and stores a new temp texture/view pair.
TempBufferEntry& TempBufferStore::create_entry(const TempBufferDesc& desc, std::string_view debug_label) {
    const std::string texture_label = make_label(debug_label, desc, "texture");
    WGPUTextureDescriptor texture_descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture_descriptor.label = gpu::string_view(texture_label);
    texture_descriptor.usage = desc.m_usage;
    texture_descriptor.dimension = WGPUTextureDimension_2D;
    texture_descriptor.size = WGPUExtent3D{desc.m_width, desc.m_height, desc.m_array_layer_count};
    texture_descriptor.format = desc.m_format;
    texture_descriptor.mipLevelCount = desc.m_mip_level_count;
    texture_descriptor.sampleCount = desc.m_sample_count;

    WGPUTexture texture = wgpuDeviceCreateTexture(m_gpu.m_device, &texture_descriptor);
    if (texture == nullptr) {
        throw EngineError("wgpuDeviceCreateTexture returned null for temp buffer.");
    }
    m_counters.m_texture_create_count += 1;

    WGPUTextureView view = nullptr;
    try {
        const std::string view_label = make_label(debug_label, desc, "view");
        WGPUTextureViewDescriptor view_descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        view_descriptor.label = gpu::string_view(view_label);
        view_descriptor.format = desc.m_format;
        view_descriptor.dimension = WGPUTextureViewDimension_2D;
        view_descriptor.baseMipLevel = 0;
        view_descriptor.mipLevelCount = 1;
        view_descriptor.baseArrayLayer = 0;
        view_descriptor.arrayLayerCount = 1;
        view_descriptor.aspect = WGPUTextureAspect_All;
        view = wgpuTextureCreateView(texture, &view_descriptor);
        if (view == nullptr) {
            throw EngineError("wgpuTextureCreateView returned null for temp buffer.");
        }
        m_counters.m_texture_view_create_count += 1;
    } catch (...) {
        wgpuTextureDestroy(texture);
        wgpuTextureRelease(texture);
        throw;
    }

    TempBufferEntry entry;
    entry.m_id = m_next_id;
    if (m_next_id < std::numeric_limits<std::uint32_t>::max()) {
        m_next_id += 1;
    } else {
        m_next_id = 1;
    }
    entry.m_desc = desc;
    entry.m_texture = texture;
    entry.m_view = view;
    entry.m_byte_size = temp_buffer_byte_size(desc);
    entry.m_last_used_frame = m_frame_index;
    entry.m_last_returned_frame = m_frame_index;

    m_entries.push_back(entry);
    m_stats.m_created_count += 1;
    update_peak_bytes();
    return m_entries.back();
}

// Removes stale reusable entries.
void TempBufferStore::prune_stale_entries() noexcept {
    const auto stale = [this](const TempBufferEntry& entry) {
        return !entry.m_active && m_frame_index >= entry.m_last_used_frame &&
               (m_frame_index - entry.m_last_used_frame) >= _stale_frame_count;
    };

    for (TempBufferEntry& entry : m_entries) {
        if (stale(entry)) {
            release_entry_resources(entry, true);
            m_stats.m_discarded_count += 1;
        }
    }
    const auto removed = std::remove_if(m_entries.begin(), m_entries.end(), [](const TempBufferEntry& entry) {
        return entry.m_texture == nullptr && entry.m_view == nullptr && !entry.m_active;
    });
    m_entries.erase(removed, m_entries.end());
}

// Recomputes active/reusable byte and count diagnostics.
void TempBufferStore::refresh_live_stats() noexcept {
    m_stats.m_active_bytes = 0;
    m_stats.m_reusable_bytes = 0;
    m_stats.m_active_count = 0;
    m_stats.m_reusable_count = 0;
    for (const TempBufferEntry& entry : m_entries) {
        if (entry.m_active) {
            m_stats.m_active_bytes += entry.m_byte_size;
            m_stats.m_active_count += 1;
        } else {
            m_stats.m_reusable_bytes += entry.m_byte_size;
            m_stats.m_reusable_count += 1;
        }
    }
}

// Updates peak allocated bytes after creation or discard.
void TempBufferStore::update_peak_bytes() noexcept {
    std::uint64_t total = 0;
    for (const TempBufferEntry& entry : m_entries) {
        total += entry.m_byte_size;
    }
    m_stats.m_peak_bytes = std::max(m_stats.m_peak_bytes, total);
}

// Finds an entry by public id.
TempBufferEntry* TempBufferStore::find_entry(std::uint32_t id) noexcept {
    if (id == 0) {
        return nullptr;
    }
    for (TempBufferEntry& entry : m_entries) {
        if (entry.m_id == id) {
            return &entry;
        }
    }
    return nullptr;
}

// Finds an entry by public id.
const TempBufferEntry* TempBufferStore::find_entry(std::uint32_t id) const noexcept {
    if (id == 0) {
        return nullptr;
    }
    for (const TempBufferEntry& entry : m_entries) {
        if (entry.m_id == id) {
            return &entry;
        }
    }
    return nullptr;
}

// Compares temporary texture descriptors for exact reuse matching.
bool operator==(const TempBufferDesc& left, const TempBufferDesc& right) noexcept {
    return left.m_width == right.m_width && left.m_height == right.m_height && left.m_format == right.m_format &&
           left.m_usage == right.m_usage && left.m_mip_level_count == right.m_mip_level_count &&
           left.m_array_layer_count == right.m_array_layer_count && left.m_sample_count == right.m_sample_count;
}

// Validates that a temporary texture descriptor is supported by TempBuffer.
void validate_temp_buffer_desc(const TempBufferDesc& desc) {
    if (desc.m_width == 0 || desc.m_height == 0) {
        throw EngineError("TempBuffer descriptor dimensions must be nonzero.");
    }
    if (desc.m_format == WGPUTextureFormat_Undefined) {
        throw EngineError("TempBuffer descriptor requires a defined texture format.");
    }
    if (desc.m_usage == 0) {
        throw EngineError("TempBuffer descriptor requires at least one texture usage flag.");
    }
    if ((desc.m_usage & WGPUTextureUsage_RenderAttachment) == 0) {
        throw EngineError("TempBuffer descriptor requires RenderAttachment usage.");
    }
    if ((desc.m_usage & ~_supported_temp_usage) != 0) {
        throw EngineError("TempBuffer descriptor contains unsupported texture usage flags.");
    }
    if (desc.m_mip_level_count != 1) {
        throw EngineError("TempBuffer currently supports exactly one mip level.");
    }
    if (desc.m_array_layer_count != 1) {
        throw EngineError("TempBuffer currently supports exactly one array layer.");
    }
    if (desc.m_sample_count != 1) {
        throw EngineError("TempBuffer currently supports exactly one sample per pixel.");
    }
    (void)bytes_per_pixel(desc.m_format);
}

// Transfers a temp-buffer handle without returning the underlying buffer.
TempBufferRef::TempBufferRef(TempBufferRef&& other) noexcept
    : m_id(std::exchange(other.m_id, 0)), m_generation(std::exchange(other.m_generation, 0)),
      m_view(std::exchange(other.m_view, nullptr)), m_width(std::exchange(other.m_width, 0)),
      m_height(std::exchange(other.m_height, 0)), m_format(std::exchange(other.m_format, WGPUTextureFormat_Undefined)) {
}

// Clears this handle, then transfers another handle into it.
TempBufferRef& TempBufferRef::operator=(TempBufferRef&& other) noexcept {
    if (this != &other) {
        invalidate();
        m_id = std::exchange(other.m_id, 0);
        m_generation = std::exchange(other.m_generation, 0);
        m_view = std::exchange(other.m_view, nullptr);
        m_width = std::exchange(other.m_width, 0);
        m_height = std::exchange(other.m_height, 0);
        m_format = std::exchange(other.m_format, WGPUTextureFormat_Undefined);
    }
    return *this;
}

// Reports whether this handle still refers to an active temp buffer.
bool TempBufferRef::valid() const noexcept {
    return TempBuffer::is_ref_active(*this);
}

// Returns this temp buffer as a render target, or throws if inactive.
RenderTarget TempBufferRef::render_target() const {
    WGPUTextureView active_view = view();
    if (active_view == nullptr || m_width == 0 || m_height == 0 || m_format == WGPUTextureFormat_Undefined) {
        throw EngineError("TempBufferRef render_target requires an active temp buffer.");
    }
    return RenderTarget{active_view, m_format, m_width, m_height};
}

// Returns the active texture view, or null after release/frame end.
WGPUTextureView TempBufferRef::view() const noexcept {
    if (!valid()) {
        return nullptr;
    }
    return m_view;
}

// Returns the width captured when this handle was issued.
std::uint32_t TempBufferRef::width() const noexcept {
    return m_width;
}

// Returns the height captured when this handle was issued.
std::uint32_t TempBufferRef::height() const noexcept {
    return m_height;
}

// Returns the format captured when this handle was issued.
WGPUTextureFormat TempBufferRef::format() const noexcept {
    return m_format;
}

// Creates a handle for one active temp-buffer checkout.
TempBufferRef::TempBufferRef(std::uint32_t id,
    std::uint32_t generation,
    WGPUTextureView view,
    std::uint32_t width,
    std::uint32_t height,
    WGPUTextureFormat format) noexcept
    : m_id(id), m_generation(generation), m_view(view), m_width(width), m_height(height), m_format(format) {}

// Clears this handle without touching the underlying texture.
void TempBufferRef::invalidate() noexcept {
    m_id = 0;
    m_generation = 0;
    m_view = nullptr;
    m_width = 0;
    m_height = 0;
    m_format = WGPUTextureFormat_Undefined;
}

// Creates the temp-buffer singleton for one borrowed WebGPU device.
void TempBuffer::create(GpuContext gpu) {
    if (_temp_buffers != nullptr) {
        throw EngineError("TempBuffer::create cannot be called while a TempBuffer singleton is live.");
    }
    if (!gpu_context_is_ready(gpu)) {
        throw EngineError("TempBuffer requires a WebGPU device and queue.");
    }
    _temp_buffers = std::make_unique<TempBufferStore>(std::move(gpu));
}

// Starts a renderer frame and performs stale inactive cleanup.
void TempBuffer::begin_frame() {
    require_temp_buffers("TempBuffer::begin_frame").begin_frame();
}

// Returns a descriptor-matched temporary render target for the current frame.
TempBufferRef TempBuffer::get(const TempBufferDesc& desc, std::string_view debug_label) {
    return require_temp_buffers("TempBuffer::get").get(desc, debug_label);
}

// Returns one temp buffer after its final encoded GPU use.
void TempBuffer::release(TempBufferRef& buffer) noexcept {
    if (_temp_buffers != nullptr) {
        _temp_buffers->release(buffer);
        return;
    }
    buffer.invalidate();
}

// Returns all still-active temp buffers to the reusable set.
void TempBuffer::end_frame() {
    require_temp_buffers("TempBuffer::end_frame").end_frame();
}

// Releases all temp-buffer WebGPU resources for renderer teardown.
bool TempBuffer::release() {
    if (_temp_buffers == nullptr) {
        return true;
    }
    return _temp_buffers->release_all();
}

// Destroys the singleton after lifecycle release has completed.
void TempBuffer::destroy() noexcept {
    _temp_buffers.reset();
}

// Reports durable WebGPU creation counters.
RendererCounters TempBuffer::counters() noexcept {
    if (_temp_buffers == nullptr) {
        return RendererCounters{};
    }
    return _temp_buffers->counters();
}

// Reports temp-buffer memory and reuse diagnostics.
TempBufferStats TempBuffer::stats() noexcept {
    if (_temp_buffers == nullptr) {
        return TempBufferStats{};
    }
    return _temp_buffers->stats();
}

// Reports whether a handle still matches an active internal entry.
bool TempBuffer::is_ref_active(const TempBufferRef& buffer) noexcept {
    if (_temp_buffers == nullptr) {
        return false;
    }
    return _temp_buffers->is_ref_active(buffer);
}

} // namespace ofg
