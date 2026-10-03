// Shared asset identity and pollable loading state; concrete types own their data.
#pragma once

#include <string>

namespace ofg {

enum class ResourceState
{
    Loading,
    Loaded,
    Failed,
};

class Resource
{
public:
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
    Resource(Resource&&) = delete;
    Resource& operator=(Resource&&) = delete;
    // Releases concrete data when the last shared owner releases the resource.
    virtual ~Resource() = default;

    // Returns the exact key used to identify this asset in Resources.
    [[nodiscard]] const std::string& key() const noexcept { return m_key; }
    // Returns loading, successful completion, or terminal failure.
    [[nodiscard]] ResourceState state() const noexcept { return m_state; }
    // Reports whether asset data is ready to consume.
    [[nodiscard]] bool isLoaded() const noexcept { return m_state == ResourceState::Loaded; }
    // Reports a terminal loading error.
    [[nodiscard]] bool isFailed() const noexcept { return m_state == ResourceState::Failed; }
    // Reports completion, including failure, so polling cannot mistake an error for pending work.
    [[nodiscard]] bool isFinished() const noexcept { return m_state != ResourceState::Loading; }
    // Returns the diagnostic on failure, or an empty string otherwise.
    [[nodiscard]] const std::string& error() const noexcept { return m_error; }

protected:
    // Creates an immediately ready, uncached procedural resource with an empty key.
    Resource();
    // Starts a resource pending; constructors must not perform loading or request dependencies.
    explicit Resource(std::string key);
    // Performs bounded work: false means pending, true means ready; throw on failure.
    // Called on the application thread. Shared dependency handles belong to the concrete resource.
    [[nodiscard]] virtual bool loadStep();

private:
    friend class Resources;

    // Advances pending work once and converts loader exceptions into a terminal diagnostic.
    void advanceLoading();

    std::string m_key;
    ResourceState m_state{ResourceState::Loading};
    std::string m_error;
};

} // namespace ofg
