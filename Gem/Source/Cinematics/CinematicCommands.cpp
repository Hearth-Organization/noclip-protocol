// Hearth Engine cutscenes (Cinematics window) in the game: the Track View sequence's console keys call
//   hearth_cine play <entity>                     voice line or sound (an entity with a MiniAudio Playback component)
//   hearth_cine light <flicker|off|on|dim> <seconds> <entity|*>   lights; * = every light within 25 m of the camera
// Lights switch the way Flicker Light does it: the light entity is deactivated and activated, nothing is saved.

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Console/IConsole.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>
#include <AzFramework/Components/CameraBus.h>
#include <MiniAudio/MiniAudioPlaybackBus.h>

#include <cstdlib>

namespace NoclipProtocol
{
    namespace
    {
        bool CineIsLight(const AZ::Entity* entity)
        {
            for (const AZ::Component* component : entity->GetComponents())
            {
                const AZStd::string type = component->RTTI_GetTypeName();
                if (type.find("Light") != AZStd::string::npos && type.find("Flicker") == AZStd::string::npos)
                {
                    return true;
                }
            }
            return false;
        }

        AZStd::vector<AZ::Entity*> CineFind(const AZStd::string& target, [[maybe_unused]] bool lightsOnly)
        {
            AZStd::vector<AZ::Entity*> found;
            AZ::Vector3 camera = AZ::Vector3::CreateZero();
            if (target == "*")
            {
                AZ::EntityId active;
                Camera::CameraSystemRequestBus::BroadcastResult(active, &Camera::CameraSystemRequests::GetActiveCamera);
                AZ::TransformBus::EventResult(camera, active, &AZ::TransformBus::Events::GetWorldTranslation);
            }
            AZ::ComponentApplicationBus::Broadcast(&AZ::ComponentApplicationRequests::EnumerateEntities, [&](AZ::Entity* entity)
            {
                if (target == "*")
                {
                    if (!CineIsLight(entity))
                    {
                        return;
                    }
                    AZ::Vector3 at = AZ::Vector3::CreateZero();
                    AZ::TransformBus::EventResult(at, entity->GetId(), &AZ::TransformBus::Events::GetWorldTranslation);
                    if (at.GetDistance(camera) <= 25.0f)
                    {
                        found.push_back(entity);
                    }
                }
                else
                {
                    AZStd::string name = entity->GetName();
                    for (char& c : name)
                    {
                        c = c == ' ' ? '_' : c;
                    }
                    if (name == target) // a named entity: whatever it is (a light, or the parent of one)
                    {
                        found.push_back(entity);
                    }
                }
            });
            return found;
        }

        void CineSetLit(AZ::Entity* entity, bool lit)
        {
            if (lit && entity->GetState() == AZ::Entity::State::Init)
            {
                entity->Activate();
            }
            else if (!lit && entity->GetState() == AZ::Entity::State::Active)
            {
                entity->Deactivate();
            }
        }

        //! Runs the timed light effects.
        class CineLights : public AZ::TickBus::Handler
        {
        public:
            struct Effect
            {
                AZStd::vector<AZ::EntityId> lights;
                AZStd::string kind;
                float left = 0.0f;
                float next = 0.0f;
            };
            AZStd::vector<Effect> m_effects;

            void Add(Effect effect)
            {
                m_effects.push_back(AZStd::move(effect));
                if (!AZ::TickBus::Handler::BusIsConnected())
                {
                    AZ::TickBus::Handler::BusConnect();
                }
            }
            void OnTick(float deltaTime, AZ::ScriptTimePoint) override
            {
                for (auto it = m_effects.begin(); it != m_effects.end();)
                {
                    Effect& effect = *it;
                    effect.left -= deltaTime;
                    effect.next -= deltaTime;
                    const bool done = effect.left <= 0.0f;
                    if (effect.kind == "flicker" && (effect.next <= 0.0f || done))
                    {
                        effect.next = 0.04f + 0.18f * (static_cast<float>(std::rand()) / RAND_MAX);
                        for (const AZ::EntityId& id : effect.lights)
                        {
                            AZ::Entity* entity = nullptr;
                            AZ::ComponentApplicationBus::BroadcastResult(entity, &AZ::ComponentApplicationRequests::FindEntity, id);
                            if (entity)
                            {
                                CineSetLit(entity, done || (std::rand() % 2) == 0);
                            }
                        }
                    }
                    it = done ? m_effects.erase(it) : it + 1;
                }
                if (m_effects.empty())
                {
                    AZ::TickBus::Handler::BusDisconnect();
                }
            }
        };

        CineLights& Lights()
        {
            static CineLights lights;
            return lights;
        }

        void HearthCine(const AZ::ConsoleCommandContainer& arguments)
        {
            if (arguments.size() < 2)
            {
                AZ_Warning("NoclipProtocol", false, "hearth_cine: play <entity> | light <flicker|off|on|dim> <seconds> <entity|*>");
                return;
            }
            const AZStd::string verb(arguments[0]);
            if (verb == "play")
            {
                const AZStd::vector<AZ::Entity*> entities = CineFind(AZStd::string(arguments[1]), false);
                for (AZ::Entity* entity : entities)
                {
                    MiniAudio::MiniAudioPlaybackRequestBus::Event(entity->GetId(), &MiniAudio::MiniAudioPlaybackRequests::Stop);
                    MiniAudio::MiniAudioPlaybackRequestBus::Event(entity->GetId(), &MiniAudio::MiniAudioPlaybackRequests::Play);
                }
                AZ_Warning("NoclipProtocol", !entities.empty(), "hearth_cine: no entity called %.*s", AZ_STRING_ARG(arguments[1]));
                return;
            }
            if (verb == "light" && arguments.size() >= 4)
            {
                const AZStd::string kind(arguments[1]);
                const float seconds = static_cast<float>(std::atof(AZStd::string(arguments[2]).c_str()));
                const AZStd::vector<AZ::Entity*> lights = CineFind(AZStd::string(arguments[3]), true);
                CineLights::Effect effect;
                effect.kind = kind;
                effect.left = seconds;
                int n = 0;
                for (AZ::Entity* light : lights)
                {
                    effect.lights.push_back(light->GetId());
                    if (kind == "off")
                    {
                        CineSetLit(light, false);
                    }
                    else if (kind == "on")
                    {
                        CineSetLit(light, true);
                    }
                    else if (kind == "dim")
                    {
                        CineSetLit(light, (n++ % 2) == 0); // every other light goes out
                    }
                }
                if (kind == "flicker")
                {
                    Lights().Add(AZStd::move(effect));
                }
                return;
            }
            AZ_Warning("NoclipProtocol", false, "hearth_cine: unknown command %s", verb.c_str());
        }
    } // namespace

    AZ_CONSOLEFREEFUNC("hearth_cine", HearthCine, AZ::ConsoleFunctorFlags::Null, "Hearth Engine cutscene keys: play <entity>, light <effect> <seconds> <entity|*>");
} // namespace NoclipProtocol
