#include "EndlessLevelComponent.h"
#include "Level0Scripting.h"
#include "Level0Json.h"
#include "../Render/RayTracing.h"
#include "../Player/FirstPersonControllerComponent.h"
#include "../Base/BaseBuilding.h"
#include "ClutterCatalog.h"
#include "LevelCatalog.h"
#include "LevelCatalogMaterials.h"
#include <AtomLyIntegration/CommonFeatures/CoreLights/AreaLightBus.h>
#include <AtomLyIntegration/CommonFeatures/Material/MaterialComponentBus.h>
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <MiniAudio/MiniAudioPlaybackBus.h>
#include <MiniAudio/SoundAsset.h>

#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Console/IConsole.h>
#include <AzCore/Asset/AssetManagerBus.h>
#include <AzCore/Component/NonUniformScaleBus.h>
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

#include <cctype>
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
                        "Which level this generates: 0 = yellow rooms, 1 = concrete warehouse, 2 = pipe tunnels, 3 = the intro facility (Sublevel 4), 4 = a catalog level (Levels 3-99, picked by /NoclipProtocol/Level/Number)")
                        ->Attribute(AZ::Edit::Attributes::Min, 0)
                        ->Attribute(AZ::Edit::Attributes::Max, 4)
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

    namespace
    {
        // noclip_shift: rearrange every chunk the player can't see right now (testing the low-sanity shifts).
        void NoclipShift([[maybe_unused]] const AZ::ConsoleCommandContainer& arguments)
        {
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Set("/NoclipProtocol/Level0/ForceShift", true);
            }
        }
        AZ_CONSOLEFREEFUNC("noclip_shift", NoclipShift, AZ::ConsoleFunctorFlags::Null,
            "Rearrange every chunk out of the player's sight now (what low sanity does, one chunk at a time)");

        // noclip_clutter 0/1: hide or show the clutter (rebuilds the loaded chunks; for testing).
        void NoclipClutter(const AZ::ConsoleCommandContainer& arguments)
        {
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Set("/NoclipProtocol/Level0/ClutterOff", !arguments.empty() && arguments[0] == "0");
            }
        }
        AZ_CONSOLEFREEFUNC("noclip_clutter", NoclipClutter, AZ::ConsoleFunctorFlags::Null, "noclip_clutter 0 hides the clutter, 1 shows it");

        // Testing the forward base (DEV BUILD).
        int ArgInt(const AZ::ConsoleCommandContainer& arguments, size_t i, int fallback)
        {
            return i < arguments.size() ? std::atoi(AZStd::string(arguments[i]).c_str()) : fallback;
        }
        float ArgFloat(const AZ::ConsoleCommandContainer& arguments, size_t i, float fallback)
        {
            return i < arguments.size() ? static_cast<float>(std::atof(AZStd::string(arguments[i]).c_str())) : fallback;
        }
        // base_give scrap wire fuel docs samples rp: adds to the base's storage.
        void BaseGive(const AZ::ConsoleCommandContainer& arguments)
        {
            Base::State& base = Base::Get();
            base.m_scrap += ArgInt(arguments, 0, 50);
            base.m_wire += ArgInt(arguments, 1, 30);
            base.m_fuel += ArgInt(arguments, 2, 5);
            base.m_docs += ArgInt(arguments, 3, 0);
            base.m_samples += ArgInt(arguments, 4, 0);
            base.m_rp += ArgInt(arguments, 5, 0);
        }
        AZ_CONSOLEFREEFUNC("base_give", BaseGive, AZ::ConsoleFunctorFlags::Null, "base_give scrap wire fuel docs samples rp (testing)");
        // base_place kind x y [yaw degrees]: puts a piece up without the checks or the cost.
        void BasePlace(const AZ::ConsoleCommandContainer& arguments)
        {
            Base::Request request{ true, static_cast<Base::Kind>(AZStd::clamp(ArgInt(arguments, 0, 0), 0, Base::KindCount - 1)), ArgFloat(arguments, 1, 0.0f),
                                   ArgFloat(arguments, 2, 0.0f), AZ::DegToRad(ArgFloat(arguments, 3, 0.0f)) };
            request.m_restore = true;
            Base::Get().m_requests.push_back(request);
        }
        AZ_CONSOLEFREEFUNC("base_place", BasePlace, AZ::ConsoleFunctorFlags::Null, "base_place kind x y [yaw] (testing: no checks, no cost)");
        // base_clear: takes every piece down (no refund).
        void BaseClear([[maybe_unused]] const AZ::ConsoleCommandContainer& arguments)
        {
            Base::State& base = Base::Get();
            for (const Base::Placed& p : base.m_placed)
            {
                base.m_requests.push_back(Base::Request{ false, p.m_kind, p.m_x, p.m_y, 0.0f });
            }
        }
        AZ_CONSOLEFREEFUNC("base_clear", BaseClear, AZ::ConsoleFunctorFlags::Null, "base_clear takes every piece down (testing)");
        // base_research 0/1/2: finishes Power II (1), Storage II too (2), and 3 adds Weapons I.
        void BaseResearch(const AZ::ConsoleCommandContainer& arguments)
        {
            Base::State& base = Base::Get();
            const int level = ArgInt(arguments, 0, 2);
            base.m_studied[0][0] = level >= 1 ? 12 : 0;
            base.m_studied[1][0] = level >= 2 ? 8 : 0;
            base.m_studied[1][1] = level >= 2 ? 2 : 0;
            base.m_weaponsI = level >= 3;
        }
        AZ_CONSOLEFREEFUNC("base_research", BaseResearch, AZ::ConsoleFunctorFlags::Null, "base_research 0-3 (testing)");
        // base_deliver: the next delivery comes now.
        void BaseDeliver([[maybe_unused]] const AZ::ConsoleCommandContainer& arguments)
        {
            Base::State& base = Base::Get();
            if (!base.m_deliveries.empty())
            {
                base.m_deliveries.front().m_left = 0.0f;
            }
        }
        AZ_CONSOLEFREEFUNC("base_deliver", BaseDeliver, AZ::ConsoleFunctorFlags::Null, "base_deliver brings the next Foundation crate now (testing)");

        // noclip_cross: arrive in Level 0 as if through Breach 4-A (testing what the crossing does, without walking it).
        void NoclipCross([[maybe_unused]] const AZ::ConsoleCommandContainer& arguments)
        {
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Set("/NoclipProtocol/Level/Arrived", AZStd::string("breach"));
            }
            AZ::TickBus::QueueFunction([]()
            {
                if (auto* console = AZ::Interface<AZ::IConsole>::Get())
                {
                    console->PerformCommand("LoadLevel levels/level0/level0.spawnable");
                }
            });
        }
        // level_go N: straight to catalog level N (0-99), as if you had noclipped into it (testing).
        void LevelGo(const AZ::ConsoleCommandContainer& arguments)
        {
            const int number = arguments.empty() ? 3 : AZStd::clamp(std::atoi(AZStd::string(arguments[0]).c_str()), 0, Catalog::LevelCount - 1);
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Set("/NoclipProtocol/Level/Number", static_cast<AZ::s64>(number));
                registry->Set("/NoclipProtocol/Level/Arrived", AZStd::string("noclip"));
            }
            AZ::TickBus::QueueFunction([number]()
            {
                if (auto* console = AZ::Interface<AZ::IConsole>::Get())
                {
                    console->PerformCommand(AZStd::string::format("LoadLevel %s", number <= 2 ? AZStd::string::format("Level%d", number).c_str() : "Backrooms").c_str());
                }
            });
        }
        AZ_CONSOLEFREEFUNC("level_go", LevelGo, AZ::ConsoleFunctorFlags::Null, "level_go N goes straight to catalog level N, 0-99 (testing)");

        AZ_CONSOLEFREEFUNC("noclip_cross", NoclipCross, AZ::ConsoleFunctorFlags::Null, "noclip_cross arrives in Level 0 as if through Breach 4-A (testing)");
    } // namespace

    void EndlessLevelComponent::Activate()
    {
        m_chunkShifts.clear();
        m_shiftClock = 0.0f;
        m_shiftsDone = 0;
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
        m_settings.m_catalog = -1;
        Level0::ApplyStyle(m_settings);
        if (m_styleField == 4)
        {
            SetUpCatalogLevel();
        }
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
            const bool catalog = m_settings.m_catalog >= 0;
            registry->Set("/NoclipProtocol/Hud/game.objective",
                catalog ? AZStd::string("Survive, scavenge, find the way deeper") : AZStd::string(objectives[AZStd::clamp(m_settings.m_style, 0, 3)]));
            registry->Set("/NoclipProtocol/Hud/game.progress",
                AZStd::string(m_settings.m_style == 3 ? "Sublevel 4, briefing room"
                    : (m_settings.m_style == 0 && !catalog ? "Put up a Foundation beacon in the spawn room [TAB]" : "Look for a tear in the walls")));
            if (m_settings.m_style == 3)
            {
                registry->Set("/NoclipProtocol/Intro/Accepted", false); // every run of the intro starts with the briefing
            }
            registry->Set("/NoclipProtocol/Menu/Active", m_mainMenu);
            if (!m_mainMenu)
            {
                // Coming into Level 0 from the intro, through Breach 4-A (a one-way trip: no Backrooms level ever leads back):
                // the crossing has shaken you, so sanity starts at 75% (the player's vitals pick this up on their next tick).
                AZStd::string previous, arrived;
                registry->Get(previous, "/NoclipProtocol/Save/Level");
                registry->Get(arrived, "/NoclipProtocol/Level/Arrived");
                if (m_settings.m_style == 0 && m_settings.m_catalog < 0 && (arrived == "breach" || previous == "Intro"))
                {
                    registry->Set("/NoclipProtocol/Player/SanityStart", 0.75);
                }
                static const char* const levels[] = { "Level0", "Level1", "Level2", "Intro" };
                registry->Set("/NoclipProtocol/Save/Level", m_settings.m_catalog >= 0
                    ? AZStd::string::format("Backrooms:%d", m_settings.m_catalog) // CONTINUE comes back to this number
                    : AZStd::string(levels[AZStd::clamp(m_settings.m_style, 0, 3)]));
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
                for (auto& piece : m_built)
                {
                    for (auto* list : { &piece.m_body, &piece.m_light })
                    {
                        for (auto& ticket : *list)
                        {
                            if (ticket.IsValid())
                            {
                                spawner->DespawnAllEntities(ticket);
                            }
                        }
                    }
                }
                for (auto* ticket : { &m_seamTicket, &m_crateTicket })
                {
                    if (ticket->IsValid())
                    {
                        spawner->DespawnAllEntities(*ticket);
                    }
                }
            }
            m_built.clear();
            m_seamTicket = AzFramework::EntitySpawnTicket();
            m_crateTicket = AzFramework::EntitySpawnTicket();
            m_seamEntities.clear();
            m_seamShown = -1.0f;
            m_crateUp = false;
            // The base stays in Base::State (and the save) while other levels play.
            Base::Get().m_requests.clear();
            Base::Get().m_active = false;
            for (auto& asset : m_buildAssets)
            {
                asset.Release();
            }
            for (auto& asset : m_kindAssets)
            {
                asset.Release();
            }
            m_seamAsset.Release();
            m_buildEnabled = false;
        }
        for (auto& asset : m_pickupAssets)
        {
            asset.Release();
        }
        for (auto& asset : m_clutterAssets)
        {
            asset.Release();
        }
        m_clutterAssets.clear();
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
        LoadClutterAssets();
        return allFound;
    }

    void EndlessLevelComponent::LoadClutterAssets()
    {
        m_clutterAssets.clear();
        m_clutterAssets.resize(Clutter::KindCount);
        if (m_settings.m_style > 2)
        {
            return; // the intro facility has its own fixed set
        }
        int missing = 0;
        for (int k = 0; k < Clutter::KindCount; ++k)
        {
            if (Clutter::Kinds[k].m_weight[m_settings.m_style] <= 0)
            {
                continue; // never in this level
            }
            AZStd::string name = Clutter::Kinds[k].m_name;
            for (char& ch : name) { ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); }
            const AZStd::string assetPath = AZStd::string::format("prefabs/clutter/%s.spawnable", name.c_str());
            AZ::Data::AssetId id;
            AZ::Data::AssetCatalogRequestBus::BroadcastResult(id, &AZ::Data::AssetCatalogRequests::GetAssetIdByPath, assetPath.c_str(),
                azrtti_typeid<AzFramework::Spawnable>(), false);
            if (!id.IsValid())
            {
                ++missing;
                continue;
            }
            m_clutterAssets[k] = AZ::Data::AssetManager::Instance().GetAsset<AzFramework::Spawnable>(id, AZ::Data::AssetLoadBehavior::PreLoad);
        }
        AZ_Warning("NoclipProtocol", missing == 0, "Endless Level: %d clutter props not found (run Tools/Clutter/make_clutter.py)", missing);
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
        for (const auto& asset : m_pickupAssets)
        {
            if (asset.GetId().IsValid() && !asset.IsReady())
            {
                return false;
            }
        }
        for (const auto& asset : m_clutterAssets)
        {
            if (asset.GetId().IsValid() && !asset.IsReady() && !asset.IsError())
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
            if (m_settings.m_style == 0 && m_settings.m_catalog < 0 && arrived == "breach" && player)
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
        if (!m_mainMenu && m_settings.m_style <= 2)
        {
            UpdateBase(deltaTime, position);
        }

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

        UpdateShifting(deltaTime, position, playerX, playerY);

        // Testing switch: clutter off/on (noclip_clutter); every loaded chunk is rebuilt.
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            bool off = false;
            registry->Get(off, "/NoclipProtocol/Level0/ClutterOff");
            if (off != m_clutterOff)
            {
                m_clutterOff = off;
                Level0::Settings styled; // the level's own clutter amounts, from a fresh copy of its style
                styled.m_style = m_settings.m_style;
                Level0::ApplyStyle(styled);
                m_settings.m_clutterChance = off ? 0.0f : styled.m_clutterChance;
                m_settings.m_scatterChance = off ? 0.0f : styled.m_scatterChance;
                for (auto& entry : m_chunks)
                {
                    DespawnChunk(entry.second);
                }
                m_chunks.clear();
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

    bool EndlessLevelComponent::ChunkInView(int chunkX, int chunkY, const AZ::Vector3& eye, const AZ::Vector3& forward) const
    {
        // Conservative: walls don't hide anything here. A chunk counts as seen if any point of it lies within a wide
        // cone (70 degrees either side, wider than the screen), or the player is close enough to glimpse it sideways.
        const float size = m_settings.m_cellSize * static_cast<float>(m_settings.m_chunkCells);
        const float x0 = static_cast<float>(chunkX) * size;
        const float y0 = static_cast<float>(chunkY) * size;
        const float nearX = AZStd::clamp(eye.GetX(), x0, x0 + size);
        const float nearY = AZStd::clamp(eye.GetY(), y0, y0 + size);
        const float dx0 = nearX - eye.GetX();
        const float dy0 = nearY - eye.GetY();
        if (dx0 * dx0 + dy0 * dy0 < 3.0f * 3.0f)
        {
            return true;
        }
        constexpr int steps = 8;
        const float cosHalf = 0.342f; // cos 70 degrees
        for (int i = 0; i <= steps; ++i)
        {
            for (int j = 0; j <= steps; ++j)
            {
                const float dx = x0 + size * static_cast<float>(i) / steps - eye.GetX();
                const float dy = y0 + size * static_cast<float>(j) / steps - eye.GetY();
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length < 0.01f || (dx * forward.GetX() + dy * forward.GetY()) / length > cosHalf)
                {
                    return true;
                }
            }
        }
        return false;
    }

    void EndlessLevelComponent::UpdateShifting(float deltaTime, const AZ::Vector3& eye, int playerX, int playerY)
    {
        if (m_settings.m_style > 2 || m_mainMenu || !m_followEntity.IsValid())
        {
            return;
        }
        auto* registry = AZ::SettingsRegistry::Get();
        if (!registry)
        {
            return;
        }
        double sanity = 1.0;
        registry->Get(sanity, "/NoclipProtocol/Hud/player.sanity");
        bool forced = false;
        registry->Get(forced, "/NoclipProtocol/Level0/ForceShift");
        if (forced)
        {
            registry->Set("/NoclipProtocol/Level0/ForceShift", false);
        }
        // Below half sanity the rooms start to move: every 18 s at first, every 3 s when almost gone.
        constexpr double startsBelow = 0.5;
        if (!forced)
        {
            if (sanity >= startsBelow)
            {
                m_shiftClock = 0.0f;
                return;
            }
            const float t = AZStd::clamp(static_cast<float>((startsBelow - sanity) / 0.45), 0.0f, 1.0f);
            const float interval = 18.0f + (3.0f - 18.0f) * t;
            m_shiftClock += deltaTime;
            if (m_shiftClock < interval)
            {
                return;
            }
            m_shiftClock = 0.0f;
        }

        AZ::Quaternion rotation = AZ::Quaternion::CreateIdentity();
        AZ::TransformBus::EventResult(rotation, m_followEntity, &AZ::TransformBus::Events::GetWorldRotationQuaternion);
        AZ::Vector3 forward = rotation.TransformVector(AZ::Vector3::CreateAxisY());
        forward.SetZ(0.0f);
        forward = forward.GetLengthSq() > 1.0e-4f ? forward.GetNormalized() : AZ::Vector3::CreateAxisY();

        // Out-of-sight chunks near enough to be rebuilt straight away (the load loop puts them back next frame).
        AZStd::vector<AZ::u64> hidden;
        for (const auto& [key, chunk] : m_chunks)
        {
            const int distance = AZStd::max(abs(chunk.m_x - playerX), abs(chunk.m_y - playerY));
            if (distance == 0 || distance > m_loadRadius || ChunkInView(chunk.m_x, chunk.m_y, eye, forward))
            {
                continue;
            }
            // The forward base's room never rearranges itself (its chunks hold the base, its floor holds the pieces).
            if (m_buildEnabled)
            {
                const Base::State& base = Base::Get();
                const float size = m_settings.m_cellSize * static_cast<float>(m_settings.m_chunkCells);
                const float x0 = chunk.m_x * size, y0 = chunk.m_y * size;
                if (x0 < base.m_maxX && x0 + size > base.m_minX && y0 < base.m_maxY && y0 + size > base.m_minY)
                {
                    continue;
                }
            }
            hidden.push_back(key);
        }
        if (hidden.empty())
        {
            return;
        }
        // One at a time (all of them when forced from the console), so the change creeps rather than jumps.
        const size_t first = forced ? 0 : static_cast<size_t>(m_shiftsDone * 2654435761u) % hidden.size();
        const size_t count = forced ? hidden.size() : 1;
        for (size_t i = 0; i < count; ++i)
        {
            const AZ::u64 key = hidden[(first + i) % hidden.size()];
            auto it = m_chunks.find(key);
            if (it == m_chunks.end())
            {
                continue;
            }
            ++m_chunkShifts[key];
            ++m_shiftsDone;
            AZ_Printf("NoclipProtocol", "Endless Level: chunk %d, %d shifted (%d) at sanity %.0f%%", it->second.m_x, it->second.m_y,
                m_chunkShifts[key], sanity * 100.0);
            DespawnChunk(it->second);
            m_chunks.erase(it);
        }
        registry->Set("/NoclipProtocol/Level0/ShiftsDone", static_cast<AZ::s64>(m_shiftsDone));
    }

    void EndlessLevelComponent::SpawnChunk(int chunkX, int chunkY)
    {
        const auto shifted = m_chunkShifts.find(Key(chunkX, chunkY));
        const int shift = shifted == m_chunkShifts.end() ? 0 : shifted->second;
        const Level0::ChunkLayout layout = Level0::GenerateChunk(m_settings, chunkX, chunkY, shift);

        Chunk chunk;
        chunk.m_x = chunkX;
        chunk.m_y = chunkY;
        chunk.m_shift = shift;
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
            SpawnBox(chunk.m_tickets[PieceWall], wall, true, 0.0f, false, 0);
        }
        if (m_enablePillars)
        {
            for (const Level0::Box& pillar : layout.m_pillars)
            {
                SpawnBox(chunk.m_tickets[PiecePillar], pillar, true, 0.0f, false, 0);
            }
        }
        if (layout.m_hasSlabs)
        {
            SpawnBox(chunk.m_tickets[PieceFloor], layout.m_floor, true, 0.0f, false, 1);
            SpawnBox(chunk.m_tickets[PieceCeiling], layout.m_ceiling, true, 0.0f, false, 2);
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
                piece == PieceLight || piece == PieceLightDying, 3);
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

        // Clutter: authored at real size, so placed and turned, never stretched.
        if (!layout.m_clutter.empty() && static_cast<int>(m_clutterAssets.size()) == Clutter::KindCount)
        {
            chunk.m_clutter.resize(Clutter::KindCount);
            for (const auto& item : layout.m_clutter)
            {
                const int k = item.m_kind;
                if (k < 0 || k >= Clutter::KindCount || !m_clutterAssets[k].IsReady())
                {
                    continue;
                }
                if (!chunk.m_clutter[k].IsValid())
                {
                    chunk.m_clutter[k] = AzFramework::EntitySpawnTicket(m_clutterAssets[k]);
                }
                SpawnBox(chunk.m_clutter[k], Level0::Box{ item.m_at.m_x, item.m_at.m_y, item.m_at.m_z, 1.0f, 1.0f, 1.0f }, false, item.m_yaw);
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
        // Supplies: each its own spawn, so picking one up removes just that one.
        for (const auto& pickup : layout.m_pickups)
        {
            const AZ::u64 id = Key(chunkX, chunkY) ^ (static_cast<AZ::u64>(pickup.m_index + 1) << 48);
            const int kind = AZStd::clamp(pickup.m_kind, 0, 5);
            if (m_collected.find(id) != m_collected.end() || !m_pickupAssets[kind].IsReady())
            {
                continue;
            }
            // Scrap: a bent plate; wire: a coil; fuel: a jerry can; Almond Water: a bottle; a Foundation document folder; a sample jar.
            static const float sizes[6][3] = { { 0.5f, 0.34f, 0.12f }, { 0.3f, 0.3f, 0.16f }, { 0.36f, 0.18f, 0.46f }, { 0.09f, 0.09f, 0.28f },
                                               { 0.32f, 0.24f, 0.03f }, { 0.1f, 0.1f, 0.16f } };
            Chunk::LivePickup live{ AZ::Vector3(pickup.m_at.m_x, pickup.m_at.m_y, 0.0f), kind, pickup.m_index,
                                    AzFramework::EntitySpawnTicket(m_pickupAssets[kind]) };
            SpawnBox(live.m_ticket, Level0::Box{ pickup.m_at.m_x, pickup.m_at.m_y, sizes[kind][2] * 0.5f, sizes[kind][0], sizes[kind][1], sizes[kind][2] },
                true, static_cast<float>(pickup.m_index * 37 % 360) * 0.01745f);
            chunk.m_pickups.push_back(AZStd::move(live));
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
        bool hum, int dress)
    {
        if (m_settings.m_catalog < 0)
        {
            dress = -1; // the hand-made levels' pieces already wear their own materials
        }
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
        if (dress >= 0 && !hum)
        {
            args.m_completionCallback =
                [this, dress]([[maybe_unused]] AzFramework::EntitySpawnTicket::Id ticketId, AzFramework::SpawnableConstEntityContainerView view)
            {
                for (const AZ::Entity* entity : view)
                {
                    DressEntity(entity->GetId(), dress);
                }
            };
        }
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
                [this, keep, delay, dress]([[maybe_unused]] AzFramework::EntitySpawnTicket::Id ticketId, AzFramework::SpawnableConstEntityContainerView view)
            {
                for (const AZ::Entity* entity : view)
                {
                    if (dress >= 0)
                    {
                        DressEntity(entity->GetId(), dress);
                    }
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

    void EndlessLevelComponent::SetUpCatalogLevel()
    {
        AZ::s64 number = 3;
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Get(number, "/NoclipProtocol/Level/Number");
        }
        number = AZStd::clamp<AZ::s64>(number, 3, Catalog::LevelCount - 1);
        const Catalog::Level& level = Catalog::Levels[number];
        Level0::ApplyCatalog(m_settings, static_cast<int>(number), level.m_layout, level.m_light);
        m_enablePillars = true;
        // The level's surfaces (Tools/Levels/make_catalog_levels.py writes them).
        static const char* const roles[3] = { "wall", "floor", "ceiling" };
        for (int i = 0; i < 3; ++i)
        {
            const AZStd::string assetPath = Catalog::Materials[number][i];
            (void)roles;
            m_dressMaterials[i] = AZ::Data::AssetId();
            AZ::Data::AssetCatalogRequestBus::BroadcastResult(m_dressMaterials[i], &AZ::Data::AssetCatalogRequests::GetAssetIdByPath,
                assetPath.c_str(), AZ::Data::s_invalidAssetType, false);
            AZ_Warning("NoclipProtocol", m_dressMaterials[i].IsValid(), "Catalog level %d: '%s' not found", static_cast<int>(number), assetPath.c_str());
        }
        // Its lights: the colour, and brighter and farther-reaching under a higher ceiling (made for Level 0's 3 m).
        m_lightTint = AZ::Color(level.m_lightColor[0], level.m_lightColor[1], level.m_lightColor[2], 1.0f);
        const float reach = m_settings.m_ceilingHeight / 3.0f;
        static const float brightness[5] = { 1.3f, 1.0f, 0.75f, 0.55f, 0.4f };
        m_lightIntensityScale = brightness[m_settings.m_light] * reach * reach;
        m_lightRadiusScale = reach;
        AZ_Printf("NoclipProtocol", "Catalog level %d: %s (layout %d, light %d, danger %d)", static_cast<int>(number), level.m_name,
            level.m_layout, level.m_light, level.m_danger);
    }

    void EndlessLevelComponent::DressEntity(AZ::EntityId id, int dress)
    {
        if (dress >= 0 && dress < 3)
        {
            if (m_dressMaterials[dress].IsValid())
            {
                AZ::Render::MaterialComponentRequestBus::Event(id, &AZ::Render::MaterialComponentRequests::SetMaterialAssetIdOnDefaultSlot,
                    m_dressMaterials[dress]);
            }
        }
        else if (dress == 3)
        {
            AZ::Render::AreaLightRequestBus::Event(id, &AZ::Render::AreaLightRequests::SetColor, m_lightTint);
            float intensity = 0.0f;
            AZ::Render::AreaLightRequestBus::EventResult(intensity, id, &AZ::Render::AreaLightRequests::GetIntensity);
            AZ::Render::AreaLightRequestBus::Event(id, static_cast<void (AZ::Render::AreaLightRequests::*)(float)>(&AZ::Render::AreaLightRequests::SetIntensity),
                intensity * m_lightIntensityScale);
            if (m_lightRadiusScale > 1.01f)
            {
                float radius = 0.0f;
                AZ::Render::AreaLightRequestBus::EventResult(radius, id, &AZ::Render::AreaLightRequests::GetAttenuationRadius);
                AZ::Render::AreaLightRequestBus::Event(id, &AZ::Render::AreaLightRequests::SetAttenuationRadius, radius * m_lightRadiusScale);
            }
        }
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
        // The spawn room's own working ceiling lights (the forward base's room): they light you like any other.
        if (m_settings.m_style == 0 && m_settings.m_catalog < 0 && !m_mainMenu)
        {
            for (const auto& lamp : Base::Get().m_roomLights)
            {
                const float dx = lamp.first - at.GetX();
                const float dy = lamp.second - at.GetY();
                light = AZStd::max(light, AZStd::clamp(1.0f - (std::sqrt(dx * dx + dy * dy) - 1.0f) / reach, 0.0f, 1.0f));
            }
        }
        // The base's own lights that are on.
        if (m_buildEnabled)
        {
            for (const Base::Placed& p : Base::Get().m_placed)
            {
                const float r = Base::State::LightRadius(p.m_kind);
                if (r > 0.0f && (p.m_powered || Base::Info(p.m_kind).m_power <= 0.0f))
                {
                    const float dx = p.m_x - at.GetX();
                    const float dy = p.m_y - at.GetY();
                    light = AZStd::max(light, AZStd::clamp(1.0f - (std::sqrt(dx * dx + dy * dy) - 1.0f) / r, 0.0f, 1.0f));
                }
            }
        }
        registry->Set("/NoclipProtocol/Player/HasBase", Base::Get().m_beaconUp); // lets sanity recover past 80%
        // A burning flare (thrown, or from the flare gun) lights you too.
        double flareLight = 0.0;
        registry->Get(flareLight, "/NoclipProtocol/Player/FlareLight");
        light = AZStd::max(light, static_cast<float>(flareLight));
        registry->Set("/NoclipProtocol/Player/Light", static_cast<double>(light));
        UpdatePickups(at);
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
        if (m_settings.m_catalog >= 0)
        {
            const Catalog::Level& level = Catalog::Levels[m_settings.m_catalog];
            static const char* const moods[5] = { "BRIGHT", "QUIET", "DIM", "DARK", "NO LIGHT" };
            static const char* const danger[6] = { "", "CALM", "UNEASY", "HOSTILE", "DANGEROUS", "DEADLY" };
            AZStd::string name = level.m_name;
            for (char& ch : name) { ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); }
            registry->Set("/NoclipProtocol/Hud/level.title", AZStd::string::format("LEVEL %d", level.m_number));
            registry->Set("/NoclipProtocol/Hud/level.subtitle",
                AZStd::string::format("%s \xc2\xb7 %s", name.c_str(), level.m_danger >= 4 ? danger[level.m_danger] : moods[m_settings.m_light]));
            registry->Set("/NoclipProtocol/Hud/level.depth", static_cast<double>(level.m_number + 1));
        }
        else
        {
            const int style = AZStd::clamp(m_settings.m_style, 0, 3);
            registry->Set("/NoclipProtocol/Hud/level.title", AZStd::string(titles[style]));
            registry->Set("/NoclipProtocol/Hud/level.subtitle", AZStd::string(subtitles[style]));
        }

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
        m_buildEnabled = m_settings.m_style == 0 && m_settings.m_catalog < 0 && !m_mainMenu && m_settings.m_spawnRoomRadius > 0;
        Base::State& base = Base::Get();
        base.LoadOnce(); // the expedition's stockpile and base (CONTINUE), once per run
        base.m_requests.clear();
        base.m_pickRequested = false;
        base.m_useRequested = false;
        base.m_active = m_buildEnabled;
        m_built.clear();
        m_collected.clear();
        m_promptPickup = 0;
        m_pickupPrompt = false;
        m_useTarget = -1;
        auto load = [](const char* path, AZ::Data::Asset<AzFramework::Spawnable>& asset)
        {
            AZ::Data::AssetId id;
            AZ::Data::AssetCatalogRequestBus::BroadcastResult(id, &AZ::Data::AssetCatalogRequests::GetAssetIdByPath, path,
                azrtti_typeid<AzFramework::Spawnable>(), false);
            if (id.IsValid())
            {
                asset = AZ::Data::AssetManager::Instance().GetAsset<AzFramework::Spawnable>(id, AZ::Data::AssetLoadBehavior::PreLoad);
                return true;
            }
            AZ_Warning("NoclipProtocol", false, "Base building: '%s' not found", path);
            return false;
        };
        // Supplies' pieces (every level with supplies, not the menu or the intro).
        if (!m_mainMenu && m_settings.m_style <= 2)
        {
            static const char* const pickups[6] = { "prefabs/base/pickup_scrap.spawnable", "prefabs/base/pickup_wire.spawnable",
                                                    "prefabs/base/pickup_fuel.spawnable", "prefabs/base/pickup_water.spawnable",
                                                    "prefabs/base/pickup_document.spawnable", "prefabs/base/pickup_sample.spawnable" };
            for (int i = 0; i < 6; ++i)
            {
                load(pickups[i], m_pickupAssets[i]);
            }
        }
        if (!m_buildEnabled)
        {
            return;
        }
        // Put the saved base back up (the pieces spawn once their assets are in).
        base.m_beaconUp = false;
        for (const Base::Placed& piece : base.m_placed)
        {
            Base::Request restore{ true, piece.m_kind, piece.m_x, piece.m_y, piece.m_yaw };
            restore.m_restore = true;
            base.m_requests.push_back(restore);
        }
        base.m_placed.clear();
        const float r = static_cast<float>(m_settings.m_spawnRoomRadius);
        const float cell = m_settings.m_cellSize;
        base.m_minX = -r * cell;
        base.m_minY = -r * cell;
        base.m_maxX = (r + 1.0f) * cell;
        base.m_maxY = (r + 1.0f) * cell;
        base.m_cell = cell;
        base.m_breachX = Level0::Level0Breach::X;
        base.m_breachY = Level0::Level0Breach::Y;
        // The room's two doorways (Level0Layout: north wall, one cell in from each side).
        {
            const int door = AZStd::max(1, m_settings.m_spawnRoomRadius - 1);
            base.m_doorways[0] = Base::Doorway{ (static_cast<float>(-door) + 0.5f) * cell, base.m_maxY, "north-west doorway" };
            base.m_doorways[1] = Base::Doorway{ (static_cast<float>(door) + 0.5f) * cell, base.m_maxY, "north doorway" };
        }
        // The room's own ceiling lights that work (for "light inside" and dark corners).
        base.m_roomLights.clear();
        base.m_pillars.clear();
        {
            int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
            Level0::WorldToChunk(m_settings, base.m_minX + 0.1f, base.m_minY + 0.1f, cx0, cy0);
            Level0::WorldToChunk(m_settings, base.m_maxX - 0.1f, base.m_maxY - 0.1f, cx1, cy1);
            for (int cx = cx0; cx <= cx1; ++cx)
            {
                for (int cy = cy0; cy <= cy1; ++cy)
                {
                    const Level0::ChunkLayout layout = Level0::GenerateChunk(m_settings, cx, cy);
                    for (const Level0::Box& pillar : layout.m_pillars)
                    {
                        if (pillar.m_cx > base.m_minX && pillar.m_cx < base.m_maxX && pillar.m_cy > base.m_minY && pillar.m_cy < base.m_maxY)
                        {
                            base.m_pillars.push_back({ pillar.m_cx, pillar.m_cy });
                        }
                    }
                    for (const Level0::Light& light : layout.m_lights)
                    {
                        const float x = light.m_position.m_x, y = light.m_position.m_y;
                        if (light.m_state != Level0::LightState::Dead && x > base.m_minX && x < base.m_maxX && y > base.m_minY && y < base.m_maxY)
                        {
                            base.m_roomLights.push_back({ x, y });
                        }
                    }
                }
            }
        }
        static const char* const paths[BuildPieceCount] = {
            "prefabs/base/build_pole.spawnable", "prefabs/base/build_floodlight.spawnable", "prefabs/base/build_flare.spawnable",
            "prefabs/base/build_beacon.spawnable", "prefabs/base/build_beaconlight.spawnable", "prefabs/base/build_uvlamp.spawnable",
        };
        for (int i = 0; i < BuildPieceCount; ++i)
        {
            load(paths[i], m_buildAssets[i]);
        }
        for (int k = 0; k < Base::KindCount; ++k)
        {
            if (const char* prefab = Base::Info(static_cast<Base::Kind>(k)).m_prefab)
            {
                load(prefab, m_kindAssets[k]);
            }
        }
        load("prefabs/base/piece_breachseam.spawnable", m_seamAsset);
        m_seamShown = -1.0f;
        m_crateUp = false;
    }

    void EndlessLevelComponent::SpawnPieceLight(int index)
    {
        Base::State& base = Base::Get();
        if (index < 0 || index >= static_cast<int>(base.m_placed.size()) || index >= static_cast<int>(m_built.size()))
        {
            return;
        }
        const Base::Placed& p = base.m_placed[index];
        const Base::ItemInfo& info = Base::Info(p.m_kind);
        BuiltPiece& built = m_built[index];
        auto spawn = [&](const AZ::Data::Asset<AzFramework::Spawnable>& asset, float z, float yaw)
        {
            if (!asset.IsReady())
            {
                return;
            }
            built.m_light.emplace_back(asset);
            SpawnBox(built.m_light.back(), Level0::Box{ p.m_x, p.m_y, z, 1.0f, 1.0f, 1.0f }, false, yaw);
        };
        switch (p.m_kind)
        {
        case Base::Kind::Floodlight: spawn(m_buildAssets[BuildFloodlight], 2.3f, p.m_yaw); break;
        case Base::Kind::CeilingPanel: spawn(m_pieceAssets[PieceLight], m_settings.m_ceilingHeight, p.m_yaw); break;
        case Base::Kind::FlareStand: spawn(m_buildAssets[BuildFlare], 0.78f, p.m_yaw); break;
        case Base::Kind::Beacon: spawn(m_buildAssets[BuildBeaconLight], 1.6f, p.m_yaw); break;
        case Base::Kind::UvLamp: spawn(m_buildAssets[BuildUvLamp], 1.3f, p.m_yaw); break;
        default:
            switch (info.m_light)
            {
            case Base::Light::Warm: spawn(m_buildAssets[BuildFloodlight], info.m_lightZ, p.m_yaw); break;
            case Base::Light::Uv: spawn(m_buildAssets[BuildUvLamp], info.m_lightZ, p.m_yaw + 1.5707963f); break;
            case Base::Light::Red: spawn(m_buildAssets[BuildFlare], info.m_lightZ, p.m_yaw); break;
            case Base::Light::Gold: spawn(m_buildAssets[BuildBeaconLight], info.m_lightZ, p.m_yaw); break;
            default: break;
            }
            break;
        }
        built.m_lightOn = true;
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
        for (const auto& asset : m_kindAssets)
        {
            if (asset.GetId().IsValid() && !asset.IsReady())
            {
                return;
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
        auto despawn = [](AZStd::vector<AzFramework::EntitySpawnTicket>& tickets)
        {
            if (auto* spawner = AzFramework::SpawnableEntitiesInterface::Get())
            {
                for (auto& ticket : tickets)
                {
                    if (ticket.IsValid())
                    {
                        spawner->DespawnAllEntities(ticket);
                    }
                }
            }
            tickets.clear();
        };
        const AZStd::vector<Base::Request> requests = AZStd::move(base.m_requests);
        base.m_requests.clear();
        for (const Base::Request& request : requests)
        {
            if (!request.m_place)
            {
                // Demolish: the piece whose footprint is aimed at (or the nearest within 1.5 m); half of what it cost comes back.
                int best = -1;
                float bestD = 1.5f * 1.5f;
                for (int i = 0; i < static_cast<int>(base.m_placed.size()); ++i)
                {
                    const Base::Placed& p = base.m_placed[i];
                    const Base::ItemInfo& pi = Base::Info(p.m_kind);
                    const float dx = p.m_x - request.m_x;
                    const float dy = p.m_y - request.m_y;
                    const float reach = 0.5f * AZStd::max(pi.m_sizeX, pi.m_sizeY);
                    const float d = AZStd::max(0.0f, std::sqrt(dx * dx + dy * dy) - reach);
                    if (d * d < bestD)
                    {
                        bestD = d * d;
                        best = i;
                    }
                }
                if (best < 0)
                {
                    continue;
                }
                despawn(m_built[best].m_body);
                despawn(m_built[best].m_light);
                const Base::ItemInfo& info = Base::Info(base.m_placed[best].m_kind);
                base.m_scrap += info.m_scrap / 2;
                base.m_wire += info.m_wire / 2;
                base.m_fuel += info.m_fuel / 2;
                if (base.m_placed[best].m_kind == Base::Kind::Beacon)
                {
                    base.m_beaconUp = base.Count(Base::Kind::Beacon) > 1;
                }
                base.m_placed.erase(base.m_placed.begin() + best);
                m_built.erase(m_built.begin() + best);
                base.Save();
                continue;
            }
            const Base::ItemInfo& info = Base::Info(request.m_kind);
            if (info.m_project)
            {
                continue;
            }
            if (!request.m_restore && base.Blocked(request.m_kind, request.m_x, request.m_y, request.m_yaw))
            {
                continue; // the HUD checks first; things may have changed since
            }
            BuiltPiece built;
            auto spawn = [&](BuildPiece piece, const Level0::Box& box, bool stretch)
            {
                if (!m_buildAssets[piece].IsReady())
                {
                    return;
                }
                built.m_body.emplace_back(m_buildAssets[piece]);
                SpawnBox(built.m_body.back(), box, stretch, request.m_yaw);
            };
            const float x = request.m_x;
            const float y = request.m_y;
            switch (request.m_kind)
            {
            case Base::Kind::Floodlight: spawn(BuildPole, Level0::Box{ x, y, 1.1f, 0.12f, 0.12f, 2.2f }, true); break;
            case Base::Kind::CeilingPanel: break; // all light
            case Base::Kind::FlareStand: spawn(BuildPole, Level0::Box{ x, y, 0.35f, 0.1f, 0.1f, 0.7f }, true); break;
            case Base::Kind::Beacon: spawn(BuildBeacon, Level0::Box{ x, y, 0.7f, 0.4f, 0.4f, 1.4f }, true); break;
            case Base::Kind::UvLamp: spawn(BuildPole, Level0::Box{ x, y, 0.6f, 0.1f, 0.1f, 1.2f }, true); break;
            default:
            {
                const auto& asset = m_kindAssets[static_cast<int>(request.m_kind)];
                if (asset.IsReady())
                {
                    built.m_body.emplace_back(asset);
                    SpawnBox(built.m_body.back(), Level0::Box{ x, y, 0.0f, 1.0f, 1.0f, 1.0f }, false, request.m_yaw);
                }
                break;
            }
            }
            const bool lightOnly = request.m_kind == Base::Kind::CeilingPanel;
            if (built.m_body.empty() && !lightOnly)
            {
                continue;
            }
            if (!request.m_restore)
            {
                base.m_scrap -= info.m_scrap;
                base.m_wire -= info.m_wire;
                base.m_fuel -= info.m_fuel;
            }
            base.m_placed.push_back(Base::Placed{ request.m_kind, x, y, request.m_yaw });
            m_built.push_back(AZStd::move(built));
            SpawnPieceLight(static_cast<int>(m_built.size()) - 1);
            if (!request.m_restore)
            {
                base.Save();
            }
            if (request.m_kind == Base::Kind::Beacon && !base.m_beaconUp)
            {
                base.m_beaconUp = true;
                if (!request.m_restore)
                {
                    toast("FOUNDATION BEACON ONLINE \xc2\xb7 FORWARD BASE ESTABLISHED");
                }
                if (registry)
                {
                    registry->Set("/NoclipProtocol/Hud/game.objective", AZStd::string("Forward base established"));
                    registry->Set("/NoclipProtocol/Hud/game.progress", AZStd::string("Scout Level 0 and find a way deeper"));
                }
            }
        }
    }

    void EndlessLevelComponent::UpdateBase(float deltaTime, const AZ::Vector3& at)
    {
        Base::State& base = Base::Get();
        auto* registry = AZ::SettingsRegistry::Get();
        const bool inBase = m_buildEnabled && at.GetX() > base.m_minX && at.GetX() < base.m_maxX && at.GetY() > base.m_minY && at.GetY() < base.m_maxY;
        base.Simulate(AZStd::min(deltaTime, 0.25f), inBase);
        if (!base.m_toast.empty() && registry)
        {
            double now = 0.0;
            registry->Get(now, "/NoclipProtocol/Hud/time");
            registry->Set("/NoclipProtocol/Hud/toast", base.m_toast);
            registry->Set("/NoclipProtocol/Hud/toast.until", now + 3.5);
            base.m_toast.clear();
        }
        if (!m_buildEnabled)
        {
            return;
        }
        // A comfortable base steadies you: a little sanity back each second you spend in it.
        if (inBase && registry && base.m_comfort > 0)
        {
            double add = 0.0;
            registry->Get(add, "/NoclipProtocol/Player/SanityAdd");
            registry->Set("/NoclipProtocol/Player/SanityAdd", add + base.m_comfort * 0.0004 * deltaTime);
        }
        auto* spawner = AzFramework::SpawnableEntitiesInterface::Get();
        // Lights follow the power (a blackout, the breaker, a piece out of the cables' reach).
        for (int i = 0; i < static_cast<int>(base.m_placed.size()) && i < static_cast<int>(m_built.size()); ++i)
        {
            const bool want = base.m_placed[i].m_powered;
            if (want == m_built[i].m_lightOn)
            {
                continue;
            }
            if (want)
            {
                SpawnPieceLight(i);
            }
            else
            {
                if (spawner)
                {
                    for (auto& ticket : m_built[i].m_light)
                    {
                        if (ticket.IsValid())
                        {
                            spawner->DespawnAllEntities(ticket);
                        }
                    }
                }
                m_built[i].m_light.clear();
                m_built[i].m_lightOn = false;
            }
        }
        // Breach 4-A: sealed, a glowing seam down the middle; a delivery opens it a crack.
        const auto& tear = m_seamAsset;
        const float width = 0.04f + 0.86f * AZStd::clamp(base.m_breachOpen, 0.0f, 1.0f);
        if (tear.IsReady() && !m_seamTicket.IsValid())
        {
            m_seamTicket = AzFramework::EntitySpawnTicket(tear);
            m_seamEntities.clear();
            AzFramework::SpawnAllEntitiesOptionalArgs args;
            const AZ::Vector3 place(base.m_breachX, base.m_breachY - 0.26f, 1.35f);
            const AZ::Vector3 scale(width, 0.06f, 2.6f);
            args.m_preInsertionCallback = [place, scale](AzFramework::EntitySpawnTicket::Id, AzFramework::SpawnableEntityContainerView view)
            {
                if (view.empty())
                {
                    return;
                }
                if (auto* transform = (*view.begin())->FindComponent<AzFramework::TransformComponent>())
                {
                    AzFramework::TransformComponentConfiguration config;
                    config.m_localTransform = AZ::Transform::CreateTranslation(place);
                    config.m_worldTransform = config.m_localTransform;
                    transform->SetConfiguration(config);
                }
                for (AZ::Entity* entity : view)
                {
                    if (auto* nonUniformScale = entity->FindComponent<AzFramework::NonUniformScaleComponent>())
                    {
                        nonUniformScale->SetScale(scale);
                    }
                }
            };
            args.m_completionCallback = [this](AzFramework::EntitySpawnTicket::Id, AzFramework::SpawnableConstEntityContainerView view)
            {
                for (const AZ::Entity* entity : view)
                {
                    m_seamEntities.push_back(entity->GetId());
                }
            };
            if (spawner)
            {
                spawner->SpawnAllEntities(m_seamTicket, AZStd::move(args));
            }
            m_seamShown = width;
        }
        else if (std::fabs(width - m_seamShown) > 0.005f && !m_seamEntities.empty())
        {
            for (const AZ::EntityId& id : m_seamEntities)
            {
                AZ::NonUniformScaleRequestBus::Event(id, &AZ::NonUniformScaleRequests::SetScale, AZ::Vector3(width, 0.06f, 2.6f));
            }
            m_seamShown = width;
        }
        // A delivered Foundation crate waits in front of the breach until it's opened.
        const auto& crate = m_kindAssets[static_cast<int>(Base::Kind::SupplyCrate)];
        const bool wantCrate = !base.m_waiting.empty() && (base.m_breachOpen > 0.5f || base.m_breachTimer <= 0.0f);
        if (wantCrate && !m_crateUp && crate.IsReady())
        {
            m_crateTicket = AzFramework::EntitySpawnTicket(crate);
            SpawnBox(m_crateTicket, Level0::Box{ base.m_breachX + 0.4f, base.m_breachY + 1.5f, 0.0f, 1.0f, 1.0f, 1.0f }, false, 0.2f);
            m_crateUp = true;
        }
        else if (base.m_waiting.empty() && m_crateUp)
        {
            if (spawner && m_crateTicket.IsValid())
            {
                spawner->DespawnAllEntities(m_crateTicket);
            }
            m_crateTicket = AzFramework::EntitySpawnTicket();
            m_crateUp = false;
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
                if (!inside || std::fabs(d.GetZ()) > 2.5f) // the followed entity may be the eye (1.6 m up), not the feet
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
                static const char* const next[] = { "Level1", "Level2", "Backrooms", "Level0" };
                AZStd::string target = !m_noclipTarget.empty() ? m_noclipTarget : AZStd::string(next[AZStd::clamp(m_settings.m_style, 0, 3)]);
                if (m_settings.m_catalog >= 0 || (m_noclipTarget.empty() && m_settings.m_style == 2))
                {
                    // Deeper: one to four levels on, picked by the tear's place (the same tear always leads to the same
                    // level). The hubs (The Matrix, The Hub) lead anywhere; The Exit? leads back to the Lobby.
                    const int from = m_settings.m_catalog >= 0 ? m_settings.m_catalog : 2;
                    const AZ::u64 h = m_settings.m_seed ^ (static_cast<AZ::u64>(static_cast<AZ::s64>(spot.GetX() * 7.0f)) * 0x9E3779B97F4A7C15ull)
                                    ^ (static_cast<AZ::u64>(static_cast<AZ::s64>(spot.GetY() * 7.0f)) * 0xC2B2AE3D27D4EB4Full);
                    const int mixed = static_cast<int>((h ^ (h >> 29)) % 1000003ull);
                    int to = from + 1 + mixed % 4;
                    if (from == 12 || from == 52)
                    {
                        to = 3 + mixed % (Catalog::LevelCount - 3);
                    }
                    if (from == Catalog::LevelCount - 1 || to >= Catalog::LevelCount)
                    {
                        to = from == Catalog::LevelCount - 1 ? 0 : Catalog::LevelCount - 1;
                    }
                    if (registry)
                    {
                        registry->Set("/NoclipProtocol/Level/Number", static_cast<AZ::s64>(to));
                    }
                    target = to <= 2 ? AZStd::string::format("Level%d", to) : AZStd::string("Backrooms");
                }
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
        if (auto* spawner = AzFramework::SpawnableEntitiesInterface::Get())
        {
            for (auto& pickup : chunk.m_pickups)
            {
                if (pickup.m_ticket.IsValid())
                {
                    spawner->DespawnAllEntities(pickup.m_ticket);
                }
            }
        }
        chunk.m_pickups.clear();
        if (auto* spawner = AzFramework::SpawnableEntitiesInterface::Get())
        {
            for (auto& ticket : chunk.m_clutter)
            {
                if (ticket.IsValid())
                {
                    spawner->DespawnAllEntities(ticket);
                }
            }
        }
        chunk.m_clutter.clear();
    }

    void EndlessLevelComponent::UpdatePickups(const AZ::Vector3& at)
    {
        auto* registry = AZ::SettingsRegistry::Get();
        if (!registry || m_mainMenu || m_settings.m_style > 2)
        {
            return;
        }
        // The nearest supply within reach.
        Chunk* bestChunk = nullptr;
        int best = -1;
        float bestD = 1.6f * 1.6f;
        for (auto& [key, chunk] : m_chunks)
        {
            for (int i = 0; i < static_cast<int>(chunk.m_pickups.size()); ++i)
            {
                const float dx = chunk.m_pickups[i].m_at.GetX() - at.GetX();
                const float dy = chunk.m_pickups[i].m_at.GetY() - at.GetY();
                if (dx * dx + dy * dy < bestD)
                {
                    bestD = dx * dx + dy * dy;
                    best = i;
                    bestChunk = &chunk;
                }
            }
        }
        Base::State& base = Base::Get();
        // In the base: the pieces you can use ([E]) and a delivered crate, if nearer than any supply.
        int use = -1;
        if (m_buildEnabled)
        {
            float useD = 1.9f * 1.9f;
            for (int i = 0; i < static_cast<int>(base.m_placed.size()); ++i)
            {
                const Base::Placed& p = base.m_placed[i];
                const Base::ItemInfo& info = Base::Info(p.m_kind);
                if (info.m_use == Base::Use::None)
                {
                    continue;
                }
                const float dx = p.m_x - at.GetX();
                const float dy = p.m_y - at.GetY();
                const float d = AZStd::max(0.0f, std::sqrt(dx * dx + dy * dy) - 0.5f * AZStd::max(info.m_sizeX, info.m_sizeY));
                if (d * d < useD)
                {
                    useD = d * d;
                    use = i;
                }
            }
            if (m_crateUp)
            {
                const float dx = base.m_breachX + 0.4f - at.GetX();
                const float dy = base.m_breachY + 1.5f - at.GetY();
                if (dx * dx + dy * dy < 2.0f * 2.0f && dx * dx + dy * dy < useD + 0.01f)
                {
                    use = -2;
                    useD = dx * dx + dy * dy;
                }
            }
            if (use != -1 && best >= 0 && bestD < useD)
            {
                use = -1;
            }
        }
        const bool near = best >= 0 && use == -1;
        AZ::u64 nearId = near ? (Key(bestChunk->m_x, bestChunk->m_y) ^ (static_cast<AZ::u64>(bestChunk->m_pickups[best].m_index + 1) << 48)) : 0;
        if (use != -1)
        {
            nearId = 0xB0000000ull + static_cast<AZ::u64>(use + 8);
        }
        if (near != m_pickupPrompt || nearId != m_promptPickup || use != m_useTarget)
        {
            m_pickupPrompt = near;
            m_promptPickup = nearId;
            m_useTarget = use;
            AZStd::string verb, item, target;
            if (near)
            {
                verb = "Pick up";
                item = Base::SupplyName(bestChunk->m_pickups[best].m_kind);
                target = "pickup";
            }
            else if (use == -2)
            {
                verb = "Open";
                item = "Foundation crate";
                target = "use";
            }
            else if (use >= 0)
            {
                const Base::Placed& p = base.m_placed[use];
                target = "use";
                switch (Base::Info(p.m_kind).m_use)
                {
                case Base::Use::Rest: verb = "Rest"; item = "Army cot"; break;
                case Base::Use::Craft: verb = "Make"; item = "Flashlight battery \xc2\xb7 Scrap 2 \xc2\xb7 Wire 1"; break;
                case Base::Use::Study: verb = "Study"; item = "Research desk"; break;
                case Base::Use::Radio: verb = "Call"; item = "Foundation requisitions"; target = "radio"; break;
                case Base::Use::Store: verb = "Check"; item = Base::Info(p.m_kind).m_name; break;
                default: break;
                }
            }
            registry->Set("/NoclipProtocol/Hud/interact.verb", verb);
            registry->Set("/NoclipProtocol/Hud/interact.item", item);
            registry->Set("/NoclipProtocol/Hud/interact.target", target);
        }
        double now = 0.0;
        registry->Get(now, "/NoclipProtocol/Hud/time");
        auto say = [&](const AZStd::string& text)
        {
            registry->Set("/NoclipProtocol/Hud/toast", text);
            registry->Set("/NoclipProtocol/Hud/toast.until", now + 3.0);
        };
        if (base.m_useRequested)
        {
            base.m_useRequested = false;
            if (m_useTarget == -2)
            {
                say(base.OpenCrate());
            }
            else if (m_useTarget >= 0 && m_useTarget < static_cast<int>(base.m_placed.size()))
            {
                const Base::Placed& p = base.m_placed[m_useTarget];
                switch (Base::Info(p.m_kind).m_use)
                {
                case Base::Use::Rest:
                    if (base.m_restReady > 0.0f)
                    {
                        say(AZStd::string::format("YOU CAN'T SLEEP YET \xc2\xb7 %d:%02d", static_cast<int>(base.m_restReady) / 60, static_cast<int>(base.m_restReady) % 60));
                    }
                    else
                    {
                        double add = 0.0;
                        registry->Get(add, "/NoclipProtocol/Player/SanityAdd");
                        registry->Set("/NoclipProtocol/Player/SanityAdd", add + 0.4);
                        base.m_restReady = 120.0f;
                        say("YOU REST A WHILE \xc2\xb7 SANITY +40%");
                    }
                    break;
                case Base::Use::Craft:
                    if (base.m_scrap < 2 || base.m_wire < 1)
                    {
                        say("NOT ENOUGH \xc2\xb7 A BATTERY TAKES SCRAP 2 \xc2\xb7 WIRE 1");
                    }
                    else
                    {
                        base.m_scrap -= 2;
                        base.m_wire -= 1;
                        if (base.m_craftQueue == 0)
                        {
                            base.m_craftLeft = 45.0f;
                        }
                        base.m_craftQueue += 1;
                        base.Save();
                        say(AZStd::string::format("WORKBENCH \xc2\xb7 FLASHLIGHT BATTERY \xc3\x97%d QUEUED", base.m_craftQueue));
                    }
                    break;
                case Base::Use::Study:
                    say(base.Study(base.ResearchDone(0) ? 1 : 0));
                    break;
                case Base::Use::Store:
                    say(AZStd::string::format("BASE STORAGE %d / %d", base.Stored(), base.m_storageMax));
                    break;
                default:
                    break;
                }
            }
            m_promptPickup = ~0ull; // refresh the prompt
        }
        if (!base.m_pickRequested)
        {
            return;
        }
        base.m_pickRequested = false;
        if (!near)
        {
            return;
        }
        Chunk::LivePickup& pickup = bestChunk->m_pickups[best];
        // How much: scrap 2-4, wire 1-2, fuel 1, Almond Water 1, a document, a sample. Into the backpack (20 per trip),
        // Almond Water into your pocket.
        int amount = 1;
        switch (pickup.m_kind)
        {
        case 0: amount = 2 + pickup.m_index % 3; break;
        case 1: amount = 1 + pickup.m_index % 2; break;
        default: break;
        }
        AZStd::string text;
        if (pickup.m_kind != 3)
        {
            const int room = Base::State::PackMax - base.Carried();
            if (room <= 0)
            {
                say("BACKPACK FULL \xc2\xb7 20 / 20 \xc2\xb7 TAKE IT BACK TO THE BASE");
                return;
            }
            amount = AZStd::min(amount, room);
            base.m_pack[AZStd::clamp(pickup.m_kind, 0, 5)] += amount;
            static const char* const names[6] = { "SCRAP", "WIRE", "FUEL", "", "FOUNDATION DOCUMENT", "ODD SAMPLE" };
            if (pickup.m_kind == 4)
            {
                base.m_docsFound += 1;
            }
            if (pickup.m_kind == 5)
            {
                base.m_samplesFound += 1;
            }
            text = AZStd::string::format("+%d %s \xc2\xb7 BACKPACK %d / 20", amount, names[AZStd::clamp(pickup.m_kind, 0, 5)], base.Carried());
        }
        else
        {
            base.m_almondWater += 1;
            text = "+1 ALMOND WATER \xc2\xb7 [Q] DRINK";
        }
        if (auto* spawner = AzFramework::SpawnableEntitiesInterface::Get(); spawner && pickup.m_ticket.IsValid())
        {
            spawner->DespawnAllEntities(pickup.m_ticket);
        }
        m_collected.insert(Key(bestChunk->m_x, bestChunk->m_y) ^ (static_cast<AZ::u64>(pickup.m_index + 1) << 48));
        bestChunk->m_pickups.erase(bestChunk->m_pickups.begin() + best);
        m_pickupPrompt = false;
        m_promptPickup = 0;
        registry->Set("/NoclipProtocol/Hud/interact.verb", AZStd::string());
        registry->Set("/NoclipProtocol/Hud/interact.item", AZStd::string());
        registry->Set("/NoclipProtocol/Hud/interact.target", AZStd::string());
        say(text);
        base.Save();
    }
} // namespace NoclipProtocol
