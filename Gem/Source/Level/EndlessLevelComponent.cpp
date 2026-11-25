#include "EndlessLevelComponent.h"

#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Asset/AssetManagerBus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/chrono/chrono.h>
#include <AzFramework/Components/NonUniformScaleComponent.h>
#include <AzFramework/Components/TransformComponent.h>

#include <cstdlib>

namespace NoclipProtocol
{
    void EndlessLevelComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<EndlessLevelComponent, AZ::Component>()
                ->Version(1)
                ->Field("FollowEntity", &EndlessLevelComponent::m_followEntity)
                ->Field("Seed", &EndlessLevelComponent::m_seed)
                ->Field("RandomSeedEachRun", &EndlessLevelComponent::m_randomSeedEachRun)
                ->Field("LoadRadius", &EndlessLevelComponent::m_loadRadius)
                ->Field("UnloadRadius", &EndlessLevelComponent::m_unloadRadius)
                ->Field("ChunksPerFrame", &EndlessLevelComponent::m_chunksPerFrame)
                ->Field("ChunkCells", &EndlessLevelComponent::m_chunkCellsField)
                ->Field("LightFraction", &EndlessLevelComponent::m_lightFractionField)
                ->Field("OpenFraction", &EndlessLevelComponent::m_openFractionField)
                ->Field("WallPrefab", &EndlessLevelComponent::m_wallPrefab)
                ->Field("FloorPrefab", &EndlessLevelComponent::m_floorPrefab)
                ->Field("CeilingPrefab", &EndlessLevelComponent::m_ceilingPrefab)
                ->Field("LightPrefab", &EndlessLevelComponent::m_lightPrefab);

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
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_wallPrefab, "Wall piece", "Spawnable product path")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_floorPrefab, "Floor piece", "Spawnable product path")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_ceilingPrefab, "Ceiling piece", "Spawnable product path")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EndlessLevelComponent::m_lightPrefab, "Light piece", "Spawnable product path");
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
        m_settings.m_seed = m_seed;
        if (m_randomSeedEachRun)
        {
            const auto now = AZStd::chrono::steady_clock::now().time_since_epoch().count();
            m_settings.m_seed = static_cast<AZ::u64>(now) ^ (static_cast<AZ::u64>(reinterpret_cast<uintptr_t>(this)) << 17);
        }
        AZ_Printf("NoclipProtocol", "Endless Level 0 seed: %llu", static_cast<unsigned long long>(m_settings.m_seed));

        m_assetsMissing = !LoadPieceAssets();
        m_announced = false;
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
        const AZStd::string* paths[PieceCount] = { &m_wallPrefab, &m_floorPrefab, &m_ceilingPrefab, &m_lightPrefab };
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
        SpawnBox(chunk.m_tickets[PieceFloor], layout.m_floor, true);
        SpawnBox(chunk.m_tickets[PieceCeiling], layout.m_ceiling, true);
        for (const Level0::Point& light : layout.m_lights)
        {
            SpawnBox(chunk.m_tickets[PieceLight], Level0::Box{ light.m_x, light.m_y, light.m_z, 1.0f, 1.0f, 1.0f }, false);
        }

        m_chunks.emplace(Key(chunkX, chunkY), AZStd::move(chunk));
    }

    void EndlessLevelComponent::SpawnBox(AzFramework::EntitySpawnTicket& ticket, const Level0::Box& box, bool stretch)
    {
        const AZ::Vector3 translation(box.m_cx, box.m_cy, box.m_cz);
        const AZ::Vector3 scale(box.m_sx, box.m_sy, box.m_sz);

        AzFramework::SpawnAllEntitiesOptionalArgs args;
        args.m_preInsertionCallback =
            [translation, scale, stretch]([[maybe_unused]] AzFramework::EntitySpawnTicket::Id ticketId, AzFramework::SpawnableEntityContainerView view)
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
                config.m_localTransform = AZ::Transform::CreateTranslation(translation);
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
