// Mutable material resource for OFG renderer shader parameters.
#include "ofg/resources/material.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/gpu/common.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/resources/texture.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ofg {
namespace {

struct CachedMaterialBindGroupLayout {
    WGPUBindGroupLayout m_layout{nullptr};
    std::uint32_t m_ref_count{0};
};

// Returns the process-local cache of structurally identical material bind-group layouts.
std::map<std::string, CachedMaterialBindGroupLayout>& material_bind_group_layout_cache() {
    static std::map<std::string, CachedMaterialBindGroupLayout> _cache;
    return _cache;
}

// Returns a compact token for the sample type required by one texture binding.
[[nodiscard]] const char* texture_sample_type_key(TexturePixelFormat format) noexcept {
    return format == TexturePixelFormat::R16Float ? "unfilterable-float" : "float";
}

// Returns a compact token for the sampler type required by one texture binding.
[[nodiscard]] const char* sampler_binding_type_key(TexturePixelFormat format) noexcept {
    return format == TexturePixelFormat::R16Float ? "non-filtering" : "filtering";
}

// Acquires a shared bind-group layout for one material layout signature.
[[nodiscard]] WGPUBindGroupLayout acquire_cached_material_bind_group_layout(const GpuContext& context,
    const std::string& label,
    const std::string& key,
    const std::vector<WGPUBindGroupLayoutEntry>& layout_entries) {
    auto& cache = material_bind_group_layout_cache();
    auto found = cache.find(key);
    if (found != cache.end()) {
        found->second.m_ref_count += 1U;
        return found->second.m_layout;
    }

    WGPUBindGroupLayoutDescriptor layout_descriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layout_descriptor.label = gpu::string_view(label);
    layout_descriptor.entryCount = layout_entries.size();
    layout_descriptor.entries = layout_entries.empty() ? nullptr : layout_entries.data();
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(context.m_device, &layout_descriptor);
    if (layout == nullptr) {
        throw EngineError("wgpuDeviceCreateBindGroupLayout returned null for material '" + label + "'.");
    }

    cache.emplace(key, CachedMaterialBindGroupLayout{layout, 1U});
    return layout;
}

// Releases one reference to a shared material bind-group layout.
void release_cached_material_bind_group_layout(const std::string& key, WGPUBindGroupLayout layout) noexcept {
    if (layout == nullptr) {
        return;
    }

    auto& cache = material_bind_group_layout_cache();
    auto found = key.empty() ? cache.end() : cache.find(key);
    if (found == cache.end() || found->second.m_layout != layout || found->second.m_ref_count == 0U) {
        wgpuBindGroupLayoutRelease(layout);
        return;
    }

    found->second.m_ref_count -= 1U;
    if (found->second.m_ref_count == 0U) {
        wgpuBindGroupLayoutRelease(found->second.m_layout);
        cache.erase(found);
    }
}

struct PreparedMaterialGpuState {
    WGPUBindGroupLayout m_bind_group_layout{nullptr};
    std::string m_bind_group_layout_key;
    WGPUBuffer m_uniform_buffer{nullptr};
    WGPUBindGroup m_bind_group{nullptr};

    PreparedMaterialGpuState() = default;
    PreparedMaterialGpuState(WGPUBindGroupLayout bind_group_layout,
        std::string bind_group_layout_key,
        WGPUBuffer uniform_buffer,
        WGPUBindGroup bind_group)
        : m_bind_group_layout(bind_group_layout), m_bind_group_layout_key(std::move(bind_group_layout_key)),
          m_uniform_buffer(uniform_buffer), m_bind_group(bind_group) {}

    PreparedMaterialGpuState(const PreparedMaterialGpuState&) = delete;
    PreparedMaterialGpuState& operator=(const PreparedMaterialGpuState&) = delete;

    // Moves WebGPU handles without duplicating ownership.
    PreparedMaterialGpuState(PreparedMaterialGpuState&& other) noexcept {
        take_from(other);
    }

    PreparedMaterialGpuState& operator=(PreparedMaterialGpuState&& other) noexcept = delete;

