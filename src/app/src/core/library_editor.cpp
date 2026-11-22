// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/library_editor.hpp"

#include "fos/app/session.hpp"
#include "fos/app/version.hpp"
#include "fos/project/components.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>

namespace fos::app {
namespace {

template <class T> std::vector<std::string> idsOf(const std::vector<T>& list) {
    std::vector<std::string> out;
    out.reserve(list.size());
    for (const auto& e : list) out.push_back(e.id.str());
    return out;
}

const std::vector<std::string>& refsOf(const project::LibraryRefs& refs, LibraryCategory category) {
    switch (category) {
    case LibraryCategory::Material: return refs.materials;
    case LibraryCategory::EdgeBand: return refs.edgeBands;
    case LibraryCategory::Hardware: return refs.hardware;
    }
    return refs.materials;
}

template <class T> void addRows(const project::Project& project, const std::vector<T>& list, LibraryCategory category,
                                std::vector<LibraryRow>& out) {
    std::vector<project::LibraryRefs> refs;
    refs.reserve(project.items.size());
    for (const auto& item : project.items) refs.push_back(project::libraryReferences(item));
    for (const auto& e : list) {
        LibraryRow row{e.id.str(), e.name, {}};
        for (std::size_t i = 0; i < project.items.size(); ++i) {
            const auto& ids = refsOf(refs[i], category);
            if (std::find(ids.begin(), ids.end(), row.id) != ids.end()) row.usedBy.push_back(project.items[i].id());
        }
        out.push_back(std::move(row));
    }
}

QString num(double v) { return QString::number(v, 'g', 10); }

/// Common checks: id (new entries only) and name.
void checkIdAndName(QStringList& out, const std::vector<std::string>& taken, const std::string& id, const std::string& name,
                    bool isNew) {
    if (isNew) {
        const QString q = QString::fromStdString(id);
        if (id.empty())
            out << LibraryText::tr("The id must not be empty.");
        else if (q.trimmed() != q)
            out << LibraryText::tr("The id \"%1\" must not start or end with a space.").arg(q);
        else if (std::find(taken.begin(), taken.end(), id) != taken.end())
            out << LibraryText::tr("The id \"%1\" is already used in this library.").arg(q);
    }
    if (QString::fromStdString(name).trimmed().isEmpty()) out << LibraryText::tr("The name must not be empty.");
}

void checkPositive(QStringList& out, const QString& field, double v) {
    if (!std::isfinite(v) || v <= 0)
        out << LibraryText::tr("%1 must be greater than 0 mm, got %2.").arg(field, num(v));
}

void checkCost(QStringList& out, const QString& field, double v) {
    if (!std::isfinite(v) || v < 0) out << LibraryText::tr("%1 must be 0 or more, got %2.").arg(field, num(v));
}

QStringList execute(Session& session, const project::Command& command) {
    if (auto r = session.execute(command); !r) return {QString::fromStdString(r.error().message)};
    return {};
}

std::string copyId(const std::vector<std::string>& taken, const std::string& id, const char* suffix) {
    std::string candidate = id + suffix;
    for (int n = 2; std::find(taken.begin(), taken.end(), candidate) != taken.end(); ++n)
        candidate = id + suffix + std::to_string(n);
    return candidate;
}

} // namespace

std::vector<LibraryRow> libraryRows(const project::Project& project, LibraryCategory category) {
    std::vector<LibraryRow> out;
    const auto& lib = project.libraries;
    switch (category) {
    case LibraryCategory::Material: addRows(project, lib.materials, category, out); break;
    case LibraryCategory::EdgeBand: addRows(project, lib.edgeBands, category, out); break;
    case LibraryCategory::Hardware: addRows(project, lib.hardware, category, out); break;
    }
    return out;
}

HardwareInput hardwareInput(const domain::HardwareDefinition& d) {
    return {d.name, d.manufacturer, d.article, d.unitCost};
}

domain::HardwareDefinition applyHardwareInput(domain::HardwareDefinition definition, const HardwareInput& input) {
    definition.name = input.name;
    definition.manufacturer = input.manufacturer;
    definition.article = input.code;
    definition.unitCost = input.unitCost;
    return definition;
}

std::string colorText(const domain::Color& c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c.r, c.g, c.b);
    return buf;
}

std::optional<domain::Color> parseColorText(const std::string& text) {
    std::string s = text;
    if (!s.empty() && s.front() == '#') s.erase(0, 1);
    if (s.size() != 6) return std::nullopt;
    int v[3] = {};
    for (int i = 0; i < 3; ++i) {
        int byte = 0;
        for (int k = 0; k < 2; ++k) {
            const char ch = s[static_cast<std::size_t>(2 * i + k)];
            int d = 0;
            if (ch >= '0' && ch <= '9') d = ch - '0';
            else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
            else return std::nullopt;
            byte = byte * 16 + d;
        }
        v[i] = byte;
    }
    return domain::Color{static_cast<std::uint8_t>(v[0]), static_cast<std::uint8_t>(v[1]), static_cast<std::uint8_t>(v[2])};
}

