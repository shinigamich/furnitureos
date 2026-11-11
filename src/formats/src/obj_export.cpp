// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/formats/obj_export.hpp"

#include "fos/formats/atomic_file.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace fos::formats {
namespace {

using geometry::Vec3;

/// Shortest decimal text that reads back as exactly `value`; never "-0".
std::string number(double value) {
    if (value == 0.0) return "0";
    char buf[32];
    const auto result = std::to_chars(buf, buf + sizeof buf, value);
    return std::string(buf, result.ptr);
}

bool isBlankOrControl(char c) { return static_cast<unsigned char>(c) <= 0x20 || c == 0x7f; }

/// OBJ names end at whitespace: replace whitespace/control characters and '#'.
std::string objName(std::string_view text) {
    std::string out;
    for (const char c : text) out.push_back(isBlankOrControl(c) || c == '#' ? '_' : c);
    return out.empty() ? std::string("_") : out;
}

/// Text safe inside a one-line comment.
std::string commentText(std::string_view text) {
    std::string out;
    for (const char c : text) out.push_back(static_cast<unsigned char>(c) < 0x20 || c == 0x7f ? ' ' : c);
    return out;
}

/// Returns `wanted`, or `wanted_2`, `wanted_3`, ... when already taken.
std::string makeUnique(const std::string& wanted, std::set<std::string>& used) {
    std::string name = wanted;
    for (int n = 2; !used.insert(name).second; ++n) name = wanted + "_" + std::to_string(n);
    return name;
}

void appendVector(std::string& out, const char* tag, Vec3 v) {
    out += tag;
    out += ' ';
    out += number(v.x);
    out += ' ';
    out += number(v.y);
    out += ' ';
    out += number(v.z);
    out += '\n';
}

// Box corner i: x = (i & 1) ? L : 0, y = (i & 2) ? W : 0, z = (i & 4) ? T : 0.
// Quads are counter-clockwise seen from outside (normal = (p1 - p0) x (p2 - p0)):
//   B  (z = 0, normal -Z): 0 2 3 1      A  (z = T, normal +Z): 4 5 7 6
//   L1 (y = 0, normal -Y): 0 1 5 4      L2 (y = W, normal +Y): 2 6 7 3
//   W1 (x = 0, normal -X): 0 4 6 2      W2 (x = L, normal +X): 1 3 7 5
constexpr std::array<std::array<int, 4>, 6> kQuads{{{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4},
                                                    {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}}};

} // namespace

