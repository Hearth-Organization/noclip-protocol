#include "FirstPersonControllerComponent.h"
#include "../Render/RayTracing.h"
#include "../Hud/NoclipHudComponent.h"
#include "PlayerVitalsComponent.h"
#include "../Debug/GameBridgeComponent.h"
#include "WeaponsComponent.h"
#include "../Monsters/MonsterSpawnerComponent.h"
#include "DeathSequence.h"
#include <cstdlib>

#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/std/parallel/thread.h>
#include <AzFramework/Components/CameraBus.h>
#include <cmath>
#include <AzCore/Console/IConsole.h>
#include <AzCore/std/string/conversions.h>
#include <cctype>

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzFramework/Input/Buses/Requests/InputSystemCursorRequestBus.h>
#include <AzFramework/Input/Devices/Keyboard/InputDeviceKeyboard.h>
#include <AzFramework/Input/Devices/Mouse/InputDeviceMouse.h>
#include <AzFramework/Physics/CharacterBus.h>
#include <AzFramework/Physics/Common/PhysicsSceneQueries.h>
#include <AzFramework/Physics/PhysicsScene.h>

namespace NoclipProtocol
{
    namespace
    {
        // The player that is playing right now (there is one first-person player per game window).
        FirstPersonControllerComponent* s_activePlayer = nullptr;

        // Height of the ray-traced light grid's middle: halfway up Level 0's 3 m rooms.
        constexpr float RayTracedLightHeight = 1.5f;

        AZ::Entity* FindEntity(const AZ::EntityId& id)
        {
            AZ::Entity* entity = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(entity, &AZ::ComponentApplicationRequests::FindEntity, id);
            return entity;
        }

        void SetEntityActive(const AZ::EntityId& id, bool active)
        {
            AZ::Entity* entity = id.IsValid() ? FindEntity(id) : nullptr;
            if (!entity)
            {
                return;
            }
            if (active && entity->GetState() == AZ::Entity::State::Init)
            {
                entity->Activate();
            }
            else if (!active && entity->GetState() == AZ::Entity::State::Active)
            {
                entity->Deactivate();
            }
        }

        bool IsEntityActive(const AZ::EntityId& id)
        {
            AZ::Entity* entity = id.IsValid() ? FindEntity(id) : nullptr;
            return entity && entity->GetState() == AZ::Entity::State::Active;
        }

