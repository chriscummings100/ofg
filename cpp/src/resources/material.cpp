// Mutable material resource for OFG renderer shader parameters.
#include "ofg/resources/material.hpp"

#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/shader.hpp"
#include "ofg/resources/texture.hpp"
#include "ofg/render/webgpu_common.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ofg {
namespace {

struct PreparedMaterialGpuState {
    WGPUBindGroupLayout m_bind_group_layout{nullptr};
    WGPUBuffer m_uniform_buffer{nullptr};
    WGPUBindGroup m_bind_group{nullptr};

    PreparedMaterialGpuState() = default;
    PreparedMaterialGpuState(WGPUBindGroupLayout bind_group_layout, WGPUBuffer uniform_buffer, WGPUBindGroup bind_group)
        : m_bind_group_layout(bind_group_layout), m_uniform_buffer(uniform_buffer), m_bind_group(bind_group) {}

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
            wgpuBindGroupLayoutRelease(m_bind_group_layout);
            m_bind_group_layout = nullptr;
        }
    }

    // Transfers the bind group layout out to the owning material.
    [[nodiscard]] WGPUBindGroupLayout take_bind_group_layout() noexcept {
        WGPUBindGroupLayout handle = m_bind_group_layout;
        m_bind_group_layout = nullptr;
        return handle;
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
        m_uniform_buffer = other.m_uniform_buffer;
        m_bind_group = other.m_bind_group;
        other.m_bind_group_layout = nullptr;
        other.m_uniform_buffer = nullptr;
        other.m_bind_group = nullptr;
    }
};

// Creates and uploads the material uniform buffer when the shader declares material uniforms.
WGPUBuffer create_uniform_buffer(const GpuContext& context,
    const std::string& label,
    const std::vector<std::byte>& uniform_bytes,
    std::string& error) {
    if (uniform_bytes.empty()) {
        return nullptr;
    }

    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.label = gpu::string_view(label);
    descriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    descriptor.size = uniform_bytes.size();
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(context.m_device, &descriptor);
    if (buffer == nullptr) {
        error = "wgpuDeviceCreateBuffer returned null for material uniform buffer '" + label + "'.";
        return nullptr;
    }

    wgpuQueueWriteBuffer(context.m_queue, buffer, 0, uniform_bytes.data(), uniform_bytes.size());
    return buffer;
}

