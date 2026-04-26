#include "engine/core/asset_fs.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

namespace nova
{
    static uint32_t readU32LE(const uint8_t* p)
    {
        return (uint32_t)p[0]
            | ((uint32_t)p[1] << 8)
            | ((uint32_t)p[2] << 16)
            | ((uint32_t)p[3] << 24);
    }

    void AssetFS::clear()
    {
        m_dirs.clear();
        m_paks.clear();
    }

    std::string AssetFS::normalize(const std::string& p)
    {
        std::string s = p;
        for (char& c : s)
        {
            if (c == '\\') c = '/';
            else c = (char)std::tolower((unsigned char)c);
        }
        // strip leading slashes
        while (!s.empty() && (s[0] == '/' || s[0] == '\\'))
            s.erase(s.begin());
        return s;
    }

    bool AssetFS::fileReadAllBytes(const std::string& path, std::vector<uint8_t>& out)
    {
        out.clear();
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0) { fclose(f); return false; }
        out.resize((size_t)sz);
        size_t rd = fread(out.data(), 1, (size_t)sz, f);
        fclose(f);
        if (rd != (size_t)sz) { out.clear(); return false; }
        return true;
    }

    void AssetFS::mountDirectory(const std::string& rootDir)
    {
        DirMount m;
        m.root = rootDir;
        m_dirs.push_back(std::move(m));
    }

    bool AssetFS::mountQuake2Pak(const std::string& pakPath)
    {
        std::vector<uint8_t> bytes;
        if (!fileReadAllBytes(pakPath, bytes)) return false;
        if (bytes.size() < 12) return false;
        if (std::memcmp(bytes.data(), "PACK", 4) != 0) return false;

        const uint32_t dirOff = readU32LE(bytes.data() + 4);
        const uint32_t dirLen = readU32LE(bytes.data() + 8);
        if ((uint64_t)dirOff + (uint64_t)dirLen > bytes.size()) return false;
        if (dirLen % 64 != 0) return false;

        PakMount mount;
        mount.path = pakPath;

        const uint8_t* dir = bytes.data() + dirOff;
        const uint32_t count = dirLen / 64;
        mount.entries.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint8_t* e = dir + i * 64;
            char nameBuf[57] = {};
            std::memcpy(nameBuf, e, 56);
            nameBuf[56] = 0;
            const uint32_t off = readU32LE(e + 56);
            const uint32_t len = readU32LE(e + 60);

            PakEntry pe;
            pe.name = normalize(nameBuf);
            pe.offset = off;
            pe.length = len;
            mount.entries.push_back(std::move(pe));
        }

        // sort for binary search
        std::sort(mount.entries.begin(), mount.entries.end(),
                  [](const PakEntry& a, const PakEntry& b){ return a.name < b.name; });

        m_paks.push_back(std::move(mount));
        return true;
    }

    bool AssetFS::exists(const std::string& logicalPath) const
    {
        const std::string key = normalize(logicalPath);

        // loose files first (last mount wins)
        for (auto it = m_dirs.rbegin(); it != m_dirs.rend(); ++it)
        {
            std::string full = it->root;
            if (!full.empty() && full.back() != '\\' && full.back() != '/')
                full += "\\";
            full += key;

            FILE* f = fopen(full.c_str(), "rb");
            if (f) { fclose(f); return true; }
        }

        for (auto pit = m_paks.rbegin(); pit != m_paks.rend(); ++pit)
        {
            const auto& entries = pit->entries;
            auto it = std::lower_bound(entries.begin(), entries.end(), key,
                [](const PakEntry& e, const std::string& s){ return e.name < s; });
            if (it != entries.end() && it->name == key)
                return true;
        }
        return false;
    }

    bool AssetFS::readAllBytes(const std::string& logicalPath, std::vector<uint8_t>& out) const
    {
        const std::string key = normalize(logicalPath);

        // loose files first (last mount wins)
        for (auto it = m_dirs.rbegin(); it != m_dirs.rend(); ++it)
        {
            std::string full = it->root;
            if (!full.empty() && full.back() != '\\' && full.back() != '/')
                full += "\\";
            full += key;

            if (fileReadAllBytes(full, out))
                return true;
        }

        // pak mounts (last mount wins)
        for (auto pit = m_paks.rbegin(); pit != m_paks.rend(); ++pit)
        {
            const auto& entries = pit->entries;
            auto it = std::lower_bound(entries.begin(), entries.end(), key,
                [](const PakEntry& e, const std::string& s){ return e.name < s; });
            if (it == entries.end() || it->name != key)
                continue;

            // read slice from pak file
            FILE* f = fopen(pit->path.c_str(), "rb");
            if (!f) return false;
            if (fseek(f, (long)it->offset, SEEK_SET) != 0) { fclose(f); return false; }
            out.resize((size_t)it->length);
            size_t rd = fread(out.data(), 1, (size_t)it->length, f);
            fclose(f);
            if (rd != (size_t)it->length) { out.clear(); return false; }
            return true;
        }

        out.clear();
        return false;
    }
}