std::string uniqueLibraryId(const std::string& prefix, const std::vector<std::string>& taken) {
    for (int n = 1;; ++n) {
        std::string id = prefix + std::to_string(n);
        if (std::find(taken.begin(), taken.end(), id) == taken.end()) return id;
    }
}

std::vector<std::string> libraryIds(const project::Project& project, LibraryCategory category) {
    switch (category) {
    case LibraryCategory::Material: return idsOf(project.libraries.materials);
    case LibraryCategory::EdgeBand: return idsOf(project.libraries.edgeBands);
    case LibraryCategory::Hardware: return idsOf(project.libraries.hardware);
    }
    return {};
}

domain::Material newMaterial(const project::Project& project) {
    domain::Material m;
    m.id = domain::MaterialId(uniqueLibraryId("material", libraryIds(project, LibraryCategory::Material)));
    m.name = LibraryText::newMaterialName().toStdString();
    return m;
}

domain::EdgeBand newEdgeBand(const project::Project& project) {
    domain::EdgeBand e;
    e.id = domain::EdgeBandId(uniqueLibraryId("edge", libraryIds(project, LibraryCategory::EdgeBand)));
    e.name = LibraryText::newEdgeBandName().toStdString();
    return e;
}

domain::Material duplicateMaterial(const project::Project& project, const domain::Material& source) {
    domain::Material m = source;
    m.id = domain::MaterialId(copyId(libraryIds(project, LibraryCategory::Material), source.id.str(), "-copy"));
    m.name = LibraryText::copyName(source.name).toStdString();
    return m;
}

domain::EdgeBand duplicateEdgeBand(const project::Project& project, const domain::EdgeBand& source) {
    domain::EdgeBand e = source;
    e.id = domain::EdgeBandId(copyId(libraryIds(project, LibraryCategory::EdgeBand), source.id.str(), "-copy"));
    e.name = LibraryText::copyName(source.name).toStdString();
    return e;
}

domain::HardwareDefinition duplicateHardware(const project::Project& project, const domain::HardwareDefinition& source) {
    domain::HardwareDefinition h = source;
    h.id = domain::HardwareId(copyId(libraryIds(project, LibraryCategory::Hardware), source.id.str(), "-copy"));
    h.name = LibraryText::copyName(source.name).toStdString();
    return h;
}

QStringList validateMaterial(const project::Project& project, const domain::Material& m, bool isNew) {
    QStringList out;
    checkIdAndName(out, libraryIds(project, LibraryCategory::Material), m.id.str(), m.name, isNew);
    checkPositive(out, LibraryText::tr("Thickness"), m.thickness);
    checkPositive(out, LibraryText::tr("Sheet length"), m.sheetLength);
    checkPositive(out, LibraryText::tr("Sheet width"), m.sheetWidth);
    checkCost(out, LibraryText::tr("Cost per sheet"), m.costPerSheet);
    checkCost(out, LibraryText::tr("Cost per m²"), m.costPerSquareMeter);
    if (!std::isfinite(m.density) || m.density < 0)
        out << LibraryText::tr("Density must be 0 (unknown) or more kg/m³, got %1.").arg(num(m.density));
    return out;
}

QStringList validateEdgeBand(const project::Project& project, const domain::EdgeBand& e, bool isNew) {
    QStringList out;
    checkIdAndName(out, libraryIds(project, LibraryCategory::EdgeBand), e.id.str(), e.name, isNew);
    checkPositive(out, LibraryText::tr("Thickness"), e.thickness);
    checkPositive(out, LibraryText::tr("Width"), e.width);
    if (std::isfinite(e.thickness) && std::isfinite(e.width) && e.thickness > 0 && e.width > 0 && e.width < e.thickness)
        out << LibraryText::tr("The tape width (%1 mm) must not be smaller than its thickness (%2 mm).")
                   .arg(num(e.width), num(e.thickness));
    checkCost(out, LibraryText::tr("Cost per metre"), e.costPerMeter);
    return out;
}

QStringList validateHardware(const project::Project& project, const domain::HardwareDefinition& h, bool isNew) {
    QStringList out;
    checkIdAndName(out, libraryIds(project, LibraryCategory::Hardware), h.id.str(), h.name, isNew);
    checkCost(out, LibraryText::tr("Unit cost"), h.unitCost);
    return out;
}

LibraryEditResult applyMaterial(Session& session, const domain::Material& material, bool isNew) {
    if (auto problems = validateMaterial(session.project(), material, isNew); !problems.isEmpty()) return problems;
    return execute(session, project::commands::upsertMaterial(material));
}

LibraryEditResult applyEdgeBand(Session& session, const domain::EdgeBand& edgeBand, bool isNew) {
    if (auto problems = validateEdgeBand(session.project(), edgeBand, isNew); !problems.isEmpty()) return problems;
    return execute(session, project::commands::upsertEdgeBand(edgeBand));
}

