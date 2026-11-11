// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/formats/sha256.hpp"

#include <gtest/gtest.h>

using fos::formats::Sha256;
using fos::formats::sha256Hex;

// Test vectors from NIST FIPS 180-4 / the NIST "SHA-256 example" documents.
TEST(Sha256, NistEmptyMessage) {
    EXPECT_EQ(sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256, NistAbc) {
    EXPECT_EQ(sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256, Nist448BitMessage) {
    // 56 bytes = 448 bits: the padding spills into a second block.
    EXPECT_EQ(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256, NistOneMillionA) {
    EXPECT_EQ(sha256Hex(std::string(1000000, 'a')),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// Padding boundaries; expected values computed with coreutils `sha256sum`.
TEST(Sha256, PaddingBoundaries) {
    EXPECT_EQ(sha256Hex(std::string(55, 'a')), "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    EXPECT_EQ(sha256Hex(std::string(56, 'a')), "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    EXPECT_EQ(sha256Hex(std::string(64, 'a')), "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    EXPECT_EQ(sha256Hex(std::string(119, 'a')), "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb");
}

TEST(Sha256, IncrementalUpdatesMatchOneShot) {
    // One million 'a' fed in uneven chunks (1, 2, 3, ... bytes) must give the NIST digest.
    const std::string data(1000000, 'a');
    Sha256 hasher;
    std::size_t offset = 0, chunk = 1;
    while (offset < data.size()) {
        const std::size_t take = std::min(chunk, data.size() - offset);
        hasher.update(data.data() + offset, take);
        offset += take;
        chunk = chunk % 131 + 1;
    }
    const auto digest = hasher.finish();
    EXPECT_EQ(digest[0], 0xcd);
    EXPECT_EQ(digest[1], 0xc7);
    EXPECT_EQ(digest[30], 0x2c);
    EXPECT_EQ(digest[31], 0xd0);
}

TEST(Sha256, BinaryDataWithZeroBytes) {
    // sha256 of the single byte 0x00 (well-known value).
    EXPECT_EQ(sha256Hex(std::string(1, '\0')), "6e340b9cffb37a989ca544e6bb780a2c78901d3fb33738768511a30617afa01d");
}
