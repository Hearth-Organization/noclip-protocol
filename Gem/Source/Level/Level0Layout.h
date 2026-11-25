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
        float m_lightFraction = 0.35f; // share of cells with a ceiling light
        float m_pillarChance = 0.5f;   // chance of a pillar on an open interior corner
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

    struct ChunkLayout
    {
        int m_chunkX = 0;
        int m_chunkY = 0;
        std::vector<Box> m_walls;    // includes pillars
        Box m_floor{};
        Box m_ceiling{};
        std::vector<Point> m_lights; // ceiling light positions (at the ceiling surface)
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
} // namespace NoclipProtocol::Level0
