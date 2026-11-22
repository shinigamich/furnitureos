// SPDX-License-Identifier: AGPL-3.0-only
// Text, table and SVG label renderings of production::PackingList.
#include "fos/core/units.hpp"
#include "fos/production/html.hpp"
#include "fos/production/packing.hpp"

namespace fos::production {
namespace {

std::string mm(double v) { return core::formatLength(v, 1); }
std::string size3(double l, double w, double h) { return mm(l) + " x " + mm(w) + " x " + mm(h); }
std::string kgCell(const std::optional<double>& kg) { return kg ? core::formatLength(*kg, 2) : "unknown"; }

std::string flags(const Package& p) {
    std::string s;
    if (p.overweight) s += "  OVERWEIGHT";
    if (p.oversize) s += "  OVERSIZE";
    return s;
}

std::string bagLineText(const BagLine& l) {
    std::string s = std::to_string(l.quantity) + " x " + l.name;
    if (!l.article.empty()) s += " (" + l.article + ")";
    if (l.unknownDefinition) s += " [unknown definition]";
    return s;
}

/// One package label (or hardware bag label) drawn in a cell of the label sheet.
struct LabelContent {
    std::string head;     ///< Project · assembly.
    std::string title;    ///< "Package 2/3" / "Hardware bag".
    std::string id;
    std::string size;     ///< Outer size or item count.
    std::string weight;
    bool weightUnknown = false;
    std::string contents; ///< "Parts: 1, 4, 5" / "12 pieces".
};

std::string n3(double v) { return core::formatLength(v, 3); }

void text(std::string& out, double x, double y, double size, const std::string& content, const char* extra = "") {
    out += "<text x=\"" + n3(x) + "\" y=\"" + n3(y) + "\" font-size=\"" + n3(size) + "\"" + extra + ">" +
           escapeXml(content) + "</text>\n";
}

void renderCell(std::string& out, const LabelContent& c, const LabelSheetOptions& o, double x, double y,
                const std::string& clipId) {
    const double s = o.labelHeight / 37.0; // designed for a 37 mm high label, scaled uniformly
    const double pad = 2.0;
    out += "<g transform=\"translate(" + n3(x) + " " + n3(y) + ")\">\n";
    out += "<clipPath id=\"" + clipId + "\"><rect width=\"" + n3(o.labelWidth) + "\" height=\"" +
           n3(o.labelHeight) + "\"/></clipPath>\n";
    if (o.drawBorders)
        out += "<rect width=\"" + n3(o.labelWidth) + "\" height=\"" + n3(o.labelHeight) +
               "\" fill=\"none\" stroke=\"#999\" stroke-width=\"0.1\"/>\n";
    out += "<g clip-path=\"url(#" + clipId + ")\"><g transform=\"scale(" + n3(s) + ")\">\n";
    text(out, pad, 5.0, 2.6, c.head);
    text(out, pad, 11.0, 4.2, c.title, " font-weight=\"bold\"");
    text(out, pad, 16.0, 2.4, c.id);
    text(out, pad, 21.5, 3.2, c.size);
    text(out, pad, 27.0, 3.4, c.weight, c.weightUnknown ? " font-weight=\"bold\"" : "");
    text(out, pad, 33.0, 2.6, c.contents);
    out += "</g></g>\n</g>\n";
}

} // namespace

std::string packingListText(const PackingList& list) {
    const auto& o = list.options;
    std::string s = "Packing list: max " + weightText(o.maxPackageWeightKg) + " per package, padding " + mm(o.padding) +
                    " mm per side";
    if (o.maxPackageLength > 0 || o.maxPackageWidth > 0 || o.maxPackageHeight > 0)
        s += ", max size " + size3(o.maxPackageLength, o.maxPackageWidth, o.maxPackageHeight) + " mm (0 = none)";
    s += ".\nPackage weights are the panels only (packaging and hardware not included); sizes are L x W x H mm.\n";
    for (const auto& a : list.assemblies) {
        s += "\n" + a.assemblyName + " (" + a.assemblyId.str() + "): " + std::to_string(a.packages.size()) +
             " package(s)" + (a.bag.empty() ? "" : " + 1 hardware bag") + "\n";
        for (const auto& p : a.packages) {
            s += "  Package " + std::to_string(p.number) + "/" + std::to_string(a.packages.size()) + "  " + p.id +
                 "  " + size3(p.length, p.width, p.height) + " mm  " + weightText(p.weightKg) + flags(p) + "\n";
            for (const auto& part : p.parts)
                s += "    #" + std::to_string(part.partNumber) + "  " + part.name + "  " +
                     size3(part.length, part.width, part.thickness) + "  " + part.materialName + "  " +
                     weightText(part.weightKg) + "\n";
        }
        if (!a.bag.empty()) {
            s += "  Hardware bag  " + a.bag.id + "  (" + std::to_string(a.bag.totalQuantity()) + " pieces)\n";
            if (!a.bag.fittings.empty()) s += "    Fittings:\n";
            for (const auto& l : a.bag.fittings) s += "      " + bagLineText(l) + "\n";
            if (!a.bag.fasteners.empty()) s += "    Fasteners:\n";
            for (const auto& l : a.bag.fasteners) s += "      " + bagLineText(l) + "\n";
        }
    }
    if (list.assemblies.empty()) s += "\nNothing to pack.\n";
    return s;
}

Table packingTable(const PackingList& list) {
    Table t;
    t.header = {"Assembly", "Package", "Package size (mm)", "Package weight (kg)", "No.", "Item",
                "Material", "L", "W", "T", "Quantity", "Weight (kg)"};
    for (const auto& a : list.assemblies) {
        for (const auto& p : a.packages)
            for (const auto& part : p.parts)
                t.rows.push_back({a.assemblyName, p.id, size3(p.length, p.width, p.height), kgCell(p.weightKg),
                                  std::to_string(part.partNumber), part.name, part.materialName, mm(part.length),
                                  mm(part.width), mm(part.thickness), "1", kgCell(part.weightKg)});
        for (const auto* lines : {&a.bag.fittings, &a.bag.fasteners})
            for (const auto& l : *lines)
                t.rows.push_back({a.assemblyName, a.bag.id, "", "unknown", "", l.name, "", "", "", "",
                                  std::to_string(l.quantity), "unknown"});
    }
    return t;
}

core::Result<std::vector<std::string>> renderPackageLabelsSvg(const PackingList& list, const std::string& projectName,
                                                              const LabelSheetOptions& o) {
    const auto grid = labelGrid(o);
    if (!grid) return grid.error();
    std::vector<LabelContent> cells;
    for (const auto& a : list.assemblies) {
        std::string head = projectName;
        if (!head.empty()) head += " \xC2\xB7 "; // U+00B7 MIDDLE DOT
        head += a.assemblyName;
        for (const auto& p : a.packages) {
            LabelContent c;
            c.head = head;
            c.title = "Package " + std::to_string(p.number) + "/" + std::to_string(a.packages.size());
            c.id = p.id + flags(p);
            c.size = size3(p.length, p.width, p.height) + " mm";
            c.weight = p.weightKg ? weightText(p.weightKg) : "WEIGHT UNKNOWN";
            c.weightUnknown = !p.weightKg;
            c.contents = "Parts:";
            for (std::size_t i = 0; i < p.parts.size(); ++i)
                c.contents += (i ? ", " : " ") + std::to_string(p.parts[i].partNumber);
            cells.push_back(std::move(c));
        }
        if (!a.bag.empty()) {
            LabelContent c;
            c.head = head;
            c.title = "Hardware bag";
            c.id = a.bag.id;
            c.size = std::to_string(a.bag.fittings.size() + a.bag.fasteners.size()) + " kinds of hardware";
            c.weight = "WEIGHT UNKNOWN";
            c.weightUnknown = true;
            c.contents = std::to_string(a.bag.totalQuantity()) + " pieces";
            cells.push_back(std::move(c));
        }
    }
    const std::size_t columns = static_cast<std::size_t>(grid.value().columns);
    const std::size_t perPage = static_cast<std::size_t>(grid.value().perPage());
    std::vector<std::string> pages;
    for (std::size_t first = 0; first < cells.size(); first += perPage) {
        std::string svg = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"" + n3(o.pageWidth) +
               "mm\" height=\"" + n3(o.pageHeight) + "mm\" viewBox=\"0 0 " + n3(o.pageWidth) + " " +
               n3(o.pageHeight) + "\" font-family=\"DejaVu Sans, Arial, Helvetica, sans-serif\" fill=\"#000\">\n";
        const std::size_t last = std::min(cells.size(), first + perPage);
        for (std::size_t i = first; i < last; ++i) {
            const std::size_t slot = i - first;
            const double x = o.marginLeft + static_cast<double>(slot % columns) * (o.labelWidth + o.gapX);
            const double y = o.marginTop + static_cast<double>(slot / columns) * (o.labelHeight + o.gapY);
            renderCell(svg, cells[i], o, x, y, "package-clip-" + std::to_string(slot + 1));
        }
        svg += "</svg>\n";
        pages.push_back(std::move(svg));
    }
    return pages;
}

} // namespace fos::production