        // noclip_tp <x> <y> <z> [yaw degrees] [pitch degrees]: put the player somewhere, facing a way.
        // A developer command, so Claude (through the editor's script console) and the owner can take
        // screenshots and test spots without walking there.
        void NoclipTeleport(const AZ::ConsoleCommandContainer& arguments)
        {
            if (!s_activePlayer || arguments.size() < 3)
            {
                AZ_Printf("Noclip", "noclip_tp <x> <y> <z> [yaw] [pitch]%s", s_activePlayer ? "" : " (no player is playing)");
                return;
            }
            float values[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
            const size_t count = AZStd::min<size_t>(arguments.size(), 5);
            for (size_t i = 0; i < count; ++i)
            {
                values[i] = AZStd::stof(AZStd::string(arguments[i]));
            }
            s_activePlayer->Teleport(AZ::Vector3(values[0], values[1], values[2]), AZ::DegToRad(values[3]),
                AZ::DegToRad(values[4]), arguments.size() >= 4, arguments.size() >= 5);
            AZ_Printf("Noclip", "noclip_tp: %.2f %.2f %.2f yaw %.1f pitch %.1f", values[0], values[1], values[2], values[3], values[4]);
        }
        AZ_CONSOLEFREEFUNC("noclip_tp", NoclipTeleport, AZ::ConsoleFunctorFlags::Null,
            "Move the player: noclip_tp <x> <y> <z> [yaw degrees] [pitch degrees]");

        // Walks the player forward for a while, as if W were held: lets Claude test footsteps and movement
        // without a keyboard. noclip_walk <seconds> [sprint 0/1]
        void NoclipWalk(const AZ::ConsoleCommandContainer& arguments)
        {
            if (!s_activePlayer || arguments.empty())
            {
                AZ_Printf("Noclip", "noclip_walk <seconds> [sprint 0/1]%s", s_activePlayer ? "" : " (no player is playing)");
                return;
            }
            const float seconds = AZStd::stof(AZStd::string(arguments[0]));
            const bool sprint = arguments.size() > 1 && AZStd::string(arguments[1]) != "0";
            s_activePlayer->WalkFor(seconds, sprint);
            AZ_Printf("Noclip", "noclip_walk: %.1f s%s", seconds, sprint ? " sprinting" : "");
        }
        AZ_CONSOLEFREEFUNC("noclip_walk", NoclipWalk, AZ::ConsoleFunctorFlags::Null,
            "Walk the player forward: noclip_walk <seconds> [sprint 0/1]");

        // noclip_aim <seconds> [monster name]: keep the sights on a monster (default: the last one monster_spawn brought in),
        // turning the way a person does, for filming fights.
        void NoclipAim(const AZ::ConsoleCommandContainer& arguments)
        {
            if (!s_activePlayer || arguments.empty())
            {
                AZ_Printf("Noclip", "noclip_aim <seconds> [monster name]%s", s_activePlayer ? "" : " (no player)");
                return;
            }
            const float seconds = AZStd::stof(AZStd::string(arguments[0]));
            AZStd::string name;
            for (size_t i = 1; i < arguments.size(); ++i)
            {
                name += (i > 1 ? " " : "") + AZStd::string(arguments[i]);
            }
            s_activePlayer->AimFor(seconds, name);
            AZ_Printf("Noclip", "noclip_aim: %.1f s at %s", seconds, name.empty() ? "the last spawned monster" : name.c_str());
        }
        AZ_CONSOLEFREEFUNC("noclip_aim", NoclipAim, AZ::ConsoleFunctorFlags::Null,
            "Keep the sights on a monster: noclip_aim <seconds> [name, default the last monster_spawn]");

        // noclip_run <seconds> [sprint 0/1] [first turn: l/r]: run through the maze on its own, turning at walls (trailer).
        void NoclipRun(const AZ::ConsoleCommandContainer& arguments)
        {
            if (!s_activePlayer || arguments.empty())
            {
                AZ_Printf("Noclip", "noclip_run <seconds> [sprint 0/1] [l/r]%s", s_activePlayer ? "" : " (no player)");
                return;
            }
            const float seconds = AZStd::stof(AZStd::string(arguments[0]));
            const bool sprint = arguments.size() > 1 && AZStd::string(arguments[1]) != "0";
            const float side = arguments.size() > 2 && AZStd::string(arguments[2]) == "r" ? -1.0f : 1.0f;
            s_activePlayer->RunFor(seconds, sprint, side);
            AZ_Printf("Noclip", "noclip_run: %.1f s%s", seconds, sprint ? " sprinting" : "");
        }
        AZ_CONSOLEFREEFUNC("noclip_run", NoclipRun, AZ::ConsoleFunctorFlags::Null,
            "Run through the maze on autopilot: noclip_run <seconds> [sprint 0/1] [first turn l/r]");

        // weapon_give <all | pistol | shotgun | carbine | flaregun | knife | shield | ammo | flares | medkit>: as if the
        // Foundation's crate had brought it (testing).
        void WeaponGive(const AZ::ConsoleCommandContainer& arguments)
        {
            const AZStd::string what = arguments.empty() ? AZStd::string("all") : AZStd::string(arguments[0]);
            Base::State& base = Base::Get();
            auto give = [&](const char* key, const char* name)
            {
                if (what == "all" || what == key)
                {
                    base.Unpack(name);
                }
            };
            give("pistol", "Service pistol");
            give("shotgun", "Pump shotgun");
            if (what == "all" || what == "carbine")
            {
                base.m_weaponsI = true; // the carbine needs Weapons I
                base.Unpack("Carbine");
                base.Unpack("Carbine magazines");
            }
            give("flaregun", "Flare gun");
            give("knife", "Combat knife");
            give("shield", "Riot shield");
            give("ammo", "Ammunition crate");
            give("flares", "Flares");
            give("medkit", "Medkit");
            if (what == "all")
            {
                base.Unpack("Pistol rounds");
                base.Unpack("Shotgun shells");
                base.Unpack("Flare gun cartridges");
            }
            base.Save();
            AZ_Printf("Noclip", "weapon_give %s", what.c_str());
        }
        AZ_CONSOLEFREEFUNC("weapon_give", WeaponGive, AZ::ConsoleFunctorFlags::Null,
            "weapon_give <all|pistol|shotgun|carbine|flaregun|knife|shield|ammo|flares|medkit> (testing)");

        // weapon_do <fire | aim 0/1 | reload | melee | flare | medkit | switch 1-3 | equip 0-5 | wheel 0-5 / -1>: the
        // weapon keys without a keyboard (testing).
        void WeaponDo(const AZ::ConsoleCommandContainer& arguments)
        {
            AZ::Entity* ui = s_activePlayer ? s_activePlayer->UiEntity() : nullptr;
            WeaponsComponent* weapons = ui ? ui->FindComponent<WeaponsComponent>() : nullptr;
            if (!weapons || arguments.empty())
            {
                AZ_Printf("Noclip", "weapon_do <fire|aim|reload|melee|flare|medkit|switch n|equip n|wheel n>%s", weapons ? "" : " (no player)");
                return;
            }
            const int value = arguments.size() > 1 ? AZStd::stoi(AZStd::string(arguments[1])) : 0;
            weapons->Command(AZStd::string(arguments[0]), value);
        }
        // monster_spawn <smiler>: one monster 10 m ahead of the player, now (testing; Level 0 only).
        void MonsterSpawn(const AZ::ConsoleCommandContainer& arguments)
        {
            AZStd::string kind = arguments.empty() ? AZStd::string("Smiler") : AZStd::string(arguments[0]);
            if (!kind.empty())
            {
                kind[0] = static_cast<char>(toupper(kind[0]));
            }
            if (arguments.size() > 1)
            {
                kind += "@" + AZStd::string(arguments[1]); // monster_spawn <kind> <meters ahead> (the trailer's scare)
            }
            MonsterSpawnerComponent::Request(kind);
            AZ_Printf("Noclip", "monster_spawn %s", kind.c_str());
        }
        AZ_CONSOLEFREEFUNC("monster_spawn", MonsterSpawn, AZ::ConsoleFunctorFlags::Null, "monster_spawn <smiler> [meters ahead] (testing, Level 0)");

        // monster_debug [freeze 0/1]: lists every monster (where it is, its components); freeze 1 stops them in
        // place (they keep playing their idle) for screenshots, freeze 0 lets them go again.
        void MonsterDebug(const AZ::ConsoleCommandContainer& arguments)
        {
            if (arguments.size() >= 2 && AZStd::string(arguments[0]) == "freeze")
            {
                const bool freeze = AZStd::string(arguments[1]) != "0";
                if (auto* registry = AZ::SettingsRegistry::Get())
                {
                    registry->Set("/NoclipProtocol/Monsters/Enabled", !freeze);
                }
                AZ_Printf("Noclip", "monster_debug: %s", freeze ? "frozen" : "moving");
                return;
            }
            AZ::ComponentApplicationBus::Broadcast(&AZ::ComponentApplicationRequests::EnumerateEntities, [](AZ::Entity* entity)
            {
                if (!entity->FindComponent<HearthMonsterComponent>())
                {
                    return;
                }
                AZ::Transform tm = AZ::Transform::CreateIdentity();
                AZ::TransformBus::EventResult(tm, entity->GetId(), &AZ::TransformBus::Events::GetWorldTM);
                AZStd::string comps;
                for (AZ::Component* c : entity->GetComponents())
                {
                    comps += AZStd::string(c->RTTI_GetTypeName()) + " ";
                }
                AZ_Printf("Noclip", "monster %s [%s] at %.2f %.2f %.2f scale %.2f state %d: %s", entity->GetName().c_str(),
                    entity->GetId().ToString().c_str(), tm.GetTranslation().GetX(), tm.GetTranslation().GetY(), tm.GetTranslation().GetZ(),
                    tm.GetUniformScale(), static_cast<int>(entity->GetState()), comps.c_str());
            });
        }
        AZ_CONSOLEFREEFUNC("monster_debug", MonsterDebug, AZ::ConsoleFunctorFlags::Null, "monster_debug [freeze 0/1] (testing)");

        AZ_CONSOLEFREEFUNC("weapon_do", WeaponDo, AZ::ConsoleFunctorFlags::Null,
            "weapon_do <fire|aim 0/1|reload|melee|flare|medkit|switch 1-3|equip 0-5|wheel 0-5/-1> (testing)");
    } // namespace

