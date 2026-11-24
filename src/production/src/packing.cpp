// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/packing.hpp"

#include "collect.hpp"
#include "fos/core/units.hpp"
#include "fos/production/reports.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace fos::production {
namespace {

std::string mm(double v) { return core::formatLength(v, 1); }

bool finiteNonNegative(double v) noexcept { return std::isfinite(v) && v >= 0.0; }

core::Result<void> checkOptions(const PackingOptions& o) {
    auto invalid = [](std::string msg) { return core::makeError(core::ErrorCode::InvalidArgument, std::move(msg)); };
    if (!detail::positiveFinite(o.maxPackageWeightKg))
        return invalid("Maximum package weight " + core::formatLength(o.maxPackageWeightKg, 3) +
                       " kg must be a positive number.");
    const std::pair<const char*, double> limits[] = {{"length", o.maxPackageLength},
                                                     {"width", o.maxPackageWidth},
                                                     {"height", o.maxPackageHeight},
                                                     {"padding", o.padding}};
    for (const auto& [name, v] : limits)
        if (!finiteNonNegative(v))
            return invalid(std::string("Package ") + name + " " + core::formatLength(v, 3) +
                           " mm must be a finite number >= 0 (0 = no limit for length/width/height).");
    return {};
}

/// A panel waiting to be packed.
struct Item {
    PackedPart part;
    double longSide = 0, shortSide = 0;
};

struct Open {
    Package pkg;
    double weight = 0;     ///< Known class only.
    double thickness = 0;  ///< Sum of panel thicknesses.
    double longSide = 0, shortSide = 0;
    bool closed = false;   ///< Single-panel packages (overweight / oversize) take nothing else.
};

void finish(Open& o, double padding, bool knownClass) {
    o.pkg.length = o.longSide + 2.0 * padding;
    o.pkg.width = o.shortSide + 2.0 * padding;
    o.pkg.height = o.thickness + 2.0 * padding;
    if (knownClass) o.pkg.weightKg = o.weight;
}

/// Packs one weight class of one assembly (see the rule in packing.hpp).
std::vector<Package> packClass(std::vector<Item> items, bool knownClass, const PackingOptions& opt,
                               const domain::Assembly& a, core::Diagnostics& diags) {
    std::stable_sort(items.begin(), items.end(), [](const Item& x, const Item& y) {
        if (x.longSide != y.longSide) return x.longSide > y.longSide;
        if (x.shortSide != y.shortSide) return x.shortSide > y.shortSide;
        return x.part.partNumber < y.part.partNumber;
    });
    const double p2 = 2.0 * opt.padding;
    auto exceeds = [](double limit, double v) { return limit > 0.0 && v > limit + core::kLengthEps; };
    std::vector<Open> open;
    for (auto& it : items) {
        const double w = it.part.weightKg.value_or(0.0);
        const bool tooLong = exceeds(opt.maxPackageLength, it.longSide + p2);
        const bool tooWide = exceeds(opt.maxPackageWidth, it.shortSide + p2);
        const bool tooHigh = exceeds(opt.maxPackageHeight, it.part.thickness + p2);
        const bool tooHeavy = knownClass && w > opt.maxPackageWeightKg + 1e-9;
        const std::string who = "Panel " + std::to_string(it.part.partNumber) + " '" + it.part.name + "' of '" +
                                a.name + "'";
        if (tooLong || tooWide || tooHigh || tooHeavy) {
            Open o;
            o.closed = true;
            o.pkg.oversize = tooLong || tooWide || tooHigh;
            o.pkg.overweight = tooHeavy;
            if (o.pkg.oversize)
                detail::warn(diags, "production.packing.oversize",
                             who + " (" + mm(it.longSide) + " x " + mm(it.shortSide) + " x " + mm(it.part.thickness) +
                                 " mm, padding " + mm(opt.padding) + " mm per side) exceeds the package size limit " +
                                 mm(opt.maxPackageLength) + " x " + mm(opt.maxPackageWidth) + " x " +
                                 mm(opt.maxPackageHeight) + " mm (0 = none); it is packed alone.",
                             it.part.partId.str());
            if (tooHeavy)
                detail::warn(diags, "production.packing.overweight",
                             who + " weighs " + weightText(w) + ", more than the package limit of " +
                                 weightText(opt.maxPackageWeightKg) + "; it is packed alone.",
                             it.part.partId.str());
            o.weight = w;
            o.thickness = it.part.thickness;
            o.longSide = it.longSide;
            o.shortSide = it.shortSide;
            o.pkg.parts.push_back(std::move(it.part));
            open.push_back(std::move(o));
            continue;
        }
        Open* target = nullptr;
        for (auto& o : open) {
            if (o.closed) continue;
            if (knownClass && o.weight + w > opt.maxPackageWeightKg + 1e-9) continue;
            if (exceeds(opt.maxPackageHeight, o.thickness + it.part.thickness + p2)) continue;
            target = &o;
            break;
        }
        if (!target) target = &open.emplace_back();
        target->weight += w;
        target->thickness += it.part.thickness;
        target->longSide = std::max(target->longSide, it.longSide);
        target->shortSide = std::max(target->shortSide, it.shortSide);
        target->pkg.parts.push_back(std::move(it.part));
    }
    std::vector<Package> out;
    for (auto& o : open) {
        finish(o, opt.padding, knownClass);
        out.push_back(std::move(o.pkg));
    }
    return out;
}

HardwareBag makeBag(const domain::Model& model, const domain::Assembly& a, core::Diagnostics& diags) {
    HardwareBag bag;
    bag.id = a.id.str() + "-H";
    Scope scope;
    scope.assembly = a.id;
    const auto report = hardwareReport(model, scope);
    diags.insert(diags.end(), report.diagnostics.begin(), report.diagnostics.end());
    for (const auto& row : report.rows) {
        BagLine line;
        line.definition = row.definitionId;
        line.name = row.unknownDefinition ? row.definitionId.str() : row.name;
        line.article = row.article;
        line.kind = row.kind;
        line.unknownDefinition = row.unknownDefinition;
        line.quantity = row.quantity;
        line.totalLengthM = row.totalLengthM;
        (isFastener(row.kind) ? bag.fasteners : bag.fittings).push_back(std::move(line));
    }
    return bag;
}

} // namespace

