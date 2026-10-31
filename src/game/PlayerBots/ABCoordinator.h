/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef MANGOS_ABCoordinator_H
#define MANGOS_ABCoordinator_H

#include "Common.h"
#include "ObjectGuid.h"
#include <map>
#include <vector>
#include <mutex>

class BattleGround;
class Player;

// Arathi Basin flag nodes: state of a node (0 neutral, 1/2 contested by Alliance/Horde, 3/4 occupied by Alliance/Horde).
uint8 const AB_NODE_STATE_INVALID = 0xFF;
uint8 GetABNodeState(BattleGround const* pBG, uint8 node);
// A flag can be captured by a team when it is neutral or belongs (even if contested) to the other team.
bool CanCaptureABNode(uint8 state, uint8 teamIndex);

enum ABRole : uint8
{
    AB_ROLE_DEFENDER,
    AB_ROLE_CAPTURER,
    AB_ROLE_GENERAL,
    AB_ROLE_COUNT
};

enum ABMode : uint8
{
    AB_MODE_NONE,
    AB_MODE_ATTACK, // Capture the flag.
    AB_MODE_DEFEND  // Hold the flag.
};

struct ABObjective
{
    uint8 node = 0xFF; // BG_AB_Nodes value, 0xFF if there is no objective.
    ABMode mode = AB_MODE_NONE;
};

// Shared brain of the battle bots of an Arathi Basin match: roles, objectives and the slots of each flag.
// Owned by the PlayerBotMgr. Bots of one match run on the map thread of the battleground, but a bot that leaves it
// is updated by another map, so everything is protected by a single mutex (calls are short).
class ABCoordinator
{
public:
    // Objective of this bot. Kept until the situation of the flags changes, then chosen again.
    ABObjective GetObjective(BattleGround* pBG, Player* pBot);
    // The bot is not part of its match anymore.
    void Unregister(ObjectGuid guid);
    // The bot died: it chooses its objective again when it is back.
    void OnDeath(ObjectGuid guid);

    // Copy of what the coordinator knows about a match, for the debug command (see HandleBattleBotABStatusCommand()).
    struct SnapshotBot
    {
        ObjectGuid guid;
        uint8 role = AB_ROLE_GENERAL;
        uint8 node = 0xFF;
        uint8 mode = AB_MODE_NONE;
        uint32 since = 0;
    };

    struct Snapshot
    {
        uint8 nodeStates[5] = { 0, 0, 0, 0, 0 }; // As last seen by the coordinator.
        uint32 epoch = 0;
        std::vector<SnapshotBot> bots[2];        // Per team index (0 Alliance, 1 Horde).
        uint32 held[2] = { 0, 0 };
        uint32 defenseWeight[2] = { 0, 0 };      // In %, the attack weight is the rest.
        uint32 defenseSlots[2] = { 0, 0 };       // Defenders the team has room for in total (not per flag).
        uint32 captureSlots = 0;                  // Capturers each flag has room for.
    };

    // False if there is no match with this battleground instance id.
    bool GetSnapshot(uint32 instanceId, Snapshot& snapshot);

private:
    struct Assignment
    {
        uint8 role = AB_ROLE_GENERAL;
        uint8 node = 0xFF;
        uint8 mode = AB_MODE_NONE;
        uint32 epoch = 0; // Epoch of the match when the objective was checked last.
        uint32 since = 0; // Order in which the bots took their objectives (lower is older).
    };

    struct Match
    {
        std::map<ObjectGuid, Assignment> bots[2]; // Per team index (0 Alliance, 1 Horde).
        uint8 nodeStates[5] = { 0, 0, 0, 0, 0 };
        uint32 epoch = 1; // Raised every time a flag changes.
        uint32 nextSequence = 1;
        uint32 nextPurge = 0;
    };

    static uint8 PickRole(std::map<ObjectGuid, Assignment> const& bots);
    // Flags held by the team (a flag being converted counts for both teams).
    static uint32 CountHeld(Match const& match, uint8 teamIndex);
    // Weight of the defense in % (the weight of the attack is the rest) and the defenders each held flag has room for.
    static uint32 GetDefenseWeight(Match const& match, uint8 teamIndex);
    static uint32 GetDefenseSlots(uint32 defenseWeight);
    // Can this be done with the flag in its current state?
    static bool IsObjectiveValid(Match const& match, uint8 teamIndex, uint8 node, uint8 mode);
    // After a change of the flags: a valid objective is kept, unless there are more bots than slots (the newest lose).
    static bool KeepObjective(Match const& match, uint8 teamIndex, ObjectGuid self, Assignment const& assignment);
    static void PickObjective(Match& match, uint8 teamIndex, ObjectGuid self, Assignment& assignment);

    std::mutex m_lock;
    std::map<uint32 /*battleground instance id*/, Match> m_matches;
};

#endif
