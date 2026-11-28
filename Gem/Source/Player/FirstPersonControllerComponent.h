#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Vector3.h>
#include <AzFramework/Input/Events/InputChannelEventListener.h>

namespace NoclipProtocol
{
    //! First-person player movement: WASD, mouse look, sprint and jump.
    //! Put it on the player entity next to a PhysX Character Controller.
    //! The camera is a child entity; it gets the up/down look (pitch),
    //! the player entity itself gets the left/right turn (yaw).
    class FirstPersonControllerComponent
        : public AZ::Component
        , public AZ::TickBus::Handler
        , public AzFramework::InputChannelEventListener
    {
    public:
        AZ_COMPONENT(FirstPersonControllerComponent, "{F759C7A4-CE27-46E4-BDFF-BADE7A17E999}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);

        //! Developer teleport (console command noclip_tp).
        void Teleport(const AZ::Vector3& position, float yaw, float pitch, bool setYaw, bool setPitch);
        AZ::Entity* UiEntity() const { return m_uiEntity; } // the HUD, vitals and weapons
        //! Walk forward for a while as if W were held (the noclip_walk dev command).
        void WalkFor(float seconds, bool sprint) { m_autoWalkSeconds = seconds; m_autoWalkSprint = sprint; m_autoSteer = false; }
        //! The same, steering round walls by itself like someone running through the maze (noclip_run; the trailer).
        void RunFor(float seconds, bool sprint, float firstTurn) { WalkFor(seconds, sprint); m_autoSteer = true; m_steerSide = firstTurn; m_steerTime = 0.0f; }

    protected:
        void Activate() override;
        void Deactivate() override;

        // AZ::TickBus
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

        // AzFramework::InputChannelEventListener
        bool OnInputChannelEventFiltered(const AzFramework::InputChannel& inputChannel) override;

    private:
        void SetCursorCaptured(bool captured);
        void UpdateRayTracedLight();

        // Settings (editable in the Editor)
        AZ::EntityId m_cameraEntity;
        float m_walkSpeed = 3.5f;        // m/s
        float m_sprintSpeed = 6.0f;      // m/s
        float m_jumpSpeed = 4.5f;        // m/s upward
        float m_gravity = 15.0f;         // m/s^2 downward
        float m_mouseSensitivity = 0.0025f; // radians per mouse unit
        float m_maxPitchDegrees = 85.0f;

        // Runtime state
        float m_yaw = 0.0f;
        double m_appliedFov = -1.0;
        float m_pitch = 0.0f;
        float m_mouseDeltaX = 0.0f;
        float m_mouseDeltaY = 0.0f;
        float m_verticalSpeed = 0.0f;
        bool m_grounded = false;
        bool m_forward = false;
        bool m_back = false;
        bool m_left = false;
        bool m_right = false;
        bool m_sprint = false;
        bool m_jumpRequested = false;
        bool m_cursorCaptured = false;
        bool m_facingRead = false;
        AZ::Quaternion m_lastWrittenRotation = AZ::Quaternion::CreateIdentity();

        // Ray-traced bounce light: an entity named "RayTracedLight" under the player with a Diffuse Probe Grid.
        // The grid can't rotate, so it's taken off the player and follows its position only; after a teleport it
        // is switched off for a few frames and starts fresh where the player landed.
        AZ::EntityId m_rayTracedLight;
        bool m_rayTracedLightSearched = false;
        int m_rayTracedLightPause = 0;

        AZ::Entity* m_uiEntity = nullptr; // HUD, pause menu and vitals, made at runtime so levels don't need them
        float m_autoWalkSeconds = 0.0f;
        bool m_autoWalkSprint = false;
        bool m_autoSteer = false;
        float m_steerSide = 1.0f;  // which way to turn when both sides are open: +1 left, -1 right
        float m_steerRate = 0.0f;  // the yaw's speed, eased
        float m_steerTime = 0.0f;
        void Steer(float deltaTime);
    };
} // namespace NoclipProtocol
