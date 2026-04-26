#include "engine/core/image_load.h"
#include <vector>
#include <cstdio>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "vendor/stb/stb_image.h"

namespace nova
{
    // We rely on the engine's I/O (IPlatform) so later we can load from archives.
    // For now, the BSP loader calls this with filesystem paths and uses SDL3Platform I/O.
    //
    // This translation unit is intentionally the only one defining STB_IMAGE_IMPLEMENTATION.

    bool loadImageRGBA8FromMemory(const uint8_t* data, size_t size, ImageRGBA8& out, std::string* errorOut)
    {
        out = {};
        if (!data || size == 0)
        {
            if (errorOut) *errorOut = "empty buffer";
            return false;
        }

        int w = 0, h = 0, comp = 0;
        stbi_uc* rgba = stbi_load_from_memory(data, (int)size, &w, &h, &comp, 4);
        if (!rgba || w <= 0 || h <= 0)
        {
            if (errorOut) *errorOut = stbi_failure_reason() ? stbi_failure_reason() : "stbi_load_from_memory failed";
            if (rgba) stbi_image_free(rgba);
            return false;
        }

        out.width = w;
        out.height = h;
        out.rgba.assign(rgba, rgba + (size_t)w * (size_t)h * 4);
        stbi_image_free(rgba);
        return true;
    }

    bool loadImageRGBA8FromFile(const char* path, ImageRGBA8& out, std::string* errorOut)
    {
        out = {};
        if (!path || !path[0])
        {
            if (errorOut) *errorOut = "empty path";
            return false;
        }

        FILE* f = fopen(path, "rb");
        if (!f)
        {
            if (errorOut) *errorOut = "failed to open file";
            return false;
        }

        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0)
        {
            fclose(f);
            if (errorOut) *errorOut = "empty file";
            return false;
        }

        std::vector<uint8_t> bytes((size_t)sz);
        size_t rd = fread(bytes.data(), 1, (size_t)sz, f);
        fclose(f);
        if (rd != (size_t)sz)
        {
            if (errorOut) *errorOut = "short read";
            return false;
        }

        return loadImageRGBA8FromMemory(bytes.data(), bytes.size(), out, errorOut);
    }
}

