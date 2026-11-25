// level0gen: the Level 0 generator on the command line, for the Hearth Engine Hub's Level Generator.
// Uses the same code as the game (Gem/Source/Level), so the map you see is the map you get.
//
//   level0gen area  "<settings>" minX minY maxX maxY   -> JSON of walls, pillars, lights, stains
//   level0gen tests "<settings>" seeds                  -> JSON of layout checks
#include "../../Gem/Source/Level/Level0Json.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv)
{
    using namespace NoclipProtocol::Level0;
    if (argc >= 7 && std::strcmp(argv[1], "area") == 0)
    {
        const std::string out = GenerateAreaJson(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]), std::atoi(argv[6]));
        std::fwrite(out.data(), 1, out.size(), stdout);
        return 0;
    }
    if (argc >= 4 && std::strcmp(argv[1], "tests") == 0)
    {
        const std::string out = RunTestsJson(argv[2], std::atoi(argv[3]));
        std::fwrite(out.data(), 1, out.size(), stdout);
        return 0;
    }
    std::fprintf(stderr, "usage: level0gen area \"<settings>\" minX minY maxX maxY | level0gen tests \"<settings>\" seeds\n");
    return 2;
}
