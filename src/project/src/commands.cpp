// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/commands.hpp"

#include "fos/furniture/edit.hpp"

#include <algorithm>
#include <charconv>

namespace fos::project {

std::string CommandLabel::english() const {
    std::string out;
    for (std::size_t i = 0; i < key.size(); ++i) {
        if (key[i] == '{') {
            const std::size_t close = key.find('}', i);
            if (close != std::string::npos) {
                const std::string index = key.substr(i + 1, close - i - 1);
                std::size_t n = 0;
                const auto [ptr, ec] = std::from_chars(index.data(), index.data() + index.size(), n);
                if (ec == std::errc() && ptr == index.data() + index.size() && !index.empty() && n < args.size()) {
                    out += args[n];
                    i = close;
                    continue;
                }
            }
        }
        out += key[i];
    }
    return out;
}

namespace commands {
namespace {

using furniture::CabinetDefinition;
using furniture::EditResult;
using furniture::Libraries;

std::string number(double v) {
    char buf[64];
    const auto [ptr, ec] = std::to_chars(buf, buf + sizeof buf, v);
    return ec == std::errc() ? std::string(buf, ptr) : std::string("?");
}

core::Error noItem(const std::string& id) {
    return core::makeError(core::ErrorCode::NotFound, "The project has no furniture item \"" + id + "\".", id);
}

std::vector<std::string> allItemIds(const Project& p) {
    std::vector<std::string> ids;
    for (const auto& item : p.items) ids.push_back(item.id());
    return ids;
}

Affected items(std::vector<std::string> ids) {
    Affected a;
    a.items = std::move(ids);
    return a;
}

/// Command that edits one cabinet through the atomic furniture editing API.
Command cabinetEdit(std::string itemId, CommandLabel label,
                    std::function<core::Result<EditResult>(CabinetDefinition&, const Libraries&)> edit,
                    std::string mergeKey = {}) {
    Command c;
    c.label = std::move(label);
    c.mergeKey = std::move(mergeKey);
    c.apply = [itemId = std::move(itemId), edit = std::move(edit)](Project& p) -> core::Result<Affected> {
        Item* item = p.findItem(itemId);
        if (item == nullptr) return noItem(itemId);
        CabinetDefinition* cab = item->cabinet();
        if (cab == nullptr)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Item \"" + itemId + "\" is a panel; this edit applies to cabinets only.", itemId);
        auto r = edit(*cab, p.libraries);
        if (!r) return r.error();
        return items({itemId});
    };
    return c;
}

/// Label argument: the item name when it exists at execution time is not known here, so the
/// label carries the item id; the UI may map it to the display name.
CommandLabel label(std::string key, std::vector<std::string> args) { return {std::move(key), std::move(args)}; }

/// Library ids an item references.
struct Refs {
    std::vector<std::string> materials, edgeBands, hardware;
};

void addEdge(Refs& r, const std::optional<domain::EdgeBandId>& e) {
    if (e && !e->empty()) r.edgeBands.push_back(e->str());
}

Refs referencesOf(const Item& item) {
    Refs r;
    if (const auto* c = item.cabinet()) {
        const auto& m = c->materials;
        for (const auto* id : {&m.carcass, &m.back, &m.front})
            if (!id->empty()) r.materials.push_back(id->str());
        if (!m.visibleEdge.empty()) r.edgeBands.push_back(m.visibleEdge.str());
        addEdge(r, m.hiddenEdge);
        addEdge(r, m.frontEdge);
        for (const auto& [slot, o] : m.overrides) {
            if (o.material) r.materials.push_back(o.material->str());
            for (const auto& [side, e] : o.edges) addEdge(r, e);
        }
        const auto& h = c->hardware;
        for (const auto* id : {&h.hinge, &h.handle, &h.shelfPin, &h.confirmat, &h.dowel, &h.foot})
            r.hardware.push_back(id->str());
    } else {
        const auto& p = item.panel()->panel;
        r.materials.push_back(p.material.str());
        for (const auto& e : p.edges) addEdge(r, e);
    }
    return r;
}

template <class T, class Pick>
Command upsert(T entry, const char* what, Pick pick) {
    Command c;
    c.label = label(std::string("Edit ") + what + " {0}", {entry.name});
    c.apply = [entry = std::move(entry), pick, what](Project& p) -> core::Result<Affected> {
        if (entry.id.empty())
            return core::makeError(core::ErrorCode::InvalidArgument, std::string("A ") + what + " needs a non-empty id.");
        auto& list = pick(p.libraries);
        const auto it = std::find_if(list.begin(), list.end(), [&](const T& e) { return e.id == entry.id; });
        if (it == list.end()) list.push_back(entry);
        else *it = entry;
        Affected a = items(allItemIds(p));
        a.libraries = true;
        return a;
    };
    return c;
}

template <class Pick, class RefList>
Command removeEntry(std::string id, const char* what, Pick pick, RefList refList) {
    Command c;
    c.label = label(std::string("Remove ") + what + " {0}", {id});
    c.apply = [id = std::move(id), what, pick, refList](Project& p) -> core::Result<Affected> {
        auto& list = pick(p.libraries);
        const auto it = std::find_if(list.begin(), list.end(), [&](const auto& e) { return e.id.str() == id; });
        if (it == list.end())
            return core::makeError(core::ErrorCode::NotFound, std::string("The library has no ") + what + " \"" + id + "\".", id);
        std::string users;
        for (const auto& item : p.items) {
            const auto refs = refList(referencesOf(item));
            if (std::find(refs.begin(), refs.end(), id) != refs.end()) users += (users.empty() ? "\"" : ", \"") + item.id() + "\"";
        }
        if (!users.empty())
            return core::makeError(core::ErrorCode::ConstraintViolation,
                                   std::string("The ") + what + " \"" + id + "\" cannot be removed because it is used by " +
                                       users + ".",
                                   id);
        list.erase(it);
        Affected a = items(allItemIds(p));
        a.libraries = true;
        return a;
    };
    return c;
}

} // namespace

// ------------------------------------------------------------------ parameters

Command setParameter(std::string itemId, std::string name, double value) {
    Command c;
    c.label = label("Set {1} of {0} to {2}", {itemId, name, number(value)});
    c.mergeKey = "parameter:" + itemId + ":" + name;
    c.apply = [itemId, name, value](Project& p) -> core::Result<Affected> {
        Item* item = p.findItem(itemId);
        if (item == nullptr) return noItem(itemId);
        if (auto* cab = item->cabinet()) {
            auto r = furniture::setParameter(*cab, p.libraries, name, value);
            if (!r) return r.error();
        } else {
            auto r = item->panel()->panel.parameters.setValue(name, value);
            if (!r) return r.error();
        }
        return items({itemId});
    };
    return c;
}

Command setParameterExpression(std::string itemId, std::string name, std::string expression) {
    Command c;
    c.label = label("Set {1} of {0} to {2}", {itemId, name, expression});
    c.mergeKey = "parameter:" + itemId + ":" + name;
    c.apply = [itemId, name, expression](Project& p) -> core::Result<Affected> {
        Item* item = p.findItem(itemId);
        if (item == nullptr) return noItem(itemId);
        if (auto* cab = item->cabinet()) {
            auto r = furniture::setParameterExpression(*cab, p.libraries, name, expression);
            if (!r) return r.error();
        } else {
            auto r = item->panel()->panel.parameters.setExpression(name, expression);
            if (!r) return r.error();
        }
        return items({itemId});
    };
    return c;
}

Command resetParameter(std::string itemId, std::string name) {
    return cabinetEdit(itemId, label("Reset {1} of {0}", {itemId, name}),
                       [name](CabinetDefinition& d, const Libraries& l) { return furniture::resetParameter(d, l, name); });
}

// ------------------------------------------------------------------ cabinet structure

Command setConstruction(std::string itemId, furniture::ConstructionOptions options) {
    return cabinetEdit(itemId, label("Change construction of {0}", {itemId}),
                       [options](CabinetDefinition& d, const Libraries& l) { return furniture::setConstruction(d, l, options); });
}

Command setMaterials(std::string itemId, furniture::MaterialAssignment materials) {
    return cabinetEdit(itemId, label("Change materials of {0}", {itemId}),
                       [materials](CabinetDefinition& d, const Libraries& l) { return furniture::setMaterials(d, l, materials); });
}

Command setHardware(std::string itemId, furniture::HardwareSelection hardware) {
    return cabinetEdit(itemId, label("Change hardware of {0}", {itemId}),
                       [hardware](CabinetDefinition& d, const Libraries& l) { return furniture::setHardware(d, l, hardware); });
}

Command setPartOverride(std::string itemId, std::string slot, std::optional<furniture::PartOverride> override) {
    return cabinetEdit(itemId, label("Change part {1} of {0}", {itemId, slot}),
                       [slot, override](CabinetDefinition& d, const Libraries& l) {
                           return furniture::setPartOverride(d, l, slot, override);
                       });
}

Command setInterior(std::string itemId, furniture::Section root) {
    return cabinetEdit(itemId, label("Change interior of {0}", {itemId}),
                       [root](CabinetDefinition& d, const Libraries& l) { return furniture::setInterior(d, l, root); });
}

Command replaceSection(std::string itemId, std::string path, furniture::Section section) {
    return cabinetEdit(itemId, label("Change section {1} of {0}", {itemId, path}),
                       [path, section](CabinetDefinition& d, const Libraries& l) {
                           return furniture::replaceSection(d, l, path, section);
                       });
}

Command setShelfCount(std::string itemId, std::string path, int count) {
    return cabinetEdit(itemId, label("Set shelf count of section {1} of {0} to {2}", {itemId, path, std::to_string(count)}),
                       [path, count](CabinetDefinition& d, const Libraries& l) {
                           return furniture::setShelfCount(d, l, path, count);
                       });
}

Command setFront(std::string itemId, std::string path, std::optional<furniture::FrontSpec> front) {
    return cabinetEdit(itemId, label(front ? "Set front of section {1} of {0}" : "Remove front of section {1} of {0}", {itemId, path}),
                       [path, front](CabinetDefinition& d, const Libraries& l) { return furniture::setFront(d, l, path, front); });
}

Command splitSection(std::string itemId, std::string path, furniture::SplitDirection direction,
                     std::vector<furniture::SectionSize> sizes) {
    return cabinetEdit(itemId, label("Split section {1} of {0} into {2}", {itemId, path, std::to_string(sizes.size())}),
                       [path, direction, sizes](CabinetDefinition& d, const Libraries& l) {
                           return furniture::splitSection(d, l, path, direction, sizes);
                       });
}

Command setSectionSize(std::string itemId, std::string path, furniture::SectionSize size) {
    return cabinetEdit(itemId, label("Resize section {1} of {0}", {itemId, path}),
                       [path, size](CabinetDefinition& d, const Libraries& l) {
                           return furniture::setSectionSize(d, l, path, size);
                       },
                       "section-size:" + itemId + ":" + path);
}

// ------------------------------------------------------------------ panels

Command setPanel(std::string itemId, furniture::PanelDefinition panel) {
    Command c;
    c.label = label("Change panel {0}", {itemId});
    c.apply = [itemId, panel](Project& p) -> core::Result<Affected> {
        Item* item = p.findItem(itemId);
        if (item == nullptr) return noItem(itemId);
        if (item->panel() == nullptr)
            return core::makeError(core::ErrorCode::InvalidArgument, "Item \"" + itemId + "\" is a cabinet, not a panel.", itemId);
        if (panel.id.empty())
            return core::makeError(core::ErrorCode::InvalidArgument, "Panel item \"" + itemId + "\" needs a non-empty part id.", itemId);
        item->panel()->panel = panel;
        return items({itemId});
    };
    return c;
}

// ------------------------------------------------------------------ items

Command addItem(Item item, std::optional<std::size_t> position) {
    Command c;
    c.label = label("Add {0}", {item.name()});
    c.apply = [item, position](Project& p) -> core::Result<Affected> {
        const std::string& id = item.id();
        if (id.empty()) return core::makeError(core::ErrorCode::InvalidArgument, "A new furniture item needs a non-empty id.");
        if (p.findItem(id) != nullptr)
            return core::makeError(core::ErrorCode::AlreadyExists, "The project already has a furniture item \"" + id + "\".", id);
        if (!item.placement().isRigid())
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "The placement of \"" + id + "\" is not a proper rotation plus translation.", id);
        if (const auto* pn = item.panel(); pn != nullptr && pn->panel.id.empty())
            return core::makeError(core::ErrorCode::InvalidArgument, "Panel item \"" + id + "\" needs a non-empty part id.", id);
        const std::size_t at = std::min(position.value_or(p.items.size()), p.items.size());
        p.items.insert(p.items.begin() + static_cast<std::ptrdiff_t>(at), item);
        return items({id});
    };
    return c;
}

