#include "gmod_lzma.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// util.Compress layout produced from GMAD + 8 zero bytes + "workshop-test".
constexpr uint8_t kCompressed[] = {
    93, 0, 0, 1, 0, 25, 0, 0, 0, 0, 0, 0, 0, 0,  35, 147, 68, 37, 200, 141, 218, 206,
    117, 116, 30, 192, 134, 136, 19, 24, 187, 152, 112, 147, 13, 163, 91, 163, 255, 251, 3, 0, 0,
};

constexpr uint8_t kPlain[] = {
    'G', 'M', 'A', 'D', 0, 0, 0, 0, 0, 0, 0, 0, 'w', 'o', 'r', 'k',
    's', 'h', 'o', 'p', '-', 't', 'e', 's', 't',
};

bool expect(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "lzma_roundtrip: %s\n", message);
        return false;
    }
    return true;
}

} // namespace

int main() {
    std::vector<uint8_t> out;
    std::string error;
    if (!expect(decompress_gmod_lzma(kCompressed, sizeof(kCompressed), out, error), error.c_str())) {
        return 1;
    }
    if (!expect(out.size() == sizeof(kPlain), "unexpected decompressed size")) {
        return 1;
    }
    if (!expect(std::memcmp(out.data(), kPlain, sizeof(kPlain)) == 0, "unexpected decompressed bytes")) {
        return 1;
    }

    const uint8_t huge[] = {93, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    if (!expect(!decompress_gmod_lzma(huge, sizeof(huge), out, error), "huge size was accepted")) {
        return 1;
    }
    if (!expect(out.empty(), "failed decompress left a buffer")) {
        return 1;
    }

    if (!expect(!decompress_gmod_lzma(kCompressed, 4, out, error), "truncated input was accepted")) {
        return 1;
    }

    std::puts("lzma_roundtrip: ok");
    return 0;
}
