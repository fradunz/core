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

#include "ABCoordinator.h"
#include "BattleGround.h"
#include "BattleGroundAB.h"
#include "BattleBotABGraph.h"
#include "Player.h"
#include "Utilities/Random.h"
#include <algorithm>
#include <vector>

namespace
{
    uint32 const NODE_COUNT = BG_AB_NODES_MAX;
    uint32 const CAPTURE_SLOTS_PER_NODE = 5;
    uint32 const DEFENSE_SLOTS_AT_FULL_WEIGHT = 5;
    uint32 const PURGE_INTERVAL = 30 * IN_MILLISECONDS;
    // Share of the team (in %) for each role.
    uint32 const ROLE_SHARE[AB_ROLE_COUNT] = { 30, 30, 40 };
    // How far below the best one (in bots) a role can be and still be picked.
    float const ROLE_TOLERANCE = 1.0f;
    // Requests for support: slots besides the bot that asks, how long a request lives, minimum time between attack requests of
    // a hub, and how far a bot can be from the hub of a request to answer it (legs of the graph or yards, whichever is met).
    // The capturable flag closest to the base of the team always gets at least this many attackers.
    uint32 const HOME_FLAG_MIN_ATTACKERS = 1;
    uint32 const SUPPORT_SLOTS = 3;
    uint32 const SUPPORT_DURATION = 15 * IN_MILLISECONDS;
    uint32 const ATTACK_REQUEST_INTERVAL = 10 * IN_MILLISECONDS;
    uint32 const SUPPORT_MAX_LEGS = 2;
    float const SUPPORT_MAX_DISTANCE = 150.0f;
}

uint8 GetABNodeState(BattleGround const* pBG, uint8 node)
{
    for (uint8 state = 0; state <= BG_AB_NODE_STATUS_HORDE_OCCUPIED; state++)
    {
        if (pBG->IsActiveEvent(node, state))
            return state;
    }
    return AB_NODE_STATE_INVALID;
}

// Odd states are Alliance (team index 0 is 'even'), see BattleGroundAB::EventPlayerClickedOnFlag().
bool CanCaptureABNode(uint8 state, uint8 teamIndex)
{
    return state != AB_NODE_STATE_INVALID && (state == BG_AB_NODE_TYPE_NEUTRAL || (state % 2) == teamIndex);
}

// Roles keep the shares of the team (defenders 30%, capturers 30%, general 40%) while bots come and go:
// the new bot gets the role that is the furthest below its share.
uint8 ABCoordinator::PickRole(std::map<ObjectGuid, Assignment> const& bots)
{
    uint32 counts[AB_ROLE_COUNT] = { 0, 0, 0 };
    for (auto const& entry : bots)
        counts[entry.second.role]++;

    float const size = float(bots.size() + 1);
    float deficits[AB_ROLE_COUNT];
    float bestDeficit = -FLT_MAX;
    for (uint8 role = 0; role < AB_ROLE_COUNT; role++)
    {
        deficits[role] = ROLE_SHARE[role] * size / 100.0f - float(counts[role]);
        bestDeficit = std::max(bestDeficit, deficits[role]);
    }

    // Any role that is not more than ROLE_TOLERANCE below the best one can be picked, so the roles of a team can be up to
    // about 2 bots away from their shares.
    std::vector<uint8> candidates;
    for (uint8 role = 0; role < AB_ROLE_COUNT; role++)
    {
        if (deficits[role] >= bestDeficit - ROLE_TOLERANCE - 0.001f)
            candidates.push_back(role);
    }
    return candidates[urand(0, candidates.size() - 1)];
}

uint32 ABCoordinator::CountHeld(Match const& match, uint8 teamIndex)
{
    uint32 held = 0;
    for (uint32 node = 0; node < NODE_COUNT; node++)
    {
        uint8 const state = match.nodeStates[node];
        if (state == BG_AB_NODE_TYPE_OCCUPIED + teamIndex || state == BG_AB_NODE_STATUS_ALLY_CONTESTED || state == BG_AB_NODE_STATUS_HORDE_CONTESTED)
            held++;
    }
    return held;
}

