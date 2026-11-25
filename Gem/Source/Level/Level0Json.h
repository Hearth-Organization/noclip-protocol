#pragma once

// Level 0 generator as JSON, with no engine dependencies, so the editor (through Python), the Hearth
// Engine Hub (through the small level0gen tool) and tests all use exactly the same code.
//
// Settings are "key=value;key=value" (seed, openFraction, roomChance, pillarChance, spawnRoomRadius,
// darkZoneShare, darkZoneScale, decalChance, chunkCells, cellSize, borderExtraOpen, pillarSize).

#include "Level0Layout.h"

#include <string>

namespace NoclipProtocol::Level0
{
    //! Changes only the settings named in "key=value;..." text; everything else stays as it is.
    void ApplySettingsText(Settings& settings, const std::string& text);

    //! Every wall, pillar, light [x, y, state] and stain in chunks minX..maxX, minY..maxY (at most 8 x 8).
    std::string GenerateAreaJson(const std::string& settings, int minX, int minY, int maxX, int maxY);

    //! Layout checks over several seeds: determinism, reachable cells, walls and lights per chunk, clear view at spawn.
    std::string RunTestsJson(const std::string& settings, int seeds);
} // namespace NoclipProtocol::Level0
