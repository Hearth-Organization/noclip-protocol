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

    protected:
        void Activate() override;
        void Deactivate() override;

        // AZ::TickBus
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

        // AzFramework::InputChannelEventListener
        bool OnInputChannelEventFiltered(const AzFramework::InputChannel& inputChannel) override;

    private:
        void SetCursorCaptured(bool captured);

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
    };
} // namespace NoclipProtocol