// 20% for each flag held by the team.
uint32 ABCoordinator::GetDefenseWeight(Match const& match, uint8 teamIndex)
{
    return 20 * CountHeld(match, teamIndex);
}

// 5 defenders for a weight of 100%, truncated to a whole number of bots.
uint32 ABCoordinator::GetDefenseSlots(uint32 defenseWeight)
{
    return DEFENSE_SLOTS_AT_FULL_WEIGHT * defenseWeight / 100;
}

// Holding is still valid for a flag that the enemy is converting: it can only be one that the bot was already holding
// (new defenders are never sent to it, see PickObjective()).
bool ABCoordinator::IsObjectiveValid(Match const& match, uint8 teamIndex, uint8 node, uint8 mode)
{
    if (node >= NODE_COUNT)
        return false;

    uint8 const state = match.nodeStates[node];
    if (mode == AB_MODE_ATTACK)
        return CanCaptureABNode(state, teamIndex);
    if (mode == AB_MODE_DEFEND)
        return state == BG_AB_NODE_TYPE_OCCUPIED + teamIndex || state == BG_AB_NODE_STATUS_ALLY_CONTESTED || state == BG_AB_NODE_STATUS_HORDE_CONTESTED;
    return false;
}

bool ABCoordinator::KeepObjective(Match const& match, uint8 teamIndex, ObjectGuid self, Assignment const& assignment)
{
    if (!IsObjectiveValid(match, teamIndex, assignment.node, assignment.mode))
        return false;

    // Supporters do not use the normal slots.
    if (assignment.support)
        return true;

    // Bots with the same objective are all valid or not valid together. The older ones keep their place.
    uint32 const capacity = assignment.mode == AB_MODE_DEFEND ? GetDefenseSlots(GetDefenseWeight(match, teamIndex)) : CAPTURE_SLOTS_PER_NODE;
    uint32 older = 0;
    for (auto const& entry : match.bots[teamIndex])
    {
        Assignment const& other = entry.second;
        if (entry.first == self || other.support || other.node != assignment.node || other.mode != assignment.mode)
            continue;
        if (other.since < assignment.since || (other.since == assignment.since && entry.first < self))
            older++;
    }
    return older < capacity;
}

