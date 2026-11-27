// The agents you play: each one a person (the owner, Nov 27 2026: the death card shows the agent's name, years of
// service, and "if they had kids and a family"; the playable agent is randomly a man or a woman "so it mixes things up").
//
// The agent in play lives in the settings registry under /NoclipProtocol/Agent/ (Number, Female, First, Last, Rank,
// Born, Service, Family, Since, Deepest). New() makes the next one; numbers count up from 4417.
#pragma once

#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/std/string/string.h>

#include <chrono>
#include <random>

namespace NoclipProtocol::Agents
{
    struct Agent
    {
        AZ::s64 m_number = 4417;
        bool m_female = false;
        AZStd::string m_first = "Daniel";
        AZStd::string m_last = "Reyes";
        AZStd::string m_rank = "CPL.";
        AZ::s64 m_born = 1994;
        AZ::s64 m_service = 6;          // years with the Foundation
        AZStd::string m_family = "Wife Elena · son Mateo (8), daughter Sofia (5)";
        double m_since = 0.0;           // game time (/NoclipProtocol/Hud/time) they came through the breach
        AZ::s64 m_deepest = 0;          // deepest level reached
    };

    inline constexpr const char* Root = "/NoclipProtocol/Agent";

    inline std::mt19937& Rng()
    {
        static std::mt19937 rng(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
        return rng;
    }
    template <size_t N>
    inline const char* Pick(const char* const (&list)[N])
    {
        return list[std::uniform_int_distribution<size_t>(0, N - 1)(Rng())];
    }
    inline int Roll(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(Rng()); }

    inline Agent Load()
    {
        Agent a;
        if (auto* r = AZ::SettingsRegistry::Get())
        {
            const AZStd::string root = Root;
            // Get() appends to a string that already holds something, so start them empty.
            AZStd::string first, last, rank, family;
            if (r->Get(first, root + "/First")) { a.m_first = first; }
            if (r->Get(last, root + "/Last")) { a.m_last = last; }
            if (r->Get(rank, root + "/Rank")) { a.m_rank = rank; }
            if (r->Get(family, root + "/Family")) { a.m_family = family; }
            r->Get(a.m_number, root + "/Number");
            r->Get(a.m_female, root + "/Female");
            r->Get(a.m_born, root + "/Born");
            r->Get(a.m_service, root + "/Service");
            r->Get(a.m_since, root + "/Since");
            r->Get(a.m_deepest, root + "/Deepest");
        }
        return a;
    }

    inline void Store(const Agent& a)
    {
        if (auto* r = AZ::SettingsRegistry::Get())
        {
            const AZStd::string root = Root;
            r->Set(root + "/Number", a.m_number);
            r->Set(root + "/Female", a.m_female);
            r->Set(root + "/First", a.m_first);
            r->Set(root + "/Last", a.m_last);
            r->Set(root + "/Rank", a.m_rank);
            r->Set(root + "/Born", a.m_born);
            r->Set(root + "/Service", a.m_service);
            r->Set(root + "/Family", a.m_family);
            r->Set(root + "/Since", a.m_since);
            r->Set(root + "/Deepest", a.m_deepest);
            r->Set(root + "/NumberText", AZStd::string::format("%d", static_cast<int>(a.m_number)));
        }
    }

    //! The next agent after `previous`: a new person, randomly a man or a woman, with a family (or none).
    inline Agent New(const Agent& previous, double now)
    {
        static const char* const men[] = { "Daniel", "Marcus", "James", "Elijah", "Tomas", "Andre", "Samuel", "Viktor", "Owen", "Rafael",
                                           "Isaac", "Kenji", "Malik", "Connor", "Diego", "Henry", "Luca", "Nathan", "Omar", "Wesley" };
        static const char* const women[] = { "Mara", "Elena", "Aisha", "Hannah", "Sofia", "Grace", "Leah", "Nadia", "Rosa", "Claire",
                                             "Ingrid", "Yuki", "Danielle", "Priya", "Tessa", "Imani", "Lucia", "Megan", "Zoe", "Vera" };
        static const char* const surnames[] = { "Reyes", "Okonjo", "Hollis", "Kowalski", "Tanaka", "Brennan", "Duarte", "Fischer", "Abara",
                                                "Lindqvist", "Moreau", "Novak", "Castillo", "Hayes", "Mbeki", "Sorensen", "Whitaker",
                                                "Vasquez", "Petrov", "Kade", "Navarro", "Ashford", "Quinn", "Oyelaran" };
        static const char* const ranks[] = { "PFC.", "PFC.", "CPL.", "CPL.", "SGT.", "SPC.", "LT." };
        static const char* const boys[] = { "Mateo", "Eli", "Noah", "Leo", "Sam", "Jack", "Ezra", "Theo", "Kai", "Ben" };
        static const char* const girls[] = { "Sofia", "Ava", "Lily", "Maya", "Nora", "Ruby", "Iris", "June", "Zara", "Ella" };

        Agent a;
        a.m_number = previous.m_number + 1;
        a.m_female = Roll(0, 1) == 1;
        a.m_first = a.m_female ? Pick(women) : Pick(men);
        a.m_last = Pick(surnames);
        a.m_rank = Pick(ranks);
        const int age = Roll(22, 46);
        a.m_born = 2026 - age;
        a.m_service = AZStd::max(1, Roll(1, AZStd::max(1, age - 20)));
        a.m_since = now;
        a.m_deepest = 0;

        // Family: married or not, children or not (ages that fit the agent's).
        const bool partner = Roll(0, 99) < 62;
        const int children = age < 25 ? Roll(0, 1) : (Roll(0, 99) < 35 ? 0 : Roll(1, 3));
        AZStd::string family;
        if (partner)
        {
            const bool husband = a.m_female ? Roll(0, 9) < 9 : Roll(0, 9) == 0;
            family = AZStd::string(husband ? "Husband " : "Wife ") + (husband ? Pick(men) : Pick(women));
        }
        for (int i = 0; i < children; ++i)
        {
            const bool boy = Roll(0, 1) == 1;
            const int childAge = Roll(1, AZStd::max(1, AZStd::min(18, age - 20)));
            family += (family.empty() ? "" : (i == 0 ? " \xc2\xb7 " : ", "));
            family += AZStd::string::format("%s %s (%d)", boy ? "son" : "daughter", boy ? Pick(boys) : Pick(girls), childAge);
        }
        if (family.empty())
        {
            static const char* const alone[] = { "None listed", "Mother in Ohio", "A sister, Kate", "None · a dog, Bishop", "Parents in Lisbon" };
            family = Pick(alone);
        }
        a.m_family = family;
        return a;
    }
} // namespace NoclipProtocol::Agents
