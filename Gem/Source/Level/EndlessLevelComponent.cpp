#include "EndlessLevelComponent.h"
#include "Level0Scripting.h"
#include "Level0Json.h"

#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Asset/AssetManagerBus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/chrono/chrono.h>
#include <AzFramework/Components/NonUniformScaleComponent.h>
#include <AzFramework/Components/TransformComponent.h>

#include <cstdlib>

namespace NoclipProtocol
{
    void EndlessLevelComponent::Reflect(AZ::ReflectContext* context)
    {
        Level0::ReflectScripting(context);

        if (auto serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<EndlessLevelComponent, AZ::Component>()
                ->Version(2)
                ->Field("FollowEntity", &EndlessLevelComponent::m_followEntity)
                ->Field("Seed", &EndlessLevelComponent::m_seed)
                ->Field("RandomSeedEachRun", &EndlessLevelComponent::m_randomSeedEachRun)
                ->Field("LoadRadius", &EndlessLevelComponent::m_loadRadius)
                ->Field("UnloadRadius", &EndlessLevelComponent::m_unloadRadius)
                ->Field("ChunksPerFrame", &EndlessLevelComponent::m_chunksPerFrame)
                ->Field("ChunkCells", &EndlessLevelComponent::m_chunkCellsField)
                ->Field("LightFraction", &EndlessLevelComponent::m_lightFractionField)
                ->Field("OpenFraction", &EndlessLevelComponent::m_openFractionField)
                ->Field("DarkZoneShare", &EndlessLevelComponent::m_darkZoneShareField)
                ->Field("DecalChance", &EndlessLevelComponent::m_decalChanceField)
                ->Field("EnablePillars", &EndlessLevelComponent::m_enablePillars)
                ->Field("EnableLightBulbs", &EndlessLevelComponent::m_enableLightBulbs)
                ->Field("EnableFlicker", &EndlessLevelComponent::m_enableFlicker)
                ->Field("EnableDeadLights", &EndlessLevelComponent::m_enableDeadLights)
                ->Field("EnableStains", &EndlessLevelComponent::m_enableStains)
                ->Field("WallPrefab", &EndlessLevelComponent::m_wallPrefab)
                ->Field("PillarPrefab", &EndlessLevelComponent::m_pillarPrefab)
                ->Field("FloorPrefab", &EndlessLevelComponent::m_floorPrefab)
                ->Field("CeilingPrefab", &EndlessLevelComponent::m_ceilingPrefab)
                ->Field("LightPrefab", &EndlessLevelComponent::m_lightPrefab)
                ->Field("LightFlickerPrefab", &EndlessLevelComponent::m_lightFlickerPrefab)
                ->Field("LightDeadPrefab", &EndlessLevelComponent::m_lightDeadPrefab)
                ->Field("DecalWetPrefab", &EndlessLevelComponent::m_decalWetPrefab)
                ->Field("DecalMoldPrefab", &EndlessLevelComponent::m_decalMoldPrefab)
                ->Field("DecalWornPrefab", &EndlessLevelComponent::m_decalWornPrefab)
                ->Field("LightDyingPrefab", &EndlessLevelComponent::m_lightDyingPrefab)
                ->Field("EnableDyingLights", &EndlessLevelComponent::m_enableDyingLights)
                ->Field("RoomChance", &EndlessLevelComponent::m_roomChanceField)
                ->Field("PillarChance", &EndlessLevelComponent::m_pillarChanceField)
                ->Field("SpawnRoomRadius", &EndlessLevelComponent::m_spawnRoomRadiusField)
                ->Field("DarkZoneScale", &EndlessLevelComponent::m_darkZoneScaleField)
                ->Field("EnableRoomShifting", &EndlessLevelComponent::m_enableRoomShifting);

            if (auto edit = serialize->GetEditContext())
            {
                edit->Class<EndlessLevelComponent>("Endless Level", "Generates endless Level 0 rooms around the player while playing")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Noclip Protocol")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_followEntity, "Follow entity",
                        "Rooms are generated around this entity (the Player)")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_randomSeedEachRun, "Random seed each run",
                        "A new world every time you play")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_seed, "Seed",
                        "World seed when not random; the same seed always makes the same world")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_loadRadius, "Load radius",
                        "Chunks around the player that always exist (1 = 3x3 chunks)")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_unloadRadius, "Unload radius",
                        "Chunks farther than this are removed")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_chunksPerFrame, "Chunks per frame",
                        "How many new chunks may be built in one frame")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_chunkCellsField, "Rooms per chunk side",
                        "Each room is 4 x 4 m")
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &EndlessLevelComponent::m_lightFractionField, "Lit rooms",
                        "Share of rooms with a ceiling light; the rest are dark")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &EndlessLevelComponent::m_openFractionField, "Openness",
                        "Share of maze walls knocked out (higher = more open)")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &EndlessLevelComponent::m_roomChanceField, "Open room chance",
                        "Chance a chunk gets one big open room")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &EndlessLevelComponent::m_pillarChanceField, "Pillar chance",
                        "Chance of a pillar on an open corner")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &EndlessLevelComponent::m_spawnRoomRadiusField, "Forward base size",
                        "Spawn room: cells each way from the middle (2 = 20 x 20 m, 3 = 28 x 28 m)")
                        ->Attribute(AZ::Edit::Attributes::Min, 0)
                        ->Attribute(AZ::Edit::Attributes::Max, 5)
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &EndlessLevelComponent::m_darkZoneShareField, "Darkness",
                        "Share of the world inside dark zones (dead or missing lights)")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &EndlessLevelComponent::m_darkZoneScaleField, "Dark zone size",
                        "About how many cells across a dark zone is")
                        ->Attribute(AZ::Edit::Attributes::Min, 1.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 8.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &EndlessLevelComponent::m_decalChanceField, "Stains",
                        "Chance a room gets a wet spot, worn patch or mold")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->ClassElement(AZ::Edit::ClassElements::Group, "Features")
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_enablePillars, "Pillars", "")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_enableLightBulbs, "Light bulbs",
                        "Real lights in working fixtures (off = glowing panels only)")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_enableFlicker, "Flickering lights", "")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_enableDeadLights, "Dead lights", "")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_enableStains, "Stains", "")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_enableDyingLights, "Dying lights", "Lights that go out when you walk under them")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_enableRoomShifting, "Room shifting (soon)",
                        "Rooms that rearrange when nobody is looking. Not built yet.")
                        ->Attribute(AZ::Edit::Attributes::ReadOnly, true)
                    ->ClassElement(AZ::Edit::ClassElements::Group, "Tile pieces")
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, false)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_wallPrefab, "Wall", "Spawnable product path")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_pillarPrefab, "Pillar", "Spawnable product path")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_floorPrefab, "Floor", "Spawnable product path")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_ceilingPrefab, "Ceiling", "Spawnable product path")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_lightPrefab, "Light", "Spawnable product path")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_lightFlickerPrefab, "Flickering light", "Spawnable product path")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_lightDeadPrefab, "Dead light", "Spawnable product path")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_decalWetPrefab, "Wet spot", "Spawnable product path")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_decalMoldPrefab, "Mold", "Spawnable product path")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_decalWornPrefab, "Worn patch", "Spawnable product path")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_lightDyingPrefab, "Dying light", "Spawnable product path");
            }
        }
    }

    void EndlessLevelComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("NoclipEndlessLevelService"));
    }

    AZ::u64 EndlessLevelComponent::Key(int x, int y)
    {
        return (static_cast<AZ::u64>(static_cast<AZ::u32>(x)) << 32) | static_cast<AZ::u32>(y);
    }

    void EndlessLevelComponent::Activate()
    {
        m_settings.m_chunkCells = AZStd::max(2, m_chunkCellsField);
        m_settings.m_lightFraction = m_lightFractionField;
        m_settings.m_openFraction = m_openFractionField;
        m_settings.m_darkZoneShare = m_darkZoneShareField;
        m_settings.m_decalChance = m_decalChanceField;
        m_settings.m_roomChance = m_roomChanceField;
        m_settings.m_pillarChance = m_pillarChanceField;
        m_settings.m_spawnRoomRadius = AZStd::clamp(m_spawnRoomRadiusField, 0, 5);
        m_settings.m_darkZoneScale = AZStd::max(1.0f, m_darkZoneScaleField);
        m_settings.m_seed = m_seed;
        if (m_randomSeedEachRun)
        {
            const auto now = AZStd::chrono::steady_clock::now().time_since_epoch().count();
            m_settings.m_seed = static_cast<AZ::u64>(now) ^ (static_cast<AZ::u64>(reinterpret_cast<uintptr_t>(this)) << 17);
        }
        // "Walk it in Play" from the Hearth Engine Level Generator: the game is started with the settings on screen,
        // as "key=value;..." in /NoclipProtocol/Level0/Override, so you walk exactly the map you were looking at.
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            AZStd::string overrideText;
            if (registry->Get(overrideText, "/NoclipProtocol/Level0/Override") && !overrideText.empty())
            {
                Level0::ApplySettingsText(m_settings, std::string(overrideText.c_str()));
                const std::string text(overrideText.c_str());
                auto flag = [&text](const char* key, bool current)
                {
                    const std::string needle = std::string(key) + "=";
                    const size_t at = text.find(needle);
                    return at == std::string::npos ? current : text[at + needle.size()] != '0';
                };
                m_enableFlicker = flag("enableFlicker", m_enableFlicker);
                m_enableDyingLights = flag("enableDying", m_enableDyingLights);
                AZ_Printf("NoclipProtocol", "Level 0 settings from the Level Generator: %s", overrideText.c_str());
            }
        }
        AZ_Printf("NoclipProtocol", "Endless Level 0 seed: %llu", static_cast<unsigned long long>(m_settings.m_seed));

        m_assetsMissing = !LoadPieceAssets();
        m_announced = false;
        m_facedPlayer = false;
        AZ::TickBus::Handler::BusConnect();
    }

    void EndlessLevelComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        for (auto& [key, chunk] : m_chunks)
        {
            DespawnChunk(chunk);
        }
        m_chunks.clear();
        for (auto& asset : m_pieceAssets)
        {
            asset.Release();
        }
    }

    bool EndlessLevelComponent::LoadPieceAssets()
    {
        const AZStd::string* paths[PieceCount] = { &m_wallPrefab, &m_pillarPrefab, &m_floorPrefab, &m_ceilingPrefab, &m_lightPrefab,
                                                   &m_lightFlickerPrefab, &m_lightDeadPrefab, &m_decalWetPrefab, &m_decalMoldPrefab,
                                                   &m_decalWornPrefab, &m_lightDyingPrefab };
        bool allFound = true;
        for (int i = 0; i < PieceCount; ++i)
        {
            AZ::Data::AssetId id;
            AZ::Data::AssetCatalogRequestBus::BroadcastResult(id, &AZ::Data::AssetCatalogRequests::GetAssetIdByPath,
                paths[i]->c_str(), azrtti_typeid<AzFramework::Spawnable>(), false);
            if (!id.IsValid())
            {
                AZ_Error("NoclipProtocol", false, "Endless Level: tile piece '%s' not found. Has the prefab been created and processed?",
                    paths[i]->c_str());
                allFound = false;
                continue;
            }
            m_pieceAssets[i] = AZ::Data::AssetManager::Instance().GetAsset<AzFramework::Spawnable>(id, AZ::Data::AssetLoadBehavior::PreLoad);
        }
        return allFound;
    }

    bool EndlessLevelComponent::PieceAssetsReady() const
    {
        for (const auto& asset : m_pieceAssets)
        {
            if (!asset.IsReady())
            {
                return false;
            }
        }
        return true;
    }

    void EndlessLevelComponent::OnTick([[maybe_unused]] float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        // First frame (every entity is active by now): turn the player toward the most open view,
        // so a run never starts staring at a wall.
        if (!m_facedPlayer && m_followEntity.IsValid())
        {
            m_facedPlayer = true;
            AZ::Vector3 start = AZ::Vector3::CreateZero();
            AZ::TransformBus::EventResult(start, m_followEntity, &AZ::TransformBus::Events::GetWorldTranslation);
            const float yaw = Level0::OpenestYaw(m_settings, start.GetX(), start.GetY());
            AZ::TransformBus::Event(m_followEntity, &AZ::TransformBus::Events::SetWorldRotationQuaternion,
                AZ::Quaternion::CreateRotationZ(yaw));
        }

        if (m_assetsMissing || !PieceAssetsReady())
        {
            return;
        }
        if (!m_announced)
        {
            m_announced = true;
            AZ_Printf("NoclipProtocol", "Endless Level 0: tile pieces loaded, generating around the player");
        }

        AZ::Vector3 position = AZ::Vector3::CreateZero();
        if (m_followEntity.IsValid())
        {
            AZ::TransformBus::EventResult(position, m_followEntity, &AZ::TransformBus::Events::GetWorldTranslation);
        }
        int playerX = 0;
        int playerY = 0;
        Level0::WorldToChunk(m_settings, position.GetX(), position.GetY(), playerX, playerY);

        // Unload chunks that are far away.
        for (auto it = m_chunks.begin(); it != m_chunks.end();)
        {
            const int distance = AZStd::max(abs(it->second.m_x - playerX), abs(it->second.m_y - playerY));
            if (distance > m_unloadRadius)
            {
                DespawnChunk(it->second);
                it = m_chunks.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Build missing chunks, nearest first, a few per frame so there's no hitch.
        int built = 0;
        for (int ring = 0; ring <= m_loadRadius && built < m_chunksPerFrame; ++ring)
        {
            for (int dx = -ring; dx <= ring && built < m_chunksPerFrame; ++dx)
            {
                for (int dy = -ring; dy <= ring && built < m_chunksPerFrame; ++dy)
                {
                    if (AZStd::max(abs(dx), abs(dy)) != ring)
                    {
                        continue; // only this ring's edge
                    }
                    const int cx = playerX + dx;
                    const int cy = playerY + dy;
                    if (m_chunks.find(Key(cx, cy)) == m_chunks.end())
                    {
                        SpawnChunk(cx, cy);
                        ++built;
                    }
                }
            }
        }
    }

    void EndlessLevelComponent::SpawnChunk(int chunkX, int chunkY)
    {
        const Level0::ChunkLayout layout = Level0::GenerateChunk(m_settings, chunkX, chunkY);

        Chunk chunk;
        chunk.m_x = chunkX;
        chunk.m_y = chunkY;
        chunk.m_tickets.reserve(PieceCount);
        for (int i = 0; i < PieceCount; ++i)
        {
            chunk.m_tickets.emplace_back(m_pieceAssets[i]);
        }

        for (const Level0::Box& wall : layout.m_walls)
        {
            SpawnBox(chunk.m_tickets[PieceWall], wall, true);
        }
        if (m_enablePillars)
        {
            for (const Level0::Box& pillar : layout.m_pillars)
            {
                SpawnBox(chunk.m_tickets[PiecePillar], pillar, true);
            }
        }
        SpawnBox(chunk.m_tickets[PieceFloor], layout.m_floor, true);
        SpawnBox(chunk.m_tickets[PieceCeiling], layout.m_ceiling, true);
        for (const Level0::Light& light : layout.m_lights)
        {
            Level0::LightState state = light.m_state;
            if (state == Level0::LightState::Flickering && !m_enableFlicker)
            {
                state = Level0::LightState::Working;
            }
            if (state == Level0::LightState::Dying && !m_enableDyingLights)
            {
                state = Level0::LightState::Working;
            }
            if (state == Level0::LightState::Dead && !m_enableDeadLights)
            {
                continue;
            }
            Piece piece = PieceLightDead;
            if (state == Level0::LightState::Working)
            {
                piece = m_enableLightBulbs ? PieceLight : PieceLightDead; // no bulbs: show a panel, no light
            }
            else if (state == Level0::LightState::Flickering)
            {
                piece = PieceLightFlicker;
            }
            else if (state == Level0::LightState::Dying)
            {
                piece = m_enableLightBulbs ? PieceLightDying : PieceLightDead;
            }
            SpawnBox(chunk.m_tickets[piece],
                Level0::Box{ light.m_position.m_x, light.m_position.m_y, light.m_position.m_z, 1.0f, 1.0f, 1.0f }, false);
        }
        if (m_enableStains)
        {
            for (const Level0::Decal& decal : layout.m_decals)
            {
                const Piece piece = decal.m_type == Level0::DecalType::Wet ? PieceDecalWet
                                  : (decal.m_type == Level0::DecalType::Mold ? PieceDecalMold : PieceDecalWorn);
                // Stains are paper-thin see-through patches lying on the carpet (a few mm up, so they never z-fight).
                SpawnBox(chunk.m_tickets[piece],
                    Level0::Box{ decal.m_position.m_x, decal.m_position.m_y, 0.01f, decal.m_size, decal.m_size, 0.01f }, true, decal.m_yawRadians);
            }
        }

        m_chunks.emplace(Key(chunkX, chunkY), AZStd::move(chunk));
    }

    void EndlessLevelComponent::SpawnBox(AzFramework::EntitySpawnTicket& ticket, const Level0::Box& box, bool stretch, float yawRadians)
    {
        const AZ::Vector3 translation(box.m_cx, box.m_cy, box.m_cz);
        const AZ::Vector3 scale(box.m_sx, box.m_sy, box.m_sz);

        AzFramework::SpawnAllEntitiesOptionalArgs args;
        args.m_preInsertionCallback =
            [translation, scale, stretch, yawRadians]([[maybe_unused]] AzFramework::EntitySpawnTicket::Id ticketId, AzFramework::SpawnableEntityContainerView view)
        {
            if (view.empty())
            {
                return;
            }
            // The first entity is the prefab's container: place it in the world.
            AZ::Entity* container = *view.begin();
            if (auto* transform = container->FindComponent<AzFramework::TransformComponent>())
            {
                AzFramework::TransformComponentConfiguration config;
                config.m_localTransform = AZ::Transform::CreateFromQuaternionAndTranslation(
                    AZ::Quaternion::CreateRotationZ(yawRadians), translation);
                config.m_worldTransform = config.m_localTransform;
                transform->SetConfiguration(config);
            }
            // Stretch the box piece(s) inside it to the wall/slab size.
            if (stretch)
            {
                for (AZ::Entity* entity : view)
                {
                    if (auto* nonUniformScale = entity->FindComponent<AzFramework::NonUniformScaleComponent>())
                    {
                        nonUniformScale->SetScale(scale);
                    }
                }
            }
        };
        AzFramework::SpawnableEntitiesInterface::Get()->SpawnAllEntities(ticket, AZStd::move(args));
    }

    void EndlessLevelComponent::DespawnChunk(Chunk& chunk)
    {
        if (auto* spawner = AzFramework::SpawnableEntitiesInterface::Get())
        {
            for (auto& ticket : chunk.m_tickets)
            {
                if (ticket.IsValid())
                {
                    spawner->DespawnAllEntities(ticket);
                }
            }
        }
        chunk.m_tickets.clear();
    }
} // namespace NoclipProtocol
