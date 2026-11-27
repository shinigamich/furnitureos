// SPDX-License-Identifier: AGPL-3.0-only
// Raw DEFLATE decoder (RFC 1951), structured after Mark Adler's public-domain "puff"
// reference decoder: canonical Huffman tables decoded bit by bit. Speed is secondary here
// (an update archive is decoded once); correctness checks are not.
#include "fos/app/update_zip.hpp"

#include <array>

namespace fos::app {
namespace {

using core::ErrorCode;
using core::makeError;

constexpr int kMaxBits = 15;

struct DecodeError {
    const char* what;
};

class Inflater {
public:
    Inflater(const QByteArray& in, qint64 expected) : in_(in), expected_(expected) {
        out_.reserve(static_cast<qsizetype>(expected));
    }

    // Returns nullptr on success, otherwise a description of the defect.
    const char* run() {
        try {
            int last = 0;
            do {
                last = bits(1);
                switch (bits(2)) {
                case 0: stored(); break;
                case 1: fixed(); break;
                case 2: dynamic(); break;
                default: return "invalid block type 3";
                }
            } while (!last);
        } catch (const DecodeError& e) {
            return e.what;
        }
        return nullptr;
    }
    QByteArray& output() { return out_; }

private:
    struct Huffman {
        std::array<short, kMaxBits + 1> count{};
        std::array<short, 288> symbol{};
    };

    int bits(int need) {
        long val = bitbuf_;
        while (bitcnt_ < need) {
            if (pos_ >= in_.size()) throw DecodeError{"compressed data ends early"};
            val |= static_cast<long>(static_cast<unsigned char>(in_[pos_++])) << bitcnt_;
            bitcnt_ += 8;
        }
        bitbuf_ = val >> need;
        bitcnt_ -= need;
        return static_cast<int>(val & ((1L << need) - 1));
    }

    void put(char c) {
        if (out_.size() >= expected_) throw DecodeError{"data is larger than the declared size"};
        out_.append(c);
    }

    void stored() {
        bitbuf_ = 0;
        bitcnt_ = 0;
        if (pos_ + 4 > in_.size()) throw DecodeError{"compressed data ends early"};
        const auto byte = [this](qsizetype i) { return static_cast<unsigned>(static_cast<unsigned char>(in_[i])); };
        const unsigned len = byte(pos_) | (byte(pos_ + 1) << 8);
        const unsigned nlen = byte(pos_ + 2) | (byte(pos_ + 3) << 8);
        pos_ += 4;
        if (len != (~nlen & 0xffffu)) throw DecodeError{"stored block length check failed"};
        if (pos_ + len > in_.size()) throw DecodeError{"compressed data ends early"};
        if (out_.size() + len > expected_) throw DecodeError{"data is larger than the declared size"};
        out_.append(in_.constData() + pos_, len);
        pos_ += len;
    }

    // Builds the canonical code; returns 0 for complete, > 0 incomplete, < 0 over-subscribed.
    static int construct(Huffman& h, const short* length, int n) {
        h.count.fill(0);
        for (int sym = 0; sym < n; ++sym) h.count[static_cast<std::size_t>(length[sym])]++;
        if (h.count[0] == n) return 0; // no codes: complete but decoding fails
        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len) {
            left <<= 1;
            left -= h.count[static_cast<std::size_t>(len)];
            if (left < 0) return left;
        }
        std::array<short, kMaxBits + 1> offs{};
        for (int len = 1; len < kMaxBits; ++len)
            offs[static_cast<std::size_t>(len + 1)] =
                static_cast<short>(offs[static_cast<std::size_t>(len)] + h.count[static_cast<std::size_t>(len)]);
        for (int sym = 0; sym < n; ++sym)
            if (length[sym] != 0)
                h.symbol[static_cast<std::size_t>(offs[static_cast<std::size_t>(length[sym])]++)] = static_cast<short>(sym);
        return left;
    }

    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            code |= bits(1);
            const int count = h.count[static_cast<std::size_t>(len)];
            if (code - count < first) return h.symbol[static_cast<std::size_t>(index + (code - first))];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        throw DecodeError{"invalid Huffman code"};
    }

