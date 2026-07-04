// Implementation of the C++ debug scalar registry.
//
// Registration is intentionally independent from Dear ImGui and WebGPU so tests,
// runtime systems, and future browser facades can use the same typed registry.
#include "ofg/debug/debug_menu.hpp"

#include <algorithm>
#include <utility>

namespace ofg {
namespace {

struct ParsedPath {
    std::string m_path;
    std::vector<std::string> m_segments;
};

// Accepts visible ASCII path bytes plus slash separators. This keeps debug
// paths stable as both C++ lookup keys and future browser facade keys.
bool is_valid_path_byte(unsigned char byte) noexcept {
    return byte >= 0x21U && byte <= 0x7EU;
}

std::optional<ParsedPath> parse_debug_path(std::string_view path, std::string& error) {
    if (path.empty()) {
        error = "Debug variable paths must not be empty.";
        return std::nullopt;
    }
    if (path.front() == '/') {
        error = "Debug variable paths must not start with '/'.";
        return std::nullopt;
    }
    if (path.back() == '/') {
        error = "Debug variable paths must not end with '/'.";
        return std::nullopt;
    }

    ParsedPath parsed;
    parsed.m_path.assign(path);

    std::size_t segment_start = 0;
    for (std::size_t index = 0; index < path.size(); ++index) {
        const unsigned char byte = static_cast<unsigned char>(path[index]);
        if (!is_valid_path_byte(byte)) {
            error = "Debug variable paths must contain only visible ASCII characters.";
            return std::nullopt;
        }
        if (path[index] != '/') {
            continue;
        }
        if (index == segment_start) {
            error = "Debug variable paths must not contain empty segments.";
            return std::nullopt;
        }
        parsed.m_segments.emplace_back(path.substr(segment_start, index - segment_start));
        segment_start = index + 1;
    }

    parsed.m_segments.emplace_back(path.substr(segment_start));
    return parsed;
}

std::string make_duplicate_message(
    std::string_view path, DebugScalarType existing_type, DebugScalarType attempted_type) {
    std::string message = "Debug variable path '";
    message.append(path);
    message.append("' is already registered as ");
    message.append(debug_scalar_type_name(existing_type));
    message.append("; attempted ");
    message.append(debug_scalar_type_name(attempted_type));
    message.append(".");
    return message;
}

std::string make_invalid_path_message(std::string_view path, std::string_view reason) {
    std::string message = "Debug variable path '";
    message.append(path);
    message.append("' is invalid: ");
    message.append(reason);
    return message;
}

DebugMenuTreeNode& find_or_add_child(std::vector<DebugMenuTreeNode>& nodes, std::string_view label, std::string path) {
    const auto existing = std::find_if(nodes.begin(), nodes.end(), [&](const DebugMenuTreeNode& node) {
        return std::string_view{node.m_label} == label;
    });
    if (existing != nodes.end()) {
        return *existing;
    }

    DebugMenuTreeNode node;
    node.m_label.assign(label);
    node.m_path = std::move(path);
    nodes.push_back(std::move(node));
    return nodes.back();
}

void sort_tree_nodes(std::vector<DebugMenuTreeNode>& nodes);

void sort_tree_entries(std::vector<DebugMenuTreeEntry>& entries) {
    std::sort(entries.begin(), entries.end(), [](const DebugMenuTreeEntry& left, const DebugMenuTreeEntry& right) {
        return left.m_label < right.m_label;
    });
}

void sort_tree_nodes(std::vector<DebugMenuTreeNode>& nodes) {
    std::sort(nodes.begin(), nodes.end(), [](const DebugMenuTreeNode& left, const DebugMenuTreeNode& right) {
        return left.m_label < right.m_label;
    });

    for (DebugMenuTreeNode& node : nodes) {
        sort_tree_nodes(node.m_children);
        sort_tree_entries(node.m_entries);
    }
}

void insert_tree_entry(DebugMenuTree& tree,
    const std::vector<std::string>& segments,
    DebugScalarType type,
    std::string_view path,
    std::uint64_t id,
    std::size_t entry_index) {
    DebugMenuTreeEntry entry;
    entry.m_type = type;
    entry.m_label = segments.back();
    entry.m_path.assign(path);
    entry.m_registration_id = id;
    entry.m_entry_index = entry_index;

    if (segments.size() == 1U) {
        tree.m_entries.push_back(std::move(entry));
        return;
    }

    std::vector<DebugMenuTreeNode>* current_level = &tree.m_nodes;
    std::string current_path;
    for (std::size_t index = 0; index + 1U < segments.size(); ++index) {
        if (!current_path.empty()) {
            current_path.push_back('/');
        }
        current_path.append(segments[index]);
        DebugMenuTreeNode& node = find_or_add_child(*current_level, segments[index], current_path);
        current_level = &node.m_children;
        if (index + 2U == segments.size()) {
            node.m_entries.push_back(std::move(entry));
            return;
        }
    }
}

} // namespace

const char* debug_scalar_type_name(DebugScalarType type) noexcept {
    switch (type) {
    case DebugScalarType::Bool:
        return "bool";
    case DebugScalarType::Int:
        return "int";
    case DebugScalarType::Float:
        return "float";
    }
    return "unknown";
}

const char* debug_menu_diagnostic_kind_name(DebugMenuDiagnosticKind kind) noexcept {
    switch (kind) {
    case DebugMenuDiagnosticKind::InvalidPath:
        return "invalid_path";
    case DebugMenuDiagnosticKind::DuplicatePath:
        return "duplicate_path";
    }
    return "unknown";
}

DebugMenu& DebugMenu::instance() noexcept {
    static DebugMenu* debug_menu = new DebugMenu();
    return *debug_menu;
}

std::uint64_t DebugMenu::registry_generation() const noexcept {
    return m_registry_generation;
}

bool DebugMenu::refresh_tree_if_dirty() {
    if (m_tree.m_generation == m_registry_generation) {
        return false;
    }

    DebugMenuTree rebuilt;
    rebuilt.m_generation = m_registry_generation;

    for (std::size_t index = 0; index < m_bool_entries.size(); ++index) {
        const DebugBoolEntry& entry = m_bool_entries[index];
        insert_tree_entry(rebuilt, entry.m_path_segments, DebugScalarType::Bool, entry.m_path, entry.m_id, index);
    }
    for (std::size_t index = 0; index < m_int_entries.size(); ++index) {
        const DebugIntEntry& entry = m_int_entries[index];
        insert_tree_entry(rebuilt, entry.m_path_segments, DebugScalarType::Int, entry.m_path, entry.m_id, index);
    }
    for (std::size_t index = 0; index < m_float_entries.size(); ++index) {
        const DebugFloatEntry& entry = m_float_entries[index];
        insert_tree_entry(rebuilt, entry.m_path_segments, DebugScalarType::Float, entry.m_path, entry.m_id, index);
    }

    sort_tree_nodes(rebuilt.m_nodes);
    sort_tree_entries(rebuilt.m_entries);
    m_tree = std::move(rebuilt);
    return true;
}

std::optional<bool> DebugMenu::get_bool(std::string_view path) const {
    const DebugBoolEntry* entry = find_bool_entry(path);
    if (entry == nullptr || entry->m_variable == nullptr) {
        return std::nullopt;
    }
    return entry->m_variable->value();
}

bool DebugMenu::set_bool(std::string_view path, bool value) {
    DebugBoolEntry* entry = find_bool_entry(path);
    if (entry == nullptr || entry->m_variable == nullptr) {
        return false;
    }
    entry->m_variable->set(value);
    return true;
}

std::span<const DebugBoolEntry> DebugMenu::bool_entries() const noexcept {
    return {m_bool_entries.data(), m_bool_entries.size()};
}

std::optional<int> DebugMenu::get_int(std::string_view path) const {
    const DebugIntEntry* entry = find_int_entry(path);
    if (entry == nullptr || entry->m_variable == nullptr) {
        return std::nullopt;
    }
    return entry->m_variable->value();
}

bool DebugMenu::set_int(std::string_view path, int value) {
    DebugIntEntry* entry = find_int_entry(path);
    if (entry == nullptr || entry->m_variable == nullptr) {
        return false;
    }
    entry->m_variable->set(value);
    return true;
}

std::span<const DebugIntEntry> DebugMenu::int_entries() const noexcept {
    return {m_int_entries.data(), m_int_entries.size()};
}

std::optional<float> DebugMenu::get_float(std::string_view path) const {
    const DebugFloatEntry* entry = find_float_entry(path);
    if (entry == nullptr || entry->m_variable == nullptr) {
        return std::nullopt;
    }
    return entry->m_variable->value();
}

bool DebugMenu::set_float(std::string_view path, float value) {
    DebugFloatEntry* entry = find_float_entry(path);
    if (entry == nullptr || entry->m_variable == nullptr) {
        return false;
    }
    entry->m_variable->set(value);
    return true;
}

std::span<const DebugFloatEntry> DebugMenu::float_entries() const noexcept {
    return {m_float_entries.data(), m_float_entries.size()};
}

const DebugMenuTree& DebugMenu::tree() const noexcept {
    return m_tree;
}

DebugMenuDiagnostics DebugMenu::diagnostics() const noexcept {
    return {m_diagnostics.data(), m_diagnostics.size()};
}

DebugMenu::RegistrationResult DebugMenu::register_bool(DebugBool& variable, std::string_view path, bool default_value) {
    std::optional<RegistrationSeed> seed = prepare_registration(DebugScalarType::Bool, path);
    if (!seed.has_value()) {
        return {};
    }

    const std::uint64_t id = seed->m_id;
    m_bool_entries.push_back(
        DebugBoolEntry{id, std::move(seed->m_path), std::move(seed->m_path_segments), &variable, default_value});
    accept_registration(DebugScalarType::Bool, id, m_bool_entries.back().m_path);
    return {true, id};
}

DebugMenu::RegistrationResult DebugMenu::register_int(DebugInt& variable, std::string_view path, int default_value) {
    std::optional<RegistrationSeed> seed = prepare_registration(DebugScalarType::Int, path);
    if (!seed.has_value()) {
        return {};
    }

    const std::uint64_t id = seed->m_id;
    m_int_entries.push_back(
        DebugIntEntry{id, std::move(seed->m_path), std::move(seed->m_path_segments), &variable, default_value});
    accept_registration(DebugScalarType::Int, id, m_int_entries.back().m_path);
    return {true, id};
}

DebugMenu::RegistrationResult DebugMenu::register_float(
    DebugFloat& variable, std::string_view path, float default_value) {
    std::optional<RegistrationSeed> seed = prepare_registration(DebugScalarType::Float, path);
    if (!seed.has_value()) {
        return {};
    }

    const std::uint64_t id = seed->m_id;
    m_float_entries.push_back(
        DebugFloatEntry{id, std::move(seed->m_path), std::move(seed->m_path_segments), &variable, default_value});
    accept_registration(DebugScalarType::Float, id, m_float_entries.back().m_path);
    return {true, id};
}

void DebugMenu::unregister_bool(std::uint64_t id) noexcept {
    const auto entry = std::find_if(m_bool_entries.begin(), m_bool_entries.end(), [&](const DebugBoolEntry& candidate) {
        return candidate.m_id == id;
    });
    if (entry == m_bool_entries.end()) {
        return;
    }

    m_registered_paths.erase(entry->m_path);
    m_bool_entries.erase(entry);
    ++m_registry_generation;
}

void DebugMenu::unregister_int(std::uint64_t id) noexcept {
    const auto entry = std::find_if(m_int_entries.begin(), m_int_entries.end(), [&](const DebugIntEntry& candidate) {
        return candidate.m_id == id;
    });
    if (entry == m_int_entries.end()) {
        return;
    }

    m_registered_paths.erase(entry->m_path);
    m_int_entries.erase(entry);
    ++m_registry_generation;
}

void DebugMenu::unregister_float(std::uint64_t id) noexcept {
    const auto entry = std::find_if(m_float_entries.begin(),
        m_float_entries.end(),
        [&](const DebugFloatEntry& candidate) { return candidate.m_id == id; });
    if (entry == m_float_entries.end()) {
        return;
    }

    m_registered_paths.erase(entry->m_path);
    m_float_entries.erase(entry);
    ++m_registry_generation;
}

std::optional<DebugMenu::RegistrationSeed> DebugMenu::prepare_registration(
    DebugScalarType type, std::string_view path) {
    std::string error;
    std::optional<ParsedPath> parsed = parse_debug_path(path, error);
    if (!parsed.has_value()) {
        add_diagnostic(DebugMenuDiagnosticKind::InvalidPath, type, path, make_invalid_path_message(path, error));
        return std::nullopt;
    }

    const auto duplicate = m_registered_paths.find(parsed->m_path);
    if (duplicate != m_registered_paths.end()) {
        add_diagnostic(DebugMenuDiagnosticKind::DuplicatePath,
            type,
            parsed->m_path,
            make_duplicate_message(parsed->m_path, duplicate->second.m_type, type));
        return std::nullopt;
    }

    RegistrationSeed seed;
    seed.m_id = m_next_registration_id++;
    seed.m_path = std::move(parsed->m_path);
    seed.m_path_segments = std::move(parsed->m_segments);
    return seed;
}

void DebugMenu::accept_registration(DebugScalarType type, std::uint64_t id, const std::string& path) {
    m_registered_paths.emplace(path, RegisteredPath{type, id});
    ++m_registry_generation;
}

void DebugMenu::add_diagnostic(
    DebugMenuDiagnosticKind kind, DebugScalarType attempted_type, std::string_view path, std::string message) {
    DebugMenuDiagnostic diagnostic;
    diagnostic.m_kind = kind;
    diagnostic.m_attempted_type = attempted_type;
    diagnostic.m_path.assign(path);
    diagnostic.m_message = std::move(message);
    m_diagnostics.push_back(std::move(diagnostic));
}

const DebugBoolEntry* DebugMenu::find_bool_entry(std::string_view path) const noexcept {
    const auto entry = std::find_if(m_bool_entries.begin(), m_bool_entries.end(), [&](const DebugBoolEntry& candidate) {
        return std::string_view{candidate.m_path} == path;
    });
    return entry == m_bool_entries.end() ? nullptr : &*entry;
}

DebugBoolEntry* DebugMenu::find_bool_entry(std::string_view path) noexcept {
    auto entry = std::find_if(m_bool_entries.begin(), m_bool_entries.end(), [&](const DebugBoolEntry& candidate) {
        return std::string_view{candidate.m_path} == path;
    });
    return entry == m_bool_entries.end() ? nullptr : &*entry;
}

const DebugIntEntry* DebugMenu::find_int_entry(std::string_view path) const noexcept {
    const auto entry = std::find_if(m_int_entries.begin(), m_int_entries.end(), [&](const DebugIntEntry& candidate) {
        return std::string_view{candidate.m_path} == path;
    });
    return entry == m_int_entries.end() ? nullptr : &*entry;
}

DebugIntEntry* DebugMenu::find_int_entry(std::string_view path) noexcept {
    auto entry = std::find_if(m_int_entries.begin(), m_int_entries.end(), [&](const DebugIntEntry& candidate) {
        return std::string_view{candidate.m_path} == path;
    });
    return entry == m_int_entries.end() ? nullptr : &*entry;
}

const DebugFloatEntry* DebugMenu::find_float_entry(std::string_view path) const noexcept {
    const auto entry = std::find_if(m_float_entries.begin(),
        m_float_entries.end(),
        [&](const DebugFloatEntry& candidate) { return std::string_view{candidate.m_path} == path; });
    return entry == m_float_entries.end() ? nullptr : &*entry;
}

DebugFloatEntry* DebugMenu::find_float_entry(std::string_view path) noexcept {
    auto entry = std::find_if(m_float_entries.begin(), m_float_entries.end(), [&](const DebugFloatEntry& candidate) {
        return std::string_view{candidate.m_path} == path;
    });
    return entry == m_float_entries.end() ? nullptr : &*entry;
}

} // namespace ofg
