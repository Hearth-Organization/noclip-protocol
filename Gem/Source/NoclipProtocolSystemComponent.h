
#pragma once

#include <AzCore/Component/Component.h>

#include <NoclipProtocol/NoclipProtocolBus.h>

namespace NoclipProtocol
{
    class NoclipProtocolSystemComponent
        : public AZ::Component
        , protected NoclipProtocolRequestBus::Handler
    {
    public:
        AZ_COMPONENT_DECL(NoclipProtocolSystemComponent);

        static void Reflect(AZ::ReflectContext* context);

        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent);

        NoclipProtocolSystemComponent();
        ~NoclipProtocolSystemComponent();

    protected:
        ////////////////////////////////////////////////////////////////////////
        // NoclipProtocolRequestBus interface implementation

        ////////////////////////////////////////////////////////////////////////

        ////////////////////////////////////////////////////////////////////////
        // AZ::Component interface implementation
        void Init() override;
        void Activate() override;
        void Deactivate() override;
        ////////////////////////////////////////////////////////////////////////
    };
}
