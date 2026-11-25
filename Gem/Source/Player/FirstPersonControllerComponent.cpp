#include "FirstPersonControllerComponent.h"

#include <AzCore/Component/TransformBus.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzFramework/Input/Buses/Requests/InputSystemCursorRequestBus.h>
#include <AzFramework/Input/Devices/Keyboard/InputDeviceKeyboard.h>
#include <AzFramework/Input/Devices/Mouse/InputDeviceMouse.h>
#include <AzFramework/Physics/CharacterBus.h>

namespace NoclipProtocol
{
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
        // Start from whatever way the player entity faces in the level.
        AZ::Quaternion rotation = AZ::Quaternion::CreateIdentity();
        AZ::TransformBus::EventResult(rotation, GetEntityId(), &AZ::TransformBus::Events::GetWorldRotationQuaternion);
        m_yaw = rotation.GetEulerRadiansXYZ().GetZ();
        m_pitch = 0.0f;

        AzFramework::InputChannelEventListener::Connect();
        AZ::TickBus::Handler::BusConnect();
        SetCursorCaptured(true);
    }

    void FirstPersonControllerComponent::Deactivate()
    {
        SetCursorCaptured(false);
        AZ::TickBus::Handler::BusDisconnect();
        AzFramework::InputChannelEventListener::Disconnect();
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

    void FirstPersonControllerComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        if (deltaTime <= 0.0f)
        {
            return;
        }

        // --- Look ---
        if (m_cursorCaptured)
        {
            m_yaw -= m_mouseDeltaX * m_mouseSensitivity;
            m_pitch -= m_mouseDeltaY * m_mouseSensitivity;
            const float maxPitch = AZ::DegToRad(m_maxPitchDegrees);
            m_pitch = AZ::GetClamp(m_pitch, -maxPitch, maxPitch);
        }
        m_mouseDeltaX = 0.0f;
        m_mouseDeltaY = 0.0f;

        AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldRotationQuaternion,
            AZ::Quaternion::CreateRotationZ(m_yaw));
        if (m_cameraEntity.IsValid())
        {
            AZ::TransformBus::Event(m_cameraEntity, &AZ::TransformBus::Events::SetLocalRotationQuaternion,
                AZ::Quaternion::CreateRotationX(m_pitch));
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
        float forwardInput = (m_forward ? 1.0f : 0.0f) - (m_back ? 1.0f : 0.0f);
        float rightInput = (m_right ? 1.0f : 0.0f) - (m_left ? 1.0f : 0.0f);

        // O3DE: +Y is forward, +X is right, +Z is up. Rotate the input by the yaw.
        const AZ::Quaternion yawRotation = AZ::Quaternion::CreateRotationZ(m_yaw);
        AZ::Vector3 move = yawRotation.TransformVector(AZ::Vector3(rightInput, forwardInput, 0.0f));
        if (move.GetLengthSq() > 1.0f)
        {
            move.Normalize();
        }
        const float speed = (m_sprint && forwardInput > 0.0f) ? m_sprintSpeed : m_walkSpeed;
        move *= speed;
        move.SetZ(m_verticalSpeed);

        Physics::CharacterRequestBus::Event(GetEntityId(), &Physics::CharacterRequests::AddVelocityForTick, move);
    }
} // namespace NoclipProtocol
