#include "FootstepsComponent.h"

#include <AzCore/Component/TransformBus.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/algorithm.h>
#include <cmath>
#include <MiniAudio/MiniAudioPlaybackBus.h>

namespace NoclipProtocol
{
    void FootstepsComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<FootstepsComponent, AZ::Component>()
                ->Version(1)
                ->Field("Follow", &FootstepsComponent::m_follow)
                ->Field("StepSounds", &FootstepsComponent::m_stepSounds)
                ->Field("LandSound", &FootstepsComponent::m_landSound)
                ->Field("WalkStride", &FootstepsComponent::m_walkStride)
                ->Field("SprintStride", &FootstepsComponent::m_sprintStride)
                ->Field("SprintSpeed", &FootstepsComponent::m_sprintSpeed)
                ->Field("WalkVolume", &FootstepsComponent::m_walkVolume)
                ->Field("SprintVolume", &FootstepsComponent::m_sprintVolume)
                ->Field("LandSpeed", &FootstepsComponent::m_landSpeed);

            if (auto edit = serialize->GetEditContext())
            {
                edit->Class<FootstepsComponent>("Footsteps", "Plays a step sound every stride while the player walks on the ground")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Noclip Protocol")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FootstepsComponent::m_follow, "Follow",
                        "The player whose steps these are (empty: this entity)")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FootstepsComponent::m_stepSounds, "Step sounds",
                        "Entities with a MiniAudio Playback component each, played in turn")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FootstepsComponent::m_landSound, "Landing sound",
                        "Entity with a MiniAudio Playback component, played when landing from a jump or fall")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FootstepsComponent::m_walkStride, "Walk stride (m)", "")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FootstepsComponent::m_sprintStride, "Sprint stride (m)", "")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FootstepsComponent::m_sprintSpeed, "Sprint speed (m/s)",
                        "Moving faster than this counts as sprinting")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FootstepsComponent::m_walkVolume, "Walk volume (%)", "")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FootstepsComponent::m_sprintVolume, "Sprint volume (%)", "")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FootstepsComponent::m_landSpeed, "Landing speed (m/s)",
                        "Falling at least this fast makes a landing sound");
            }
        }
    }

    void FootstepsComponent::Activate()
    {
        m_random.SetSeed(static_cast<AZ::u64>(GetEntityId()) ^ 0xF007ull);
        m_started = false;
        m_distance = 0.0f;
        m_airborne = false;
        m_next = 0;
        AZ::TickBus::Handler::BusConnect();
    }

    void FootstepsComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
    }

    AZ::Vector3 FootstepsComponent::FollowPosition() const
    {
        AZ::Vector3 position = AZ::Vector3::CreateZero();
        const AZ::EntityId who = m_follow.IsValid() ? m_follow : GetEntityId();
        AZ::TransformBus::EventResult(position, who, &AZ::TransformBus::Events::GetWorldTranslation);
        return position;
    }

    void FootstepsComponent::PlaySound(AZ::EntityId entity, float volumePercent)
    {
        if (!entity.IsValid())
        {
            return;
        }
        MiniAudio::MiniAudioPlaybackRequestBus::Event(entity, &MiniAudio::MiniAudioPlaybackRequests::SetVolumePercentage, volumePercent);
        MiniAudio::MiniAudioPlaybackRequestBus::Event(entity, &MiniAudio::MiniAudioPlaybackRequests::Stop);
        MiniAudio::MiniAudioPlaybackRequestBus::Event(entity, &MiniAudio::MiniAudioPlaybackRequests::Play);
    }

    void FootstepsComponent::PlayStep(float volumePercent)
    {
        if (m_stepSounds.empty())
        {
            return;
        }
        // Never the same variation twice in a row, and a little volume variation.
        size_t pick = m_next;
        if (m_stepSounds.size() > 2)
        {
            pick = (m_next + 1 + static_cast<size_t>(m_random.GetRandomFloat() * static_cast<float>(m_stepSounds.size() - 1)))
                % m_stepSounds.size();
        }
        m_next = pick;
        PlaySound(m_stepSounds[pick], volumePercent * (0.85f + 0.3f * m_random.GetRandomFloat()));
    }

    void FootstepsComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        if (deltaTime <= 0.0f)
        {
            return;
        }
        const AZ::Vector3 position = FollowPosition();
        if (!m_started)
        {
            m_started = true;
            m_lastPosition = position;
            return;
        }
        const AZ::Vector3 moved = position - m_lastPosition;
        m_lastPosition = position;

        // Teleports (spawn, level change) are not steps.
        if (moved.GetLengthSq() > 4.0f)
        {
            m_distance = 0.0f;
            return;
        }

        const float verticalSpeed = moved.GetZ() / deltaTime;
        const bool airborneNow = std::fabs(verticalSpeed) > 1.0f;
        if (airborneNow)
        {
            m_fallSpeed = AZStd::max(m_fallSpeed, -verticalSpeed);
            m_airborne = true;
            return;
        }
        if (m_airborne)
        {
            // Just landed.
            m_airborne = false;
            if (m_fallSpeed >= m_landSpeed)
            {
                PlaySound(m_landSound.IsValid() ? m_landSound : (m_stepSounds.empty() ? AZ::EntityId() : m_stepSounds[0]),
                          m_sprintVolume);
                m_distance = 0.0f;
            }
            m_fallSpeed = 0.0f;
        }

        const AZ::Vector3 flat(moved.GetX(), moved.GetY(), 0.0f);
        const float step = flat.GetLength();
        const float speed = step / deltaTime;
        if (speed < 0.3f)
        {
            // Standing still: the next step comes after half a stride, so starting to walk sounds right.
            m_distance = AZStd::min(m_distance, m_walkStride * 0.5f);
            return;
        }
        const bool sprinting = speed > m_sprintSpeed;
        const float stride = sprinting ? m_sprintStride : m_walkStride;
        m_distance += step;
        if (m_distance >= stride)
        {
            m_distance -= stride;
            PlayStep(sprinting ? m_sprintVolume : m_walkVolume);
        }
    }
} // namespace NoclipProtocol