Command removeItem(std::string itemId) {
    Command c;
    c.label = label("Remove {0}", {itemId});
    c.apply = [itemId](Project& p) -> core::Result<Affected> {
        const auto it = std::find_if(p.items.begin(), p.items.end(), [&](const Item& i) { return i.id() == itemId; });
        if (it == p.items.end()) return noItem(itemId);
        p.items.erase(it);
        Affected a = items({itemId});
        const std::string drawing = assemblyDrawingId(itemId);
        if (p.drawingEdits.erase(drawing) != 0) a.drawings.push_back(drawing);
        return a;
    };
    return c;
}

Command duplicateItem(std::string itemId, std::string newId, std::string newName, geometry::Transform placement) {
    Command c;
    c.label = label("Duplicate {0} as {1}", {itemId, newName});
    c.apply = [itemId, newId, newName, placement](Project& p) -> core::Result<Affected> {
        const auto it = std::find_if(p.items.begin(), p.items.end(), [&](const Item& i) { return i.id() == itemId; });
        if (it == p.items.end()) return noItem(itemId);
        if (newId.empty()) return core::makeError(core::ErrorCode::InvalidArgument, "The copy needs a non-empty id.");
        if (p.findItem(newId) != nullptr)
            return core::makeError(core::ErrorCode::AlreadyExists, "The project already has a furniture item \"" + newId + "\".", newId);
        if (!placement.isRigid())
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "The placement of \"" + newId + "\" is not a proper rotation plus translation.", newId);
        Item copy = *it;
        if (auto* cab = copy.cabinet()) {
            cab->id = domain::AssemblyId(newId);
            cab->name = newName;
            cab->placement = placement;
        } else {
            PanelItem& pn = *copy.panel();
            pn.id = domain::AssemblyId(newId);
            pn.panel.name = newName;
            pn.panel.id = domain::PartId(newId + "/panel");
            pn.placement = placement;
        }
        p.items.insert(it + 1, std::move(copy));
        return items({newId});
    };
    return c;
}