    AZ::ComponentDescriptor* CreateWeaponsDescriptor()
    {
        return WeaponsComponent::CreateDescriptor();
    }

    namespace
    {
        // player_die [Kind]: drop the player's health to 0, as if Kind (default Smiler) got them (testing the death sequence).
        void PlayerDie(const AZ::ConsoleCommandContainer& arguments)
        {
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Set("/NoclipProtocol/Player/LastHitBy", AZStd::string(arguments.empty() ? "Smiler" : AZStd::string(arguments.front()).c_str()));
                registry->Set("/NoclipProtocol/Player/Damage", 5.0);
            }
        }
        AZ_CONSOLEFREEFUNC("player_die", PlayerDie, AZ::ConsoleFunctorFlags::Null, "player_die [Kind]: die now, killed by Kind (testing)");
    }

    AZ::ComponentDescriptor* CreateDeathSequenceDescriptor()
    {
        return DeathSequenceComponent::CreateDescriptor();
    }

    AZ::ComponentDescriptor* CreateVoicesDescriptor()
    {
        return VoicesComponent::CreateDescriptor();
    }

    AZ::ComponentDescriptor* CreateMonsterSpawnerDescriptor()
    {
        return MonsterSpawnerComponent::CreateDescriptor();
    }

    void FirstPersonControllerComponent::Teleport(const AZ::Vector3& position, float yaw, float pitch, bool setYaw, bool setPitch)
    {
        // A ray-traced light grid that jumps a long way can crash the graphics driver: switch it off for a few
        // frames and let it start again at the new spot.
        if (m_rayTracedLight.IsValid())
        {
            SetEntityActive(m_rayTracedLight, false);
            m_rayTracedLightPause = 6;
        }
        Physics::CharacterRequestBus::Event(GetEntityId(), &Physics::CharacterRequests::SetBasePosition, position);
        AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldTranslation, position);
        if (setYaw)
        {
            m_yaw = yaw;
            m_facingRead = true;
            m_lastWrittenRotation = AZ::Quaternion::CreateRotationZ(m_yaw);
            AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldRotationQuaternion, m_lastWrittenRotation);
        }
        if (setPitch)
        {
            m_pitch = pitch;
        }
        m_verticalSpeed = 0.0f;
    }

    void FirstPersonControllerComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<FirstPersonControllerComponent, AZ::Component>()
                ->Version(1)
                ->Field("CameraEntity", &FirstPersonControllerComponent::m_cameraEntity)
                ->Field("WalkSpeed", &FirstPersonControllerComponent::m_walkSpeed)
                ->Field("SprintSpeed", &FirstPersonControllerComponent::m_sprintSpeed)
                ->Field("JumpSpeed", &FirstPersonControllerComponent::m_jumpSpeed)
                ->Field("Gravity", &FirstPersonControllerComponent::m_gravity)
                ->Field("MouseSensitivity", &FirstPersonControllerComponent::m_mouseSensitivity)
                ->Field("MaxPitchDegrees", &FirstPersonControllerComponent::m_maxPitchDegrees);

            if (auto edit = serialize->GetEditContext())
            {
                edit->Class<FirstPersonControllerComponent>("First Person Controller", "WASD movement, mouse look, sprint and jump")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Noclip Protocol")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FirstPersonControllerComponent::m_cameraEntity,
                        "Camera entity", "Child entity with the Camera component (gets the up/down look)")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FirstPersonControllerComponent::m_walkSpeed,
                        "Walk speed", "Meters per second")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FirstPersonControllerComponent::m_sprintSpeed,
                        "Sprint speed", "Meters per second while holding Shift")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FirstPersonControllerComponent::m_jumpSpeed,
                        "Jump speed", "Upward speed when jumping")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FirstPersonControllerComponent::m_gravity,
                        "Gravity", "Downward acceleration")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FirstPersonControllerComponent::m_mouseSensitivity,
                        "Mouse sensitivity", "Radians per mouse unit")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FirstPersonControllerComponent::m_maxPitchDegrees,
                        "Max look up/down", "Degrees");
            }
        }
    }

    void FirstPersonControllerComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        required.push_back(AZ_CRC_CE("TransformService"));
        required.push_back(AZ_CRC_CE("PhysicsCharacterControllerService"));
    }

    void FirstPersonControllerComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("NoclipFirstPersonControllerService"));
    }

    void FirstPersonControllerComponent::Activate()
    {
        // The facing is read on the first frame (see OnTick), after other components such as the
        // level generator have had the chance to turn the player.
        m_facingRead = false;
        m_pitch = 0.0f;
        s_activePlayer = this;

        AzFramework::InputChannelEventListener::Connect();
        AZ::TickBus::Handler::BusConnect();
        SetCursorCaptured(true);

        // The in-game UI and the player's vitals live on their own runtime entity.
        m_uiEntity = aznew AZ::Entity("Noclip UI");
        m_uiEntity->CreateComponent<NoclipHudComponent>();
        m_uiEntity->CreateComponent<PlayerVitalsComponent>();
        m_uiEntity->CreateComponent<GameBridgeComponent>();
        m_uiEntity->CreateComponent<WeaponsComponent>()->m_player = GetEntityId();
        m_uiEntity->CreateComponent<MonsterSpawnerComponent>()->m_player = GetEntityId();
        auto* death = m_uiEntity->CreateComponent<DeathSequenceComponent>();
        death->m_player = GetEntityId();
        death->m_camera = m_cameraEntity;
        m_uiEntity->CreateComponent<VoicesComponent>()->m_player = GetEntityId();
        m_uiEntity->Init();
        m_uiEntity->Activate();
    }

    void FirstPersonControllerComponent::Deactivate()
    {
        if (s_activePlayer == this)
        {
            s_activePlayer = nullptr;
        }
        SetCursorCaptured(false);
        AZ::TickBus::Handler::BusDisconnect();
        AzFramework::InputChannelEventListener::Disconnect();
        if (m_uiEntity)
        {
            m_uiEntity->Deactivate();
            delete m_uiEntity;
            m_uiEntity = nullptr;
        }
    }

    void FirstPersonControllerComponent::SetCursorCaptured(bool captured)
    {
        m_cursorCaptured = captured;
        AzFramework::InputSystemCursorRequestBus::Event(
            AzFramework::InputDeviceMouse::Id,
            &AzFramework::InputSystemCursorRequests::SetSystemCursorState,
            captured ? AzFramework::SystemCursorState::ConstrainedAndHidden
                     : AzFramework::SystemCursorState::UnconstrainedAndVisible);
    }

    bool FirstPersonControllerComponent::OnInputChannelEventFiltered(const AzFramework::InputChannel& inputChannel)
    {
        using Keyboard = AzFramework::InputDeviceKeyboard::Key;
        using Mouse = AzFramework::InputDeviceMouse;

        const AzFramework::InputChannelId& id = inputChannel.GetInputChannelId();
        const bool held = inputChannel.IsActive();

        // Dead: the death sequence has the camera; any key or button skips the card ahead.
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            bool dead = false;
            if (registry->Get(dead, "/NoclipProtocol/Player/Dead") && dead)
            {
                if (inputChannel.IsStateBegan() && id != Mouse::Movement::X && id != Mouse::Movement::Y &&
                    id != Mouse::Movement::Z && id != Mouse::SystemCursorPosition)
                {
                    registry->Set("/NoclipProtocol/Death/Skip", true);
                }
                return false;
            }
        }

        if (id == Mouse::Movement::X)
        {
            m_mouseDeltaX += inputChannel.GetValue();
        }
        else if (id == Mouse::Movement::Y)
        {
            m_mouseDeltaY += inputChannel.GetValue();
        }
        else if (id == Keyboard::AlphanumericW) { m_forward = held; }
        else if (id == Keyboard::AlphanumericS) { m_back = held; }
        else if (id == Keyboard::AlphanumericA) { m_left = held; }
        else if (id == Keyboard::AlphanumericD) { m_right = held; }
        else if (id == Keyboard::ModifierShiftL) { m_sprint = held; }
        else if (id == Keyboard::EditSpace)
        {
            if (inputChannel.IsStateBegan())
            {
                m_jumpRequested = true;
            }
        }
        else if (id == Keyboard::Escape && inputChannel.IsStateBegan())
        {
            // Escape frees the mouse; clicking in the window grabs it again.
            SetCursorCaptured(false);
        }
        else if (id == Mouse::Button::Left && inputChannel.IsStateBegan() && !m_cursorCaptured)
        {
            SetCursorCaptured(true);
        }

        return false; // never swallow input; other components may want it too
    }

    void FirstPersonControllerComponent::UpdateRayTracedLight()
    {
        if (!m_rayTracedLightSearched)
        {
            m_rayTracedLightSearched = true;
            AZStd::vector<AZ::EntityId> descendants;
            AZ::TransformBus::EventResult(descendants, GetEntityId(), &AZ::TransformBus::Events::GetAllDescendants);
            for (const AZ::EntityId& id : descendants)
            {
                AZ::Entity* entity = FindEntity(id);
                if (entity && entity->GetName() == "RayTracedLight")
                {
                    m_rayTracedLight = id;
                    break;
                }
            }
            if (!m_rayTracedLight.IsValid())
            {
                return;
            }
            // Off the player, so turning doesn't rotate the grid (it only supports moving).
            AZ::TransformBus::Event(m_rayTracedLight, &AZ::TransformBus::Events::SetParent, AZ::EntityId());
            AZ_Printf("NoclipProtocol", "Ray-traced bounce light: %s", RayTracing::IsOn() ? "on" : "off (no ray tracing here, or turned off)");
        }
        if (!m_rayTracedLight.IsValid())
        {
            return;
        }

        const bool wanted = RayTracing::IsOn();
        if (m_rayTracedLightPause > 0)
        {
            --m_rayTracedLightPause;
            if (m_rayTracedLightPause > 0)
            {
                return;
            }
        }
        if (!wanted)
        {
            SetEntityActive(m_rayTracedLight, false);
            return;
        }
        SetEntityActive(m_rayTracedLight, true);
        if (!IsEntityActive(m_rayTracedLight))
        {
            return;
        }
        AZ::Vector3 at = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(at, GetEntityId(), &AZ::TransformBus::Events::GetWorldTranslation);
        at.SetZ(RayTracedLightHeight);
        AZ::TransformBus::Event(m_rayTracedLight, &AZ::TransformBus::Events::SetWorldTM, AZ::Transform::CreateTranslation(at));
    }

    void FirstPersonControllerComponent::Steer(float deltaTime)
    {
        // Feel ahead with rays at chest height, the way a runner glances: keep to the middle of the corridor, and at a
        // wall turn towards the open side (the given side first when both are open), easing in and out of the turn.
        auto* scenes = AZ::Interface<AzPhysics::SceneInterface>::Get();
        const AzPhysics::SceneHandle scene = scenes ? scenes->GetSceneHandle(AzPhysics::DefaultPhysicsSceneName) : AzPhysics::InvalidSceneHandle;
        if (scene == AzPhysics::InvalidSceneHandle)
        {
            AZ_Printf("Noclip", "noclip_run: no physics scene");
            return;
        }
        AZ::Vector3 at = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(at, GetEntityId(), &AZ::TransformBus::Events::GetWorldTranslation);
        if (m_cameraEntity.IsValid())
        {
            AZ::TransformBus::EventResult(at, m_cameraEntity, &AZ::TransformBus::Events::GetWorldTranslation);
            at -= AZ::Vector3(0.0f, 0.0f, 0.4f);
        }
        const AZ::EntityId self = GetEntityId();
        auto free = [&](float offsetDegrees)
        {
            const float yaw = m_yaw + AZ::DegToRad(offsetDegrees);
            AzPhysics::RayCastRequest request;
            request.m_start = at;
            request.m_direction = AZ::Quaternion::CreateRotationZ(yaw).TransformVector(AZ::Vector3::CreateAxisY());
            request.m_distance = 12.0f;
            request.m_filterCallback = [self](const AzPhysics::SimulatedBody* body, [[maybe_unused]] const Physics::Shape* shape)
            {
                return body && body->GetEntityId() == self ? AzPhysics::SceneQuery::QueryHitType::None : AzPhysics::SceneQuery::QueryHitType::Block;
            };
            AzPhysics::SceneQueryHits hits = scenes->QueryScene(scene, &request);
            return hits ? hits.m_hits.front().m_distance : 12.0f;
        };
        m_steerTime += deltaTime;
        const float ahead = free(0.0f);
        const float left = free(50.0f), right = free(-50.0f);
        const float farLeft = free(90.0f), farRight = free(-90.0f);
        float want = 0.0f; // yaw speed wanted, radians a second (+ is left)
        if (ahead < 3.2f)
        {
            // A wall: turn hard to the open side.
            const float l = left + farLeft * 0.5f, r = right + farRight * 0.5f;
            const float side = std::fabs(l - r) < 1.5f ? m_steerSide : (l > r ? 1.0f : -1.0f);
            want = side * AZ::DegToRad(150.0f);
        }
        else
        {
            // Open ahead: drift to the middle (more room on one side pulls that way), and a turn now and then into a
            // side opening, as if looking for the way out.
            want = AZ::GetClamp((left - right) * 0.10f, -0.6f, 0.6f);
            if (m_steerTime > 2.2f && (m_steerSide > 0.0f ? farLeft : farRight) > 7.0f && ahead < 9.0f)
            {
                want = m_steerSide * AZ::DegToRad(130.0f);
                m_steerTime = 1.2f;
            }
        }
        if (static_cast<int>((m_steerTime - deltaTime) * 2.0f) != static_cast<int>(m_steerTime * 2.0f))
        {
            AZ_Printf("Noclip", "noclip_run: at %.1f %.1f %.1f yaw %.0f ahead %.1f left %.1f right %.1f far %.1f %.1f -> %.0f deg/s",
                at.GetX(), at.GetY(), at.GetZ(), AZ::RadToDeg(m_yaw), ahead, left, right, farLeft, farRight, AZ::RadToDeg(want));
        }
        const float ease = AZ::GetMin(1.0f, deltaTime * 6.0f);
        m_steerRate += (want - m_steerRate) * ease;
        m_yaw += m_steerRate * deltaTime;
        // The head bobs with the run and looks a little down, like someone running scared.
        m_pitch += (AZ::DegToRad(-4.0f) + AZ::DegToRad(1.2f) * std::sin(m_steerTime * 9.0f) - m_pitch) * ease * 0.5f;
    }

    void FirstPersonControllerComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        if (deltaTime <= 0.0f)
        {
            return;
        }
        UpdateRayTracedLight();
        bool paused = false;
        double sensitivity = 1.0;
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Get(paused, "/NoclipProtocol/Paused");
            registry->Get(sensitivity, "/NoclipProtocol/Player/MouseSensitivity");
        }
        bool dead = false;
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Get(dead, "/NoclipProtocol/Player/Dead");
        }
        if (dead)
        {
            // The death sequence moves the camera; the body stays where it fell.
            m_mouseDeltaX = 0.0f;
            m_mouseDeltaY = 0.0f;
            m_jumpRequested = false;
            m_forward = m_back = m_left = m_right = m_sprint = false;
            return;
        }
        if (paused)
        {
            // The pause menu needs the mouse; the player stands still.
            if (m_cursorCaptured)
            {
                SetCursorCaptured(false);
            }
            m_mouseDeltaX = 0.0f;
            m_mouseDeltaY = 0.0f;
            m_jumpRequested = false;
            return;
        }
        if (!m_cursorCaptured)
        {
            SetCursorCaptured(true);
        }
        m_mouseDeltaX *= static_cast<float>(sensitivity);
        m_mouseDeltaY *= static_cast<float>(sensitivity);
        {
            // Pick up the facing on the first frame, and whenever something else turns the player
            // (the level generator at the start of a run, a teleport...).
            AZ::Quaternion rotation = AZ::Quaternion::CreateIdentity();
            AZ::TransformBus::EventResult(rotation, GetEntityId(), &AZ::TransformBus::Events::GetWorldRotationQuaternion);
            if (!m_facingRead || !rotation.IsClose(m_lastWrittenRotation, 0.0005f))
            {
                m_yaw = rotation.GetEulerRadiansXYZ().GetZ();
                m_facingRead = true;
            }
        }

        // --- Look --- (held still while the weapon wheel is open; a shot's recoil kicks the view up)
        bool lookLocked = false;
        double recoil = 0.0;
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Get(lookLocked, "/NoclipProtocol/Player/LookLocked");
            if (registry->Get(recoil, "/NoclipProtocol/Player/Recoil") && recoil != 0.0)
            {
                registry->Set("/NoclipProtocol/Player/Recoil", 0.0);
            }
        }
        if (lookLocked)
        {
            m_mouseDeltaX = 0.0f;
            m_mouseDeltaY = 0.0f;
        }
        m_pitch += static_cast<float>(recoil);
        // Settings › Controls: invert look; Display: field of view; the frame-rate limit.
        bool invert = false;
        double fov = 95.0, limit = 0.0;
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Get(invert, "/NoclipProtocol/Player/InvertLook");
            registry->Get(fov, "/NoclipProtocol/Display/Fov");
            registry->Get(limit, "/NoclipProtocol/Display/FrameLimit");
        }
        if (m_cameraEntity.IsValid() && std::fabs(fov - m_appliedFov) > 0.01)
        {
            // Horizontal field of view (as players set it) to the camera's vertical one, for the window's shape.
            float fw = 16.0f, fh = 9.0f;
            Camera::CameraRequestBus::EventResult(fw, m_cameraEntity, &Camera::CameraComponentRequests::GetFrustumWidth);
            Camera::CameraRequestBus::EventResult(fh, m_cameraEntity, &Camera::CameraComponentRequests::GetFrustumHeight);
            const float aspect = fh > 0.0f ? fw / fh : 16.0f / 9.0f;
            const float vertical = 2.0f * std::atan(std::tan(AZ::DegToRad(static_cast<float>(fov)) * 0.5f) / AZStd::max(0.5f, aspect));
            Camera::CameraRequestBus::Event(m_cameraEntity, &Camera::CameraComponentRequests::SetFovRadians, vertical);
            m_appliedFov = fov;
        }
        if (limit > 0.0 && deltaTime < 1.0 / limit)
        {
            AZStd::this_thread::sleep_for(AZStd::chrono::microseconds(static_cast<int64_t>((1.0 / limit - deltaTime) * 1e6 * 0.9)));
        }
        if (m_cursorCaptured)
        {
            m_yaw -= m_mouseDeltaX * m_mouseSensitivity;
            m_pitch -= m_mouseDeltaY * m_mouseSensitivity * (invert ? -1.0f : 1.0f);
            const float maxPitch = AZ::DegToRad(m_maxPitchDegrees);
            m_pitch = AZ::GetClamp(m_pitch, -maxPitch, maxPitch);
        }
        m_mouseDeltaX = 0.0f;
        m_mouseDeltaY = 0.0f;

        if (m_autoSteer && m_autoWalkSeconds > 0.0f)
        {
            Steer(deltaTime);
        }
        if (m_aimFor > 0.0f)
        {
            m_aimFor -= deltaTime;
            auto* registry = AZ::SettingsRegistry::Get();
            AZStd::string name = m_aimName;
            if (name.empty() && registry)
            {
                registry->Get(name, "/NoclipProtocol/Monsters/LastSpawned");
            }
            double mx = 0.0, my = 0.0;
            AZStd::string state;
            if (registry && !name.empty() && registry->Get(mx, "/NoclipProtocol/Monsters/" + name + "/X") &&
                registry->Get(my, "/NoclipProtocol/Monsters/" + name + "/Y") &&
                !(registry->Get(state, "/NoclipProtocol/Monsters/" + name + "/State") && state == "Dead"))
            {
                AZ::Vector3 eye = AZ::Vector3::CreateZero();
                AZ::TransformBus::EventResult(eye, m_cameraEntity.IsValid() ? m_cameraEntity : GetEntityId(),
                                              &AZ::TransformBus::Events::GetWorldTranslation);
                AZ::Vector3 feet = AZ::Vector3::CreateZero();
                AZ::TransformBus::EventResult(feet, GetEntityId(), &AZ::TransformBus::Events::GetWorldTranslation);
                const float dx = static_cast<float>(mx) - eye.GetX(), dy = static_cast<float>(my) - eye.GetY();
                const float flat = std::sqrt(dx * dx + dy * dy);
                if (flat > 0.3f)
                {
                    // Chest height on whatever it is; a person turns fast but not instantly, and overshoots a little less up close.
                    const float wantYaw = std::atan2(-dx, dy);
                    const float wantPitch = std::atan2(feet.GetZ() + 1.15f - eye.GetZ(), flat);
                    const float yawDiff = std::remainder(wantYaw - m_yaw, AZ::Constants::TwoPi);
                    const float rate = AZ::DegToRad(260.0f) * deltaTime;
                    m_yaw += AZStd::clamp(yawDiff * AZStd::min(1.0f, deltaTime * 9.0f), -rate, rate);
                    m_pitch += AZStd::clamp((wantPitch - m_pitch) * AZStd::min(1.0f, deltaTime * 8.0f), -rate, rate);
                }
            }
        }
        m_lastWrittenRotation = AZ::Quaternion::CreateRotationZ(m_yaw);
        AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldRotationQuaternion, m_lastWrittenRotation);
        // A blow from a monster snaps the head: a jolt back and to one side that settles in about half a second.
        {
            double health = 1.0;
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Get(health, "/NoclipProtocol/Hud/player.health");
            }
            if (static_cast<float>(health) < m_lastHealth - 0.02f)
            {
                m_hitKick = 1.0f;
                m_hitSide = (std::rand() & 1) ? 1.0f : -1.0f;
            }
            m_lastHealth = static_cast<float>(health);
            m_hitKick *= std::exp(-7.0f * deltaTime);
            if (m_hitKick < 0.01f)
            {
                m_hitKick = 0.0f;
            }
        }
        if (m_cameraEntity.IsValid())
        {
            const float k = m_hitKick * m_hitKick * (3.0f - 2.0f * m_hitKick); // sharp start, soft settle
            const float shake = m_hitKick > 0.0f ? std::sin(m_hitKick * 38.0f) * 0.25f : 0.0f;
            AZ::TransformBus::Event(m_cameraEntity, &AZ::TransformBus::Events::SetLocalRotationQuaternion,
                AZ::Quaternion::CreateRotationZ(AZ::DegToRad(6.0f) * m_hitSide * k) *
                AZ::Quaternion::CreateRotationX(m_pitch + AZ::DegToRad(7.0f) * k) *
                AZ::Quaternion::CreateRotationY(AZ::DegToRad(14.0f) * m_hitSide * (k + shake * m_hitKick)));
        }

        // --- Fly (noclip), from the debug overlay (F4): through walls, where you look, no gravity ---
        bool fly = false;
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Get(fly, "/NoclipProtocol/Player/Fly");
        }
        if (fly)
        {
            const float forwardFly = (m_forward ? 1.0f : 0.0f) - (m_back ? 1.0f : 0.0f);
            const float rightFly = (m_right ? 1.0f : 0.0f) - (m_left ? 1.0f : 0.0f);
            const AZ::Quaternion look = AZ::Quaternion::CreateRotationZ(m_yaw) * AZ::Quaternion::CreateRotationX(m_pitch);
            AZ::Vector3 flyMove = look.TransformVector(AZ::Vector3(rightFly, forwardFly, 0.0f));
            if (m_jumpRequested)
            {
                flyMove += AZ::Vector3::CreateAxisZ();
            }
            m_jumpRequested = false;
            if (flyMove.GetLengthSq() > 1.0f)
            {
                flyMove.Normalize();
            }
            AZ::Vector3 at = AZ::Vector3::CreateZero();
            AZ::TransformBus::EventResult(at, GetEntityId(), &AZ::TransformBus::Events::GetWorldTranslation);
            at += flyMove * (m_sprint ? m_sprintSpeed * 3.0f : m_walkSpeed * 2.0f) * deltaTime;
            Physics::CharacterRequestBus::Event(GetEntityId(), &Physics::CharacterRequests::SetBasePosition, at);
            m_verticalSpeed = 0.0f;
            return;
        }

        // --- Ground check ---
        // While falling, the character moves down at our vertical speed. If it moved down
        // much slower than asked, something (the floor) stopped it, so it's on the ground.
        AZ::Vector3 actualVelocity = AZ::Vector3::CreateZero();
        Physics::CharacterRequestBus::EventResult(actualVelocity, GetEntityId(), &Physics::CharacterRequests::GetVelocity);
        if (m_verticalSpeed <= 0.0f)
        {
            m_grounded = actualVelocity.GetZ() > m_verticalSpeed * 0.5f;
        }
        else
        {
            m_grounded = false;
        }

        // --- Jump and gravity ---
        if (m_grounded)
        {
            m_verticalSpeed = -1.0f; // small push down keeps it stuck to floors and stairs
            if (m_jumpRequested)
            {
                m_verticalSpeed = m_jumpSpeed;
                m_grounded = false;
            }
        }
        else
        {
            m_verticalSpeed -= m_gravity * deltaTime;
            m_verticalSpeed = AZ::GetMax(m_verticalSpeed, -50.0f);
        }
        m_jumpRequested = false;

        // --- Walk / sprint ---
        const bool autoWalk = m_autoWalkSeconds > 0.0f;
        if (autoWalk)
        {
            m_autoWalkSeconds -= deltaTime;
        }
        float forwardInput = ((m_forward || autoWalk) ? 1.0f : 0.0f) - (m_back ? 1.0f : 0.0f);
        float rightInput = (m_right ? 1.0f : 0.0f) - (m_left ? 1.0f : 0.0f);

        // O3DE: +Y is forward, +X is right, +Z is up. Rotate the input by the yaw.
        const AZ::Quaternion yawRotation = AZ::Quaternion::CreateRotationZ(m_yaw);
        AZ::Vector3 move = yawRotation.TransformVector(AZ::Vector3(rightInput, forwardInput, 0.0f));
        if (move.GetLengthSq() > 1.0f)
        {
            move.Normalize();
        }
        bool exhausted = false;
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Get(exhausted, "/NoclipProtocol/Player/Exhausted");
        }
        const bool sprinting = (m_sprint || (autoWalk && m_autoWalkSprint)) && forwardInput > 0.0f && !exhausted;
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Set("/NoclipProtocol/Player/Sprinting", sprinting);
        }
        const float speed = sprinting ? m_sprintSpeed : m_walkSpeed;
        move *= speed;
        move.SetZ(m_verticalSpeed);

        KeepOutOfWalls(move, deltaTime);
        Physics::CharacterRequestBus::Event(GetEntityId(), &Physics::CharacterRequests::AddVelocityForTick, move);
    }

    void FirstPersonControllerComponent::KeepOutOfWalls(AZ::Vector3& move, float deltaTime)
    {
        // The owner, Nov 28: "see how we ran thru a wall that cant happen". The character controller let the body slip
        // into a wall in a corner (once its capsule overlaps a wall, the controller stops seeing that wall) and then out
        // the other side. So, on top of the controller: sweep the body's capsule (above the step height, so floors and
        // stairs don't count) along this frame's move against the level's static walls; if it's already in one, push it
        // back out the way it came in (the cast's MTD); and take the part of the move that goes into a wall away.
        auto* scenes = AZ::Interface<AzPhysics::SceneInterface>::Get();
        const AzPhysics::SceneHandle scene = scenes ? scenes->GetSceneHandle(AzPhysics::DefaultPhysicsSceneName) : AzPhysics::InvalidSceneHandle;
        if (scene == AzPhysics::InvalidSceneHandle)
        {
            return;
        }
        AZ::Vector3 base = AZ::Vector3::CreateZero();
        Physics::CharacterRequestBus::EventResult(base, GetEntityId(), &Physics::CharacterRequests::GetBasePosition);
        // What counts as a wall: anything solid but the player and the monsters (they're handled by combat).
        const AZ::EntityId self = GetEntityId();
        auto wallsOnly = [self](const AzPhysics::SimulatedBody* body, [[maybe_unused]] const Physics::Shape* shape)
        {
            if (!body || body->GetEntityId() == self)
            {
                return AzPhysics::SceneQuery::QueryHitType::None;
            }
            AZ::Entity* entity = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(entity, &AZ::ComponentApplicationRequests::FindEntity, body->GetEntityId());
            if (entity && entity->FindComponent<HearthMonsterComponent>())
            {
                return AzPhysics::SceneQuery::QueryHitType::None;
            }
            return AzPhysics::SceneQuery::QueryHitType::Block;
        };
        // The last line of defence: the body's path since last frame may not cross a wall. Rays at knee and chest
        // height from where it was to where it is; if one hits a wall on the way, it goes back to where it was.
        // (A jump of more than 3 m is a teleport, a level change or a respawn, and is let through.)
        if (m_safeKnown)
        {
            const AZ::Vector3 step = base - m_safeAt;
            const float length = AZ::Vector3(step.GetX(), step.GetY(), 0.0f).GetLength();
            if (length > 0.0005f && length < 3.0f)
            {
                bool crossed = false;
                for (float h : { 0.6f, 1.3f })
                {
                    AzPhysics::RayCastRequest ray;
                    ray.m_start = m_safeAt + AZ::Vector3(0.0f, 0.0f, h);
                    ray.m_direction = AZ::Vector3(step.GetX(), step.GetY(), 0.0f) / length;
                    ray.m_distance = length + 0.05f;
                    ray.m_filterCallback = wallsOnly;
                    AzPhysics::SceneQueryHits hits = scenes->QueryScene(scene, &ray);
                    if (hits && hits.m_hits.front().m_distance < length + 0.05f)
                    {
                        crossed = true;
                    }
                }
                if (crossed)
                {
                    Physics::CharacterRequestBus::Event(GetEntityId(), &Physics::CharacterRequests::SetBasePosition, m_safeAt);
                    base = m_safeAt;
                    if (++m_wallPushes % 30 == 1)
                    {
                        AZ_Printf("Noclip", "walls: stopped the player going through a wall at %.1f %.1f", base.GetX(), base.GetY());
                    }
                }
            }
        }
        m_safeAt = base;
        m_safeKnown = true;
        constexpr float radius = 0.29f, height = 1.25f, lift = 0.38f; // capsule from 0.38 m to 1.63 m above the feet
        AZ::Vector3 flat(move.GetX(), move.GetY(), 0.0f);
        const float speed = flat.GetLength();
        const AZ::Vector3 dir = speed > 0.001f ? flat / speed : AZ::Quaternion::CreateRotationZ(m_yaw).TransformVector(AZ::Vector3::CreateAxisY());
        AzPhysics::ShapeCastRequest cast = AzPhysics::ShapeCastRequestHelpers::CreateCapsuleCastRequest(radius, height,
            AZ::Transform::CreateTranslation(base + AZ::Vector3(0.0f, 0.0f, lift + height * 0.5f)), dir,
            speed * AZ::GetMax(deltaTime, 1.0f / 30.0f) + 0.04f);
        cast.m_reportMultipleHits = true;
        cast.m_filterCallback = [wallsOnly](const AzPhysics::SimulatedBody* body, const Physics::Shape* shape)
        {
            return wallsOnly(body, shape) == AzPhysics::SceneQuery::QueryHitType::None ? AzPhysics::SceneQuery::QueryHitType::None
                                                                                       : AzPhysics::SceneQuery::QueryHitType::Touch;
        };
        AzPhysics::SceneQueryHits hits = scenes->QueryScene(scene, &cast);
        AZ::Vector3 push = AZ::Vector3::CreateZero();
        for (const AzPhysics::SceneQueryHit& hit : hits.m_hits)
        {
            AZ::Vector3 n(hit.m_normal.GetX(), hit.m_normal.GetY(), 0.0f);
            if (n.GetLengthSq() < 0.01f)
            {
                continue; // a floor or ceiling face: the controller handles those
            }
            n.Normalize();
            if (hit.m_distance <= 0.0001f)
            {
                // Already inside it: out along the wall's normal by how deep we are.
                push += n * (AZ::GetMax(0.0f, -hit.m_distance) + 0.02f);
            }
            const float into = flat.Dot(n);
            if (into < 0.0f)
            {
                flat -= n * into;
            }
        }
        if (!push.IsZero())
        {
            if (push.GetLength() > 0.6f)
            {
                push = push.GetNormalized() * 0.6f;
            }
            Physics::CharacterRequestBus::Event(GetEntityId(), &Physics::CharacterRequests::SetBasePosition, base + push);
            m_safeAt = base + push;
            if (++m_wallPushes % 30 == 1)
            {
                AZ_Printf("Noclip", "walls: pushed the player out of a wall by %.2f m at %.1f %.1f", push.GetLength(), base.GetX(), base.GetY());
            }
        }
        move.SetX(flat.GetX());
        move.SetY(flat.GetY());
    }
} // namespace NoclipProtocol
