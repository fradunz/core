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

#ifndef MANGOS_BattleBotABGraph_H
#define MANGOS_BattleBotABGraph_H

#include "BattleBotWaypoints.h"

// Arathi Basin route graph: hubs (junctions, flags, graveyards, bases) linked by legs.
// A leg is a regular BattleBotPath from one hub to another, so the generic waypoint movement walks it.
// Bots only take decisions when they are at a hub.
namespace ABGraph
{
    static uint8 const HUB_NONE = 0xFF;
    static uint8 const FLAG_NONE = 0xFF;

    enum HubId : uint8
    {
        // BEGIN GENERATED: ab_graph hubs (contrib/battlebot_routes/ab/gen_graph.py, do not edit by hand)
        HUB_AS = 0,
        HUB_SMB,
        HUB_M,
        HUB_MBF,
        HUB_F,
        HUB_SH1,
        HUB_S,
        HUB_SBL,
        HUB_L,
        HUB_LBF,
        HUB_SBF,
        HUB_B,
        HUB_SGY,
        HUB_SH2,
        HUB_LGY,
        HUB_TBGY,
        HUB_MGY,
        HUB_BGY,
        HUB_BH1,
        HUB_FGY,
        HUB_FH1,
        HUB_HS,
        HUB_FH2,
        HUB_FH3,
        HUB_DDGY,
        HUB_COUNT
        // END GENERATED: ab_graph hubs
    };

    struct Hub
    {
        char const* name;
        float x, y, z;
        uint8 flagNode; // BG_AB_Nodes value of the flag standing at this hub, FLAG_NONE if there is none.
    };

    struct Edge
    {
        uint8 from;
        uint8 to;
        BattleBotPath* pPath; // Points are stored from 'from' to 'to'.
        bool oneWay;          // Can only be walked from 'from' to 'to'.
    };

    Hub const& GetHub(uint8 hub);
    // Hub where the flag of this node stands (HUB_NONE if the node is invalid).
    uint8 GetFlagHub(uint8 flagNode);
    // Closest hub within maxDistance (3D), HUB_NONE if there is none.
    uint8 FindNearestHub(float x, float y, float z, float maxDistance);
    // Length in yards of the shortest route between two hubs, FLT_MAX if there is none.
    float GetRouteLength(uint8 from, uint8 to);
    // First leg of the shortest route from one hub to another (from != to).
    bool GetNextLeg(uint8 from, uint8 to, BattleBotPath*& pPath, bool& reverse);
    // Hub where a leg ends when it is walked in the given direction.
    bool GetLegEnd(BattleBotPath const* pPath, bool reverse, uint8& hub);
    bool IsOneWayLeg(BattleBotPath const* pPath);
}

#endif