    void codes(const Huffman& lencode, const Huffman& distcode) {
        static constexpr std::array<short, 29> lbase{3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                     31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static constexpr std::array<short, 29> lext{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static constexpr std::array<short, 30> dbase{1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                                     33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                                     1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static constexpr std::array<short, 30> dext{0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        while (true) {
            int symbol = decode(lencode);
            if (symbol < 256) {
                put(static_cast<char>(symbol));
            } else if (symbol == 256) {
                return;
            } else {
                symbol -= 257;
                if (symbol >= 29) throw DecodeError{"invalid length symbol"};
                const int len = lbase[static_cast<std::size_t>(symbol)] + bits(lext[static_cast<std::size_t>(symbol)]);
                symbol = decode(distcode);
                if (symbol >= 30) throw DecodeError{"invalid distance symbol"};
                const qsizetype dist = dbase[static_cast<std::size_t>(symbol)] + bits(dext[static_cast<std::size_t>(symbol)]);
                if (dist > out_.size()) throw DecodeError{"distance points before the start of the data"};
                for (int i = 0; i < len; ++i) put(out_.at(out_.size() - dist));
            }
        }
    }

    void fixed() {
        if (!fixedBuilt_) {
            std::array<short, 288> lengths{};
            int sym = 0;
            for (; sym < 144; ++sym) lengths[static_cast<std::size_t>(sym)] = 8;
            for (; sym < 256; ++sym) lengths[static_cast<std::size_t>(sym)] = 9;
            for (; sym < 280; ++sym) lengths[static_cast<std::size_t>(sym)] = 7;
            for (; sym < 288; ++sym) lengths[static_cast<std::size_t>(sym)] = 8;
            construct(fixedLen_, lengths.data(), 288);
            for (sym = 0; sym < 30; ++sym) lengths[static_cast<std::size_t>(sym)] = 5;
            construct(fixedDist_, lengths.data(), 30);
            fixedBuilt_ = true;
        }
        codes(fixedLen_, fixedDist_);
    }

    void dynamic() {
        static constexpr std::array<short, 19> order{16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        const int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
        if (nlen > 286 || ndist > 30) throw DecodeError{"bad dynamic block counts"};
        std::array<short, 320> lengths{};
        for (int i = 0; i < ncode; ++i) lengths[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])] = static_cast<short>(bits(3));
        Huffman lencode, distcode;
        if (construct(lencode, lengths.data(), 19) != 0) throw DecodeError{"incomplete code-length code"};
        int index = 0;
        while (index < nlen + ndist) {
            int symbol = decode(lencode);
            if (symbol < 16) {
                lengths[static_cast<std::size_t>(index++)] = static_cast<short>(symbol);
                continue;
            }
            short len = 0;
            if (symbol == 16) {
                if (index == 0) throw DecodeError{"repeat with no previous length"};
                len = lengths[static_cast<std::size_t>(index - 1)];
                symbol = 3 + bits(2);
            } else if (symbol == 17) {
                symbol = 3 + bits(3);
            } else {
                symbol = 11 + bits(7);
            }
            if (index + symbol > nlen + ndist) throw DecodeError{"too many code lengths"};
            while (symbol--) lengths[static_cast<std::size_t>(index++)] = len;
        }
        if (lengths[256] == 0) throw DecodeError{"no end-of-block code"};
        int err = construct(lencode, lengths.data(), nlen);
        if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) throw DecodeError{"bad literal/length code"};
        err = construct(distcode, lengths.data() + nlen, ndist);
        if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) throw DecodeError{"bad distance code"};
        codes(lencode, distcode);
    }

    const QByteArray& in_;
    qint64 expected_;
    QByteArray out_;
    qsizetype pos_ = 0;
    long bitbuf_ = 0;
    int bitcnt_ = 0;
    bool fixedBuilt_ = false;
    Huffman fixedLen_, fixedDist_;
};

} // namespace

core::Result<QByteArray> inflateRaw(const QByteArray& compressed, qint64 expectedSize) {
    if (expectedSize < 0) return makeError(ErrorCode::InvalidArgument, "Negative expected size for DEFLATE data.");
    Inflater inflater(compressed, expectedSize);
    if (const char* defect = inflater.run())
        return makeError(ErrorCode::CorruptData, std::string("The compressed data is damaged: ") + defect + ".");
    if (inflater.output().size() != expectedSize)
        return makeError(ErrorCode::CorruptData, "The compressed data decodes to " + std::to_string(inflater.output().size()) +
                                                     " bytes instead of " + std::to_string(expectedSize) + ".");
    return std::move(inflater.output());
}

} // namespace fos::app
