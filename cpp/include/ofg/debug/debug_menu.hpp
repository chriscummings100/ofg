// Runtime debug scalar registry used by C++ tools and debug UI frontends.
//
// DebugBool, DebugInt, and DebugFloat are lightweight global-friendly wrappers
// that register themselves with DebugMenu. DebugMenu parses slash-separated
// paths once during registration, stores durable path segments, and exposes a
// cached menu tree that is rebuilt only when the registry generation changes.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ofg {

enum class DebugScalarType {
    Bool,
    Int,
    Float,
};

// Returns a stable lowercase display name for a debug scalar type.
const char* debug_scalar_type_name(DebugScalarType type) noexcept;

enum class DebugMenuDiagnosticKind {
    InvalidPath,
    DuplicatePath,
};

// Returns a stable lowercase display name for a debug menu diagnostic kind.
const char* debug_menu_diagnostic_kind_name(DebugMenuDiagnosticKind kind) noexcept;

class DebugBool;
class DebugInt;
class DebugFloat;

struct DebugBoolEntry {
    std::uint64_t m_id{0};
    std::string m_path;
    std::vector<std::string> m_path_segments;
    DebugBool* m_variable{nullptr};
    bool m_default_value{false};
};

struct DebugIntEntry {
    std::uint64_t m_id{0};
    std::string m_path;
    std::vector<std::string> m_path_segments;
    DebugInt* m_variable{nullptr};
    int m_default_value{0};
};

struct DebugFloatEntry {
    std::uint64_t m_id{0};
    std::string m_path;
    std::vector<std::string> m_path_segments;
    DebugFloat* m_variable{nullptr};
    float m_default_value{0.0f};
};

struct DebugMenuTreeEntry {
    DebugScalarType m_type{DebugScalarType::Bool};
    std::string m_label;
    std::string m_path;
    std::uint64_t m_registration_id{0};
    std::size_t m_entry_index{0};
};

struct DebugMenuTreeNode {
    std::string m_label;
    std::string m_path;
    std::vector<DebugMenuTreeNode> m_children;
    std::vector<DebugMenuTreeEntry> m_entries;
};

struct DebugMenuTree {
    std::uint64_t m_generation{0};
    std::vector<DebugMenuTreeNode> m_nodes;
    std::vector<DebugMenuTreeEntry> m_entries;
};

struct DebugMenuDiagnostic {
    DebugMenuDiagnosticKind m_kind{DebugMenuDiagnosticKind::InvalidPath};
    DebugScalarType m_attempted_type{DebugScalarType::Bool};
    std::string m_path;
    std::string m_message;
};

using DebugMenuDiagnostics = std::span<const DebugMenuDiagnostic>;

class DebugMenu {
public:
    // Returns the process-wide debug registry used by global debug scalars.
    static DebugMenu& instance() noexcept;

    DebugMenu(const DebugMenu&) = delete;
    DebugMenu& operator=(const DebugMenu&) = delete;

    std::uint64_t registry_generation() const noexcept;
    bool refresh_tree_if_dirty();

    std::optional<bool> get_bool(std::string_view path) const;
    bool set_bool(std::string_view path, bool value);
    std::span<const DebugBoolEntry> bool_entries() const noexcept;

    std::optional<int> get_int(std::string_view path) const;
    bool set_int(std::string_view path, int value);
    std::span<const DebugIntEntry> int_entries() const noexcept;

    std::optional<float> get_float(std::string_view path) const;
    bool set_float(std::string_view path, float value);
    std::span<const DebugFloatEntry> float_entries() const noexcept;

    const DebugMenuTree& tree() const noexcept;
    DebugMenuDiagnostics diagnostics() const noexcept;

private:
    struct RegisteredPath {
        DebugScalarType m_type{DebugScalarType::Bool};
        std::uint64_t m_id{0};
    };

    struct RegistrationSeed {
        std::uint64_t m_id{0};
        std::string m_path;
        std::vector<std::string> m_path_segments;
    };

    struct RegistrationResult {
        bool m_registered{false};
        std::uint64_t m_id{0};
    };

    DebugMenu() = default;

    friend class DebugBool;
    friend class DebugInt;
    friend class DebugFloat;

