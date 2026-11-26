#include "Level0Json.h"
#include "Level0Layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <queue>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace NoclipProtocol::Level0
{
    namespace
    {
        Settings ParseSettings(const std::string& text)
        {
            Settings s;
            ApplySettingsText(s, text);
            return s;
        }
    } // namespace

    void ApplySettingsText(Settings& s, const std::string& text)
    {
        {
            const std::string& all = text;
            size_t start = 0;
            while (start < all.size())
            {
                size_t end = all.find(';', start);
                if (end == std::string::npos)
                {
                    end = all.size();
                }
                const std::string pair = all.substr(start, end - start);
                start = end + 1;
                const size_t eq = pair.find('=');
                if (eq == std::string::npos)
                {
                    continue;
                }
                const std::string key = pair.substr(0, eq);
                const char* value = pair.c_str() + eq + 1;
                const double number = std::strtod(value, nullptr);
                if (key == "seed") { s.m_seed = std::strtoull(value, nullptr, 10); }
                else if (key == "chunkCells") { s.m_chunkCells = std::max(3, static_cast<int>(number)); }
                else if (key == "cellSize") { s.m_cellSize = static_cast<float>(number); }
                else if (key == "openFraction") { s.m_openFraction = static_cast<float>(number); }
                else if (key == "borderExtraOpen") { s.m_borderExtraOpen = static_cast<float>(number); }
                else if (key == "roomChance") { s.m_roomChance = static_cast<float>(number); }
                else if (key == "pillarChance") { s.m_pillarChance = static_cast<float>(number); }
                else if (key == "pillarSize") { s.m_pillarSize = static_cast<float>(number); }
                else if (key == "spawnRoomRadius") { s.m_spawnRoomRadius = std::max(0, static_cast<int>(number)); }
                else if (key == "darkZoneShare") { s.m_darkZoneShare = static_cast<float>(number); }
                else if (key == "darkZoneScale") { s.m_darkZoneScale = static_cast<float>(number); }
                else if (key == "decalChance") { s.m_decalChance = static_cast<float>(number); }
                else if (key == "workingLights") { s.m_workingLights = static_cast<float>(number); }
                else if (key == "noclipChance") { s.m_noclipChance = static_cast<float>(number); }
                else if (key == "style") { s.m_style = static_cast<int>(number); ApplyStyle(s); }
            }
        }
    }

    namespace
    {

        void Append(std::string& out, const char* format, double a, double b, double c, double d)
        {
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer), format, a, b, c, d);
            out += buffer;
        }

    } // namespace

        std::string GenerateAreaJson(const std::string& settingsText, int minX, int minY, int maxX, int maxY)
        {
            const Settings settings = ParseSettings(settingsText);
            // Keep it reasonable: at most 8 x 8 chunks per call.
            maxX = std::min(maxX, minX + 7);
            maxY = std::min(maxY, minY + 7);
            std::string walls, pillars, lights, decals;
            for (int cx = minX; cx <= maxX; ++cx)
            {
                for (int cy = minY; cy <= maxY; ++cy)
                {
                    const ChunkLayout layout = GenerateChunk(settings, cx, cy);
                    for (const Box& b : layout.m_walls)
                    {
                        Append(walls, "[%.3f,%.3f,%.3f,%.3f],", b.m_cx, b.m_cy, b.m_sx, b.m_sy);
                    }
                    for (const Box& b : layout.m_pillars)
                    {
                        Append(pillars, "[%.3f,%.3f,%.3f,%.3f],", b.m_cx, b.m_cy, b.m_sx, b.m_sy);
                    }
                    for (const Light& l : layout.m_lights)
                    {
                        char buffer[96];
                        std::snprintf(buffer, sizeof(buffer), "[%.3f,%.3f,%d],", l.m_position.m_x, l.m_position.m_y,
                                      static_cast<int>(l.m_state));
                        lights += buffer;
                    }
                    for (const Decal& d : layout.m_decals)
                    {
                        Append(decals, "[%.3f,%.3f,%.3f,%.0f],", d.m_position.m_x, d.m_position.m_y, d.m_size,
                               static_cast<double>(static_cast<int>(d.m_type)));
                    }
                }
            }
            auto strip = [](std::string& s) { if (!s.empty()) { s.pop_back(); } };
            strip(walls); strip(pillars); strip(lights); strip(decals);
            const Point spawn = SpawnPoint(settings);
            char head[256];
            std::snprintf(head, sizeof(head),
                "{\"chunkSize\":%.3f,\"min\":[%d,%d],\"max\":[%d,%d],\"spawn\":[%.3f,%.3f],\"spawnYaw\":%.4f,",
                settings.m_cellSize * static_cast<float>(settings.m_chunkCells), minX, minY, maxX, maxY,
                spawn.m_x, spawn.m_y, OpenestYaw(settings, spawn.m_x, spawn.m_y));
            std::string out = std::string(head) + "\"walls\":[" + walls + "],\"pillars\":[" + pillars + "],\"lights\":["
                + lights + "],\"decals\":[" + decals + "]}";
            return out;
        }

        // Rays against 2D boxes, for the clear-view check.
        static float RayDistance(const std::vector<Box>& blockers, float x, float y, float yaw, float maxDistance)
        {
            const float dx = -std::sin(yaw);
            const float dy = std::cos(yaw);
            for (float d = 0.0f; d < maxDistance; d += 0.05f)
            {
                const float px = x + dx * d;
                const float py = y + dy * d;
                for (const Box& b : blockers)
                {
                    if (std::fabs(px - b.m_cx) <= b.m_sx * 0.5f && std::fabs(py - b.m_cy) <= b.m_sy * 0.5f)
                    {
                        return d;
                    }
                }
            }
            return maxDistance;
        }

        std::string RunTestsJson(const std::string& settingsText, int seeds)
        {
            Settings settings = ParseSettings(settingsText);
            seeds = std::clamp(seeds, 1, 200);
            const int n = settings.m_chunkCells;
            const float cell = settings.m_cellSize;
            long reachable = 0, total = 0, walls = 0, lights = 0, chunks = 0;
            float minView = 1.0e9f, sumView = 0.0f;
            bool deterministic = true;
            const uint64_t baseSeed = settings.m_seed;
            for (int run = 0; run < seeds; ++run)
            {
                settings.m_seed = baseSeed + 0x9E3779B97F4A7C15ull * static_cast<uint64_t>(run);
                std::set<std::tuple<int, int, int>> blocked; // (vertical, line, cell)
                std::vector<Box> nearSpawn;
                for (int cx = -2; cx <= 1; ++cx)
                {
                    for (int cy = -2; cy <= 1; ++cy)
                    {
                        const ChunkLayout layout = GenerateChunk(settings, cx, cy);
                        if (run == 0 && cx == 1 && cy == 1)
                        {
                            const ChunkLayout again = GenerateChunk(settings, cx, cy);
                            deterministic = again.m_walls.size() == layout.m_walls.size() && again.m_lights.size() == layout.m_lights.size();
                        }
                        ++chunks;
                        walls += static_cast<long>(layout.m_walls.size());
                        lights += static_cast<long>(layout.m_lights.size());
                        for (const Box& w : layout.m_walls)
                        {
                            const bool horizontal = w.m_sy < w.m_sx;
                            const float length = (horizontal ? w.m_sx : w.m_sy) - settings.m_wallThickness;
                            const int count = static_cast<int>(std::lround(length / cell));
                            const int line = static_cast<int>(std::lround((horizontal ? w.m_cy : w.m_cx) / cell));
                            const int first = static_cast<int>(std::lround(((horizontal ? w.m_cx : w.m_cy) - length * 0.5f) / cell));
                            for (int i = 0; i < count; ++i)
                            {
                                blocked.insert({ horizontal ? 0 : 1, line, first + i });
                            }
                        }
                        if (cx >= -1 && cy >= -1 && cx <= 0 && cy <= 0)
                        {
                            nearSpawn.insert(nearSpawn.end(), layout.m_walls.begin(), layout.m_walls.end());
                            nearSpawn.insert(nearSpawn.end(), layout.m_pillars.begin(), layout.m_pillars.end());
                        }
                    }
                }
                const int lo = -2 * n, hi = 2 * n;
                std::set<std::pair<int, int>> seen{ { 0, 0 } };
                std::queue<std::pair<int, int>> queue;
                queue.push({ 0, 0 });
                while (!queue.empty())
                {
                    auto [x, y] = queue.front();
                    queue.pop();
                    auto go = [&](int nx, int ny, bool open)
                    {
                        if (open && nx >= lo && nx < hi && ny >= lo && ny < hi && !seen.count({ nx, ny }))
                        {
                            seen.insert({ nx, ny });
                            queue.push({ nx, ny });
                        }
                    };
                    go(x + 1, y, !blocked.count({ 1, x + 1, y }));
                    go(x - 1, y, !blocked.count({ 1, x, y }));
                    go(x, y + 1, !blocked.count({ 0, y + 1, x }));
                    go(x, y - 1, !blocked.count({ 0, y, x }));
                }
                reachable += static_cast<long>(seen.size());
                total += static_cast<long>((hi - lo) * (hi - lo));
                const Point spawn = SpawnPoint(settings);
                const float view = RayDistance(nearSpawn, spawn.m_x, spawn.m_y, OpenestYaw(settings, spawn.m_x, spawn.m_y),
                                               cell * static_cast<float>(n));
                minView = std::min(minView, view);
                sumView += view;
            }
            char out[512];
            std::snprintf(out, sizeof(out),
                "{\"seeds\":%d,\"deterministic\":%s,\"reachable\":%ld,\"total\":%ld,\"wallsPerChunk\":%.1f,"
                "\"lightsPerChunk\":%.1f,\"minView\":%.1f,\"avgView\":%.1f}",
                seeds, deterministic ? "true" : "false", reachable, total,
                static_cast<double>(walls) / static_cast<double>(std::max(1L, chunks)),
                static_cast<double>(lights) / static_cast<double>(std::max(1L, chunks)),
                static_cast<double>(minView), static_cast<double>(sumView / static_cast<float>(seeds)));
            return std::string(out);
        }
} // namespace NoclipProtocol::Level0
