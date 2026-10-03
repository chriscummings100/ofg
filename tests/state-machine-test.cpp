// Doctest coverage for the portable hierarchical State runtime.
//
// These tests pin the first state-system milestone: lifecycle hooks execute in
// order, child and substate replacements drain through leave, parents wait for
// active descendants, and invalid tree mutations fail clearly.
#include "doctest.h"

#include "core/engine-error.h"
#include "state/state.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using EventLog = std::vector<std::string>;

// Records a stable event name in the shared test log.
void appendEvent(EventLog& log, const std::string& stateName, const char* eventName)
{
    log.push_back(stateName + "." + eventName);
}

// Reports whether the shared log contains one exact event.
bool hasEvent(const EventLog& log, const std::string& eventName)
{
    return std::find(log.begin(), log.end(), eventName) != log.end();
}

// Returns the index of one expected event for ordering assertions.
std::size_t eventIndex(const EventLog& log, const std::string& eventName)
{
    const auto found = std::find(log.begin(), log.end(), eventName);
    REQUIRE(found != log.end());
    return static_cast<std::size_t>(found - log.begin());
}

class ProbeState : public ofg::State
{
public:
    // Creates a named state that logs every lifecycle hook.
    ProbeState(std::string name, EventLog& log)
        : m_name(std::move(name))
        , m_log(log)
    {
    }

    // Configures how many enter calls should report unfinished work.
    void setEnterFalseCount(int count) noexcept { m_enterFalseCount = count; }

    // Configures how many leave calls should report unfinished work.
    void setLeaveFalseCount(int count) noexcept { m_leaveFalseCount = count; }

    // Configures whether onMain completes this state.
    void setMainFinishes(bool value) noexcept { m_mainFinishes = value; }

    // Configures enter to complete as soon as leave has been requested.
    void setFinishEnterWhenLeaveRequested(bool value) noexcept { m_finishEnterWhenLeaveRequested = value; }

    // Returns how often onMain has run.
    [[nodiscard]] int mainCalls() const noexcept { return m_mainCalls; }

protected:
    // Logs the single-shot enter-start hook.
    void onEnterStart() override { record("enterStart"); }

    // Logs repeated enter work and optionally spans multiple updates.
    bool onEnter() override
    {
        record("enter");
        if (m_finishEnterWhenLeaveRequested && leaveRequested())
        {
            return true;
        }
        if (m_enterFalseCount > 0)
        {
            --m_enterFalseCount;
            return false;
        }
        return true;
    }

    // Logs the single-shot enter-end hook.
    void onEnterEnd() override { record("enterEnd"); }

    // Logs main work and optionally completes the state.
    bool onMain() override
    {
        ++m_mainCalls;
        record("main");
        return m_mainFinishes;
    }

    // Logs the single-shot leave-start hook.
    void onLeaveStart() override { record("leaveStart"); }

    // Logs repeated leave work and optionally spans multiple updates.
    bool onLeave() override
    {
        record("leave");
        if (m_leaveFalseCount > 0)
        {
            --m_leaveFalseCount;
            return false;
        }
        return true;
    }

    // Logs the single-shot leave-end hook.
    void onLeaveEnd() override { record("leaveEnd"); }

    // Appends one hook event for this state.
    void record(const char* eventName) { appendEvent(m_log, m_name, eventName); }

private:
    std::string m_name;
    EventLog& m_log;
    int m_enterFalseCount{0};
    int m_leaveFalseCount{0};
    int m_mainCalls{0};
    bool m_mainFinishes{false};
    bool m_finishEnterWhenLeaveRequested{false};
};

class SiblingSpawnerState : public ofg::State
{
public:
    // Creates a state that replaces itself with a named sibling from onMain.
    SiblingSpawnerState(EventLog& log, ofg::State** spawnedState)
        : m_log(log)
        , m_spawnedState(spawnedState)
    {
    }

protected:
    // Spawns the sibling and completes this state immediately.
    bool onMain() override
    {
        appendEvent(m_log, "spawner", "main");
        *m_spawnedState = spawnSibling(std::make_unique<ProbeState>("sibling", m_log));
        return true;
    }

private:
    EventLog& m_log;
    ofg::State** m_spawnedState{nullptr};
};

class RecursiveUpdateState : public ofg::State
{
protected:
    // Attempts to recursively update the same state.
    bool onMain() override
    {
        update();
        return false;
    }
};

class ParentLeaveRequesterState : public ofg::State
{
protected:
    // Requests the parent to leave while this child is being updated.
    bool onMain() override
    {
        parent()->leave();
        return false;
    }
};

class SiblingLoopState : public ofg::State
{
protected:
    // Replaces itself every main pass so the parent transition budget is used.
    bool onMain() override
    {
        (void)spawnSibling(std::make_unique<SiblingLoopState>());
        return true;
    }
};

} // namespace