// Chooses a flag and what to do there. Defense weight is 20% for each flag held by the team (a flag being converted
// counts for both teams), attack weight is the rest. A held flag has 5 defense slots at full weight, a flag to capture has 5.
void ABCoordinator::PickObjective(Match& match, uint8 teamIndex, ObjectGuid self, Assignment& assignment)
{
    assignment.support = false;
    assignment.arrived = false;

    uint32 defenders[NODE_COUNT] = { 0, 0, 0, 0, 0 };
    uint32 attackers[NODE_COUNT] = { 0, 0, 0, 0, 0 };
    for (auto const& entry : match.bots[teamIndex])
    {
        // Bots that did not check since the last change still count if what they do is still possible.
        // Supporters have slots of their own.
        if (entry.first == self || entry.second.support || !IsObjectiveValid(match, teamIndex, entry.second.node, entry.second.mode))
            continue;
        (entry.second.mode == AB_MODE_DEFEND ? defenders : attackers)[entry.second.node]++;
    }

    uint32 const defenseWeight = GetDefenseWeight(match, teamIndex);
    uint32 const defenseSlots = GetDefenseSlots(defenseWeight);
    bool canDefend[NODE_COUNT];
    bool canCapture[NODE_COUNT];
    for (uint32 node = 0; node < NODE_COUNT; node++)
    {
        uint8 const state = match.nodeStates[node];
        // Not a flag the enemy is converting: it may have been neutral, and there is no way to know.
        canDefend[node] = state == BG_AB_NODE_TYPE_OCCUPIED + teamIndex || state == BG_AB_NODE_TYPE_CONTESTED + teamIndex;
        canCapture[node] = CanCaptureABNode(state, teamIndex);
    }


    auto pickFree = [&](bool defend) -> uint8
    {
        std::vector<uint8> nodes;
        for (uint8 node = 0; node < NODE_COUNT; node++)
        {
            if (defend ? (canDefend[node] && defenders[node] < defenseSlots) : (canCapture[node] && attackers[node] < CAPTURE_SLOTS_PER_NODE))
                nodes.push_back(node);
        }
        return nodes.empty() ? uint8(0xFF) : nodes[urand(0, nodes.size() - 1)];
    };

    bool wantDefend;
    switch (assignment.role)
    {
        case AB_ROLE_DEFENDER: wantDefend = true; break;
        case AB_ROLE_CAPTURER: wantDefend = defenseWeight >= 100; break;
        default:               wantDefend = urand(0, 99) < defenseWeight; break;
    }

    uint8 node = pickFree(wantDefend);
    if (node != 0xFF)
        assignment.mode = wantDefend ? AB_MODE_DEFEND : AB_MODE_ATTACK;
    else
    {
        // Nothing free for what it wanted to do (defenders capture when there is no room for them to defend).
        node = pickFree(!wantDefend);
        if (node != 0xFF)
            assignment.mode = wantDefend ? AB_MODE_ATTACK : AB_MODE_DEFEND;
    }

    if (node == 0xFF)
    {
        // Everything is full: the flag to capture with the fewest bots, or any flag we hold.
        uint32 fewest = UINT32_MAX;
        for (uint8 i = 0; i < NODE_COUNT; i++)
        {
            if (canCapture[i] && attackers[i] < fewest)
            {
                fewest = attackers[i];
                node = i;
                assignment.mode = AB_MODE_ATTACK;
            }
        }
        if (node == 0xFF)
        {
            for (uint8 i = 0; i < NODE_COUNT; i++)
            {
                if (canDefend[i])
                {
                    node = i;
                    assignment.mode = AB_MODE_DEFEND;
                    break;
                }
            }
        }
    }

    // The flag closest to the base (by route) is never left without attackers: if it can be captured and has fewer than the
    // minimum, a bot that goes to attack takes it instead of what it had chosen.
    if (node != 0xFF && assignment.mode == AB_MODE_ATTACK)
    {
        ABGraph::HubId const startHub = teamIndex == 0 ? ABGraph::HUB_AS : ABGraph::HUB_HS;
        uint8 homeNode = 0xFF;
        float homeDistance = FLT_MAX;
        for (uint8 i = 0; i < NODE_COUNT; i++)
        {
            float const distance = canCapture[i] ? ABGraph::GetRouteLength(startHub, ABGraph::GetFlagHub(i)) : FLT_MAX;
            if (distance < homeDistance)
            {
                homeDistance = distance;
                homeNode = i;
            }
        }
        if (homeNode != 0xFF && node != homeNode && attackers[homeNode] < HOME_FLAG_MIN_ATTACKERS && attackers[homeNode] < CAPTURE_SLOTS_PER_NODE)
            node = homeNode;
    }

    assignment.node = node;
    if (node == 0xFF)
        assignment.mode = AB_MODE_NONE;
    assignment.epoch = match.epoch;
    assignment.since = match.nextSequence++;
}

