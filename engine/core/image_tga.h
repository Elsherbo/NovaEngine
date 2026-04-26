#pragma once

#include <cstdint>
#include <vector>

namespace nova
{
    struct TgaImage
    {
        int width = 0;
        int height = 0;
        // Always RGBA8, row-major, top-left origin.
        std::vector<uint8_t> rgba;
    };

    // Supports TGA truecolor:
    // - image type 2 (uncompressed) and 10 (RLE)
    // - 24bpp BGR and 32bpp BGRA
    // Output is RGBA8, origin normalized to top-left.
    bool loadTgaFromMemory(const uint8_t* data, size_t size, TgaImage& out);
}

