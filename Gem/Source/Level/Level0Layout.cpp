#include "Level0Layout.h"

#include <algorithm>
#include <cmath>

namespace NoclipProtocol::Level0
{
    namespace
    {
        // Small, fast, platform-independent hashing and RNG, so every machine gets identical maps.
        uint64_t Mix(uint64_t z)
        {
            z += 0x9E3779B97F4A7C15ull;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        uint64_t Hash(uint64_t seed, int64_t a, int64_t b = 0, int64_t c = 0, int64_t d = 0)
        {
            uint64_t h = Mix(seed);
            h = Mix(h ^ static_cast<uint64_t>(a));
            h = Mix(h ^ static_cast<uint64_t>(b));
            h = Mix(h ^ static_cast<uint64_t>(c));
            h = Mix(h ^ static_cast<uint64_t>(d));
            return h;
        }

        float Unit(uint64_t h) // [0, 1)
        {
            return static_cast<float>(h >> 40) / static_cast<float>(1ull << 24);
        }

        class Rng
        {
        public:
            explicit Rng(uint64_t seed) : m_state(seed) {}
            uint64_t Next() { m_state += 0x9E3779B97F4A7C15ull; return Mix(m_state); }
            float NextUnit() { return Unit(Next()); }
            int NextInt(int n) { return n <= 1 ? 0 : static_cast<int>(Next() % static_cast<uint64_t>(n)); }

        private:
            uint64_t m_state;
        };

        int FloorDiv(int a, int b)
        {
            int q = a / b;
            if ((a % b != 0) && ((a < 0) != (b < 0)))
            {
                --q;
            }
            return q;
        }

        int Mod(int a, int b)
        {
            int m = a % b;
            return m < 0 ? m + b : m;
        }

        enum : int64_t
        {
            TagBorderGuaranteed = 11,
            TagBorderExtra = 12,
            TagChunk = 13,
            TagLight = 14,
            TagZone = 15,
            TagDecal = 16,
        };

        float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }
    } // namespace

    bool IsBorderOpen(const Settings& settings, bool vertical, int lineIndex, int cell)
    {
        const int n = settings.m_chunkCells;
        const int segment = FloorDiv(cell, n); // which chunk-side this edge belongs to
        const int local = Mod(cell, n);

        // Two guaranteed doorways per chunk side, at hashed positions (always different).
        const uint64_t h = Hash(settings.m_seed, TagBorderGuaranteed, vertical ? 1 : 0, lineIndex, segment);
        const int first = static_cast<int>(h % static_cast<uint64_t>(n));
        const int second = (first + 1 + static_cast<int>((h >> 32) % static_cast<uint64_t>(std::max(1, n - 1)))) % n;
        if (local == first || local == second)
        {
            return true;
        }
        return Unit(Hash(settings.m_seed, TagBorderExtra, vertical ? 1 : 0, lineIndex, cell)) < settings.m_borderExtraOpen;
    }

    float DarkZoneValue(const Settings& settings, int cellX, int cellY)
    {
        // Smooth value noise over a coarse lattice, so dark zones are connected blobs, not random dark cells.
        const float scale = std::max(1.0f, settings.m_darkZoneScale);
        const float fx = static_cast<float>(cellX) / scale;
        const float fy = static_cast<float>(cellY) / scale;
        const int x0 = static_cast<int>(std::floor(fx));
        const int y0 = static_cast<int>(std::floor(fy));
        const float tx = Smooth(fx - static_cast<float>(x0));
        const float ty = Smooth(fy - static_cast<float>(y0));
        auto lattice = [&](int x, int y) { return Unit(Hash(settings.m_seed, TagZone, x, y)); };
        const float a = lattice(x0, y0) + (lattice(x0 + 1, y0) - lattice(x0, y0)) * tx;
        const float b = lattice(x0, y0 + 1) + (lattice(x0 + 1, y0 + 1) - lattice(x0, y0 + 1)) * tx;
        return a + (b - a) * ty;
    }

    void WorldToChunk(const Settings& settings, float x, float y, int& chunkX, int& chunkY)
    {
        const float chunkSize = settings.m_cellSize * static_cast<float>(settings.m_chunkCells);
        chunkX = static_cast<int>(std::floor(x / chunkSize));
        chunkY = static_cast<int>(std::floor(y / chunkSize));
    }

    Point SpawnPoint(const Settings& settings)
    {
        return Point{ settings.m_cellSize * 0.5f, settings.m_cellSize * 0.5f, 0.0f };
    }

