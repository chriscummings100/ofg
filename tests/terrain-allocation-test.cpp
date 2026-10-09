// Deterministic allocator faults exercise real controller exception boundaries without production fault hooks.
#include "terrain/terrain-stream.h"
#include <doctest.h>
#include <cstdlib>
#include <new>

namespace {
thread_local long allocationCountdown = -1;
}

// Fails exactly one selected allocation on this thread, then restores normal allocation for unwinding/assertions.
void* operator new(std::size_t size)
{
    if (allocationCountdown == 0)
    {
        allocationCountdown = -1;
        throw std::bad_alloc();
    }
    if (allocationCountdown > 0)
    {
        --allocationCountdown;
    }
    if (void* memory = std::malloc(size ? size : 1))
    {
        return memory;
    }
    throw std::bad_alloc();
}
// Matches the test allocator for ordinary object destruction.
void operator delete(void* memory) noexcept
{
    std::free(memory);
}
// Matches sized object destruction selected by the compiler.
void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}
// Uses the same controlled allocation path for arrays.
void* operator new[](std::size_t size)
{
    return ::operator new(size);
}
// Matches array destruction.
void operator delete[](void* memory) noexcept
{
    std::free(memory);
}
// Matches sized array destruction.
void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

using namespace ofg::terrain;
namespace {
// Completes a symbolic one-byte payload through both production phases.
void finish(TerrainStream& stream, BuildRequest request)
{
    auto payload = std::make_shared<ReadyContent>();
    payload->cpuBytes = payload->gpuBytes = 1;
    REQUIRE(stream.acceptGenerated(request.id, 1));
    REQUIRE(stream.complete(request.id, *payload));
}
} // namespace

TEST_CASE("S02 S18 S35 allocation failures preserve complete child groups and atomic snapshots")
{
    for (bool prepared : {false, true})
    {
        unsigned failures = 0;
        for (long position = 0; position < 2000; ++position)
        {
            TerrainStream stream;
            stream.requestRoot({});
            stream.setRefinement({}, false);
            stream.update();
            finish(stream, stream.takeRequests().at(0));
            stream.update();
            stream.setRefinement({}, true);
            for (const auto& child : childAddresses({}))
            {
                stream.setRefinement(child, false);
            }
            if (prepared)
            {
                stream.update();
                for (const auto& job : stream.takeRequests())
                {
                    finish(stream, job);
                }
            }
            bool threw = false;
            allocationCountdown = position;
            try
            {
                stream.update();
            } catch (const std::bad_alloc&)
            {
                threw = true;
            }
            allocationCountdown = -1;
            CAPTURE(prepared);
            CAPTURE(position);
            CHECK_NOTHROW(stream.validate());
            const auto nodes = stream.diagnostics().nodes;
            CHECK((nodes == 1 || nodes == 9));
            CHECK((stream.cut().size() == 1 || stream.cut().size() == 8));
            stream.update();
            for (const auto& job : stream.takeRequests())
            {
                finish(stream, job);
            }
            stream.update();
            CHECK(stream.cut().size() == 8);
            if (!threw)
            {
                break;
            }
            ++failures;
        }
        CHECK(failures > 8);
        CHECK(failures < 2000);
    }
}

TEST_CASE("S36 settled controller updates allocate nothing and publish nothing")
{
    TerrainStream stream;
    stream.requestRoot({});
    stream.setRefinement({}, false);
    stream.update();
    finish(stream, stream.takeRequests().at(0));
    stream.update();
    stream.update();
    const auto before = stream.diagnostics().publications;
    bool threw = false;
    allocationCountdown = 0;
    try
    {
        for (int frame = 0; frame < 100; ++frame)
        {
            stream.update();
        }
    } catch (const std::bad_alloc&)
    {
        threw = true;
    }
    allocationCountdown = -1;
    CHECK_FALSE(threw);
    CHECK(stream.diagnostics().publications == before);
    CHECK(stream.takeRequests().empty());
}

TEST_CASE("Unchanged budget-blocked terrain stops planning until an input changes")
{
    StreamSettings settings;
    settings.maximumPayloadBytes = 8;
    settings.cpuBudget = settings.gpuBudget = 64;
    TerrainStream stream(settings);
    stream.requestRoot({});
    stream.setRefinement({}, false);
    stream.update();
    finish(stream, stream.takeRequests().at(0));
    stream.update();
    stream.setRefinement({}, true);
    stream.update();
    stream.update();
    REQUIRE(stream.diagnostics().budgetBlocked);
    REQUIRE(stream.takeRequests().empty());
    bool threw = false;
    allocationCountdown = 0;
    try
    {
        for (int frame = 0; frame < 100; ++frame)
        {
            stream.update();
        }
    } catch (const std::bad_alloc&)
    {
        threw = true;
    }
    allocationCountdown = -1;
    CHECK_FALSE(threw);
    CHECK(stream.cut().size() == 1);
    // Changed demand wakes the controller and permits the otherwise blocked topology to be discarded.
    stream.withdrawRoot({});
    stream.update();
    CHECK(stream.cut().empty());
}