Command setPlacement(std::string itemId, geometry::Transform placement) {
    Command c;
    c.label = label("Move {0}", {itemId});
    c.mergeKey = "placement:" + itemId;
    c.apply = [itemId, placement](Project& p) -> core::Result<Affected> {
        Item* item = p.findItem(itemId);
        if (item == nullptr) return noItem(itemId);
        if (!placement.isRigid())
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "The placement of \"" + itemId + "\" is not a proper rotation plus translation.", itemId);
        if (auto* cab = item->cabinet()) cab->placement = placement;
        else item->panel()->placement = placement;
        return items({itemId});
    };
    return c;
}

Command renameItem(std::string itemId, std::string name) {
    Command c;
    c.label = label("Rename {0} to {1}", {itemId, name});
    c.apply = [itemId, name](Project& p) -> core::Result<Affected> {
        Item* item = p.findItem(itemId);
        if (item == nullptr) return noItem(itemId);
        if (auto* cab = item->cabinet()) cab->name = name;
        else item->panel()->panel.name = name;
        return items({itemId});
    };
    return c;
}

// ------------------------------------------------------------------ libraries

Command upsertMaterial(domain::Material material) {
    return upsert(std::move(material), "material", [](Libraries& l) -> auto& { return l.materials; });
}
Command upsertEdgeBand(domain::EdgeBand edgeBand) {
    return upsert(std::move(edgeBand), "edge band", [](Libraries& l) -> auto& { return l.edgeBands; });
}
Command upsertHardware(domain::HardwareDefinition definition) {
    return upsert(std::move(definition), "hardware definition", [](Libraries& l) -> auto& { return l.hardware; });
}
Command removeMaterial(std::string materialId) {
    return removeEntry(std::move(materialId), "material", [](Libraries& l) -> auto& { return l.materials; },
                       [](const Refs& r) { return r.materials; });
}
Command removeEdgeBand(std::string edgeBandId) {
    return removeEntry(std::move(edgeBandId), "edge band", [](Libraries& l) -> auto& { return l.edgeBands; },
                       [](const Refs& r) { return r.edgeBands; });
}
Command removeHardware(std::string hardwareId) {
    return removeEntry(std::move(hardwareId), "hardware definition", [](Libraries& l) -> auto& { return l.hardware; },
                       [](const Refs& r) { return r.hardware; });
}

