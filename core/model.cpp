#include "model.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <string_view>
#include <unordered_set>

namespace radial {
namespace {
bool validId(const std::string& id) {
    return !id.empty() && id.size() <= 128 &&
        std::all_of(id.begin(), id.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        });
}
}

bool isValidKey(const std::string& key) {
    if (key.size() == 1)
        return (key[0] >= 'A' && key[0] <= 'Z') || (key[0] >= '0' && key[0] <= '9');
    if (key.size() >= 2 && key.size() <= 3 && key[0] == 'F') {
        int n = 0;
        for (std::size_t i = 1; i < key.size(); ++i) {
            if (key[i] < '0' || key[i] > '9') return false;
            n = n * 10 + key[i] - '0';
        }
        return key[1] != '0' && n >= 1 && n <= 24;
    }
    static const std::unordered_set<std::string> names{
        "Space", "Enter", "Tab", "Escape", "Backspace", "Delete", "Insert",
        "Home", "End", "PageUp", "PageDown", "ArrowLeft", "ArrowRight",
        "ArrowUp", "ArrowDown", "Minus", "Equal", "BracketLeft", "BracketRight",
        "Backslash", "Semicolon", "Quote", "Comma", "Period", "Slash", "Backquote"};
    return names.contains(key);
}

std::vector<ValidationIssue> validate(const Profile& profile) {
    std::vector<ValidationIssue> issues;
    auto issue = [&](std::string code, std::string path, std::string message) {
        issues.push_back({std::move(code), std::move(path), std::move(message)});
    };
    if (profile.schemaVersion != ProfileSchemaVersion)
        issue("schema_version", "schemaVersion", "Unsupported profile schema version.");
    if (!validId(profile.id))
        issue("invalid_id", "id", "Profile ID must be a nonempty portable identifier.");
    std::unordered_map<std::string, std::string> owners;
    auto slots = [&](const SlotArray& entries, const std::string& path) {
        std::set<std::string> used;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (!entries[i]) continue;
            const auto at = path + "[" + std::to_string(i) + "]";
            if (!profile.nodes.contains(*entries[i]))
                issue("missing_node", at, "Slot refers to an unknown node.");
            if (!used.insert(*entries[i]).second)
                issue("duplicate_slot", at, "A node cannot occupy two slots in the same group.");
            const auto [owner, inserted] = owners.emplace(*entries[i], at);
            if (!inserted)
                issue("multiple_parents", at, "A node may occupy only one slot; it is already placed at " + owner->second + ".");
        }
    };
    slots(profile.rootSlots, "rootSlots");
    std::vector<std::string> keys;
    keys.reserve(profile.nodes.size());
    for (const auto& [key, node] : profile.nodes) { (void)node; keys.push_back(key); }
    std::sort(keys.begin(), keys.end());
    std::set<std::string> nodeIds, actionIds;
    for (const auto& key : keys) {
        const auto& node = profile.nodes.at(key);
        const auto path = "nodes." + key;
        if (!validId(node.id)) issue("invalid_id", path + ".id", "Invalid node ID.");
        if (node.id != key)
            issue("id_mismatch", path + ".id", "Node ID must match its map key.");
        if (!nodeIds.insert(node.id).second)
            issue("duplicate_id", path + ".id", "Duplicate node ID.");
        const bool hasChildren = std::any_of(node.children.begin(), node.children.end(),
                                             [](const auto& child) { return child.has_value(); });
        slots(node.children, path + ".children");
        if (node.kind == NodeKind::Group) {
            if (!hasChildren) issue("empty_group", path, "A group needs at least one child.");
            if (node.action.kind != ActionKind::None || !node.action.id.empty() ||
                !node.action.key.empty() || !node.action.text.empty() || node.action.modifiers)
                issue("group_action", path + ".action", "A group cannot also execute an action.");
            continue;
        }
        if (node.kind != NodeKind::Action)
            issue("node_kind", path + ".kind", "Unknown node kind.");
        if (hasChildren)
            issue("action_children", path + ".children", "An action cannot contain children.");
        const auto& action = node.action;
        if (action.kind != ActionKind::None) {
            if (!validId(action.id)) issue("invalid_id", path + ".action.id", "Invalid action ID.");
            if (!actionIds.insert(action.id).second)
                issue("duplicate_action_id", path + ".action.id", "Duplicate action ID.");
        }
        if ((action.modifiers & ~std::uint8_t(15)) != 0)
            issue("invalid_modifiers", path + ".action.modifiers", "Unknown modifier bits.");
        switch (action.kind) {
        case ActionKind::None:
            if (!action.key.empty() || !action.text.empty() || action.modifiers)
                issue("inactive_payload", path + ".action", "A disabled action cannot have a payload.");
            break;
        case ActionKind::Shortcut:
            if (!isValidKey(action.key))
                issue("invalid_key", path + ".action.key", "Unknown canonical shortcut key.");
            if (!action.text.empty())
                issue("shortcut_text", path + ".action.text", "A shortcut cannot contain text.");
            break;
        case ActionKind::Text:
            if (action.text.empty()) issue("empty_text", path + ".action.text", "Text cannot be empty.");
            if (!action.key.empty() || action.modifiers)
                issue("text_shortcut", path + ".action", "Text cannot also specify a shortcut.");
            break;
        default:
            issue("action_kind", path + ".action.kind", "Unknown action kind.");
        }
    }
    // Walk every node, including unplaced draft nodes, so moving an existing node
    // into a slot cannot introduce a previously hidden cycle or excessive depth.
    std::unordered_map<std::string, int> color;
    std::unordered_map<std::string, std::size_t> depths;
    std::function<std::size_t(const std::string&)> visit = [&](const std::string& id) {
        if (!profile.nodes.contains(id)) return std::size_t(0);
        if (color[id] == 1) {
            issue("cycle", "nodes." + id, "Groups must not contain a cycle.");
            return MaximumGroupDepth + 1;
        }
        if (color[id] == 2) return depths[id];
        color[id] = 1;
        const auto& node = profile.nodes.at(id);
        std::size_t depth = node.kind == NodeKind::Group ? 1 : 0;
        if (node.kind == NodeKind::Group) {
            for (const auto& child : node.children) if (child)
                depth = std::max(depth, 1 + visit(*child));
        }
        color[id] = 2;
        depths[id] = depth;
        if (depth > MaximumGroupDepth)
            issue("maximum_depth", "nodes." + id, "At most four group levels are supported.");
        return depth;
    };
    for (const auto& key : keys) visit(key);
    return issues;
}

