// Pollable resource loading, weak-cache lifetime and state-entry integration contracts.
#include "doctest.h"

#include "resources/resources.h"
#include "state/state.h"

#include <memory>
#include <ostream>
#include <string>
#include <utility>

namespace {

class StagedResource : public ofg::Resource
{
public:
    // Creates a pending fixture which needs two update passes.
    explicit StagedResource(std::string key)
        : Resource(std::move(key))
    {
    }
    // Exposes scheduler progress without relying on timing or threads.
    [[nodiscard]] int steps() const noexcept { return m_steps; }

protected:
    // Completes on the second scheduler pass.
    bool loadStep() override { return ++m_steps == 2; }

private:
    int m_steps{0};
};

class FailedResource : public ofg::Resource
{
public:
    // Creates a pending fixture which will report a loading error.
    explicit FailedResource(std::string key)
        : Resource(std::move(key))
    {
    }

protected:
    // Simulates a concrete loader rejecting malformed input.
    bool loadStep() override { throw ofg::EngineError("Fixture data is invalid."); }
};

class DependentResource : public ofg::Resource
{
public:
    // Creates an asset which will retain a shared child asset.
    explicit DependentResource(std::string key)
        : Resource(std::move(key))
    {
    }
    // Observes the child without adding another owner in lifetime tests.
    [[nodiscard]] std::weak_ptr<StagedResource> dependency() const noexcept { return m_dependency; }

protected:
    // Requests a child during scheduling, then waits for it on subsequent passes.
    bool loadStep() override
    {
        if (!m_dependency)
        {
            m_dependency = ofg::Resources::loadResourceAsync<StagedResource>(key() + "/child");
        }
        return m_dependency->isLoaded();
    }

private:
    std::shared_ptr<StagedResource> m_dependency;
};

class ReentrantResource : public ofg::Resource
{
public:
    // Creates a deliberately invalid recursive-loader fixture.
    explicit ReentrantResource(std::string key)
        : Resource(std::move(key))
    {
    }

protected:
    // Exercises the scheduler's reentry boundary.
    bool loadStep() override
    {
        ofg::Resources::update();
        return true;
    }
};

class BlockingResource : public ofg::Resource
{
public:
    // Creates a loader that incorrectly tries to block inside the scheduler.
    explicit BlockingResource(std::string key)
        : Resource(std::move(key))
    {
    }

protected:
    // Exercises native blocking-load rejection from within a load step.
    bool loadStep() override
    {
        (void)ofg::Resources::loadResource<StagedResource>(key() + "/child");
        return true;
    }
};

class ResourceEntryState : public ofg::State
{
public:
    // Observes the single handle retained through entry and main.
    [[nodiscard]] const std::shared_ptr<StagedResource>& resource() const noexcept { return m_resource; }

protected:
    // Starts loading once, keeping the same handle for the state's entire lifetime.
    void onEnterStart() override { m_resource = ofg::Resources::loadResourceAsync<StagedResource>("state-entry"); }
    // Keeps entry pending until the asset is ready, rejecting a failed load.
    bool onEnter() override
    {
        if (m_resource->isFailed())
        {
            throw ofg::EngineError(m_resource->error());
        }
        return m_resource->isLoaded();
    }

private:
    std::shared_ptr<StagedResource> m_resource;
};

} // namespace

TEST_CASE("resource async loading returns one pollable shared identity")
{
    auto resource = ofg::Resources::loadResourceAsync<StagedResource>("staged");
    CHECK(resource->key() == "staged");
    CHECK(resource->state() == ofg::ResourceState::Loading);
    CHECK_FALSE(resource->isLoaded());
    CHECK_FALSE(resource->isFinished());
    CHECK_FALSE(resource->isFailed());
    CHECK(resource->steps() == 0);
    CHECK(ofg::Resources::loadResourceAsync<StagedResource>("staged") == resource);

    ofg::Resources::update();
    CHECK(resource->steps() == 1);
    CHECK_FALSE(resource->isFinished());
    ofg::Resources::update();
    CHECK(resource->isLoaded());
    CHECK(resource->isFinished());
    CHECK(resource->error().empty());
    CHECK(ofg::Resources::loadResourceAsync<StagedResource>("staged") == resource);

    ofg::Resources::update();
    CHECK(resource->steps() == 2);
}

