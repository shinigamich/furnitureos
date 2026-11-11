// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/ids.hpp"

#include <cstdint>
#include <map>
#include <string>

namespace fos::domain {

/// sRGB colour used for display only.
struct Color {
    std::uint8_t r = 200, g = 200, b = 200;
    bool operator==(const Color&) const = default;
};

enum class MaterialKind { Particleboard, Mdf, Hdf, Plywood, SolidWood, Other };

/// Sheet (board) material. Lengths in mm, costs in the project currency.
struct Material {
    MaterialId id;
    std::string name;
    std::string manufacturer;
    std::string code;           ///< Manufacturer / supplier decor code.
    std::string supplier;
    MaterialKind kind = MaterialKind::Particleboard;
    double thickness = 18.0;
    double sheetLength = 2800.0; ///< Stock sheet size along the grain (if any).
    double sheetWidth = 2070.0;
    bool hasGrain = false;       ///< True for decors/veneers with a visible direction (runs along sheetLength).
    Color color;
    std::string texture;         ///< Optional texture reference (relative path or library key).
    double costPerSheet = 0.0;   ///< 0 = unknown.
    bool available = true;
    std::map<std::string, std::string> custom; ///< User-defined properties (always preserved).
};

enum class EdgeBandKind { Abs, Pvc, Melamine, Veneer, SolidWood, Other };

struct EdgeBand {
    EdgeBandId id;
    std::string name;
    std::string manufacturer;
    std::string code;
    EdgeBandKind kind = EdgeBandKind::Abs;
    double thickness = 2.0;  ///< Added to the panel edge.
    double width = 22.0;     ///< Tape width (must cover the board thickness).
    Color color;
    double costPerMeter = 0.0;
    std::map<std::string, std::string> custom;
};

const char* toString(MaterialKind k) noexcept;
const char* toString(EdgeBandKind k) noexcept;

} // namespace fos::domain
