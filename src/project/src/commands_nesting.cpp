// SPDX-License-Identifier: AGPL-3.0-only
// Command of the project nesting settings (shaped parts, README section 2.6).
#include "fos/project/commands.hpp"

namespace fos::project::commands {

Command setNestingSettings(std::optional<NestingSettings> settings) {
    Command c;
    c.label = CommandLabel{settings ? "Change nesting settings" : "Reset nesting settings", {}};
    c.apply = [settings](Project& p) -> core::Result<Affected> {
        if (settings)
            if (auto ok = checkNestingSettings(*settings); !ok) return ok.error();
        p.cutting.nesting = settings;
        Affected a;
        a.settings = true;
        return a;
    };
    return c;
}

} // namespace fos::project::commands
