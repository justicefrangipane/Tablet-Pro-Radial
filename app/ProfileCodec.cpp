#include "ProfileCodec.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace tabletpro {
namespace {
QJsonArray slotsJson(const radial::SlotArray& entries) {
    QJsonArray result;
    for (const auto& slot : entries) result.append(slot ? QJsonValue(QString::fromStdString(*slot)) : QJsonValue(QJsonValue::Null));
    return result;
}
bool readSlots(const QJsonValue& value, radial::SlotArray& out) {
    if (!value.isArray() || value.toArray().size() != 6) return false;
    const auto values = value.toArray();
    for (int i = 0; i < 6; ++i) {
        if (values[i].isNull()) out[i].reset();
        else if (values[i].isString() && !values[i].toString().isEmpty()) out[i] = values[i].toString().toStdString();
        else return false;
    }
    return true;
}
}
QByteArray encodeProfile(const radial::Profile& profile) {
    QJsonObject root{{"format", "tabletpro.radial"}, {"schemaVersion", profile.schemaVersion},
        {"id", QString::fromStdString(profile.id)}, {"name", QString::fromStdString(profile.name)},
        {"rootSlots", slotsJson(profile.rootSlots)}};
    QJsonArray nodes;
    std::vector<std::string> ids;
    for (const auto& [id, node] : profile.nodes) { (void)node; ids.push_back(id); }
    std::sort(ids.begin(), ids.end());
    for (const auto& id : ids) {
        const auto& node = profile.nodes.at(id);
        QJsonObject item{{"id", QString::fromStdString(node.id)}, {"label", QString::fromStdString(node.label)}, {"icon", QString::fromStdString(node.icon)}, {"kind", node.kind == radial::NodeKind::Group ? "group" : "action"}};
        if (node.kind == radial::NodeKind::Group) item["slots"] = slotsJson(node.children);
        else {
            QJsonArray mods;
            for (const auto& pair : {std::pair{radial::Ctrl,"Ctrl"}, {radial::Shift,"Shift"}, {radial::Alt,"Alt"}, {radial::Meta,"Meta"}})
                if (node.action.modifiers & pair.first) mods.append(pair.second);
            const char* type = node.action.kind == radial::ActionKind::Shortcut ? "shortcut" : node.action.kind == radial::ActionKind::Text ? "text" : "none";
            item["action"] = QJsonObject{{"id", QString::fromStdString(node.action.id)}, {"type", type}, {"key", QString::fromStdString(node.action.key)}, {"modifiers", mods}, {"text", QString::fromStdString(node.action.text)}};
        }
        nodes.append(item);
    }
    root["nodes"] = nodes;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}
DecodedProfile decodeProfile(const QByteArray& bytes) {
    auto fail = [](const QString& error) { return DecodedProfile{std::nullopt, error}; };
    if (bytes.size() > 1024 * 1024) return fail("Profile exceeds the 1 MiB limit.");
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return fail("Invalid JSON: " + parseError.errorString());
    const auto root = doc.object();
    if (root["format"] != "tabletpro.radial") return fail("This is not a Tablet Pro radial profile. Legacy import is reserved for a future adapter.");
    if (!root["schemaVersion"].isDouble() || root["schemaVersion"].toDouble() != 1) return fail("Unsupported profile schema. This build reads schema 1 only.");
    if (!root["id"].isString() || !root["name"].isString()) return fail("Profile ID and name must be strings.");
    radial::Profile p; p.id = root["id"].toString().toStdString(); p.name = root["name"].toString().toStdString();
    if (!readSlots(root["rootSlots"], p.rootSlots)) return fail("rootSlots must contain exactly six IDs or null values.");
    if (!root["nodes"].isArray() || root["nodes"].toArray().size() > 256) return fail("nodes must be an array of at most 256 nodes.");
    for (const auto& v : root["nodes"].toArray()) {
        if (!v.isObject()) return fail("Each node must be an object.");
        auto n = v.toObject(); radial::Node node;
        for (const auto& key : {"id","label","icon","kind"}) if (!n[key].isString()) return fail(QString("Node field %1 must be a string.").arg(key));
        node.id = n["id"].toString().toStdString(); node.label = n["label"].toString().toStdString(); node.icon = n["icon"].toString().toStdString();
        if (node.label.size() > 240 || node.icon.size() > 64 || node.id.size() > 128) return fail("A node field exceeds its size limit.");
        if (p.nodes.contains(node.id)) return fail("Duplicate node ID: " + QString::fromStdString(node.id));
        if (n["kind"] == "group") {
            if (n.contains("action")) return fail("A group cannot also contain an action. No data was changed.");
            node.kind = radial::NodeKind::Group;
            if (!readSlots(n["slots"], node.children)) return fail("Group slots must contain exactly six IDs or null values.");
        } else if (n["kind"] == "action") {
            if (n.contains("slots")) return fail("An action cannot also contain child slots. No data was changed.");
            if (!n["action"].isObject()) return fail("Action node is missing its typed action.");
            auto action = n["action"].toObject();
            if (!action["id"].isString() || !action["type"].isString()) return fail("Action ID and type must be strings.");
            node.action.id = action["id"].toString().toStdString();
            const auto type = action["type"].toString();
            if (type == "shortcut") node.action.kind = radial::ActionKind::Shortcut;
            else if (type == "text") node.action.kind = radial::ActionKind::Text;
            else if (type == "none") node.action.kind = radial::ActionKind::None;
            else return fail("Unsupported action type: " + type + ". The file was not changed.");
            if (!action["key"].isString() || !action["text"].isString() || !action["modifiers"].isArray()) return fail("Action key, text, or modifiers have invalid types.");
            node.action.key = action["key"].toString().toStdString(); node.action.text = action["text"].toString().toStdString();
            if (node.action.text.size() > 4096) return fail("Text actions are limited to 4096 UTF-8 bytes.");
            for (const auto& mod : action["modifiers"].toArray()) {
                int bit = mod == "Ctrl" ? radial::Ctrl : mod == "Shift" ? radial::Shift : mod == "Alt" ? radial::Alt : mod == "Meta" ? radial::Meta : 0;
                if (!bit || (node.action.modifiers & bit)) return fail("Invalid or duplicate shortcut modifier.");
                node.action.modifiers |= bit;
            }
        } else return fail("Unknown node kind.");
        p.nodes.emplace(node.id, std::move(node));
    }
    const auto issues = radial::validate(p);
    if (!issues.empty()) return fail(QString::fromStdString(issues.front().path + ": " + issues.front().message));
    return {std::move(p), {}};
}

