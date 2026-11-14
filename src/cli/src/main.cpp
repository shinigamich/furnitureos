// SPDX-License-Identifier: AGPL-3.0-only
// `fos` executable: forwards to fos::cli::run().
#include "fos/cli/cli.hpp"

#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <fcntl.h>
#include <io.h>
#include <cwchar>
#include <cstdio>
#endif

namespace {

#ifdef _WIN32
/// The arguments as UTF-8. argv of a narrow main() is in the ANSI code page on Windows, which
/// turns "кухня.fosp" into "?????.fosp" on a Western system; the wide command line is exact.
std::vector<std::string> utf8Arguments() {
    std::vector<std::string> out;
    int count = 0;
    wchar_t** wide = ::CommandLineToArgvW(::GetCommandLineW(), &count);
    if (wide == nullptr) return out;
    for (int i = 1; i < count; ++i) {
        const int length = static_cast<int>(std::wcslen(wide[i]));
        const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide[i], length, nullptr, 0, nullptr, nullptr);
        std::string arg(static_cast<std::size_t>(bytes > 0 ? bytes : 0), '\0');
        if (bytes > 0) ::WideCharToMultiByte(CP_UTF8, 0, wide[i], length, arg.data(), bytes, nullptr, nullptr);
        out.push_back(std::move(arg));
    }
    ::LocalFree(wide);
    return out;
}
#endif

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
#ifdef _WIN32
    (void)argc;
    (void)argv;
    args = utf8Arguments();
    // All output is UTF-8: tell the console so, and write bytes untranslated (a text-mode
    // stream would turn the CRLF of a CSV written to stdout into CR CR LF).
    ::SetConsoleOutputCP(CP_UTF8);
    (void)::_setmode(::_fileno(stdout), _O_BINARY);
    (void)::_setmode(::_fileno(stderr), _O_BINARY);
#else
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
#endif
    const int code = fos::cli::run(args, std::cout, std::cerr);
    std::cout.flush();
    return code;
}
