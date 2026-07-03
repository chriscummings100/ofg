// Observable base class for asynchronously loaded OFG resources.
//
// Resource extends Object so persistent observers can use Ptr<T>. Resources
// owns mutation of the load state; gameplay and renderer code should only read
// state, source URI, and load diagnostics.
#pragma once

#include "ofg/core/object.hpp"

#include <string>

namespace ofg {

enum class ResourceState {
    Unloaded,
    Queued,
    LoadingRootBlob,
    DiscoveringDependencies,
    WaitingForDependencies,
    Importing,
    Loaded,
    Failed,
};

// Converts a ResourceState into a stable diagnostic string.
[[nodiscard]] const char* resource_state_name(ResourceState state) noexcept;

class Resource : public Object {
public:
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
    Resource(Resource&&) = delete;
    Resource& operator=(Resource&&) = delete;
    ~Resource() override = default;

    // Returns the current resource loading state.
    [[nodiscard]] ResourceState state() const noexcept;
    // Returns the normalized source URI, or an empty string when not URI-backed.
    [[nodiscard]] const std::string& source_uri() const noexcept;
    // Returns the terminal failure message, or an empty string.
    [[nodiscard]] const std::string& load_error() const noexcept;
    // Reports whether this resource is still scheduled for loading work.
    [[nodiscard]] bool is_in_progress() const noexcept;
    // Reports whether this resource completed successfully.
    [[nodiscard]] bool is_loaded() const noexcept;
    // Reports whether this resource completed with a failure.
    [[nodiscard]] bool is_failed() const noexcept;
    // Reports whether this resource has reached a terminal state.
    [[nodiscard]] bool is_terminal() const noexcept;

protected:
    // Creates an unloaded observable resource.
    Resource() noexcept = default;

private:
    friend class Resources;

    // Replaces the source URI reported to observers.
    void set_source_uri(std::string source_uri);
    // Replaces the current resource state.
    void set_resource_state(ResourceState state) noexcept;
    // Marks the resource failed and stores a diagnostic.
    void set_resource_failed(std::string message);
    // Clears any previous load error.
    void clear_resource_error();

    ResourceState m_state{ResourceState::Unloaded};
    std::string m_source_uri;
    std::string m_load_error;
};

} // namespace ofg