    // Releases any WebGPU handles still owned by this temporary bundle.
    ~PreparedMaterialGpuState() {
        release();
    }

    // Releases a temporary or owned material GPU state bundle.
    void release() noexcept {
        if (m_bind_group != nullptr) {
            wgpuBindGroupRelease(m_bind_group);
            m_bind_group = nullptr;
        }
        if (m_uniform_buffer != nullptr) {
            wgpuBufferRelease(m_uniform_buffer);
            m_uniform_buffer = nullptr;
        }
        if (m_bind_group_layout != nullptr) {
            release_cached_material_bind_group_layout(m_bind_group_layout_key, m_bind_group_layout);
            m_bind_group_layout = nullptr;
            m_bind_group_layout_key.clear();
        }
    }

    // Transfers the bind group layout out to the owning material.
    [[nodiscard]] WGPUBindGroupLayout take_bind_group_layout() noexcept {
        WGPUBindGroupLayout handle = m_bind_group_layout;
        m_bind_group_layout = nullptr;
        return handle;
    }

    // Transfers the shared bind group layout cache key out to the owning material.
    [[nodiscard]] std::string take_bind_group_layout_key() noexcept {
        return std::move(m_bind_group_layout_key);
    }

    // Transfers the uniform buffer out to the owning material.
    [[nodiscard]] WGPUBuffer take_uniform_buffer() noexcept {
        WGPUBuffer handle = m_uniform_buffer;
        m_uniform_buffer = nullptr;
        return handle;
    }

