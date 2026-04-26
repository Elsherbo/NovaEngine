#include "engine/core/image_tga.h"

#include <cstring>
#include <algorithm>

namespace nova
{
    static uint16_t readU16(const uint8_t* p)
    {
        return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    }

    bool loadTgaFromMemory(const uint8_t* data, size_t size, TgaImage& out)
    {
        out = {};
        if (!data || size < 18) return false;

        const uint8_t idLen = data[0];
        const uint8_t colorMapType = data[1];
        const uint8_t imageType = data[2];
        const uint16_t cmFirst = readU16(data + 3);
        const uint16_t cmLen = readU16(data + 5);
        const uint8_t cmBpp = data[7];
        (void)cmFirst; (void)cmLen; (void)cmBpp;

        const uint16_t w = readU16(data + 12);
        const uint16_t h = readU16(data + 14);
        const uint8_t bpp = data[16];
        const uint8_t desc = data[17];

        if (colorMapType != 0) return false;                // no paletted support
        if (imageType != 2 && imageType != 10) return false; // truecolor only
        if (w == 0 || h == 0) return false;
        if (bpp != 24 && bpp != 32) return false;

        const bool originTop = (desc & 0x20) != 0;
        const size_t pixelSize = (bpp == 24) ? 3 : 4;

        const size_t headerBytes = 18;
        if (headerBytes + idLen > size) return false;
        size_t offset = headerBytes + idLen;

        const size_t pixelCount = (size_t)w * (size_t)h;
        std::vector<uint8_t> rgba(pixelCount * 4);

        auto writePixel = [&](size_t dstIndex, const uint8_t* src)
        {
            // TGA stores BGR(A)
            rgba[dstIndex + 0] = src[2];
            rgba[dstIndex + 1] = src[1];
            rgba[dstIndex + 2] = src[0];
            rgba[dstIndex + 3] = (pixelSize == 4) ? src[3] : 255;
        };

        if (imageType == 2)
        {
            const size_t needed = pixelCount * pixelSize;
            if (offset + needed > size) return false;

            for (size_t i = 0; i < pixelCount; ++i)
                writePixel(i * 4, data + offset + i * pixelSize);
        }
        else // 10: RLE
        {
            size_t outPix = 0;
            while (outPix < pixelCount)
            {
                if (offset >= size) return false;
                const uint8_t packet = data[offset++];
                const size_t count = (packet & 0x7Fu) + 1u;

                if (packet & 0x80u)
                {
                    // RLE packet: one pixel repeated
                    if (offset + pixelSize > size) return false;
                    const uint8_t* px = data + offset;
                    offset += pixelSize;
                    for (size_t j = 0; j < count && outPix < pixelCount; ++j, ++outPix)
                        writePixel(outPix * 4, px);
                }
                else
                {
                    // Raw packet: count pixels follow
                    const size_t bytes = count * pixelSize;
                    if (offset + bytes > size) return false;
                    for (size_t j = 0; j < count && outPix < pixelCount; ++j, ++outPix)
                        writePixel(outPix * 4, data + offset + j * pixelSize);
                    offset += bytes;
                }
            }
        }

        // Normalize origin to top-left.
        if (!originTop)
        {
            const size_t rowBytes = (size_t)w * 4;
            for (size_t y = 0; y < h / 2; ++y)
            {
                uint8_t* rowA = rgba.data() + y * rowBytes;
                uint8_t* rowB = rgba.data() + (size_t)(h - 1 - y) * rowBytes;
                for (size_t i = 0; i < rowBytes; ++i)
                    std::swap(rowA[i], rowB[i]);
            }
        }

        out.width = (int)w;
        out.height = (int)h;
        out.rgba = std::move(rgba);
        return true;
    }
}

