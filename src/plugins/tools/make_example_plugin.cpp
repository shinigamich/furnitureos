// SPDX-License-Identifier: AGPL-3.0-only
// Regenerates the committed library of the example plugin:
//   fos_plugins_make_example <repo>/examples/plugins/org.furnitureos.example-materials
#include "example_materials.hpp"

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: fos_plugins_make_example <example plugin directory>\n";
        return 2;
    }
    const auto file = std::filesystem::path(argv[1]) / std::string(fos::plugins::example::kLibraryRelativePath);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    auto r = fos::plugins::example::writeLibrary(file);
    if (!r.ok()) {
        std::cerr << "error: " << r.error().message << "\n";
        return 1;
    }
    std::cout << "wrote " << r.value().path.string() << " (sha256 " << r.value().checksum << ")\n";
    return 0;
}