// Verifies public state phase names are stable.
TEST_CASE("State phases have diagnostic names")
{
    CHECK(std::string(ofg::statePhaseName(ofg::StatePhase::Entering)) == "entering");
    CHECK(std::string(ofg::statePhaseName(ofg::StatePhase::Main)) == "main");
    CHECK(std::string(ofg::statePhaseName(ofg::StatePhase::Leaving)) == "leaving");
    CHECK(std::string(ofg::statePhaseName(ofg::StatePhase::Finished)) == "finished");
    CHECK(std::string(ofg::statePhaseName(static_cast<ofg::StatePhase>(100))) == "unknown");
}

// Verifies a fully synchronous state can finish all phases in one update.
TEST_CASE("State immediate lifecycle finishes in one update")
{
    EventLog log;
    ProbeState state{"state", log};
    state.setMainFinishes(true);

    state.update();

    CHECK(state.finished());
    state.leave();
    CHECK(state.finished());
    CHECK(
        log == EventLog{
                   "state.enterStart",
                   "state.enter",
                   "state.enterEnd",
                   "state.main",
                   "state.leaveStart",
                   "state.leave",
                   "state.leaveEnd",
               }
    );
}

// Verifies enter can span frames and descendants do not run before enter ends.
TEST_CASE("State enter waits before activating pending descendants")
{
    EventLog log;
    ProbeState parent{"parent", log};
    parent.setEnterFalseCount(2);

    auto child = std::make_unique<ProbeState>("child", log);
    ProbeState* childRaw = child.get();
    (void)parent.spawnChild(std::move(child));
    auto substate = std::make_unique<ProbeState>("substate", log);
    ProbeState* substateRaw = substate.get();
    (void)parent.spawnSubstate(2, std::move(substate));

    parent.update();
    CHECK(parent.phase() == ofg::StatePhase::Entering);
    CHECK(parent.child() == nullptr);
    CHECK(parent.substate(2) == nullptr);
    CHECK(parent.hasPendingChild());
    CHECK(parent.hasPendingSubstate(2));
    CHECK(childRaw->parent() == &parent);
    CHECK_FALSE(hasEvent(log, "child.enterStart"));

    parent.update();
    CHECK(parent.phase() == ofg::StatePhase::Entering);
    CHECK(parent.child() == nullptr);
    CHECK_FALSE(hasEvent(log, "substate.enterStart"));

    parent.update();
    CHECK(parent.phase() == ofg::StatePhase::Main);
    CHECK(parent.child() == childRaw);
    CHECK(parent.substate(2) == substateRaw);
    CHECK_FALSE(parent.hasPendingChild());
    CHECK_FALSE(parent.hasPendingSubstate(2));
    CHECK(childRaw->mainCalls() == 1);
    CHECK(substateRaw->mainCalls() == 1);
}

// Verifies leave requested during enter is visible and skips main.
TEST_CASE("State leave during enter completes enter then skips main")
{
    EventLog log;
    ProbeState parent{"parent", log};
    parent.setFinishEnterWhenLeaveRequested(true);
    (void)parent.spawnChild(std::make_unique<ProbeState>("pendingChild", log));
    (void)parent.spawnSubstate(2, std::make_unique<ProbeState>("pendingSubstate", log));

    parent.leave();
    CHECK(parent.leaveRequested());
    parent.update();

    CHECK(parent.finished());
    CHECK(parent.child() == nullptr);
    CHECK(parent.substate(2) == nullptr);
    CHECK_FALSE(parent.hasPendingChild());
    CHECK_FALSE(parent.hasPendingSubstate(2));
    CHECK_FALSE(hasEvent(log, "parent.main"));
    CHECK_FALSE(hasEvent(log, "pendingChild.enterStart"));
    CHECK_FALSE(hasEvent(log, "pendingSubstate.enterStart"));
    CHECK(eventIndex(log, "parent.enterEnd") < eventIndex(log, "parent.leaveStart"));
}

// Verifies direct main-phase spawns and const accessors.
TEST_CASE("State main-phase spawns activate immediately")
{
    EventLog log;
    ofg::State parent;
    parent.update();
    REQUIRE(parent.phase() == ofg::StatePhase::Main);
    CHECK_FALSE(parent.inhibitControlOnChild());

    ofg::State* child = parent.spawnChild(std::make_unique<ProbeState>("child", log));
    ofg::State* substate = parent.spawnSubstate(4, std::make_unique<ProbeState>("substate", log));
    CHECK(parent.child() == child);
    CHECK(parent.substate(4) == substate);

    const ofg::State& constParent = parent;
    CHECK(constParent.parent() == nullptr);
    CHECK(constParent.child() == child);
    CHECK(constParent.substate(4) == substate);
    CHECK_FALSE(constParent.inhibitControlOnChild());
}