    // Transfers the bind group out to the owning material.
    [[nodiscard]] WGPUBindGroup take_bind_group() noexcept {
        WGPUBindGroup handle = m_bind_group;
        m_bind_group = nullptr;
        return handle;
    }

private:
    // Takes WebGPU handles from another bundle and leaves it empty.
    void take_from(PreparedMaterialGpuState& other) noexcept {
        m_bind_group_layout = other.m_bind_group_layout;
        m_bind_group_layout_key = std::move(other.m_bind_group_layout_key);
        m_uniform_buffer = other.m_uniform_buffer;
        m_bind_group = other.m_bind_group;
        other.m_bind_group_layout = nullptr;
        other.m_bind_group_layout_key.clear();
        other.m_uniform_buffer = nullptr;
        other.m_bind_group = nullptr;
    }
};

// Creates and uploads the material uniform buffer when the shader declares material uniforms.
WGPUBuffer create_uniform_buffer(
    const GpuContext& context, const std::string& label, const std::vector<std::byte>& uniform_bytes) {
    if (uniform_bytes.empty()) {
        return nullptr;
    }

    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.label = gpu::string_view(label);
    descriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    descriptor.size = uniform_bytes.size();
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(context.m_device, &descriptor);
    if (buffer == nullptr) {
        throw EngineError("wgpuDeviceCreateBuffer returned null for material uniform buffer '" + label + "'.");
    }

    wgpuQueueWriteBuffer(context.m_queue, buffer, 0, uniform_bytes.data(), uniform_bytes.size());
    return buffer;
}

// Creates GPU state for one validated material property bag.
PreparedMaterialGpuState create_material_gpu_state(
    const GpuContext& context, const std::string& label, const Shader& shader, const PropertyBag& properties) {
    PreparedMaterialGpuState state;

    std::vector<std::byte> uniform_bytes = properties.pack_uniforms_for_scope(shader, ShaderParameterScope::Material);

    const std::string uniform_label = label + " material uniforms";
    state.m_uniform_buffer = create_uniform_buffer(context, uniform_label, uniform_bytes);

    std::vector<WGPUBindGroupLayoutEntry> layout_entries;
    std::vector<WGPUBindGroupEntry> bind_entries;
    std::string layout_key = "device:" + std::to_string(reinterpret_cast<std::uintptr_t>(context.m_device)) +
                             ";shader:" + shader.label() + ":" + std::to_string(shader.revision()) + ";";
    std::uint32_t next_binding = 0;
    if (!uniform_bytes.empty()) {
        layout_key += "uniform:" + std::to_string(next_binding) + ":" + std::to_string(uniform_bytes.size()) + ";";
        WGPUBindGroupLayoutEntry layout_entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layout_entry.binding = next_binding;
        layout_entry.visibility = WGPUShaderStage_Fragment;
        layout_entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        layout_entry.buffer.type = WGPUBufferBindingType_Uniform;
        layout_entry.buffer.minBindingSize = uniform_bytes.size();
        layout_entries.push_back(layout_entry);

        WGPUBindGroupEntry bind_entry = WGPU_BIND_GROUP_ENTRY_INIT;
        bind_entry.binding = next_binding;
        bind_entry.buffer = state.m_uniform_buffer;
        bind_entry.size = uniform_bytes.size();
        bind_entries.push_back(bind_entry);
        next_binding += 1;
    }

    const std::vector<ShaderParameter> material_parameters =
        shader.parameters_for_scope(ShaderParameterScope::Material);
    for (const ShaderParameter& parameter : material_parameters) {
        if (parameter.m_type != ShaderParameterType::Texture) {
            continue;
        }
        const PropertyValue* value = properties.get(parameter.m_name);
        if (value == nullptr) {
            continue;
        }
        Texture* texture = std::get<Ptr<Texture>>(*value).get();
        if (texture == nullptr) {
            throw EngineError("Material texture property '" + parameter.m_name + "' must not be null.");
        }
        if (texture->view() == nullptr || texture->sampler() == nullptr) {
            throw EngineError("Material texture property '" + parameter.m_name + "' requires a GPU-ready texture.");
        }

        layout_key += "texture:" + parameter.m_name + ":" + std::to_string(next_binding) + ":" +
                      texture_sample_type_key(texture->pixel_format()) + ";";
        WGPUBindGroupLayoutEntry texture_layout = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        texture_layout.binding = next_binding;
        texture_layout.visibility = WGPUShaderStage_Fragment;
        texture_layout.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
        texture_layout.texture.sampleType = texture->pixel_format() == TexturePixelFormat::R16Float
                                                ? WGPUTextureSampleType_UnfilterableFloat
                                                : WGPUTextureSampleType_Float;
        texture_layout.texture.viewDimension = WGPUTextureViewDimension_2D;
        layout_entries.push_back(texture_layout);

        WGPUBindGroupEntry texture_entry = WGPU_BIND_GROUP_ENTRY_INIT;
        texture_entry.binding = next_binding;
        texture_entry.textureView = texture->view();
        bind_entries.push_back(texture_entry);
        next_binding += 1;

        layout_key += "sampler:" + parameter.m_name + ":" + std::to_string(next_binding) + ":" +
                      sampler_binding_type_key(texture->pixel_format()) + ";";
        WGPUBindGroupLayoutEntry sampler_layout = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        sampler_layout.binding = next_binding;
        sampler_layout.visibility = WGPUShaderStage_Fragment;
        sampler_layout.sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
        sampler_layout.sampler.type = texture->pixel_format() == TexturePixelFormat::R16Float
                                          ? WGPUSamplerBindingType_NonFiltering
                                          : WGPUSamplerBindingType_Filtering;
        layout_entries.push_back(sampler_layout);

        WGPUBindGroupEntry sampler_entry = WGPU_BIND_GROUP_ENTRY_INIT;
        sampler_entry.binding = next_binding;
        sampler_entry.sampler = texture->sampler();
        bind_entries.push_back(sampler_entry);
        next_binding += 1;
    }

    state.m_bind_group_layout = acquire_cached_material_bind_group_layout(context, label, layout_key, layout_entries);
    state.m_bind_group_layout_key = std::move(layout_key);

    WGPUBindGroupDescriptor bind_group_descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bind_group_descriptor.label = gpu::string_view(label);
    bind_group_descriptor.layout = state.m_bind_group_layout;
    bind_group_descriptor.entryCount = bind_entries.size();
    bind_group_descriptor.entries = bind_entries.empty() ? nullptr : bind_entries.data();
    state.m_bind_group = wgpuDeviceCreateBindGroup(context.m_device, &bind_group_descriptor);
    if (state.m_bind_group == nullptr) {
        throw EngineError("wgpuDeviceCreateBindGroup returned null for material '" + label + "'.");
    }

    return state;
}

} // namespace