// Creates GPU state for one validated material property bag.
std::optional<PreparedMaterialGpuState> create_material_gpu_state(const GpuContext& context,
    const std::string& label,
    const Shader& shader,
    const PropertyBag& properties,
    std::string& error) {
    PreparedMaterialGpuState state;

    std::optional<std::vector<std::byte>> uniform_bytes =
        properties.pack_uniforms_for_scope(shader, ShaderParameterScope::Material, error);
    if (!uniform_bytes.has_value()) {
        return std::nullopt;
    }

    const std::string uniform_label = label + " material uniforms";
    state.m_uniform_buffer = create_uniform_buffer(context, uniform_label, *uniform_bytes, error);
    if (!uniform_bytes->empty() && state.m_uniform_buffer == nullptr) {
        return std::nullopt;
    }

    std::vector<WGPUBindGroupLayoutEntry> layout_entries;
    std::vector<WGPUBindGroupEntry> bind_entries;
    std::uint32_t next_binding = 0;
    if (!uniform_bytes->empty()) {
        WGPUBindGroupLayoutEntry layout_entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layout_entry.binding = next_binding;
        layout_entry.visibility = WGPUShaderStage_Fragment;
        layout_entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        layout_entry.buffer.type = WGPUBufferBindingType_Uniform;
        layout_entry.buffer.minBindingSize = uniform_bytes->size();
        layout_entries.push_back(layout_entry);

        WGPUBindGroupEntry bind_entry = WGPU_BIND_GROUP_ENTRY_INIT;
        bind_entry.binding = next_binding;
        bind_entry.buffer = state.m_uniform_buffer;
        bind_entry.size = uniform_bytes->size();
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
        Texture* texture = std::get<Texture*>(*value);
        if (texture->view() == nullptr || texture->sampler() == nullptr) {
            error = "Material texture property '" + parameter.m_name + "' requires a GPU-ready texture.";
            return std::nullopt;
        }

        WGPUBindGroupLayoutEntry texture_layout = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        texture_layout.binding = next_binding;
        texture_layout.visibility = WGPUShaderStage_Fragment;
        texture_layout.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
        texture_layout.texture.sampleType = WGPUTextureSampleType_Float;
        texture_layout.texture.viewDimension = WGPUTextureViewDimension_2D;
        layout_entries.push_back(texture_layout);

        WGPUBindGroupEntry texture_entry = WGPU_BIND_GROUP_ENTRY_INIT;
        texture_entry.binding = next_binding;
        texture_entry.textureView = texture->view();
        bind_entries.push_back(texture_entry);
        next_binding += 1;

        WGPUBindGroupLayoutEntry sampler_layout = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        sampler_layout.binding = next_binding;
        sampler_layout.visibility = WGPUShaderStage_Fragment;
        sampler_layout.sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
        sampler_layout.sampler.type = WGPUSamplerBindingType_Filtering;
        layout_entries.push_back(sampler_layout);

        WGPUBindGroupEntry sampler_entry = WGPU_BIND_GROUP_ENTRY_INIT;
        sampler_entry.binding = next_binding;
        sampler_entry.sampler = texture->sampler();
        bind_entries.push_back(sampler_entry);
        next_binding += 1;
    }

    WGPUBindGroupLayoutDescriptor layout_descriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layout_descriptor.label = gpu::string_view(label);
    layout_descriptor.entryCount = layout_entries.size();
    layout_descriptor.entries = layout_entries.empty() ? nullptr : layout_entries.data();
    state.m_bind_group_layout = wgpuDeviceCreateBindGroupLayout(context.m_device, &layout_descriptor);
    if (state.m_bind_group_layout == nullptr) {
        error = "wgpuDeviceCreateBindGroupLayout returned null for material '" + label + "'.";
        return std::nullopt;
    }

    WGPUBindGroupDescriptor bind_group_descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bind_group_descriptor.label = gpu::string_view(label);
    bind_group_descriptor.layout = state.m_bind_group_layout;
    bind_group_descriptor.entryCount = bind_entries.size();
    bind_group_descriptor.entries = bind_entries.empty() ? nullptr : bind_entries.data();
    state.m_bind_group = wgpuDeviceCreateBindGroup(context.m_device, &bind_group_descriptor);
    if (state.m_bind_group == nullptr) {
        error = "wgpuDeviceCreateBindGroup returned null for material '" + label + "'.";
        return std::nullopt;
    }

    error.clear();
    return std::make_optional<PreparedMaterialGpuState>(std::move(state));
}

} // namespace

// Stores validated material data; use create() for validation.
Material::Material(GpuContext gpu, std::string label, Shader& shader, PropertyBag properties)
    : m_gpu(std::move(gpu)), m_label(std::move(label)), m_shader(&shader), m_properties(std::move(properties)) {}

// Moves material CPU and GPU handles without duplicating ownership.
Material::Material(Material&& other) noexcept
    : m_gpu(std::move(other.m_gpu)), m_label(std::move(other.m_label)), m_shader(other.m_shader),
      m_properties(std::move(other.m_properties)), m_bind_group_layout(other.m_bind_group_layout),
      m_uniform_buffer(other.m_uniform_buffer), m_bind_group(other.m_bind_group), m_revision(other.m_revision) {
    other.m_shader = nullptr;
    other.m_bind_group_layout = nullptr;
    other.m_uniform_buffer = nullptr;
    other.m_bind_group = nullptr;
}

