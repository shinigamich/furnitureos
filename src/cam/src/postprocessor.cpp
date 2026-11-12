// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/cam/postprocessor.hpp"

#include "fos/cam/generic_iso.hpp"

#include <algorithm>

namespace fos::cam {

using core::ErrorCode;
using core::makeError;

core::Result<PostOutput> Postprocessor::generate(const PostInput& in, const SimulationOptions& simOptions) const {
    SimulationReport report = simulate(in.program, in.part, in.setup, in.machine, in.tools, simOptions);
    if (report.hasErrors()) {
        std::size_t n = 0;
        std::string list;
        for (const auto& d : report.diagnostics) {
            if (d.severity != core::Severity::Error) continue;
            if (++n <= 5) list += " [" + d.code + "] " + d.message;
        }
        return makeError(ErrorCode::ConstraintViolation,
                         "Postprocessor '" + id() + "' refuses to emit code for '" + in.part.name + "': the simulation reports " +
                             std::to_string(n) + " error(s)." + list + (n > 5 ? " ..." : ""),
                         in.part.id.str());
    }
    auto out = emit(in, report);
    if (!out) return out;
    PostOutput& o = out.value();
    o.postprocessorId = id();
    o.postprocessorName = name();
    o.verifiedForMachine = verified() && in.machine.verified;
    o.simulation = std::move(report);
    return out;
}

core::Result<void> PostprocessorRegistry::add(std::unique_ptr<Postprocessor> post) {
    if (!post) return makeError(ErrorCode::InvalidArgument, "Cannot register a null postprocessor.");
    const std::string pid = post->id();
    if (pid.empty()) return makeError(ErrorCode::InvalidArgument, "A postprocessor needs a non-empty id.");
    if (find(pid)) return makeError(ErrorCode::AlreadyExists, "Postprocessor '" + pid + "' is already registered.", pid);
    posts_.push_back(std::move(post));
    return {};
}

const Postprocessor* PostprocessorRegistry::find(const std::string& pid) const noexcept {
    for (const auto& p : posts_)
        if (p->id() == pid) return p.get();
    return nullptr;
}

std::vector<std::string> PostprocessorRegistry::ids() const {
    std::vector<std::string> out;
    for (const auto& p : posts_) out.push_back(p->id());
    std::sort(out.begin(), out.end());
    return out;
}

PostprocessorRegistry builtinPostprocessors() {
    PostprocessorRegistry r;
    (void)r.add(std::make_unique<GenericIsoPostprocessor>());
    return r;
}

} // namespace fos::cam