LibraryEditResult applyHardware(Session& session, const domain::HardwareDefinition& definition, bool isNew) {
    if (auto problems = validateHardware(session.project(), definition, isNew); !problems.isEmpty()) return problems;
    return execute(session, project::commands::upsertHardware(definition));
}

LibraryEditResult removeLibraryEntry(Session& session, LibraryCategory category, const std::string& id) {
    switch (category) {
    case LibraryCategory::Material: return execute(session, project::commands::removeMaterial(id));
    case LibraryCategory::EdgeBand: return execute(session, project::commands::removeEdgeBand(id));
    case LibraryCategory::Hardware: return execute(session, project::commands::removeHardware(id));
    }
    return {};
}

formats::Library projectLibrary(const project::Project& project) {
    formats::Library lib;
    lib.materials = project.libraries.materials;
    lib.edgeBands = project.libraries.edgeBands;
    lib.hardware = project.libraries.hardware;
    return lib;
}

core::Result<void> exportProjectLibrary(const project::Project& project, const std::filesystem::path& path) {
    formats::AppInfo info;
    info.name = kApplicationName;
    info.version = applicationVersion();
    if (auto r = formats::exportLibrary(path, projectLibrary(project), info); !r) return r.error();
    return {};
}

namespace {

/// Renamed entries get "<id>-imported" (then -imported2, ...): mergeLibrary renames exactly the
/// incoming entries whose id exists in the same target list, in the order materials, edge bands,
/// hardware, and asks the generator once per such entry (a candidate is never already in use
/// because it avoids every id of both libraries and every earlier candidate).
formats::IdGenerator renameGenerator(const formats::Library& target, const formats::Library& incoming) {
    auto queue = std::make_shared<std::vector<std::string>>();
    auto used = std::make_shared<std::set<std::string>>();
    const auto collect = [&](const auto& targetList, const auto& incomingList) {
        for (const auto& e : targetList) used->insert(e.id.str());
        for (const auto& e : incomingList) {
            used->insert(e.id.str());
            if (std::any_of(targetList.begin(), targetList.end(), [&](const auto& t) { return t.id == e.id; }))
                queue->push_back(e.id.str());
        }
    };
    collect(target.materials, incoming.materials);
    collect(target.edgeBands, incoming.edgeBands);
    collect(target.hardware, incoming.hardware);
    auto next = std::make_shared<std::size_t>(0);
    return [queue, used, next]() {
        const std::string base = (*next < queue->size() ? (*queue)[*next] : std::string("entry")) + "-imported";
        ++*next;
        std::string candidate = base;
        for (int n = 2; used->count(candidate) != 0; ++n) candidate = base + std::to_string(n);
        used->insert(candidate);
        return candidate;
    };
}

template <class T> const T* findById(const std::vector<T>& list, const std::string& id) {
    const auto it = std::find_if(list.begin(), list.end(), [&](const T& e) { return e.id.str() == id; });
    return it == list.end() ? nullptr : &*it;
}

} // namespace

core::Result<formats::MergeReport> previewLibraryImport(const project::Project& project, const formats::Library& incoming,
                                                        formats::ConflictPolicy policy) {
    formats::Library target = projectLibrary(project);
    return formats::mergeLibrary(target, incoming, policy, renameGenerator(target, incoming));
}

project::Command importLibraryCommand(formats::Library incoming, formats::ConflictPolicy policy, std::string sourceName) {
    project::Command c;
    c.label = {"Import library {0}", {std::move(sourceName)}};
    c.apply = [incoming = std::move(incoming), policy](project::Project& p) -> core::Result<project::Affected> {
        formats::Library merged = projectLibrary(p);
        auto report = formats::mergeLibrary(merged, incoming, policy, renameGenerator(merged, incoming));
        if (!report) return report.error();
        project::Affected affected;
        for (const auto& entry : report.value().entries) {
            if (entry.action == formats::MergeAction::Skipped) continue;
            std::optional<project::Command> upsert;
            switch (entry.category) {
            case LibraryCategory::Material:
                if (const auto* e = findById(merged.materials, entry.resultId)) upsert = project::commands::upsertMaterial(*e);
                break;
            case LibraryCategory::EdgeBand:
                if (const auto* e = findById(merged.edgeBands, entry.resultId)) upsert = project::commands::upsertEdgeBand(*e);
                break;
            case LibraryCategory::Hardware:
                if (const auto* e = findById(merged.hardware, entry.resultId)) upsert = project::commands::upsertHardware(*e);
                break;
            }
            if (!upsert)
                return core::makeError(core::ErrorCode::Internal,
                                       "Merged library entry \"" + entry.resultId + "\" was not found.", entry.resultId);
            auto r = upsert->apply(p);
            if (!r) return r.error();
            affected = std::move(r.value());
        }
        return affected;
    };
    return c;
}

} // namespace fos::app
