// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/browser_tree.hpp"

#include "fos/core/units.hpp"

namespace fos::app {
namespace {
std::string sizeText(double a, double b, double c) {
    return core::formatLength(a) + " × " + core::formatLength(b) + " × " + core::formatLength(c);
}
} // namespace

std::vector<BrowserNode> buildBrowserTree(const project::Project& project, const project::Resolved* resolved) {
    std::vector<BrowserNode> nodes;
    for (const auto& item : project.items) {
        BrowserNode node;
        node.kind = BrowserNode::Kind::Item;
        node.id = item.id();
        node.label = item.name();
        node.isCabinet = item.isCabinet();
        node.generated = false;
        const domain::Assembly* assembly = resolved ? resolved->model.findAssembly(domain::AssemblyId(item.id())) : nullptr;
        if (assembly) {
            node.generated = true;
            BrowserNode a;
            a.kind = BrowserNode::Kind::Assembly;
            a.id = assembly->id.str();
            a.label = assembly->name;
            const auto size = assembly->bounds().size();
            a.detail = sizeText(size.x, size.y, size.z);
            for (const auto& part : assembly->parts) {
                BrowserNode p;
                p.kind = BrowserNode::Kind::Part;
                p.id = part.id.str();
                p.label = part.name;
                p.detail = sizeText(part.length, part.width, part.thickness);
                a.children.push_back(std::move(p));
            }
            node.children.push_back(std::move(a));
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}

} // namespace fos::app
