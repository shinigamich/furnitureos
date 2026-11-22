// SPDX-License-Identifier: AGPL-3.0-only
// `fos component save|list|insert`: reusable components (.foscomp files).
#include "common.hpp"

#include "fos/cli/cli.hpp"
#include "fos/project/components.hpp"
#include "fos/project/document.hpp"

#include <sstream>

namespace fos::cli::detail {
namespace {

const char* kComponentHelp =
    "Usage: fos component save <project.fosp> <item-id> <out.foscomp> --name N\n"
    "                          [--category C] [--tags a,b] [--description D] [--author A]\n"
    "                          [--id ID] [--force]\n"
    "       fos component list <dir|file.foscomp>...\n"
    "       fos component insert <project.fosp> <file.foscomp> <component-name-or-id>\n"
    "                          [--id ITEM-ID] [--at x,y,z] [--on-conflict keep|replace|rename]\n\n"
    "save    captures one item (cabinet or panel) with copies of the materials, edge bands and\n"
    "        hardware it references, so the component is self-contained. --id defaults to\n"
    "        'comp.<slug of the name>'. An existing output file is replaced only with --force.\n"
    "list    prints every component of the files (directories are searched recursively),\n"
    "        sorted by category and name; unreadable files are reported (exit 1).\n"
    "insert  adds the component to the project as a new item (default id: the first free of\n"
    "        'comp1', 'comp2', ...) at --at (mm, default 0,0,0) and saves the project atomically.\n"
    "        Embedded library entries whose id already exists in the project and differ from it:\n"
    "        keep (default) uses the project's entry, replace overwrites it, rename adds the\n"
    "        component's entry under '<item-id>.libN' and points the new item at it. Identical\n"
    "        entries are never duplicated.\n";

std::vector<std::string> splitList(const std::string& text, char sep) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream in(text);
    while (std::getline(in, cur, sep))
        if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string number(double v) {
    std::ostringstream s;
    s << v;
    return s.str();
}

int componentSave(const ParsedArgs& a, Io& io) {
    if (a.positional.size() != 4) return usageError(io, "component", "save expects <project.fosp> <item-id> <out.foscomp>.");
    const auto name = a.value("name");
    if (!name || name->empty()) return usageError(io, "component", "save needs --name.");
    const std::string& file = a.positional[1];
    const std::string& itemId = a.positional[2];
    const std::string& out = a.positional[3];
    std::error_code ec;
    if (!a.has("force") && std::filesystem::exists(pathFromUtf8(out), ec))
        return usageError(io, "component", "'" + out + "' exists; use --force to replace it.");
    auto loaded = loadProjectOrReport(io, "component", file);
    if (!loaded) return kExitUsageOrIo;
    formats::ComponentMeta meta;
    meta.name = *name;
    meta.id = a.value("id").value_or("");
    meta.category = a.value("category").value_or("");
    meta.description = a.value("description").value_or("");
    meta.author = a.value("author").value_or(loaded->project.metadata.author);
    if (auto tags = a.value("tags")) meta.tags = splitList(*tags, ',');
    auto component = project::makeComponent(loaded->project, itemId, meta);
    if (!component) {
        io.err << "fos component: " << component.error().message << "\n";
        return kExitUsageOrIo;
    }
    formats::SaveOptions options;
    options.backupCount = 0;
    if (auto r = project::saveComponents(pathFromUtf8(out), {component.value()}, options); !r) {
        io.err << "fos component: " << r.error().message << "\n";
        return kExitUsageOrIo;
    }
    const auto& c = component.value();
    io.out << "Saved component '" << c.meta.name << "' (" << c.meta.id << ") to " << out << ": "
           << c.library.materials.size() << " material(s), " << c.library.edgeBands.size() << " edge band(s), "
           << c.library.hardware.size() << " hardware definition(s) embedded.\n";
    return kExitOk;
}

int componentList(const ParsedArgs& a, Io& io) {
    if (a.positional.size() < 2) return usageError(io, "component", "list expects at least one directory or file.");
    std::vector<std::filesystem::path> paths;
    for (std::size_t i = 1; i < a.positional.size(); ++i) paths.push_back(pathFromUtf8(a.positional[i]));
    project::ComponentLibrary library;
    const auto diagnostics = library.scan(paths);
    std::string category = "\x01";
    for (const auto& e : library.entries()) {
        const auto& m = e.component.meta;
        if (m.category != category) {
            category = m.category;
            io.out << (category.empty() ? std::string("(uncategorised)") : category) << "\n";
        }
        io.out << "  " << m.name << "  [" << m.id << "]  " << (e.component.item.isCabinet() ? "cabinet" : "panel") << "  "
               << number(m.previewSize.width) << " x " << number(m.previewSize.height) << " x "
               << number(m.previewSize.depth) << " mm";
        if (!m.tags.empty()) {
            io.out << "  tags:";
            for (const auto& t : m.tags) io.out << " " << t;
        }
        io.out << "  (" << e.source << ")\n";
    }
    io.out << library.entries().size() << " component(s).\n";
    if (!diagnostics.empty()) printDiagnostics(io.err, diagnostics);
    return core::hasErrors(diagnostics) ? kExitValidationErrors : kExitOk;
}

int componentInsert(const ParsedArgs& a, Io& io) {
    if (a.positional.size() != 4)
        return usageError(io, "component", "insert expects <project.fosp> <file.foscomp> <component-name-or-id>.");
    const std::string& file = a.positional[1];
    const std::string& compFile = a.positional[2];
    const std::string& which = a.positional[3];
    formats::ConflictPolicy policy = formats::ConflictPolicy::Skip;
    if (auto p = a.value("on-conflict")) {
        if (*p == "keep") policy = formats::ConflictPolicy::Skip;
        else if (*p == "replace") policy = formats::ConflictPolicy::Replace;
        else if (*p == "rename") policy = formats::ConflictPolicy::RenameWithNewId;
        else return usageError(io, "component", "--on-conflict must be keep, replace or rename, not '" + *p + "'.");
    }
    geometry::Vec3 at{0, 0, 0};
    if (auto text = a.value("at")) {
        const auto parts = splitList(*text, ',');
        std::optional<double> v[3];
        if (parts.size() == 3)
            for (int i = 0; i < 3; ++i) v[i] = parseNumber(parts[static_cast<std::size_t>(i)]);
        if (parts.size() != 3 || !v[0] || !v[1] || !v[2])
            return usageError(io, "component", "--at expects three numbers x,y,z in mm, got '" + *text + "'.");
        at = {*v[0], *v[1], *v[2]};
    }
    auto loaded = loadProjectOrReport(io, "component", file);
    if (!loaded) return kExitUsageOrIo;
    auto components = project::loadComponents(pathFromUtf8(compFile));
    if (!components) {
        io.err << "fos component: " << components.error().message << "\n";
        return kExitUsageOrIo;
    }
    const project::Component* c = project::findComponent(components.value(), which);
    if (c == nullptr) {
        std::string names;
        for (const auto& x : components.value()) names += (names.empty() ? "'" : ", '") + x.meta.name + "'";
        return usageError(io, "component", "no component '" + which + "' in " + compFile + " (it has " + names + ").");
    }
    std::string itemId = a.value("id").value_or("");
    if (itemId.empty())
        for (int n = 1; itemId.empty() || loaded->project.findItem(itemId) != nullptr; ++n) itemId = "comp" + std::to_string(n);
    auto plan = project::planComponentInsert(loaded->project, *c, policy, project::componentIdGenerator(itemId));
    project::Document doc(loaded->project);
    if (auto r = doc.execute(project::insertComponent(*c, itemId, geometry::Transform::translation(at), policy)); !r) {
        io.err << "fos component: refused: " << r.error().message << "\nNothing was saved.\n";
        return kExitValidationErrors;
    }
    if (auto r = doc.save(pathFromUtf8(file)); !r) {
        io.err << "fos component: cannot save '" << file << "': " << r.error().message << "\n";
        return kExitUsageOrIo;
    }
    io.out << "Inserted '" << c->meta.name << "' as item " << itemId << " at " << number(at.x) << "," << number(at.y)
           << "," << number(at.z) << "\n";
    if (plan)
        for (const auto& e : plan.value().entries)
            if (e.action != formats::MergeAction::Skipped || e.incomingId != e.resultId)
                io.out << "  " << formats::toString(e.category) << " " << e.incomingId << ": "
                       << formats::toString(e.action)
                       << (e.action == formats::MergeAction::Renamed ? " to " + e.resultId : std::string()) << "\n";
    io.out << "Saved " << file << "\n";
    return kExitOk;
}

} // namespace

int cmdComponent(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"name"}, {"category"}, {"tags"}, {"description"}, {"author"}, {"id"}, {"at"},
                                   {"on-conflict"}, {"force", false}});
    if (!parsed) return usageError(io, "component", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kComponentHelp;
        return kExitOk;
    }
    if (a.positional.empty()) return usageError(io, "component", "expected save, list or insert.");
    const std::string& sub = a.positional[0];
    if (sub == "save") return componentSave(a, io);
    if (sub == "list") return componentList(a, io);
    if (sub == "insert") return componentInsert(a, io);
    return usageError(io, "component", "unknown subcommand '" + sub + "' (expected save, list or insert).");
}

} // namespace fos::cli::detail
