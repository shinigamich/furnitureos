// SPDX-License-Identifier: AGPL-3.0-only
// fos new / info / set / validate
#include "common.hpp"

#include "fos/core/units.hpp"
#include "fos/furniture/templates.hpp"
#include "fos/project/document.hpp"
#include "fos/validation/validation.hpp"

#include <chrono>
#include <cstdio>
#include <random>

namespace fos::cli::detail {
namespace {

const char* kNewHelp =
    "Usage: fos new <file.fosp> [--name N] [--author A]\n"
    "               [--template base|wall|tall|drawers|wardrobe|shelving [--item-id ID]\n"
    "                [--width W] [--height H] [--depth D]]\n"
    "               [--id UUID] [--date YYYY-MM-DDTHH:MM:SSZ] [--force]\n\n"
    "Creates a project with the generic default libraries (placeholder materials, no prices).\n"
    "With --template one cabinet is added (item id 'cab1' unless --item-id is given); --width,\n"
    "--height and --depth (mm) are applied through undoable document commands.\n"
    "Template defaults (W x H x D): base 600x820x560, wall 600x720x320, tall 600x2100x560,\n"
    "drawers 600x820x560, wardrobe 1000x2200x580, shelving 800x1800x300.\n"
    "--id / --date make the file reproducible (default: random UUID v4, current UTC time).\n"
    "An existing file is not overwritten unless --force is given.\n";

const char* kInfoHelp =
    "Usage: fos info <file.fosp> [--all]\n\n"
    "Prints the project metadata, every item with its envelope, its input parameters (--all:\n"
    "also the formula parameters with their expressions and values), part counts and the\n"
    "generator diagnostics. Exit code 1 when the diagnostics contain errors.\n";

const char* kSetHelp =
    "Usage: fos set <file.fosp> <item-id> <parameter> <value-or-expression>\n\n"
    "Sets a parameter through the project document (the same undoable command the desktop\n"
    "application uses) and saves the file atomically (previous version kept as .bak1).\n"
    "A plain number sets a constant; anything else is stored as an expression, e.g.\n"
    "  fos set kitchen.fosp cab1 width 900\n"
    "  fos set kitchen.fosp cab1 depth \"height / 2\"\n"
    "Exit code 1 when the model rules refuse the value (nothing is saved) or when the value is\n"
    "accepted but the model then has errors (saved; the previous value is printed).\n";

const char* kValidateHelp =
    "Usage: fos validate <file.fosp> [--format text|markdown]\n\n"
    "Runs the generator self-checks and every model validation check (collisions, materials,\n"
    "edges, machining, hardware). Exit code 1 when an error is found, 0 otherwise.\n";

using TemplateFn = furniture::CabinetDefinition (*)(domain::AssemblyId, const furniture::templates::TemplateMaterials&);

std::optional<TemplateFn> templateByName(const std::string& name) {
    namespace t = furniture::templates;
    if (name == "base") return &t::baseCabinet;
    if (name == "wall") return &t::wallCabinet;
    if (name == "tall") return &t::tallCabinet;
    if (name == "drawers") return &t::drawerUnit;
    if (name == "wardrobe") return &t::wardrobeSection;
    if (name == "shelving") return &t::openShelving;
    return std::nullopt;
}

std::string randomUuidV4() {
    std::random_device rd;
    std::uniform_int_distribution<int> byte(0, 255);
    unsigned char b[16];
    for (auto& x : b) x = static_cast<unsigned char>(byte(rd));
    b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x40);
    b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80);
    char buf[37];
    std::snprintf(buf, sizeof buf, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1],
                  b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return buf;
}

std::string utcNow() {
    using namespace std::chrono;
    const auto now = floor<seconds>(system_clock::now());
    const auto day = floor<days>(now);
    const year_month_day ymd{day};
    const hh_mm_ss hms{now - day};
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02uT%02lld:%02lld:%02lldZ", static_cast<int>(ymd.year()),
                  static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()),
                  static_cast<long long>(hms.hours().count()), static_cast<long long>(hms.minutes().count()),
                  static_cast<long long>(hms.seconds().count()));
    return buf;
}

bool looksLikeIsoUtc(const std::string& s) {
    if (s.size() != 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':' || s[19] != 'Z')
        return false;
    for (std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u, 11u, 12u, 14u, 15u, 17u, 18u})
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

const parametric::ParameterGraph* graphOf(const project::Item& item) {
    if (const auto* c = item.cabinet()) return &c->parameters;
    if (const auto* p = item.panel()) return &p->panel.parameters;
    return nullptr;
}

std::size_t partCount(const project::Resolved& r, const std::string& itemId) {
    for (const auto& a : r.model.assemblies)
        if (a.id.str() == itemId) return a.parts.size();
    return 0;
}

std::string valueText(const parametric::ParameterGraph& g, const std::string& name) {
    auto v = g.value(name);
    return v ? core::formatLength(v.value(), 3) : std::string("(error: ") + v.error().message + ")";
}

} // namespace