QByteArray encodeTestDocument(const radial::Profile& profile, const RadialSettings& settings) {
    const QJsonObject root{{"format", "tabletpro.radial.test"}, {"schemaVersion", 1},
        {"profile", QJsonDocument::fromJson(encodeProfile(profile)).object()},
        {"settings", QJsonObject{{"soundEnabled", settings.soundEnabled},
            {"reducedMotion", settings.reducedMotion}, {"durationMs", settings.durationMs},
            {"backdropEnabled", settings.backdropEnabled}}}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

DecodedRadialDocument decodeRadialDocument(const QByteArray& bytes) {
    auto fail = [](const QString& error) { return DecodedRadialDocument{std::nullopt, std::nullopt, error}; };
    if (bytes.size() > 1024 * 1024) return fail("Radial document exceeds the 1 MiB limit.");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return fail("Invalid JSON: " + error.errorString());
    const auto root = document.object();
    if (root["format"] == "tabletpro.radial") {
        auto result = decodeProfile(bytes);
        return {std::move(result.profile), std::nullopt, std::move(result.error)};
    }
    if (root["format"] != "tabletpro.radial.test") return fail("This is not a Tablet Pro radial document.");
    if (!root["schemaVersion"].isDouble() || root["schemaVersion"].toDouble() != 1)
        return fail("Unsupported test document schema. This build reads schema 1 only.");
    for (const auto& key : root.keys())
        if (!QStringList{"format", "schemaVersion", "profile", "settings"}.contains(key))
            return fail("Unknown test document field: " + key);
    if (!root["profile"].isObject()) return fail("Test document must contain a radial profile object.");
    auto profile = decodeProfile(QJsonDocument(root["profile"].toObject()).toJson(QJsonDocument::Compact));
    if (!profile.profile) return fail(profile.error);
    std::optional<RadialSettings> settings;
    if (root.contains("settings")) {
        if (!root["settings"].isObject()) return fail("Test document settings must be an object.");
        const auto values = root["settings"].toObject();
        const QStringList fields{"soundEnabled", "reducedMotion", "durationMs", "backdropEnabled"};
        for (const auto& key : values.keys())
            if (!fields.contains(key)) return fail("Unknown radial setting: " + key);
        for (const auto& key : {"soundEnabled", "reducedMotion", "backdropEnabled"})
            if (!values[key].isBool()) return fail(QString("Setting %1 must be a boolean.").arg(key));
        const auto duration = values["durationMs"];
        if (!duration.isDouble() || !std::isfinite(duration.toDouble()) || std::floor(duration.toDouble()) != duration.toDouble()
            || duration.toDouble() < 250 || duration.toDouble() > 2400)
            return fail("Setting durationMs must be an integer from 250 to 2400.");
        settings = RadialSettings{values["soundEnabled"].toBool(), values["reducedMotion"].toBool(),
            duration.toInt(), values["backdropEnabled"].toBool()};
    }
    return {std::move(profile.profile), settings, {}};
}
}