// ------------------------------------------------------------------ drawings

Command setDrawingEdits(std::string drawingId, std::optional<drawings::DrawingEdits> edits) {
    Command c;
    c.label = label(edits ? "Edit drawing {0}" : "Reset drawing {0}", {drawingId});
    c.apply = [drawingId, edits](Project& p) -> core::Result<Affected> {
        if (drawingId.empty()) return core::makeError(core::ErrorCode::InvalidArgument, "A drawing id must not be empty.");
        if (edits) p.drawingEdits[drawingId] = *edits;
        else p.drawingEdits.erase(drawingId);
        Affected a;
        a.drawings = {drawingId};
        return a;
    };
    return c;
}

Command setElementEdit(std::string drawingId, std::string elementId, std::optional<drawings::ElementEdit> edit) {
    Command c;
    c.label = label(edit ? "Edit {1} in drawing {0}" : "Reset {1} in drawing {0}", {drawingId, elementId});
    c.mergeKey = "element:" + drawingId + ":" + elementId;
    c.apply = [drawingId, elementId, edit](Project& p) -> core::Result<Affected> {
        if (drawingId.empty() || elementId.empty())
            return core::makeError(core::ErrorCode::InvalidArgument, "Drawing and element ids must not be empty.");
        if (edit) {
            p.drawingEdits[drawingId].elements[elementId] = *edit;
        } else if (auto it = p.drawingEdits.find(drawingId); it != p.drawingEdits.end()) {
            it->second.elements.erase(elementId);
            if (it->second.empty()) p.drawingEdits.erase(it);
        }
        Affected a;
        a.drawings = {drawingId};
        return a;
    };
    return c;
}

