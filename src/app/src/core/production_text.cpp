// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/production_text.hpp"

namespace fos::app {
namespace {
std::string s(const QString& text) { return text.toStdString(); }
} // namespace

production::Localisation ProductionText::localisation() {
    production::Localisation l;
    auto& p = l.partList;
    p.number = s(tr("No.")); p.assembly = s(tr("Assembly")); p.partId = s(tr("Part ID")); p.name = s(tr("Name"));
    p.role = s(tr("Role")); p.material = s(tr("Material")); p.length = s(tr("Length")); p.width = s(tr("Width"));
    p.thickness = s(tr("Thickness")); p.cutLength = s(tr("Cut length")); p.cutWidth = s(tr("Cut width"));
    p.grain = s(tr("Grain")); p.edgeL1 = s(tr("Edge L1")); p.edgeL2 = s(tr("Edge L2")); p.edgeW1 = s(tr("Edge W1"));
    p.edgeW2 = s(tr("Edge W2")); p.operations = s(tr("Operations"));

    auto& c = l.cutList;
    c.material = p.material; c.thickness = p.thickness; c.cutLength = p.cutLength; c.cutWidth = p.cutWidth;
    c.quantity = s(tr("Qty")); c.length = s(tr("Finished length")); c.width = s(tr("Finished width"));
    c.grain = p.grain; c.rotationAllowed = s(tr("Rotation allowed")); c.edgeL1 = p.edgeL1; c.edgeL2 = p.edgeL2;
    c.edgeW1 = p.edgeW1; c.edgeW2 = p.edgeW2; c.name = p.name; c.operations = p.operations;
    c.partNumbers = s(tr("Part numbers")); c.partIds = s(tr("Part IDs"));

    auto& m = l.materials;
    m.material = p.material; m.thickness = p.thickness; m.parts = s(tr("Parts"));
    m.finishedArea = s(tr("Finished area (m2)")); m.cutArea = s(tr("Cut area (m2)"));
    m.sheetLength = s(tr("Sheet length")); m.sheetWidth = s(tr("Sheet width"));
    m.sheetsLowerBound = s(tr("Sheets (area lower bound)")); m.costPerSheet = s(tr("Cost per sheet"));
    m.costAtLowerBound = s(tr("Cost at area lower bound")); m.note = s(tr("Note"));

    auto& e = l.edgeBanding;
    e.edgeBand = s(tr("Edge band")); e.thickness = p.thickness; e.width = p.width; e.edges = s(tr("Edges"));
    e.appliedLength = s(tr("Applied length (m)")); e.overhangAllowance = s(tr("Overhang allowance (m)"));
    e.wasteAllowance = s(tr("Waste allowance (m)")); e.purchaseLength = s(tr("Purchase length (m)"));
    e.costPerMeter = s(tr("Cost per m")); e.cost = s(tr("Cost")); e.note = m.note;

    auto& h = l.hardware;
    h.hardware = s(tr("Hardware")); h.manufacturer = s(tr("Manufacturer")); h.article = s(tr("Article"));
    h.quantity = c.quantity; h.unitCost = s(tr("Unit cost")); h.total = s(tr("Total"));
    h.dimensionsVerified = s(tr("Dimensions verified")); h.note = m.note;

    auto& x = l.machining;
    x.number = p.number; x.assembly = p.assembly; x.partId = p.partId; x.name = p.name;
    x.holes = s(tr("Holes (count x diameter @ face)")); x.holeCount = s(tr("Holes")); x.grooves = s(tr("Grooves"));
    x.grooveLength = s(tr("Groove length")); x.pockets = s(tr("Pockets"));

    auto& lb = l.labels;
    lb.number = p.number; lb.project = s(tr("Project")); lb.assembly = p.assembly; lb.name = p.name;
    lb.partId = p.partId; lb.length = p.length; lb.width = p.width; lb.thickness = p.thickness;
    lb.cutLength = p.cutLength; lb.cutWidth = p.cutWidth; lb.material = p.material;
    lb.edgeCode = s(tr("Edges (L1 L2 W1 W2)")); lb.quantity = s(tr("Qty index"));

    auto& co = l.cost;
    co.category = s(tr("Category")); co.item = s(tr("Item")); co.quantity = s(tr("Quantity")); co.unit = s(tr("Unit"));
    co.unitCost = h.unitCost; co.cost = e.cost; co.currency = s(tr("Currency")); co.note = m.note;

    auto& su = l.summary;
    su.assembly = p.assembly; su.parts = m.parts; su.hardware = s(tr("Hardware items")); su.operations = p.operations;
    su.finishedArea = m.finishedArea; su.width = p.width; su.depth = s(tr("Depth")); su.height = s(tr("Height"));

    auto& t = l.terms;
    t.yes = s(tr("yes")); t.no = s(tr("no")); t.missing = s(tr("MISSING"));
    t.unknownDefinition = s(tr("UNKNOWN DEFINITION")); t.unverified = s(tr("dimensions not verified"));
    t.priceUnknown = s(tr("price unknown")); t.noSheetSize = s(tr("no sheet size"));
    t.grainNone = s(tr("none")); t.grainAlongLength = s(tr("along length")); t.grainAlongWidth = s(tr("along width"));
    t.materials = s(tr("Materials")); t.edgeBanding = s(tr("Edge banding")); t.hardware = h.hardware;
    t.subtotal = s(tr("Subtotal")); t.total = h.total; t.sheets = s(tr("sheets")); t.meters = s(tr("m"));
    t.pieces = s(tr("pcs"));
    t.areaLowerBound = s(tr("area lower bound - real sheet count comes from the cutting optimiser"));
    t.suppliedSheetCount = s(tr("supplied sheet count"));
    t.totalIsLowerBound = s(tr("lower bound (materials at area lower bound)"));
    t.totalIncomplete = s(tr("incomplete (some prices unknown)"));
    using R = domain::PartRole;
    t.roles = {{R::Side, s(tr("Side"))},           {R::Top, s(tr("Top"))},
               {R::Bottom, s(tr("Bottom"))},       {R::Shelf, s(tr("Shelf"))},
               {R::Partition, s(tr("Partition"))}, {R::Back, s(tr("Back"))},
               {R::Door, s(tr("Door"))},           {R::DrawerFront, s(tr("Drawer front"))},
               {R::DrawerSide, s(tr("Drawer side"))}, {R::DrawerBack, s(tr("Drawer back"))},
               {R::DrawerBottom, s(tr("Drawer bottom"))}, {R::Plinth, s(tr("Plinth"))},
               {R::Worktop, s(tr("Worktop"))},     {R::Rail, s(tr("Rail"))},
               {R::Generic, s(tr("Panel"))}};
    return l;
}

production::HtmlSheetText ProductionText::htmlSheetText() {
    production::HtmlSheetText t;
    t.lang = s(tr("en", "HTML language code of the production sheet"));
    t.title = s(tr("Production sheet"));
    t.project = s(tr("Project"));
    t.assembly = s(tr("Assembly"));
    t.envelope = s(tr("Envelope (W x D x H, mm)"));
    t.partList = s(tr("Part list"));
    t.cutList = s(tr("Cut list"));
    t.edgeBanding = s(tr("Edge banding"));
    t.hardware = s(tr("Hardware"));
    t.warnings = s(tr("Warnings"));
    t.none = s(tr("none"));
    return t;
}

production::LabelText ProductionText::labelText() {
    production::LabelText t;
    t.cut = s(tr("Cut"));
    t.number = s(tr("No."));
    return t;
}

} // namespace fos::app
