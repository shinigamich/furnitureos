// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Minimal independent OBJ reader for the subset the exporter writes (test helper).
#include "fos/geometry/aabb.hpp"
#include "fos/geometry/vec.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

namespace fos::formats::test {

using geometry::Vec3;

struct ObjFace {
    std::vector<int> vertices; // 0-based global indices
    int normal = -1;
};

struct ObjObject {
    std::string name, group, material;
    std::vector<ObjFace> faces;
};

/// Minimal independent OBJ reader for the subset the exporter writes.
struct ParsedObj {
    std::vector<Vec3> vertices, normals;
    std::vector<ObjObject> objects;
    std::string mtllib;
    std::vector<std::string> comments;
};

inline ParsedObj parseObj(const std::string& text) {
    ParsedObj out;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') {
            out.comments.push_back(line);
            continue;
        }
        std::istringstream in(line);
        std::string tag;
        in >> tag;
        if (tag == "v" || tag == "vn") {
            Vec3 v;
            in >> v.x >> v.y >> v.z;
            EXPECT_FALSE(in.fail()) << line;
            (tag == "v" ? out.vertices : out.normals).push_back(v);
        } else if (tag == "o") {
            out.objects.emplace_back();
            in >> out.objects.back().name;
        } else if (tag == "g") {
            in >> out.objects.back().group;
        } else if (tag == "usemtl") {
            in >> out.objects.back().material;
        } else if (tag == "mtllib") {
            in >> out.mtllib;
        } else if (tag == "f") {
            ObjFace face;
            std::string corner;
            while (in >> corner) {
                const auto slashes = corner.find("//");
                EXPECT_NE(slashes, std::string::npos) << line;
                face.vertices.push_back(std::stoi(corner.substr(0, slashes)) - 1);
                face.normal = std::stoi(corner.substr(slashes + 2)) - 1;
            }
            out.objects.back().faces.push_back(face);
        } else {
            ADD_FAILURE() << "unexpected OBJ statement: " << line;
        }
    }
    return out;
}

/// Signed volume by the divergence theorem: sum over fan triangles of v0 . (v1 x v2) / 6.
/// Positive exactly when the faces wind counter-clockwise seen from outside.
inline double signedVolume(const ParsedObj& obj, const ObjObject& object) {
    double six = 0;
    for (const ObjFace& face : object.faces)
        for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i)
            six += dot(obj.vertices[face.vertices[0]], cross(obj.vertices[face.vertices[i]], obj.vertices[face.vertices[i + 1]]));
    return six / 6.0;
}

inline const ObjObject& find(const ParsedObj& obj, const std::string& name) {
    for (const auto& o : obj.objects)
        if (o.name == name) return o;
    ADD_FAILURE() << "object " << name << " not found";
    return obj.objects.front();
}

inline geometry::Aabb boundsOf(const ParsedObj& obj, const ObjObject& object) {
    geometry::Aabb box;
    for (const ObjFace& face : object.faces)
        for (const int v : face.vertices) box.expand(obj.vertices[v]);
    return box;
}


} // namespace fos::formats::test