// Moves material CPU and GPU handles without duplicating ownership.
Material& Material::operator=(Material&& other) noexcept {
    if (this != &other) {
        release_gpu_state();
        m_gpu = std::move(other.m_gpu);
        m_label = std::move(other.m_label);
        m_shader = other.m_shader;
        m_properties = std::move(other.m_properties);
        m_bind_group_layout = other.m_bind_group_layout;
        m_uniform_buffer = other.m_uniform_buffer;
        m_bind_group = other.m_bind_group;
        m_revision = other.m_revision;
        other.m_shader = nullptr;
        other.m_bind_group_layout = nullptr;
        other.m_uniform_buffer = nullptr;
        other.m_bind_group = nullptr;
    }
    return *this;
}

// Releases owned GPU material state.
Material::~Material() {
    release_gpu_state();
}

// Creates a material and validates its properties against material scope.
std::optional<Material> Material::create(
    GpuContext gpu, std::string label, Shader& shader, PropertyBag properties, std::string& error) {
    if (label.empty()) {
        error = "Material label must not be empty.";
        return std::nullopt;
    }
    if (!properties.validate_for_scope(shader, ShaderParameterScope::Material, error)) {
        return std::nullopt;
    }
    Material material(std::move(gpu), std::move(label), shader, std::move(properties));
    if (!material.prepare_gpu_state(error)) {
        return std::nullopt;
    }
    error.clear();
    return material;
}

// Replaces one property and refreshes validation state.
bool Material::set_property(std::string name, PropertyValue value, std::string& error) {
    if (m_shader == nullptr) {
        error = "Material shader reference is not initialized.";
        return false;
    }

    PropertyBag updated = m_properties;
    updated.set(std::move(name), value);
    if (!updated.validate_for_scope(*m_shader, ShaderParameterScope::Material, error)) {
        return false;
    }
    if (gpu_context_is_empty(m_gpu)) {
        release_gpu_state();
        m_properties = std::move(updated);
        m_revision += 1;
        error.clear();
        return true;
    }
    if (!gpu_context_is_ready(m_gpu)) {
        error = "Material GPU preparation requires a WebGPU device and queue.";
        return false;
    }

    std::optional<PreparedMaterialGpuState> next_state =
        create_material_gpu_state(m_gpu, m_label, *m_shader, updated, error);
    if (!next_state.has_value()) {
        return false;
    }

    release_gpu_state();
    m_bind_group_layout = next_state->take_bind_group_layout();
    m_uniform_buffer = next_state->take_uniform_buffer();
    m_bind_group = next_state->take_bind_group();
    m_properties = std::move(updated);
    m_revision += 1;
    error.clear();
    return true;
}

// Returns the referenced shader.
const Shader& Material::shader() const noexcept {
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
bool Material::prepare_gpu_state(std::string& error) {
    if (gpu_context_is_empty(m_gpu)) {
        error.clear();
        return true;
    }
    if (!gpu_context_is_ready(m_gpu)) {
        error = "Material GPU preparation requires a WebGPU device and queue.";
        return false;
    }
    if (m_shader == nullptr) {
        error = "Material shader reference is not initialized.";
        return false;
    }

    std::optional<PreparedMaterialGpuState> next_state =
        create_material_gpu_state(m_gpu, m_label, *m_shader, m_properties, error);
    if (!next_state.has_value()) {
        return false;
    }

    release_gpu_state();
    m_bind_group_layout = next_state->take_bind_group_layout();
    m_uniform_buffer = next_state->take_uniform_buffer();
    m_bind_group = next_state->take_bind_group();
    error.clear();
    return true;
}

// Releases all owned WebGPU material handles.
void Material::release_gpu_state() noexcept {
    PreparedMaterialGpuState state{m_bind_group_layout, m_uniform_buffer, m_bind_group};
    m_bind_group_layout = nullptr;
    m_uniform_buffer = nullptr;
    m_bind_group = nullptr;
}

} // namespace ofg
