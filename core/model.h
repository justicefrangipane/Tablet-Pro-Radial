#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace radial {

inline constexpr int ProfileSchemaVersion = 1;
inline constexpr std::size_t SlotCount = 6;
inline constexpr std::size_t MaximumGroupDepth = 4;
using SlotArray = std::array<std::optional<std::string>, SlotCount>;

enum class NodeKind { Action, Group };
enum class ActionKind { None, Shortcut, Text, Modifier };
enum Modifier : std::uint8_t { Shift = 1, Ctrl = 2, Alt = 4, Meta = 8 };

struct Action {
    std::string id;
    ActionKind kind = ActionKind::None;
    // Canonical portable key: A-Z, 0-9, F1-F24, or a name accepted by isValidKey.
    std::string key;
    std::string text;
    std::uint8_t modifiers = 0;
    bool operator==(const Action&) const = default;
};

struct Node {
    std::string id;
    std::string label;
    std::string icon;
    NodeKind kind = NodeKind::Action;
    Action action;
    SlotArray children{};
    bool operator==(const Node&) const = default;
};

struct Profile {
    int schemaVersion = ProfileSchemaVersion;
    std::string id;
    std::string name;
    SlotArray rootSlots{};
    std::unordered_map<std::string, Node> nodes;
    bool operator==(const Profile&) const = default;
};

struct ValidationIssue {
    std::string code;
    std::string path;
    std::string message;
};

bool isValidKey(const std::string& key);
std::vector<ValidationIssue> validate(const Profile& profile);
Profile defaultProfile();
Profile legacyDefaultProfile();

} // namespace radial
