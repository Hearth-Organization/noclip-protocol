#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Random.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/vector.h>

namespace NoclipProtocol
{
    //! Footsteps for the player. Put it on the player (or a child of it): it measures how far the player
    //! walks on the ground and plays a step every stride, from a set of entities that each carry a
    //! MiniAudio Playback component with one step sound. Strides are longer and steps louder when sprinting;
    //! landing from a jump plays the landing sound.
    class FootstepsComponent
        : public AZ::Component
        , public AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(FootstepsComponent, "{5B1E8C3A-2F47-4D6B-9A0E-7C3D1F8B4E26}");

        static void Reflect(AZ::ReflectContext* context);

    protected:
        void Activate() override;
        void Deactivate() override;

        // AZ::TickBus
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

    private:
        AZ::Vector3 FollowPosition() const;
        void PlayStep(float volumePercent);
        void PlaySound(AZ::EntityId entity, float volumePercent);

        // Settings
        AZ::EntityId m_follow;                         // whose steps these are (empty = this entity)
        AZStd::vector<AZ::EntityId> m_stepSounds;      // one MiniAudio Playback per entity, played in turn
        AZ::EntityId m_landSound;
        float m_walkStride = 0.75f;
        float m_sprintStride = 1.0f;
        float m_sprintSpeed = 4.8f;                    // m/s above which it counts as sprinting
        float m_walkVolume = 55.0f;
        float m_sprintVolume = 80.0f;
        float m_landSpeed = 4.0f;                      // falling speed (m/s) that makes a landing sound

        // Runtime
        AZ::SimpleLcgRandom m_random;
        AZ::Vector3 m_lastPosition = AZ::Vector3::CreateZero();
        float m_distance = 0.0f;
        float m_fallSpeed = 0.0f;
        bool m_airborne = false;
        bool m_started = false;
        size_t m_next = 0;
    };
} // namespace NoclipProtocol
