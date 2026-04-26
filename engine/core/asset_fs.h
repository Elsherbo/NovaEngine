#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nova
{
    // Very small "virtual filesystem" for assets:
    // - mountDirectory(): load from loose files (dev)
    // - mountQuake2Pak(): load from Quake2 PAK archives (import/debug)
    //
    // Paths are treated as forward-slash logical paths, e.g. "textures/foo.tga".
    class AssetFS
    {
    public:
        void clear();

        void mountDirectory(const std::string& rootDir);
        bool mountQuake2Pak(const std::string& pakPath);

        bool exists(const std::string& logicalPath) const;
        bool readAllBytes(const std::string& logicalPath, std::vector<uint8_t>& out) const;

    private:
        struct DirMount
        {
            std::string root;
        };

        struct PakEntry
        {
            std::string name; // normalized, lowercase, forward slashes
            uint32_t offset = 0;
            uint32_t length = 0;
        };

        struct PakMount
        {
            std::string path;
            std::vector<PakEntry> entries;
        };

        static std::string normalize(const std::string& p);
        static bool fileReadAllBytes(const std::string& path, std::vector<uint8_t>& out);

        std::vector<DirMount> m_dirs;
        std::vector<PakMount> m_paks;
    };
}