// Verifies a parent does not start local leave callbacks until descendants end.
TEST_CASE("State leave waits for active child and substates")
{
    EventLog log;
    ProbeState parent{"parent", log};
    auto child = std::make_unique<ProbeState>("child", log);
    child->setLeaveFalseCount(1);
    auto substate = std::make_unique<ProbeState>("substate", log);
    substate->setLeaveFalseCount(1);

    (void)parent.spawnChild(std::move(child));
    (void)parent.spawnSubstate(1, std::move(substate));
    parent.update();
    log.clear();

    parent.leave();
    parent.update();
    CHECK_FALSE(parent.finished());
    CHECK_FALSE(hasEvent(log, "parent.leaveStart"));

    for (int attempt = 0; attempt < 4 && !parent.finished(); ++attempt)
    {
        parent.update();
    }

    CHECK(parent.finished());
    CHECK(eventIndex(log, "child.leaveEnd") < eventIndex(log, "parent.leaveStart"));
    CHECK(eventIndex(log, "substate.leaveEnd") < eventIndex(log, "parent.leaveStart"));
}

// Verifies a parent waits when the only active descendant is a substate.
TEST_CASE("State leave waits for active substate without child")
{
    EventLog log;
    ProbeState parent{"parent", log};
    auto substate = std::make_unique<ProbeState>("substate", log);
    substate->setLeaveFalseCount(1);

    (void)parent.spawnSubstate(1, std::move(substate));
    parent.update();
    log.clear();

    parent.leave();
    parent.update();
    CHECK_FALSE(parent.finished());
    CHECK_FALSE(hasEvent(log, "parent.leaveStart"));

    for (int attempt = 0; attempt < 4 && !parent.finished(); ++attempt)
    {
        parent.update();
    }

    CHECK(parent.finished());
    CHECK(eventIndex(log, "substate.leaveEnd") < eventIndex(log, "parent.leaveStart"));
}

// Verifies parent main observes externally finished children and child-requested leave.
TEST_CASE("State main refreshes externally finished child and child-requested leave")
{
    EventLog log;
    ofg::State parent;
    auto child = std::make_unique<ProbeState>("child", log);
    ProbeState* childRaw = child.get();
    (void)parent.spawnChild(std::move(child));
    parent.update();
    REQUIRE(parent.child() == childRaw);

    childRaw->leave();
    childRaw->update();
    REQUIRE(childRaw->finished());
    parent.update();
    CHECK(parent.child() == nullptr);

    (void)parent.spawnChild(std::make_unique<ParentLeaveRequesterState>());
    parent.update();
    CHECK(parent.leaveRequested());
    CHECK(parent.finished());
}

// Verifies child replacement drains the old child and keeps the newest pending child.
TEST_CASE("State child replacement activates the newest pending child")
{
    EventLog log;
    ofg::State parent;
    auto oldChild = std::make_unique<ProbeState>("old", log);
    oldChild->setLeaveFalseCount(1);
    ProbeState* oldRaw = oldChild.get();
    (void)parent.spawnChild(std::move(oldChild));
    parent.update();
    REQUIRE(parent.child() == oldRaw);

    (void)parent.spawnChild(std::make_unique<ProbeState>("firstPending", log));
    ofg::State* secondPending = parent.spawnChild(std::make_unique<ProbeState>("secondPending", log));
    CHECK(parent.child() == oldRaw);
    CHECK(parent.hasPendingChild());
    CHECK(oldRaw->leaveRequested());

    for (int attempt = 0; attempt < 4 && parent.child() != secondPending; ++attempt)
    {
        parent.update();
    }

    CHECK(parent.child() == secondPending);
    CHECK_FALSE(parent.hasPendingChild());
    CHECK_FALSE(hasEvent(log, "firstPending.enterStart"));
    CHECK(hasEvent(log, "secondPending.enterStart"));
}

// Verifies substate replacement is per index and substate update order is stable.
TEST_CASE("State substates replace by index and update deterministically")
{
    EventLog log;
    ofg::State parent;
    auto first = std::make_unique<ProbeState>("one", log);
    ProbeState* firstRaw = first.get();
    auto second = std::make_unique<ProbeState>("two", log);
    second->setLeaveFalseCount(1);
    ProbeState* secondRaw = second.get();

    (void)parent.spawnSubstate(1, std::move(first));
    (void)parent.spawnSubstate(2, std::move(second));
    parent.update();
    REQUIRE(parent.substate(1) == firstRaw);
    REQUIRE(parent.substate(2) == secondRaw);

    log.clear();
    parent.update();
    REQUIRE(log.size() >= 2);
    CHECK(log[0] == "one.main");
    CHECK(log[1] == "two.main");

    log.clear();
    ofg::State* replacement = parent.spawnSubstate(2, std::make_unique<ProbeState>("newTwo", log));
    CHECK(parent.substate(1) == firstRaw);
    CHECK(parent.substate(2) == secondRaw);
    CHECK(parent.hasPendingSubstate(2));
    CHECK(secondRaw->leaveRequested());

    for (int attempt = 0; attempt < 4 && parent.substate(2) != replacement; ++attempt)
    {
        parent.update();
    }

    CHECK(parent.substate(1) == firstRaw);
    CHECK(parent.substate(2) == replacement);
    CHECK_FALSE(parent.hasPendingSubstate(2));
    CHECK(hasEvent(log, "newTwo.enterStart"));
}

