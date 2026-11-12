// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// Internal helpers shared by the checks (not installed, not public API).
#include "fos/domain/model.hpp"
#include "fos/validation/options.hpp"
#include "fos/validation/report.hpp"

#include <string>
#include <vector>

namespace fos::validation::detail {

/// State shared by the checks of one validateModel() run.
struct Context {
    const domain::Model& model;
    const ValidationOptions& options;
    ValidationReport& report;

    /// Appends a finding (counts and sorting are done once at the end).
    void add(core::Severity severity, std::string code, std::string message, std::string entityId,
             std::string assemblyId = {}, std::string partId = {}, std::vector<std::string> related = {});
};

/// A registered check: name, one-sentence description, enable switch and implementation.
struct Check {
    const char* name;
    const char* description;
    bool ValidationOptions::*enabled;
    void (*run)(Context&);
};

void checkParts(Context& c);
void checkEdgeBanding(Context& c);
void checkPartInterference(Context& c);
void checkAssemblyInterference(Context& c);
void checkMachining(Context& c);
void checkHardware(Context& c);
void checkCompleteness(Context& c);

/// core::formatLength (0.1 mm), but "NaN"/"inf" for non-finite values.
std::string num(double mm);
/// "'Name'" or, for an unnamed part, "part 'id'".
std::string label(const domain::Part& p);
std::string label(const domain::Assembly& a);
bool finite(double v) noexcept;
bool finite(const geometry::Vec3& v) noexcept;
bool finite(const geometry::Transform& t) noexcept;
/// Finite, positive length/width/thickness.
bool validSize(const domain::Part& p) noexcept;
/// Finite and rigid (geometry::Transform::isRigid with 1e-6).
bool validPlacement(const geometry::Transform& t) noexcept;
/// "X"/"Y"/"Z" when `axis` is (anti)parallel to a frame axis within 1e-9, else "(x, y, z)" (3 decimals).
std::string axisName(geometry::Vec3 axis);
/// True when every axis of `t` is (anti)parallel to a frame axis within 1e-9.
bool axisAligned(const geometry::Transform& t) noexcept;

} // namespace fos::validation::detail
