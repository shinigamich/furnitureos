// SPDX-License-Identifier: AGPL-3.0-only
// Runs the real `fos` executable as a separate process (path injected by CMake) to check
// that main() forwards arguments, output streams and exit codes.
#include "cli_test_support.hpp"

#include <cstdlib>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace fos::cli::test {
namespace {

#if defined(_WIN32)
int exitStatus(int raw) { return raw; }
#else
int exitStatus(int raw) { return WIFEXITED(raw) ? WEXITSTATUS(raw) : -1; }
#endif

std::string quote(const std::string& s) { return "\"" + s + "\""; }

/// Runs `fos <args>` with stdout redirected to `stdoutFile`; returns the exit code.
int runBinary(const std::string& args, const std::string& stdoutFile) {
    const std::string cmd = quote(FOS_CLI_EXECUTABLE) + " " + args + " > " + quote(stdoutFile) + " 2>&1";
    return exitStatus(std::system(cmd.c_str()));
}

TEST(CliBinary, ExitCodesAndOutput) {
    TempDir d;
    const std::string log = d.file("out.txt");
    EXPECT_EQ(runBinary("--help", log), 0);
    EXPECT_TRUE(contains(readFile(log), "Usage: fos <command>"));
    EXPECT_EQ(runBinary("", log), 2);
    EXPECT_EQ(runBinary("info " + quote(d.file("missing.fosp")), log), 2);
    EXPECT_TRUE(contains(readFile(log), "cannot load"));

    const std::string p = d.file("p.fosp");
    ASSERT_EQ(runBinary("new " + quote(p) + " --template base --width 800 --id " + kTestUuid + " --date " + kTestDate, log), 0);
    EXPECT_EQ(runBinary("validate " + quote(p), log), 0);
    EXPECT_TRUE(contains(readFile(log), "Validation passed"));
    EXPECT_EQ(runBinary("set " + quote(p) + " cab1 width 900", log), 0);
    EXPECT_EQ(runBinary("set " + quote(p) + " cab1 width 10", log), 1); // inner width 10 - 36 < 0: refused
    EXPECT_EQ(runBinary("report " + quote(p) + " --type summary", log), 0);
    EXPECT_TRUE(contains(readFile(log), "900"));
}

} // namespace
} // namespace fos::cli::test
