// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/fixtures.hpp"
#include "fos/validation/validation.hpp"

#include <gtest/gtest.h>

#include <string>

namespace fos::validation::test {

inline domain::Part& part(domain::Model& m, const std::string& id, std::size_t assembly = 0) {
    domain::Part* p = m.assemblies[assembly].findPart(domain::PartId(id));
    if (!p) {
        ADD_FAILURE() << "missing part " << id;
        static domain::Part dummy;
        return dummy;
    }
    return *p;
}

/// All findings, one per line, for failure messages.
inline std::string dumpFindings(const ValidationReport& r) { return toText(r); }

inline std::size_t count(const ValidationReport& r, const std::string& code) {
    return findingsWithCode(r, code).size();
}

/// The only finding with `code`; fails the test (and returns an empty finding) otherwise.
inline Finding only(const ValidationReport& r, const std::string& code) {
    const auto list = findingsWithCode(r, code);
    EXPECT_EQ(list.size(), 1u) << code << "\n" << toText(r);
    return list.empty() ? Finding{} : list.front();
}

/// A model with the fixture's libraries and one assembly "a1" holding `parts`.
inline domain::Model libraryModel(std::vector<domain::Part> parts = {}) {
    domain::Model m = domain::fixtures::basicCabinet();
    m.assemblies.clear();
    domain::Assembly a;
    a.id = domain::AssemblyId("a1");
    a.name = "Test assembly";
    a.parts = std::move(parts);
    m.assemblies.push_back(std::move(a));
    return m;
}

inline domain::Part board(const std::string& id, double l, double w, geometry::Transform placement = {},
                          const std::string& material = "mat.pb18", double t = 18.0) {
    domain::Part p;
    p.id = domain::PartId(id);
    p.name = "Board " + id;
    p.length = l;
    p.width = w;
    p.thickness = t;
    p.material = domain::MaterialId(material);
    p.placement = placement;
    return p;
}

} // namespace fos::validation::test