Profile defaultProfile() {
    Profile profile;
    profile.id = "tablet-pro.default";
    profile.name = "Artist tools";
    auto shortcut = [&](std::string id, std::string label, std::string icon,
                        std::string key, std::uint8_t modifiers = 0) {
        Node node;
        node.id = id; node.label = std::move(label); node.icon = std::move(icon);
        node.action = {"command." + id, ActionKind::Shortcut, std::move(key), {}, modifiers};
        profile.nodes.emplace(id, std::move(node));
        return id;
    };
    auto group = [&](std::string id, std::string label, std::string icon, SlotArray children) {
        Node node;
        node.id = id; node.label = std::move(label); node.icon = std::move(icon);
        node.kind = NodeKind::Group; node.children = std::move(children);
        profile.nodes.emplace(id, std::move(node));
        return id;
    };
    const auto brush = shortcut("brush", "Brush", "paintbrush", "B");
    const auto eraser = shortcut("eraser", "Eraser", "eraser", "E");
    const auto select = shortcut("select", "Select", "lasso-select", "L");
    const auto color = shortcut("color", "Color", "palette", "I");
    const auto bold = shortcut("text.bold", "Bold", "bold", "B", Ctrl);
    const auto italic = shortcut("text.italic", "Italic", "italic", "I", Ctrl);
    const auto copy = shortcut("text.copy", "Copy", "copy", "C", Ctrl);
    const auto paste = shortcut("text.paste", "Paste", "clipboard-paste", "V", Ctrl);
    const auto cut = shortcut("text.cut", "Cut", "scissors", "X", Ctrl);
    const auto underline = shortcut("text.underline", "Underline", "underline", "U", Ctrl);
    const auto selectAll = shortcut("text.select-all", "Select all", "text-select", "A", Ctrl);
    const auto lineStart = shortcut("text.line-start", "Line start", "arrow-left-to-line", "Home");
    const auto lineEnd = shortcut("text.line-end", "Line end", "arrow-right-to-line", "End");
    const auto more = group("text.more", "More text", "ellipsis", {
        underline, selectAll, std::nullopt, std::nullopt, lineStart, lineEnd});
    const auto text = group("text", "Text", "type", {bold, italic, copy, paste, cut, more});
    const auto undo = shortcut("settings.undo", "Undo", "undo-2", "Z", Ctrl);
    const auto redo = shortcut("settings.redo", "Redo", "redo-2", "Z", Ctrl | Shift);
    const auto save = shortcut("settings.save", "Save", "save", "S", Ctrl);
    const auto escape = shortcut("settings.escape", "Escape", "x", "Escape");
    const auto remove = shortcut("settings.delete", "Delete", "trash-2", "Delete");
    const auto settings = group("settings", "Settings", "settings-2", {
        undo, redo, save, std::nullopt, escape, remove});
    profile.rootSlots = {brush, eraser, select, color, text, settings};
    return profile;
}

} // namespace radial
