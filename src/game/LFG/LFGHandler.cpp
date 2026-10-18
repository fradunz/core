/*
 * This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
 *
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

#include "Common.h"
#include "WorldPacket.h"
#include "Opcodes.h"
#include "Log.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "WorldSession.h"
#include "Object.h"
#include "World.h"
#include "Group.h"
#include "LFGMgr.h"
#include "GossipDef.h"
#include "Chat.h"
#include "PlayerBotMgr.h"

bool WorldSession::CanJoinMeetingStoneQueue()
{
    if (Group* grp = _player->GetGroup())
    {
        if (!grp->IsLeader(_player->GetObjectGuid()))
        {
            SendMeetingstoneFailed(MEETINGSTONE_FAIL_PARTYLEADER);
            return false;
        }

        if (grp->isRaidGroup())
        {
            SendMeetingstoneFailed(MEETINGSTONE_FAIL_RAID_GROUP);
            return false;
        }

        if (grp->IsFull())
        {
            SendMeetingstoneFailed(MEETINGSTONE_FAIL_FULL_GROUP);
            return false;
        }
    }
    return true;
}

void WorldSession::JoinMeetingStoneQueue(GameObject* obj)
{
    if (!CanJoinMeetingStoneQueue())
        return;

    GameObjectInfo const* gInfo = sObjectMgr.GetGameObjectTemplate(obj->GetEntry());
    sLFGMgr.AddToQueue(_player, gInfo->meetingstone.areaID);
}

void WorldSession::HandleMeetingStoneJoinOpcode(WorldPackets::Misc::MeetingStoneJoin const& packet)
{
    // ignore for remote control state
    if (!_player->IsSelfMover())
        return;

    GameObject* obj = _player->GetGameObjectIfCanInteractWith(packet.guid);

    if (!obj)
        return;

    // Never expect this opcode for some type GO's
    if (obj->GetGoType() != GAMEOBJECT_TYPE_MEETINGSTONE)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "HandleMeetingStoneJoinOpcode: CMSG_MEETINGSTONE_JOIN for not allowed GameObject type %u (Entry %u), didn't expect this to happen.", obj->GetGoType(), obj->GetEntry());
        return;
    }

    if (!CanJoinMeetingStoneQueue())
        return;

    // Meeting stone menu: join the LFG queue or recruit party bots. The choice is handled in
    // HandleGossipSelectOptionOpcode -> HandleMeetingStoneMenuSelect.
    _player->PlayerTalkClass->ClearMenus();
    GossipMenu& menu = _player->PlayerTalkClass->GetGossipMenu();
    menu.AddMenuItem(GOSSIP_ICON_CHAT, "Join the LFG queue", MEETINGSTONE_GOSSIP_SENDER, MEETINGSTONE_MENU_LFG);
    menu.AddMenuItem(GOSSIP_ICON_CHAT, "I need a brave and mighty adventurer to stand at the front of my party.", MEETINGSTONE_GOSSIP_SENDER, MEETINGSTONE_MENU_TANK);
    menu.AddMenuItem(GOSSIP_ICON_CHAT, "I need a caring adventurer to tend our wounds along the journey.", MEETINGSTONE_GOSSIP_SENDER, MEETINGSTONE_MENU_HEALER);
    menu.AddMenuItem(GOSSIP_ICON_CHAT, "I need more adventurers to complete our expedition.", MEETINGSTONE_GOSSIP_SENDER, MEETINGSTONE_MENU_DPS);
    _player->PlayerTalkClass->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, obj->GetObjectGuid());
}

void WorldSession::HandleMeetingStoneMenuSelect(GameObject* obj, uint32 action)
{
    if (action == MEETINGSTONE_MENU_LFG)
    {
        JoinMeetingStoneQueue(obj);
        return;
    }

    if (action != MEETINGSTONE_MENU_TANK &&
        action != MEETINGSTONE_MENU_HEALER &&
        action != MEETINGSTONE_MENU_DPS)
        return;

    // A party has at most 5 members: count the other humans plus the bots already recruited
    // (bots still logging in are not in the group yet).
    constexpr uint32 maxPartySize = 5;
    uint32 humans = 1;
    if (Group* grp = _player->GetGroup())
    {
        humans = 0;
        for (GroupReference* itr = grp->GetFirstMember(); itr != nullptr; itr = itr->next())
            if (Player* pMember = itr->getSource())
                if (!pMember->IsBot())
                    ++humans;
        humans = std::max(humans, 1u);
    }

    uint32 const bots = sPlayerBotMgr.CountPartyBots(_player->GetObjectGuid());
    uint32 slots = humans + bots >= maxPartySize ? 0 : maxPartySize - humans - bots;

    if (uint32 const maxBots = sWorld.getConfig(CONFIG_UINT32_PARTY_BOT_MAX_BOTS))
        slots = bots >= maxBots ? 0 : std::min(slots, maxBots - bots);

    if (!slots)
    {
        ChatHandler(this).SendSysMessage("Your party is already complete.");
        return;
    }

    if (action == MEETINGSTONE_MENU_TANK)
        ChatHandler::PartyBotMenuAdd(_player, "tank");
    else if (action == MEETINGSTONE_MENU_HEALER)
        ChatHandler::PartyBotMenuAdd(_player, "healer");
    else
    {
        for (uint32 i = 0; i < slots; ++i)
            if (!ChatHandler::PartyBotMenuAdd(_player, "dps"))
                break;
    }
}

void WorldSession::HandleMeetingStoneLeaveOpcode(NullClientPacket const& /*packet*/)
{
    if (Group* grp = _player->GetGroup())
    {
        if (grp->IsLeader(_player->GetObjectGuid()) && grp->IsInLFG())
        {
            sWorld.GetLFGQueue().GetMessager().AddMessage([groupId = grp->GetId()](LFGQueue* queue)
            {
                queue->RemoveGroupFromQueue(groupId);
            });
        }
        else
        {
            SendMeetingstoneSetqueue(0, MEETINGSTONE_STATUS_NONE);
        }
    }
    else
    {
        sWorld.GetLFGQueue().GetMessager().AddMessage([playerGuid = _player->GetObjectGuid()](LFGQueue* queue)
        {
            queue->RemovePlayerFromQueue(playerGuid);
        });
    }
}

