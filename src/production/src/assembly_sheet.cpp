// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/assembly_sheet.hpp"

#include "collect.hpp"
#include "fos/core/units.hpp"
#include "fos/production/html.hpp"

#include <algorithm>
#include <map>

namespace fos::production {

const char* const kAssemblyOrderNote =
    "Generic step order derived from part roles and hardware kinds (carcass, back, shelves/dividers, drawer "
    "boxes, fronts, handles); not a verified assembly instruction for this design.";

namespace {

using K = AssemblyStepKind;
constexpr std::size_t kStepCount = 7;

const char* instructionFor(K k) noexcept {
    switch (k) {
    case K::Carcass:
        return "Join the sides, bottom and top (and rails / plinth) with the listed fasteners; fit feet or legs. "
               "Check that the carcass is square (equal diagonals).";
    case K::Back: return "Fit the back panel; it holds the carcass square, so check the diagonals first.";
    case K::ShelvesAndDividers: return "Insert the shelf supports, then fit the shelves and dividers.";
    case K::DrawerBoxes: return "Assemble the drawer boxes and mount the drawer slides.";
    case K::Fronts: return "Mount hinges / fittings, hang the doors and drawer fronts and adjust the gaps.";
    case K::Handles: return "Fit the handles.";
    case K::Other: return "Fit the remaining parts and hardware.";
    }
    return "";
}

/// Kind-based step of a hardware instance; nullopt = decided by the parts it connects.
std::optional<K> stepForHardwareKind(domain::HardwareKind kind) noexcept {
    using H = domain::HardwareKind;
    switch (kind) {
    case H::Foot:
    case H::Leg: return K::Carcass;
    case H::ShelfSupport: return K::ShelvesAndDividers;
    case H::DrawerSlide: return K::DrawerBoxes;
    case H::Hinge:
    case H::MountingPlate:
    case H::LiftMechanism:
    case H::SlidingSystem: return K::Fronts;
    case H::Handle: return K::Handles;
    case H::Suspension: return K::Other;
    case H::Confirmat:
    case H::Dowel:
    case H::CamConnector:
    case H::Screw:
    case H::Other: return std::nullopt;
    }
    return std::nullopt;
}

std::string n(double v) { return core::formatLength(v, 3); }

/// Greedy word wrap at `maxChars` bytes (no font metrics are available; the limits used below
/// leave room for wide glyphs at the chosen font sizes).
std::vector<std::string> wrap(const std::string& text, std::size_t maxChars) {
    std::vector<std::string> out;
    std::string line;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find(' ', pos);
        if (end == std::string::npos) end = text.size();
        const std::string word = text.substr(pos, end - pos);
        if (!line.empty() && line.size() + 1 + word.size() > maxChars) {
            out.push_back(line);
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
        pos = end + 1;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

} // namespace

const char* toString(AssemblyStepKind k) noexcept {
    switch (k) {
    case K::Carcass: return "Carcass";
    case K::Back: return "Back panel";
    case K::ShelvesAndDividers: return "Shelves and dividers";
    case K::DrawerBoxes: return "Drawer boxes";
    case K::Fronts: return "Doors and drawer fronts";
    case K::Handles: return "Handles";
    case K::Other: return "Other parts";
    }
    return "?";
}

AssemblyStepKind stepForRole(domain::PartRole role) noexcept {
    using R = domain::PartRole;
    switch (role) {
    case R::Side:
    case R::Top:
    case R::Bottom:
    case R::Rail:
    case R::Plinth: return K::Carcass;
    case R::Back: return K::Back;
    case R::Shelf:
    case R::Partition: return K::ShelvesAndDividers;
    case R::DrawerSide:
    case R::DrawerBack:
    case R::DrawerBottom: return K::DrawerBoxes;
    case R::Door:
    case R::DrawerFront: return K::Fronts;
    case R::Worktop:
    case R::Generic: return K::Other;
    }
    return K::Other;
}

AssemblySheets assemblySheets(const domain::Model& model, const Scope& scope) {
    auto c = detail::collect(model, scope);
    AssemblySheets out;
    out.diagnostics = std::move(c.diagnostics);
    for (const auto* a : c.assemblies) {
        std::array<AssemblyStep, kStepCount> steps;
        for (std::size_t i = 0; i < kStepCount; ++i) steps[i].kind = static_cast<K>(i);
        for (const auto& ref : c.parts) {
            if (ref.assembly != a) continue;
            const auto k = static_cast<std::size_t>(stepForRole(ref.part->role));
            steps[k].parts.push_back({ref.number, ref.part->id, ref.part->name, ref.part->role});
        }
        // Hardware quantities per step and definition.
        std::array<std::map<domain::HardwareId, int>, kStepCount> qty;
        for (const auto& h : a->hardware) {
            if (h.quantity <= 0) {
                detail::warn(out.diagnostics, "production.hardware.invalid_quantity",
                             "Hardware instance '" + h.id.str() + "' in assembly '" + a->name + "' has quantity " +
                                 std::to_string(h.quantity) + "; it is not in the assembly sheet.",
                             h.id.str());
                continue;
            }
            const auto* def = model.findHardware(h.definition);
            if (!def)
                detail::warn(out.diagnostics, "production.hardware.unknown_definition",
                             "Hardware instance '" + h.id.str() + "' in assembly '" + a->name +
                                 "' references definition '" + h.definition.str() +
                                 "' which is not in the hardware catalogue; it is listed by its id.",
                             h.id.str());
            std::optional<K> k = def ? stepForHardwareKind(def->kind) : std::nullopt;
            if (!k) {
                for (const auto& pid : h.parts)
                    if (const auto* p = a->findPart(pid)) {
                        const K pk = stepForRole(p->role);
                        if (!k || pk > *k) k = pk;
                    }
            }
            qty[static_cast<std::size_t>(k.value_or(K::Other))][h.definition] += h.quantity;
        }
        for (std::size_t i = 0; i < kStepCount; ++i) {
            auto& m = qty[i];
            for (const auto& def : model.hardwareCatalog) {
                auto it = m.find(def.id);
                if (it == m.end()) continue;
                steps[i].hardware.push_back({def.id, def.name, def.kind, false, it->second});
                m.erase(it);
            }
            for (const auto& [id, q] : m) steps[i].hardware.push_back({id, id.str(), domain::HardwareKind::Other, true, q});
        }
        AssemblySheet sheet;
        sheet.assemblyId = a->id;
        sheet.assemblyName = a->name;
        for (auto& s : steps) {
            if (s.parts.empty() && s.hardware.empty()) continue;
            s.number = static_cast<int>(sheet.steps.size() + 1);
            s.title = toString(s.kind);
            s.instruction = instructionFor(s.kind);
            sheet.steps.push_back(std::move(s));
        }
        if (!sheet.steps.empty()) out.sheets.push_back(std::move(sheet));
    }
    return out;
}

namespace {

/// Lines of a sheet shared by the text and SVG renderers: {indent level, text}.
std::vector<std::pair<int, std::string>> sheetLines(const AssemblySheet& sheet) {
    std::vector<std::pair<int, std::string>> lines;
    for (const auto& s : sheet.steps) {
        lines.push_back({0, "Step " + std::to_string(s.number) + ": " + s.title});
        lines.push_back({1, s.instruction});
        if (!s.parts.empty()) lines.push_back({1, "Parts:"});
        for (const auto& p : s.parts) lines.push_back({2, "#" + std::to_string(p.partNumber) + "  " + p.name});
        if (!s.hardware.empty()) lines.push_back({1, "Hardware:"});
        for (const auto& h : s.hardware)
            lines.push_back({2, std::to_string(h.quantity) + " x " + h.name +
                                    (h.unknownDefinition ? " [unknown definition]" : "")});
    }
    return lines;
}

} // namespace

std::string assemblySheetText(const AssemblySheet& sheet) {
    std::string s = "Assembly sheet: " + sheet.assemblyName + " (" + sheet.assemblyId.str() + ")\n";
    s += std::string("Note: ") + kAssemblyOrderNote + "\n";
    for (const auto& [indent, line] : sheetLines(sheet)) {
        if (indent == 0) s += "\n";
        s += std::string(static_cast<std::size_t>(indent) * 2, ' ') + line + "\n";
    }
    return s;
}

std::string assemblySheetSvg(const AssemblySheet& sheet, const std::string& projectName) {
    const double margin = 15.0, lineH = 5.5, width = 210.0;
    const auto lines = sheetLines(sheet);
    double y = margin + 30.0;
    std::string body;
    auto text = [&body](double x, double yy, double size, const std::string& t, const char* extra = "") {
        body += "<text x=\"" + n(x) + "\" y=\"" + n(yy) + "\" font-size=\"" + n(size) + "\"" + extra + ">" +
                escapeXml(t) + "</text>\n";
    };
    text(margin, margin + 6.0, 6.0, "Assembly sheet: " + sheet.assemblyName, " font-weight=\"bold\"");
    text(margin, margin + 13.0, 3.5, (projectName.empty() ? "" : projectName + " \xC2\xB7 ") + sheet.assemblyId.str());
    const auto note = wrap(kAssemblyOrderNote, 110);
    for (std::size_t i = 0; i < note.size(); ++i)
        text(margin, margin + 20.0 + 3.6 * static_cast<double>(i), 2.6, note[i], " font-style=\"italic\"");
    y += 3.6 * static_cast<double>(note.size());
    for (const auto& [indent, rawLine] : lines) {
        if (indent == 1 && rawLine.size() > 90) {
            for (const auto& part : wrap(rawLine, 90)) {
                text(margin + 6.0, y, 3.2, part);
                y += lineH;
            }
            continue;
        }
        const std::string& line = rawLine;
        if (indent == 0) {
            y += 3.0;
            body += "<line x1=\"" + n(margin) + "\" y1=\"" + n(y - 5.0) + "\" x2=\"" + n(width - margin) + "\" y2=\"" +
                    n(y - 5.0) + "\" stroke=\"#999\" stroke-width=\"0.2\"/>\n";
            text(margin, y, 4.5, line, " font-weight=\"bold\"");
        } else {
            text(margin + 6.0 * indent, y, indent == 1 ? 3.2 : 3.4, line);
        }
        y += lineH;
    }
    const double height = std::max(297.0, y + margin);
    std::string svg = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"" + n(width) + "mm\" height=\"" +
           n(height) + "mm\" viewBox=\"0 0 " + n(width) + " " + n(height) +
           "\" font-family=\"DejaVu Sans, Arial, Helvetica, sans-serif\" fill=\"#000\">\n";
    svg += "<rect width=\"" + n(width) + "\" height=\"" + n(height) + "\" fill=\"#fff\"/>\n";
    svg += body + "</svg>\n";
    return svg;
}

} // namespace fos::production