// ------------------------------------------------------------------ settings

Command setCuttingSettings(std::optional<std::string> materialId, std::optional<CuttingSettings> settings) {
    Command c;
    c.label = label(materialId ? "Change cutting settings of {0}" : "Change default cutting settings",
                    {materialId.value_or("")});
    c.apply = [materialId, settings](Project& p) -> core::Result<Affected> {
        if (!materialId) {
            if (!settings)
                return core::makeError(core::ErrorCode::InvalidArgument, "The default cutting settings cannot be removed.");
            p.cutting.defaults = *settings;
        } else if (settings) {
            if (materialId->empty()) return core::makeError(core::ErrorCode::InvalidArgument, "A material id must not be empty.");
            p.cutting.materials[*materialId] = *settings;
        } else {
            p.cutting.materials.erase(*materialId);
        }
        Affected a;
        a.settings = true;
        return a;
    };
    return c;
}

Command setMetadata(Metadata metadata) {
    Command c;
    c.label = label("Edit project properties", {});
    c.apply = [metadata](Project& p) -> core::Result<Affected> {
        if (metadata.id.empty()) return core::makeError(core::ErrorCode::InvalidArgument, "The project id must not be empty.");
        p.metadata = metadata;
        Affected a;
        a.settings = true;
        return a;
    };
    return c;
}

Command setSettings(ProjectSettings settings) {
    Command c;
    c.label = label("Change project settings", {});
    c.apply = [settings](Project& p) -> core::Result<Affected> {
        if (settings.backupCount < 0)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "The backup count must be 0 or more (is " + std::to_string(settings.backupCount) + ").");
        p.settings = settings;
        Affected a;
        a.settings = true;
        return a;
    };
    return c;
}

Command setCustom(formats::Json custom) {
    Command c;
    c.label = label("Edit custom project data", {});
    c.apply = [custom](Project& p) -> core::Result<Affected> {
        if (!custom.is_object())
            return core::makeError(core::ErrorCode::InvalidArgument, "Custom project data must be a JSON object.");
        p.custom = custom;
        Affected a;
        a.settings = true;
        return a;
    };
    return c;
}

} // namespace commands
} // namespace fos::project
