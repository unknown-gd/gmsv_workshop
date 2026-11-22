#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Garry's Mod util.Compress / Workshop .bin layout:
// 5 byte LZMA properties, 8 byte little-endian uncompressed size, LZMA payload.
bool decompress_gmod_lzma(const uint8_t *data,
                          std::size_t size,
                          std::vector<uint8_t> &out,
                          std::string &error);