void WorldSession::HandleMeetingStoneInfoOpcode(NullClientPacket const& /*packet*/)
{
    if (Group* grp = _player->GetGroup())
    {
        if (grp->IsInLFG())
        {
            SendMeetingstoneSetqueue(grp->GetLFGAreaId(), MEETINGSTONE_STATUS_JOINED_QUEUE);
        }
        else
        {
            SendMeetingstoneSetqueue(0, MEETINGSTONE_STATUS_NONE);
        }
    }
    else
    {
        if (!_player || !_player->GetSession())
            return;

        sWorld.GetLFGQueue().GetMessager().AddMessage([playerGuid = _player->GetObjectGuid()](LFGQueue* queue)
        {
            queue->RestoreOfflinePlayer(playerGuid);
        });
    }
}

void WorldSession::SendMeetingstoneFailed(uint8 status)
{
    auto packet = std::make_unique<WorldPackets::Misc::MeetingstoneJoinFailed>();
    packet->reason = status;
    SendPacket(std::move(packet));
}

void WorldSession::SendMeetingstoneSetqueue(uint32 areaid, uint8 status)
{
    auto packet = std::make_unique<WorldPackets::Misc::MeetingstoneSetQueue>();

    packet->areaId = areaid;
#if SUPPORTED_CLIENT_BUILD <= CLIENT_BUILD_1_4_2
    packet->idempotencyToken = 0; // TODO: Must forward this but there are soo many callsites of `SendMeetingstoneSetqueue`
#else
    packet->status = status;
#endif

    SendPacket(std::move(packet));
}