ABObjective ABCoordinator::GetObjective(BattleGround* pBG, Player* pBot)
{
    ABObjective objective;
    if (!pBG || !pBot || pBG->GetTypeID() != BATTLEGROUND_AB)
        return objective;

    uint8 const teamIndex = pBot->GetTeam() == HORDE ? 1 : 0;
    uint32 const now = WorldTimer::getMSTime();

    std::lock_guard<std::mutex> guard(m_lock);
    Match& match = m_matches[pBG->GetInstanceID()];

    // A flag changed: everybody chooses again when they reach their next hub.
    for (uint8 node = 0; node < NODE_COUNT; node++)
    {
        uint8 const state = GetABNodeState(pBG, node);
        if (state != AB_NODE_STATE_INVALID && state != match.nodeStates[node])
        {
            match.nodeStates[node] = state;
            match.epoch++;
        }
    }

    // Bots that left without telling (logout, removal).
    if (int32(match.nextPurge - now) <= 0)
    {
        match.nextPurge = now + PURGE_INTERVAL;
        // Requests are kept for a while after they expire: the interval between attack requests counts from the last use.
        for (auto& requests : match.requests)
            requests.erase(std::remove_if(requests.begin(), requests.end(),
                [now](Request const& request) { return int32(request.lastUse + 2 * ATTACK_REQUEST_INTERVAL - now) <= 0 && !IsRequestAlive(request, now); }), requests.end());
        for (auto& bots : match.bots)
        {
            for (auto itr = bots.begin(); itr != bots.end();)
            {
                if (!pBG->IsPlayerInBattleGround(itr->first))
                    itr = bots.erase(itr);
                else
                    ++itr;
            }
        }
    }

    auto& bots = match.bots[teamIndex];
    auto itr = bots.find(pBot->GetObjectGuid());
    if (itr == bots.end())
    {
        Assignment assignment;
        assignment.role = PickRole(bots);
        itr = bots.emplace(pBot->GetObjectGuid(), assignment).first;
    }

    Assignment& assignment = itr->second;
    // A supporter goes to its destination, even if the request is over and the flags changed, and chooses again there.
    bool const traveling = assignment.support && !assignment.arrived;
    if (assignment.epoch != match.epoch && !traveling)
    {
        // A valid objective is never changed by a change of the flags.
        if (KeepObjective(match, teamIndex, itr->first, assignment))
            assignment.epoch = match.epoch;
        else
            PickObjective(match, teamIndex, itr->first, assignment);
    }

    objective.node = assignment.node;
    objective.mode = ABMode(assignment.mode);
    objective.support = assignment.support && !assignment.arrived;
    return objective;
}

void ABCoordinator::Unregister(ObjectGuid guid)
{
    std::lock_guard<std::mutex> guard(m_lock);
    for (auto itr = m_matches.begin(); itr != m_matches.end();)
    {
        itr->second.bots[0].erase(guid);
        itr->second.bots[1].erase(guid);
        if (itr->second.bots[0].empty() && itr->second.bots[1].empty())
            itr = m_matches.erase(itr);
        else
            ++itr;
    }
}

void ABCoordinator::OnDeath(ObjectGuid guid)
{
    std::lock_guard<std::mutex> guard(m_lock);
    for (auto& entry : m_matches)
    {
        for (auto& bots : entry.second.bots)
        {
            auto itr = bots.find(guid);
            if (itr != bots.end())
            {
                itr->second.node = 0xFF;
                itr->second.mode = AB_MODE_NONE;
                itr->second.epoch = 0;
                itr->second.support = false;
                itr->second.arrived = false;
            }
        }
    }
}

