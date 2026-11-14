// SPDX-License-Identifier: AGPL-3.0-only
// Runs the real `fos` executable as a separate process (path injected by CMake) to check
// that main() forwards arguments, output streams and exit codes.
#include "cli_test_support.hpp"

#include <cstdlib>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#endif

namespace fos::cli::test {
namespace {

std::string quote(const std::string& s) { return "\"" + s + "\""; }

#if defined(_WIN32)
/// Absolute, backslash-separated executable path. Under Wine the CMake-generated path is a
/// Unix path ("/home/..."), which resolves against the current drive (Z: = "/").
std::string executable() {
    const auto u8 = std::filesystem::absolute(FOS_CLI_EXECUTABLE).make_preferred().u8string();
    return std::string(u8.begin(), u8.end());
}

/// Runs a UTF-8 command line through cmd.exe. Two Windows rules matter:
///  * `cmd /c` removes the first and the last quote of a command line that starts with a
///    quote and contains more than two, which mangles `"fos.exe" "arg" > "out"`; an extra
///    outer pair of quotes is what cmd strips instead.
///  * std::system() takes the ANSI code page, so "кухня.fosp" would arrive as "?????.fosp";
///    _wsystem() with the UTF-16 command line passes it exactly.
int runCommandLine(const std::string& utf8) {
    const std::string wrapped = "\"" + utf8 + "\"";
    const int n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, wrapped.data(), static_cast<int>(wrapped.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0)
        ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, wrapped.data(), static_cast<int>(wrapped.size()), wide.data(), n);
    return ::_wsystem(wide.c_str()); // the exit code of cmd.exe, i.e. of the last command
}
#else
std::string executable() { return FOS_CLI_EXECUTABLE; }

int runCommandLine(const std::string& utf8) {
    const int raw = std::system(utf8.c_str());
    return WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
}
#endif

/// Runs `fos <args>` with stdout and stderr redirected to `stdoutFile`; returns the exit code.
int runBinary(const std::string& args, const std::string& stdoutFile) {
    return runCommandLine(quote(executable()) + " " + args + " > " + quote(stdoutFile) + " 2>&1");
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

    // Console output is the same bytes on every platform: stdout is in binary mode on
    // Windows, so "\n" is not expanded to "\r\n" (a CSV piped through a text-mode stdout
    // would get CR CR LF). Files written by the binary keep their exact CRLF.
    EXPECT_EQ(readFile(log).find('\r'), std::string::npos);
    EXPECT_EQ(runBinary("report " + quote(p) + " --type parts --csv " + quote(d.file("parts.csv")), log), 0);
    const std::string csv = readFile(u8path(d.file("parts.csv")));
    EXPECT_NE(csv.find("\r\n"), std::string::npos);
    EXPECT_EQ(csv.find("\r\r"), std::string::npos);
}

TEST(CliBinary, CyrillicFileNameEndToEnd) {
    // A non-ASCII project name through the real process boundary: the argument must reach
    // main() intact (Windows: wide command line, not the ANSI argv), the file must be created
    // under exactly that name, loaded again, modified and saved again.
    TempDir d;
    if (const auto why = nonAsciiFileNameSupport(d.path); !why.empty()) GTEST_SKIP() << why;
    const std::string log = d.file("out.txt");
    const std::string p = d.file("кухня.fosp"); // UTF-8
    ASSERT_EQ(runBinary("new " + quote(p) + " --template base --width 800 --id " + kTestUuid + " --date " + kTestDate, log), 0)
        << readFile(u8path(log));
    EXPECT_TRUE(std::filesystem::is_regular_file(u8path(p)));
    EXPECT_TRUE(std::filesystem::is_regular_file(d.path / std::filesystem::path(u8"кухня.fosp")));
    EXPECT_TRUE(contains(readFile(u8path(log)), "кухня")) << readFile(u8path(log)); // project name = UTF-8 file stem
    EXPECT_EQ(runBinary("set " + quote(p) + " cab1 width 900", log), 0) << readFile(u8path(log));
    EXPECT_EQ(runBinary("validate " + quote(p), log), 0) << readFile(u8path(log));
    EXPECT_EQ(runBinary("report " + quote(p) + " --type summary", log), 0);
    EXPECT_TRUE(contains(readFile(u8path(log)), "900"));
    // Only the expected entries exist (no mojibake twin such as "ÐºÑƒÑ…Ð½Ñ.fosp").
    std::size_t projects = 0;
    for (const auto& e : std::filesystem::directory_iterator(d.path))
        if (e.path().extension() == ".fosp") ++projects;
    EXPECT_EQ(projects, 1u);
}

} // namespace
} // namespace fos::cli::test
