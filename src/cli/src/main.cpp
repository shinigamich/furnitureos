// SPDX-License-Identifier: AGPL-3.0-only
// `fos` executable: forwards to fos::cli::run().
#include "fos/cli/cli.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    const int code = fos::cli::run(args, std::cout, std::cerr);
    std::cout.flush();
    return code;
}