bool ABCoordinator::GetSnapshot(uint32 instanceId, Snapshot& snapshot)
{
    std::lock_guard<std::mutex> guard(m_lock);
    auto itr = m_matches.find(instanceId);
    if (itr == m_matches.end())
        return false;

    Match const& match = itr->second;
    snapshot = Snapshot();
    snapshot.epoch = match.epoch;
    snapshot.captureSlots = CAPTURE_SLOTS_PER_NODE;
    for (uint32 node = 0; node < NODE_COUNT; node++)
        snapshot.nodeStates[node] = match.nodeStates[node];

    for (uint8 team = 0; team < 2; team++)
    {
        snapshot.held[team] = CountHeld(match, team);
        snapshot.defenseWeight[team] = GetDefenseWeight(match, team);
        snapshot.defenseSlots[team] = GetDefenseSlots(snapshot.defenseWeight[team]);
        for (auto const& entry : match.bots[team])
        {
            SnapshotBot bot;
            bot.guid = entry.first;
            bot.role = entry.second.role;
            bot.node = entry.second.node;
            bot.mode = entry.second.mode;
            bot.since = entry.second.since;
            bot.support = entry.second.support;
            snapshot.bots[team].push_back(bot);
        }

        uint32 const now = WorldTimer::getMSTime();
        for (Request const& request : match.requests[team])
        {
            if (!IsRequestAlive(request, now))
                continue;
            SnapshotRequest info;
            info.type = request.type;
            info.node = request.node;
            info.hub = request.hub;
            info.slotsUsed = SUPPORT_SLOTS - request.slotsLeft;
            info.msLeft = int32(request.deadline - now);
            snapshot.requests[team].push_back(info);
        }
    }
    return true;
}

bool ABCoordinator::IsRequestAlive(Request const& request, uint32 now)
{
    return int32(request.deadline - now) > 0;
}

// Defense: defenders are the ones that answer most, capturers the least. Attack: the other way around.
float ABCoordinator::GetSupportChance(uint8 type, uint8 role, uint32 defenseWeight)
{
    float multiplier;
    uint32 weight;
    if (type == AB_SUPPORT_DEFENSE)
    {
        weight = defenseWeight;
        multiplier = role == AB_ROLE_DEFENDER ? 2.0f : (role == AB_ROLE_GENERAL ? 1.0f : 0.5f);
    }
    else
    {
        weight = 100 - defenseWeight;
        multiplier = role == AB_ROLE_CAPTURER ? 2.0f : (role == AB_ROLE_GENERAL ? 1.0f : 0.5f);
    }
    return std::min(100.0f, multiplier * float(weight));
}

void ABCoordinator::RequestSupport(BattleGround* pBG, Player* pBot, uint8 type, uint8 node, uint8 hub, bool open, bool renew)
{
    if (!pBG || !pBot || pBG->GetTypeID() != BATTLEGROUND_AB || type >= AB_SUPPORT_COUNT || node >= NODE_COUNT)
        return;

    uint8 const teamIndex = pBot->GetTeam() == HORDE ? 1 : 0;
    uint32 const now = WorldTimer::getMSTime();

    std::lock_guard<std::mutex> guard(m_lock);
    Match& match = m_matches[pBG->GetInstanceID()];

    Request* pRequest = nullptr;
    for (Request& request : match.requests[teamIndex])
    {
        if (request.type == type && request.hub == hub)
        {
            pRequest = &request;
            break;
        }
    }

    // Attack requests of a hub are at least 10 s apart (counted from the last time it was used), defense ones are not.
    bool const allowed = type == AB_SUPPORT_DEFENSE || !pRequest || int32(now - pRequest->lastUse - ATTACK_REQUEST_INTERVAL) >= 0;

    if (pRequest && IsRequestAlive(*pRequest, now))
    {
        // Renewing only extends the deadline.
        if (renew && allowed)
        {
            pRequest->deadline = now + SUPPORT_DURATION;
            pRequest->lastUse = now;
        }
        return;
    }

    if (!open || !allowed)
        return;

    // A team has only one attack request at a time, for any flag.
    if (type == AB_SUPPORT_ATTACK)
    {
        for (Request const& request : match.requests[teamIndex])
        {
            if (request.type == AB_SUPPORT_ATTACK && IsRequestAlive(request, now))
                return;
        }
    }

    if (!pRequest)
    {
        match.requests[teamIndex].emplace_back();
        pRequest = &match.requests[teamIndex].back();
    }
    pRequest->type = type;
    pRequest->node = node;
    pRequest->hub = hub;
    pRequest->deadline = now + SUPPORT_DURATION;
    pRequest->lastUse = now;
    pRequest->slotsLeft = SUPPORT_SLOTS;
    pRequest->rolled.clear();
    pRequest->rolled.insert(pBot->GetObjectGuid());
}

