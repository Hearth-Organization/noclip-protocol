#include "FootstepsComponent.h"

#include <AzCore/Component/TransformBus.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/algorithm.h>
#include <cmath>
#include <MiniAudio/MiniAudioPlaybackBus.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <MiniAudio/SoundAsset.h>

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
        m_verticalSmooth = 0.0f;
        m_speedSmooth = 0.0f;
        m_stillTime = 0.0f;
        m_debugTimer = 0.0f;
        m_debugTotal = 0.0f;
        m_stepsPlayed = 0;
        m_selfTestDone = false;
        AZ_TracePrintf("Footsteps", "Footsteps active: %zu step sounds, landing %s, follow %s\n", m_stepSounds.size(),
            m_landSound.IsValid() ? "yes" : "no", m_follow.IsValid() ? m_follow.ToString().c_str() : "self");
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

    void FootstepsComponent::DiagnoseSound(AZ::EntityId entity, const char* label) const
    {
        const bool listening = MiniAudio::MiniAudioPlaybackRequestBus::FindFirstHandler(entity) != nullptr;
        AZ::Data::Asset<MiniAudio::SoundAsset> asset;
        MiniAudio::MiniAudioPlaybackRequestBus::EventResult(asset, entity, &MiniAudio::MiniAudioPlaybackRequests::GetSoundAsset);
        AZ_TracePrintf("Footsteps", "check %s: entity %s listening %d asset %s status %d ready %d bytes %zu\n", label,
            entity.ToString().c_str(), listening ? 1 : 0, asset.GetId().ToFixedString().c_str(), static_cast<int>(asset.GetStatus()),
            asset.IsReady() ? 1 : 0, asset.IsReady() ? asset->m_data.size() : size_t(0));
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
        // Counted for the debug overlay ("Steps heard: 0 of 19"; monsters add to the heard count when they hear one).
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            AZ::s64 steps = 0;
            registry->Get(steps, "/NoclipProtocol/Debug/Steps");
            registry->Set("/NoclipProtocol/Debug/Steps", steps + 1);
        }
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
        ++m_stepsPlayed;
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
        // The character capsule bobs a few millimetres every frame; only a sustained vertical speed means a jump or fall.
        m_verticalSmooth += (verticalSpeed - m_verticalSmooth) * AZStd::min(1.0f, deltaTime * 8.0f);
        const bool airborneNow = std::fabs(m_verticalSmooth) > 1.5f;

        // Two seconds in: check every sound once and play one step (a self-test we can read in the log).
        if (!m_selfTestDone && m_debugTotal > 2.0f)
        {
            m_selfTestDone = true;
            for (size_t i = 0; i < m_stepSounds.size(); ++i)
            {
                DiagnoseSound(m_stepSounds[i], "step");
            }
            DiagnoseSound(m_landSound, "land");
            if (!m_stepSounds.empty())
            {
                PlaySound(m_stepSounds[0], m_sprintVolume);
                AZ_TracePrintf("Footsteps", "self-test: played step 1\n");
            }
        }

        // For the first minute of play, log twice a second while moving (to tune steps without guessing).
        m_debugTotal += deltaTime;
        m_debugTimer += deltaTime;
        if (m_debugTotal < 60.0f && m_debugTimer >= 0.5f)
        {
            m_debugTimer = 0.0f;
            const float flatSpeed = m_speedSmooth;
            if (flatSpeed > 0.3f || airborneNow)
            {
                AZ_TracePrintf("Footsteps", "speed %.2f vz %.2f (smoothed %.2f) airborne %d steps %d\n", flatSpeed, verticalSpeed,
                    m_verticalSmooth, airborneNow ? 1 : 0, m_stepsPlayed);
            }
        }
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
        // Physics moves the player at its own fixed rate, so on a fast graphics card many frames show no movement
        // and the next one shows two frames' worth. Judge speed smoothed over time, and only count as standing still
        // after a moment without movement (it used to reset the stride on every still frame, so no step ever played).
        m_speedSmooth += (step / deltaTime - m_speedSmooth) * AZStd::min(1.0f, deltaTime * 10.0f);
        if (step < 0.0005f)
        {
            m_stillTime += deltaTime;
        }
        else
        {
            m_stillTime = 0.0f;
        }
        if (m_stillTime > 0.2f || (step < 0.0005f && m_speedSmooth < 0.3f))
        {
            // Standing still: the next step comes after half a stride, so starting to walk sounds right.
            m_distance = AZStd::min(m_distance, m_walkStride * 0.5f);
            return;
        }
        const bool sprinting = m_speedSmooth > m_sprintSpeed;
        const float stride = sprinting ? m_sprintStride : m_walkStride;
        m_distance += step;
        if (m_distance >= stride)
        {
            m_distance -= stride;
            PlayStep(sprinting ? m_sprintVolume : m_walkVolume);
        }
    }
} // namespace NoclipProtocol

// Player vitals are compiled with this file until the project is reconfigured with them in noclipprotocol_files.cmake
// (a reconfigure build waits for the owner's OK).
#include "PlayerVitalsComponent.cpp"
#include "../Debug/GameBridgeComponent.cpp" // until the owner OKs a reconfigure for new files
