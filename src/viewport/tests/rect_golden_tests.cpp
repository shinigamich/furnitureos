// SPDX-License-Identifier: AGPL-3.0-only
// Golden regression for RECTANGULAR parts (phase 12, ADR-0008): the GPU buffers of
// fixtures::basicCabinet() must stay bit-identical to those of the phase-11 viewport (box meshes).
// The hash was recorded from the unmodified phase-11 sources. If the fixture changes on purpose,
// re-record (the test prints the actual values).
#include "fos/viewport/scene.hpp"

#include "fos/domain/fixtures.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace fos;

namespace {
std::uint64_t fnv1a(const void* data, std::size_t n, std::uint64_t h = 1469598103934665603ull) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}
} // namespace

TEST(RectangularGolden, SceneBuffersUnchanged) {
    viewport::Scene s;
    s.setModel(domain::fixtures::basicCabinet());
    const auto b = s.buildBuffers();
    std::uint64_t h = fnv1a(b.vertices.data(), b.vertices.size() * sizeof(viewport::MeshVertex));
    h = fnv1a(b.indices.data(), b.indices.size() * sizeof(std::uint32_t), h);
    h = fnv1a(b.lines.data(), b.lines.size() * sizeof(viewport::LineVertex), h);
    std::printf("viewport golden: v=%zu i=%zu l=%zu hash=%llu\n", b.vertices.size(), b.indices.size(),
                b.lines.size(), static_cast<unsigned long long>(h));
    // 6 box parts: 24 vertices, 36 indices, 24 line vertices each.
    EXPECT_EQ(b.vertices.size(), 144u);
    EXPECT_EQ(b.indices.size(), 216u);
    EXPECT_EQ(b.lines.size(), 144u);
    EXPECT_EQ(h, 10982770568126127723ull);
}
