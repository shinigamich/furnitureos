// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/cam/machine.hpp"
#include "fos/cam/setup.hpp"
#include "fos/cam/simulate.hpp"
#include "fos/cam/tool.hpp"
#include "fos/cam/toolpath.hpp"
#include "fos/core/result.hpp"

#include <memory>
#include <string>
#include <vector>

/// Postprocessor interface. A postprocessor translates a simulated Program into text for a
/// controller dialect. No FurnitureOS postprocessor is verified for any machine.
namespace fos::cam {

struct PostInput {
    const Program& program;
    const domain::Part& part;
    const Setup& setup;
    const MachineProfile& machine;
    const ToolLibrary& tools;
};

struct PostOutput {
    std::string postprocessorId;
    std::string postprocessorName;
    std::string fileExtension;
    std::string text;
    /// Always false for every postprocessor shipped with FurnitureOS.
    bool verifiedForMachine = false;
    /// Points an operator must check before use (dialect assumptions etc.).
    std::vector<std::string> reviewWarnings;
    SimulationReport simulation;
};

class Postprocessor {
public:
    virtual ~Postprocessor() = default;
    virtual std::string id() const = 0;
    virtual std::string name() const = 0;
    /// True only for a postprocessor proven on a real control. None is.
    virtual bool verified() const noexcept { return false; }

    /// Runs simulate() first and REFUSES (ConstraintViolation, listing the errors) when the
    /// simulation reports any error; then emits. verifiedForMachine = verified() && machine.verified.
    core::Result<PostOutput> generate(const PostInput& input, const SimulationOptions& simOptions = {}) const;

protected:
    virtual core::Result<PostOutput> emit(const PostInput& input, const SimulationReport& report) const = 0;
};

/// Owns postprocessors by id (no global state).
class PostprocessorRegistry {
public:
    /// Error (AlreadyExists / InvalidArgument) for a duplicate or empty id or null pointer.
    core::Result<void> add(std::unique_ptr<Postprocessor> post);
    const Postprocessor* find(const std::string& id) const noexcept;
    std::vector<std::string> ids() const; ///< sorted

private:
    std::vector<std::unique_ptr<Postprocessor>> posts_;
};

/// Registry with the reference postprocessor "generic-iso" only.
PostprocessorRegistry builtinPostprocessors();

} // namespace fos::cam
