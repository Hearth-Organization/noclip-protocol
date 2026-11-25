#pragma once

// Level 0 layout: endless, chunked, deterministic.
//
// The world is an infinite grid of square cells (CellSize meters). Cells are grouped into chunks of
// ChunkCells x ChunkCells. Every chunk is generated only from (seed, chunk x, chunk y), so any player,
// at any time, gets the same rooms for the same seed.
//
// Chunks connect: the walls on the lines between chunks are decided by a hash of the line itself, and each
// side of every chunk always gets at least two doorways, so the whole world is one connected maze.
// Each chunk builds its own interior walls plus its south (-Y) and west (-X) border lines; the north and
// east lines belong to the neighbors, so no wall is ever built twice.
//
// This file has no engine dependencies so it can be unit-tested on its own.

#include <cstdint>
#include <vector>

namespace NoclipProtocol::Level0
{
    struct Settings
    {
        uint64_t m_seed = 1;
        int m_chunkCells = 6;          // cells per chunk side
        float m_cellSize = 4.0f;       // meters
        float m_ceilingHeight = 3.0f;  // meters
        float m_wallThickness = 0.2f;  // meters
        float m_openFraction = 0.2f;   // share of leftover interior maze walls knocked out
        float m_borderExtraOpen = 0.15f; // chance of extra doorways on chunk borders (beyond the guaranteed two)
        float m_roomChance = 0.35f;    // chance a chunk gets one big open room
        float m_lightFraction = 0.35f; // kept for compatibility; lighting now comes from zones (below)
        float m_pillarChance = 0.6f;   // chance of a pillar on an open interior corner
        float m_pillarSize = 0.8f;     // chunky square pillars, meters
        int m_spawnRoomRadius = 2;      // cells each way from the spawn cell: a big open, lit room to start in
        float m_darkZoneShare = 0.4f;  // share of the world inside dark zones (dead / missing lights)
        float m_darkZoneScale = 3.5f;  // dark zones are about this many cells across
        float m_decalChance = 0.3f;    // chance a cell gets a floor stain
    };

    //! An axis-aligned box: center and full size, in world meters. Z is up; floor top is z = 0.
    struct Box
    {
        float m_cx, m_cy, m_cz;
        float m_sx, m_sy, m_sz;
    };

    struct Point
    {
        float m_x, m_y, m_z;
    };

    enum class LightState : int
    {
        Working = 0,
        Flickering,
        Dead,
        Dying, // looks working; goes out for good when the player walks under it
    };

    struct Light
    {
        Point m_position; // at the ceiling surface
        LightState m_state;
    };

    enum class DecalType : int
    {
        Wet = 0,
        Mold,
        Worn,
    };

    struct Decal
    {
        Point m_position; // on the floor
        float m_yawRadians;
        float m_size;      // meters across
        DecalType m_type;
    };

    struct ChunkLayout
    {
        int m_chunkX = 0;
        int m_chunkY = 0;
        std::vector<Box> m_walls;   // fixed one-cell segments, so wall textures never stretch
        std::vector<Box> m_pillars;
        Box m_floor{};
        Box m_ceiling{};
        std::vector<Light> m_lights; // cells with no light at all are simply absent
        std::vector<Decal> m_decals;
    };

    //! Builds the layout of one chunk.
    ChunkLayout GenerateChunk(const Settings& settings, int chunkX, int chunkY);

    //! Which chunk a world position falls in.
    void WorldToChunk(const Settings& settings, float x, float y, int& chunkX, int& chunkY);

    //! Center of the spawn cell (cell 0,0 of chunk 0,0), on the floor.
    Point SpawnPoint(const Settings& settings);

    //! Is the wall segment on a chunk border open (a doorway)?
    //! vertical == true: the line x = lineIndex (in cells), segment from cell y = cell to cell + 1.
    //! vertical == false: the line y = lineIndex, segment from cell x = cell to cell + 1.
    bool IsBorderOpen(const Settings& settings, bool vertical, int lineIndex, int cell);

    //! The way to face from (x, y) for the longest, widest clear view (yaw in radians, 0 = facing +Y,
    //! positive turns toward -X), so the player never spawns staring at a wall.
    float OpenestYaw(const Settings& settings, float x, float y);

    //! 0..1 "darkness" of the area around a world cell; above (1 - darkZoneShare) the lights are dead or gone.
    float DarkZoneValue(const Settings& settings, int cellX, int cellY);
} // namespace NoclipProtocol::Level0