int cmdNew(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"name"}, {"author"}, {"template"}, {"item-id"}, {"width"}, {"height"}, {"depth"},
                                   {"id"}, {"date"}, {"force", false}});
    if (!parsed) return usageError(io, "new", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kNewHelp;
        return kExitOk;
    }
    if (a.positional.size() != 1) return usageError(io, "new", "expected exactly one file name.");
    const std::filesystem::path path = a.positional[0];
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !a.has("force"))
        return usageError(io, "new", "'" + path.string() + "' already exists (use --force to overwrite).");

    project::Metadata meta;
    meta.id = a.value("id").value_or(randomUuidV4());
    meta.name = a.value("name").value_or(path.stem().string());
    meta.author = a.value("author").value_or("");
    meta.createdAt = a.value("date").value_or(utcNow());
    if (!looksLikeIsoUtc(meta.createdAt))
        return usageError(io, "new", "--date must look like 2026-11-13T09:30:00Z, got '" + meta.createdAt + "'.");
    meta.modifiedAt = meta.createdAt;
    if (meta.id.empty()) return usageError(io, "new", "--id must not be empty.");

    project::Document doc(project::newProject(meta));
    const bool sizeGiven = a.has("width") || a.has("height") || a.has("depth");
    if (!a.has("template") && (sizeGiven || a.has("item-id")))
        return usageError(io, "new", "--width/--height/--depth/--item-id need --template.");
    std::string itemId;
    if (auto tname = a.value("template")) {
        const auto fn = templateByName(*tname);
        if (!fn)
            return usageError(io, "new", "unknown template '" + *tname +
                                             "' (choose base, wall, tall, drawers, wardrobe or shelving).");
        itemId = a.value("item-id").value_or("cab1");
        if (itemId.empty()) return usageError(io, "new", "--item-id must not be empty.");
        project::Item item;
        item.definition = (*fn)(domain::AssemblyId(itemId), project::defaultTemplateMaterials());
        if (auto r = doc.execute(project::commands::addItem(std::move(item))); !r) {
            io.err << "fos new: cannot add the template: " << r.error().message << "\n";
            return kExitValidationErrors;
        }
        for (const char* p : {"width", "height", "depth"}) {
            auto text = a.value(p);
            if (!text) continue;
            const auto v = parseNumber(*text);
            if (!v || *v <= 0) return usageError(io, "new", std::string("--") + p + " must be a positive number in mm, got '" + *text + "'.");
            if (auto r = doc.execute(project::commands::setParameter(itemId, p, *v)); !r) {
                io.err << "fos new: " << p << " = " << *text << " refused: " << r.error().message << "\n";
                return kExitValidationErrors;
            }
        }
    }
    if (auto r = doc.save(path); !r) {
        io.err << "fos new: cannot write '" << path.string() << "': " << r.error().message << "\n";
        return kExitUsageOrIo;
    }
    io.out << "Created " << path.string() << " (project '" << meta.name << "', id " << meta.id << ")\n";
    if (!itemId.empty()) {
        auto resolved = doc.resolve();
        const auto* item = doc.project().findItem(itemId);
        const auto* g = item ? graphOf(*item) : nullptr;
        io.out << "  item " << itemId << ": " << item->name() << ", W " << valueText(*g, "width") << " x H "
               << valueText(*g, "height") << " x D " << valueText(*g, "depth") << " mm, "
               << (resolved ? partCount(resolved.value(), itemId) : 0) << " parts\n";
    }
    return kExitOk;
}

int cmdInfo(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"all", false}});
    if (!parsed) return usageError(io, "info", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kInfoHelp;
        return kExitOk;
    }
    if (a.positional.size() != 1) return usageError(io, "info", "expected exactly one project file.");
    auto loaded = loadProjectOrReport(io, "info", a.positional[0]);
    if (!loaded) return kExitUsageOrIo;
    const project::Project& p = loaded->project;
    const project::Resolved& r = loaded->resolved;
    auto& out = io.out;
    out << "Project: " << p.metadata.name << "\n"
        << "  id: " << p.metadata.id << "\n";
    if (!p.metadata.author.empty()) out << "  author: " << p.metadata.author << "\n";
    out << "  file format version: " << loaded->fileVersion << "\n"
        << "  libraries: " << p.libraries.materials.size() << " materials, " << p.libraries.edgeBands.size()
        << " edge bands, " << p.libraries.hardware.size() << " hardware definitions\n"
        << "Items: " << p.items.size() << "\n";
    std::size_t totalParts = 0;
    for (const auto& item : p.items) {
        const std::size_t parts = partCount(r, item.id());
        totalParts += parts;
        const auto* g = graphOf(item);
        out << "  " << item.id() << "  " << (item.isCabinet() ? "cabinet" : "panel") << "  \"" << item.name() << "\"";
        if (item.isCabinet())
            out << "  W " << valueText(*g, "width") << " x H " << valueText(*g, "height") << " x D "
                << valueText(*g, "depth") << " mm";
        out << "  parts: " << parts << "\n";
        const auto defs = g->definitions();
        std::size_t inputs = 0;
        for (const auto& d : defs) inputs += d.isExpression() ? 0 : 1;
        out << "    parameters: " << defs.size() << " (" << inputs << " inputs, " << defs.size() - inputs
            << " formulas)\n";
        for (const auto& d : defs) {
            if (d.isExpression() && !a.has("all")) continue;
            out << "      " << d.name << " = ";
            if (d.isExpression()) out << d.expression << "  -> " << valueText(*g, d.name);
            else out << core::formatLength(d.value, 3);
            out << "\n";
        }
    }
    out << "Parts: " << totalParts << " in " << r.model.assemblies.size() << " generated assemblies\n";
    if (loaded->diagnostics.empty()) {
        out << "Diagnostics: none\n";
    } else {
        out << "Diagnostics: " << errorCount(loaded->diagnostics) << " errors, " << warningCount(loaded->diagnostics)
            << " warnings\n";
        printDiagnostics(out, loaded->diagnostics);
    }
    return core::hasErrors(loaded->diagnostics) ? kExitValidationErrors : kExitOk;
}