// Verifies child inhibition pauses parent main while still ticking descendants.
TEST_CASE("State inhibit control on child pauses parent main")
{
    EventLog log;
    ProbeState parent{"parent", log};
    parent.setInhibitControlOnChild(true);
    (void)parent.spawnChild(std::make_unique<ProbeState>("child", log));
    (void)parent.spawnSubstate(1, std::make_unique<ProbeState>("substate", log));
    parent.update();
    log.clear();

    parent.update();
    CHECK(hasEvent(log, "child.main"));
    CHECK(hasEvent(log, "substate.main"));
    CHECK_FALSE(hasEvent(log, "parent.main"));

    log.clear();
    REQUIRE(parent.child() != nullptr);
    parent.child()->leave();
    parent.update();
    CHECK(parent.child() == nullptr);
    CHECK_FALSE(hasEvent(log, "parent.main"));

    log.clear();
    parent.update();
    CHECK(hasEvent(log, "substate.main"));
    CHECK(hasEvent(log, "parent.main"));
}

// Verifies spawnSibling delegates through the active primary child path.
TEST_CASE("State spawn sibling replaces the active primary child")
{
    EventLog log;
    ofg::State parent;
    ofg::State* spawnedSibling = nullptr;
    (void)parent.spawnChild(std::make_unique<SiblingSpawnerState>(log, &spawnedSibling));

    parent.update();

    REQUIRE(spawnedSibling != nullptr);
    CHECK(parent.child() == spawnedSibling);
    CHECK(hasEvent(log, "spawner.main"));
    CHECK(hasEvent(log, "sibling.enterStart"));
}

// Verifies invalid sibling spawns fail clearly.
TEST_CASE("State spawn sibling rejects non-child states")
{
    EventLog log;
    ofg::State root;
    CHECK_THROWS_WITH_AS(
        [&]()
        {
            (void)root.spawnSibling(std::make_unique<ProbeState>("invalid", log));
        }(),
        doctest::Contains("active child"),
        ofg::EngineError
    );

    ofg::State parent;
    ofg::State* substate = parent.spawnSubstate(0, std::make_unique<ProbeState>("substate", log));
    parent.update();
    REQUIRE(parent.substate(0) == substate);

    CHECK_THROWS_WITH_AS(
        [&]()
        {
            (void)substate->spawnSibling(std::make_unique<ProbeState>("invalid", log));
        }(),
        doctest::Contains("active child"),
        ofg::EngineError
    );
}

// Verifies invalid spawn requests report useful errors.
TEST_CASE("State spawn validates ownership and lifecycle")
{
    EventLog log;
    ofg::State parent;

    CHECK_THROWS_WITH_AS(
        [&]()
        {
            (void)parent.spawnChild(nullptr);
        }(),
        doctest::Contains("non-null"),
        ofg::EngineError
    );
    CHECK_THROWS_WITH_AS(
        [&]()
        {
            (void)parent.spawnSubstate(-1, std::make_unique<ProbeState>("negative", log));
        }(),
        doctest::Contains("non-negative"),
        ofg::EngineError
    );

    parent.leave();
    CHECK_THROWS_WITH_AS(
        [&]()
        {
            (void)parent.spawnChild(std::make_unique<ProbeState>("late", log));
        }(),
        doctest::Contains("leave has been requested"),
        ofg::EngineError
    );
}

// Verifies recursive updates are rejected instead of corrupting traversal state.
TEST_CASE("State update rejects recursive self update")
{
    RecursiveUpdateState state;

    CHECK_THROWS_WITH_AS(state.update(), doctest::Contains("recursively"), ofg::EngineError);
}

// Verifies immediate replacement loops stop at the per-update transition budget.
TEST_CASE("State update rejects unbounded same-frame transitions")
{
    ofg::State root;
    (void)root.spawnChild(std::make_unique<SiblingLoopState>());

    CHECK_THROWS_WITH_AS(root.update(), doctest::Contains("transition budget"), ofg::EngineError);
}
