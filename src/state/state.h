// Hierarchical runtime state machine.
//
// State is a portable C++ gameplay-flow primitive. It owns one primary child
// state plus indexed substates, drives enter/main/leave phases, and drains
// descendants before a parent performs its own leave callbacks.
#pragma once

#include <map>
#include <memory>
#include <utility>

namespace ofg {

enum class StatePhase
{
    Entering,
    Main,
    Leaving,
    Finished,
};

// Converts a public state phase into a stable diagnostic name.
[[nodiscard]] const char* statePhaseName(StatePhase phase) noexcept;

class State
{
public:
    // Creates a detached state in its enter phase.
    State();
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    State(State&&) = delete;
    State& operator=(State&&) = delete;
    // Releases owned descendants; destruction does not run leave hooks.
    virtual ~State();

    // Requests this state and all active descendants to leave.
    void leave();
    // Spawns or replaces the primary child state.
    [[nodiscard]] State* spawnChild(std::unique_ptr<State> state);
    // Spawns or replaces one indexed substate.
    [[nodiscard]] State* spawnSubstate(int substateIndex, std::unique_ptr<State> state);
    // Requests this state's parent to replace the active primary child.
    [[nodiscard]] State* spawnSibling(std::unique_ptr<State> state);
    // Advances this state tree until it reaches a frame boundary or finishes.
    void update();

    // Returns this state's owning parent, or nullptr for a root state.
    [[nodiscard]] State* parent() noexcept;
    // Returns this state's owning parent, or nullptr for a root state.
    [[nodiscard]] const State* parent() const noexcept;
    // Returns the active primary child, or nullptr.
    [[nodiscard]] State* child() noexcept;
    // Returns the active primary child, or nullptr.
    [[nodiscard]] const State* child() const noexcept;
    // Returns the active substate for an index, or nullptr.
    [[nodiscard]] State* substate(int substateIndex) noexcept;
    // Returns the active substate for an index, or nullptr.
    [[nodiscard]] const State* substate(int substateIndex) const noexcept;
    // Reports this state's broad lifecycle phase.
    [[nodiscard]] StatePhase phase() const noexcept;
    // Reports whether leave() has been requested.
    [[nodiscard]] bool leaveRequested() const noexcept;
    // Reports whether this state has completed enter, main, and leave.
    [[nodiscard]] bool finished() const noexcept;
    // Reports whether a primary child replacement is waiting.
    [[nodiscard]] bool hasPendingChild() const noexcept;
    // Reports whether a substate replacement is waiting for an index.
    [[nodiscard]] bool hasPendingSubstate(int substateIndex) const noexcept;
    // Reports whether this state skips onMain while an active child exists.
    [[nodiscard]] bool inhibitControlOnChild() const noexcept;
    // Configures whether this state skips onMain while an active child exists.
    void setInhibitControlOnChild(bool inhibit) noexcept;

    // Convenience wrapper that constructs and spawns a primary child.
    template<typename T, typename... Args>
    [[nodiscard]] T& emplaceChild(Args&&... args)
    {
        auto state = std::make_unique<T>(std::forward<Args>(args)...);
        T* result = state.get();
        (void)spawnChild(std::move(state));
        return *result;
    }

    // Convenience wrapper that constructs and spawns an indexed substate.
    template<typename T, typename... Args>
    [[nodiscard]] T& emplaceSubstate(int substateIndex, Args&&... args)
    {
        auto state = std::make_unique<T>(std::forward<Args>(args)...);
        T* result = state.get();
        (void)spawnSubstate(substateIndex, std::move(state));
        return *result;
    }

protected:
    // Single-shot hook called before repeated enter work starts.
    virtual void onEnterStart();
    // Repeated hook called until it returns true.
    [[nodiscard]] virtual bool onEnter();
    // Single-shot hook called after enter work completes.
    virtual void onEnterEnd();
    // Repeated hook called during main work until it returns true.
    [[nodiscard]] virtual bool onMain();
    // Single-shot hook called before repeated leave work starts.
    virtual void onLeaveStart();
    // Repeated hook called until it returns true.
    [[nodiscard]] virtual bool onLeave();
    // Single-shot hook called after leave work completes.
    virtual void onLeaveEnd();

private:
    struct SubstateSlot
    {
        std::unique_ptr<State> active;
        std::unique_ptr<State> pending;
    };

    // Advances one internal transition and reports whether another may follow.
    [[nodiscard]] bool updateOnce();
    // Advances enter hooks.
    [[nodiscard]] bool updateEnteringOnce();
    // Advances descendants and the main hook.
    [[nodiscard]] bool updateMainOnce();
    // Drains descendants and advances leave hooks.
    [[nodiscard]] bool updateLeavingOnce();
    // Attaches an incoming state to this parent and returns its raw observer.
    [[nodiscard]] State* adoptChild(std::unique_ptr<State>& state);
    // Activates pending descendants when allowed and drops finished descendants.
    [[nodiscard]] bool refreshDescendantSlots();
    // Updates active descendants once in deterministic order.
    [[nodiscard]] bool updateActiveDescendantsOnce(bool requireLeave);
    // Requests active descendants to leave.
    void requestActiveDescendantsLeave();
    // Discards all pending descendants.
    void discardPendingDescendants();
    // Returns whether any active child or substate is still live.
    [[nodiscard]] bool hasActiveDescendants() const noexcept;
    // Throws if this state cannot accept a new descendant.
    void requireCanSpawn(const char* operation) const;

    State* m_parent{nullptr};
    std::unique_ptr<State> m_child;
    std::unique_ptr<State> m_pendingChild;
    std::map<int, SubstateSlot> m_substates;
    StatePhase m_phase{StatePhase::Entering};
    bool m_enterStarted{false};
    bool m_leaveStarted{false};
    bool m_leaveRequested{false};
    bool m_inhibitControlOnChild{false};
    bool m_updating{false};
};

} // namespace ofg