int cmdSet(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {});
    if (!parsed) return usageError(io, "set", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kSetHelp;
        return kExitOk;
    }
    if (a.positional.size() != 4)
        return usageError(io, "set", "expected <file.fosp> <item-id> <parameter> <value-or-expression>.");
    const std::string& file = a.positional[0];
    const std::string& itemId = a.positional[1];
    const std::string& name = a.positional[2];
    const std::string& text = a.positional[3];
    auto loaded = loadProjectOrReport(io, "set", file);
    if (!loaded) return kExitUsageOrIo;
    const auto* item = loaded->project.findItem(itemId);
    if (!item) return usageError(io, "set", "no item '" + itemId + "' in " + file + ".");
    const auto* g = graphOf(*item);
    if (!g->contains(name)) return usageError(io, "set", "item '" + itemId + "' has no parameter '" + name + "'.");
    const std::string before = valueText(*g, name);
    const auto* def = g->find(name);
    const std::string restore = def && def->isExpression() ? "\"" + def->expression + "\"" : before;

    project::Document doc(loaded->project);
    const auto number = parseNumber(text);
    const project::Command cmd = number ? project::commands::setParameter(itemId, name, *number)
                                        : project::commands::setParameterExpression(itemId, name, text);
    if (auto r = doc.execute(cmd); !r) {
        io.err << "fos set: refused: " << r.error().message << "\nNothing was saved.\n";
        return kExitValidationErrors;
    }
    if (!doc.isDirty()) {
        io.out << name << " of " << itemId << " is already " << text << "; file unchanged.\n";
        return kExitOk;
    }
    if (auto r = doc.save(file); !r) {
        io.err << "fos set: cannot save '" << file << "': " << r.error().message << "\n";
        return kExitUsageOrIo;
    }
    const auto* after = doc.project().findItem(itemId);
    io.out << name << " of " << itemId << ": " << before << " -> " << valueText(*graphOf(*after), name) << "\n"
           << "Saved " << file << "\n";
    auto resolved = doc.resolve();
    if (resolved && !resolved.value().diagnostics.empty()) {
        io.out << "Diagnostics after the change:\n";
        printDiagnostics(io.out, resolved.value().diagnostics);
    }
    if (resolved && core::hasErrors(resolved.value().diagnostics)) {
        // The document accepted the edit (the item still generates), but the model now has
        // errors; the file is saved so the user can continue editing, and the exit code says so.
        io.err << "fos set: saved, but the model now has " << errorCount(resolved.value().diagnostics)
               << " error(s); 'fos set " << file << " " << itemId << " " << name << " " << restore
               << "' restores the previous value.\n";
        return kExitValidationErrors;
    }
    return kExitOk;
}

int cmdValidate(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"format"}});
    if (!parsed) return usageError(io, "validate", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kValidateHelp;
        return kExitOk;
    }
    if (a.positional.size() != 1) return usageError(io, "validate", "expected exactly one project file.");
    const std::string format = a.value("format").value_or("text");
    if (format != "text" && format != "markdown")
        return usageError(io, "validate", "--format must be 'text' or 'markdown', got '" + format + "'.");
    auto loaded = loadProjectOrReport(io, "validate", a.positional[0]);
    if (!loaded) return kExitUsageOrIo;
    const auto report = validation::validateModel(loaded->resolved.model);
    const core::Diagnostics& gen = loaded->diagnostics;
    if (format == "text") {
        io.out << "Generator diagnostics: " << (gen.empty() ? "none" : std::to_string(gen.size())) << "\n";
        printDiagnostics(io.out, gen);
        io.out << validation::toText(report);
    } else {
        io.out << "# Validation of " << loaded->project.metadata.name << "\n\n## Generator diagnostics\n\n";
        if (gen.empty()) io.out << "None.\n";
        for (const auto& d : gen)
            io.out << "- **" << severityText(d.severity) << "** `" << d.code << "`"
                   << (d.entityId.empty() ? "" : " (" + d.entityId + ")") << ": " << d.message << "\n";
        io.out << "\n" << validation::toMarkdown(report);
    }
    return (core::hasErrors(gen) || !report.ok()) ? kExitValidationErrors : kExitOk;
}

} // namespace fos::cli::detail
