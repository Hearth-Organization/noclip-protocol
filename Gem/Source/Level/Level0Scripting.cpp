#include "Level0Scripting.h"
#include "Level0Json.h"

#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Script/ScriptContextAttributes.h>
#include <AzCore/std/string/string.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <queue>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace NoclipProtocol::Level0
{
    namespace
    {
        AZStd::string GenerateArea(const AZStd::string& settingsText, int minX, int minY, int maxX, int maxY)
        {
            return AZStd::string(GenerateAreaJson(std::string(settingsText.c_str()), minX, minY, maxX, maxY).c_str());
        }

        AZStd::string RunTests(const AZStd::string& settingsText, int seeds)
        {
            return AZStd::string(RunTestsJson(std::string(settingsText.c_str()), seeds).c_str());
        }
    } // namespace

    void ReflectScripting(AZ::ReflectContext* context)
    {
        if (auto behavior = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behavior->Method("NoclipLevel0GenerateArea", &GenerateArea)
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Automation)
                ->Attribute(AZ::Script::Attributes::Module, "noclip");
            behavior->Method("NoclipLevel0RunTests", &RunTests)
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Automation)
                ->Attribute(AZ::Script::Attributes::Module, "noclip");
        }
    }
} // namespace NoclipProtocol::Level0
