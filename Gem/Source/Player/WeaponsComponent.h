// "Weapons": the guns, the knife and the shield in your hands (approved PlayerWeapons mockup). Lives on the player's
// runtime UI entity (FirstPersonControllerComponent makes it). It
//   - takes [LMB] fire / pump / slash / bash, [RMB] stab / raise the shield, [R] reload, [V] melee, [1-3] quick switch,
//     hold [C] for the weapon wheel (the world slows while it's open), [G] throws a road flare, [H] uses a medkit;
//   - fires hitscan shots through the physics scene (the shotgun's pellets spread), leaves sparks and bullet holes,
//     tells the monsters (a shot is heard far away) and anything that wants to take damage (/NoclipProtocol/Weapons/Hit);
//   - shows what you hold (Prefabs/Weapons/View_*, from Tools/Weapons/make_weapons.py) with recoil, bob, reload and swing;
//   - plays its sounds (Prefabs/Weapons/WeaponSounds) and lights flares (the base's flare light) where they land;
//   - publishes /NoclipProtocol/Hud/weapon.* for the HUD's weapon block. The state itself is Player/Weapons.h.
// Only in the Backrooms: the Foundation's own facility (the intro) sends you in unarmed.
#pragma once

#include "Weapons.h"
#include <Atom/RPI.Public/AuxGeom/AuxGeomDraw.h>
#include <Atom/RPI.Public/AuxGeom/AuxGeomFeatureProcessorInterface.h>
#include <Atom/RPI.Public/View.h>
#include <Atom/RPI.Public/ViewportContext.h>
#include <Atom/RPI.Public/ViewportContextBus.h>
#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Asset/AssetManagerBus.h>
#include <AzCore/Component/Component.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Console/IConsole.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/Math/Random.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/std/chrono/chrono.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Input/Devices/Keyboard/InputDeviceKeyboard.h>
#include <AzFramework/Input/Devices/Mouse/InputDeviceMouse.h>
#include <AzFramework/Input/Events/InputChannelEventListener.h>
#include <AzFramework/Physics/Common/PhysicsSceneQueries.h>
#include <AzFramework/Physics/Common/PhysicsSimulatedBody.h>
#include <AzFramework/Physics/PhysicsScene.h>
#include <AzFramework/Physics/PhysicsSystem.h>
#include <AzFramework/Spawnable/Spawnable.h>
#include <AzFramework/Spawnable/SpawnableEntitiesInterface.h>
#include <MiniAudio/MiniAudioPlaybackBus.h>
#include <cmath>