    ChunkLayout GenerateChunk(const Settings& settings, int chunkX, int chunkY)
    {
        const int n = settings.m_chunkCells;
        const float cell = settings.m_cellSize;
        const float thick = settings.m_wallThickness;
        const float height = settings.m_ceilingHeight;
        const int baseX = chunkX * n; // global cell index of this chunk's first column
        const int baseY = chunkY * n;

        Rng rng(Hash(settings.m_seed, TagChunk, chunkX, chunkY));

        // Local wall grids. h[x][y]: wall on line y (0..n) spanning cell x. v[x][y]: wall on line x (0..n) spanning cell y.
        std::vector<std::vector<bool>> h(n, std::vector<bool>(n + 1, true));
        std::vector<std::vector<bool>> v(n + 1, std::vector<bool>(n, true));

        // Borders come from the shared hash, so neighbors agree.
        for (int i = 0; i < n; ++i)
        {
            h[i][0] = !IsBorderOpen(settings, false, baseY, baseX + i);
            h[i][n] = !IsBorderOpen(settings, false, baseY + n, baseX + i);
            v[0][i] = !IsBorderOpen(settings, true, baseX, baseY + i);
            v[n][i] = !IsBorderOpen(settings, true, baseX + n, baseY + i);
        }

        // 1. Perfect maze inside the chunk (every cell reachable from every other).
        std::vector<std::vector<bool>> seen(n, std::vector<bool>(n, false));
        std::vector<std::pair<int, int>> stack;
        const int startX = rng.NextInt(n);
        const int startY = rng.NextInt(n);
        seen[startX][startY] = true;
        stack.emplace_back(startX, startY);
        while (!stack.empty())
        {
            const auto [x, y] = stack.back();
            std::pair<int, int> options[4];
            int count = 0;
            if (x + 1 < n && !seen[x + 1][y]) { options[count++] = { x + 1, y }; }
            if (x - 1 >= 0 && !seen[x - 1][y]) { options[count++] = { x - 1, y }; }
            if (y + 1 < n && !seen[x][y + 1]) { options[count++] = { x, y + 1 }; }
            if (y - 1 >= 0 && !seen[x][y - 1]) { options[count++] = { x, y - 1 }; }
            if (count == 0)
            {
                stack.pop_back();
                continue;
            }
            const auto [nx, ny] = options[rng.NextInt(count)];
            if (nx == x)
            {
                h[x][std::max(y, ny)] = false;
            }
            else
            {
                v[std::max(x, nx)][y] = false;
            }
            seen[nx][ny] = true;
            stack.emplace_back(nx, ny);
        }

        // 2. Knock out a share of the remaining interior walls so it loops.
        std::vector<std::pair<bool, std::pair<int, int>>> interior;
        for (int x = 0; x < n; ++x)
        {
            for (int y = 1; y < n; ++y)
            {
                if (h[x][y]) { interior.push_back({ false, { x, y } }); }
            }
        }
        for (int x = 1; x < n; ++x)
        {
            for (int y = 0; y < n; ++y)
            {
                if (v[x][y]) { interior.push_back({ true, { x, y } }); }
            }
        }
        for (size_t i = interior.size(); i > 1; --i) // Fisher-Yates with our RNG
        {
            std::swap(interior[i - 1], interior[static_cast<size_t>(rng.NextInt(static_cast<int>(i)))]);
        }
        const size_t knockCount = static_cast<size_t>(static_cast<float>(interior.size()) * settings.m_openFraction);
        for (size_t i = 0; i < knockCount; ++i)
        {
            const auto& [isVertical, key] = interior[i];
            (isVertical ? v[key.first][key.second] : h[key.first][key.second]) = false;
        }

        // 3. Sometimes one big open room.
        if (rng.NextUnit() < settings.m_roomChance && n >= 3)
        {
            const int rw = 2 + rng.NextInt(2);
            const int rd = 2 + rng.NextInt(2);
            const int rx = rng.NextInt(std::max(1, n - rw));
            const int ry = rng.NextInt(std::max(1, n - rd));
            for (int x = rx; x < std::min(n, rx + rw); ++x)
            {
                for (int y = ry + 1; y < std::min(n, ry + rd); ++y) { h[x][y] = false; }
            }
            for (int x = rx + 1; x < std::min(n, rx + rw); ++x)
            {
                for (int y = ry; y < std::min(n, ry + rd); ++y) { v[x][y] = false; }
            }
        }

        ChunkLayout layout;
        layout.m_chunkX = chunkX;
        layout.m_chunkY = chunkY;
        const float originX = static_cast<float>(baseX) * cell;
        const float originY = static_cast<float>(baseY) * cell;
        const float chunkSize = static_cast<float>(n) * cell;

        // 4. Walls: one fixed-size segment per cell edge (cell + thickness long), so the wallpaper texture maps 1:1.
        //    Lines y = 0..n-1 and x = 0..n-1 only (the north/east lines are the neighbors').
        for (int y = 0; y < n; ++y)
        {
            for (int x = 0; x < n; ++x)
            {
                if (h[x][y])
                {
                    layout.m_walls.push_back(Box{ originX + (static_cast<float>(x) + 0.5f) * cell, originY + static_cast<float>(y) * cell,
                                                  height * 0.5f, cell + thick, thick, height });
                }
            }
        }
        for (int x = 0; x < n; ++x)
        {
            for (int y = 0; y < n; ++y)
            {
                if (v[x][y])
                {
                    layout.m_walls.push_back(Box{ originX + static_cast<float>(x) * cell, originY + (static_cast<float>(y) + 0.5f) * cell,
                                                  height * 0.5f, thick, cell + thick, height });
                }
            }
        }

        // 5. Pillars on interior corners that no wall touches.
        for (int x = 1; x < n; ++x)
        {
            for (int y = 1; y < n; ++y)
            {
                const bool touching = h[x - 1][y] || h[x][y] || v[x][y - 1] || v[x][y];
                if (!touching && rng.NextUnit() < settings.m_pillarChance)
                {
                    layout.m_pillars.push_back(Box{ originX + static_cast<float>(x) * cell, originY + static_cast<float>(y) * cell, height * 0.5f,
                                                    settings.m_pillarSize, settings.m_pillarSize, height });
                }
            }
        }

        // 6. Floor and ceiling slabs.
        layout.m_floor = Box{ originX + chunkSize * 0.5f, originY + chunkSize * 0.5f, -0.05f, chunkSize, chunkSize, 0.1f };
        layout.m_ceiling = Box{ originX + chunkSize * 0.5f, originY + chunkSize * 0.5f, height + 0.05f, chunkSize, chunkSize, 0.1f };

        // 7. Lights, decided per global cell (independent of chunk generation order).
        //    Lit areas: mostly working, some flickering, a few dead. Dark zones: mostly missing, some dead, rare flicker.
        //    The spawn cell is always lit.
        for (int x = 0; x < n; ++x)
        {
            for (int y = 0; y < n; ++y)
            {
                const int gx = baseX + x;
                const int gy = baseY + y;
                const Point at{ (static_cast<float>(gx) + 0.5f) * cell, (static_cast<float>(gy) + 0.5f) * cell, height };
                if (gx == 0 && gy == 0)
                {
                    layout.m_lights.push_back(Light{ at, LightState::Working });
                    continue;
                }
                const bool dark = DarkZoneValue(settings, gx, gy) > 1.0f - settings.m_darkZoneShare;
                const float roll = Unit(Hash(settings.m_seed, TagLight, gx, gy));
                if (dark)
                {
                    if (roll < 0.05f) { layout.m_lights.push_back(Light{ at, LightState::Flickering }); }
                    else if (roll < 0.35f) { layout.m_lights.push_back(Light{ at, LightState::Dead }); }
                    // otherwise missing: pitch black
                }
                else
                {
                    if (roll < 0.66f) { layout.m_lights.push_back(Light{ at, LightState::Working }); }
                    else if (roll < 0.70f) { layout.m_lights.push_back(Light{ at, LightState::Dying }); }
                    else if (roll < 0.82f) { layout.m_lights.push_back(Light{ at, LightState::Flickering }); }
                    else if (roll < 0.94f) { layout.m_lights.push_back(Light{ at, LightState::Dead }); }
                }
            }
        }

        // 8. Floor stains: wet spots, worn patches, mold (more damp and mold in the dark).
        for (int x = 0; x < n; ++x)
        {
            for (int y = 0; y < n; ++y)
            {
                const int gx = baseX + x;
                const int gy = baseY + y;
                const uint64_t hd = Hash(settings.m_seed, TagDecal, gx, gy);
                const bool dark = DarkZoneValue(settings, gx, gy) > 1.0f - settings.m_darkZoneShare;
                const float chance = settings.m_decalChance * (dark ? 1.6f : 1.0f);
                if (Unit(hd) >= chance)
                {
                    continue;
                }
                const float kind = Unit(Mix(hd ^ 1));
                const DecalType type = kind < (dark ? 0.4f : 0.5f) ? DecalType::Wet
                                     : (kind < (dark ? 0.65f : 0.85f) ? DecalType::Worn : DecalType::Mold);
                const float ox = (Unit(Mix(hd ^ 2)) - 0.5f) * cell * 0.6f;
                const float oy = (Unit(Mix(hd ^ 3)) - 0.5f) * cell * 0.6f;
                layout.m_decals.push_back(Decal{
                    Point{ (static_cast<float>(gx) + 0.5f) * cell + ox, (static_cast<float>(gy) + 0.5f) * cell + oy, 0.0f },
                    Unit(Mix(hd ^ 4)) * 6.2831853f,
                    1.5f + Unit(Mix(hd ^ 5)) * 1.8f,
                    type });
            }
        }

        return layout;
    }
} // namespace NoclipProtocol::Level0
