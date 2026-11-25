#pragma once

namespace AZ
{
    class ReflectContext;
}

namespace NoclipProtocol::Level0
{
    //! Exposes the Level 0 layout code to the editor's Python (module azlmbr.noclip), so tools such as the
    //! Level Generator panel draw and test exactly what the game generates:
    //!   NoclipLevel0GenerateArea(settings, minChunkX, minChunkY, maxChunkX, maxChunkY) -> JSON
    //!   NoclipLevel0RunTests(settings, seeds) -> JSON
    //! settings is "key=value;key=value" with the Settings field names without the m_ prefix
    //! (seed, openFraction, roomChance, pillarChance, darkZoneShare, darkZoneScale, decalChance, spawnRoomRadius, ...).
    void ReflectScripting(AZ::ReflectContext* context);
} // namespace NoclipProtocol::Level0
