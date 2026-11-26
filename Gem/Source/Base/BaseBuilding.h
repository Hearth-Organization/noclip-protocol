// Base building in Level 0's spawn room (the forward base), from the approved Build mockup
// (hearth-workspace noclip-protocol/mockups/ui-and-hearth-engine/Build.dc.html).
//
// Shared between the HUD (build mode: the panel, aiming, the footprint, [LMB] place / [R] rotate / [X] demolish)
// and the Level 0 generator (it spawns and removes the placed pieces). Header only: both include it, and this one
// static State is the hand-off between them.
#pragma once

#include <AzCore/IO/FileIO.h>
#include <AzCore/JSON/document.h>
#include <AzCore/Utils/Utils.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>

#include <cmath>

namespace NoclipProtocol::Base
{
    enum class Kind : int
    {
        Floodlight = 0,
        CeilingPanel,
        FlareStand,
        Beacon,
        UvLamp,
        LightTower,
        Count
    };

    struct ItemInfo
    {
        const char* m_name;   // as on the tile
        const char* m_upper;  // as on the placement label
        int m_scrap;
        int m_wire;
        int m_fuel;
        float m_power;        // kW while it stands
        float m_sizeX;        // footprint (m), before rotating
        float m_sizeY;
        bool m_locked;
        const char* m_lockNote;
    };

    inline const ItemInfo& Info(Kind kind)
    {
        static const ItemInfo infos[static_cast<int>(Kind::Count)] = {
            { "Floodlight", "FLOODLIGHT", 4, 2, 0, 1.0f, 1.0f, 0.7f, false, nullptr },
            { "Ceiling panel fix", "CEILING PANEL FIX", 0, 1, 0, 0.5f, 1.2f, 0.6f, false, nullptr },
            { "Flare stand", "FLARE STAND", 2, 0, 1, 0.0f, 0.6f, 0.6f, false, nullptr },
            { "Foundation beacon", "FOUNDATION BEACON", 10, 6, 0, 2.0f, 0.8f, 0.8f, false, nullptr },
            { "UV lamp", "UV LAMP", 6, 4, 0, 1.5f, 0.8f, 0.6f, false, nullptr },
            { "Light tower", "LIGHT TOWER", 0, 0, 0, 3.0f, 1.2f, 1.2f, true, "Research: Power II" },
        };
        return infos[static_cast<int>(kind)];
    }

    struct Placed
    {
        Kind m_kind;
        float m_x;
        float m_y;
        float m_yaw;
    };

    struct Request
    {
        bool m_place; // false: demolish the piece nearest (m_x, m_y)
        Kind m_kind;
        float m_x;
        float m_y;
        float m_yaw;
        bool m_restore = false; // putting a saved piece back: no cost, no checks, no fanfare
    };

    //! Supplies found lying in the levels.
    enum class Supply : int { Scrap = 0, Wire, Fuel, AlmondWater, Count };
    inline const char* SupplyName(int kind)
    {
        static const char* const names[] = { "Scrap", "Wire", "Fuel", "Almond Water" };
        return names[kind < 0 || kind > 3 ? 0 : kind];
    }

    struct State
    {
        bool m_active = false;   // Level 0 with a spawn room is running (build mode is possible)
        float m_minX = 0.0f;     // the spawn room's inside
        float m_minY = 0.0f;
        float m_maxX = 0.0f;
        float m_maxY = 0.0f;
        float m_cell = 4.0f;     // pillars can stand on the room's cell corners
        float m_breachX = 0.0f;  // keep Breach 4-A clear
        float m_breachY = 0.0f;
        int m_scrap = 38;
        int m_wire = 12;
        int m_fuel = 3;
        int m_almondWater = 5;
        float m_powerMax = 10.0f;
        bool m_beaconUp = false;
        AZStd::vector<Placed> m_placed;     // what stands in the forward base (kept while other levels play, and saved)
        AZStd::vector<Request> m_requests;  // from the HUD, done by the generator on its next tick
        bool m_pickRequested = false;       // [E] at a supply (the HUD asks, the generator picks it up)
        bool m_loaded = false;              // the save has been read this run

        void ResetToStart()
        {
            m_scrap = 38;
            m_wire = 12;
            m_fuel = 3;
            m_almondWater = 5;
            m_powerMax = 10.0f;
            m_beaconUp = false;
            m_placed.clear();
            m_requests.clear();
        }

        // The expedition's save: the forward base and the stockpile (CONTINUE picks it up again).
        static constexpr const char* SavePath = "@user@/Saves/expedition.json";