    RegistrationResult register_bool(DebugBool& variable, std::string_view path, bool default_value);
    RegistrationResult register_int(DebugInt& variable, std::string_view path, int default_value);
    RegistrationResult register_float(DebugFloat& variable, std::string_view path, float default_value);

    void unregister_bool(std::uint64_t id) noexcept;
    void unregister_int(std::uint64_t id) noexcept;
    void unregister_float(std::uint64_t id) noexcept;

    std::optional<RegistrationSeed> prepare_registration(DebugScalarType type, std::string_view path);
    void accept_registration(DebugScalarType type, std::uint64_t id, const std::string& path);
    void add_diagnostic(
        DebugMenuDiagnosticKind kind, DebugScalarType attempted_type, std::string_view path, std::string message);

    const DebugBoolEntry* find_bool_entry(std::string_view path) const noexcept;
    DebugBoolEntry* find_bool_entry(std::string_view path) noexcept;
    const DebugIntEntry* find_int_entry(std::string_view path) const noexcept;
    DebugIntEntry* find_int_entry(std::string_view path) noexcept;
    const DebugFloatEntry* find_float_entry(std::string_view path) const noexcept;
    DebugFloatEntry* find_float_entry(std::string_view path) noexcept;

    std::uint64_t m_next_registration_id{1};
    std::uint64_t m_registry_generation{0};
    std::unordered_map<std::string, RegisteredPath> m_registered_paths;
    std::vector<DebugBoolEntry> m_bool_entries;
    std::vector<DebugIntEntry> m_int_entries;
    std::vector<DebugFloatEntry> m_float_entries;
    std::vector<DebugMenuDiagnostic> m_diagnostics;
    DebugMenuTree m_tree;
};

class DebugBool {
public:
    DebugBool(const char* path, bool default_value) noexcept;
    ~DebugBool() noexcept;

    DebugBool(const DebugBool&) = delete;
    DebugBool& operator=(const DebugBool&) = delete;

    operator bool() const noexcept;
    DebugBool& operator=(bool value) noexcept;

    bool value() const noexcept;
    bool default_value() const noexcept;
    std::string_view path() const noexcept;
    bool registered() const noexcept;
    void set(bool value) noexcept;

private:
    friend class DebugMenu;

    std::string_view m_path;
    bool m_value{false};
    bool m_default_value{false};
    bool m_registered{false};
    std::uint64_t m_registration_id{0};
};

class DebugInt {
public:
    DebugInt(const char* path, int default_value) noexcept;
    ~DebugInt() noexcept;

    DebugInt(const DebugInt&) = delete;
    DebugInt& operator=(const DebugInt&) = delete;

    operator int() const noexcept;
    DebugInt& operator=(int value) noexcept;

    int value() const noexcept;
    int default_value() const noexcept;
    std::string_view path() const noexcept;
    bool registered() const noexcept;
    void set(int value) noexcept;

private:
    friend class DebugMenu;

    std::string_view m_path;
    int m_value{0};
    int m_default_value{0};
    bool m_registered{false};
    std::uint64_t m_registration_id{0};
};

class DebugFloat {
public:
    DebugFloat(const char* path, float default_value) noexcept;
    ~DebugFloat() noexcept;

    DebugFloat(const DebugFloat&) = delete;
    DebugFloat& operator=(const DebugFloat&) = delete;

    operator float() const noexcept;
    DebugFloat& operator=(float value) noexcept;

    float value() const noexcept;
    float default_value() const noexcept;
    std::string_view path() const noexcept;
    bool registered() const noexcept;
    void set(float value) noexcept;

private:
    friend class DebugMenu;

    std::string_view m_path;
    float m_value{0.0f};
    float m_default_value{0.0f};
    bool m_registered{false};
    std::uint64_t m_registration_id{0};
};

} // namespace ofg

#define DEBUG_BOOL(path, variable, default_value) ::ofg::DebugBool variable((path), (default_value))
#define DEBUG_INT(path, variable, default_value) ::ofg::DebugInt variable((path), (default_value))
#define DEBUG_FLOAT(path, variable, default_value) ::ofg::DebugFloat variable((path), (default_value))
