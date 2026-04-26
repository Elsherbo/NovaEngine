#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nova
{
    // Simple RGBA8 image for textures.
    struct ImageRGBA8
    {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba;
    };

    // Loads an image file into RGBA8 using stb_image.
    // Returns false on failure; `errorOut` (if non-null) receives a human-readable reason.
    bool loadImageRGBA8FromFile(const char* path, ImageRGBA8& out, std::string* errorOut = nullptr);

    // Load from an in-memory buffer (used by AssetFS/Pak reads).
    bool loadImageRGBA8FromMemory(const uint8_t* data, size_t size, ImageRGBA8& out, std::string* errorOut = nullptr);
}