int HardwareBag::totalQuantity() const noexcept {
    int n = 0;
    for (const auto& l : fittings) n += l.quantity;
    for (const auto& l : fasteners) n += l.quantity;
    return n;
}

bool isFastener(domain::HardwareKind kind) noexcept {
    using K = domain::HardwareKind;
    return kind == K::Confirmat || kind == K::Dowel || kind == K::CamConnector || kind == K::Screw;
}

std::string weightText(const std::optional<double>& kg) {
    return kg ? core::formatLength(*kg, 2) + " kg" : std::string("weight unknown");
}

core::Result<PackingList> packingList(const domain::Model& model, const PackingOptions& options, const Scope& scope) {
    if (auto ok = checkOptions(options); !ok) return ok.error();
    auto c = detail::collect(model, scope);
    PackingList list;
    list.options = options;
    list.diagnostics = std::move(c.diagnostics);

    // Materials without density, reported once each with the number of affected panels.
    std::map<std::string, int> noDensity;
    std::set<std::string> missingMaterial;
    std::map<const domain::Assembly*, std::pair<std::vector<Item>, std::vector<Item>>> byAssembly; // known, unknown
    for (const auto& ref : c.parts) {
        const PartRow row = detail::makeRow(model, ref);
        if (!detail::positiveFinite(row.length) || !detail::positiveFinite(row.width) ||
            !detail::positiveFinite(row.thickness)) {
            detail::warn(list.diagnostics, "production.packing.invalid_size",
                         "Part " + detail::describe(*ref.part) + " has the size " + mm(row.length) + " x " +
                             mm(row.width) + " x " + mm(row.thickness) + " mm; it is not in the packing list.",
                         ref.part->id.str());
            continue;
        }
        Item it;
        it.part.partNumber = row.number;
        it.part.partId = row.partId;
        it.part.name = row.name;
        it.part.role = row.role;
        it.part.materialName = row.materialMissing ? row.materialId.str() : row.materialName;
        it.part.length = row.length;
        it.part.width = row.width;
        it.part.thickness = row.thickness;
        it.longSide = std::max(row.length, row.width);
        it.shortSide = std::min(row.length, row.width);
        const domain::Material* mat = model.findMaterial(row.materialId);
        const double area = row.finishedAreaM2();
        if (!mat) {
            if (missingMaterial.insert(row.materialId.str()).second)
                detail::warn(list.diagnostics, "production.material.missing",
                             "Material '" + row.materialId.str() + "' of part " + detail::describe(*ref.part) +
                                 " is not in the model; the weight of its panels is unknown.",
                             row.materialId.str());
        } else if (!detail::positiveFinite(mat->density)) {
            ++noDensity[mat->id.str()];
        } else if (area > 0.0) {
            it.part.weightKg = area * (row.thickness / 1000.0) * mat->density;
        } else {
            detail::warn(list.diagnostics, "production.packing.weight_unknown",
                         "The area of part " + detail::describe(*ref.part) +
                             " cannot be computed (invalid outline); its weight is unknown.",
                         ref.part->id.str());
        }
        auto& slot = byAssembly[ref.assembly];
        (it.part.weightKg ? slot.first : slot.second).push_back(std::move(it));
    }
    for (const auto& mat : model.materials) {
        auto found = noDensity.find(mat.id.str());
        if (found == noDensity.end()) continue;
        detail::warn(list.diagnostics, "production.packing.density_unknown",
                     "Material '" + mat.name + "' (" + mat.id.str() + ") has no density; the weight of " +
                         std::to_string(found->second) +
                         " panel(s) is unknown. Set the material density (kg/m3) to get package weights.",
                     mat.id.str());
        noDensity.erase(found); // duplicate material ids: report once
    }

    for (const auto* a : c.assemblies) {
        AssemblyPacking ap;
        ap.assemblyId = a->id;
        ap.assemblyName = a->name;
        auto it = byAssembly.find(a);
        if (it != byAssembly.end()) {
            ap.packages = packClass(std::move(it->second.first), true, options, *a, list.diagnostics);
            auto unknown = packClass(std::move(it->second.second), false, options, *a, list.diagnostics);
            ap.packages.insert(ap.packages.end(), std::make_move_iterator(unknown.begin()),
                               std::make_move_iterator(unknown.end()));
        }
        for (std::size_t i = 0; i < ap.packages.size(); ++i) {
            ap.packages[i].number = static_cast<int>(i + 1);
            ap.packages[i].id = a->id.str() + "-P" + std::to_string(i + 1);
        }
        ap.bag = makeBag(model, *a, list.diagnostics);
        if (ap.packages.empty() && ap.bag.empty()) continue;
        list.assemblies.push_back(std::move(ap));
    }
    return list;
}

} // namespace fos::production