        void Save() const
        {
            AZStd::string text = AZStd::string::format(
                "{\n  \"version\": 1,\n  \"scrap\": %d,\n  \"wire\": %d,\n  \"fuel\": %d,\n  \"almondWater\": %d,\n  \"placed\": [",
                m_scrap, m_wire, m_fuel, m_almondWater);
            for (size_t i = 0; i < m_placed.size(); ++i)
            {
                const Placed& p = m_placed[i];
                text += AZStd::string::format("%s\n    { \"kind\": %d, \"x\": %.2f, \"y\": %.2f, \"yaw\": %.4f }", i ? "," : "",
                    static_cast<int>(p.m_kind), p.m_x, p.m_y, p.m_yaw);
            }
            text += "\n  ]\n}\n";
            if (auto* io = AZ::IO::FileIOBase::GetInstance())
            {
                char resolved[AZ_MAX_PATH_LEN] = {};
                if (io->ResolvePath(SavePath, resolved, sizeof(resolved)))
                {
                    AZ::Utils::WriteFile(text, resolved);
                }
            }
        }

        //! Reads the save once per run (later calls do nothing). No save: the starting stockpile.
        void LoadOnce()
        {
            if (m_loaded)
            {
                return;
            }
            m_loaded = true;
            auto* io = AZ::IO::FileIOBase::GetInstance();
            char resolved[AZ_MAX_PATH_LEN] = {};
            if (!io || !io->ResolvePath(SavePath, resolved, sizeof(resolved)))
            {
                return;
            }
            auto read = AZ::Utils::ReadFile<AZStd::string>(resolved);
            if (!read.IsSuccess())
            {
                return;
            }
            rapidjson::Document doc;
            doc.Parse(read.GetValue().c_str());
            if (!doc.IsObject())
            {
                return;
            }
            auto number = [&doc](const char* key, int fallback)
            { return doc.HasMember(key) && doc[key].IsNumber() ? static_cast<int>(doc[key].GetDouble()) : fallback; };
            m_scrap = number("scrap", m_scrap);
            m_wire = number("wire", m_wire);
            m_fuel = number("fuel", m_fuel);
            m_almondWater = number("almondWater", m_almondWater);
            m_placed.clear();
            if (doc.HasMember("placed") && doc["placed"].IsArray())
            {
                for (const auto& item : doc["placed"].GetArray())
                {
                    if (!item.IsObject() || !item.HasMember("kind") || !item["kind"].IsNumber())
                    {
                        continue;
                    }
                    const int kind = static_cast<int>(item["kind"].GetDouble());
                    if (kind < 0 || kind >= static_cast<int>(Kind::Count))
                    {
                        continue;
                    }
                    auto f = [&item](const char* key) { return item.HasMember(key) && item[key].IsNumber() ? static_cast<float>(item[key].GetDouble()) : 0.0f; };
                    m_placed.push_back(Placed{ static_cast<Kind>(kind), f("x"), f("y"), f("yaw") });
                }
            }
        }

        //! NEW EXPEDITION: back to the starting stockpile, no base, and the save gone.
        void NewExpedition()
        {
            ResetToStart();
            m_loaded = true;
            if (auto* io = AZ::IO::FileIOBase::GetInstance())
            {
                io->Remove(SavePath);
            }
        }

        float PowerUsed() const
        {
            float used = 0.0f;
            for (const Placed& p : m_placed)
            {
                used += Info(p.m_kind).m_power;
            }
            return used;
        }

        bool Affordable(Kind kind) const
        {
            const ItemInfo& info = Info(kind);
            return !info.m_locked && m_scrap >= info.m_scrap && m_wire >= info.m_wire && m_fuel >= info.m_fuel &&
                PowerUsed() + info.m_power <= m_powerMax + 0.001f;
        }

        //! Why a piece can't go at (x, y), or nullptr when it can.
        const char* Blocked(Kind kind, float x, float y) const
        {
            const ItemInfo& info = Info(kind);
            if (info.m_locked)
            {
                return "locked";
            }
            const float margin = 0.5f + 0.5f * std::fmax(info.m_sizeX, info.m_sizeY);
            if (x < m_minX + margin || x > m_maxX - margin || y < m_minY + margin || y > m_maxY - margin)
            {
                return "outside the forward base";
            }
            if (std::fabs(x - m_breachX) < 2.6f && std::fabs(y - m_breachY) < 2.0f)
            {
                return "keep the breach clear";
            }
            // Pillars may stand on the cell corners inside the room.
            const float cx = std::round(x / m_cell) * m_cell;
            const float cy = std::round(y / m_cell) * m_cell;
            if (std::fabs(x - cx) < 0.6f + 0.5f * info.m_sizeX && std::fabs(y - cy) < 0.6f + 0.5f * info.m_sizeY)
            {
                return "too close to a pillar";
            }
            for (const Placed& p : m_placed)
            {
                const float dx = p.m_x - x;
                const float dy = p.m_y - y;
                if (dx * dx + dy * dy < 1.3f * 1.3f)
                {
                    return "too close to another piece";
                }
            }
            if (m_scrap < info.m_scrap)
            {
                return "not enough scrap";
            }
            if (m_wire < info.m_wire)
            {
                return "not enough wire";
            }
            if (m_fuel < info.m_fuel)
            {
                return "not enough fuel";
            }
            if (PowerUsed() + info.m_power > m_powerMax + 0.001f)
            {
                return "not enough power";
            }
            return nullptr;
        }
    };

    inline State& Get()
    {
        static State state;
        return state;
    }
} // namespace NoclipProtocol::Base
