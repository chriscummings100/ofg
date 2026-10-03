// Hierarchical runtime state machine implementation.
#include "state/state.h"

#include "core/engine-error.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace ofg {
namespace {

constexpr std::size_t maxStateTransitionsPerUpdate = 1024;

// Builds a consistent operation error for invalid state tree mutation.
[[nodiscard]] EngineError stateError(const char* operation, const char* message)
{
    return EngineError(std::string(operation) + " " + message);
}

} // namespace

const char* statePhaseName(StatePhase phase) noexcept
{
    switch (phase)
    {
    case StatePhase::Entering:
        return "entering";
    case StatePhase::Main:
        return "main";
    case StatePhase::Leaving:
        return "leaving";
    case StatePhase::Finished:
        return "finished";
    }
    return "unknown";
}

State::State() = default;

// Releases owned child and substate trees.
State::~State() = default;

void State::leave()
{
    if (m_phase == StatePhase::Finished)
    {
        return;
    }

    m_leaveRequested = true;
    if (m_phase == StatePhase::Main)
    {
        m_phase = StatePhase::Leaving;
        discardPendingDescendants();
        requestActiveDescendantsLeave();
    }
}

State* State::spawnChild(std::unique_ptr<State> state)
{
    requireCanSpawn("State::spawnChild");
    State* result = adoptChild(state);

    if (m_phase == StatePhase::Main && m_child == nullptr)
    {
        m_child = std::move(state);
        return result;
    }

    m_pendingChild = std::move(state);
    if (m_child != nullptr)
    {
        m_child->leave();
    }
    return result;
}

State* State::spawnSubstate(int substateIndex, std::unique_ptr<State> state)
{
    if (substateIndex < 0)
    {
        throw stateError("State::spawnSubstate", "requires a non-negative substate index.");
    }
    requireCanSpawn("State::spawnSubstate");
    State* result = adoptChild(state);

    SubstateSlot& slot = m_substates[substateIndex];
    if (m_phase == StatePhase::Main && slot.active == nullptr)
    {
        slot.active = std::move(state);
        return result;
    }

    slot.pending = std::move(state);
    if (slot.active != nullptr)
    {
        slot.active->leave();
    }
    return result;
}

State* State::spawnSibling(std::unique_ptr<State> state)
{
    if (m_parent == nullptr || m_parent->m_child.get() != this)
    {
        throw stateError("State::spawnSibling", "requires this state to be its parent's active child.");
    }
    return m_parent->spawnChild(std::move(state));
}

void State::update()
{
    if (m_updating)
    {
        throw stateError("State::update", "cannot be called recursively on the same state.");
    }

    struct UpdateGuard
    {
        State& m_state;
        // Marks this state as updating until the scope ends.
        explicit UpdateGuard(State& state)
            : m_state(state)
        {
            m_state.m_updating = true;
        }
        UpdateGuard(const UpdateGuard&) = delete;
        UpdateGuard& operator=(const UpdateGuard&) = delete;
        // Restores update eligibility even when a hook throws.
        ~UpdateGuard() { m_state.m_updating = false; }
    } guard(*this);

    for (std::size_t transitionCount = 0; transitionCount < maxStateTransitionsPerUpdate; ++transitionCount)
    {
        if (!updateOnce())
        {
            return;
        }
    }

    throw stateError("State::update", "exceeded the per-update transition budget.");
}

State* State::parent() noexcept
{
    return m_parent;
}

const State* State::parent() const noexcept
{
    return m_parent;
}

State* State::child() noexcept
{
    return m_child.get();
}

const State* State::child() const noexcept
{
    return m_child.get();
}

State* State::substate(int substateIndex) noexcept
{
    const auto found = m_substates.find(substateIndex);
    return found == m_substates.end() ? nullptr : found->second.active.get();
}

const State* State::substate(int substateIndex) const noexcept
{
    const auto found = m_substates.find(substateIndex);
    return found == m_substates.end() ? nullptr : found->second.active.get();
}

StatePhase State::phase() const noexcept
{
    return m_phase;
}

bool State::leaveRequested() const noexcept
{
    return m_leaveRequested;
}

bool State::finished() const noexcept
{
    return m_phase == StatePhase::Finished;
}

bool State::hasPendingChild() const noexcept
{
    return m_pendingChild != nullptr;
}

bool State::hasPendingSubstate(int substateIndex) const noexcept
{
    const auto found = m_substates.find(substateIndex);
    return found != m_substates.end() && found->second.pending != nullptr;
}

bool State::inhibitControlOnChild() const noexcept
{
    return m_inhibitControlOnChild;
}

void State::setInhibitControlOnChild(bool inhibit) noexcept
{
    m_inhibitControlOnChild = inhibit;
}

void State::onEnterStart() {}

bool State::onEnter()
{
    return true;
}

void State::onEnterEnd() {}

bool State::onMain()
{
    return false;
}

void State::onLeaveStart() {}

bool State::onLeave()
{
    return true;
}

void State::onLeaveEnd() {}

bool State::updateOnce()
{
    switch (m_phase)
    {
    case StatePhase::Entering:
        return updateEnteringOnce();
    case StatePhase::Main:
        return updateMainOnce();
    case StatePhase::Leaving:
        return updateLeavingOnce();
    case StatePhase::Finished:
        return false;
    }
    throw stateError("State::update", "encountered an unknown phase.");
}

