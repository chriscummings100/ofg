// Observable base class for asynchronously loaded OFG resources.
#include "ofg/resources/resource.hpp"

#include <utility>

namespace ofg {

// Converts a ResourceState into a stable diagnostic string.
const char* resource_state_name(ResourceState state) noexcept {
    switch (state) {
    case ResourceState::Unloaded:
        return "unloaded";
    case ResourceState::Queued:
        return "queued";
    case ResourceState::LoadingRootBlob:
        return "loading_root_blob";
    case ResourceState::DiscoveringDependencies:
        return "discovering_dependencies";
    case ResourceState::WaitingForDependencies:
        return "waiting_for_dependencies";
    case ResourceState::Importing:
        return "importing";
    case ResourceState::Loaded:
        return "loaded";
    case ResourceState::Failed:
        return "failed";
    }
    return "unknown";
}

// Returns the current resource loading state.
ResourceState Resource::state() const noexcept {
    return m_state;
}

// Returns the normalized source URI, or an empty string when not URI-backed.
const std::string& Resource::source_uri() const noexcept {
    return m_source_uri;
}

// Returns the terminal failure message, or an empty string.
const std::string& Resource::load_error() const noexcept {
    return m_load_error;
}

// Reports whether this resource is still scheduled for loading work.
bool Resource::is_in_progress() const noexcept {
    return m_state == ResourceState::Queued || m_state == ResourceState::LoadingRootBlob ||
           m_state == ResourceState::DiscoveringDependencies || m_state == ResourceState::WaitingForDependencies ||
           m_state == ResourceState::Importing;
}

// Reports whether this resource completed successfully.
bool Resource::is_loaded() const noexcept {
    return m_state == ResourceState::Loaded;
}

// Reports whether this resource completed with a failure.
bool Resource::is_failed() const noexcept {
    return m_state == ResourceState::Failed;
}

// Reports whether this resource has reached a terminal state.
bool Resource::is_terminal() const noexcept {
    return is_loaded() || is_failed();
}

// Replaces the source URI reported to observers.
void Resource::set_source_uri(std::string source_uri) {
    m_source_uri = std::move(source_uri);
}

// Replaces the current resource state.
void Resource::set_resource_state(ResourceState state) noexcept {
    m_state = state;
}

// Marks the resource failed and stores a diagnostic.
void Resource::set_resource_failed(std::string message) {
    if (message.empty()) {
        message = "Unknown resource loading error.";
    }
    m_load_error = std::move(message);
    m_state = ResourceState::Failed;
}

// Clears any previous load error.
void Resource::clear_resource_error() {
    m_load_error.clear();
}

} // namespace ofg