bool ABCoordinator::TryAcceptSupport(BattleGround* pBG, Player* pBot, uint8 botHub)
{
    if (!pBG || !pBot || pBG->GetTypeID() != BATTLEGROUND_AB || botHub >= ABGraph::HUB_COUNT)
        return false;

    uint8 const teamIndex = pBot->GetTeam() == HORDE ? 1 : 0;
    uint32 const now = WorldTimer::getMSTime();

    std::lock_guard<std::mutex> guard(m_lock);
    auto matchItr = m_matches.find(pBG->GetInstanceID());
    if (matchItr == m_matches.end())
        return false;

    Match& match = matchItr->second;
    auto botItr = match.bots[teamIndex].find(pBot->GetObjectGuid());
    if (botItr == match.bots[teamIndex].end())
        return false;

    Assignment& assignment = botItr->second;
    // Already on its way to help somewhere.
    if (assignment.support && !assignment.arrived)
        return false;

    uint32 const defenseWeight = GetDefenseWeight(match, teamIndex);
    for (Request& request : match.requests[teamIndex])
    {
        if (!IsRequestAlive(request, now) || !request.slotsLeft || request.rolled.count(pBot->GetObjectGuid()))
            continue;

        ABMode const mode = request.type == AB_SUPPORT_DEFENSE ? AB_MODE_DEFEND : AB_MODE_ATTACK;
        // Already has that flag as its objective (attacking or defending it): it cannot take a slot of the request.
        if (assignment.node == request.node)
            continue;

        ABGraph::Hub const& hub = ABGraph::GetHub(request.hub);
        if (ABGraph::GetRouteLegCount(botHub, request.hub) > SUPPORT_MAX_LEGS && !pBot->IsWithinDist3d(hub.x, hub.y, hub.z, SUPPORT_MAX_DISTANCE))
            continue;

        // Each bot rolls once for each request.
        request.rolled.insert(pBot->GetObjectGuid());
        if (frand(0.0f, 100.0f) >= GetSupportChance(request.type, assignment.role, defenseWeight))
            continue;

        request.slotsLeft--;
        assignment.node = request.node;
        assignment.mode = mode;
        assignment.support = true;
        assignment.arrived = false;
        assignment.epoch = match.epoch;
        assignment.since = match.nextSequence++;
        return true;
    }
    return false;
}

void ABCoordinator::OnSupportArrived(BattleGround* pBG, Player* pBot)
{
    if (!pBG || !pBot || pBG->GetTypeID() != BATTLEGROUND_AB)
        return;

    uint8 const teamIndex = pBot->GetTeam() == HORDE ? 1 : 0;

    std::lock_guard<std::mutex> guard(m_lock);
    auto matchItr = m_matches.find(pBG->GetInstanceID());
    if (matchItr == m_matches.end())
        return;

    Match& match = matchItr->second;
    auto botItr = match.bots[teamIndex].find(pBot->GetObjectGuid());
    if (botItr == match.bots[teamIndex].end() || !botItr->second.support || botItr->second.arrived)
        return;

    Assignment& assignment = botItr->second;
    assignment.arrived = true;

    // A defender that arrives can take a normal defense slot, if there is one left.
    if (assignment.mode == AB_MODE_DEFEND)
    {
        uint32 defenders = 0;
        for (auto const& entry : match.bots[teamIndex])
        {
            if (entry.first != botItr->first && !entry.second.support && entry.second.node == assignment.node && entry.second.mode == AB_MODE_DEFEND)
                defenders++;
        }
        if (defenders < GetDefenseSlots(GetDefenseWeight(match, teamIndex)))
            assignment.support = false;
    }
}