bool State::updateEnteringOnce()
{
    if (!m_enterStarted)
    {
        onEnterStart();
        m_enterStarted = true;
        return true;
    }

    if (!onEnter())
    {
        return false;
    }

    onEnterEnd();
    if (m_leaveRequested)
    {
        discardPendingDescendants();
        m_phase = StatePhase::Leaving;
        requestActiveDescendantsLeave();
        return true;
    }

    m_phase = StatePhase::Main;
    (void)refreshDescendantSlots();
    return true;
}

bool State::updateMainOnce()
{
    if (refreshDescendantSlots())
    {
        return true;
    }

    const bool hadChildAtStart = m_child != nullptr;
    const bool descendantsChanged = updateActiveDescendantsOnce(false);
    if (m_phase != StatePhase::Main)
    {
        return true;
    }
    if (refreshDescendantSlots())
    {
        return !m_inhibitControlOnChild || !hadChildAtStart;
    }
    if (descendantsChanged && m_inhibitControlOnChild && hadChildAtStart)
    {
        return false;
    }

    if (m_leaveRequested)
    {
        m_phase = StatePhase::Leaving;
        discardPendingDescendants();
        requestActiveDescendantsLeave();
        return true;
    }

    if (m_inhibitControlOnChild && (hadChildAtStart || m_child != nullptr))
    {
        return false;
    }

    const bool mainDone = onMain();
    if (m_leaveRequested || mainDone)
    {
        leave();
        return true;
    }

    return false;
}

bool State::updateLeavingOnce()
{
    discardPendingDescendants();
    requestActiveDescendantsLeave();
    (void)updateActiveDescendantsOnce(true);
    (void)refreshDescendantSlots();
    if (hasActiveDescendants())
    {
        return false;
    }

    if (!m_leaveStarted)
    {
        onLeaveStart();
        m_leaveStarted = true;
        return true;
    }

    if (!onLeave())
    {
        return false;
    }

    onLeaveEnd();
    m_phase = StatePhase::Finished;
    return true;
}

State* State::adoptChild(std::unique_ptr<State>& state)
{
    if (state == nullptr)
    {
        throw stateError("State::spawn", "requires a non-null state.");
    }
    if (state.get() == this)
    {
        throw stateError("State::spawn", "cannot spawn itself.");
    }
    if (state->m_parent != nullptr)
    {
        throw stateError("State::spawn", "requires a detached state.");
    }

    State* result = state.get();
    result->m_parent = this;
    return result;
}

bool State::refreshDescendantSlots()
{
    bool changed = false;

    if (m_child != nullptr && m_child->finished())
    {
        m_child.reset();
        changed = true;
    }
    if (m_phase == StatePhase::Main && !m_leaveRequested && m_child == nullptr && m_pendingChild != nullptr)
    {
        m_child = std::move(m_pendingChild);
        changed = true;
    }

    for (auto iterator = m_substates.begin(); iterator != m_substates.end();)
    {
        SubstateSlot& slot = iterator->second;
        if (slot.active != nullptr && slot.active->finished())
        {
            slot.active.reset();
            changed = true;
        }
        if (m_phase == StatePhase::Main && !m_leaveRequested && slot.active == nullptr && slot.pending != nullptr)
        {
            slot.active = std::move(slot.pending);
            changed = true;
        }

        if (slot.active == nullptr && slot.pending == nullptr)
        {
            iterator = m_substates.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }

    return changed;
}

bool State::updateActiveDescendantsOnce(bool requireLeave)
{
    bool changed = false;

    if (m_child != nullptr)
    {
        if (requireLeave)
        {
            m_child->leave();
        }
        m_child->update();
        changed = changed || m_child->finished();
    }

    for (auto& entry : m_substates)
    {
        State* substate = entry.second.active.get();
        if (substate == nullptr)
        {
            continue;
        }
        if (requireLeave)
        {
            substate->leave();
        }
        substate->update();
        changed = changed || substate->finished();
    }

    return changed;
}

void State::requestActiveDescendantsLeave()
{
    if (m_child != nullptr)
    {
        m_child->leave();
    }
    for (auto& entry : m_substates)
    {
        if (entry.second.active != nullptr)
        {
            entry.second.active->leave();
        }
    }
}

void State::discardPendingDescendants()
{
    m_pendingChild.reset();
    for (auto iterator = m_substates.begin(); iterator != m_substates.end();)
    {
        iterator->second.pending.reset();
        if (iterator->second.active == nullptr)
        {
            iterator = m_substates.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
}

bool State::hasActiveDescendants() const noexcept
{
    if (m_child != nullptr)
    {
        return true;
    }
    for (const auto& entry : m_substates)
    {
        if (entry.second.active != nullptr)
        {
            return true;
        }
    }
    return false;
}

void State::requireCanSpawn(const char* operation) const
{
    if (m_phase == StatePhase::Leaving || m_phase == StatePhase::Finished || m_leaveRequested)
    {
        throw stateError(operation, "cannot spawn after leave has been requested.");
    }
}

} // namespace ofg