TEST_CASE("weak resource lookup does not retain abandoned pending or loaded assets")
{
    auto resource = ofg::Resources::loadResourceAsync<StagedResource>("weak");
    std::weak_ptr<StagedResource> observer = resource;
    resource.reset();
    CHECK(observer.expired());
    ofg::Resources::update();

    resource = ofg::Resources::loadResource<StagedResource>("weak");
    REQUIRE(resource->isLoaded());
    observer = resource;
    resource.reset();
    CHECK(observer.expired());

    resource = ofg::Resources::loadResourceAsync<StagedResource>("weak");
    CHECK_FALSE(resource->isFinished());
    CHECK(resource->steps() == 0);
}

TEST_CASE("failed resources finish polling and remain deduplicated until released")
{
    auto resource = ofg::Resources::loadResourceAsync<FailedResource>("failed");
    ofg::Resources::update();
    CHECK(resource->isFailed());
    CHECK(resource->isFinished());
    CHECK_FALSE(resource->isLoaded());
    CHECK(resource->error() == "Fixture data is invalid.");
    CHECK(ofg::Resources::loadResource<FailedResource>("failed") == resource);
    std::weak_ptr<FailedResource> observer = resource;
    resource.reset();
    CHECK(observer.expired());

    resource = ofg::Resources::loadResourceAsync<FailedResource>("failed");
    CHECK_FALSE(resource->isFinished());
    CHECK(resource->error().empty());
}

TEST_CASE("resource lookup rejects empty keys and incompatible live types")
{
    CHECK_THROWS_AS((void)ofg::Resources::loadResourceAsync<StagedResource>(""), ofg::EngineError);
    auto resource = ofg::Resources::loadResourceAsync<StagedResource>("typed");
    CHECK_THROWS_WITH_AS(
        (void)ofg::Resources::loadResourceAsync<FailedResource>("typed"),
        doctest::Contains("different type"),
        ofg::EngineError
    );
    CHECK(ofg::Resources::loadResourceAsync<StagedResource>("typed") == resource);
}

TEST_CASE("loading dependencies can extend the dictionary and own child lifetimes")
{
    auto parent = ofg::Resources::loadResourceAsync<DependentResource>("parent");
    ofg::Resources::update();
    auto childObserver = parent->dependency();
    REQUIRE_FALSE(childObserver.expired());
    CHECK(childObserver.lock()->steps() == 0); // Created during this pass; starts next frame.
    CHECK_FALSE(parent->isFinished());

    // Child needs two passes; dictionary order may defer parent readiness by one extra pass.
    for (int frame = 0; frame < 3; ++frame)
    {
        ofg::Resources::update();
    }
    REQUIRE(parent->isLoaded());
    REQUIRE(childObserver.lock()->isLoaded());
    auto anotherOwner = ofg::Resources::loadResourceAsync<StagedResource>("parent/child");
    CHECK(anotherOwner == childObserver.lock());
    parent.reset();
    CHECK_FALSE(childObserver.expired());
    anotherOwner.reset();
    CHECK(childObserver.expired());
}

TEST_CASE("native blocking load pumps shared dependencies to completion")
{
    auto pending = ofg::Resources::loadResourceAsync<DependentResource>("blocking");
    auto resource = ofg::Resources::loadResource<DependentResource>("blocking");
    CHECK(resource == pending);
    CHECK(resource->isLoaded());
    CHECK(resource->dependency().lock()->isLoaded());
}

TEST_CASE("invalid recursive loading fails without leaving the scheduler stuck")
{
    auto recursive = ofg::Resources::loadResource<ReentrantResource>("recursive");
    CHECK(recursive->isFailed());
    CHECK(recursive->error().find("recursively") != std::string::npos);
    auto blocking = ofg::Resources::loadResource<BlockingResource>("nested-blocking");
    CHECK(blocking->isFailed());
    CHECK(blocking->error().find("Blocking") != std::string::npos);
    CHECK(ofg::Resources::loadResource<StagedResource>("after-recursion")->isLoaded());
}

TEST_CASE("state entry polls one resource handle which remains usable in main")
{
    ResourceEntryState state;
    state.update();
    CHECK(state.phase() == ofg::StatePhase::Entering);
    auto resource = state.resource();
    REQUIRE(resource != nullptr);

    ofg::Resources::update();
    state.update();
    CHECK(state.phase() == ofg::StatePhase::Entering);
    ofg::Resources::update();
    state.update();
    CHECK(state.phase() == ofg::StatePhase::Main);
    CHECK(state.resource() == resource);
    CHECK(resource->isLoaded());
}
