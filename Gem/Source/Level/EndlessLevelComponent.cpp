#include "EndlessLevelComponent.h"
#include "Level0Scripting.h"
#include "Level0Json.h"
#include "../Render/RayTracing.h"
#include "../Player/FirstPersonControllerComponent.h"
#include "../Base/BaseBuilding.h"
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <MiniAudio/MiniAudioPlaybackBus.h>
#include <MiniAudio/SoundAsset.h>

#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Console/IConsole.h>
#include <AzCore/Asset/AssetManagerBus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/chrono/chrono.h>
#include <AzFramework/Components/NonUniformScaleComponent.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Entity/EntityDebugDisplayBus.h>

#include <cmath>
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
                ->Field("WorkingLights", &EndlessLevelComponent::m_workingLightsField)
                ->Field("WorkingLightsRayTraced", &EndlessLevelComponent::m_workingLightsRayTracedField)
                ->Field("HumShare", &EndlessLevelComponent::m_humShareField)
                ->Field("Style", &EndlessLevelComponent::m_styleField)
                ->Field("PropPrefab", &EndlessLevelComponent::m_propPrefab)
                ->Field("NoclipPrefab", &EndlessLevelComponent::m_noclipPrefab)
                ->Field("FramePrefab", &EndlessLevelComponent::m_framePrefab)
                ->Field("MainMenu", &EndlessLevelComponent::m_mainMenu)
                ->Field("ScreenPrefab", &EndlessLevelComponent::m_screenPrefab)
                ->Field("NoclipTarget", &EndlessLevelComponent::m_noclipTarget)
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
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &EndlessLevelComponent::m_workingLightsField, "Working lights",
                        "Share of lights outside dark zones that work (the rest die, flicker, are dead or missing)")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.05f)
                        ->Attribute(AZ::Edit::Attributes::Max, 0.9f)
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &EndlessLevelComponent::m_workingLightsRayTracedField,
                        "Working lights (ray traced)", "Used instead when ray-traced bounce light is on: bounced light brightens rooms, so fewer lights work")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.05f)
                        ->Attribute(AZ::Edit::Attributes::Max, 0.9f)
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &EndlessLevelComponent::m_styleField, "Level",
                        "Which level this generates: 0 = yellow rooms, 1 = concrete warehouse, 2 = pipe tunnels, 3 = the intro facility (Sublevel 4)")
                        ->Attribute(AZ::Edit::Attributes::Min, 0)
                        ->Attribute(AZ::Edit::Attributes::Max, 3)
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &EndlessLevelComponent::m_humShareField, "Humming lights",
                        "Share of working lights that hum (flickering ones always buzz while lit)")
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
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_lightDyingPrefab, "Dying light", "Spawnable product path")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_propPrefab, "Prop", "Spawnable product path: Level 1 crate, Level 2 pipe (empty = none)")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_noclipPrefab, "Noclip spot", "Spawnable product path (empty = no noclip spots)")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_mainMenu, "Main menu",
                        "This level is the main menu's backdrop: the player stays put and the HUD shows the main menu")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_framePrefab, "Breach frame", "Spawnable product path: Breach 4-A's hazard frame (empty = none)")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_screenPrefab, "Terminal screen", "Spawnable product path (empty = none)")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_noclipTarget, "Noclip leads to",
                        "Level a noclip spot drops you into (empty = the next level: 0 -> 1 -> 2 -> 0)");
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
        const bool rayTraced = RayTracing::IsOn();
        m_settings.m_workingLights = rayTraced ? m_workingLightsRayTracedField : m_workingLightsField;
        AZ_Printf("NoclipProtocol", "Level 0 lights: ray tracing %s, %.0f%% of lit-area lights work", rayTraced ? "on" : "off",
            m_settings.m_workingLights * 100.0f);
        m_settings.m_decalChance = m_decalChanceField;
        m_settings.m_roomChance = m_roomChanceField;
        m_settings.m_pillarChance = m_pillarChanceField;
        m_settings.m_spawnRoomRadius = AZStd::clamp(m_spawnRoomRadiusField, 0, 5);
        m_settings.m_darkZoneScale = AZStd::max(1.0f, m_darkZoneScaleField);
        m_settings.m_seed = m_seed;
        m_settings.m_style = m_styleField;
        Level0::ApplyStyle(m_settings);
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Get(m_shiftsSeen, "/NoclipProtocol/Level0/Shifts");
        }
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
            if (m_settings.m_style != 3 && registry->Get(overrideText, "/NoclipProtocol/Level0/Override") && !overrideText.empty())
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
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            static const char* const objectives[] = {
                "Establish the forward base",
                "Find a way through the warehouse",
                "Get through the tunnels",
                "Report to the briefing terminal",
            };
            registry->Set("/NoclipProtocol/Hud/game.objective", AZStd::string(objectives[AZStd::clamp(m_settings.m_style, 0, 3)]));
            registry->Set("/NoclipProtocol/Hud/game.progress",
                AZStd::string(m_settings.m_style == 3 ? "Sublevel 4, briefing room"
                    : (m_settings.m_style == 0 ? "Put up a Foundation beacon in the spawn room [TAB]" : "Look for a tear in the walls")));
            if (m_settings.m_style == 3)
            {
                registry->Set("/NoclipProtocol/Intro/Accepted", false); // every run of the intro starts with the briefing
            }
            registry->Set("/NoclipProtocol/Menu/Active", m_mainMenu);
            if (!m_mainMenu)
            {
                static const char* const levels[] = { "Level0", "Level1", "Level2", "Intro" };
                registry->Set("/NoclipProtocol/Save/Level", AZStd::string(levels[AZStd::clamp(m_settings.m_style, 0, 3)]));
            }
        }

        m_assetsMissing = !LoadPieceAssets();
        m_announced = false;
        m_facedPlayer = false;
        m_noclipping = false;
        m_terminalPrompt = false;
        m_accepted = false;
        StartBuilding();
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
        m_pendingHums.clear();
        m_statsShown.clear();
        if (m_buildEnabled)
        {
            if (auto* spawner = AzFramework::SpawnableEntitiesInterface::Get())
            {
                for (auto& tickets : m_buildTickets)
                {
                    for (auto& ticket : tickets)
                    {
                        if (ticket.IsValid())
                        {
                            spawner->DespawnAllEntities(ticket);
                        }
                    }
                }
            }
            m_buildTickets.clear();
            Base::Get().m_placed.clear();
            Base::Get().m_requests.clear();
            Base::Get().m_active = false;
            for (auto& asset : m_buildAssets)
            {
                asset.Release();
            }
            m_buildEnabled = false;
        }
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Set("/Hearth/Viewport/Stats", AZStd::string_view{});
        }
        for (auto& asset : m_pieceAssets)
        {
            asset.Release();
        }
    }

    bool EndlessLevelComponent::LoadPieceAssets()
    {
        const AZStd::string* paths[PieceCount] = { &m_wallPrefab, &m_pillarPrefab, &m_floorPrefab, &m_ceilingPrefab, &m_lightPrefab,
                                                   &m_lightFlickerPrefab, &m_lightDeadPrefab, &m_decalWetPrefab, &m_decalMoldPrefab,
                                                   &m_decalWornPrefab, &m_lightDyingPrefab, &m_propPrefab, &m_noclipPrefab,
                                                   &m_framePrefab, &m_screenPrefab };
        bool allFound = true;
        for (int i = 0; i < PieceCount; ++i)
        {
            if (paths[i]->empty())
            {
                continue; // optional piece (props) not used by this level
            }
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
            if (asset.GetId().IsValid() && !asset.IsReady())
            {
                return false;
            }
        }
        return true;
    }

    void EndlessLevelComponent::OnTick([[maybe_unused]] float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        UpdateHums(deltaTime);
        CheckNoclipSpots();
        PublishPlayerState();
        // First frame (every entity is active by now): turn the player toward the most open view,
        // so a run never starts staring at a wall.
        if (!m_facedPlayer && m_followEntity.IsValid())
        {
            m_facedPlayer = true;
            AZ::Vector3 start = AZ::Vector3::CreateZero();
            AZ::TransformBus::EventResult(start, m_followEntity, &AZ::TransformBus::Events::GetWorldTranslation);
            AZStd::string arrived;
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Get(arrived, "/NoclipProtocol/Level/Arrived");
                registry->Set("/NoclipProtocol/Level/Arrived", AZStd::string());
            }
            FirstPersonControllerComponent* player = nullptr;
            AZ::Entity* entity = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(entity, &AZ::ComponentApplicationRequests::FindEntity, m_followEntity);
            if (entity)
            {
                player = entity->FindComponent<FirstPersonControllerComponent>();
            }
            if (m_settings.m_style == 0 && arrived == "breach" && player)
            {
                // Stepped out of Breach 4-A: in front of the hole in the spawn room's south wall, facing into the room.
                player->Teleport(AZ::Vector3(Level0::Level0Breach::ArriveX, Level0::Level0Breach::ArriveY, start.GetZ()), 0.0f, 0.0f, true, true);
            }
            else if (m_mainMenu && player)
            {
                // The main menu's backdrop: inside Level 0's spawn room, looking at Breach 4-A in its south wall.
                player->Teleport(AZ::Vector3(Level0::Level0Breach::X + 1.2f, Level0::Level0Breach::Y + 6.5f, start.GetZ()),
                    AZ::DegToRad(172.0f), AZ::DegToRad(2.0f), true, true);
            }
            else if (m_settings.m_style == 3 && player)
            {
                // The intro always starts at the lift end of the corridor, looking down it.
                const Level0::Point spawn = Level0::SpawnPoint(m_settings);
                player->Teleport(AZ::Vector3(spawn.m_x, spawn.m_y, start.GetZ()), 0.0f, 0.0f, true, true);
            }
            else
            {
                const float yaw = Level0::OpenestYaw(m_settings, start.GetX(), start.GetY());
                AZ::TransformBus::Event(m_followEntity, &AZ::TransformBus::Events::SetWorldRotationQuaternion,
                    AZ::Quaternion::CreateRotationZ(yaw));
            }
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
        DoBuildRequests();

        AZ::Vector3 position = AZ::Vector3::CreateZero();
        if (m_followEntity.IsValid())
        {
            AZ::TransformBus::EventResult(position, m_followEntity, &AZ::TransformBus::Events::GetWorldTranslation);
        }
        int playerX = 0;
        int playerY = 0;
        Level0::WorldToChunk(m_settings, position.GetX(), position.GetY(), playerX, playerY);

        // For the debug overlay: the seed and the chunk the player is in (0, 0 is the forward base).
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Set("/NoclipProtocol/Level0/Seed", AZStd::string::format("%llu", static_cast<unsigned long long>(m_settings.m_seed)));
            registry->Set("/NoclipProtocol/Level0/PlayerChunk", AZStd::string::format("%d, %d%s", playerX, playerY,
                (playerX == 0 && playerY == 0) ? " · forward base" : ""));
        }

        // Shift the rooms (a Hearth Blocks script asked): every chunk but the player's is rebuilt with new rooms.
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            AZ::s64 shifts = 0;
            registry->Get(shifts, "/NoclipProtocol/Level0/Shifts");
            if (shifts != m_shiftsSeen)
            {
                m_shiftsSeen = shifts;
                m_settings.m_seed += 0x9E3779B97F4A7C15ull;
                for (auto it = m_chunks.begin(); it != m_chunks.end();)
                {
                    if (it->second.m_x == playerX && it->second.m_y == playerY)
                    {
                        ++it;
                        continue;
                    }
                    DespawnChunk(it->second);
                    it = m_chunks.erase(it);
                }
                AZ_Printf("NoclipProtocol", "Endless Level 0: the rooms shifted");
            }
        }

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

        PublishStats();
        DrawChunkBorders();
    }

    // Hearth Engine viewport (approved mockup "Editor"): "9 chunks · 412 pieces" in the status line, and the
    // Chunks button there turns on chunk borders. Both go through the settings registry so the engine needs no game code.
    void EndlessLevelComponent::PublishStats()
    {
        int pieces = 0;
        for (const auto& entry : m_chunks)
        {
            pieces += entry.second.m_pieces;
        }
        const AZStd::string text = AZStd::string::format("%d chunks · %d pieces", static_cast<int>(m_chunks.size()), pieces);
        if (text != m_statsShown)
        {
            m_statsShown = text;
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Set("/Hearth/Viewport/Stats", text);
            }
        }
    }

    void EndlessLevelComponent::DrawChunkBorders() const
    {
        bool show = false;
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Get(show, "/Hearth/Viewport/ShowChunks");
        }
        if (!show)
        {
            return;
        }
        AzFramework::DebugDisplayRequestBus::BusPtr debugBus;
        AzFramework::DebugDisplayRequestBus::Bind(debugBus, AzFramework::g_defaultSceneEntityDebugDisplayId);
        AzFramework::DebugDisplayRequests* debug = AzFramework::DebugDisplayRequestBus::FindFirstHandler(debugBus);
        if (!debug)
        {
            return;
        }
        const float size = m_settings.m_cellSize * static_cast<float>(m_settings.m_chunkCells);
        const float top = m_settings.m_ceilingHeight;
        debug->SetColor(AZ::Color(0.91f, 0.45f, 0.23f, 1.0f)); // ember
        for (const auto& entry : m_chunks)
        {
            const float x0 = static_cast<float>(entry.second.m_x) * size;
            const float y0 = static_cast<float>(entry.second.m_y) * size;
            const AZ::Vector3 corners[4] = { AZ::Vector3(x0, y0, 0.05f), AZ::Vector3(x0 + size, y0, 0.05f),
                                             AZ::Vector3(x0 + size, y0 + size, 0.05f), AZ::Vector3(x0, y0 + size, 0.05f) };
            for (int i = 0; i < 4; ++i)
            {
                debug->DrawLine(corners[i], corners[(i + 1) % 4]);
                debug->DrawLine(corners[i], corners[i] + AZ::Vector3(0.0f, 0.0f, top));
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
        const int piecesBefore = m_spawnedPieces;
        for (int i = 0; i < PieceCount; ++i)
        {
            if (m_pieceAssets[i].GetId().IsValid())
            {
                chunk.m_tickets.emplace_back(m_pieceAssets[i]);
            }
            else
            {
                chunk.m_tickets.emplace_back(); // unused piece: an empty ticket that never spawns
            }
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
        if (layout.m_hasSlabs)
        {
            SpawnBox(chunk.m_tickets[PieceFloor], layout.m_floor, true);
            SpawnBox(chunk.m_tickets[PieceCeiling], layout.m_ceiling, true);
        }
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
            if (piece == PieceLight || piece == PieceLightDying || piece == PieceLightFlicker)
            {
                chunk.m_litLights.push_back(AZ::Vector3(light.m_position.m_x, light.m_position.m_y,
                    piece == PieceLightFlicker ? -1.0f : 1.0f)); // z: -1 marks a flickering one (half as much light)
            }
            SpawnBox(chunk.m_tickets[piece],
                Level0::Box{ light.m_position.m_x, light.m_position.m_y, light.m_position.m_z, 1.0f, 1.0f, 1.0f }, false, 0.0f,
                piece == PieceLight || piece == PieceLightDying);
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

        if (chunk.m_tickets[PieceProp].IsValid())
        {
            for (const Level0::Box& prop : layout.m_props)
            {
                SpawnBox(chunk.m_tickets[PieceProp], prop, true);
            }
        }

        for (const Level0::Point& spot : layout.m_noclipSpots)
        {
            chunk.m_noclipSpots.push_back(AZ::Vector3(spot.m_x, spot.m_y, spot.m_z));
            if (chunk.m_tickets[PieceNoclip].IsValid())
            {
                if (m_settings.m_style == 3)
                {
                    // Breach 4-A: a 3 m wide hole hanging in its frame, facing down the corridor.
                    SpawnBox(chunk.m_tickets[PieceNoclip], Level0::Box{ spot.m_x, spot.m_y, 1.35f, 3.0f, 0.1f, 2.7f }, true);
                }
                else
                {
                    // A tall, thin tear standing in the middle of the cell.
                    SpawnBox(chunk.m_tickets[PieceNoclip], Level0::Box{ spot.m_x, spot.m_y, 1.15f, 0.9f, 0.06f, 2.3f }, true,
                        static_cast<float>(chunk.m_x * 7 + chunk.m_y * 3));
                }
            }
        }
        if (chunk.m_tickets[PieceNoclip].IsValid())
        {
            for (const Level0::Box& hole : layout.m_voids)
            {
                SpawnBox(chunk.m_tickets[PieceNoclip], hole, true);
            }
        }
        if (chunk.m_tickets[PieceFrame].IsValid())
        {
            for (const Level0::Box& frame : layout.m_frames)
            {
                SpawnBox(chunk.m_tickets[PieceFrame], frame, true);
            }
        }
        if (chunk.m_tickets[PieceScreen].IsValid())
        {
            for (const Level0::Box& screen : layout.m_screens)
            {
                SpawnBox(chunk.m_tickets[PieceScreen], screen, true);
            }
        }

        chunk.m_pieces = m_spawnedPieces - piecesBefore;
        m_chunks.emplace(Key(chunkX, chunkY), AZStd::move(chunk));
    }

    void EndlessLevelComponent::SpawnBox(AzFramework::EntitySpawnTicket& ticket, const Level0::Box& box, bool stretch, float yawRadians,
        bool hum)
    {
        ++m_spawnedPieces;
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
        if (hum)
        {
            // Decided per light from its position, so a light hums (or doesn't) the same way every time its chunk loads.
            const AZ::u64 seed = m_settings.m_seed ^ (static_cast<AZ::u64>(static_cast<AZ::s64>(box.m_cx * 4.0f)) * 0x9E3779B97F4A7C15ull)
                               ^ (static_cast<AZ::u64>(static_cast<AZ::s64>(box.m_cy * 4.0f)) * 0xC2B2AE3D27D4EB4Full);
            auto unit = [](AZ::u64 x)
            {
                x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ull; x ^= x >> 33;
                return static_cast<float>(x >> 40) / static_cast<float>(1ull << 24);
            };
            const bool keep = unit(seed) < m_humShareField;
            const float delay = unit(seed + 1) * 4.0f;
            args.m_completionCallback =
                [this, keep, delay]([[maybe_unused]] AzFramework::EntitySpawnTicket::Id ticketId, AzFramework::SpawnableConstEntityContainerView view)
            {
                for (const AZ::Entity* entity : view)
                {
                    const AZStd::string& name = entity->GetName();
                    if (name.size() < 4 || name.substr(name.size() - 4) != "Bulb")
                    {
                        continue;
                    }
                    // The bulbs' hum doesn't start by itself; kept ones start after their own delay.
                    MiniAudio::MiniAudioPlaybackRequestBus::Event(entity->GetId(), &MiniAudio::MiniAudioPlaybackRequests::Stop);
                    if (keep)
                    {
                        m_pendingHums.push_back(PendingHum{ entity->GetId(), delay, 0 });
                    }
                }
            };
        }
        AzFramework::SpawnableEntitiesInterface::Get()->SpawnAllEntities(ticket, AZStd::move(args));
    }

    void EndlessLevelComponent::PublishPlayerState()
    {
        auto* registry = AZ::SettingsRegistry::Get();
        if (!registry || !m_followEntity.IsValid())
        {
            return;
        }
        AZ::Vector3 at = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(at, m_followEntity, &AZ::TransformBus::Events::GetWorldTranslation);
        // How lit the player is: from the nearest light that's on (a flickering one counts half).
        const float reach = m_settings.m_cellSize * 1.25f + 1.0f;
        float light = 0.0f;
        for (const auto& [key, chunk] : m_chunks)
        {
            for (const AZ::Vector3& lamp : chunk.m_litLights)
            {
                const float dx = lamp.GetX() - at.GetX();
                const float dy = lamp.GetY() - at.GetY();
                const float d = std::sqrt(dx * dx + dy * dy);
                float lit = AZStd::clamp(1.0f - (d - 1.0f) / reach, 0.0f, 1.0f);
                if (lamp.GetZ() < 0.0f)
                {
                    lit *= 0.5f;
                }
                light = AZStd::max(light, lit);
            }
        }
        registry->Set("/NoclipProtocol/Player/Light", static_cast<double>(light));
        // The forward base is the arrival room around the spawn point.
        const Level0::Point base = Level0::SpawnPoint(m_settings);
        const float bx = base.m_x - at.GetX();
        const float by = base.m_y - at.GetY();
        registry->Set("/NoclipProtocol/Hud/base.distance", static_cast<double>(std::sqrt(bx * bx + by * by)));
        registry->Set("/NoclipProtocol/Hud/base.x", static_cast<double>(base.m_x));
        registry->Set("/NoclipProtocol/Hud/base.y", static_cast<double>(base.m_y));
        static const char* const titles[] = { "LEVEL 0", "LEVEL 1", "LEVEL 2", "SITE-[REDACTED]" };
        static const char* const subtitles[] = { "THE LOBBY · UNSTABLE", "HABITABLE ZONE · DAMP", "PIPE DREAMS · HOT",
                                                 "SUBLEVEL 4 · BREACH 4-A" };
        const int style = AZStd::clamp(m_settings.m_style, 0, 3);
        registry->Set("/NoclipProtocol/Hud/level.title", AZStd::string(titles[style]));
        registry->Set("/NoclipProtocol/Hud/level.subtitle", AZStd::string(subtitles[style]));

        if (m_settings.m_style == 3)
        {
            // The briefing terminal: "[E] Access" when standing at it. The HUD opens the terminal page on E.
            const float tx = Level0::Facility::TerminalX - at.GetX();
            const float ty = Level0::Facility::TerminalY - at.GetY();
            const bool near = tx * tx + ty * ty < 1.8f * 1.8f;
            if (near != m_terminalPrompt)
            {
                m_terminalPrompt = near;
                registry->Set("/NoclipProtocol/Hud/interact.verb", AZStd::string(near ? "Access" : ""));
                registry->Set("/NoclipProtocol/Hud/interact.item", AZStd::string(near ? "Secure terminal" : ""));
                registry->Set("/NoclipProtocol/Hud/interact.target", AZStd::string(near ? "terminal" : ""));
            }
            bool accepted = false;
            registry->Get(accepted, "/NoclipProtocol/Intro/Accepted");
            if (accepted && !m_accepted)
            {
                m_accepted = true;
                registry->Set("/NoclipProtocol/Hud/game.objective", AZStd::string("Enter Breach 4-A"));
                registry->Set("/NoclipProtocol/Hud/game.progress", AZStd::string("Breach chamber, end of the corridor"));
            }
        }
    }

    void EndlessLevelComponent::StartBuilding()
    {
        // The forward base: Level 0's spawn room (not the main menu's backdrop).
        m_buildEnabled = m_settings.m_style == 0 && !m_mainMenu && m_settings.m_spawnRoomRadius > 0;
        Base::State& base = Base::Get();
        base.m_placed.clear();
        base.m_requests.clear();
        base.m_beaconUp = false;
        base.m_active = m_buildEnabled;
        m_buildTickets.clear();
        if (!m_buildEnabled)
        {
            return;
        }
        const float r = static_cast<float>(m_settings.m_spawnRoomRadius);
        const float cell = m_settings.m_cellSize;
        base.m_minX = -r * cell;
        base.m_minY = -r * cell;
        base.m_maxX = (r + 1.0f) * cell;
        base.m_maxY = (r + 1.0f) * cell;
        base.m_cell = cell;
        base.m_breachX = Level0::Level0Breach::X;
        base.m_breachY = Level0::Level0Breach::Y;
        static const char* const paths[BuildPieceCount] = {
            "prefabs/base/build_pole.spawnable", "prefabs/base/build_floodlight.spawnable", "prefabs/base/build_flare.spawnable",
            "prefabs/base/build_beacon.spawnable", "prefabs/base/build_beaconlight.spawnable", "prefabs/base/build_uvlamp.spawnable",
        };
        for (int i = 0; i < BuildPieceCount; ++i)
        {
            AZ::Data::AssetId id;
            AZ::Data::AssetCatalogRequestBus::BroadcastResult(id, &AZ::Data::AssetCatalogRequests::GetAssetIdByPath, paths[i],
                azrtti_typeid<AzFramework::Spawnable>(), false);
            if (id.IsValid())
            {
                m_buildAssets[i] = AZ::Data::AssetManager::Instance().GetAsset<AzFramework::Spawnable>(id, AZ::Data::AssetLoadBehavior::PreLoad);
            }
            else
            {
                AZ_Warning("NoclipProtocol", false, "Base building: piece '%s' not found", paths[i]);
            }
        }
    }

    void EndlessLevelComponent::DoBuildRequests()
    {
        Base::State& base = Base::Get();
        if (!m_buildEnabled || base.m_requests.empty())
        {
            return;
        }
        for (const auto& asset : m_buildAssets)
        {
            if (asset.GetId().IsValid() && !asset.IsReady())
            {
                return; // still loading: keep the requests for the next tick
            }
        }
        auto* registry = AZ::SettingsRegistry::Get();
        auto toast = [registry](const char* text)
        {
            if (registry)
            {
                double now = 0.0;
                registry->Get(now, "/NoclipProtocol/Hud/time");
                registry->Set("/NoclipProtocol/Hud/toast", AZStd::string(text));
                registry->Set("/NoclipProtocol/Hud/toast.until", now + 3.0);
            }
        };
        const AZStd::vector<Base::Request> requests = AZStd::move(base.m_requests);
        base.m_requests.clear();
        for (const Base::Request& request : requests)
        {
            if (!request.m_place)
            {
                // Demolish: the nearest piece within 1.5 m; half of what it cost comes back.
                int best = -1;
                float bestD = 1.5f * 1.5f;
                for (int i = 0; i < static_cast<int>(base.m_placed.size()); ++i)
                {
                    const float dx = base.m_placed[i].m_x - request.m_x;
                    const float dy = base.m_placed[i].m_y - request.m_y;
                    if (dx * dx + dy * dy < bestD)
                    {
                        bestD = dx * dx + dy * dy;
                        best = i;
                    }
                }
                if (best < 0)
                {
                    continue;
                }
                if (auto* spawner = AzFramework::SpawnableEntitiesInterface::Get())
                {
                    for (auto& ticket : m_buildTickets[best])
                    {
                        if (ticket.IsValid())
                        {
                            spawner->DespawnAllEntities(ticket);
                        }
                    }
                }
                const Base::ItemInfo& info = Base::Info(base.m_placed[best].m_kind);
                base.m_scrap += info.m_scrap / 2;
                base.m_wire += info.m_wire / 2;
                base.m_fuel += info.m_fuel / 2;
                if (base.m_placed[best].m_kind == Base::Kind::Beacon)
                {
                    base.m_beaconUp = false;
                }
                base.m_placed.erase(base.m_placed.begin() + best);
                m_buildTickets.erase(m_buildTickets.begin() + best);
                continue;
            }
            if (base.Blocked(request.m_kind, request.m_x, request.m_y))
            {
                continue; // the HUD checks first; things may have changed since
            }
            const Base::ItemInfo& info = Base::Info(request.m_kind);
            AZStd::vector<AzFramework::EntitySpawnTicket> tickets;
            auto spawn = [&](BuildPiece piece, const Level0::Box& box, bool stretch)
            {
                if (!m_buildAssets[piece].IsReady())
                {
                    return;
                }
                tickets.emplace_back(m_buildAssets[piece]);
                SpawnBox(tickets.back(), box, stretch, request.m_yaw);
            };
            const float x = request.m_x;
            const float y = request.m_y;
            switch (request.m_kind)
            {
            case Base::Kind::Floodlight:
                spawn(BuildPole, Level0::Box{ x, y, 1.1f, 0.12f, 0.12f, 2.2f }, true);
                spawn(BuildFloodlight, Level0::Box{ x, y, 2.3f, 1.0f, 1.0f, 1.0f }, false);
                break;
            case Base::Kind::CeilingPanel:
                if (m_pieceAssets[PieceLight].IsReady())
                {
                    tickets.emplace_back(m_pieceAssets[PieceLight]);
                    SpawnBox(tickets.back(), Level0::Box{ x, y, m_settings.m_ceilingHeight, 1.0f, 1.0f, 1.0f }, false, request.m_yaw);
                }
                break;
            case Base::Kind::FlareStand:
                spawn(BuildPole, Level0::Box{ x, y, 0.35f, 0.1f, 0.1f, 0.7f }, true);
                spawn(BuildFlare, Level0::Box{ x, y, 0.78f, 1.0f, 1.0f, 1.0f }, false);
                break;
            case Base::Kind::Beacon:
                spawn(BuildBeacon, Level0::Box{ x, y, 0.7f, 0.4f, 0.4f, 1.4f }, true);
                spawn(BuildBeaconLight, Level0::Box{ x, y, 1.6f, 1.0f, 1.0f, 1.0f }, false);
                break;
            case Base::Kind::UvLamp:
                spawn(BuildPole, Level0::Box{ x, y, 0.6f, 0.1f, 0.1f, 1.2f }, true);
                spawn(BuildUvLamp, Level0::Box{ x, y, 1.3f, 1.0f, 1.0f, 1.0f }, false);
                break;
            default:
                break;
            }
            if (tickets.empty())
            {
                continue;
            }
            base.m_scrap -= info.m_scrap;
            base.m_wire -= info.m_wire;
            base.m_fuel -= info.m_fuel;
            base.m_placed.push_back(Base::Placed{ request.m_kind, x, y, request.m_yaw });
            m_buildTickets.push_back(AZStd::move(tickets));
            if (request.m_kind == Base::Kind::Beacon && !base.m_beaconUp)
            {
                base.m_beaconUp = true;
                toast("FOUNDATION BEACON ONLINE \xc2\xb7 FORWARD BASE ESTABLISHED");
                if (registry)
                {
                    registry->Set("/NoclipProtocol/Hud/game.objective", AZStd::string("Forward base established"));
                    registry->Set("/NoclipProtocol/Hud/game.progress", AZStd::string("Scout Level 0 and find a way deeper"));
                }
            }
        }
    }

    void EndlessLevelComponent::CheckNoclipSpots()
    {
        if (m_noclipping || !m_followEntity.IsValid())
        {
            return;
        }
        AZ::Vector3 at = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(at, m_followEntity, &AZ::TransformBus::Events::GetWorldTranslation);
        for (const auto& [key, chunk] : m_chunks)
        {
            for (const AZ::Vector3& spot : chunk.m_noclipSpots)
            {
                const AZ::Vector3 d = at - spot;
                const bool breach = m_settings.m_style == 3;
                const bool inside = breach
                    ? std::fabs(d.GetX()) < 1.5f && std::fabs(d.GetY()) < 0.35f // walking through the 3 m wide hole
                    : d.GetX() * d.GetX() + d.GetY() * d.GetY() <= 0.6f * 0.6f;
                if (!inside || std::fabs(d.GetZ()) > 1.5f)
                {
                    continue;
                }
                auto* registry = AZ::SettingsRegistry::Get();
                double now = 0.0;
                if (registry)
                {
                    registry->Get(now, "/NoclipProtocol/Hud/time");
                }
                if (breach && !m_accepted)
                {
                    // Sealed until the briefing is accepted at the terminal.
                    if (registry && now - m_sealedToastAt > 4.0)
                    {
                        m_sealedToastAt = static_cast<float>(now);
                        registry->Set("/NoclipProtocol/Hud/toast", AZStd::string("BREACH 4-A SEALED · ACCEPT THE BRIEFING FIRST"));
                        registry->Set("/NoclipProtocol/Hud/toast.until", now + 3.0);
                    }
                    return;
                }
                m_noclipping = true;
                static const char* const next[] = { "Level1", "Level2", "Level0", "Level0" };
                const AZStd::string target = !m_noclipTarget.empty() ? m_noclipTarget : AZStd::string(next[AZStd::clamp(m_settings.m_style, 0, 3)]);
                AZ_Printf("NoclipProtocol", "Noclip spot at %.1f %.1f: falling through into %s", spot.GetX(), spot.GetY(), target.c_str());
                if (registry)
                {
                    registry->Set("/NoclipProtocol/Hud/toast", AZStd::string(breach ? "Stepping through Breach 4-A..." : "You slip through the floor..."));
                    registry->Set("/NoclipProtocol/Hud/toast.until", now + 3.0);
                    registry->Set("/NoclipProtocol/Level/Arrived", AZStd::string(breach ? "breach" : "noclip"));
                }
                // Next frame, outside this tick: loading a level tears down this component.
                AZ::TickBus::QueueFunction([target]()
                {
                    if (auto* console = AZ::Interface<AZ::IConsole>::Get())
                    {
                        console->PerformCommand(AZStd::string::format("LoadLevel %s", target.c_str()).c_str());
                    }
                });
                return;
            }
        }
    }

    void EndlessLevelComponent::UpdateHums(float deltaTime)
    {
        for (size_t i = 0; i < m_pendingHums.size();)
        {
            PendingHum& hum = m_pendingHums[i];
            hum.m_delay -= deltaTime;
            if (hum.m_delay > 0.0f)
            {
                ++i;
                continue;
            }
            AZ::Data::Asset<MiniAudio::SoundAsset> asset;
            MiniAudio::MiniAudioPlaybackRequestBus::EventResult(asset, hum.m_entity, &MiniAudio::MiniAudioPlaybackRequests::GetSoundAsset);
            if (asset.IsReady())
            {
                MiniAudio::MiniAudioPlaybackRequestBus::Event(hum.m_entity, &MiniAudio::MiniAudioPlaybackRequests::Play);
            }
            else if (++hum.m_tries < 20)
            {
                hum.m_delay = 0.25f; // still loading (or its chunk is gone): try again shortly, then give up
                ++i;
                continue;
            }
            m_pendingHums[i] = m_pendingHums.back();
            m_pendingHums.pop_back();
        }
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
