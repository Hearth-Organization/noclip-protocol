#include "Level0Layout.h"
#include "ClutterCatalog.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

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
            TagSmallRoom = 17,
            TagSpawnLight = 18,
            TagProp = 19,
            TagNoclip = 20,
            TagPickup = 21,
            TagClutter = 22,
            TagExit = 23,
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

    void ApplyStyle(Settings& settings)
    {
        if (settings.m_style == 1)
        {
            // Level 1: a huge, damp concrete warehouse / parking garage. Big 8 m bays, a column on almost every
            // corner, few walls (concrete partitions), high ceiling, long strip lights, crates stacked about.
            settings.m_chunkCells = 6;
            settings.m_cellSize = 8.0f;
            settings.m_ceilingHeight = 5.0f;
            settings.m_wallThickness = 0.3f;
            settings.m_openFraction = 0.8f;
            settings.m_borderExtraOpen = 0.5f;
            settings.m_roomChance = 0.5f;
            settings.m_pillarChance = 0.9f;
            settings.m_pillarSize = 0.9f;
            settings.m_spawnRoomRadius = 1;
            settings.m_darkZoneScale = 3.0f;
            settings.m_decalChance = 0.45f;
            settings.m_lightSpacing = 1;
            settings.m_propChance = 0.2f;
            settings.m_clutterChance = 0.65f;
            settings.m_scatterChance = 0.3f;
        }
        else if (settings.m_style == 3)
        {
            // The intro facility: one chunk big enough to hold it, nothing generated.
            settings.m_chunkCells = 1;
            settings.m_cellSize = 64.0f;
            settings.m_ceilingHeight = 3.2f;
            settings.m_wallThickness = 0.3f;
            settings.m_spawnRoomRadius = 0;
            settings.m_noclipChance = 0.0f;
            settings.m_lightSpacing = 1;
            settings.m_propChance = 0.0f;
            settings.m_clutterChance = 0.0f;
            settings.m_scatterChance = 0.0f;
        }
        else if (settings.m_style == 2)
        {
            settings.m_pickupChance = 0.03f; // small cells: fewer per cell
            // Level 2: cramped maintenance tunnels. 3 m cells, almost no loops, low ceiling, pipes along the walls,
            // a caged bulb every other cell at most.
            settings.m_chunkCells = 8;
            settings.m_cellSize = 3.0f;
            settings.m_ceilingHeight = 2.6f;
            settings.m_wallThickness = 0.25f;
            settings.m_openFraction = 0.06f;
            settings.m_borderExtraOpen = 0.1f;
            settings.m_roomChance = 0.12f;
            settings.m_pillarChance = 0.0f;
            settings.m_spawnRoomRadius = 1;
            settings.m_darkZoneScale = 4.0f;
            settings.m_decalChance = 0.35f;
            settings.m_lightSpacing = 2;
            settings.m_propChance = 0.65f;
            settings.m_clutterChance = 0.5f;
            settings.m_scatterChance = 0.4f;
        }
    }

    void ApplyCatalog(Settings& settings, int number, int layout, int light)
    {
        static const int nearest[7] = { 0, 1, 2, 0, 1, 0, 2 };
        settings.m_catalog = number;
        settings.m_layout = layout < 0 || layout > 6 ? 0 : layout;
        settings.m_light = light < 0 || light > 4 ? 1 : light;
        settings.m_style = nearest[settings.m_layout];
        ApplyStyle(settings);
        settings.m_spawnRoomRadius = 1;
        settings.m_wallHeight = 0.0f;
        settings.m_propChance = 0.0f; // the hand-made levels' crates and pipes need their own pieces
        switch (settings.m_layout)
        {
        case 0: // rooms: Level 0's grid
            settings.m_chunkCells = 6; settings.m_cellSize = 4.0f; settings.m_ceilingHeight = 3.0f;
            break;
        case 1: // halls: Level 1's bays
            settings.m_chunkCells = 6; settings.m_cellSize = 8.0f; settings.m_ceilingHeight = 5.0f;
            break;
        case 2: // corridors: Level 2's tunnels
            settings.m_chunkCells = 8; settings.m_cellSize = 3.0f; settings.m_ceilingHeight = 2.6f;
            break;
        case 3: // outdoor: open ground, low fences and hedges, a few big blocks (sheds, trees, rocks), the sky far overhead
            settings.m_chunkCells = 6; settings.m_cellSize = 10.0f; settings.m_ceilingHeight = 8.0f; settings.m_wallHeight = 2.4f;
            settings.m_spawnRoomRadius = 0;
            settings.m_openFraction = 1.0f; settings.m_borderExtraOpen = 1.0f; settings.m_roomChance = 0.0f;
            settings.m_pillarChance = 0.3f; settings.m_pillarSize = 2.6f; settings.m_lightSpacing = 1;
            settings.m_decalChance = 0.0f; settings.m_clutterChance = 0.0f; settings.m_scatterChance = 0.0f; // no Backrooms junk outside
            break;
        case 4: // caves: wide winding tunnels, thick rock columns
            settings.m_chunkCells = 6; settings.m_cellSize = 5.0f; settings.m_ceilingHeight = 4.5f;
            settings.m_openFraction = 0.3f; settings.m_borderExtraOpen = 0.2f; settings.m_roomChance = 0.3f;
            settings.m_pillarChance = 0.85f; settings.m_pillarSize = 1.6f; settings.m_lightSpacing = 2;
            settings.m_clutterChance = 0.2f;
            break;
        case 5: // water: wide flooded rooms and pools
            settings.m_chunkCells = 6; settings.m_cellSize = 6.0f; settings.m_ceilingHeight = 3.5f;
            settings.m_openFraction = 0.6f; settings.m_borderExtraOpen = 0.4f; settings.m_roomChance = 0.6f;
            settings.m_pillarChance = 0.5f; settings.m_pillarSize = 0.7f; settings.m_decalChance = 0.0f;
            settings.m_clutterChance = 0.1f; settings.m_scatterChance = 0.05f;
            break;
        case 6: // stairs and shafts: narrow and very tall
            settings.m_chunkCells = 8; settings.m_cellSize = 3.0f; settings.m_ceilingHeight = 7.0f;
            settings.m_openFraction = 0.08f; settings.m_roomChance = 0.1f;
            break;
        }
        // Light: how much of the level sits in dead zones, and how many lights work where it doesn't.
        static const float dark[5] = { 0.05f, 0.3f, 0.5f, 0.7f, 1.0f };
        static const float working[5] = { 0.95f, 0.75f, 0.55f, 0.4f, 0.0f };
        settings.m_darkZoneShare = dark[settings.m_light];
        settings.m_workingLights = (settings.m_workingLights / 0.66f < 1.0f ? settings.m_workingLights / 0.66f : 1.0f) * working[settings.m_light];
    }

    Point SpawnPoint(const Settings& settings)
    {
        if (settings.m_style == 3)
        {
            return Point{ Facility::StartX, Facility::StartY, 0.0f };
        }
        return Point{ settings.m_cellSize * 0.5f, settings.m_cellSize * 0.5f, 0.0f };
    }

    namespace
    {
        // Breach 4-A's frame: two hazard-striped columns and a lintel around a 3 m wide hole, facing along Y.
        void AddBreachFrame(ChunkLayout& layout, float cx, float cy)
        {
            layout.m_frames.push_back(Box{ cx - 1.7f, cy, 1.45f, 0.4f, 0.6f, 2.9f });
            layout.m_frames.push_back(Box{ cx + 1.7f, cy, 1.45f, 0.4f, 0.6f, 2.9f });
            layout.m_frames.push_back(Box{ cx, cy, 3.03f, 3.8f, 0.6f, 0.26f });
        }

        // Site-[REDACTED], Sublevel 4: a corridor from the lift, the briefing room with the terminal off its east side,
        // and the breach chamber at the far end where Breach 4-A hangs in its frame.
        ChunkLayout GenerateFacility(const Settings& settings, int chunkX, int chunkY)
        {
            ChunkLayout layout;
            layout.m_chunkX = chunkX;
            layout.m_chunkY = chunkY;
            if (chunkX != 0 || chunkY != 0)
            {
                layout.m_hasSlabs = false;
                return layout;
            }
            const float h = settings.m_ceilingHeight;
            const float t = settings.m_wallThickness;
            auto wall = [&](float x0, float y0, float x1, float y1)
            {
                const bool alongX = std::fabs(y1 - y0) < 0.01f;
                layout.m_walls.push_back(alongX
                    ? Box{ (x0 + x1) * 0.5f, y0, h * 0.5f, std::fabs(x1 - x0) + t, t, h }
                    : Box{ x0, (y0 + y1) * 0.5f, h * 0.5f, t, std::fabs(y1 - y0) + t, h });
            };
            // Corridor x 8..12, y 4..32 (door to the briefing room at y 15..17).
            wall(8, 4, 8, 32);
            wall(12, 4, 12, 15);
            wall(12, 17, 12, 32);
            wall(8, 4, 12, 4);
            // Briefing room x 12..20, y 12..20.
            wall(12, 12, 20, 12);
            wall(12, 20, 20, 20);
            wall(20, 12, 20, 20);
            // Breach chamber x 4..16, y 32..44, open to the corridor at x 8..12.
            wall(4, 32, 8, 32);
            wall(12, 32, 16, 32);
            wall(4, 32, 4, 44);
            wall(16, 32, 16, 44);
            wall(4, 44, 16, 44);
            // Columns at the chamber's corners (hazard bands, like the Foundation marks a containment area).
            for (float cx : { 4.6f, 15.4f })
            {
                for (float cy : { 32.6f, 43.4f })
                {
                    layout.m_pillars.push_back(Box{ cx, cy, h * 0.5f, 0.7f, 0.7f, h });
                }
            }
            // Strip lights: down the corridor, two in the briefing room, four in the chamber (one of them failing).
            for (float y = 6.0f; y <= 30.0f; y += 4.0f)
            {
                layout.m_lights.push_back(Light{ Point{ 10.0f, y, h }, LightState::Working });
            }
            layout.m_lights.push_back(Light{ Point{ 16.0f, 14.0f, h }, LightState::Working });
            layout.m_lights.push_back(Light{ Point{ 16.0f, 18.0f, h }, LightState::Working });
            layout.m_lights.push_back(Light{ Point{ 7.0f, 36.0f, h }, LightState::Working });
            layout.m_lights.push_back(Light{ Point{ 13.0f, 36.0f, h }, LightState::Working });
            layout.m_lights.push_back(Light{ Point{ 7.0f, 42.0f, h }, LightState::Working });
            layout.m_lights.push_back(Light{ Point{ 13.0f, 42.0f, h }, LightState::Flickering });
            // The terminal (a prop: a kiosk), and the breach (a noclip spot that stays sealed until the briefing is accepted).
            layout.m_props.push_back(Box{ Facility::TerminalX + 0.1f, Facility::TerminalY, 0.6f, 0.5f, 0.9f, 1.2f });
            layout.m_screens.push_back(Box{ Facility::TerminalX - 0.18f, Facility::TerminalY, 1.42f, 0.04f, 0.78f, 0.46f });
            layout.m_noclipSpots.push_back(Point{ Facility::BreachX, Facility::BreachY, 0.0f });
            AddBreachFrame(layout, Facility::BreachX, Facility::BreachY);
            // Slabs covering the whole facility.
            layout.m_floor = Box{ 12.0f, 24.0f, -0.05f, 24.0f, 48.0f, 0.1f };
            layout.m_ceiling = Box{ 12.0f, 24.0f, h + 0.05f, 24.0f, 48.0f, 0.1f };
            return layout;
        }
    } // namespace

    ChunkLayout GenerateChunk(const Settings& settings, int chunkX, int chunkY, int shift)
    {
        if (settings.m_style == 3)
        {
            return GenerateFacility(settings, chunkX, chunkY);
        }
        const int n = settings.m_chunkCells;
        const float cell = settings.m_cellSize;
        const float thick = settings.m_wallThickness;
        const float height = settings.m_ceilingHeight;
        // Outdoors the walls are fences and hedges and the pillars are sheds and trees, under a far-off sky.
        const float wallHeight = settings.m_wallHeight > 0.0f ? settings.m_wallHeight : height;
        const float pillarHeight = settings.m_wallHeight > 0.0f ? settings.m_wallHeight * 2.2f : height;
        const int baseX = chunkX * n; // global cell index of this chunk's first column
        const int baseY = chunkY * n;

        // A shifted chunk draws its interior from a new stream; shift 0 keeps the original layout exactly.
        Rng rng(shift == 0 ? Hash(settings.m_seed, TagChunk, chunkX, chunkY) : Hash(settings.m_seed, TagChunk, chunkX, chunkY, shift));

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

        // 3a. Small rooms dotted through the maze: up to four tries per chunk, each one coming true with the
        //     open room chance (0.35 gives about 1.4 small rooms per chunk). 2 x 2 or 2 x 3 cells (8 x 8 m or 8 x 12 m),
        //     clear inside but still walled round by the maze, so they feel like rooms you find, not holes.
        //     Their own random stream, so the rest of the chunk doesn't change when this does.
        {
            Rng roomRng(shift == 0 ? Hash(settings.m_seed, TagSmallRoom, chunkX, chunkY) : Hash(settings.m_seed, TagSmallRoom, chunkX, chunkY, shift));
            for (int attempt = 0; attempt < 4 && n >= 3; ++attempt)
            {
                const bool make = roomRng.NextUnit() < settings.m_roomChance;
                const bool wide = roomRng.NextUnit() < 0.5f;
                const bool long3 = roomRng.NextUnit() < 0.4f;
                const int rw = wide && long3 ? 3 : 2;
                const int rd = !wide && long3 ? 3 : 2;
                const int rx = roomRng.NextInt(std::max(1, n - rw + 1));
                const int ry = roomRng.NextInt(std::max(1, n - rd + 1));
                if (!make)
                {
                    continue;
                }
                for (int x = rx; x < std::min(n, rx + rw); ++x)
                {
                    for (int y = ry + 1; y < std::min(n, ry + rd); ++y) { h[x][y] = false; }
                }
                for (int x = rx + 1; x < std::min(n, rx + rw); ++x)
                {
                    for (int y = ry; y < std::min(n, ry + rd); ++y) { v[x][y] = false; }
                }
            }
        }

        // 3b. The spawn room (the players' first base): a big open room centered on the spawn cell, spanning the
        //     four chunks around the origin. Walled all round, with two doorways side by side in the north
        //     wall, so the back of the room is the safe end to build in. A hallway loops around the outside of it, so every part of the
        //     maze that used to connect through the room still connects. Both chunks sharing a border apply
        //     the same rule, so they agree.
        const int r = settings.m_spawnRoomRadius;
        auto inRoom = [r](int gx, int gy) { return gx >= -r && gx <= r && gy >= -r && gy <= r; };
        auto inRing = [r, &inRoom](int gx, int gy)
        { return !inRoom(gx, gy) && gx >= -r - 1 && gx <= r + 1 && gy >= -r - 1 && gy <= r + 1; };
        // The edge between two side-by-side cells: true = wall, false = open, -1 = leave as the maze made it.
        auto spawnRule = [&](int ax, int ay, int bx, int by) -> int
        {
            const bool a = inRoom(ax, ay);
            const bool b = inRoom(bx, by);
            if (a && b)
            {
                return 0;
            }
            if (a != b)
            {
                // Both doorways in the north wall, one cell in from each side (solid wall between them).
                const int doorX = std::max(1, r - 1);
                const bool northEdge = (ay == r && by == r + 1) || (ay == r + 1 && by == r);
                const bool door = ax == bx && (ax == doorX || ax == -doorX) && northEdge;
                return door ? 0 : 1;
            }
            if (inRing(ax, ay) && inRing(bx, by))
            {
                return 0;
            }
            return -1;
        };
        if (r > 0)
        {
            for (int x = 0; x < n; ++x)
            {
                for (int y = 0; y <= n; ++y)
                {
                    // h[x][y] separates cells (x, y - 1) and (x, y)
                    const int rule = spawnRule(baseX + x, baseY + y - 1, baseX + x, baseY + y);
                    if (rule >= 0) { h[x][y] = rule == 1; }
                }
            }
            for (int x = 0; x <= n; ++x)
            {
                for (int y = 0; y < n; ++y)
                {
                    // v[x][y] separates cells (x - 1, y) and (x, y)
                    const int rule = spawnRule(baseX + x - 1, baseY + y, baseX + x, baseY + y);
                    if (rule >= 0) { v[x][y] = rule == 1; }
                }
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
                                                  wallHeight * 0.5f, cell + thick, thick, wallHeight });
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
                                                  wallHeight * 0.5f, thick, cell + thick, wallHeight });
                }
            }
        }

        // 5. Pillars on interior corners that no wall touches.
        for (int x = 1; x < n; ++x)
        {
            for (int y = 1; y < n; ++y)
            {
                const bool touching = h[x - 1][y] || h[x][y] || v[x][y - 1] || v[x][y];
                const int gx = baseX + x;
                const int gy = baseY + y;
                const bool roomInside = inRoom(gx - 1, gy - 1) && inRoom(gx, gy - 1) && inRoom(gx - 1, gy) && inRoom(gx, gy);
                const float pillarRoll = rng.NextUnit(); // always drawn, so the rest of the chunk stays the same
                if (!touching && !roomInside && pillarRoll < settings.m_pillarChance)
                {
                    layout.m_pillars.push_back(Box{ originX + static_cast<float>(x) * cell, originY + static_cast<float>(y) * cell, pillarHeight * 0.5f,
                                                    settings.m_pillarSize, settings.m_pillarSize, pillarHeight });
                }
            }
        }

        // 6. Floor and ceiling slabs.
        layout.m_floor = Box{ originX + chunkSize * 0.5f, originY + chunkSize * 0.5f, -0.05f, chunkSize, chunkSize, 0.1f };
        layout.m_ceiling = Box{ originX + chunkSize * 0.5f, originY + chunkSize * 0.5f, height + 0.05f, chunkSize, chunkSize, 0.1f };

        // 6b. Level 0's end of Breach 4-A: the hole the Foundation punched through the spawn room's south wall.
        if (settings.m_style == 0 && settings.m_catalog < 0 && settings.m_spawnRoomRadius > 0 && Level0Breach::X >= originX && Level0Breach::X < originX + chunkSize &&
            Level0Breach::Y >= originY && Level0Breach::Y < originY + chunkSize)
        {
            AddBreachFrame(layout, Level0Breach::X, Level0Breach::Y);
            // Sealed: the generator draws its glowing seam (it opens a crack for a Foundation delivery).
        }

        // 7. Lights, decided per global cell (independent of chunk generation order).
        //    Lit areas: mostly working, some flickering, a few dead. Dark zones: mostly missing, some dead, rare flicker.
        //    The spawn room is mostly lit: the cells around spawn always work, elsewhere some lights are out.
        //    Working lights: 0.66 is the original balance. Fewer working lights (ray tracing) turns the difference
        //    mostly into dead fixtures and a bit more flicker; the spawn room loses some too. Dark zones don't change.
        const float share = std::clamp(settings.m_lightShare, 0.1f, 1.0f);
        const float working = std::clamp(settings.m_workingLights, 0.05f, 0.9f) * share;
        const float fewer = std::max(0.0f, 0.66f - working);
        const float litFlicker = std::min(0.94f, working + 0.16f + fewer * 0.25f);
        const float spawnDead = 1.0f - (1.0f - std::min(0.9f, 0.20f + fewer * 0.5f)) * share;
        const float spawnFlicker = spawnDead + 0.06f + fewer * 0.1f;
        for (int x = 0; x < n; ++x)
        {
            for (int y = 0; y < n; ++y)
            {
                const int gx = baseX + x;
                const int gy = baseY + y;
                const Point at{ (static_cast<float>(gx) + 0.5f) * cell, (static_cast<float>(gy) + 0.5f) * cell, height };
                if (settings.m_lightSpacing > 1 && Mod(gx + gy, settings.m_lightSpacing) != 0 && !(gx == 0 && gy == 0))
                {
                    continue; // no fixture in this cell (Level 2's bulbs are spread out)
                }
                if (inRoom(gx, gy))
                {
                    // Mostly lit, but it has been here a while: about one light in five is dead and a few flicker.
                    // The spawn cell and the cells next to it always work, so you never start in the dark.
                    const bool corner = r > 0 && gx == r && gy == r;
                    const bool nearSpawn = std::abs(gx) + std::abs(gy) <= 1;
                    const float spawnRoll = Unit(Hash(settings.m_seed, TagSpawnLight, gx, gy));
                    LightState state = LightState::Working;
                    if (corner) { state = LightState::Flickering; }
                    else if (!nearSpawn && spawnRoll < spawnDead) { state = LightState::Dead; }
                    else if (!nearSpawn && spawnRoll < spawnFlicker) { state = LightState::Flickering; }
                    layout.m_lights.push_back(Light{ at, state });
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
                    if (roll < working) { layout.m_lights.push_back(Light{ at, LightState::Working }); }
                    else if (roll < working + 0.04f) { layout.m_lights.push_back(Light{ at, LightState::Dying }); }
                    else if (roll < litFlicker) { layout.m_lights.push_back(Light{ at, LightState::Flickering }); }
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
                    (1.5f + Unit(Mix(hd ^ 5)) * 1.8f) * (settings.m_style == 1 ? 1.6f : 1.0f), // Level 1: big puddles
                    type });
            }
        }

        // 9. Props. Their own random stream per cell or wall, so they never shift the rest of the layout.
        if (settings.m_style == 1 && settings.m_propChance > 0.0f)
        {
            // Crates and pallets of crates standing about the bays, off the middle so the strip lights stay clear.
            for (int x = 0; x < n; ++x)
            {
                for (int y = 0; y < n; ++y)
                {
                    const int gx = baseX + x;
                    const int gy = baseY + y;
                    if (inRoom(gx, gy))
                    {
                        continue; // the arrival area stays clear
                    }
                    const uint64_t hp = Hash(settings.m_seed, TagProp, gx, gy);
                    if (Unit(hp) >= settings.m_propChance)
                    {
                        continue;
                    }
                    const int count = 1 + static_cast<int>(Unit(Mix(hp ^ 1)) * 3.0f); // 1-3 crates in a cluster
                    const float cx = (static_cast<float>(gx) + 0.5f) * cell + (Unit(Mix(hp ^ 2)) - 0.5f) * cell * 0.45f;
                    const float cy = (static_cast<float>(gy) + 0.5f) * cell + (Unit(Mix(hp ^ 3)) - 0.5f) * cell * 0.45f;
                    for (int i = 0; i < count; ++i)
                    {
                        const float size = 1.0f + Unit(Mix(hp ^ (10 + i))) * 0.5f;
                        const bool stacked = Unit(Mix(hp ^ (20 + i))) < 0.35f;
                        const float px = cx + static_cast<float>(i % 2) * 1.6f;
                        const float py = cy + static_cast<float>(i / 2) * 1.6f;
                        layout.m_props.push_back(Box{ px, py, size * 0.5f, size, size, size });
                        if (stacked)
                        {
                            const float top = size * 0.8f;
                            layout.m_props.push_back(Box{ px, py, size + top * 0.5f, top, top, top });
                        }
                    }
                }
            }
        }
        else if (settings.m_style == 2 && settings.m_propChance > 0.0f)
        {
            // Pipes along the tunnel walls: a run near the ceiling on most walls, sometimes a second one low down.
            const size_t wallCount = layout.m_walls.size();
            for (size_t i = 0; i < wallCount; ++i)
            {
                const Box wall = layout.m_walls[i];
                const uint64_t hp = Hash(settings.m_seed, TagProp,
                    static_cast<int64_t>(std::floor(wall.m_cx * 2.0f)), static_cast<int64_t>(std::floor(wall.m_cy * 2.0f)));
                if (Unit(hp) >= settings.m_propChance)
                {
                    continue;
                }
                const bool alongX = wall.m_sx > wall.m_sy;
                const float side = Unit(Mix(hp ^ 1)) < 0.5f ? -1.0f : 1.0f;
                const int runs = Unit(Mix(hp ^ 2)) < 0.3f ? 2 : 1;
                for (int r2 = 0; r2 < runs; ++r2)
                {
                    const float pipe = r2 == 0 ? 0.22f : 0.14f;
                    const float z = r2 == 0 ? height - 0.3f : 0.45f;
                    const float offset = thick * 0.5f + pipe * 0.5f + 0.04f;
                    layout.m_props.push_back(alongX
                        ? Box{ wall.m_cx, wall.m_cy + side * offset, z, wall.m_sx, pipe, pipe }
                        : Box{ wall.m_cx + side * offset, wall.m_cy, z, pipe, wall.m_sy, pipe });
                }
            }
        }

        // 10. Now and then a noclip spot: a tear in the level that drops you into the next one.
        if (settings.m_noclipChance > 0.0f)
        {
            const uint64_t hn = Hash(settings.m_seed, TagNoclip, chunkX, chunkY);
            const bool nearOrigin = std::abs(chunkX) + std::abs(chunkY) <= 1; // never right by the arrival area
            if (!nearOrigin && Unit(hn) < settings.m_noclipChance)
            {
                const int cx = static_cast<int>(Mix(hn ^ 1) % static_cast<uint64_t>(n));
                const int cy = static_cast<int>(Mix(hn ^ 2) % static_cast<uint64_t>(n));
                layout.m_noclipSpots.push_back(Point{ (static_cast<float>(baseX + cx) + 0.5f) * cell, (static_cast<float>(baseY + cy) + 0.5f) * cell, 0.0f });
            }
        }

        // 11. Supplies lying about: scrap, wire, fuel, Almond Water, Foundation documents, odd samples; never inside the spawn room.
        if (settings.m_pickupChance > 0.0f && settings.m_style <= 2)
        {
            const int r = settings.m_spawnRoomRadius;
            int index = 0;
            for (int cy = 0; cy < n; ++cy)
            {
                for (int cx = 0; cx < n; ++cx)
                {
                    const int gx = baseX + cx;
                    const int gy = baseY + cy;
                    if (gx >= -r && gx <= r && gy >= -r && gy <= r)
                    {
                        continue;
                    }
                    const uint64_t hp = Hash(settings.m_seed, TagPickup, gx, gy);
                    if (Unit(hp) >= settings.m_pickupChance)
                    {
                        continue;
                    }
                    const float roll = Unit(Mix(hp ^ 3));
                    // Scrap (most), wire, fuel, Almond Water, a Foundation document, an odd sample (rarest).
                    const int kind = roll < 0.42f ? 0 : (roll < 0.64f ? 1 : (roll < 0.77f ? 2 : (roll < 0.87f ? 3 : (roll < 0.96f ? 4 : 5))));
                    const float jx = (Unit(Mix(hp ^ 4)) - 0.5f) * 0.4f * cell;
                    const float jy = (Unit(Mix(hp ^ 5)) - 0.5f) * 0.4f * cell;
                    layout.m_pickups.push_back(ChunkLayout::Pickup{
                        Point{ (static_cast<float>(gx) + 0.5f) * cell + jx, (static_cast<float>(gy) + 0.5f) * cell + jy, 0.0f }, kind, index++ });
                }
            }
        }

        // 12. Clutter: anything can end up in the Backrooms. Per cell, one of: props against its walls, one thing
        //     standing in the open, a big thing or a little scene in an open cell; plus small things lying about.
        //     Room is kept so every doorway and walkway stays passable: wall props keep out of the corners and
        //     within a band along the wall, free props keep to the middle, and the spawn room stays clear to build in.
        if (settings.m_clutterChance > 0.0f && settings.m_style <= 2)
        {
            using namespace Clutter;
            const int level = settings.m_style;
            const float inner = cell * 0.5f - thick * 0.5f;               // cell middle to a wall's face
            const float band = std::min(0.95f, cell * 0.25f);              // how deep wall props may be
            const float span = inner - band - 0.05f;                        // half the usable length of a wall
            const float middle = inner - band - 0.05f;                      // free props keep within this of the middle
            const float open = cell * 0.5f - 0.25f;                         // open cells: half-size for big things and scenes

            auto pick = [&](Place place, uint64_t h, auto&& fits) -> int
            {
                int total = 0;
                for (int k = 0; k < KindCount; ++k)
                {
                    if (Kinds[k].m_place == place && Kinds[k].m_weight[level] > 0 && fits(Kinds[k])) { total += Kinds[k].m_weight[level]; }
                }
                if (total == 0) { return -1; }
                int roll = static_cast<int>(Mix(h) % static_cast<uint64_t>(total));
                for (int k = 0; k < KindCount; ++k)
                {
                    if (Kinds[k].m_place == place && Kinds[k].m_weight[level] > 0 && fits(Kinds[k]))
                    {
                        roll -= Kinds[k].m_weight[level];
                        if (roll < 0) { return k; }
                    }
                }
                return -1;
            };
            auto radius = [](const Kind& k) { return 0.5f * std::sqrt(k.m_width * k.m_width + k.m_depth * k.m_depth); };
            auto lowPipe = [&](float wx, float wy, float sideSign) // Level 2: a low pipe run on this side of this wall
            {
                if (settings.m_style != 2) { return false; }
                const uint64_t hp = Hash(settings.m_seed, TagProp, static_cast<int64_t>(std::floor(wx * 2.0f)), static_cast<int64_t>(std::floor(wy * 2.0f)));
                if (Unit(hp) >= settings.m_propChance) { return false; }
                const float side = Unit(Mix(hp ^ 1)) < 0.5f ? -1.0f : 1.0f;
                return Unit(Mix(hp ^ 2)) < 0.3f && side == sideSign;
            };

            for (int x = 0; x < n; ++x)
            {
                for (int y = 0; y < n; ++y)
                {
                    const int gx = baseX + x;
                    const int gy = baseY + y;
                    if (inRoom(gx, gy))
                    {
                        continue; // the forward base stays clear
                    }
                    if (settings.m_style == 1 && Unit(Hash(settings.m_seed, TagProp, gx, gy)) < settings.m_propChance)
                    {
                        continue; // Level 1: crates already stand here
                    }
                    const float mx = originX + (static_cast<float>(x) + 0.5f) * cell;
                    const float my = originY + (static_cast<float>(y) + 0.5f) * cell;
                    bool busy = settings.m_pickupChance > 0.0f && Unit(Hash(settings.m_seed, TagPickup, gx, gy)) < settings.m_pickupChance;
                    for (const Point& spot : layout.m_noclipSpots)
                    {
                        busy = busy || (std::fabs(spot.m_x - mx) < 0.1f && std::fabs(spot.m_y - my) < 0.1f);
                    }
                    // This cell's walls: south, north, west, east (outward direction, and the wall's center).
                    struct Side { bool m_wall; float m_nx, m_ny; };
                    const Side sides[4] = {
                        { h[x][y], 0.0f, -1.0f }, { h[x][y + 1], 0.0f, 1.0f }, { v[x][y], -1.0f, 0.0f }, { v[x + 1][y], 1.0f, 0.0f } };
                    int walls = 0;
                    for (const Side& s : sides) { walls += s.m_wall ? 1 : 0; }

                    const uint64_t hc = Hash(settings.m_seed, TagClutter, gx, gy, shift);
                    bool middleUsed = busy;
                    if (Unit(hc) < settings.m_clutterChance)
                    {
                        const float mode = Unit(Mix(hc ^ 1));
                        if (walls == 0 && !busy && mode < 0.45f)
                        {
                            // An open cell: a little scene, or one big thing (a car, a bed, a bathtub).
                            const float turn = static_cast<float>(Mix(hc ^ 2) % 4) * 1.5707963f;
                            const float c = std::cos(turn);
                            const float s = std::sin(turn);
                            bool placed = false;
                            if (Unit(Mix(hc ^ 3)) < 0.55f)
                            {
                                int total = 0;
                                for (const Scene& scene : Scenes) { total += scene.m_weight[level]; }
                                if (total > 0)
                                {
                                    int roll = static_cast<int>(Mix(hc ^ 4) % static_cast<uint64_t>(total));
                                    const Scene* chosen = nullptr;
                                    for (const Scene& scene : Scenes)
                                    {
                                        roll -= scene.m_weight[level];
                                        if (roll < 0) { chosen = &scene; break; }
                                    }
                                    // Does it fit in this cell? Each piece's turned footprint must stay inside.
                                    bool fits = chosen != nullptr;
                                    for (int i = 0; fits && i < chosen->m_count; ++i)
                                    {
                                        const ScenePiece& piece = chosen->m_pieces[i];
                                        const Kind& kind = Kinds[piece.m_kind];
                                        const float px = piece.m_x * c - piece.m_y * s;
                                        const float py = piece.m_x * s + piece.m_y * c;
                                        fits = std::fabs(px) + radius(kind) * 0.85f <= open && std::fabs(py) + radius(kind) * 0.85f <= open;
                                    }
                                    if (fits)
                                    {
                                        for (int i = 0; i < chosen->m_count; ++i)
                                        {
                                            const ScenePiece& piece = chosen->m_pieces[i];
                                            layout.m_clutter.push_back(ChunkLayout::Clutter{
                                                Point{ mx + piece.m_x * c - piece.m_y * s, my + piece.m_x * s + piece.m_y * c, 0.0f },
                                                turn + piece.m_yawDegrees * 0.01745329f, piece.m_kind });
                                        }
                                        placed = true;
                                    }
                                }
                            }
                            if (!placed)
                            {
                                const int k = pick(Place::Big, hc ^ 5, [&](const Kind& kind) { return radius(kind) <= open; });
                                if (k >= 0)
                                {
                                    // Parked straight (give or take), like someone left it there.
                                    const float yaw = turn + (Unit(Mix(hc ^ 6)) - 0.5f) * 0.25f;
                                    const float room = open - radius(Kinds[k]);
                                    layout.m_clutter.push_back(ChunkLayout::Clutter{
                                        Point{ mx + (Unit(Mix(hc ^ 7)) - 0.5f) * room, my + (Unit(Mix(hc ^ 8)) - 0.5f) * room, 0.0f }, yaw, k });
                                    placed = true;
                                }
                            }
                            middleUsed = middleUsed || placed;
                        }
                        else if (walls > 0 && mode < 0.75f)
                        {
                            // Against the walls: one, sometimes two (on different walls).
                            const int want = Unit(Mix(hc ^ 9)) < 0.3f ? 2 : 1;
                            int done = 0;
                            int used = 0; // sides already taken (bits)
                            for (int tries = 0; tries < 6 && done < want; ++tries)
                            {
                                const int sideIndex = static_cast<int>(Mix(hc ^ (10 + tries)) % 4);
                                const Side& side = sides[sideIndex];
                                if ((used & (1 << sideIndex)) != 0)
                                {
                                    continue;
                                }
                                used |= 1 << sideIndex;
                                if (!side.m_wall || lowPipe(mx + side.m_nx * cell * 0.5f, my + side.m_ny * cell * 0.5f, side.m_nx + side.m_ny < 0.0f ? 1.0f : -1.0f))
                                {
                                    continue;
                                }
                                const int k = pick(Place::Wall, hc ^ (20 + tries), [&](const Kind& kind)
                                    { return kind.m_depth <= band && kind.m_width * 0.5f <= span; });
                                if (k < 0) { break; }
                                const Kind& kind = Kinds[k];
                                const float along = (Unit(Mix(hc ^ (30 + tries))) - 0.5f) * 2.0f * (span - kind.m_width * 0.5f);
                                const float out = inner - kind.m_depth * 0.5f - 0.03f;
                                // Front faces into the cell: yaw turns +Y to (-sin, cos).
                                const float yaw = std::atan2(side.m_nx, -side.m_ny);
                                layout.m_clutter.push_back(ChunkLayout::Clutter{
                                    Point{ mx + side.m_nx * out - side.m_ny * along, my + side.m_ny * out + side.m_nx * along, 0.0f }, yaw, k });
                                ++done;
                            }
                        }
                        else if (!busy)
                        {
                            // One thing standing in the open, turned any which way.
                            const float limit = walls == 0 ? open - 0.6f : middle;
                            const int k = pick(Place::Free, hc ^ 40, [&](const Kind& kind) { return radius(kind) <= limit; });
                            if (k >= 0)
                            {
                                const float room = limit - radius(Kinds[k]);
                                layout.m_clutter.push_back(ChunkLayout::Clutter{
                                    Point{ mx + (Unit(Mix(hc ^ 41)) - 0.5f) * 2.0f * room, my + (Unit(Mix(hc ^ 42)) - 0.5f) * 2.0f * room, 0.0f },
                                    Unit(Mix(hc ^ 43)) * 6.2831853f, k });
                                middleUsed = true;
                            }
                        }
                    }
                    // Small things lying about (no colliders), in the middle of the cell when nothing else is there.
                    if (!middleUsed && Unit(Mix(hc ^ 50)) < settings.m_scatterChance)
                    {
                        const int count = 1 + (Unit(Mix(hc ^ 51)) < 0.35f ? 1 : 0);
                        for (int i = 0; i < count; ++i)
                        {
                            const int k = pick(Place::Small, hc ^ (52 + i), [&](const Kind& kind) { return radius(kind) <= middle + 0.3f; });
                            if (k < 0) { break; }
                            const float room = std::max(0.0f, middle + 0.3f - radius(Kinds[k]));
                            layout.m_clutter.push_back(ChunkLayout::Clutter{
                                Point{ mx + (Unit(Mix(hc ^ (60 + i))) - 0.5f) * 2.0f * room, my + (Unit(Mix(hc ^ (70 + i))) - 0.5f) * 2.0f * room, 0.0f },
                                Unit(Mix(hc ^ (80 + i))) * 6.2831853f, k });
                        }
                    }
                }
            }
        }

        // 12. Now and then a way on that isn't a tear (owner, Nov 27): an EXIT door in a wall, stairs going down
        //     through the floor, or a drop through the floor into water. Never by the arrival area or the spawn room.
        if (settings.m_exitChance > 0.0f && layout.m_hasSlabs && settings.m_style != 3)
        {
            const uint64_t he = Hash(settings.m_seed, TagExit, chunkX, chunkY);
            const bool nearOrigin = std::abs(chunkX) + std::abs(chunkY) <= 1;
            const bool outdoors = settings.m_layout == 3;
            if (!nearOrigin && Unit(he) < settings.m_exitChance)
            {
                const float roll = Unit(Mix(he ^ 3));
                int kind = roll < 0.45f ? 0 : (roll < 0.75f ? 1 : 2);
                if (outdoors || (kind == 1 && cell < 3.8f) || (kind == 2 && cell < 2.8f))
                {
                    kind = 0; // no holes outdoors or in small cells: a door instead
                }
                if (kind == 0)
                {
                    // An EXIT door on one face of one of the chunk's wall segments.
                    std::vector<const Box*> walls;
                    for (const Box& w : layout.m_walls)
                    {
                        if (std::max(w.m_sx, w.m_sy) >= cell * 0.9f)
                        {
                            walls.push_back(&w);
                        }
                    }
                    if (!walls.empty())
                    {
                        const Box& w = *walls[static_cast<size_t>(Mix(he ^ 4) % walls.size())];
                        const bool alongX = w.m_sx > w.m_sy;
                        const float side = (Mix(he ^ 5) & 1u) ? 1.0f : -1.0f;
                        const float half = (alongX ? w.m_sy : w.m_sx) * 0.5f;
                        ChunkLayout::Exit door{};
                        door.m_kind = 0;
                        door.m_at = alongX ? Point{ w.m_cx, w.m_cy + side * half, 0.0f } : Point{ w.m_cx + side * half, w.m_cy, 0.0f };
                        // Front (+Y of the door) toward the room it opens into.
                        door.m_yaw = alongX ? (side > 0.0f ? 0.0f : 3.14159265f) : (side > 0.0f ? -1.5707963f : 1.5707963f);
                        layout.m_exits.push_back(door);
                    }
                }
                else
                {
                    const int cx = static_cast<int>(Mix(he ^ 6) % static_cast<uint64_t>(n));
                    const int cy = static_cast<int>(Mix(he ^ 7) % static_cast<uint64_t>(n));
                    ChunkLayout::Exit hole{};
                    hole.m_kind = kind;
                    hole.m_at = Point{ (static_cast<float>(baseX + cx) + 0.5f) * cell, (static_cast<float>(baseY + cy) + 0.5f) * cell, 0.0f };
                    const bool turned = (Mix(he ^ 8) & 1u) != 0;
                    hole.m_yaw = turned ? 1.5707963f : 0.0f;
                    const float hx = kind == 1 ? 0.85f : 1.1f, hy = kind == 1 ? 1.65f : 1.1f;
                    hole.m_halfX = turned ? hy : hx;
                    hole.m_halfY = turned ? hx : hy;
                    layout.m_exits.push_back(hole);
                    // The floor in four pieces round the hole.
                    const Box& f = layout.m_floor;
                    const float x0 = f.m_cx - f.m_sx * 0.5f, x1 = f.m_cx + f.m_sx * 0.5f, y0 = f.m_cy - f.m_sy * 0.5f, y1 = f.m_cy + f.m_sy * 0.5f;
                    const float a0 = hole.m_at.m_x - hole.m_halfX, a1 = hole.m_at.m_x + hole.m_halfX;
                    const float b0 = hole.m_at.m_y - hole.m_halfY, b1 = hole.m_at.m_y + hole.m_halfY;
                    auto part = [&](float px0, float px1, float py0, float py1)
                    {
                        if (px1 - px0 > 0.01f && py1 - py0 > 0.01f)
                        {
                            layout.m_floorParts.push_back(Box{ (px0 + px1) * 0.5f, (py0 + py1) * 0.5f, f.m_cz, px1 - px0, py1 - py0, f.m_sz });
                        }
                    };
                    part(x0, x1, y0, b0);
                    part(x0, x1, b1, y1);
                    part(x0, a0, b0, b1);
                    part(a1, x1, b0, b1);
                    // Nothing left standing in it.
                    auto inside = [&](float x, float y, float margin)
                    {
                        return x > a0 - margin && x < a1 + margin && y > b0 - margin && y < b1 + margin;
                    };
                    layout.m_clutter.erase(std::remove_if(layout.m_clutter.begin(), layout.m_clutter.end(),
                        [&](const ChunkLayout::Clutter& c) { return inside(c.m_at.m_x, c.m_at.m_y, 0.8f); }), layout.m_clutter.end());
                    layout.m_pickups.erase(std::remove_if(layout.m_pickups.begin(), layout.m_pickups.end(),
                        [&](const ChunkLayout::Pickup& p) { return inside(p.m_at.m_x, p.m_at.m_y, 0.4f); }), layout.m_pickups.end());
                    layout.m_noclipSpots.erase(std::remove_if(layout.m_noclipSpots.begin(), layout.m_noclipSpots.end(),
                        [&](const Point& p) { return inside(p.m_x, p.m_y, 1.0f); }), layout.m_noclipSpots.end());
                    layout.m_decals.erase(std::remove_if(layout.m_decals.begin(), layout.m_decals.end(),
                        [&](const Decal& d) { return inside(d.m_position.m_x, d.m_position.m_y, d.m_size * 0.5f); }), layout.m_decals.end());
                    layout.m_props.erase(std::remove_if(layout.m_props.begin(), layout.m_props.end(),
                        [&](const Box& b) { return b.m_cz < 1.5f && inside(b.m_cx, b.m_cy, std::max(b.m_sx, b.m_sy) * 0.5f); }), layout.m_props.end());
                }
            }
        }
        // A door's doorway is kept clear too.
        for (const ChunkLayout::Exit& e : layout.m_exits)
        {
            if (e.m_kind == 0)
            {
                const float fx = -std::sin(e.m_yaw), fy = std::cos(e.m_yaw);
                const float mx = e.m_at.m_x + fx * 1.0f, my = e.m_at.m_y + fy * 1.0f;
                layout.m_clutter.erase(std::remove_if(layout.m_clutter.begin(), layout.m_clutter.end(),
                    [&](const ChunkLayout::Clutter& c) { return std::hypot(c.m_at.m_x - mx, c.m_at.m_y - my) < 1.4f; }), layout.m_clutter.end());
            }
        }

        return layout;
    }

    namespace
    {
        // Distance along (dx, dy) from (ox, oy) to a 2D box, or a large number if the ray misses.
        float RayToBox(float ox, float oy, float dx, float dy, const Box& box)
        {
            const float big = 1.0e9f;
            float tMin = 0.0f;
            float tMax = big;
            const float o[2] = { ox, oy };
            const float d[2] = { dx, dy };
            const float lo[2] = { box.m_cx - box.m_sx * 0.5f, box.m_cy - box.m_sy * 0.5f };
            const float hi[2] = { box.m_cx + box.m_sx * 0.5f, box.m_cy + box.m_sy * 0.5f };
            for (int axis = 0; axis < 2; ++axis)
            {
                if (std::fabs(d[axis]) < 1.0e-6f)
                {
                    if (o[axis] < lo[axis] || o[axis] > hi[axis])
                    {
                        return big;
                    }
                    continue;
                }
                float t1 = (lo[axis] - o[axis]) / d[axis];
                float t2 = (hi[axis] - o[axis]) / d[axis];
                if (t1 > t2)
                {
                    std::swap(t1, t2);
                }
                tMin = std::max(tMin, t1);
                tMax = std::min(tMax, t2);
                if (tMin > tMax)
                {
                    return big;
                }
            }
            return tMin;
        }
    } // namespace

    float OpenestYaw(const Settings& settings, float x, float y)
    {
        if (settings.m_style == 3)
        {
            return 0.0f; // the intro starts looking down the corridor
        }
        int cx = 0;
        int cy = 0;
        WorldToChunk(settings, x, y, cx, cy);
        std::vector<Box> blockers;
        for (int ix = cx - 1; ix <= cx + 1; ++ix)
        {
            for (int iy = cy - 1; iy <= cy + 1; ++iy)
            {
                const ChunkLayout layout = GenerateChunk(settings, ix, iy);
                blockers.insert(blockers.end(), layout.m_walls.begin(), layout.m_walls.end());
                blockers.insert(blockers.end(), layout.m_pillars.begin(), layout.m_pillars.end());
            }
        }

        constexpr int directions = 16;
        const float maxView = settings.m_cellSize * static_cast<float>(settings.m_chunkCells); // what is loaded around us
        float distance[directions];
        for (int i = 0; i < directions; ++i)
        {
            const float yaw = static_cast<float>(i) * 6.28318530718f / static_cast<float>(directions);
            const float dx = -std::sin(yaw);
            const float dy = std::cos(yaw);
            float nearest = maxView;
            for (const Box& box : blockers)
            {
                nearest = std::min(nearest, RayToBox(x, y, dx, dy, box));
            }
            distance[i] = nearest;
        }

        // Prefer a direction that is open and has open space either side of it (a room, not a slit).
        int best = 0;
        float bestScore = -1.0f;
        for (int i = 0; i < directions; ++i)
        {
            const float score = distance[i] + 0.5f * (distance[(i + directions - 1) % directions] + distance[(i + 1) % directions]);
            if (score > bestScore)
            {
                bestScore = score;
                best = i;
            }
        }
        return static_cast<float>(best) * 6.28318530718f / static_cast<float>(directions);
    }
} // namespace NoclipProtocol::Level0