core::Result<ObjExport> exportObj(const domain::Model& model, const ObjExportOptions& options) {
    using core::ErrorCode;
    using core::makeError;
    if (options.mtlFileName.empty() || objName(options.mtlFileName) != options.mtlFileName)
        return makeError(ErrorCode::InvalidArgument,
                         "OBJ export: the MTL file name \"" + commentText(options.mtlFileName) +
                             "\" must be non-empty and must not contain spaces, control characters or '#'.");

    ObjExport out;
    std::string& obj = out.obj;
    obj += "# FurnitureOS Wavefront OBJ export\n";
    obj += "# Units: millimetres\n";
    obj += "# Axes: FurnitureOS project frame, right-handed, Z up\n";
    obj += "# One object per part: box of the finished size (length x width x thickness).\n";
    obj += "# Not included: machining operations, edge bands, hardware, textures.\n";
    obj += "mtllib " + options.mtlFileName + "\n";

    // OBJ material name per model material id, assigned on first use (deterministic order).
    std::map<std::string, std::string> materialNames;
    std::set<std::string> usedMaterialNames, usedObjectNames;
    std::string& mtl = out.mtl;
    mtl += "# FurnitureOS material library (display colours only)\n";

    const auto materialFor = [&](const domain::Part& part) -> const std::string& {
        const std::string& id = part.material.str();
        if (const auto it = materialNames.find(id); it != materialNames.end()) return it->second;
        const domain::Material* material = model.findMaterial(part.material);
        const std::string name = makeUnique(id.empty() ? std::string("unassigned") : objName(id), usedMaterialNames);
        domain::Color color; // default grey for unknown materials
        if (material != nullptr) {
            color = material->color;
            mtl += "\n# " + commentText(material->name) + ", " + number(material->thickness) + " mm\n";
        } else {
            mtl += id.empty() ? "\n# parts without a material\n"
                              : "\n# material \"" + commentText(id) + "\" is not defined in the model\n";
            out.diagnostics.push_back(
                {core::Severity::Warning, "formats.obj.material_missing",
                 id.empty() ? "Part '" + part.name + "' (" + part.id.str() +
                                  ") has no material; it is exported with a neutral grey OBJ material."
                            : "Part '" + part.name + "' (" + part.id.str() + ") refers to material \"" + id +
                                  "\" which is not in the model; it is exported with a neutral grey OBJ material.",
                 part.id.str()});
        }
        char kd[64];
        std::snprintf(kd, sizeof kd, "Kd %.4f %.4f %.4f\n", color.r / 255.0, color.g / 255.0, color.b / 255.0);
        mtl += "newmtl " + name + "\n" + kd + "d 1\nillum 1\n";
        return materialNames.emplace(id, name).first->second;
    };

    std::size_t vertexBase = 1, normalBase = 1; // OBJ indices are 1-based and global
    for (const domain::Assembly& assembly : model.assemblies) {
        if (!assembly.placement.isRigid())
            return makeError(ErrorCode::InvalidArgument,
                             "OBJ export: the placement of assembly '" + assembly.name + "' (" + assembly.id.str() +
                                 ") is not a rigid transform.",
                             assembly.id.str());
        for (const domain::Part& part : assembly.parts) {
            const std::string who = "part '" + part.name + "' (" + part.id.str() + ")";
            const double L = part.length, W = part.width, T = part.thickness;
            if (!(std::isfinite(L) && std::isfinite(W) && std::isfinite(T) && L > 0 && W > 0 && T > 0))
                return makeError(ErrorCode::InvalidArgument,
                                 "OBJ export: " + who + " has the size " + number(L) + " x " + number(W) + " x " +
                                     number(T) + " mm; all three dimensions must be positive.",
                                 part.id.str());
            if (!part.placement.isRigid())
                return makeError(ErrorCode::InvalidArgument,
                                 "OBJ export: the placement of " + who + " is not a rigid transform.", part.id.str());
            const geometry::Transform toProject = assembly.placement * part.placement;

            const std::string name = makeUnique(objName(part.id.empty() ? part.name : part.id.str()), usedObjectNames);
            obj += "\n# " + commentText(part.name) + ": " + number(L) + " x " + number(W) + " x " + number(T) +
                   " mm, assembly " + commentText(assembly.id.str()) + "\n";
            obj += "o " + name + "\ng " + name + "\nusemtl " + materialFor(part) + "\n";
            for (int i = 0; i < 8; ++i)
                appendVector(obj, "v", toProject.applyPoint({(i & 1) ? L : 0.0, (i & 2) ? W : 0.0, (i & 4) ? T : 0.0}));
            const std::array<Vec3, 6> normals{-toProject.zAxis, toProject.zAxis,  -toProject.yAxis,
                                              toProject.yAxis,  -toProject.xAxis, toProject.xAxis};
            for (const Vec3 n : normals) appendVector(obj, "vn", n);
            for (std::size_t f = 0; f < kQuads.size(); ++f) {
                obj += "f";
                for (const int corner : kQuads[f])
                    obj += " " + std::to_string(vertexBase + static_cast<std::size_t>(corner)) + "//" +
                           std::to_string(normalBase + f);
                obj += '\n';
            }
            vertexBase += 8;
            normalBase += 6;
        }
    }
    return out;
}

core::Result<ObjExport> writeObjFiles(const std::filesystem::path& objPath, const domain::Model& model) {
    ObjExportOptions options;
    options.mtlFileName = objName(objPath.stem().string()) + ".mtl";
    auto exported = exportObj(model, options);
    if (!exported) return exported;
    SaveOptions save;
    save.backupCount = 0;
    std::filesystem::path mtlPath = objPath;
    mtlPath.replace_filename(options.mtlFileName);
    if (auto r = atomicWriteFile(mtlPath, exported.value().mtl, save); !r) return r.error();
    if (auto r = atomicWriteFile(objPath, exported.value().obj, save); !r) return r.error();
    return exported;
}

} // namespace fos::formats