namespace NoclipProtocol
{
    class WeaponsComponent
        : public AZ::Component
        , public AZ::TickBus::Handler
        , public AzFramework::InputChannelEventListener
    {
    public:
        AZ_COMPONENT(WeaponsComponent, "{5C1E7A42-9B3D-4E8F-A6C2-71D0E4B9F3A5}");

        static void Reflect(AZ::ReflectContext* context)
        {
            if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
            {
                serialize->Class<WeaponsComponent, AZ::Component>()->Version(1);
            }
        }

        AZ::EntityId m_player; // the player (its own capsule is never hit)

        void Activate() override
        {
            m_slots = AZStd::make_shared<Slots>();
            m_spawned = false;
            m_random.SetSeed(static_cast<AZ::u64>(AZStd::chrono::steady_clock::now().time_since_epoch().count()));
            Weapons::State& w = Weapons::Get();
            w.m_reloadLeft = w.m_pumpLeft = w.m_meleeLeft = w.m_drawLeft = 0.0f;
            w.m_trigger = w.m_aim = w.m_wheelOpen = w.m_shieldUp = false;
            w.m_impacts.clear();
            for (int i = 0; i < Weapons::Count; ++i)
            {
                if (Weapons::State::UsesAmmo(i) && Weapons::State::Owned(i) && !HasGear(Weapons::State::MagKey(i)))
                {
                    Weapons::State::SetMag(i, Weapons::Info(i).m_cap);
                }
            }
            const int saved = Base::Get().GearCount("equipped") - 1;
            w.m_equipped = saved >= 0 && saved < Weapons::Count && Weapons::State::Owned(saved) ? saved : -1;
            InputChannelEventListener::Connect();
            AZ::TickBus::Handler::BusConnect();
        }

        void Deactivate() override
        {
            AZ::TickBus::Handler::BusDisconnect();
            InputChannelEventListener::Disconnect();
            SetTimeScale(1.0f);
            m_flares.clear();
            for (auto& ticket : m_viewTickets)
            {
                ticket.reset();
            }
            m_soundTicket.reset();
            m_flareAsset = {};
            Weapons::Get().m_wheelOpen = false;
            Publish(true);
        }

        int GetTickOrder() override { return AZ::TICK_PRE_RENDER + 100; }

    private:
        struct Slots
        {
            AZ::EntityId m_views[Weapons::Count];
            AZStd::vector<AZStd::pair<AZStd::string, AZ::EntityId>> m_sounds;
        };
        struct Flare
        {
            AZStd::unique_ptr<AzFramework::EntitySpawnTicket> m_ticket;
            AZ::Vector3 m_at;
            float m_until;
        };

        static bool HasGear(const AZStd::string& name)
        {
            for (const auto& g : Base::Get().m_gear)
            {
                if (g.first == name)
                {
                    return true;
                }
            }
            return false;
        }

        // ------------------------------------------------------------ where we are

        static bool InBackrooms()
        {
            auto* registry = AZ::SettingsRegistry::Get();
            if (!registry)
            {
                return false;
            }
            bool menu = false;
            registry->Get(menu, "/NoclipProtocol/Menu/Active");
            AZStd::string level;
            registry->Get(level, "/NoclipProtocol/Save/Level");
            return !menu && (level == "Level0" || level == "Level1" || level == "Level2");
        }

        static bool HandsFree()
        {
            auto* registry = AZ::SettingsRegistry::Get();
            bool paused = false, page = false;
            if (registry)
            {
                registry->Get(paused, "/NoclipProtocol/Paused");
                registry->Get(page, "/NoclipProtocol/Hud/pageOpen");
            }
            return !paused && !page;
        }

        // ------------------------------------------------------------ input

        bool OnInputChannelEventFiltered(const AzFramework::InputChannel& input) override
        {
            using Key = AzFramework::InputDeviceKeyboard::Key;
            using Mouse = AzFramework::InputDeviceMouse;
            const AzFramework::InputChannelId& id = input.GetInputChannelId();
            Weapons::State& w = Weapons::Get();
            if (!m_armed)
            {
                return false;
            }
            if (id == Mouse::Movement::X || id == Mouse::Movement::Y)
            {
                if (w.m_wheelOpen)
                {
                    (id == Mouse::Movement::X ? w.m_wheelX : w.m_wheelY) += input.GetValue();
                }
                return false;
            }
            const bool began = input.IsStateBegan();
            const bool ended = input.IsStateEnded();
            if (id == Mouse::Button::Left) { if (began) { m_pressed = true; } w.m_trigger = input.IsActive(); return false; }
            if (id == Mouse::Button::Right) { if (began) { m_aimPressed = true; } w.m_aim = input.IsActive(); return false; }
            if (id == Key::AlphanumericC)
            {
                if (began) { OpenWheel(); }
                if (ended) { CloseWheel(true); }
                return false;
            }
            if (!began || !HandsFree())
            {
                return false;
            }
            if (id == Key::AlphanumericR) { Reload(); }
            else if (id == Key::AlphanumericV) { QuickMelee(); }
            else if (id == Key::AlphanumericG) { ThrowFlare(); }
            else if (id == Key::AlphanumericH) { UseMedkit(); }
            else if (id == Key::Alphanumeric1) { QuickSwitch(1); }
            else if (id == Key::Alphanumeric2) { QuickSwitch(2); }
            else if (id == Key::Alphanumeric3) { QuickSwitch(3); }
            return false;
        }

    public:
        // The bridge's / console's way in (testing): the same as the keys.
        void Command(const AZStd::string& what, int value)
        {
            if (what == "fire") { m_pressed = true; m_fireOnce = true; }
            else if (what == "aim") { m_aimPressed = true; Weapons::Get().m_aim = value != 0; }
            else if (what == "reload") { Reload(); }
            else if (what == "melee") { QuickMelee(); }
            else if (what == "flare") { ThrowFlare(); }
            else if (what == "medkit") { UseMedkit(); }
            else if (what == "switch") { QuickSwitch(value); }
            else if (what == "equip") { Equip(value); }
            else if (what == "ray") { DebugRays(); }
            else if (what == "wheel")
            {
                if (value >= 0)
                {
                    OpenWheel();
                    Weapons::Get().m_wheelPick = Weapons::WheelOrder[value % Weapons::Count];
                    m_wheelHeldByCommand = true;
                }
                else
                {
                    CloseWheel(true);
                }
            }
        }

    private:
        // ------------------------------------------------------------ actions

        void Notice(const char* text, float seconds = 2.0f)
        {
            Weapons::State& w = Weapons::Get();
            w.m_notice = text;
            w.m_noticeUntil = w.m_clock + seconds;
        }

        void Equip(int id)
        {
            Weapons::State& w = Weapons::Get();
            if (id < 0 || id >= Weapons::Count)
            {
                if (w.m_equipped >= 0)
                {
                    w.m_equipped = -1;
                    SaveEquipped();
                }
                return;
            }
            if (Weapons::State::Locked(id))
            {
                Notice("NEEDS WEAPONS I RESEARCH");
                return;
            }
            if (!Weapons::State::Owned(id))
            {
                Notice("NOT ISSUED \xc2\xb7 REQUEST IT AT THE FIELD RADIO");
                return;
            }
            if (w.m_equipped == id)
            {
                return;
            }
            w.m_equipped = id;
            if (id == Weapons::Shotgun || id == Weapons::Carbine)
            {
                w.m_lastLong = id;
            }
            w.m_reloadLeft = w.m_pumpLeft = w.m_meleeLeft = 0.0f;
            w.m_pumpNeeded = false;
            w.m_shieldUp = false;
            w.m_drawLeft = 0.35f;
            SaveEquipped();
        }

        void SaveEquipped()
        {
            Base::State& base = Base::Get();
            base.AddGear("equipped", (Weapons::Get().m_equipped + 1) - base.GearCount("equipped"));
            m_dirty = true;
        }

        void QuickSwitch(int slot)
        {
            Weapons::State& w = Weapons::Get();
            auto owned = [](int id) { return Weapons::State::Owned(id) && !Weapons::State::Locked(id); };
            int pick = -1;
            if (slot == 1)
            {
                // The sidearm: the pistol, else the flare gun (again: the other one).
                pick = owned(Weapons::Pistol) && w.m_equipped != Weapons::Pistol ? Weapons::Pistol
                     : (owned(Weapons::FlareGun) ? Weapons::FlareGun : (owned(Weapons::Pistol) ? Weapons::Pistol : -1));
            }
            else if (slot == 2)
            {
                // The long gun: the last one you had, again: the other one.
                const int other = w.m_lastLong == Weapons::Carbine ? Weapons::Shotgun : Weapons::Carbine;
                if (w.m_equipped == w.m_lastLong && owned(other)) { pick = other; }
                else if (w.m_lastLong >= 0 && owned(w.m_lastLong)) { pick = w.m_lastLong; }
                else { pick = owned(Weapons::Carbine) ? Weapons::Carbine : (owned(Weapons::Shotgun) ? Weapons::Shotgun : -1); }
            }
            else if (slot == 3)
            {
                pick = owned(Weapons::Knife) && w.m_equipped != Weapons::Knife ? Weapons::Knife
                     : (owned(Weapons::Shield) ? Weapons::Shield : (owned(Weapons::Knife) ? Weapons::Knife : -1));
            }
            if (pick < 0)
            {
                Notice(slot == 3 ? "NO KNIFE OR SHIELD ISSUED" : (slot == 2 ? "NO LONG GUN ISSUED" : "NO SIDEARM ISSUED"));
                return;
            }
            Equip(pick);
        }

        void Reload()
        {
            Weapons::State& w = Weapons::Get();
            const int id = w.m_equipped;
            if (id < 0 || !Weapons::State::UsesAmmo(id) || w.Busy())
            {
                return;
            }
            const Weapons::Def& d = Weapons::Info(id);
            if (Weapons::State::Mag(id) >= d.m_cap)
            {
                return;
            }
            if (Weapons::State::Reserve(id) <= 0)
            {
                Notice("[R] HAS NOTHING TO LOAD");
                Play("dry_click");
                return;
            }
            w.m_reloadTotal = w.m_reloadLeft = d.m_reload;
            Play(d.m_perShell ? "shell_in" : "reload_mag");
        }

        void FinishReload()
        {
            Weapons::State& w = Weapons::Get();
            const int id = w.m_equipped;
            if (id < 0)
            {
                return;
            }
            const Weapons::Def& d = Weapons::Info(id);
            const int mag = Weapons::State::Mag(id);
            const int reserve = Weapons::State::Reserve(id);
            const int take = AZStd::min(d.m_perShell ? 1 : d.m_cap - mag, reserve);
            Weapons::State::SetMag(id, mag + take);
            Weapons::State::TakeReserve(id, take);
            m_dirty = true;
            // The shotgun loads shell after shell until it's full (or you fire).
            if (d.m_perShell && mag + take < d.m_cap && reserve - take > 0)
            {
                w.m_reloadTotal = w.m_reloadLeft = d.m_reload;
                Play("shell_in");
            }
            else if (d.m_pump && w.m_pumpNeeded)
            {
                w.m_pumpNeeded = false; // loading the tube racks one in
            }
        }

        void QuickMelee()
        {
            Weapons::State& w = Weapons::Get();
            if (w.m_meleeLeft > 0.0f || w.m_drawLeft > 0.0f)
            {
                return;
            }
            // With the knife if you have one (a quick slash), otherwise the weapon in your hands.
            const bool knife = Weapons::State::Owned(Weapons::Knife);
            w.m_reloadLeft = 0.0f;
            w.m_meleeLeft = 0.45f;
            w.m_stab = false;
            Strike(knife ? Weapons::Info(Weapons::Knife).m_damage : 15.0f, 1.6f);
            Play("knife_swish");
        }

        void ThrowFlare()
        {
            Base::State& base = Base::Get();
            if (base.GearCount("Flares") <= 0)
            {
                Notice("NO ROAD FLARES");
                return;
            }
            AZ::Vector3 eye, ahead;
            if (!Eye(eye, ahead))
            {
                return;
            }
            // Lobbed about 6 m ahead; it lands where it hits, else on the floor under where it came down.
            AZ::Vector3 flat(ahead.GetX(), ahead.GetY(), 0.0f);
            flat = flat.GetLengthSq() > 0.0001f ? flat.GetNormalized() : AZ::Vector3::CreateAxisY();
            AZ::Vector3 land = eye + flat * 6.0f;
            AzPhysics::SceneQueryHit hit = Ray(eye, flat, 6.0f);
            if (hit)
            {
                land = hit.m_position - flat * 0.3f;
            }
            AzPhysics::SceneQueryHit floor = Ray(AZ::Vector3(land.GetX(), land.GetY(), eye.GetZ()), -AZ::Vector3::CreateAxisZ(), 4.0f);
            land.SetZ(floor ? floor.m_position.GetZ() + 0.05f : eye.GetZ() - 1.6f);
            base.AddGear("Flares", -1);
            m_dirty = true;
            LightFlare(land, Weapons::RoadFlareSeconds);
            Play("flare_strike");
        }

        void UseMedkit()
        {
            Base::State& base = Base::Get();
            const bool medkit = base.GearCount("Medkit") > 0;
            const bool bandage = !medkit && base.GearCount("Bandages") > 0;
            if (!medkit && !bandage)
            {
                Notice("NO MEDKIT");
                return;
            }
            double health = 1.0;
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Get(health, "/NoclipProtocol/Hud/player.health");
                if (health >= 0.999)
                {
                    Notice("HEALTH IS FULL");
                    return;
                }
                double damage = 0.0;
                registry->Get(damage, "/NoclipProtocol/Player/Damage");
                registry->Set("/NoclipProtocol/Player/Damage", damage - (medkit ? 0.5 : 0.2)); // negative damage heals
            }
            base.AddGear(medkit ? "Medkit" : "Bandages", -1);
            m_dirty = true;
            Notice(medkit ? "MEDKIT \xc2\xb7 +50 HEALTH" : "BANDAGE \xc2\xb7 +20 HEALTH");
        }

