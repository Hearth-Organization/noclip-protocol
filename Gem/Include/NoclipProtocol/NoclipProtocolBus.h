
#pragma once

#include <NoclipProtocol/NoclipProtocolTypeIds.h>

#include <AzCore/EBus/EBus.h>
#include <AzCore/Interface/Interface.h>

namespace NoclipProtocol
{
    class NoclipProtocolRequests
    {
    public:
        AZ_RTTI(NoclipProtocolRequests, NoclipProtocolRequestsTypeId);
        virtual ~NoclipProtocolRequests() = default;
        // Put your public methods here
    };

    class NoclipProtocolBusTraits
        : public AZ::EBusTraits
    {
    public:
        //////////////////////////////////////////////////////////////////////////
        // EBusTraits overrides
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;
        //////////////////////////////////////////////////////////////////////////
    };

    using NoclipProtocolRequestBus = AZ::EBus<NoclipProtocolRequests, NoclipProtocolBusTraits>;
    using NoclipProtocolInterface = AZ::Interface<NoclipProtocolRequests>;

} // namespace NoclipProtocol
