// "Hearth Blocks": the plain-language scripts made in Hearth Engine's Script Canvas window (approved mockup
// "Script Canvas"). The window writes the blocks into this component as JSON; this runs them in the game:
//   When: the player enters an area, a light dies, an entity sees the player
//   Check: the player has an item (/NoclipProtocol/Items/<item>, given with noclip_give <item>)
//   Do: play a sound or line, open/close/move something, send to another level, shift the rooms
// Every block that runs prints "HearthBlocks: ran <script> <block>" so the editor can light it up.
#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>
#include <AzFramework/Physics/Common/PhysicsSimulatedBodyEvents.h>

namespace NoclipProtocol
{
    class HearthBlocksComponent
        : public AZ::Component
        , public AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(HearthBlocksComponent, "{6A0C8A3E-2E35-4F7B-9B0C-4C1C7E5B9D21}");
        static void Reflect(AZ::ReflectContext* context);

        void Activate() override;
        void Deactivate() override;
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

    private:
        struct Block
        {
            int m_id = 0;
            AZStd::string m_kind;     // when.enter, when.lightDies, when.seen, check.item, do.play, do.move, do.level, do.shift
            AZStd::string m_target;   // entity name (area, light, watcher, sound, thing to move) or level name or item
            AZStd::string m_sound;    // do.move: entity that plays while it moves
            AZStd::string m_mode;     // do.move: open, close or move
            AZ::Vector3 m_offset = AZ::Vector3(0.0f, 0.0f, 3.0f);
            float m_seconds = 1.5f;
            float m_distance = 12.0f;
            bool m_playerOnly = true;
            AZStd::vector<int> m_next;
            AZStd::vector<int> m_no;  // check: when the answer is no
        };
        struct Move
        {
            AZ::EntityId m_entity;
            AZ::Vector3 m_from;
            AZ::Vector3 m_to;
            float m_seconds = 1.0f;
            float m_time = 0.0f;
        };

        void Parse();
        void Run(int id, int depth = 0);
        const Block* Find(int id) const;
        AZ::EntityId Named(const AZStd::string& name) const;
        AZ::EntityId Player() const;

        AZStd::string m_name = "Script";
        AZStd::string m_graph = "{}";
        AZStd::vector<Block> m_blocks;
        AZStd::vector<Move> m_moves;
        AZStd::vector<AzPhysics::SimulatedBodyEvents::OnTriggerEnter::Handler> m_enterHandlers;
        AZStd::vector<bool> m_lightWasOn;   // per when.lightDies block
        AZStd::vector<bool> m_wasSeen;      // per when.seen block
        AZStd::vector<AZStd::string> m_openedNames; // things opened by do.move (open twice does nothing)
        bool m_hooked = false;
    };
} // namespace NoclipProtocol
