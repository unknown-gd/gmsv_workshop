#include "gmod_lzma.hpp"

#include "LzmaDec.h"

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>

namespace {

constexpr std::size_t kPropsSize = 5;
constexpr std::size_t kHeaderSize = kPropsSize + 8;
// Reject hostile headers before allocating. Large Workshop GMAs still fit.
constexpr uint64_t kMaxUncompressed = 1024ull * 1024ull * 1024ull;

extern "C" void *sz_alloc(ISzAllocPtr, std::size_t size) {
    return std::malloc(size);
}

extern "C" void sz_free(ISzAllocPtr, void *address) {
    std::free(address);
}

const ISzAlloc kAlloc = {sz_alloc, sz_free};

} // namespace

bool decompress_gmod_lzma(const uint8_t *data,
                          std::size_t size,
                          std::vector<uint8_t> &out,
                          std::string &error) {
    out.clear();

    if (data == nullptr || size < kHeaderSize) {
        error = "compressed data is truncated";
        return false;
    }

    uint64_t dest_len = 0;
    for (int i = 0; i < 8; ++i) {
        dest_len |= static_cast<uint64_t>(data[kPropsSize + i]) << (8 * i);
    }

    if (dest_len == 0 || dest_len > kMaxUncompressed ||
        dest_len > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
        error = "uncompressed size is missing or too large";
        return false;
    }

    try {
        out.assign(static_cast<std::size_t>(dest_len), 0);
    } catch (const std::bad_alloc &) {
        error = "out of memory";
        return false;
    }

    SizeT written = static_cast<SizeT>(dest_len);
    SizeT src_len = static_cast<SizeT>(size - kHeaderSize);
    ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
    const SRes result = LzmaDecode(out.data(),
                                   &written,
                                   data + kHeaderSize,
                                   &src_len,
                                   data,
                                   LZMA_PROPS_SIZE,
                                   LZMA_FINISH_ANY,
                                   &status,
                                   &kAlloc);

    if (result != SZ_OK || written != static_cast<SizeT>(dest_len)) {
        out.clear();
        error = "lzma decompress failed";
        return false;
    }

    return true;
}