// Allocates a labeled material resource using the creating Resources context.
Material::Material(GpuContext gpu, std::string label) : m_gpu(std::move(gpu)), m_label(std::move(label)) {
    if (m_label.empty()) {
        throw EngineError("Material label must not be empty.");
    }
}

// Releases owned GPU material state.
Material::~Material() {
    release_gpu_state();
}

// Initializes this material and validates its properties against material scope.
void Material::init(Shader& shader, PropertyBag properties) {
    properties.validate_for_scope(shader, ShaderParameterScope::Material);
    release_gpu_state();
    m_shader = &shader;
    m_properties = std::move(properties);
    prepare_gpu_state();
    m_revision += 1;
}

// Replaces one property and refreshes validation state.
void Material::set_property(std::string name, PropertyValue value) {
    if (!m_shader) {
        throw EngineError("Material shader reference is not initialized.");
    }

    PropertyBag updated = m_properties;
    updated.set(std::move(name), value);
    updated.validate_for_scope(*m_shader, ShaderParameterScope::Material);
    if (gpu_context_is_empty(m_gpu)) {
        release_gpu_state();
        m_properties = std::move(updated);
        m_revision += 1;
        return;
    }
    if (!gpu_context_is_ready(m_gpu)) {
        throw EngineError("Material GPU preparation requires a WebGPU device and queue.");
    }

    PreparedMaterialGpuState next_state = create_material_gpu_state(m_gpu, m_label, *m_shader, updated);

    release_gpu_state();
    m_bind_group_layout = next_state.take_bind_group_layout();
    m_bind_group_layout_key = next_state.take_bind_group_layout_key();
    m_uniform_buffer = next_state.take_uniform_buffer();
    m_bind_group = next_state.take_bind_group();
    m_properties = std::move(updated);
    m_revision += 1;
}

// Returns the referenced shader.
Shader& Material::shader() {
    return *m_shader;
}

// Returns the referenced shader.
const Shader& Material::shader() const {
    return *m_shader;
}

// Returns the material label.
const std::string& Material::label() const noexcept {
    return m_label;
}

// Returns the material properties.
const PropertyBag& Material::properties() const noexcept {
    return m_properties;
}

// Returns the WebGPU bind group layout, null for CPU-only resources.
WGPUBindGroupLayout Material::bind_group_layout() const noexcept {
    return m_bind_group_layout;
}

// Returns the material uniform buffer, null when no uniform data exists.
WGPUBuffer Material::uniform_buffer() const noexcept {
    return m_uniform_buffer;
}

// Returns the WebGPU bind group, null for CPU-only resources.
WGPUBindGroup Material::bind_group() const noexcept {
    return m_bind_group;
}

// Returns the material revision.
std::uint64_t Material::revision() const noexcept {
    return m_revision;
}

// Creates GPU material uniform and bind-group state.
void Material::prepare_gpu_state() {
    if (gpu_context_is_empty(m_gpu)) {
        return;
    }
    if (!gpu_context_is_ready(m_gpu)) {
        throw EngineError("Material GPU preparation requires a WebGPU device and queue.");
    }
    if (!m_shader) {
        throw EngineError("Material shader reference is not initialized.");
    }

    PreparedMaterialGpuState next_state = create_material_gpu_state(m_gpu, m_label, *m_shader, m_properties);

    release_gpu_state();
    m_bind_group_layout = next_state.take_bind_group_layout();
    m_bind_group_layout_key = next_state.take_bind_group_layout_key();
    m_uniform_buffer = next_state.take_uniform_buffer();
    m_bind_group = next_state.take_bind_group();
}

// Releases all owned WebGPU material handles.
void Material::release_gpu_state() noexcept {
    PreparedMaterialGpuState state{
        m_bind_group_layout, std::move(m_bind_group_layout_key), m_uniform_buffer, m_bind_group};
    m_bind_group_layout = nullptr;
    m_bind_group_layout_key.clear();
    m_uniform_buffer = nullptr;
    m_bind_group = nullptr;
}

} // namespace ofg
