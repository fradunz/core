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
#include <set>
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

enum ABSupportType : uint8
{
    AB_SUPPORT_DEFENSE,
    AB_SUPPORT_ATTACK,
    AB_SUPPORT_COUNT
};

struct ABObjective
{
    uint8 node = 0xFF; // BG_AB_Nodes value, 0xFF if there is no objective.
    ABMode mode = AB_MODE_NONE;
    bool support = false; // Answering a request for support and not there yet.
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

    // A bot asks the team for support at a flag hub (rules in AB_BEHAVIOR.md). 'open' creates the request if there is no live one,
    // 'renew' extends the live one (never reopens its slots). Attack requests wait 10 s between each use of the same hub, and a
    // team has only one live attack request.
    // Returns true when the bot has to announce the request in the battleground chat (opened, or renewed after a while).
    bool RequestSupport(BattleGround* pBG, Player* pBot, uint8 type, uint8 node, uint8 hub, bool open, bool renew);
    // The bot has just reached a hub: it may answer a request of its team, which becomes its objective. True if it did.
    bool TryAcceptSupport(BattleGround* pBG, Player* pBot, uint8 botHub);
    // The bot that answered a request is at its destination: a defender takes a normal defense slot if there is one.
    void OnSupportArrived(BattleGround* pBG, Player* pBot);

    // Copy of what the coordinator knows about a match, for the debug command (see HandleBattleBotABStatusCommand()).
    struct SnapshotBot
    {
        ObjectGuid guid;
        uint8 role = AB_ROLE_GENERAL;
        uint8 node = 0xFF;
        uint8 mode = AB_MODE_NONE;
        uint32 since = 0;
        bool support = false;
        bool arrived = false; // A supporter that got to its destination.
    };

    struct SnapshotRequest
    {
        uint8 type = AB_SUPPORT_DEFENSE;
        uint8 node = 0xFF;
        uint8 hub = 0xFF;
        uint32 slotsUsed = 0;
        int32 msLeft = 0;
    };

    struct Snapshot
    {
        uint8 nodeStates[5] = { 0, 0, 0, 0, 0 }; // As last seen by the coordinator.
        uint32 epoch = 0;
        std::vector<SnapshotBot> bots[2];        // Per team index (0 Alliance, 1 Horde).
        std::vector<SnapshotRequest> requests[2]; // Requests for support that have not expired.
        uint32 held[2] = { 0, 0 };
        uint32 defenseWeight[2] = { 0, 0 };      // In %, the attack weight is the rest.
        uint32 defenseSlots[2] = { 0, 0 };       // Defenders each held flag has room for.
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
        bool support = false; // Objective taken by answering a request: it does not use the normal slots.
        bool arrived = false; // A supporter that got to its destination.
    };

    struct Request
    {
        uint8 type = AB_SUPPORT_DEFENSE;
        uint8 node = 0xFF;     // Flag to defend or to capture.
        uint8 hub = 0xFF;      // Hub of the flag (the destination): the bots are evaluated by their distance to it, from any side.
        uint32 deadline = 0;   // ms timestamp.
        uint32 lastUse = 0;    // ms timestamp of the last time it was opened or renewed.
        uint32 lastAnnounce = 0; // ms timestamp of the last announcement in the chat.
        uint32 slotsLeft = 0;
        std::set<ObjectGuid> rolled; // Bots that already rolled for this request.
    };

    struct Match
    {
        std::map<ObjectGuid, Assignment> bots[2]; // Per team index (0 Alliance, 1 Horde).
        std::vector<Request> requests[2];
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
    // Chance in % that a bot of this role answers a request, given the defense weight of its team.
    static float GetSupportChance(uint8 type, uint8 role, uint32 defenseWeight);
    static bool IsRequestAlive(Request const& request, uint32 now);

    std::mutex m_lock;
    std::map<uint32 /*battleground instance id*/, Match> m_matches;
};

#endif
