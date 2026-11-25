
#include <AzCore/Serialization/SerializeContext.h>

#include "NoclipProtocolSystemComponent.h"

#include <NoclipProtocol/NoclipProtocolTypeIds.h>

namespace NoclipProtocol
{
    AZ_COMPONENT_IMPL(NoclipProtocolSystemComponent, "NoclipProtocolSystemComponent",
        NoclipProtocolSystemComponentTypeId);

    void NoclipProtocolSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<NoclipProtocolSystemComponent, AZ::Component>()
                ->Version(0)
                ;
        }
    }

    void NoclipProtocolSystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("NoclipProtocolService"));
    }

    void NoclipProtocolSystemComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("NoclipProtocolService"));
    }

    void NoclipProtocolSystemComponent::GetRequiredServices([[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& required)
    {
    }

    void NoclipProtocolSystemComponent::GetDependentServices([[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& dependent)
    {
    }

    NoclipProtocolSystemComponent::NoclipProtocolSystemComponent()
    {
        if (NoclipProtocolInterface::Get() == nullptr)
        {
            NoclipProtocolInterface::Register(this);
        }
    }

    NoclipProtocolSystemComponent::~NoclipProtocolSystemComponent()
    {
        if (NoclipProtocolInterface::Get() == this)
        {
            NoclipProtocolInterface::Unregister(this);
        }
    }

    void NoclipProtocolSystemComponent::Init()
    {
    }

    void NoclipProtocolSystemComponent::Activate()
    {
        NoclipProtocolRequestBus::Handler::BusConnect();
    }

    void NoclipProtocolSystemComponent::Deactivate()
    {
        NoclipProtocolRequestBus::Handler::BusDisconnect();
    }
}