        void OpenWheel()
        {
            Weapons::State& w = Weapons::Get();
            if (!HandsFree() || w.m_wheelOpen)
            {
                return;
            }
            w.m_wheelOpen = true;
            w.m_wheelX = w.m_wheelY = 0.0f;
            w.m_wheelPick = w.m_equipped;
            SetLookLocked(true);
            SetTimeScale(0.25f);
        }

        void CloseWheel(bool equip)
        {
            Weapons::State& w = Weapons::Get();
            if (!w.m_wheelOpen)
            {
                return;
            }
            w.m_wheelOpen = false;
            m_wheelHeldByCommand = false;
            SetLookLocked(false);
            SetTimeScale(1.0f);
            if (equip && w.m_wheelPick >= 0 && w.m_wheelPick != w.m_equipped)
            {
                Equip(w.m_wheelPick);
            }
        }

        static void SetLookLocked(bool locked)
        {
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Set("/NoclipProtocol/Player/LookLocked", locked);
            }
        }

        void SetTimeScale(float scale)
        {
            if (std::fabs(scale - m_timeScale) < 0.001f)
            {
                return;
            }
            m_timeScale = scale;
            if (auto* console = AZ::Interface<AZ::IConsole>::Get())
            {
                console->PerformCommand(AZStd::string::format("t_simulationTickScale %.2f", scale).c_str());
            }
        }

        // ------------------------------------------------------------ shooting

        bool Eye(AZ::Vector3& eye, AZ::Vector3& ahead) const
        {
            auto* viewports = AZ::Interface<AZ::RPI::ViewportContextRequestsInterface>::Get();
            AZ::RPI::ViewportContextPtr viewport = viewports ? viewports->GetDefaultViewportContext() : nullptr;
            AZ::RPI::ViewPtr view = viewport ? viewport->GetDefaultView() : nullptr;
            if (!view)
            {
                return false;
            }
            const AZ::Transform camera = view->GetCameraTransform();
            eye = camera.GetTranslation();
            ahead = camera.GetBasisY().GetNormalized();
            return true;
        }

        AzPhysics::SceneQueryHit Ray(const AZ::Vector3& from, const AZ::Vector3& direction, float distance) const
        {
            auto* scenes = AZ::Interface<AzPhysics::SceneInterface>::Get();
            const AzPhysics::SceneHandle scene = scenes ? scenes->GetSceneHandle(AzPhysics::DefaultPhysicsSceneName) : AzPhysics::InvalidSceneHandle;
            AzPhysics::SceneQueryHit none;
            if (scene == AzPhysics::InvalidSceneHandle)
            {
                AZ_Warning("NoclipProtocol", false, "Weapons: no physics scene to shoot through");
                return none;
            }
            AzPhysics::RayCastRequest request;
            request.m_start = from;
            request.m_direction = direction.GetNormalized();
            request.m_distance = distance;
            const AZ::EntityId player = m_player;
            request.m_filterCallback = [player](const AzPhysics::SimulatedBody* body, [[maybe_unused]] const Physics::Shape* shape)
            {
                return body && body->GetEntityId() == player ? AzPhysics::SceneQuery::QueryHitType::None : AzPhysics::SceneQuery::QueryHitType::Block;
            };
            AzPhysics::SceneQueryHits hits = scenes->QueryScene(scene, &request);
            if (m_logRays > 0)
            {
                --m_logRays;
                AZ_Printf("NoclipProtocol", "Weapons: ray from %.2f %.2f %.2f dir %.2f %.2f %.2f -> %d hits%s", from.GetX(), from.GetY(), from.GetZ(),
                    direction.GetX(), direction.GetY(), direction.GetZ(), static_cast<int>(hits.m_hits.size()),
                    hits ? AZStd::string::format(" (first at %.2f m, entity %s)", hits.m_hits.front().m_distance,
                               hits.m_hits.front().m_entityId.ToString().c_str()).c_str() : "");
            }
            return hits ? hits.m_hits.front() : none;
        }

        void Hit(const AzPhysics::SceneQueryHit& hit, float damage)
        {
            Weapons::State& w = Weapons::Get();
            const bool soft = hit.m_entityId.IsValid() && hit.m_entityId != AZ::EntityId() && IsMoving(hit.m_entityId);
            w.m_impacts.push_back({ hit.m_position, hit.m_normal, w.m_clock, soft });
            if (w.m_impacts.size() > 48)
            {
                w.m_impacts.erase(w.m_impacts.begin());
            }
            // Anything that takes damage (monsters, later breakables) reads the last hit and its serial.
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                AZ::s64 serial = 0;
                registry->Get(serial, "/NoclipProtocol/Weapons/Hit/Serial");
                registry->Set("/NoclipProtocol/Weapons/Hit/Entity", static_cast<AZ::u64>(hit.m_entityId));
                registry->Set("/NoclipProtocol/Weapons/Hit/Damage", static_cast<double>(damage));
                registry->Set("/NoclipProtocol/Weapons/Hit/X", static_cast<double>(hit.m_position.GetX()));
                registry->Set("/NoclipProtocol/Weapons/Hit/Y", static_cast<double>(hit.m_position.GetY()));
                registry->Set("/NoclipProtocol/Weapons/Hit/Z", static_cast<double>(hit.m_position.GetZ()));
                registry->Set("/NoclipProtocol/Weapons/Hit/Serial", serial + 1);
            }
        }

        // weapon_do ray: what the physics scene answers, several ways (checking why a shot hits nothing).
        void DebugRays()
        {
            AZ::Vector3 eye, ahead;
            if (!Eye(eye, ahead))
            {
                AZ_Printf("NoclipProtocol", "Weapons ray: no camera");
                return;
            }
            auto* scenes = AZ::Interface<AzPhysics::SceneInterface>::Get();
            const AzPhysics::SceneHandle scene = scenes ? scenes->GetSceneHandle(AzPhysics::DefaultPhysicsSceneName) : AzPhysics::InvalidSceneHandle;
            AZ_Printf("NoclipProtocol", "Weapons ray: scene interface %s, default scene %s", scenes ? "yes" : "no",
                scene == AzPhysics::InvalidSceneHandle ? "invalid" : "valid");
            if (scene == AzPhysics::InvalidSceneHandle)
            {
                return;
            }
            auto cast = [&](const char* label, const AZ::Vector3& dir, bool filter, bool many, AzPhysics::SceneQuery::QueryType type)
            {
                AzPhysics::RayCastRequest request;
                request.m_start = eye;
                request.m_direction = dir.GetNormalized();
                request.m_distance = 100.0f;
                request.m_reportMultipleHits = many;
                request.m_queryType = type;
                if (filter)
                {
                    const AZ::EntityId player = m_player;
                    request.m_filterCallback = [player](const AzPhysics::SimulatedBody* body, const Physics::Shape*)
                    {
                        return body && body->GetEntityId() == player ? AzPhysics::SceneQuery::QueryHitType::None : AzPhysics::SceneQuery::QueryHitType::Block;
                    };
                }
                AzPhysics::SceneQueryHits hits = scenes->QueryScene(scene, &request);
                AZStd::string list;
                for (const auto& h : hits.m_hits)
                {
                    list += AZStd::string::format(" [%.2f m %s]", h.m_distance, h.m_entityId.ToString().c_str());
                }
                AZ_Printf("NoclipProtocol", "Weapons ray %s: %d hits%s", label, static_cast<int>(hits.m_hits.size()), list.c_str());
            };
            using QT = AzPhysics::SceneQuery::QueryType;
            cast("ahead, filtered", ahead, true, false, QT::StaticAndDynamic);
            cast("ahead, plain", ahead, false, false, QT::StaticAndDynamic);
            cast("ahead, all hits", ahead, false, true, QT::StaticAndDynamic);
            cast("ahead, static", ahead, false, true, QT::Static);
            cast("down", -AZ::Vector3::CreateAxisZ(), false, true, QT::StaticAndDynamic);
            cast("flat east", AZ::Vector3::CreateAxisX(), false, true, QT::StaticAndDynamic);
            cast("flat north", AZ::Vector3::CreateAxisY(), false, true, QT::StaticAndDynamic);
            AZ_Printf("NoclipProtocol", "Weapons ray: player %s, eye %.2f %.2f %.2f", m_player.ToString().c_str(), eye.GetX(), eye.GetY(), eye.GetZ());
        }

        static bool IsMoving(AZ::EntityId) { return false; } // (monsters will say so; walls and props get sparks)

        void Fire()
        {
            Weapons::State& w = Weapons::Get();
            const int id = w.m_equipped;
            const Weapons::Def& d = Weapons::Info(id);
            AZ::Vector3 eye, ahead;
            if (!Eye(eye, ahead))
            {
                return;
            }
            Weapons::State::SetMag(id, Weapons::State::Mag(id) - 1);
            m_dirty = true;
            w.m_cooldown = d.m_interval;
            w.m_lastShot = w.m_clock;
            w.m_kick = id == Weapons::Shotgun ? 1.0f : (id == Weapons::Carbine ? 0.35f : 0.6f);
            if (d.m_pump)
            {
                w.m_pumpNeeded = true;
            }
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                AZ::s64 shots = 0;
                registry->Get(shots, "/NoclipProtocol/Player/Shots");
                registry->Set("/NoclipProtocol/Player/Shots", shots + 1);
                double recoil = 0.0;
                registry->Get(recoil, "/NoclipProtocol/Player/Recoil");
                const float kick = AZ::DegToRad(id == Weapons::Shotgun ? 3.2f : (id == Weapons::Carbine ? 0.7f : 1.4f));
                registry->Set("/NoclipProtocol/Player/Recoil", recoil + kick);
                m_recoilBack += kick * 0.75f; // most of it settles back down

            }
            if (id == Weapons::FlareGun)
            {
                // The flare flies until it hits something and burns there.
                AzPhysics::SceneQueryHit hit = Ray(eye, ahead, d.m_range);
                const AZ::Vector3 at = hit ? hit.m_position + hit.m_normal * 0.12f : eye + ahead * d.m_range;
                if (hit)
                {
                    Hit(hit, d.m_damage);
                }
                LightFlare(at, Weapons::GunFlareSeconds);
                Play("flare_shot");
                return;
            }
            const AZ::Vector3 right = ahead.Cross(AZ::Vector3::CreateAxisZ()).GetNormalizedSafe();
            const AZ::Vector3 up = right.Cross(ahead).GetNormalizedSafe();
            for (int p = 0; p < d.m_pellets; ++p)
            {
                // A random point in the cone (uniform over the disc).
                const float spread = AZ::DegToRad(d.m_spreadDegrees) * std::sqrt(m_random.GetRandomFloat());
                const float around = AZ::Constants::TwoPi * m_random.GetRandomFloat();
                const AZ::Vector3 dir = (ahead + (right * std::cos(around) + up * std::sin(around)) * std::tan(spread)).GetNormalized();
                AzPhysics::SceneQueryHit hit = Ray(eye, dir, d.m_range);
                if (hit)
                {
                    Hit(hit, d.m_damage);
                }
            }
            Play(id == Weapons::Shotgun ? "shotgun_shot" : (id == Weapons::Carbine ? "carbine_shot" : "pistol_shot"));
        }

        void Strike(float damage, float reach)
        {
            AZ::Vector3 eye, ahead;
            if (!Eye(eye, ahead))
            {
                return;
            }
            AzPhysics::SceneQueryHit hit = Ray(eye, ahead, reach);
            if (hit)
            {
                Hit(hit, damage);
                Play("knife_hit");
            }
        }

        void LightFlare(const AZ::Vector3& at, float seconds)
        {
            if (!m_flareAsset.IsReady())
            {
                return;
            }
            Flare flare;
            flare.m_ticket = AZStd::make_unique<AzFramework::EntitySpawnTicket>(m_flareAsset);
            flare.m_at = at;
            flare.m_until = Weapons::Get().m_clock + seconds;
            AzFramework::SpawnAllEntitiesOptionalArgs args;
            args.m_preInsertionCallback = [at]([[maybe_unused]] AzFramework::EntitySpawnTicket::Id ticketId, AzFramework::SpawnableEntityContainerView view)
            {
                if (view.empty())
                {
                    return;
                }
                if (auto* transform = (*view.begin())->FindComponent<AzFramework::TransformComponent>())
                {
                    AzFramework::TransformComponentConfiguration config;
                    config.m_localTransform = AZ::Transform::CreateTranslation(at);
                    config.m_worldTransform = config.m_localTransform;
                    transform->SetConfiguration(config);
                }
            };
            AzFramework::SpawnableEntitiesInterface::Get()->SpawnAllEntities(*flare.m_ticket, AZStd::move(args));
            m_flares.push_back(AZStd::move(flare));
            if (m_flares.size() > 8)
            {
                m_flares.erase(m_flares.begin()); // the oldest goes out
            }
        }

        // ------------------------------------------------------------ sound

        void Play(const char* name)
        {
            const AZStd::string want = AZStd::string("Sound_") + name;
            for (const auto& [soundName, entity] : m_slots->m_sounds)
            {
                if (soundName == want)
                {
                    MiniAudio::MiniAudioPlaybackRequestBus::Event(entity, &MiniAudio::MiniAudioPlaybackRequests::Stop);
                    MiniAudio::MiniAudioPlaybackRequestBus::Event(entity, &MiniAudio::MiniAudioPlaybackRequests::Play);
                    return;
                }
            }
        }

        // ------------------------------------------------------------ assets

        static AZ::Data::Asset<AzFramework::Spawnable> Load(const char* path)
        {
            AZ::Data::AssetId id;
            AZ::Data::AssetCatalogRequestBus::BroadcastResult(id, &AZ::Data::AssetCatalogRequests::GetAssetIdByPath, path,
                azrtti_typeid<AzFramework::Spawnable>(), false);
            if (!id.IsValid())
            {
                AZ_Warning("NoclipProtocol", false, "Weapons: '%s' not found", path);
                return {};
            }
            return AZ::Data::AssetManager::Instance().GetAsset<AzFramework::Spawnable>(id, AZ::Data::AssetLoadBehavior::PreLoad);
        }

        void SpawnAssets()
        {
            if (!m_loaded)
            {
                m_loaded = true;
                for (int i = 0; i < Weapons::Count; ++i)
                {
                    m_viewAssets[i] = Load(Weapons::Info(i).m_view);
                }
                m_soundAsset = Load("prefabs/weapons/weaponsounds.spawnable");
                m_flareAsset = Load("prefabs/weapons/flare.spawnable");
            }
            bool ready = m_soundAsset.IsReady();
            for (int i = 0; i < Weapons::Count; ++i)
            {
                ready = ready && m_viewAssets[i].IsReady();
            }
            if (!ready)
            {
                return;
            }
            m_spawned = true;
            AZStd::shared_ptr<Slots> slots = m_slots;
            for (int i = 0; i < Weapons::Count; ++i)
            {
                m_viewTickets[i] = AZStd::make_unique<AzFramework::EntitySpawnTicket>(m_viewAssets[i]);
                AzFramework::SpawnAllEntitiesOptionalArgs args;
                args.m_preInsertionCallback = [slots, i]([[maybe_unused]] AzFramework::EntitySpawnTicket::Id ticketId, AzFramework::SpawnableEntityContainerView view)
                {
                    if (view.empty())
                    {
                        return;
                    }
                    AZ::Entity* container = *view.begin();
                    slots->m_views[i] = container->GetId();
                    if (auto* transform = container->FindComponent<AzFramework::TransformComponent>())
                    {
                        AzFramework::TransformComponentConfiguration config;
                        config.m_localTransform = AZ::Transform::CreateTranslation(AZ::Vector3(0.0f, 0.0f, -500.0f)); // out of sight
                        config.m_worldTransform = config.m_localTransform;
                        transform->SetConfiguration(config);
                    }
                };
                AzFramework::SpawnableEntitiesInterface::Get()->SpawnAllEntities(*m_viewTickets[i], AZStd::move(args));
            }
            m_soundTicket = AZStd::make_unique<AzFramework::EntitySpawnTicket>(m_soundAsset);
            AzFramework::SpawnAllEntitiesOptionalArgs args;
            args.m_preInsertionCallback = [slots]([[maybe_unused]] AzFramework::EntitySpawnTicket::Id ticketId, AzFramework::SpawnableEntityContainerView view)
            {
                for (AZ::Entity* entity : view)
                {
                    if (entity->GetName().find("Sound_") == 0)
                    {
                        slots->m_sounds.push_back({ entity->GetName(), entity->GetId() });
                    }
                }
            };
            AzFramework::SpawnableEntitiesInterface::Get()->SpawnAllEntities(*m_soundTicket, AZStd::move(args));
        }

        // ------------------------------------------------------------ every frame

        void OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time) override
        {
            Weapons::State& w = Weapons::Get();
            m_armed = InBackrooms();
            if (!m_armed)
            {
                if (w.m_wheelOpen)
                {
                    CloseWheel(false);
                }
                Publish(true);
                return;
            }
            if (!m_spawned)
            {
                SpawnAssets();
            }
            // While the wheel slows the world the game's clock slows too; the wheel and your hands keep real time.
            const float dt = AZStd::min(deltaTime / AZStd::max(m_timeScale, 0.05f), 0.1f);
            w.m_clock += dt;
            const bool free = HandsFree();
            if (!free && w.m_wheelOpen)
            {
                CloseWheel(false);
            }

            // A weapon arriving with nothing in your hands: it comes out.
            int ownedNow = 0;
            for (int i = 0; i < Weapons::Count; ++i)
            {
                ownedNow += Weapons::State::Owned(i) ? 1 : 0;
            }
            if (m_ownedBefore >= 0 && ownedNow > m_ownedBefore && w.m_equipped < 0)
            {
                for (int i = 0; i < Weapons::Count; ++i)
                {
                    if (Weapons::State::Owned(i) && !Weapons::State::Locked(i))
                    {
                        Equip(i);
                        break;
                    }
                }
            }
            m_ownedBefore = ownedNow;
            if (w.m_equipped >= 0 && !Weapons::State::Owned(w.m_equipped))
            {
                w.m_equipped = -1;
            }

            // The wheel: the mouse's pull picks a slice (clockwise from the top, as drawn).
            if (w.m_wheelOpen && !m_wheelHeldByCommand)
            {
                const float pull = std::sqrt(w.m_wheelX * w.m_wheelX + w.m_wheelY * w.m_wheelY);
                if (pull > 18.0f)
                {
                    const float angle = std::atan2(w.m_wheelY, w.m_wheelX); // screen: y grows downward
                    float fromTop = AZ::RadToDeg(angle) + 90.0f + 30.0f;   // slice 0 spans -120..-60 degrees
                    fromTop = std::fmod(fromTop + 720.0f, 360.0f);
                    w.m_wheelPick = Weapons::WheelOrder[static_cast<int>(fromTop / 60.0f) % Weapons::Count];
                    if (pull > 60.0f)
                    {
                        const float k = 60.0f / pull; // keep the pull short so turning back is quick
                        w.m_wheelX *= k;
                        w.m_wheelY *= k;
                    }
                }
            }

            // Timers.
            w.m_cooldown = AZStd::max(0.0f, w.m_cooldown - dt);
            w.m_drawLeft = AZStd::max(0.0f, w.m_drawLeft - dt);
            w.m_meleeLeft = AZStd::max(0.0f, w.m_meleeLeft - dt);
            w.m_kick = AZStd::max(0.0f, w.m_kick - dt * 6.0f);
            if (w.m_pumpLeft > 0.0f)
            {
                w.m_pumpLeft -= dt;
                if (w.m_pumpLeft <= 0.0f)
                {
                    w.m_pumpLeft = 0.0f;
                    w.m_pumpNeeded = false;
                }
            }
            if (w.m_reloadLeft > 0.0f)
            {
                w.m_reloadLeft -= dt;
                if (w.m_reloadLeft <= 0.0f)
                {
                    w.m_reloadLeft = 0.0f;
                    FinishReload();
                }
            }

            // The trigger.
            const int id = w.m_equipped;
            const bool pressed = m_pressed;
            const bool aimPressed = m_aimPressed;
            m_pressed = m_aimPressed = false;
            const bool held = w.m_trigger || m_fireOnce;
            m_fireOnce = false;
            if (free && !w.m_wheelOpen && id >= 0)
            {
                const Weapons::Def& d = Weapons::Info(id);
                if (id == Weapons::Knife)
                {
                    if ((pressed || aimPressed) && w.m_meleeLeft <= 0.0f && w.m_drawLeft <= 0.0f)
                    {
                        w.m_stab = aimPressed && !pressed;
                        w.m_meleeLeft = w.m_stab ? 0.7f : 0.45f;
                        Strike(w.m_stab ? 60.0f : d.m_damage, w.m_stab ? 1.9f : d.m_range);
                        Play("knife_swish");
                    }
                }
                else if (id == Weapons::Shield)
                {
                    w.m_shieldUp = w.m_aim;
                    if (pressed && w.m_meleeLeft <= 0.0f && w.m_drawLeft <= 0.0f)
                    {
                        w.m_meleeLeft = d.m_interval;
                        Strike(d.m_damage, d.m_range);
                        Play("knife_swish");
                    }
                }
                else
                {
                    // A shot interrupts the shotgun's shell-by-shell loading.
                    if (pressed && d.m_perShell && w.m_reloadLeft > 0.0f && Weapons::State::Mag(id) > 0)
                    {
                        w.m_reloadLeft = 0.0f;
                    }
                    const bool wants = d.m_auto ? held : pressed;
                    if (wants && w.m_cooldown <= 0.0f && !w.Busy())
                    {
                        if (d.m_pump && w.m_pumpNeeded)
                        {
                            w.m_pumpLeft = 0.45f;
                            Play("pump");
                        }
                        else if (Weapons::State::Mag(id) > 0)
                        {
                            Fire();
                        }
                        else if (pressed)
                        {
                            Play("dry_click");
                            w.m_cooldown = 0.25f;
                            if (Weapons::State::Reserve(id) > 0)
                            {
                                Reload(); // an empty gun reloads when you pull the trigger
                            }
                            else
                            {
                                Notice("EMPTY \xc2\xb7 NO AMMUNITION LEFT");
                            }
                        }
                    }
                }
            }
            if (id != Weapons::Shield)
            {
                w.m_shieldUp = false;
            }
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Set("/NoclipProtocol/Player/Shield", w.m_shieldUp);
            }

            // Flares burning out, and how much they light you.
            float flareLight = 0.0f;
            AZ::Vector3 eye = AZ::Vector3::CreateZero(), ahead = AZ::Vector3::CreateAxisY();
            Eye(eye, ahead);
            for (size_t i = 0; i < m_flares.size();)
            {
                if (w.m_clock >= m_flares[i].m_until)
                {
                    m_flares.erase(m_flares.begin() + i);
                    continue;
                }
                const float d = m_flares[i].m_at.GetDistance(eye);
                flareLight = AZStd::max(flareLight, AZ::GetClamp(1.0f - (d - 1.0f) / 7.0f, 0.0f, 1.0f));
                ++i;
            }
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Set("/NoclipProtocol/Player/FlareLight", static_cast<double>(flareLight));
            }

            // Ammunition and the weapon in hand are saved with the expedition every few seconds when they change.
            m_saveIn -= dt;
            if (m_dirty && m_saveIn <= 0.0f)
            {
                m_dirty = false;
                m_saveIn = 5.0f;
                Base::Get().Save();
            }

            // The recoil settling: the view comes most of the way back down over a moment.
            if (m_recoilBack > 0.0f)
            {
                const float step = AZStd::min(m_recoilBack, dt * (m_recoilBack * 7.0f + 0.002f));
                m_recoilBack -= step;
                if (auto* registry = AZ::SettingsRegistry::Get())
                {
                    double recoil = 0.0;
                    registry->Get(recoil, "/NoclipProtocol/Player/Recoil");
                    registry->Set("/NoclipProtocol/Player/Recoil", recoil - step);
                }
            }
            PlaceView(dt);
            Draw();
            Publish(false);
        }

        // What you hold: put in front of the camera, with the motion of the moment.
        void PlaceView(float dt)
        {
            Weapons::State& w = Weapons::Get();
            AZ::Vector3 eye, ahead;
            auto* viewports = AZ::Interface<AZ::RPI::ViewportContextRequestsInterface>::Get();
            AZ::RPI::ViewportContextPtr viewport = viewports ? viewports->GetDefaultViewportContext() : nullptr;
            AZ::RPI::ViewPtr view = viewport ? viewport->GetDefaultView() : nullptr;
            if (!view || !Eye(eye, ahead))
            {
                return;
            }
            const AZ::Transform camera = view->GetCameraTransform();
            // Walking bob from how fast the eye moves.
            const float speed = dt > 0.0f ? (eye - m_lastEye).GetLength() / dt : 0.0f;
            m_lastEye = eye;
            m_bobSpeed = AZ::Lerp(m_bobSpeed, AZStd::min(speed, 6.0f), AZStd::min(1.0f, dt * 8.0f));
            m_bobPhase += dt * (4.0f + m_bobSpeed * 1.6f);
            const float bob = AZStd::min(m_bobSpeed / 4.0f, 1.0f);
            for (int i = 0; i < Weapons::Count; ++i)
            {
                const AZ::EntityId entity = m_slots->m_views[i];
                if (!entity.IsValid())
                {
                    continue;
                }
                AZ::Transform tm = AZ::Transform::CreateTranslation(AZ::Vector3(0.0f, 0.0f, -500.0f));
                if (i == w.m_equipped && !w.m_wheelOpen)
                {
                    // Camera space: x right, y ahead, z up.
                    static const AZ::Vector3 hold[Weapons::Count] = {
                        AZ::Vector3(0.17f, 0.52f, -0.19f), AZ::Vector3(0.17f, 0.50f, -0.23f), AZ::Vector3(0.16f, 0.50f, -0.22f),
                        AZ::Vector3(0.17f, 0.50f, -0.19f), AZ::Vector3(0.20f, 0.46f, -0.22f), AZ::Vector3(-0.26f, 0.62f, -0.62f),
                    };
                    AZ::Vector3 at = hold[i];
                    float pitch = 0.0f, yaw = 0.0f, roll = 0.0f;
                    at += AZ::Vector3(std::sin(m_bobPhase) * 0.008f * bob, 0.0f, -std::fabs(std::cos(m_bobPhase)) * 0.010f * bob);
                    at += AZ::Vector3(0.0f, -0.05f * w.m_kick, 0.012f * w.m_kick);
                    pitch += 0.18f * w.m_kick;
                    if (w.m_drawLeft > 0.0f)
                    {
                        const float t = w.m_drawLeft / 0.35f;
                        at += AZ::Vector3(0.0f, 0.0f, -0.25f * t);
                        pitch -= 0.6f * t;
                    }
                    if (w.m_reloadLeft > 0.0f)
                    {
                        const float t = std::sin(w.ReloadProgress() * AZ::Constants::Pi);
                        at += AZ::Vector3(-0.03f * t, 0.0f, -0.07f * t);
                        roll += 0.55f * t;
                        pitch -= 0.25f * t;
                    }
                    if (w.m_pumpLeft > 0.0f)
                    {
                        at += AZ::Vector3(0.0f, -0.04f * std::sin(w.m_pumpLeft / 0.45f * AZ::Constants::Pi), 0.0f);
                    }
                    if (w.m_meleeLeft > 0.0f)
                    {
                        const float total = w.m_stab ? 0.7f : 0.45f;
                        const float t = std::sin((1.0f - w.m_meleeLeft / total) * AZ::Constants::Pi);
                        if (i == Weapons::Knife && !w.m_stab)
                        {
                            at += AZ::Vector3(-0.22f * t, 0.08f * t, 0.05f * t);
                            yaw += 1.0f * t;
                            roll -= 0.6f * t;
                        }
                        else
                        {
                            at += AZ::Vector3(-0.05f * t, 0.16f * t, 0.03f * t);
                        }
                    }
                    if (i == Weapons::Shield && w.m_shieldUp)
                    {
                        at = AZ::Vector3(-0.05f, 0.55f, -0.42f);
                    }
                    const AZ::Quaternion turn = AZ::Quaternion::CreateRotationZ(yaw) * AZ::Quaternion::CreateRotationX(pitch) *
                        AZ::Quaternion::CreateRotationY(roll);
                    tm = camera * AZ::Transform::CreateFromQuaternionAndTranslation(turn, at);
                    m_muzzle = tm.TransformPoint(Muzzle(i));
                }
                AZ::TransformBus::Event(entity, &AZ::TransformBus::Events::SetWorldTM, tm);
            }
        }

        static AZ::Vector3 Muzzle(int id)
        {
            switch (id)
            {
            case Weapons::Pistol: return AZ::Vector3(0.0f, 0.16f, 0.05f);
            case Weapons::Shotgun: return AZ::Vector3(0.0f, 0.50f, 0.04f);
            case Weapons::Carbine: return AZ::Vector3(0.0f, 0.46f, 0.04f);
            case Weapons::FlareGun: return AZ::Vector3(0.0f, 0.17f, 0.04f);
            default: return AZ::Vector3::CreateZero();
            }
        }

        // The muzzle flash, sparks and bullet holes (a hole is a small dark octagon with a lighter, scuffed rim).
        void Draw()
        {
            Weapons::State& w = Weapons::Get();
            auto* viewports = AZ::Interface<AZ::RPI::ViewportContextRequestsInterface>::Get();
            AZ::RPI::ViewportContextPtr viewport = viewports ? viewports->GetDefaultViewportContext() : nullptr;
            if (!viewport || !viewport->GetRenderScene())
            {
                return;
            }
            AZ::RPI::AuxGeomDrawPtr draw = AZ::RPI::AuxGeomFeatureProcessorInterface::GetDrawQueueForScene(viewport->GetRenderScene());
            AZ::Vector3 eye, ahead;
            if (!draw || !Eye(eye, ahead))
            {
                return;
            }
            AZStd::vector<AZ::Vector3> holes, flashes;
            AZStd::vector<AZ::Color> holeColours, flashColours;
            auto quad = [](AZStd::vector<AZ::Vector3>& out, AZStd::vector<AZ::Color>& colours, const AZ::Vector3& centre, const AZ::Vector3& a,
                           const AZ::Vector3& b, const AZ::Color& c)
            {
                out.insert(out.end(), { centre - a - b, centre + a - b, centre + a + b, centre - a - b, centre + a + b, centre - a + b });
                colours.insert(colours.end(), 6, c);
            };
            for (const Weapons::Impact& impact : w.m_impacts)
            {
                const float age = w.m_clock - impact.m_born;
                const AZ::Vector3 n = impact.m_normal.GetNormalizedSafe();
                const AZ::Vector3 t1 = (std::fabs(n.GetZ()) < 0.9f ? n.Cross(AZ::Vector3::CreateAxisZ()) : n.Cross(AZ::Vector3::CreateAxisX())).GetNormalizedSafe();
                const AZ::Vector3 t2 = n.Cross(t1).GetNormalizedSafe();
                if (!impact.m_soft && age < 30.0f)
                {
                    const float fade = AZ::GetClamp((30.0f - age) / 5.0f, 0.0f, 1.0f);
                    const AZ::Vector3 c = impact.m_at + n * 0.004f;
                    for (int q = 0; q < 8; ++q)
                    {
                        const float a0 = AZ::Constants::TwoPi * q / 8.0f, a1 = AZ::Constants::TwoPi * (q + 1) / 8.0f;
                        const AZ::Vector3 d0 = t1 * std::cos(a0) + t2 * std::sin(a0), d1 = t1 * std::cos(a1) + t2 * std::sin(a1);
                        // the scuffed rim, then the hole
                        holes.insert(holes.end(), { c + d0 * 0.009f, c + d0 * 0.016f, c + d1 * 0.016f, c + d0 * 0.009f, c + d1 * 0.016f, c + d1 * 0.009f });
                        holeColours.insert(holeColours.end(), 6, AZ::Color(0.55f, 0.5f, 0.38f, 0.35f * fade));
                        holes.insert(holes.end(), { c, c + d0 * 0.009f, c + d1 * 0.009f });
                        holeColours.insert(holeColours.end(), 3, AZ::Color(0.015f, 0.012f, 0.01f, 0.9f * fade));
                    }
                }
                if (age < 0.12f)
                {
                    const float k = 1.0f - age / 0.12f;
                    quad(flashes, flashColours, impact.m_at + n * 0.02f, t1 * 0.05f * k, t2 * 0.05f * k, AZ::Color(1.0f, 0.85f, 0.45f, 0.9f * k));
                    quad(flashes, flashColours, impact.m_at + n * 0.02f, (t1 + t2) * 0.035f * k, (t1 - t2) * 0.02f * k, AZ::Color(1.0f, 0.7f, 0.3f, 0.8f * k));
                }
            }
            // The muzzle flash, for a moment after the shot (two crossed quads facing you).
            const float since = w.m_clock - w.m_lastShot;
            if (since < 0.05f && w.m_equipped >= 0 && w.m_equipped != Weapons::FlareGun)
            {
                const float size = w.m_equipped == Weapons::Shotgun ? 0.09f : 0.06f;
                const AZ::Vector3 right = ahead.Cross(AZ::Vector3::CreateAxisZ()).GetNormalizedSafe();
                const AZ::Vector3 up = right.Cross(ahead).GetNormalizedSafe();
                quad(flashes, flashColours, m_muzzle + ahead * 0.03f, right * size, up * size * 0.6f, AZ::Color(1.0f, 0.86f, 0.5f, 0.85f));
                quad(flashes, flashColours, m_muzzle + ahead * 0.03f, (right + up) * size * 0.5f, (right - up) * size * 0.3f, AZ::Color(1.0f, 0.95f, 0.75f, 0.9f));
            }
            auto submit = [&](const AZStd::vector<AZ::Vector3>& verts, const AZStd::vector<AZ::Color>& colours, bool depth)
            {
                if (verts.empty())
                {
                    return;
                }
                AZ::RPI::AuxGeomDraw::AuxGeomDynamicDrawArguments args;
                args.m_verts = verts.data();
                args.m_vertCount = static_cast<uint32_t>(verts.size());
                args.m_colors = colours.data();
                args.m_colorCount = static_cast<uint32_t>(colours.size());
                args.m_opacityType = AZ::RPI::AuxGeomDraw::OpacityType::Translucent;
                args.m_depthTest = depth ? AZ::RPI::AuxGeomDraw::DepthTest::On : AZ::RPI::AuxGeomDraw::DepthTest::Off;
                args.m_depthWrite = AZ::RPI::AuxGeomDraw::DepthWrite::Off;
                draw->DrawTriangles(args);
            };
            submit(holes, holeColours, true);
            submit(flashes, flashColours, false);
        }

        // For the HUD's weapon block (PlayerHUD / PlayerWeapons).
        void Publish(bool none)
        {
            auto* registry = AZ::SettingsRegistry::Get();
            if (!registry)
            {
                return;
            }
            Weapons::State& w = Weapons::Get();
            const int id = none ? -1 : w.m_equipped;
            if (id < 0)
            {
                registry->Set("/NoclipProtocol/Hud/weapon.name", AZStd::string());
                return;
            }
            const Weapons::Def& d = Weapons::Info(id);
            const int mag = Weapons::State::Mag(id);
            const int reserve = Weapons::State::Reserve(id);
            const bool gun = Weapons::State::UsesAmmo(id);
            AZStd::string state, note;
            if (w.m_reloadLeft > 0.0f) { state = "RELOADING"; }
            else if (w.m_pumpNeeded || w.m_pumpLeft > 0.0f) { state = "PUMP [LMB]"; }
            else if (id == Weapons::Shield && w.m_shieldUp) { state = "RAISED"; }
            if (w.m_clock < w.m_noticeUntil) { note = w.m_notice; }
            else if (gun && reserve <= 0 && mag <= 0) { note = "EMPTY \xc2\xb7 [R] has nothing to load"; }
            else if (gun && reserve <= 0 && d.m_cap > 1) { note = "LAST MAGAZINE \xc2\xb7 [R] has nothing to load"; }
            else { note = d.m_note; }
            registry->Set("/NoclipProtocol/Hud/weapon.name", AZStd::string(d.m_hud));
            registry->Set("/NoclipProtocol/Hud/weapon.kind", AZStd::string(gun ? "gun" : "melee"));
            registry->Set("/NoclipProtocol/Hud/weapon.mag", static_cast<double>(mag));
            registry->Set("/NoclipProtocol/Hud/weapon.cap", static_cast<double>(d.m_cap));
            registry->Set("/NoclipProtocol/Hud/weapon.reserve", static_cast<double>(reserve));
            registry->Set("/NoclipProtocol/Hud/weapon.mode", AZStd::string(d.m_mode));
            registry->Set("/NoclipProtocol/Hud/weapon.icon", AZStd::string(d.m_icon));
            registry->Set("/NoclipProtocol/Hud/weapon.rounds", AZStd::string(d.m_roundsIcon));
            registry->Set("/NoclipProtocol/Hud/weapon.state", state);
            registry->Set("/NoclipProtocol/Hud/weapon.reload", static_cast<double>(w.m_reloadLeft > 0.0f ? w.ReloadProgress() : -1.0f));
            registry->Set("/NoclipProtocol/Hud/weapon.note", note);
            registry->Set("/NoclipProtocol/Hud/weapon.low", static_cast<double>(gun && reserve <= 0 && d.m_cap > 1 ? 1.0 : 0.0));
        }

        AZStd::shared_ptr<Slots> m_slots;
        AZ::Data::Asset<AzFramework::Spawnable> m_viewAssets[Weapons::Count];
        AZStd::unique_ptr<AzFramework::EntitySpawnTicket> m_viewTickets[Weapons::Count];
        AZ::Data::Asset<AzFramework::Spawnable> m_soundAsset;
        AZStd::unique_ptr<AzFramework::EntitySpawnTicket> m_soundTicket;
        AZ::Data::Asset<AzFramework::Spawnable> m_flareAsset;
        AZStd::vector<Flare> m_flares;
        bool m_loaded = false;
        bool m_spawned = false;
        bool m_armed = false;
        bool m_pressed = false;
        bool m_aimPressed = false;
        bool m_fireOnce = false;
        bool m_wheelHeldByCommand = false;
        bool m_dirty = false;
        float m_saveIn = 0.0f;
        float m_timeScale = 1.0f;
        int m_ownedBefore = -1;
        AZ::Vector3 m_lastEye = AZ::Vector3::CreateZero();
        AZ::Vector3 m_muzzle = AZ::Vector3::CreateZero();
        float m_bobSpeed = 0.0f;
        float m_recoilBack = 0.0f;
        float m_bobPhase = 0.0f;
        AZ::SimpleLcgRandom m_random;
        mutable int m_logRays = 12; // the first shots are written to the log (checking the aim)
    };
} // namespace NoclipProtocol
