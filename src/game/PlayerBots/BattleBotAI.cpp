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

#include "BattleBotAI.h"
#include "BattleBotWaypoints.h"
#include "BattleGround.h"
#include "BattleGroundWS.h"
#include "Player.h"
#include "Group.h"
#include "CreatureAI.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "PlayerBotMgr.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "World.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "Chat.h"
#include "Utilities/Random.h"
#include "TargetedMovementGenerator.h"

enum BattleBotSpells
{
    BB_SPELL_FOOD = 1131,
    BB_SPELL_DRINK = 1137,
    BB_SPELL_AUTO_SHOT = 75,
    BB_SPELL_SHOOT_WAND = 5019,

    BB_SPELL_MOUNT_40_HUMAN = 470,
    BB_SPELL_MOUNT_40_NELF = 10787,
    BB_SPELL_MOUNT_40_DWARF = 6896,
    BB_SPELL_MOUNT_40_GNOME = 17456,
    BB_SPELL_MOUNT_40_TROLL = 10795,
    BB_SPELL_MOUNT_40_ORC = 581,
    BB_SPELL_MOUNT_40_TAUREN = 18363,
    BB_SPELL_MOUNT_40_UNDEAD = 8980,

    BB_SPELL_MOUNT_60_HUMAN = 22717,
    BB_SPELL_MOUNT_60_NELF = 22723,
    BB_SPELL_MOUNT_60_DWARF = 22720,
    BB_SPELL_MOUNT_60_GNOME = 22719,
    BB_SPELL_MOUNT_60_TROLL = 22721,
    BB_SPELL_MOUNT_60_ORC = 22724,
    BB_SPELL_MOUNT_60_TAUREN = 22718,
    BB_SPELL_MOUNT_60_UNDEAD = 22722,

    BB_SPELL_MOUNT_40_PALADIN = 13819,
    BB_SPELL_MOUNT_60_PALADIN = 23214,

    BB_SPELL_MOUNT_40_WARLOCK = 5784,
    BB_SPELL_MOUNT_60_WARLOCK = 23161,

    BB_ITEM_ARROW  = 2512,
    BB_ITEM_BULLET = 2516,
};

#define BB_UPDATE_INTERVAL 1000

#define GO_WSG_DROPPED_SILVERWING_FLAG 179785
#define GO_WSG_DROPPED_WARSONG_FLAG 179786
#define GO_WSG_SILVERWING_FLAG 179830
#define GO_WSG_WARSONG_FLAG 179831

void BattleBotAI::AddPremadeGearAndSpells()
{
    uint8 const level = m_level ? m_level : sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

    if (level != me->GetLevel())
    {
        me->GiveLevel(level);
        me->InitTalentForLevel();
        me->SetUInt32Value(PLAYER_XP, 0);
    }

    std::vector<PlayerPremadeSpecTemplate const*> vSpecs;
    for (const auto& itr : sObjectMgr.GetPlayerPremadeSpecTemplates())
    {
        if (itr.second.requiredClass == me->GetClass() &&
            (itr.second.level == level || 
            (level < sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL) && itr.second.level == (level-1))))
            vSpecs.push_back(&itr.second);
    }
    // Use lower level spec template if there are no templates for the current level.
    if (vSpecs.empty())
    {
        for (const auto& itr : sObjectMgr.GetPlayerPremadeSpecTemplates())
        {
            if (itr.second.requiredClass == me->GetClass() &&
                itr.second.level < level)
                vSpecs.push_back(&itr.second);
        }
    }
    if (!vSpecs.empty())
    {
        PlayerPremadeSpecTemplate const* pSpec = SelectRandomContainerElement(vSpecs);
        sObjectMgr.ApplyPremadeSpecTemplateToPlayer(pSpec->entry, me);
        m_role = pSpec->role;
    }

    if (m_role == ROLE_INVALID)
        AutoAssignRole();

    std::vector<PlayerPremadeGearTemplate const*> vGear;
    for (const auto& itr : sObjectMgr.GetPlayerPremadeGearTemplates())
    {
        if (itr.second.requiredClass == me->GetClass() &&
            (itr.second.level == level ||
            (level < sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL) && itr.second.level == (level-1))))
            vGear.push_back(&itr.second);
    }
    // Use lower level gear template if there are no templates for the current level.
    if (vGear.empty())
    {
        for (const auto& itr : sObjectMgr.GetPlayerPremadeGearTemplates())
        {
            if (itr.second.requiredClass == me->GetClass() &&
                itr.second.level < level)
                vGear.push_back(&itr.second);
        }
    }
    if (!vGear.empty())
    {
        uint32 gearId = 0;
        gearId = SelectRandomContainerElement(vGear)->entry;
        sObjectMgr.ApplyPremadeGearTemplateToPlayer(gearId, me);
    } 

    switch (me->GetClass())
    {
        case CLASS_HUNTER:
        {
            if (Item* pItem = me->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED))
            {
                if (pItem->GetProto()->SubClass == ITEM_SUBCLASS_WEAPON_GUN)
                    me->StoreNewItemInBestSlots(BB_ITEM_BULLET, 200);
                else
                    me->StoreNewItemInBestSlots(BB_ITEM_ARROW, 1000);
            }
            break;
        }
    }

    
}

uint32 BattleBotAI::GetMountSpellId() const
{
    if (me->GetLevel() >= 60)
    {
        if (me->GetClass() == CLASS_PALADIN)
            return BB_SPELL_MOUNT_60_PALADIN;
        if (me->GetClass() == CLASS_WARLOCK)
            return BB_SPELL_MOUNT_60_WARLOCK;

        switch (me->GetRace())
        {
            case RACE_HUMAN:
                return BB_SPELL_MOUNT_60_HUMAN;
            case RACE_NIGHTELF:
                return BB_SPELL_MOUNT_60_NELF;
            case RACE_DWARF:
                return BB_SPELL_MOUNT_60_DWARF;
            case RACE_GNOME:
                return BB_SPELL_MOUNT_60_GNOME;
            case RACE_TROLL:
                return BB_SPELL_MOUNT_60_TROLL;
            case RACE_ORC:
                return BB_SPELL_MOUNT_60_ORC;
            case RACE_TAUREN:
                return BB_SPELL_MOUNT_60_TAUREN;
            case RACE_UNDEAD:
                return BB_SPELL_MOUNT_60_UNDEAD;
        }
    }
    else if (me->GetLevel() >= 40)
    {
        if (me->GetClass() == CLASS_PALADIN)
            return BB_SPELL_MOUNT_40_PALADIN;
        if (me->GetClass() == CLASS_WARLOCK)
            return BB_SPELL_MOUNT_40_WARLOCK;

        switch (me->GetRace())
        {
            case RACE_HUMAN:
                return BB_SPELL_MOUNT_40_HUMAN;
            case RACE_NIGHTELF:
                return BB_SPELL_MOUNT_40_NELF;
            case RACE_DWARF:
                return BB_SPELL_MOUNT_40_DWARF;
            case RACE_GNOME:
                return BB_SPELL_MOUNT_40_GNOME;
            case RACE_TROLL:
                return BB_SPELL_MOUNT_40_TROLL;
            case RACE_ORC:
                return BB_SPELL_MOUNT_40_ORC;
            case RACE_TAUREN:
                return BB_SPELL_MOUNT_40_TAUREN;
            case RACE_UNDEAD:
                return BB_SPELL_MOUNT_40_UNDEAD;
        }
    }

    return 0;
}

bool BattleBotAI::UseMount()
{
    if (me->IsMounted())
        return false;

    if (me->IsMoving())
        return false;

    if (me->GetDisplayId() != me->GetNativeDisplayId())
        return false;

    if (me->GetClass() == CLASS_ROGUE)
        return false;

    if (BattleGround* bg = me->GetBattleGround())
        if (bg->GetStatus() == STATUS_WAIT_JOIN)
            return false;

    if (me->HasAura(AURA_WARSONG_FLAG) ||
        me->HasAura(AURA_SILVERWING_FLAG))
        return false;

    if (me->HasAura(SPELL_AURA_MOD_STEALTH))
        return false;

    uint32 spellId = GetMountSpellId();
    if (!spellId)
        return false;

    if (me->CastSpell(me, spellId, false) == SPELL_CAST_OK)
        return true;

    return false;
}

static float const DRINK_EAT_ENEMY_DISTANCE = 40.0f;

bool BattleBotAI::HasHostilePlayerNearby(float range) const
{
    std::list<Player*> players;
    me->GetAlivePlayerListInRange(me, players, range);
    for (Player* pPlayer : players)
    {
        if (pPlayer != me && !pPlayer->IsGameMaster() && me->IsHostileTo(pPlayer) && pPlayer->IsVisibleForOrDetect(me, me, false))
            return true;
    }
    return false;
}

bool BattleBotAI::DrinkAndEat()
{
    if (m_isBuffing)
        return false;

    if (me->IsMounted())
        return false;

    if (me->GetVictim())
        return false;

    // Never sit down (or stay seated) with an enemy close by: stand up and let the combat logic pick a target.
    if (HasHostilePlayerNearby(DRINK_EAT_ENEMY_DISTANCE))
    {
        if (me->HasAura(BB_SPELL_FOOD))
            me->RemoveAurasDueToSpell(BB_SPELL_FOOD);
        if (me->HasAura(BB_SPELL_DRINK))
            me->RemoveAurasDueToSpell(BB_SPELL_DRINK);
        if (me->GetStandState() != UNIT_STAND_STATE_STAND)
            me->SetStandState(UNIT_STAND_STATE_STAND);
        return false;
    }

    bool const isEating = me->HasAura(BB_SPELL_FOOD);
    bool const isDrinking = me->HasAura(BB_SPELL_DRINK);

    // Below this level (in %) the bot stops to eat or drink. Once it started, it goes on until 95%.
    // Warsong Gulch: stopping delays the advance towards the enemy base, so only do it when really needed,
    // and even less when escorting our flag carrier (staying with him comes first).
    static float const REST_UNTIL = 95.0f;
    static float const WSG_START_RESTING = 75.0f;
    static float const WSG_ESCORT_START_RESTING = 35.0f;
    float startResting = REST_UNTIL;
    if (BattleGround* pBG = me->GetBattleGround())
    {
        if (pBG->GetTypeID() == BATTLEGROUND_WS)
            startResting = GetEscortCarrier() ? WSG_ESCORT_START_RESTING : WSG_START_RESTING;
    }

    BattleGround* bg;
    bool const needToEat = me->GetHealthPercent() < (isEating ? REST_UNTIL : startResting) &&
                           !((bg = me->GetBattleGround()) && bg->GetStatus() == STATUS_WAIT_JOIN);
    bool const needToDrink = (me->GetPowerType() == POWER_MANA) &&
                             (me->GetPowerPercent(POWER_MANA) < (isDrinking ? REST_UNTIL : startResting));

    if (!needToEat && !needToDrink)
        return false;

    // Do not stop while carrying flag.
    if (me->HasAura(AURA_WARSONG_FLAG) ||
        me->HasAura(AURA_SILVERWING_FLAG))
        return false;

    if (!isEating && needToEat)
    {
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType())
        {
            ClearPath();
            StopMoving();
        }
        if (SpellEntry const* pSpellEntry = sSpellMgr.GetSpellEntry(BB_SPELL_FOOD))
        {
            me->CastSpell(me, pSpellEntry, true);
            me->RemoveSpellCooldown(pSpellEntry);
        }
        return true;
    }

    if (!isDrinking && needToDrink)
    {
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType())
        {
            ClearPath();
            StopMoving();
        }
        if (SpellEntry const* pSpellEntry = sSpellMgr.GetSpellEntry(BB_SPELL_DRINK))
        {
            me->CastSpell(me, pSpellEntry, true);
            me->RemoveSpellCooldown(pSpellEntry);
        }
        return true;
    }

    return needToEat || needToDrink;
}

float BattleBotAI::GetMaxAggroDistanceForMap() const
{
    BattleGround* bg = me->GetBattleGround();
    if (!bg || bg->GetTypeID() != BATTLEGROUND_AV)
        return 50.0f;

    return 30.0f;
}

bool BattleBotAI::AttackStart(Unit* pVictim)
{
    m_isBuffing = false;

    if (me->IsMounted())
        me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);

    if (me->Attack(pVictim, true))
    {
        SavePathForResume();
        ClearPath();
        BeginChasing(pVictim);
        return true;
    }

    return false;
}

bool BattleBotAI::ShouldIgnoreCombat() const
{
    if (m_battlegroundId == BATTLEGROUND_QUEUE_WS && !me->IsRooted() &&
       (me->HasAura(AURA_SILVERWING_FLAG) || me->HasAura(AURA_WARSONG_FLAG)))
    {
        // A carrier waiting at the capture point for our flag to come back defends itself.
        static float const CARRIER_WAIT_DISTANCE = 10.0f;
        Position const& capturePos = me->GetTeam() == HORDE ? WS_FLAG_POS_HORDE : WS_FLAG_POS_ALLIANCE;
        if (me->IsWithinDist3d(capturePos.x, capturePos.y, capturePos.z, CARRIER_WAIT_DISTANCE))
            return false;

        // A carrier with no ally close by cannot rely on anybody: when an enemy catches it, it stops and fights back,
        // unless it still has an ability to slow, stun or escape from the attacker (used by UpdateFlagCarrierAI).
        static float const CARRIER_ALLY_DISTANCE = 40.0f;
        static float const CARRIER_CATCH_DISTANCE = 15.0f;
        BattleGround* bg = me->GetBattleGround();
        if (!bg)
            return true;

        for (auto const& itr : bg->GetPlayers())
        {
            if (itr.first == me->GetObjectGuid())
                continue;
            Player* pAlly = sObjectAccessor.FindPlayer(itr.first);
            if (pAlly && pAlly->IsAlive() && pAlly->GetTeam() == me->GetTeam() &&
                pAlly->GetMapId() == me->GetMapId() && me->IsWithinDist(pAlly, CARRIER_ALLY_DISTANCE))
                return true;
        }

        // Alone: keep running while nobody is close, the fight starts only when an enemy catches the carrier.
        Unit* pAttacker = me->GetAttackerForHelper();
        if (!pAttacker || me->GetCombatDistance(pAttacker) > CARRIER_CATCH_DISTANCE)
            return true;

        return HasCarrierEscapeAbility(pAttacker);
    }
    return false;
}

bool BattleBotAI::HasCarrierEscapeAbility(Unit const* pAttacker) const
{
    auto const ready = [&](Unit const* pTarget, SpellEntry const* pSpell)
    {
        return pSpell && CanTryToCastSpell(pTarget, pSpell);
    };

    switch (me->GetClass())
    {
        case CLASS_PALADIN:
            return ready(pAttacker, m_spells.paladin.pHammerOfJustice);
        case CLASS_SHAMAN:
            return ready(pAttacker, m_spells.shaman.pFrostShock);
        case CLASS_HUNTER:
            return ready(pAttacker, m_spells.hunter.pWingClip) || ready(pAttacker, m_spells.hunter.pConcussiveShot) ||
                   ready(me, m_spells.hunter.pFrosTrap) || ready(me, m_spells.hunter.pDeterrence);
        case CLASS_MAGE:
            return ready(me, m_spells.mage.pFrostNova) || ready(me, m_spells.mage.pConeofCold) || ready(me, m_spells.mage.pBlink);
        case CLASS_PRIEST:
            return ready(me, m_spells.priest.pPsychicScream);
        case CLASS_WARLOCK:
            return ready(pAttacker, m_spells.warlock.pDeathCoil) || ready(me, m_spells.warlock.pHowlofTerror) ||
                   ready(pAttacker, m_spells.warlock.pCurseofExhaustion);
        case CLASS_WARRIOR:
            return ready(pAttacker, m_spells.warrior.pConcussionBlow) || ready(pAttacker, m_spells.warrior.pPiercingHowl);
        case CLASS_ROGUE:
            return ready(me, m_spells.rogue.pSprint);
        default:
            return false;
    }
}

// Flag carrier of our team (carries the flag of the enemy team).
bool BattleBotAI::IsOwnFlagCarrier(Unit const* pUnit) const
{
    return pUnit->HasAura(me->GetTeam() == ALLIANCE ? AURA_WARSONG_FLAG : AURA_SILVERWING_FLAG);
}

// Distances (yards) used while escorting the carrier of our team.
static float const ESCORT_MAX_CARRIER_DISTANCE = 80.0f; // Farther bots walk towards the carrier using the paths.
static float const ESCORT_THREAT_RADIUS = 20.0f;        // Enemies this close to the carrier are worth fighting.
static float const ESCORT_SELF_DEFENSE_DISTANCE = 8.0f; // Enemies this close to us are fought anywhere.
static float const ESCORT_CLOSE_DISTANCE = 8.0f;        // Escorts try to stay this close to the carrier.

// Distance to the enemy carrier under which a defender goes after him instead of protecting our carrier.
static float const DEFENDER_CHASE_DISTANCE = 40.0f;

// Share (in %) of the general bots that go after the enemy carrier when our flag is taken. The rest of them
// escorts our carrier if we have one, or keeps going for the enemy flag. The dice are rolled again at every
// change of the flags situation (m_wsgChaseRoll), so different bots do each thing during the match.
static uint32 const CHASER_SHARE = 60;

// What the bot should be doing in Warsong Gulch now, from its role and the state of the flags:
//
//                  nothing taken | only our carrier | only our flag taken       | both taken
//   capturer       CAPTURER      | ESCORT           | CAPTURER (never changes)   | CHASER
//   general        GENERAL       | ESCORT           | CHASER (60%) / GENERAL     | CHASER (60%) / ESCORT
//   defender       DEFENDER      | DEFENDER         | CHASER                     | CHASER if the enemy carrier is close, else ESCORT
//
// A carrier is always in CARRIER mode. Capturers keep going for the flag when only the enemy captured one,
// otherwise there would never be a moment with both flags taken.
WSGMode BattleBotAI::GetWSGMode() const
{
    BattleGround* bg = me->GetBattleGround();
    if (!bg || bg->GetTypeID() != BATTLEGROUND_WS)
        return WSG_MODE_GENERAL;

    if (me->HasAura(AURA_WARSONG_FLAG) || me->HasAura(AURA_SILVERWING_FLAG))
        return WSG_MODE_CARRIER;

    BattleGroundWS* pWS = static_cast<BattleGroundWS*>(bg);
    bool const isHorde = me->GetTeam() == HORDE;
    // The enemy carries our flag / our carrier carries the enemy flag.
    ObjectGuid const enemyCarrierGuid = isHorde ? pWS->GetHordeFlagPickerGuid() : pWS->GetAllianceFlagPickerGuid();
    ObjectGuid const ourCarrierGuid = isHorde ? pWS->GetAllianceFlagPickerGuid() : pWS->GetHordeFlagPickerGuid();
    bool const ownFlagStolen = !enemyCarrierGuid.IsEmpty();

    Player* pOurCarrier = ourCarrierGuid.IsEmpty() ? nullptr : sObjectAccessor.FindPlayer(ourCarrierGuid);
    bool const ourCarrierAlive = pOurCarrier && pOurCarrier->IsAlive() && pOurCarrier->GetMapId() == me->GetMapId();

    if (IsWSGDefender())
    {
        if (!ownFlagStolen)
            return WSG_MODE_DEFENDER;
        if (!ourCarrierAlive)
            return WSG_MODE_CHASER;

        Player* pEnemyCarrier = sObjectAccessor.FindPlayer(enemyCarrierGuid);
        if (pEnemyCarrier && pEnemyCarrier->IsAlive() && me->IsWithinDist(pEnemyCarrier, DEFENDER_CHASE_DISTANCE))
            return WSG_MODE_CHASER;
        return WSG_MODE_ESCORT;
    }

    if (IsWSGCapturer())
        return (ownFlagStolen && ourCarrierAlive) ? WSG_MODE_CHASER :
               (!ownFlagStolen && ourCarrierAlive) ? WSG_MODE_ESCORT : WSG_MODE_CAPTURER;

    if (ownFlagStolen && m_wsgChaseRoll < CHASER_SHARE)
        return WSG_MODE_CHASER;

    return ourCarrierAlive ? WSG_MODE_ESCORT : WSG_MODE_GENERAL;
}

// Who carries which flag: bit 0 = the enemy has our flag, bit 1 = we have the enemy flag.
uint8 BattleBotAI::GetWSGFlagState() const
{
    BattleGround* bg = me->GetBattleGround();
    if (!bg || bg->GetTypeID() != BATTLEGROUND_WS)
        return 0;

    BattleGroundWS* pWS = static_cast<BattleGroundWS*>(bg);
    bool const isHorde = me->GetTeam() == HORDE;
    return (!(isHorde ? pWS->GetHordeFlagPickerGuid() : pWS->GetAllianceFlagPickerGuid()).IsEmpty() ? 1 : 0) |
           (!(isHorde ? pWS->GetAllianceFlagPickerGuid() : pWS->GetHordeFlagPickerGuid()).IsEmpty() ? 2 : 0);
}

// Carrier of our team that this bot has to stay close to, if it is escorting (see GetWSGMode()).
Player* BattleBotAI::GetEscortCarrier() const
{
    if (m_battlegroundId != BATTLEGROUND_QUEUE_WS)
        return nullptr;

    BattleGround* bg = me->GetBattleGround();
    if (!bg || bg->GetTypeID() != BATTLEGROUND_WS || bg->GetStatus() != STATUS_IN_PROGRESS)
        return nullptr;

    if (GetWSGMode() != WSG_MODE_ESCORT)
        return nullptr;

    BattleGroundWS* pWS = static_cast<BattleGroundWS*>(bg);
    bool const isHorde = me->GetTeam() == HORDE;
    ObjectGuid const carrierGuid = isHorde ? pWS->GetAllianceFlagPickerGuid() : pWS->GetHordeFlagPickerGuid();
    if (carrierGuid.IsEmpty())
        return nullptr;

    Player* pCarrier = sObjectAccessor.FindPlayer(carrierGuid);
    if (!pCarrier || !pCarrier->IsAlive() || pCarrier->GetMapId() != me->GetMapId() ||
        !me->IsWithinDist(pCarrier, ESCORT_MAX_CARRIER_DISTANCE))
        return nullptr;

    return pCarrier;
}

// In battlegrounds heal the flag carrier first, then whoever has the lowest health.
Unit* BattleBotAI::SelectHealTarget(float healthPercent, bool periodic) const
{
    if (IsInDuel())
        return nullptr;

    // The carrier is healed earlier than other allies.
    static float const CARRIER_HEAL_BONUS = 15.0f;

    Unit* pCarrier = nullptr;
    Unit* pLowest = nullptr;
    float lowestPercent = 101.0f;

    auto const consider = [&](Unit* pUnit, bool isSelf)
    {
        if (periodic && pUnit->HasAuraType(SPELL_AURA_PERIODIC_HEAL))
            return;

        if (!isSelf)
        {
            if (Player* pPlayer = pUnit->ToPlayer())
            {
                // Avoid several healers picking the same target.
                if ((!periodic && pPlayer->GetHealDirectTargetTimer() > 0) ||
                    (periodic && pPlayer->GetHealPeriodicTargetTimer() > 0))
                    return;
            }
        }

        if (IsOwnFlagCarrier(pUnit))
        {
            if (!pCarrier && (isSelf ? pUnit->GetHealthPercent() < std::min(95.0f, healthPercent + CARRIER_HEAL_BONUS) :
                              IsValidHealTarget(pUnit, std::min(95.0f, healthPercent + CARRIER_HEAL_BONUS))))
                pCarrier = pUnit;
            return;
        }

        if (isSelf ? pUnit->GetHealthPercent() >= healthPercent : !IsValidHealTarget(pUnit, healthPercent))
            return;

        if (pUnit->GetHealthPercent() < lowestPercent)
        {
            lowestPercent = pUnit->GetHealthPercent();
            pLowest = pUnit;
        }
    };

    consider(me, true);

    if (Group* pGroup = me->GetGroup())
    {
        for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            if (Player* pMember = itr->getSource())
            {
                if (pMember != me)
                    consider(pMember, false);
            }
        }
    }

    return pCarrier ? pCarrier : pLowest;
}

// Target for Power Word: Shield: the carrier, or the most hurt ally that is being attacked.
Player* BattleBotAI::SelectShieldTarget() const
{
    static uint32 const SPELL_WEAKENED_SOUL = 6788;

    if (IsInDuel())
        return nullptr;

    Player* pTarget = nullptr;
    float lowestPercent = 70.0f;

    if (Group* pGroup = me->GetGroup())
    {
        for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* pMember = itr->getSource();
            if (!pMember || pMember == me || !pMember->IsAlive())
                continue;

            if (pMember->HasAura(SPELL_WEAKENED_SOUL) || pMember->IsImmuneToMechanic(MECHANIC_SHIELD) ||
                !IsValidHealTarget(pMember, 101.0f))
                continue;

            if (IsOwnFlagCarrier(pMember) && !pMember->GetAttackers().empty())
                return pMember;

            if (!pMember->GetAttackers().empty() && pMember->GetHealthPercent() < lowestPercent)
            {
                lowestPercent = pMember->GetHealthPercent();
                pTarget = pMember;
            }
        }
    }

    return pTarget;
}

// Distance below which a focused bot (flag capturer) still fights back.
static float const OBJECTIVE_FOCUS_COMBAT_DISTANCE = 30.0f;

bool BattleBotAI::IsObjectiveFocused() const
{
    return m_battlegroundId == BATTLEGROUND_QUEUE_WS && GetWSGMode() == WSG_MODE_CAPTURER;
}

// Someone is attacking us (not just a fight we started).
bool BattleBotAI::IsBeingAttacked() const
{
    for (Unit* pAttacker : me->GetAttackers())
    {
        if (pAttacker && IsValidHostileTarget(pAttacker) && pAttacker->GetVictim() == me)
            return true;
    }
    return false;
}

// Capturers do not get stuck in the middle of the map: after a short fight that they started they give up and keep running,
// and for a while they do not start new fights. When someone attacks them they always fight back.
bool BattleBotAI::IsFocusFightTooLong() const
{
    static uint32 const FOCUS_MAX_FIGHT_TIME = 12 * IN_MILLISECONDS;
    static uint32 const FOCUS_IGNORE_COMBAT_TIME = 12 * IN_MILLISECONDS;

    // Fights against the enemy carrier or against those who attack us are never abandoned.
    Unit* pVictim = me->GetVictim();
    if (!me->IsInCombat() || IsBeingAttacked() ||
        (pVictim && pVictim->HasAura(me->GetTeam() == HORDE ? AURA_WARSONG_FLAG : AURA_SILVERWING_FLAG)))
    {
        m_focusFightStart = 0;
        return false;
    }

    uint32 const now = WorldTimer::getMSTime();
    if (!m_focusFightStart)
        m_focusFightStart = now;

    if (WorldTimer::getMSTimeDiff(m_focusFightStart, now) > FOCUS_MAX_FIGHT_TIME)
    {
        // Restart the clock so the ignore window really ends (and the fight can resume) after the ignore time.
        m_focusFightStart = 0;
        m_focusIgnoreUntil = now + FOCUS_IGNORE_COMBAT_TIME;
        return true;
    }

    return false;
}

bool BattleBotAI::ShouldKeepMoving() const
{
    return ShouldIgnoreCombat() ||
           (IsObjectiveFocused() && (IsFocusFightTooLong() || !SelectAttackTarget(nullptr)));
}

Unit* BattleBotAI::SelectAttackTarget(Unit* pExcept) const
{
    // Ignore attackers while carrying flag, just keep running.
    if (ShouldIgnoreCombat())
        return nullptr;

    bool const focused = IsObjectiveFocused();
    Player* pEscort = GetEscortCarrier();

    // A capturer that just left a long fight does not start another one right away (except against the enemy carrier).
    if (focused && int32(m_focusIgnoreUntil - WorldTimer::getMSTime()) > 0 && !IsBeingAttacked())
    {
        std::list<Player*> nearPlayers;
        me->GetAlivePlayerListInRange(me, nearPlayers, VISIBILITY_DISTANCE_NORMAL);
        for (Player* pPlayer : nearPlayers)
        {
            if (pPlayer != pExcept && IsValidHostileTarget(pPlayer) &&
                pPlayer->HasAura(me->GetTeam() == HORDE ? AURA_WARSONG_FLAG : AURA_SILVERWING_FLAG))
                return pPlayer;
        }
        return nullptr;
    }

    // While escorting only fight what threatens the carrier or us.
    auto const escortAllows = [&](Unit* pTarget)
    {
        return !pEscort ||
               pTarget->IsWithinDist(pEscort, ESCORT_THREAT_RADIUS) ||
               me->IsWithinDist(pTarget, ESCORT_SELF_DEFENSE_DISTANCE);
    };

    // 1. Check units we are currently in combat with.

    std::list<Unit*> targets;
    HostileReference* pReference = me->GetHostileRefManager().getFirst();

    while (pReference)
    {
        if (Unit* pTarget = pReference->getSourceUnit())
        {
            if (pTarget != pExcept &&
                IsValidHostileTarget(pTarget) &&
                me->IsWithinDist(pTarget, VISIBILITY_DISTANCE_NORMAL))
            {
                if (me->GetTeam() == HORDE)
                {
                    if (pTarget->HasAura(AURA_WARSONG_FLAG))
                        return pTarget;
                }
                else
                {
                    if (pTarget->HasAura(AURA_SILVERWING_FLAG))
                        return pTarget;
                }

                // Focused bots only fight enemies that are very close.
                if (focused && !me->IsWithinDist(pTarget, OBJECTIVE_FOCUS_COMBAT_DISTANCE) && pTarget->GetVictim() != me)
                    continue;

                if (!escortAllows(pTarget))
                    continue;

                targets.push_back(pTarget);
            }
        }
        pReference = pReference->next();
    }

    if (!targets.empty())
    {
        targets.sort([this](Unit* pUnit1, const Unit* pUnit2)
        {
            return me->GetDistance(pUnit1) < me->GetDistance(pUnit2);
        });

        return *targets.begin();
    }

    // 2. Find enemy player in range.

    std::list<Player*> players;
    me->GetAlivePlayerListInRange(me, players, VISIBILITY_DISTANCE_NORMAL);
    float const maxAggroDistance = GetMaxAggroDistanceForMap();

    for (const auto& pTarget : players)
    {
        if (pTarget == pExcept)
            continue;

        if (!IsValidHostileTarget(pTarget))
            continue;

        if (me->GetTeam() == HORDE)
        {
            if (pTarget->HasAura(AURA_WARSONG_FLAG))
                return pTarget;
        }
        else
        {
            if (pTarget->HasAura(AURA_SILVERWING_FLAG))
                return pTarget;
        }

        // Aggro weak enemies from further away.
        float aggroDistance = me->GetHealth() > pTarget->GetHealth() ? maxAggroDistance : 20.0f;
        if (focused)
            aggroDistance = OBJECTIVE_FOCUS_COMBAT_DISTANCE;

        if (!escortAllows(pTarget))
            continue;
        if (!me->IsWithinDist(pTarget, aggroDistance))
            continue;

        if (me->GetDistanceZ(pTarget) > 10.0f)
            continue;

        if (me->IsWithinLOSInMap(pTarget))
            return pTarget;
    }

    // 3. Check party attackers.

    if (Group* pGroup = (focused || pEscort) ? nullptr : me->GetGroup())
    {
        for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            if (Unit* pMember = itr->getSource())
            {
                if (pMember == me)
                    continue;

                if (me->GetDistance(pMember) > 30.0f)
                    continue;

                if (Unit* pAttacker = pMember->GetAttackerForHelper())
                {
                    if (pAttacker != pExcept &&
                        IsValidHostileTarget(pAttacker) &&
                        me->IsWithinDist(pAttacker, maxAggroDistance * 2.0f) &&
                        me->GetDistanceZ(pAttacker) < 10.0f &&
                        me->IsWithinLOSInMap(pAttacker))
                        return pAttacker;
                }
            }
        }
    }

    return nullptr;
}

Unit* BattleBotAI::SelectFollowTarget() const
{
    if (me->HasAura(AURA_WARSONG_FLAG) ||
        me->HasAura(AURA_SILVERWING_FLAG))
        return nullptr;

    std::list<Player*> players;
    me->GetAlivePlayerListInRange(me, players, VISIBILITY_DISTANCE_NORMAL);
    Player* pHealerFollowTarget = nullptr;

    for (const auto& pTarget : players)
    {
        if (pTarget == me)
            continue;

        if (me->GetTeam() != pTarget->GetTeam())
            continue;

        if (pTarget->IsGameMaster())
            continue;

        if (me->GetTeam() == ALLIANCE)
        {
            if (pTarget->HasAura(AURA_WARSONG_FLAG))
                return pTarget;
        }
        else
        {
            if (pTarget->HasAura(AURA_SILVERWING_FLAG))
                return pTarget;
        }

        // In Warsong Gulch healers follow only the carrier: they go with the team using the paths like everybody else.
        if (m_role == ROLE_HEALER && m_battlegroundId != BATTLEGROUND_QUEUE_WS &&
           !IsHealerClass(pTarget->GetClass()) &&
           !IsStealthClass(pTarget->GetClass()) &&
           (pTarget->IsMounted() == me->IsMounted()) &&
           (me->GetDistance2d(pTarget) <= 20.0f) &&
           (me->GetDistanceZ(pTarget) <= 3.0f))
            pHealerFollowTarget = pTarget;
    }

    return pHealerFollowTarget;
}

void BattleBotAI::DoGraveyardJump()
{
    BattleGround* bg = me->GetBattleGround();
    if (!bg || bg->GetTypeID() != BATTLEGROUND_WS)
        return;

    m_doingGraveyardJump = true;
    uint32 timeOffset = 0;
    // Our own graveyard, unless we are standing at the other one (leaving the enemy base).
    std::vector<RecordedMovementPacket>* pPath = me->GetTeam() == HORDE ? &vHordeGraveyardJumpPath : &vAllianceGraveyardJumpPath;
    std::vector<RecordedMovementPacket>* pOtherPath = me->GetTeam() == HORDE ? &vAllianceGraveyardJumpPath : &vHordeGraveyardJumpPath;
    if (me->GetDistance2d(pOtherPath->front().position.x, pOtherPath->front().position.y) <
        me->GetDistance2d(pPath->front().position.x, pPath->front().position.y))
        pPath = pOtherPath;
    for (uint32 i = 0; i < (*pPath).size(); i++)
    {
        RecordedMovementPacket* point = &((*pPath)[i]);
        Player* pBot = me;
        BattleBotAI* pAI = this;
        bool isLast = i == (*pPath).size() - 1;
        timeOffset += point->timeDiff;
        me->m_Events.AddLambdaEventAtOffset([pBot, pAI, point, isLast]
        {
            if (!pBot->HasUnitState(UNIT_STATE_NO_FREE_MOVE))
            {
                pBot->SetUnitMovementFlags(point->moveFlags);
                pBot->Relocate(point->position.x, point->position.y, point->position.z, point->position.o);
                pBot->SendMovementPacket(point->opcode, false);
            }

            if (isLast)
                pAI->m_doingGraveyardJump = false;
        }, timeOffset);
    }
}

void BattleBotAI::StopMoving()
{
    me->StopMoving();
    me->GetMotionMaster()->Clear();
    me->GetMotionMaster()->MoveIdle();
}

void BattleBotAI::OnPacketReceived(WorldPacket const* packet)
{
    //printf("Bot received %s\n", LookupOpcodeName(packet->GetOpcode()));
    switch (packet->GetOpcode())
    {
#if SUPPORTED_CLIENT_BUILD > CLIENT_BUILD_1_4_2
        case MSG_PVP_LOG_DATA:
        {
            if (!me)
                return;

            uint8 ended = *((uint8*)(*packet).contents());
            if (ended)
            {
                // Temporary battlebots are removed after bg ends. Half of the others give up too,
                // so the bots in the battlegrounds change between matches. Each bot leaves a few seconds after
                // the end, at a random moment, so the battleground does not empty at once.
                static uint32 const GIVE_UP_CHANCE = 50;
                static uint32 const MIN_LEAVE_DELAY = 5 * IN_MILLISECONDS;
                static uint32 const MAX_LEAVE_DELAY = 45 * IN_MILLISECONDS;
                if (!m_endActionTime)
                {
                    m_endActionRemove = m_temporary || urand(0, 99) < GIVE_UP_CHANCE;
                    m_endActionTime = WorldTimer::getMSTime() + urand(MIN_LEAVE_DELAY, MAX_LEAVE_DELAY);
                }
            }
            return;
        }
#endif
    }

    CombatBotBaseAI::OnPacketReceived(packet);
}

void BattleBotAI::OnPlayerLogin()
{
    if (!m_initialized)
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING);
}

void BattleBotAI::UpdateWaypointMovement()
{
    // The battleground is over (or gone): nothing left to walk to.
    BattleGround* pBG = me->GetBattleGround();
    if (!pBG || pBG->GetStatus() == STATUS_WAIT_LEAVE)
    {
        m_decidePending = false;
        return;
    }

    // We already have a path.
    if (m_currentPath)
    {
        m_resumePath = nullptr;
        return;
    }

    // Stuck following someone who died, left, or is no longer worth escorting (the flag was lost or returned).
    if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
    {
        if (FollowMovementGenerator<Player> const* pMoveGen = dynamic_cast<FollowMovementGenerator<Player> const*>(me->GetMotionMaster()->GetCurrent()))
        {
            Unit* pTarget = pMoveGen->GetTarget();
            // Following someone as an escort: stop as soon as that player is not the escorted carrier anymore
            // (flag captured, dropped, returned), even if he does not carry a flag anymore.
            bool const staleEscort = pTarget && pTarget->GetObjectGuid() == m_escortFollowGuid && GetEscortCarrier() != pTarget;
            if (!pTarget || !pTarget->IsAlive() || !pTarget->IsWithinDist(me, VISIBILITY_DISTANCE_NORMAL) || staleEscort)
            {
                m_escortFollowGuid.Clear();
                StopMoving();
            }
        }
    }

    if (me->IsMoving())
        return;

    if (!me->IsStopped())
        return;

    if (me->HasUnitState(UNIT_STATE_CAN_NOT_MOVE))
        return;

    switch (me->GetMotionMaster()->GetCurrentMovementGeneratorType())
    {
        case IDLE_MOTION_TYPE:
        case CHASE_MOTION_TYPE:
        case POINT_MOTION_TYPE:
            break;
        default:
            return;
    }

    if (BattleGround* bg = me->GetBattleGround())
    {
        if (bg->GetStatus() == STATUS_WAIT_JOIN)
        {
            // Do not stand still while the gates are closed.
            if (bg->GetTypeID() == BATTLEGROUND_WS)
                WanderAtStart();
            return;
        }
    }

    DecideNextAction();
}

void BattleBotAI::DecideNextAction()
{
    // WSG: a flag carrier close to its own base goes straight to the capture point and waits there
    // (own flag may be missing) instead of picking random paths around the base.
    if (me->GetBattleGround() && me->GetBattleGround()->GetTypeID() == BATTLEGROUND_WS)
    {
        bool const isHorde = me->GetTeam() == HORDE;
        if (me->HasAura(isHorde ? AURA_SILVERWING_FLAG : AURA_WARSONG_FLAG))
        {
            Position const& capturePos = isHorde ? WS_FLAG_POS_HORDE : WS_FLAG_POS_ALLIANCE;
            float const distance = me->GetDistance(capturePos);
            if (distance < 40.0f)
            {
                if (distance > 3.0f)
                    me->GetMotionMaster()->MovePoint(0, capturePos.x, capturePos.y, capturePos.z, MOVE_PATHFINDING | MOVE_EXCLUDE_STEEP_SLOPES);
                else if (!urand(0, 4))
                    ActivateNearbyAreaTrigger(); // Retry capture, own flag may have just returned.
                return;
            }
        }
    }

    // Arathi Basin: an interrupted leg is finished first, decisions are only taken at hubs.
    BattleGround* pBG = me->GetBattleGround();
    if (pBG && pBG->GetTypeID() == BATTLEGROUND_AB && ResumeSavedPath())
        return;

    if (StartNewPathToObjective())
        return;

    // Fight is over: continue the interrupted path instead of walking back.
    if (ResumeSavedPath())
        return;

    if (StartNewPathFromBeginning())
        return;

    StartNewPathFromAnywhere();
}

void BattleBotAI::OnJustDied()
{
    m_decidePending = false;
    m_resumePath = nullptr;
    ClearPath();
    if (me->GetMotionMaster()->GetCurrentMovementGeneratorType())
        StopMoving();
}

void BattleBotAI::OnJustRevived()
{
    SummonPetIfNeeded();
    if (!me->SelectRandomUnfriendlyTarget(nullptr, 30.0f))
        DoGraveyardJump();
}

void BattleBotAI::OnEnterBattleGround()
{
    m_endActionTime = 0;
    m_wsgRole = WSG_ROLE_NONE;
    m_wsgMode = WSG_MODE_GENERAL;
    m_wsgFlagState = 0;
    m_lastBuffGuid.Clear();
    m_resumePath = nullptr;
    m_waitingForGroup = false;
    m_groupWaitStart = 0;
    m_groupWaitIgnoreUntil = 0;
    if (me->GetBattleGround() && me->GetBattleGround()->GetTypeID() == BATTLEGROUND_WS)
        RollWSGRole();

    BattleGround* bg = me->GetBattleGround();
    if (!bg)
        return;

    if (bg->GetStatus() != STATUS_WAIT_JOIN)
        return;

    SummonPetIfNeeded();

    if (bg->GetTypeID() == BATTLEGROUND_WS)
    {
        m_waitingSpot = urand(BB_WSG_WAIT_SPOT_SPAWN, BB_WSG_WAIT_SPOT_RIGHT);
        if (m_waitingSpot == BB_WSG_WAIT_SPOT_RIGHT)
        {
            if (me->GetTeam() == HORDE)
                me->GetMotionMaster()->MovePoint(0, WS_WAITING_POS_HORDE_1.x, WS_WAITING_POS_HORDE_1.y, WS_WAITING_POS_HORDE_1.z, MOVE_PATHFINDING | MOVE_EXCLUDE_STEEP_SLOPES, 0, WS_WAITING_POS_HORDE_1.o);
            else
                me->GetMotionMaster()->MovePoint(0, WS_WAITING_POS_ALLIANCE_1.x, WS_WAITING_POS_ALLIANCE_1.y, WS_WAITING_POS_ALLIANCE_1.z, MOVE_PATHFINDING | MOVE_EXCLUDE_STEEP_SLOPES, 0, WS_WAITING_POS_ALLIANCE_1.o);
        }
        else if (m_waitingSpot == BB_WSG_WAIT_SPOT_LEFT)
        {
            if (me->GetTeam() == HORDE)
                me->GetMotionMaster()->MovePoint(0, WS_WAITING_POS_HORDE_2.x, WS_WAITING_POS_HORDE_2.y, WS_WAITING_POS_HORDE_2.z, MOVE_PATHFINDING | MOVE_EXCLUDE_STEEP_SLOPES, 0, WS_WAITING_POS_HORDE_2.o);
            else
                me->GetMotionMaster()->MovePoint(0, WS_WAITING_POS_ALLIANCE_2.x, WS_WAITING_POS_ALLIANCE_2.y, WS_WAITING_POS_ALLIANCE_2.z, MOVE_PATHFINDING | MOVE_EXCLUDE_STEEP_SLOPES, 0, WS_WAITING_POS_ALLIANCE_2.o);
        }
    }
    else if (bg->GetTypeID() == BATTLEGROUND_AB)
    {
        if (me->GetTeam() == HORDE)
            me->GetMotionMaster()->MovePoint(0, AB_WAITING_POS_HORDE.x + frand(-2.0f, 2.0f), AB_WAITING_POS_HORDE.y + frand(-2.0f, 2.0f), AB_WAITING_POS_HORDE.z, MOVE_PATHFINDING | MOVE_EXCLUDE_STEEP_SLOPES, 0, AB_WAITING_POS_HORDE.o);
        else
            me->GetMotionMaster()->MovePoint(0, AB_WAITING_POS_ALLIANCE.x + frand(-2.0f, 2.0f), AB_WAITING_POS_ALLIANCE.y + frand(-2.0f, 2.0f), AB_WAITING_POS_ALLIANCE.z, MOVE_PATHFINDING | MOVE_EXCLUDE_STEEP_SLOPES, 0, AB_WAITING_POS_ALLIANCE.o);
    }
    else if (bg->GetTypeID() == BATTLEGROUND_AV)
    {
        if (me->GetTeam() == HORDE)
            me->GetMotionMaster()->MovePoint(0, AV_WAITING_POS_HORDE.x + frand(-2.0f, 2.0f), AV_WAITING_POS_HORDE.y + frand(-2.0f, 2.0f), AV_WAITING_POS_HORDE.z, MOVE_PATHFINDING | MOVE_EXCLUDE_STEEP_SLOPES, 0, AV_WAITING_POS_HORDE.o);
        else
            me->GetMotionMaster()->MovePoint(0, AV_WAITING_POS_ALLIANCE.x + frand(-2.0f, 2.0f), AV_WAITING_POS_ALLIANCE.y + frand(-2.0f, 2.0f), AV_WAITING_POS_ALLIANCE.z, MOVE_PATHFINDING | MOVE_EXCLUDE_STEEP_SLOPES, 0, AV_WAITING_POS_ALLIANCE.o);
    }
}

void BattleBotAI::OnLeaveBattleGround()
{
    ClearPath();
    if (me->GetMotionMaster()->GetCurrentMovementGeneratorType())
        StopMoving();

    // Temporary battlebots are removed after bg ends.
    if (m_temporary)
        botEntry->requestRemoval = true;
}

bool BattleBotAI::CheckForUnreachableTarget()
{
    if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE &&
       !me->GetMotionMaster()->GetCurrent()->IsReachable())
    {
        if (Unit* pTarget = static_cast<ChaseMovementGenerator<Player> const*>(me->GetMotionMaster()->GetCurrent())->GetTarget())
        {
            if (!me->CanReachWithMeleeAutoAttack(pTarget))
            {
                if (!me->IsWithinDist(pTarget, VISIBILITY_DISTANCE_NORMAL))
                {
                    me->AttackStop(false);
                    StopMoving();
                    return true;
                }

                if (pTarget->IsCreature() && !me->IsMoving())
                {
                    // Cheating to prevent getting stuck because of bad mmaps.
                    me->NearTeleportTo(pTarget->GetPosition());
                    return true;
                }

                if (me->GetDistanceZ(pTarget) > 10.0f)
                {
                    me->AttackStop(false);
                    StopMoving();
                    return true;
                }
            }
        }
    }

    return false;
}

void BattleBotAI::UpdateAI(uint32 const diff)
{
    m_updateTimer.Update(diff);
    if (m_updateTimer.Passed())
        m_updateTimer.Reset(BB_UPDATE_INTERVAL);
    else
        return;

    if (!me->IsInWorld() || me->IsBeingTeleported() || m_doingGraveyardJump)
        return;

    UpdatePathProgress();

    if (!m_initialized)
    {
        if (m_level && m_level != me->GetLevel())
        {
            me->GiveLevel(m_level);
            me->InitTalentForLevel();
            me->SetUInt32Value(PLAYER_XP, 0);
        }

        // MyBranch: with premade gear, use the custom template selection (accepts level-1 templates)
        if (sWorld.getConfig(CONFIG_UINT32_BATTLE_BOT_AUTO_EQUIP) == PLAYER_BOT_AUTO_EQUIP_PREMADE_GEAR)
            AddPremadeGearAndSpells();
        else
        {
            LearnPremadeSpecForClass();

            if (m_role == ROLE_INVALID)
                AutoAssignRole();

            AutoEquipGear(sWorld.getConfig(CONFIG_UINT32_BATTLE_BOT_AUTO_EQUIP));
        }
        ResetSpellData();
        PopulateSpellData();
        AddAllSpellReagents();
        me->UpdateSkillsToMaxSkillsForLevel();
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING);
        SummonPetIfNeeded();
        me->SetHealthPercent(100.0f);
        me->SetPowerPercent(me->GetPowerType(), 100.0f);

        if (urand(0, 1))
        {
            me->ToggleFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM);
            me->ToggleFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK);
        }

        uint32 newzone, newarea;
        me->GetZoneAndAreaId(newzone, newarea);
        me->UpdateZone(newzone, newarea);

        m_initialized = true;
        return;
    }

    // The battleground ended and this bot's random leaving time came.
    if (m_endActionTime && int32(m_endActionTime - WorldTimer::getMSTime()) <= 0)
    {
        m_endActionTime = 0;
        if (m_endActionRemove)
            botEntry->requestRemoval = true;
        else if (me->InBattleGround()) // Already out (kicked at the end): the normal queue logic goes on.
        {
            auto data = std::make_unique<WorldPackets::Battleground::LeaveBattlefield>();
#if SUPPORTED_CLIENT_BUILD > CLIENT_BUILD_1_8_4
            data->mapId = me->GetMapId();
#endif
            me->GetSession()->QueuePacket(std::move(data));
        }
        return;
    }

    if (!me->InBattleGround())
    {
        if (m_wasInBG)
        {
            m_wasInBG = false;
            OnLeaveBattleGround();
            return;
        }

        if (m_receivedBgInvite)
        {
            SendBattlefieldPortPacket();
            m_receivedBgInvite = false;
            return;
        }

        if (!me->InBattleGroundQueue())
        {
            bool canQueue;
            char args[] = "";
            switch (m_battlegroundId)
            {
                case BATTLEGROUND_QUEUE_AV:
                    canQueue = ChatHandler(me).HandleGoAlteracCommand(args);
                    break;
                case BATTLEGROUND_QUEUE_WS:
                    canQueue = ChatHandler(me).HandleGoWarsongCommand(args);
                    break;
                case BATTLEGROUND_QUEUE_AB:
                    canQueue = ChatHandler(me).HandleGoArathiCommand(args);
                    break;
                default:
                    sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "BattleBot: Invalid BG queue type!");
                    botEntry->requestRemoval = true;
                    return;
            }

            if (!canQueue)
            {
                sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "BattleBot: Attempt to queue for BG failed! Bot is too low level or BG is not available in this patch.");
                botEntry->requestRemoval = true;
                return;
            }

            SendBattlemasterJoinPacket(m_battlegroundId);
            return;
        }

        // Remain idle until we can join battleground.
        return;
    }
    else
    {
        if (!m_wasInBG)
        {
            m_wasInBG = true;
            OnEnterBattleGround();
            return;
        }
        else if (m_temporary)
        {
            // Remove temporary battlebots if no real players in map.
            if (BattleGround* bg = me->GetBattleGround())
            {
                if (bg->GetStatus() == STATUS_IN_PROGRESS)
                {
                    if (!me->GetMap()->HaveRealPlayers())
                    {
                        botEntry->requestRemoval = true;
                        return;
                    }
                }
            }
        }
    }

    if (me->IsDead())
    {
        if (!m_wasDead)
        {
            m_wasDead = true;
            OnJustDied();
            return;
        }

        if (me->InBattleGround())
        {
            if (me->GetDeathState() == CORPSE)
            {
                me->BuildPlayerRepop();
                me->RepopAtGraveyard();
            }
        }
        else
        {
            if (me->GetDeathState() == DEAD)
            {
                me->ResurrectPlayer(0.5f);
                me->SpawnCorpseBones();
                //me->SendCreateUpdateToPlayer(pLeader);
            }
        }

        return;
    }
    else
    {
        if (m_wasDead)
        {
            m_wasDead = false;
            OnJustRevived();
            return;
        }
    }

    // The battleground is over: stop fighting and moving, and wait for the leave time.
    if (BattleGround* pEndedBG = me->GetBattleGround())
    {
        if (pEndedBG->GetStatus() == STATUS_WAIT_LEAVE)
        {
            if (me->GetVictim() || me->IsNonMeleeSpellCasted(false))
            {
                me->AttackStop();
                me->InterruptNonMeleeSpells(false);
                me->ClearTarget();
            }
            if (me->IsMoving())
            {
                me->StopMoving();
                me->GetMotionMaster()->Clear();
                me->GetMotionMaster()->MoveIdle();
            }
            m_decidePending = false;
            return;
        }
    }

    if (me->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL))
    {
        BreakCrowdControlEffects();
        return;
    }

    if (me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
    {
        // Stop auto shot if no target.
        if (!me->GetVictim())
            me->InterruptSpell(CURRENT_AUTOREPEAT_SPELL, true);
        else if (me->GetClass() == CLASS_HUNTER)
        {
            if (me->GetCombatDistance(me->GetVictim()) < 8.0f)
                me->InterruptSpell(CURRENT_AUTOREPEAT_SPELL, true);
            else
                UpdateInCombatAI_Hunter();
        }

        return;
    }

    if (me->IsNonMeleeSpellCasted(false, false, true))
        return;

    if (me->GetTargetGuid() == me->GetObjectGuid())
        me->ClearTarget();

    Unit* pVictim = me->GetVictim();

    // Prevent battelbot from chasing target entered stealth mode
    if (pVictim && !pVictim->IsVisibleForOrDetect(me, me, false))
    {
        me->AttackStop();
        me->ClearTarget();
        me->StopMoving();
        if (pVictim = SelectAttackTarget(pVictim))
            AttackStart(pVictim);
        return;
    }

    if (!me->IsInCombat())
    {
        if (DrinkAndEat())
            return;
    }

    if (me->GetStandState() != UNIT_STAND_STATE_STAND)
        me->SetStandState(UNIT_STAND_STATE_STAND);

    if (me->GetSheath() == SHEATH_STATE_UNARMED && !me->IsMounted())
        me->SetSheath(SHEATH_STATE_MELEE);

    UpdateBattleGroundAI();

    if (!me->IsInCombat())
    {
        if (CheckForUnreachableTarget())
            return;

        UpdateOutOfCombatAI();

        if (m_isBuffing)
            return;

        // Can enter combat from UpdateOutOfCombatAI().
        if (me->IsInCombat())
            return;

        if (me->IsNonMeleeSpellCasted())
            return;

        if (!pVictim || !IsValidHostileTarget(pVictim))
        {
            if (pVictim = SelectAttackTarget(pVictim))
            {
                AttackStart(pVictim);
                return;
            }

            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
            {
                if (Unit* pFollowTarget = SelectFollowTarget())
                {
                    ClearPath();
                    me->GetMotionMaster()->MoveFollow(pFollowTarget, frand(3.0f, 5.0f), frand(0.0f, 3.0f));
                    return;
                }
            }
            else if (FollowMovementGenerator<Player> const* pMoveGen = dynamic_cast<FollowMovementGenerator<Player> const*>(me->GetMotionMaster()->GetCurrent()))
            {
                Unit* pTarget = pMoveGen->GetTarget();
                if (!pTarget || !pTarget->IsAlive() || !pTarget->IsWithinDist(me, VISIBILITY_DISTANCE_NORMAL))
                {
                    StopMoving();
                    return;
                }
            }

            if (UseMount())
                return;

            UpdateWaypointMovement();
        }
        return;
    }

    if (ShouldIgnoreCombat())
    {
        UpdateFlagCarrierAI();
        UpdateWaypointMovement();
        return;
    }

    // Escorts do not leave the carrier alone: ignore fights that do not threaten the carrier or us.
    if (m_role != ROLE_HEALER)
    {
        if (Player* pEscort = GetEscortCarrier())
        {
            if (!SelectAttackTarget(nullptr))
            {
                if (me->GetVictim())
                {
                    me->AttackStop(false);
                    if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
                        StopMoving();
                }

                if (!me->IsWithinDist(pEscort, ESCORT_CLOSE_DISTANCE) &&
                    me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                {
                    ClearPath();
                    m_escortFollowGuid = pEscort->GetObjectGuid();
                    me->GetMotionMaster()->MoveFollow(pEscort, frand(3.0f, 5.0f), frand(0.0f, 3.0f));
                }
                return;
            }
        }
    }

    // Objective focused bots ignore fights that are not very close and keep following their path.
    if (IsObjectiveFocused() && (IsFocusFightTooLong() || !SelectAttackTarget(nullptr)))
    {
        if (me->GetVictim())
        {
            me->AttackStop(false);
            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
                StopMoving();
        }
        UpdateWaypointMovement();
        return;
    }

    if (!pVictim || !IsValidHostileTarget(pVictim) ||
        !pVictim->IsWithinDist(me, VISIBILITY_DISTANCE_SMALL))
    {
        if (Unit* pNewVictim = SelectAttackTarget(pVictim))
        {
            if (pVictim != pNewVictim)
            {
                AttackStart(pNewVictim);
                return;
            }
        }

        if (me->GetVictim() &&
           (me != me->GetVictim()->GetVictim()))
        {
            me->AttackStop(false);
            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
                StopMoving();
            return;
        }
    }
    else
    {
        if (!me->HasInArc(pVictim, 2 * M_PI_F / 3) && !me->IsMoving())
        {
            me->SetInFront(pVictim);
            me->SendMovementPacket(MSG_MOVE_SET_FACING, false);
        }

        if (!me->HasUnitState(UNIT_STATE_MELEE_ATTACKING) &&
           (m_role != ROLE_HEALER) &&
            IsValidHostileTarget(pVictim) &&
            AttackStart(pVictim))
            return;
    }

    if (me->IsInCombat())
        UpdateInCombatAI();
}

void BattleBotAI::UpdateBattleGroundAI()
{
    BattleGround* bg = me->GetBattleGround();
    if (!bg)
        return;

    switch (bg->GetTypeID())
    {
        case BATTLEGROUND_WS:
        {
            // Pick up dropped flags.
            if (GameObject* pGo = me->FindNearestGameObject(GO_WSG_DROPPED_SILVERWING_FLAG, INTERACTION_DISTANCE))
                pGo->Use(me);
            if (GameObject* pGo = me->FindNearestGameObject(GO_WSG_DROPPED_WARSONG_FLAG, INTERACTION_DISTANCE))
                pGo->Use(me);

            // Pick up stationary flags from bases.
            {
                static float const FLAG_DETOUR_DISTANCE = 30.0f;
                bool const carryingFlag = me->HasAura(AURA_WARSONG_FLAG) || me->HasAura(AURA_SILVERWING_FLAG);
                uint32 const enemyFlagEntry = me->GetTeam() == HORDE ? GO_WSG_SILVERWING_FLAG : GO_WSG_WARSONG_FLAG;

                if (!carryingFlag)
                {
                    if (GameObject* pGo = me->FindNearestGameObject(enemyFlagEntry, FLAG_DETOUR_DISTANCE))
                    {
                        if (pGo->isSpawned())
                        {
                            if (me->IsWithinDistInMap(pGo, INTERACTION_DISTANCE))
                            {
                                pGo->Use(me);
                            }
                            else if (m_role != ROLE_HEALER && !me->IsInCombat() && !me->IsMounted() &&
                                     (!me->IsMoving() || m_currentPath))
                            {
                                // The path passed close to the flag without ending at it: go and take it.
                                ClearPath();
                                me->GetMotionMaster()->MovePoint(0, pGo->GetPositionX(), pGo->GetPositionY(), pGo->GetPositionZ(),
                                                                 MOVE_PATHFINDING | MOVE_EXCLUDE_STEEP_SLOPES);
                            }
                        }
                    }
                }
            }
            break;
        }
    }
}

void BattleBotAI::UpdateFlagCarrierAI()
{
    // First those that can be cast both in and out of combat.
    switch (me->GetClass())
    {
        case CLASS_PALADIN:
        {
            if (m_spells.paladin.pBlessingOfFreedom &&
               (me->HasUnitState(UNIT_STATE_ROOT) || me->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED)) &&
                CanTryToCastSpell(me, m_spells.paladin.pBlessingOfFreedom))
            {
                me->CastSpell(me, m_spells.paladin.pBlessingOfFreedom, false);
                return;
            }
            if (m_spells.paladin.pHolyShock && me->GetHealthPercent() < 90.0f &&
                CanTryToCastSpell(me, m_spells.paladin.pHolyShock))
            {
                me->CastSpell(me, m_spells.paladin.pHolyShock, false);
                return;
            }
            break;
        }
        case CLASS_SHAMAN:
        {
            if (m_spells.shaman.pGhostWolf && !me->IsMoving() &&
                CanTryToCastSpell(me, m_spells.shaman.pGhostWolf))
            {
                me->CastSpell(me, m_spells.shaman.pGhostWolf, false);
                return;
            }
            break;
        }
        case CLASS_MAGE:
        {
            if (m_spells.mage.pBlink &&
               (me->HasUnitState(UNIT_STATE_ROOT) || me->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED)) &&
                CanTryToCastSpell(me, m_spells.mage.pBlink))
            {
                me->CastSpell(me, m_spells.mage.pBlink, false);
                return;
            }
            if (m_spells.mage.pManaShield &&
                CanTryToCastSpell(me, m_spells.mage.pManaShield))
            {
                me->CastSpell(me, m_spells.mage.pManaShield, false);
                return;
            }
            if (m_spells.mage.pIceBarrier &&
                CanTryToCastSpell(me, m_spells.mage.pIceBarrier))
            {
                me->CastSpell(me, m_spells.mage.pIceBarrier, false);
                return;
            }
            break;
        }
        case CLASS_PRIEST:
        {
            if (m_spells.priest.pPowerWordShield &&
                CanTryToCastSpell(me, m_spells.priest.pPowerWordShield))
            {
                me->CastSpell(me, m_spells.priest.pPowerWordShield, false);
                return;
            }
            if (m_spells.priest.pHolyNova && me->GetHealthPercent() < 90.0f &&
                CanTryToCastSpell(me, m_spells.priest.pHolyNova))
            {
                me->CastSpell(me, m_spells.priest.pHolyNova, false);
                return;
            }
            break;
        }
        case CLASS_WARRIOR:
        {
            if (m_spells.warrior.pDefensiveStance &&
                CanTryToCastSpell(me, m_spells.warrior.pDefensiveStance))
            {
                me->CastSpell(me, m_spells.warrior.pDefensiveStance, false);
                return;
            }
            break;
        }
        case CLASS_ROGUE:
        {
            if (m_spells.rogue.pSprint &&
                CanTryToCastSpell(me, m_spells.rogue.pSprint))
            {
                me->CastSpell(me, m_spells.rogue.pSprint, false);
                return;
            }
            break;
        }
        case CLASS_DRUID:
        {
            if (me->GetShapeshiftForm() == FORM_NONE)
            {
                if (m_spells.druid.pTravelForm &&
                    CanTryToCastSpell(me, m_spells.druid.pTravelForm))
                {
                    me->CastSpell(me, m_spells.druid.pTravelForm, false);
                    return;
                }
            }
            else if (me->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED))
            {
                me->RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);
            }
            break;
        }
    }

    Unit* pAttacker = me->GetAttackerForHelper();
    if (pAttacker)
    {
        switch (me->GetClass())
        {
            case CLASS_PALADIN:
            {
                if (m_spells.paladin.pHammerOfJustice &&
                    CanTryToCastSpell(pAttacker, m_spells.paladin.pHammerOfJustice))
                {
                    me->CastSpell(pAttacker, m_spells.paladin.pHammerOfJustice, false);
                    return;
                }
                break;
            }
            case CLASS_SHAMAN:
            {
                if (m_spells.shaman.pFrostShock &&
                    CanTryToCastSpell(pAttacker, m_spells.shaman.pFrostShock))
                {
                    me->CastSpell(pAttacker, m_spells.shaman.pFrostShock, false);
                    return;
                }
                break;
            }
            case CLASS_HUNTER:
            {
                if (m_spells.hunter.pAspectOfTheCheetah &&
                    me->HasAura(m_spells.hunter.pAspectOfTheCheetah->Id))
                {
                    me->RemoveAurasDueToSpellByCancel(m_spells.hunter.pAspectOfTheCheetah->Id);
                    return;
                }
                if (m_spells.hunter.pDeterrence && me->GetHealthPercent() < 50.0f &&
                    CanTryToCastSpell(me, m_spells.hunter.pDeterrence))
                {
                    me->CastSpell(me, m_spells.hunter.pDeterrence, false);
                    return;
                }
                if (!pAttacker->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED))
                {
                    if (m_spells.hunter.pWingClip && pAttacker->CanReachWithMeleeAutoAttack(me) &&
                        CanTryToCastSpell(pAttacker, m_spells.hunter.pWingClip))
                    {
                        me->CastSpell(pAttacker, m_spells.hunter.pWingClip, false);
                        return;
                    }
                    if (m_spells.hunter.pConcussiveShot &&
                        CanTryToCastSpell(pAttacker, m_spells.hunter.pConcussiveShot))
                    {
                        me->CastSpell(pAttacker, m_spells.hunter.pConcussiveShot, false);
                        return;
                    }
                }
                if (m_spells.hunter.pFrosTrap && pAttacker->CanReachWithMeleeAutoAttack(me) &&
                    CanTryToCastSpell(me, m_spells.hunter.pFrosTrap))
                {
                    me->CastSpell(me, m_spells.hunter.pFrosTrap, false);
                    return;
                }
                break;
            }
            case CLASS_MAGE:
            {
                if (m_spells.mage.pFrostNova &&
                    CanTryToCastSpell(me, m_spells.mage.pFrostNova) &&
                    m_spells.mage.pFrostNova->IsTargetInRange(me, pAttacker))
                {
                    me->CastSpell(me, m_spells.mage.pFrostNova, false);
                    return;
                }
                if (m_spells.mage.pConeofCold && !pAttacker->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) &&
                    pAttacker->CanReachWithMeleeAutoAttack(me) &&
                    CanTryToCastSpell(me, m_spells.mage.pConeofCold))
                {
                    me->CastSpell(me, m_spells.mage.pConeofCold, false);
                    return;
                }
                if (m_spells.mage.pBlink && me->GetHealthPercent() < 50.0f &&
                    pAttacker->CanReachWithMeleeAutoAttack(me) &&
                    CanTryToCastSpell(me, m_spells.mage.pBlink))
                {
                    me->CastSpell(me, m_spells.mage.pBlink, false);
                    return;
                }
                break;
            }
            case CLASS_PRIEST:
            {
                if (m_spells.priest.pPsychicScream &&
                    CanTryToCastSpell(me, m_spells.priest.pPsychicScream) &&
                    m_spells.priest.pPsychicScream->IsTargetInRange(me, pAttacker))
                {
                    me->CastSpell(me, m_spells.priest.pPsychicScream, false);
                    return;
                }
                break;
            }
            case CLASS_WARLOCK:
            {
                if (m_spells.warlock.pDeathCoil &&
                    CanTryToCastSpell(pAttacker, m_spells.warlock.pDeathCoil))
                {
                    me->CastSpell(pAttacker, m_spells.warlock.pDeathCoil, false);
                    return;
                }
                if (m_spells.warlock.pHowlofTerror && pAttacker->CanReachWithMeleeAutoAttack(me) &&
                    CanTryToCastSpell(me, m_spells.warlock.pHowlofTerror))
                {
                    me->CastSpell(me, m_spells.warlock.pHowlofTerror, false);
                    return;
                }
                if (m_spells.warlock.pCurseofExhaustion && !pAttacker->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) &&
                    CanTryToCastSpell(pAttacker, m_spells.warlock.pCurseofExhaustion))
                {
                    me->CastSpell(pAttacker, m_spells.warlock.pCurseofExhaustion, false);
                    return;
                }
                break;
            }
            case CLASS_WARRIOR:
            {
                if (m_spells.warrior.pShieldWall && me->GetHealthPercent() < 50.0f &&
                    CanTryToCastSpell(me, m_spells.warrior.pShieldWall))
                {
                    me->CastSpell(me, m_spells.warrior.pShieldWall, false);
                    return;
                }
                if (m_spells.warrior.pConcussionBlow && pAttacker->CanReachWithMeleeAutoAttack(me) &&
                    CanTryToCastSpell(pAttacker, m_spells.warrior.pConcussionBlow))
                {
                    me->CastSpell(pAttacker, m_spells.warrior.pConcussionBlow, false);
                    return;
                }
                if (m_spells.warrior.pPiercingHowl && !pAttacker->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) &&
                    me->GetCombatDistance(pAttacker) <= 10.0f &&
                    CanTryToCastSpell(pAttacker, m_spells.warrior.pPiercingHowl))
                {
                    me->CastSpell(me, m_spells.warrior.pPiercingHowl, false);
                    return;
                }
                break;
            }
            case CLASS_ROGUE:
            {
                if (m_spells.rogue.pEvasion && me->GetHealthPercent() < 50.0f &&
                    CanTryToCastSpell(me, m_spells.rogue.pEvasion))
                {
                    me->CastSpell(me, m_spells.rogue.pEvasion, false);
                    return;
                }
                if (m_spells.rogue.pBlind && me->GetCombatDistance(pAttacker) <= 8.0f &&
                    CanTryToCastSpell(pAttacker, m_spells.rogue.pBlind))
                {
                    me->CastSpell(pAttacker, m_spells.rogue.pBlind, false);
                    return;
                }
                if (m_spells.rogue.pKidneyShot && me->GetComboPoints() >= 2 &&
                    pAttacker->CanReachWithMeleeAutoAttack(me) &&
                    CanTryToCastSpell(pAttacker, m_spells.rogue.pKidneyShot))
                {
                    me->CastSpell(pAttacker, m_spells.rogue.pKidneyShot, false);
                    return;
                }
                break;
            }
            case CLASS_DRUID:
            {
                if (m_spells.druid.pBarkskin && me->GetHealthPercent() < 50.0f &&
                    CanTryToCastSpell(me, m_spells.druid.pBarkskin))
                {
                    me->CastSpell(me, m_spells.druid.pBarkskin, false);
                    return;
                }
                break;
            }
        }

        // Nothing to slow or stop the attacker with: keep running and apply instant damage over time on it.
        std::vector<SpellEntry const*> dots;
        switch (me->GetClass())
        {
            case CLASS_WARLOCK:
                dots = { m_spells.warlock.pCurseofAgony, m_spells.warlock.pCorruption, m_spells.warlock.pSiphonLife };
                break;
            case CLASS_PRIEST:
                dots = { m_spells.priest.pShadowWordPain };
                break;
            case CLASS_HUNTER:
                dots = { m_spells.hunter.pSerpentSting };
                break;
            case CLASS_DRUID:
                dots = { m_spells.druid.pMoonfire };
                break;
            case CLASS_SHAMAN:
                dots = { m_spells.shaman.pFlameShock };
                break;
            case CLASS_WARRIOR:
                if (pAttacker->CanReachWithMeleeAutoAttack(me))
                    dots = { m_spells.warrior.pRend };
                break;
            default:
                break;
        }
        for (SpellEntry const* pDot : dots)
        {
            // Only instant spells: a cast time would stop the carrier.
            if (pDot && pDot->GetCastTime(me) == 0 && IsSpellReady(pDot) && CanTryToCastSpell(pAttacker, pDot))
            {
                me->CastSpell(pAttacker, pDot, false);
                return;
            }
        }
    }
    else // no attackers
    {
        switch (me->GetClass())
        {
            case CLASS_HUNTER:
            {
                if (m_spells.hunter.pAspectOfTheCheetah &&
                    CanTryToCastSpell(me, m_spells.hunter.pAspectOfTheCheetah))
                {
                    me->CastSpell(me, m_spells.hunter.pAspectOfTheCheetah, false);
                    return;
                }
                break;
            }
        }
    }
}

void BattleBotAI::UpdateOutOfCombatAI()
{
    switch (me->GetClass())
    {
        case CLASS_PALADIN:
            UpdateOutOfCombatAI_Paladin();
            break;
        case CLASS_SHAMAN:
            UpdateOutOfCombatAI_Shaman();
            break;
        case CLASS_HUNTER:
            UpdateOutOfCombatAI_Hunter();
            break;
        case CLASS_MAGE:
            UpdateOutOfCombatAI_Mage();
            break;
        case CLASS_PRIEST:
            UpdateOutOfCombatAI_Priest();
            break;
        case CLASS_WARLOCK:
            UpdateOutOfCombatAI_Warlock();
            break;
        case CLASS_WARRIOR:
            UpdateOutOfCombatAI_Warrior();
            break;
        case CLASS_ROGUE:
            UpdateOutOfCombatAI_Rogue();
            break;
        case CLASS_DRUID:
            UpdateOutOfCombatAI_Druid();
            break;
    }
}

// A caster (or healer) with a melee enemy on top of him: if there is a way to immobilize that enemy, use it and
// then get away from him. Without such a spell nothing is done (running from a melee class does not help).
bool BattleBotAI::TryImmobilizeMeleeAndRun()
{
    static float const MELEE_THREAT_DISTANCE = 8.0f; // The enemy is this close to us.
    static float const RUN_DISTANCE = 25.0f;

    if (!me->IsAlive() || (m_role != ROLE_RANGE_DPS && m_role != ROLE_HEALER))
        return false;

    switch (me->GetClass())
    {
        case CLASS_MAGE:
        case CLASS_PRIEST:
        case CLASS_WARLOCK:
            break;
        case CLASS_PALADIN:
        case CLASS_DRUID:
            // Only in the caster form (feral and tank druids / ret and protection paladins have other roles).
            if (me->GetClass() == CLASS_DRUID && me->GetShapeshiftForm() != FORM_NONE)
                return false;
            break;
        default:
            return false;
    }

    // Already running away: let it finish.
    if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == DISTANCING_MOTION_TYPE)
        return true;

    // Cannot act at all.
    if (me->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL))
        return false;

    // The closest melee enemy that is hitting us.
    Unit* pMelee = nullptr;
    float closest = MELEE_THREAT_DISTANCE;
    for (Unit* pAttacker : me->GetAttackers())
    {
        if (!pAttacker || !pAttacker->IsPlayer() || !IsValidHostileTarget(pAttacker) ||
            !IsMeleeDamageClass(pAttacker->GetClass()))
            continue;
        float const distance = me->GetCombatDistance(pAttacker);
        if (distance < closest)
        {
            closest = distance;
            pMelee = pAttacker;
        }
    }
    if (!pMelee)
        return false;

    // He cannot follow us right now (immobilized by us or by someone else): this is the moment to get away.
    if (pMelee->HasUnitState(UNIT_STATE_ROOT) || pMelee->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL))
    {
        if (!me->HasUnitState(UNIT_STATE_CAN_NOT_MOVE) && me->GetMotionMaster()->MoveDistance(pMelee, RUN_DISTANCE))
            return true;
        return false;
    }

    // Try to immobilize him. Self cast area spells need him in range.
    switch (me->GetClass())
    {
        case CLASS_MAGE:
        {
            if (m_spells.mage.pFrostNova &&
                CanTryToCastSpell(me, m_spells.mage.pFrostNova) &&
                m_spells.mage.pFrostNova->IsTargetInRange(me, pMelee))
                return DoCastSpell(me, m_spells.mage.pFrostNova) == SPELL_CAST_OK;
            break;
        }
        case CLASS_PRIEST:
        {
            if (m_spells.priest.pPsychicScream &&
                CanTryToCastSpell(me, m_spells.priest.pPsychicScream) &&
                m_spells.priest.pPsychicScream->IsTargetInRange(me, pMelee))
                return DoCastSpell(me, m_spells.priest.pPsychicScream) == SPELL_CAST_OK;
            break;
        }
        case CLASS_WARLOCK:
        {
            if (m_spells.warlock.pDeathCoil &&
                CanTryToCastSpell(pMelee, m_spells.warlock.pDeathCoil) &&
                DoCastSpell(pMelee, m_spells.warlock.pDeathCoil) == SPELL_CAST_OK)
                return true;
            if (m_spells.warlock.pHowlofTerror &&
                CanTryToCastSpell(me, m_spells.warlock.pHowlofTerror) &&
                m_spells.warlock.pHowlofTerror->IsTargetInRange(me, pMelee) &&
                DoCastSpell(me, m_spells.warlock.pHowlofTerror) == SPELL_CAST_OK)
                return true;
            if (m_spells.warlock.pFear &&
                CanTryToCastSpell(pMelee, m_spells.warlock.pFear))
                return DoCastSpell(pMelee, m_spells.warlock.pFear) == SPELL_CAST_OK;
            break;
        }
        case CLASS_DRUID:
        {
            if (m_spells.druid.pEntanglingRoots &&
                CanTryToCastSpell(pMelee, m_spells.druid.pEntanglingRoots))
                return DoCastSpell(pMelee, m_spells.druid.pEntanglingRoots) == SPELL_CAST_OK;
            break;
        }
        case CLASS_PALADIN:
        {
            if (m_spells.paladin.pHammerOfJustice &&
                CanTryToCastSpell(pMelee, m_spells.paladin.pHammerOfJustice))
                return DoCastSpell(pMelee, m_spells.paladin.pHammerOfJustice) == SPELL_CAST_OK;
            break;
        }
    }

    // Nothing to immobilize him with: stay and fight.
    return false;
}

void BattleBotAI::UpdateInCombatAI()
{
    if (TryImmobilizeMeleeAndRun())
        return;

    switch (me->GetClass())
    {
        case CLASS_PALADIN:
            UpdateInCombatAI_Paladin();
            break;
        case CLASS_SHAMAN:
            UpdateInCombatAI_Shaman();
            break;
        case CLASS_HUNTER:
            UpdateInCombatAI_Hunter();
            break;
        case CLASS_MAGE:
            UpdateInCombatAI_Mage();
            break;
        case CLASS_PRIEST:
            UpdateInCombatAI_Priest();
            break;
        case CLASS_WARLOCK:
            UpdateInCombatAI_Warlock();
            break;
        case CLASS_WARRIOR:
            UpdateInCombatAI_Warrior();
            break;
        case CLASS_ROGUE:
            UpdateInCombatAI_Rogue();
            break;
        case CLASS_DRUID:
            UpdateInCombatAI_Druid();
            break;
    }

    if (me->GetVictim())
        UseTrinketEffects();
}

void BattleBotAI::UpdateOutOfCombatAI_Paladin()
{
    if (m_spells.paladin.pAura &&
        CanTryToCastSpell(me, m_spells.paladin.pAura))
    {
        if (DoCastSpell(me, m_spells.paladin.pAura) == SPELL_CAST_OK)
            return;
    }

    if (m_spells.paladin.pBlessingBuff)
    {
        if (Player* pTarget = SelectBuffTarget(m_spells.paladin.pBlessingBuff))
        {
            if (CanTryToCastSpell(pTarget, m_spells.paladin.pBlessingBuff))
            {
                if (DoCastSpell(pTarget, m_spells.paladin.pBlessingBuff) == SPELL_CAST_OK)
                {
                    m_isBuffing = true;
                    return;
                }
            }
        }
    }

    if (m_isBuffing &&
       (!m_spells.paladin.pBlessingBuff ||
        !me->HasGCD(m_spells.paladin.pBlessingBuff)))
    {
        m_isBuffing = false;
    }

    FindAndHealInjuredAlly(90.0f,50.0f);
}

void BattleBotAI::UpdateInCombatAI_Paladin()
{
    if (m_spells.paladin.pDivineShield &&
       (me->GetHealthPercent() < 20.0f) &&
       (me->GetPowerPercent(POWER_MANA) > 40.0f) &&
       !me->HasAura(AURA_WARSONG_FLAG) &&
        CanTryToCastSpell(me, m_spells.paladin.pDivineShield))
    {
        if (DoCastSpell(me, m_spells.paladin.pDivineShield) == SPELL_CAST_OK)
            return;
    }

    // Holy paladins heal before anything else.
    if (m_role == ROLE_HEALER && FindAndHealInjuredAlly(80.0f, 40.0f))
        return;

    bool const hasSeal = m_spells.paladin.pSeal && me->HasAura(m_spells.paladin.pSeal->Id);

    if (!hasSeal &&
        m_spells.paladin.pSeal &&
        CanTryToCastSpell(me, m_spells.paladin.pSeal))
    {
        me->CastSpell(me, m_spells.paladin.pSeal, false);
    }

    if (Unit* pVictim = me->GetVictim())
    {
        if (hasSeal && m_spells.paladin.pJudgement &&
            CanTryToCastSpell(pVictim, m_spells.paladin.pJudgement))
        {
            if (DoCastSpell(pVictim, m_spells.paladin.pJudgement) == SPELL_CAST_OK)
                return;
        }
        if (m_spells.paladin.pHammerOfJustice &&
            pVictim->IsNonMeleeSpellCasted() &&
            CanTryToCastSpell(pVictim, m_spells.paladin.pHammerOfJustice))
        {
            if (DoCastSpell(pVictim, m_spells.paladin.pHammerOfJustice) == SPELL_CAST_OK)
                return;
        }
        if (m_spells.paladin.pHammerOfWrath &&
            pVictim->GetHealthPercent() < 20.0f &&
            CanTryToCastSpell(pVictim, m_spells.paladin.pHammerOfWrath))
        {
            if (DoCastSpell(pVictim, m_spells.paladin.pHammerOfWrath) == SPELL_CAST_OK)
                return;
        }
        if (m_spells.paladin.pHolyShield &&
            CanTryToCastSpell(me, m_spells.paladin.pHolyShield) &&
           (IsMeleeDamageClass(pVictim->GetClass()) || (GetAttackersInRangeCount(8.0f) > 1)))
        {
            if (DoCastSpell(me, m_spells.paladin.pHolyShield) == SPELL_CAST_OK)
                return;
        }
        if (m_spells.paladin.pConsecration &&
           (GetAttackersInRangeCount(10.0f) > 2) &&
            CanTryToCastSpell(me, m_spells.paladin.pConsecration))
        {
            if (DoCastSpell(me, m_spells.paladin.pConsecration) == SPELL_CAST_OK)
                return;
        }
        if (m_spells.paladin.pHolyShock &&
            CanTryToCastSpell(pVictim, m_spells.paladin.pHolyShock))
        {
            if (m_spells.paladin.pDivineFavor &&
                CanTryToCastSpell(me, m_spells.paladin.pDivineFavor))
            {
                DoCastSpell(me, m_spells.paladin.pDivineFavor);
            }

            if (DoCastSpell(pVictim, m_spells.paladin.pHolyShock) == SPELL_CAST_OK)
                return;
        }
        if (m_spells.paladin.pExorcism &&
            pVictim->IsCreature() &&
           (pVictim->GetCreatureType() == CREATURE_TYPE_UNDEAD) &&
            CanTryToCastSpell(pVictim, m_spells.paladin.pExorcism))
        {
            if (DoCastSpell(pVictim, m_spells.paladin.pExorcism) == SPELL_CAST_OK)
                return;
        }
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE &&
           !me->CanReachWithMeleeAutoAttack(pVictim))
        {
            BeginChasing(pVictim);
        }
    }

    if (Unit* pFriend = me->FindLowestHpFriendlyUnit(30.0f, 70, true, me))
    {
        if (m_spells.paladin.pBlessingOfProtection &&
           !IsPhysicalDamageClass(pFriend->GetClass()) &&
           !pFriend->HasAura(AURA_WARSONG_FLAG) &&
            CanTryToCastSpell(pFriend, m_spells.paladin.pBlessingOfProtection))
        {
            if (DoCastSpell(pFriend, m_spells.paladin.pBlessingOfProtection) == SPELL_CAST_OK)
                return;
        }
        if (m_spells.paladin.pBlessingOfSacrifice &&
            pFriend->HasAura(AURA_WARSONG_FLAG) &&
            CanTryToCastSpell(pFriend, m_spells.paladin.pBlessingOfSacrifice))
        {
            if (DoCastSpell(pFriend, m_spells.paladin.pBlessingOfSacrifice) == SPELL_CAST_OK)
                return;
        }
        if (m_spells.paladin.pLayOnHands &&
           (pFriend->GetHealthPercent() < 15.0f) &&
            CanTryToCastSpell(pFriend, m_spells.paladin.pLayOnHands))
        {
            if (DoCastSpell(pFriend, m_spells.paladin.pLayOnHands) == SPELL_CAST_OK)
                return;
        }
    }

    if (m_spells.paladin.pBlessingOfFreedom &&
       (me->HasUnitState(UNIT_STATE_ROOT) || me->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED)) &&
        CanTryToCastSpell(me, m_spells.paladin.pBlessingOfFreedom))
    {
        if (DoCastSpell(me, m_spells.paladin.pBlessingOfFreedom) == SPELL_CAST_OK)
            return;
    }

    // Purify is the only dispel available before Cleanse is learned.
    if (SpellEntry const* pDispelSpell = m_spells.paladin.pCleanse ? m_spells.paladin.pCleanse : m_spells.paladin.pPurify)
    {
        if (Unit* pFriend = SelectDispelTarget(pDispelSpell))
        {
            if (CanTryToCastSpell(pFriend, pDispelSpell))
            {
                if (DoCastSpell(pFriend, pDispelSpell) == SPELL_CAST_OK)
                    return;
            }
        }
    }

    FindAndHealInjuredAlly(me->IsTotalImmune() ? 80.0f : 60.0f, 35.0f);
}

void BattleBotAI::UpdateOutOfCombatAI_Shaman()
{
    if (m_spells.shaman.pWeaponBuff &&
        CanTryToCastSpell(me, m_spells.shaman.pWeaponBuff))
    {
        if (CastWeaponBuff(m_spells.shaman.pWeaponBuff, EQUIPMENT_SLOT_MAINHAND) == SPELL_CAST_OK)
            return;
    }

    if (m_spells.shaman.pLightningShield &&
        CanTryToCastSpell(me, m_spells.shaman.pLightningShield))
    {
        if (DoCastSpell(me, m_spells.shaman.pLightningShield) == SPELL_CAST_OK)
            return;
    }

    if (me->GetVictim())
    {
        if (SummonShamanTotems())
            return;

        UpdateInCombatAI_Shaman();
    }
    else
    {
        if (m_spells.shaman.pGhostWolf &&
           !me->IsMoving() && !me->IsMounted() &&
           (!GetMountSpellId() || me->HasAura(AURA_WARSONG_FLAG) || me->HasAura(AURA_SILVERWING_FLAG)) &&
            CanTryToCastSpell(me, m_spells.shaman.pGhostWolf))
        {
            if (DoCastSpell(me, m_spells.shaman.pGhostWolf) == SPELL_CAST_OK)
                return;
        }
    }
}

void BattleBotAI::UpdateInCombatAI_Shaman()
{
    if (m_spells.shaman.pGhostWolf &&
        me->GetShapeshiftForm() == FORM_GHOSTWOLF)
        me->RemoveAurasDueToSpellByCancel(m_spells.shaman.pGhostWolf->Id);

    // Restoration shamans heal before anything else.
    if (m_role == ROLE_HEALER && FindAndHealInjuredAlly(80.0f, 40.0f))
        return;

    if (Unit* pVictim = me->GetVictim())
    {
        if (m_spells.shaman.pManaTideTotem &&
           (me->GetPowerPercent(POWER_MANA) < 50.0f) &&
            CanTryToCastSpell(me, m_spells.shaman.pManaTideTotem))
        {
            if (DoCastSpell(me, m_spells.shaman.pManaTideTotem) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.shaman.pElementalMastery &&
            me->GetAttackers().empty() &&
            CanTryToCastSpell(me, m_spells.shaman.pElementalMastery))
        {
            if (DoCastSpell(me, m_spells.shaman.pElementalMastery) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.shaman.pEarthShock &&
            pVictim->IsNonMeleeSpellCasted(false, false, true) &&
            CanTryToCastSpell(pVictim, m_spells.shaman.pEarthShock))
        {
            if (DoCastSpell(pVictim, m_spells.shaman.pEarthShock) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.shaman.pFrostShock &&
            pVictim->IsMoving() &&
            CanTryToCastSpell(pVictim, m_spells.shaman.pFrostShock))
        {
            if (DoCastSpell(pVictim, m_spells.shaman.pFrostShock) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.shaman.pStormstrike &&
            CanTryToCastSpell(pVictim, m_spells.shaman.pStormstrike))
        {
            if (DoCastSpell(pVictim, m_spells.shaman.pStormstrike) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.shaman.pChainLightning &&
            CanTryToCastSpell(pVictim, m_spells.shaman.pChainLightning))
        {
            if (DoCastSpell(pVictim, m_spells.shaman.pChainLightning) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.shaman.pPurge &&
            IsValidDispelTarget(pVictim, m_spells.shaman.pPurge) &&
            CanTryToCastSpell(pVictim, m_spells.shaman.pPurge))
        {
            if (DoCastSpell(pVictim, m_spells.shaman.pPurge) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.shaman.pFlameShock &&
            CanTryToCastSpell(pVictim, m_spells.shaman.pFlameShock))
        {
            if (DoCastSpell(pVictim, m_spells.shaman.pFlameShock) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.shaman.pLightningBolt &&
           !me->CanReachWithMeleeAutoAttack(pVictim) &&
            CanTryToCastSpell(pVictim, m_spells.shaman.pLightningBolt))
        {
            if (DoCastSpell(pVictim, m_spells.shaman.pLightningBolt) == SPELL_CAST_OK)
                return;
        }
    }

    if (SummonShamanTotems())
        return;

    if (m_spells.shaman.pCureDisease &&
        CanTryToCastSpell(me, m_spells.shaman.pCureDisease) &&
        IsValidDispelTarget(me, m_spells.shaman.pCureDisease))
    {
        if (DoCastSpell(me, m_spells.shaman.pCureDisease) == SPELL_CAST_OK)
            return;
    }

    if (m_spells.shaman.pCurePoison &&
        CanTryToCastSpell(me, m_spells.shaman.pCurePoison) &&
        IsValidDispelTarget(me, m_spells.shaman.pCurePoison))
    {
        if (DoCastSpell(me, m_spells.shaman.pCurePoison) == SPELL_CAST_OK)
            return;
    }

    FindAndHealInjuredAlly(50.0f,25.0f);
}

void BattleBotAI::UpdateOutOfCombatAI_Hunter()
{
    if (m_spells.hunter.pAspectOfTheCheetah &&
       !me->IsMounted() &&
        CanTryToCastSpell(me, m_spells.hunter.pAspectOfTheCheetah))
    {
        if (DoCastSpell(me, m_spells.hunter.pAspectOfTheCheetah) == SPELL_CAST_OK)
            return;
    }

    SummonPetIfNeeded();

    if (Unit* pVictim = me->GetVictim())
    {
        if (m_spells.hunter.pHuntersMark &&
            CanTryToCastSpell(pVictim, m_spells.hunter.pHuntersMark))
        {
            if (DoCastSpell(pVictim, m_spells.hunter.pHuntersMark) == SPELL_CAST_OK)
                return;
        }

        if (Pet* pPet = me->GetPet())
        {
            if (!pPet->GetVictim())
            {
                pPet->GetCharmInfo()->SetIsCommandAttack(true);
                pPet->AI()->AttackStart(pVictim);
            }
        }

        UpdateInCombatAI_Hunter();
    }
}

void BattleBotAI::UpdateInCombatAI_Hunter()
{
    if (Unit* pVictim = me->GetVictim())
    {
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE
            && me->GetDistance(pVictim) > 30.0f)
        {
            BeginChasing(pVictim);
        }

        if (me->HasSpell(BB_SPELL_AUTO_SHOT) &&
            !me->IsMoving() &&
            (me->GetCombatDistance(pVictim) > 8.0f) &&
            !me->IsNonMeleeSpellCasted())
        {
            switch (me->CastSpell(pVictim, BB_SPELL_AUTO_SHOT, false))
            {
                case SPELL_FAILED_NEED_AMMO:
                case SPELL_FAILED_NO_AMMO:
                {
                    AddHunterAmmo();
                    break;
                }
            }
        }

        bool const victimInMelee = pVictim->CanReachWithMeleeAutoAttack(me);

        if (m_spells.hunter.pDeterrence &&
            (me->GetHealthPercent() < 50.0f) &&
            GetAttackersInRangeCount(8.0f) &&
            CanTryToCastSpell(me, m_spells.hunter.pDeterrence))
        {
            if (DoCastSpell(me, m_spells.hunter.pDeterrence) == SPELL_CAST_OK)
                return;
        }

        // Tranquilizing Shot removes enrage / frenzy effects.
        if (m_spells.hunter.pTranquilizingShot &&
            pVictim->HasAuraType(SPELL_AURA_MOD_MELEE_HASTE) &&
            CanTryToCastSpell(pVictim, m_spells.hunter.pTranquilizingShot))
        {
            if (DoCastSpell(pVictim, m_spells.hunter.pTranquilizingShot) == SPELL_CAST_OK)
                return;
        }

        if (me->GetPet())
        {
            if (m_spells.hunter.pIntimidation && victimInMelee &&
                CanTryToCastSpell(pVictim, m_spells.hunter.pIntimidation))
            {
                if (DoCastSpell(pVictim, m_spells.hunter.pIntimidation) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.hunter.pBestialWrath &&
                CanTryToCastSpell(pVictim, m_spells.hunter.pBestialWrath))
            {
                if (DoCastSpell(pVictim, m_spells.hunter.pBestialWrath) == SPELL_CAST_OK)
                    return;
            }
        }

        if (m_spells.hunter.pRapidFire && !victimInMelee &&
            pVictim->GetHealthPercent() > 50.0f &&
            CanTryToCastSpell(me, m_spells.hunter.pRapidFire))
        {
            if (DoCastSpell(me, m_spells.hunter.pRapidFire) == SPELL_CAST_OK)
                return;
        }

        // Traps are only worth it when an enemy is on top of us.
        if (victimInMelee && !me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
        {
            std::vector<SpellEntry const*> vTraps;
            if (m_spells.hunter.pFrosTrap && !pVictim->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) &&
                CanTryToCastSpell(me, m_spells.hunter.pFrosTrap))
                vTraps.push_back(m_spells.hunter.pFrosTrap);
            if (m_spells.hunter.pImmolationTrap && CanTryToCastSpell(me, m_spells.hunter.pImmolationTrap))
                vTraps.push_back(m_spells.hunter.pImmolationTrap);
            if (m_spells.hunter.pExplosiveTrap && CanTryToCastSpell(me, m_spells.hunter.pExplosiveTrap))
                vTraps.push_back(m_spells.hunter.pExplosiveTrap);
            if (!vTraps.empty())
            {
                if (DoCastSpell(me, SelectRandomContainerElement(vTraps)) == SPELL_CAST_OK)
                    return;
            }
        }

        if (m_spells.hunter.pCounterattack && victimInMelee &&
            CanTryToCastSpell(pVictim, m_spells.hunter.pCounterattack))
        {
            if (DoCastSpell(pVictim, m_spells.hunter.pCounterattack) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.hunter.pVolley && !GetAttackersInRangeCount(8.0f) &&
            (me->GetEnemyCountInRadiusAround(pVictim, 10.0f) > 2) &&
            CanTryToCastSpell(pVictim, m_spells.hunter.pVolley))
        {
            if (DoCastSpell(pVictim, m_spells.hunter.pVolley) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.hunter.pConcussiveShot &&
            pVictim->IsMoving() && (pVictim->GetVictim() == me) &&
            CanTryToCastSpell(pVictim, m_spells.hunter.pConcussiveShot))
        {
            if (DoCastSpell(pVictim, m_spells.hunter.pConcussiveShot) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.hunter.pAimedShot &&
            CanTryToCastSpell(pVictim, m_spells.hunter.pAimedShot))
        {
            if (DoCastSpell(pVictim, m_spells.hunter.pAimedShot) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.hunter.pArcaneShot &&
            CanTryToCastSpell(pVictim, m_spells.hunter.pArcaneShot))
        {
            if (DoCastSpell(pVictim, m_spells.hunter.pArcaneShot) == SPELL_CAST_OK)
                return;
        }

        // Only one sting per hunter can be on a target: drain mana from casters, damage everyone else.
        SpellEntry const* pSting = (m_spells.hunter.pViperSting && pVictim->IsCaster() &&
                                    pVictim->GetPowerType() == POWER_MANA &&
                                    pVictim->GetPowerPercent(POWER_MANA) > 30.0f) ?
                                    m_spells.hunter.pViperSting : m_spells.hunter.pSerpentSting;
        if (pSting && CanTryToCastSpell(pVictim, pSting))
        {
            if (DoCastSpell(pVictim, pSting) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.hunter.pMultiShot &&
            CanTryToCastSpell(pVictim, m_spells.hunter.pMultiShot))
        {
            if (DoCastSpell(pVictim, m_spells.hunter.pMultiShot) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.hunter.pAspectOfTheCheetah &&
            me->HasAura(m_spells.hunter.pAspectOfTheCheetah->Id))
        {
            if (pVictim->CanReachWithMeleeAutoAttack(me))
            {
                if (m_spells.hunter.pAspectOfTheMonkey &&
                    CanTryToCastSpell(me, m_spells.hunter.pAspectOfTheMonkey))
                {
                    if (DoCastSpell(me, m_spells.hunter.pAspectOfTheMonkey) == SPELL_CAST_OK)
                        return;
                }
            }
            else
            {
                if (m_spells.hunter.pAspectOfTheHawk &&
                    CanTryToCastSpell(me, m_spells.hunter.pAspectOfTheHawk))
                {
                    if (DoCastSpell(me, m_spells.hunter.pAspectOfTheHawk) == SPELL_CAST_OK)
                        return;
                }
            }
        }

        if (pVictim->CanReachWithMeleeAutoAttack(me))
        {
            if (me->HasUnitState(UNIT_STATE_ROOT))
            {
                if (m_spells.hunter.pMongooseBite &&
                    CanTryToCastSpell(pVictim, m_spells.hunter.pMongooseBite))
                {
                    if (DoCastSpell(pVictim, m_spells.hunter.pMongooseBite) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.hunter.pRaptorStrike &&
                    CanTryToCastSpell(pVictim, m_spells.hunter.pRaptorStrike))
                {
                    if (DoCastSpell(pVictim, m_spells.hunter.pRaptorStrike) == SPELL_CAST_OK)
                        return;
                }
            }
            else
            {
                if (m_spells.hunter.pWingClip &&
                    CanTryToCastSpell(pVictim, m_spells.hunter.pWingClip))
                {
                    DoCastSpell(pVictim, m_spells.hunter.pWingClip);
                }
            }
        }

        if (!me->HasUnitState(UNIT_STATE_ROOT) &&
            (me->GetCombatDistance(pVictim) < 8.0f) &&
             me->GetMotionMaster()->GetCurrentMovementGeneratorType() != DISTANCING_MOTION_TYPE)
        {
            if (!me->IsStopped())
                me->StopMoving();
            me->GetMotionMaster()->Clear();
            if (me->GetMotionMaster()->MoveDistance(pVictim, 25.0f))
                return;
        }
    }
}

void BattleBotAI::UpdateOutOfCombatAI_Mage()
{
    if (m_spells.mage.pArcaneBrilliance)
    {
        if (CanTryToCastSpell(me, m_spells.mage.pArcaneBrilliance))
        {
            if (DoCastSpell(me, m_spells.mage.pArcaneBrilliance) == SPELL_CAST_OK)
                return;
        }
    }
    else if (m_spells.mage.pArcaneIntellect)
    {
        if (CanTryToCastSpell(me, m_spells.mage.pArcaneIntellect))
        {
            if (DoCastSpell(me, m_spells.mage.pArcaneIntellect) == SPELL_CAST_OK)
                return;
        }
    }

    if (m_spells.mage.pIceArmor &&
        CanTryToCastSpell(me, m_spells.mage.pIceArmor))
    {
        if (DoCastSpell(me, m_spells.mage.pIceArmor) == SPELL_CAST_OK)
            return;
    }

    if (m_spells.mage.pIceBarrier &&
        CanTryToCastSpell(me, m_spells.mage.pIceBarrier))
    {
        if (DoCastSpell(me, m_spells.mage.pIceBarrier) == SPELL_CAST_OK)
            return;
    }

    if (me->GetVictim())
        UpdateInCombatAI_Mage();
}

void BattleBotAI::UpdateInCombatAI_Mage()
{
    if (Unit* pVictim = me->GetVictim())
    {
        if (m_spells.mage.pCombustion &&
            CanTryToCastSpell(me, m_spells.mage.pCombustion))
        {
            if (DoCastSpell(me, m_spells.mage.pCombustion) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.mage.pPyroblast &&
            m_spells.mage.pPresenceOfMind &&
            me->HasAura(m_spells.mage.pPresenceOfMind->Id) &&
            CanTryToCastSpell(pVictim, m_spells.mage.pPyroblast))
        {
            if (DoCastSpell(pVictim, m_spells.mage.pPyroblast) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.mage.pIceBlock &&
           (me->GetHealthPercent() < 10.0f) &&
            CanTryToCastSpell(me, m_spells.mage.pIceBlock))
        {
            if (DoCastSpell(me, m_spells.mage.pIceBlock) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.mage.pManaShield &&
            IsPhysicalDamageClass(pVictim->GetClass()) &&
           (me->GetPowerPercent(POWER_MANA) > 20.0f) &&
            CanTryToCastSpell(me, m_spells.mage.pManaShield))
        {
            if (DoCastSpell(me, m_spells.mage.pManaShield) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.mage.pCounterspell &&
            pVictim->IsNonMeleeSpellCasted(false, false, true) &&
            CanTryToCastSpell(pVictim, m_spells.mage.pCounterspell))
        {
            if (DoCastSpell(pVictim, m_spells.mage.pCounterspell) == SPELL_CAST_OK)
                return;
        }

        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE
            && me->GetDistance(pVictim) > 30.0f)
        {
            BeginChasing(pVictim);
        }
        else if (pVictim->CanReachWithMeleeAutoAttack(me) &&
                (pVictim->GetVictim() == me) &&
                (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != DISTANCING_MOTION_TYPE))
        {
            if (m_spells.mage.pConeofCold &&
                CanTryToCastSpell(me, m_spells.mage.pConeofCold))
            {
                if (DoCastSpell(pVictim, m_spells.mage.pConeofCold) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.mage.pBlink &&
               (me->HasUnitState(UNIT_STATE_CAN_NOT_MOVE) ||
                me->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED)) &&
                CanTryToCastSpell(me, m_spells.mage.pBlink))
            {
                if (me->GetMotionMaster()->GetCurrentMovementGeneratorType())
                    me->GetMotionMaster()->MoveIdle();

                if (DoCastSpell(me, m_spells.mage.pBlink) == SPELL_CAST_OK)
                    return;
            }

            if (!me->HasUnitState(UNIT_STATE_CAN_NOT_MOVE))
            {
                if (m_spells.mage.pFrostNova &&
                    !pVictim->HasUnitState(UNIT_STATE_ROOT) &&
                    !pVictim->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL) &&
                    CanTryToCastSpell(me, m_spells.mage.pFrostNova))
                {
                    DoCastSpell(me, m_spells.mage.pFrostNova);
                }

                if (me->GetMotionMaster()->MoveDistance(pVictim, 25.0f))
                    return;
            }
        }

        if (GetAttackersInRangeCount(10.0f) > 1)
        {
            if (m_spells.mage.pBlastWave &&
                CanTryToCastSpell(me, m_spells.mage.pBlastWave))
            {
                if (DoCastSpell(me, m_spells.mage.pBlastWave) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.mage.pArcaneExplosion &&
                CanTryToCastSpell(me, m_spells.mage.pArcaneExplosion))
            {
                if (DoCastSpell(me, m_spells.mage.pArcaneExplosion) == SPELL_CAST_OK)
                    return;
            }
        }

        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == DISTANCING_MOTION_TYPE)
            return;

        if (m_spells.mage.pRemoveLesserCurse &&
           (me->GetAttackers().size() < 3) &&
            CanTryToCastSpell(me, m_spells.mage.pRemoveLesserCurse) &&
            IsValidDispelTarget(me, m_spells.mage.pRemoveLesserCurse))
        {
            if (DoCastSpell(me, m_spells.mage.pRemoveLesserCurse) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.mage.pPolymorph)
        {
            if (Unit* pTarget = SelectAttackerDifferentFrom(pVictim))
            {
                if (CanTryToCastSpell(pVictim, m_spells.mage.pPolymorph))
                {
                    if (DoCastSpell(pVictim, m_spells.mage.pPolymorph) == SPELL_CAST_OK)
                        return;
                }
            }
        }

        if (m_spells.mage.pArcanePower &&
           (me->GetPowerPercent(POWER_MANA) > 50.0f) &&
            CanTryToCastSpell(me, m_spells.mage.pArcanePower))
        {
            if (DoCastSpell(me, m_spells.mage.pArcanePower) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.mage.pPresenceOfMind &&
           (me->GetPowerPercent(POWER_MANA) > 50.0f) &&
            CanTryToCastSpell(me, m_spells.mage.pPresenceOfMind))
        {
            if (DoCastSpell(me, m_spells.mage.pPresenceOfMind) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.mage.pFlamestrike && !GetAttackersInRangeCount(10.0f) &&
           (me->GetEnemyCountInRadiusAround(pVictim, 8.0f) > 2) &&
            CanTryToCastSpell(pVictim, m_spells.mage.pFlamestrike))
        {
            if (DoCastSpell(pVictim, m_spells.mage.pFlamestrike) == SPELL_CAST_OK)
                return;
        }

        // Arcane Missiles is a channel: only when nobody is hitting us, as main filler for arcane mages
        // or when no other bolt is available yet.
        if (m_spells.mage.pArcaneMissiles && !GetAttackersInRangeCount(10.0f) &&
           (m_spells.mage.pImprovedArcaneMissiles || (!m_spells.mage.pFrostbolt && !m_spells.mage.pFireball)) &&
            CanTryToCastSpell(pVictim, m_spells.mage.pArcaneMissiles))
        {
            if (DoCastSpell(pVictim, m_spells.mage.pArcaneMissiles) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.mage.pScorch &&
           (pVictim->GetHealthPercent() < 20.0f) &&
            CanTryToCastSpell(pVictim, m_spells.mage.pScorch))
        {
            if (DoCastSpell(pVictim, m_spells.mage.pScorch) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.mage.pFrostbolt &&
            CanTryToCastSpell(pVictim, m_spells.mage.pFrostbolt))
        {
            if (DoCastSpell(pVictim, m_spells.mage.pFrostbolt) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.mage.pFireBlast &&
            CanTryToCastSpell(pVictim, m_spells.mage.pFireBlast))
        {
            if (DoCastSpell(pVictim, m_spells.mage.pFireBlast) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.mage.pFireball &&
            CanTryToCastSpell(pVictim, m_spells.mage.pFireball))
        {
            if (DoCastSpell(pVictim, m_spells.mage.pFireball) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.mage.pEvocation &&
           (me->GetPowerPercent(POWER_MANA) < 30.0f) &&
           (GetAttackersInRangeCount(10.0f) == 0) &&
            CanTryToCastSpell(me, m_spells.mage.pEvocation))
        {
            if (DoCastSpell(me, m_spells.mage.pEvocation) == SPELL_CAST_OK)
                return;
        }

        if (me->HasSpell(BB_SPELL_SHOOT_WAND) &&
           !me->IsMoving() &&
           (me->GetPowerPercent(POWER_MANA) < 5.0f) &&
           !me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
            me->CastSpell(pVictim, BB_SPELL_SHOOT_WAND, false);
    }
}

void BattleBotAI::UpdateOutOfCombatAI_Priest()
{
    BattleGround* bg = me->GetBattleGround();
    if (bg && bg->GetStatus() == STATUS_WAIT_JOIN)
    {
        if (m_spells.priest.pPrayerofFortitude)
        {
            if (Player* pTarget = SelectBuffTarget(m_spells.priest.pPrayerofFortitude))
            {
                if (CanTryToCastSpell(pTarget, m_spells.priest.pPrayerofFortitude))
                {
                    if (DoCastSpell(pTarget, m_spells.priest.pPrayerofFortitude) == SPELL_CAST_OK)
                    {
                        m_isBuffing = true;
                        return;
                    }
                }
            }
        }

        if (m_spells.priest.pPrayerofSpirit)
        {
            if (Player* pTarget = SelectBuffTarget(m_spells.priest.pPrayerofSpirit))
            {
                if (CanTryToCastSpell(pTarget, m_spells.priest.pPrayerofSpirit))
                {
                    if (DoCastSpell(pTarget, m_spells.priest.pPrayerofSpirit) == SPELL_CAST_OK)
                    {
                        m_isBuffing = true;
                        return;
                    }
                }
            }
        }

        if (m_spells.priest.pShadowProtection)
        {
            if (Player* pTarget = SelectBuffTarget(m_spells.priest.pShadowProtection))
            {
                if (CanTryToCastSpell(pTarget, m_spells.priest.pShadowProtection))
                {
                    if (DoCastSpell(pTarget, m_spells.priest.pShadowProtection) == SPELL_CAST_OK)
                    {
                        m_isBuffing = true;
                        return;
                    }
                }
            }
        }
    }
    else if (bg && bg->GetStatus() == STATUS_IN_PROGRESS)
    {
        if (m_spells.priest.pPowerWordFortitude &&
            IsValidBuffTarget(me, m_spells.priest.pPowerWordFortitude) &&
            CanTryToCastSpell(me, m_spells.priest.pPowerWordFortitude))
        {
            if (DoCastSpell(me, m_spells.priest.pPowerWordFortitude) == SPELL_CAST_OK)
            {
                m_isBuffing = true;
                return;
            }
        }

        if (m_spells.priest.pDivineSpirit &&
            IsValidBuffTarget(me, m_spells.priest.pDivineSpirit) &&
            CanTryToCastSpell(me, m_spells.priest.pDivineSpirit))
        {
            if (DoCastSpell(me, m_spells.priest.pDivineSpirit) == SPELL_CAST_OK)
            {
                m_isBuffing = true;
                return;
            }
        }
    }

    if (m_spells.priest.pInnerFire &&
        CanTryToCastSpell(me, m_spells.priest.pInnerFire))
    {
        if (DoCastSpell(me, m_spells.priest.pInnerFire) == SPELL_CAST_OK)
        {
            m_isBuffing = true;
            return;
        }
    }

    if (m_isBuffing &&
       (!m_spells.priest.pPowerWordFortitude ||
        !me->HasGCD(m_spells.priest.pPowerWordFortitude)))
    {
        m_isBuffing = false;
    }

    if (me->GetVictim())
        UpdateInCombatAI_Priest();
}

void BattleBotAI::UpdateInCombatAI_Priest()
{
    if (m_spells.priest.pPowerWordShield &&
        CanTryToCastSpell(me, m_spells.priest.pPowerWordShield))
    {
        if (DoCastSpell(me, m_spells.priest.pPowerWordShield) == SPELL_CAST_OK)
            return;
    }

    if (m_spells.priest.pInnerFocus &&
       (me->GetPowerPercent(POWER_MANA) < 50.0f) &&
        CanTryToCastSpell(me, m_spells.priest.pInnerFocus))
    {
        DoCastSpell(me, m_spells.priest.pInnerFocus);
    }

    if (m_spells.priest.pTouchOfWeakness &&
        CanTryToCastSpell(me, m_spells.priest.pTouchOfWeakness))
    {
        if (DoCastSpell(me, m_spells.priest.pTouchOfWeakness) == SPELL_CAST_OK)
            return;
    }

    // Shield the flag carrier or allies under attack.
    if (m_role == ROLE_HEALER && m_spells.priest.pPowerWordShield)
    {
        if (Player* pShieldTarget = SelectShieldTarget())
        {
            if (CanTryToCastSpell(pShieldTarget, m_spells.priest.pPowerWordShield) &&
                DoCastSpell(pShieldTarget, m_spells.priest.pPowerWordShield) == SPELL_CAST_OK)
                return;
        }
    }

    // Group heal
    if (m_role == ROLE_HEALER && me->GetShapeshiftForm() == FORM_NONE &&
        m_spells.priest.pPrayerofHealing &&
        GetAlliesNeedingHealCount(20.0f, 70.0f) >= 3 &&
        CanTryToCastSpell(me, m_spells.priest.pPrayerofHealing))
    {
        if (DoCastSpell(me, m_spells.priest.pPrayerofHealing) == SPELL_CAST_OK)
            return;
    }

    // Heal
    if (me->GetShapeshiftForm() == FORM_NONE &&
        (m_role == ROLE_HEALER ? FindAndHealInjuredAlly(80.0f, 40.0f) : FindAndHealInjuredAlly(70.0f, 35.0f)))
        return;

    // Dispels
    if (m_spells.priest.pDispelMagic)
    {
        if (m_role == ROLE_HEALER)
        {
            if (Unit* pFriend = SelectDispelTarget(m_spells.priest.pDispelMagic))
            {
                if (CanTryToCastSpell(pFriend, m_spells.priest.pDispelMagic))
                {
                    if (DoCastSpell(pFriend, m_spells.priest.pDispelMagic) == SPELL_CAST_OK)
                        return;
                }
            }
        }
        else if (IsValidDispelTarget(me, m_spells.priest.pDispelMagic) &&
                 CanTryToCastSpell(me, m_spells.priest.pDispelMagic))
        {
            if (DoCastSpell(me, m_spells.priest.pDispelMagic) == SPELL_CAST_OK)
                return;
        }
    }
    if (m_spells.priest.pAbolishDisease)
    {
        if (m_role == ROLE_HEALER)
        {
            if (Unit* pFriend = SelectDispelTarget(m_spells.priest.pAbolishDisease))
            {
                if (CanTryToCastSpell(pFriend, m_spells.priest.pAbolishDisease))
                {
                    if (DoCastSpell(pFriend, m_spells.priest.pAbolishDisease) == SPELL_CAST_OK)
                        return;
                }
            }
        }
        else if (IsValidDispelTarget(me, m_spells.priest.pAbolishDisease) &&
                 CanTryToCastSpell(me, m_spells.priest.pAbolishDisease))
        {
            if (DoCastSpell(me, m_spells.priest.pAbolishDisease) == SPELL_CAST_OK)
                return;
        }
    }

    // Attack
    if (Unit* pVictim = me->GetVictim())
    {
        if (m_spells.priest.pShadowform &&
            CanTryToCastSpell(me, m_spells.priest.pShadowform))
        {
            if (DoCastSpell(me, m_spells.priest.pShadowform) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.priest.pSilence &&
            pVictim->IsNonMeleeSpellCasted() &&
            CanTryToCastSpell(pVictim, m_spells.priest.pSilence))
        {
            if (DoCastSpell(pVictim, m_spells.priest.pSilence) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.priest.pVampiricEmbrace &&
            CanTryToCastSpell(pVictim, m_spells.priest.pVampiricEmbrace))
        {
            if (DoCastSpell(pVictim, m_spells.priest.pVampiricEmbrace) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.priest.pMindBlast &&
            CanTryToCastSpell(pVictim, m_spells.priest.pMindBlast))
        {
            if (DoCastSpell(pVictim, m_spells.priest.pMindBlast) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.priest.pShadowWordPain &&
            CanTryToCastSpell(pVictim, m_spells.priest.pShadowWordPain))
        {
            if (DoCastSpell(pVictim, m_spells.priest.pShadowWordPain) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.priest.pDevouringPlague &&
            CanTryToCastSpell(pVictim, m_spells.priest.pDevouringPlague))
        {
            if (DoCastSpell(pVictim, m_spells.priest.pDevouringPlague) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.priest.pPsychicScream &&
            GetAttackersInRangeCount(10.0f) &&
            CanTryToCastSpell(me, m_spells.priest.pPsychicScream))
        {
            if (DoCastSpell(me, m_spells.priest.pPsychicScream) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.priest.pManaBurn &&
           (pVictim->GetPowerType() == POWER_MANA) &&
            CanTryToCastSpell(pVictim, m_spells.priest.pManaBurn))
        {
            if (DoCastSpell(pVictim, m_spells.priest.pManaBurn) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.priest.pMindFlay &&
           (!GetAttackersInRangeCount(10.0f) || me->HasAuraType(SPELL_AURA_SCHOOL_ABSORB)) &&
            CanTryToCastSpell(pVictim, m_spells.priest.pMindFlay))
        {
            if (DoCastSpell(pVictim, m_spells.priest.pMindFlay) == SPELL_CAST_OK)
                return;
        }

        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE
            && me->GetDistance(pVictim) > 30.0f)
        {
            BeginChasing(pVictim);
        }

        if (me->GetShapeshiftForm() == FORM_NONE)
        {
            if (m_spells.priest.pHolyNova &&
                GetAttackersInRangeCount(10.0f) > 2 &&
                CanTryToCastSpell(me, m_spells.priest.pHolyNova))
            {
                if (DoCastSpell(me, m_spells.priest.pHolyNova) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.priest.pSmite &&
                CanTryToCastSpell(pVictim, m_spells.priest.pSmite))
            {
                if (DoCastSpell(pVictim, m_spells.priest.pSmite) == SPELL_CAST_OK)
                    return;
            }
        }

        if (me->HasSpell(BB_SPELL_SHOOT_WAND) &&
           !me->IsMoving() &&
           (me->GetPowerPercent(POWER_MANA) < 5.0f) &&
           !me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
            me->CastSpell(pVictim, BB_SPELL_SHOOT_WAND, false);
    }

}

void BattleBotAI::UpdateOutOfCombatAI_Warlock()
{
    BattleGround* bg = me->GetBattleGround();
    if (bg && bg->GetStatus() == STATUS_WAIT_JOIN)
    {
        if (m_spells.warlock.pDetectInvisibility)
        {
            if (Player* pTarget = SelectBuffTarget(m_spells.warlock.pDetectInvisibility))
            {
                if (CanTryToCastSpell(pTarget, m_spells.warlock.pDetectInvisibility))
                {
                    if (DoCastSpell(pTarget, m_spells.warlock.pDetectInvisibility) == SPELL_CAST_OK)
                    {
                        m_isBuffing = true;
                        return;
                    }
                }
            }
        }
    }

    // Demon Skin is the armor buff before Demon Armor is learned.
    if (SpellEntry const* pArmorSpell = m_spells.warlock.pDemonArmor ? m_spells.warlock.pDemonArmor : m_spells.warlock.pDemonSkin)
    {
        if (CanTryToCastSpell(me, pArmorSpell))
        {
            if (DoCastSpell(me, pArmorSpell) == SPELL_CAST_OK)
            {
                m_isBuffing = true;
                return;
            }
        }
    }

    if (m_isBuffing &&
       (!m_spells.warlock.pDetectInvisibility ||
        !me->HasGCD(m_spells.warlock.pDetectInvisibility)))
    {
        m_isBuffing = false;
    }

    if (Unit* pVictim = me->GetVictim())
    {
        if (Pet* pPet = me->GetPet())
        {
            if (!pPet->GetVictim())
            {
                pPet->GetCharmInfo()->SetIsCommandAttack(true);
                pPet->AI()->AttackStart(pVictim);
            }
        }

        UpdateInCombatAI_Warlock();
    }
    else
        SummonPetIfNeeded();
}

void BattleBotAI::UpdateInCombatAI_Warlock()
{
    if (Unit* pVictim = me->GetVictim())
    {
        if (m_spells.warlock.pDeathCoil &&
           (pVictim->CanReachWithMeleeAutoAttack(me) || pVictim->IsNonMeleeSpellCasted()) &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pDeathCoil))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pDeathCoil) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warlock.pShadowburn &&
           (pVictim->GetHealthPercent() < 10.0f) &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pShadowburn))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pShadowburn) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warlock.pSearingPain &&
           (pVictim->GetHealthPercent() < 20.0f) &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pSearingPain))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pSearingPain) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warlock.pShadowWard &&
           (pVictim->GetClass() == CLASS_WARLOCK) &&
            CanTryToCastSpell(me, m_spells.warlock.pShadowWard))
        {
            if (DoCastSpell(me, m_spells.warlock.pShadowWard) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warlock.pDemonicSacrifice)
        {
            if (Pet* pPet = me->GetPet())
            {
                if (pPet->IsAlive() &&
                    CanTryToCastSpell(pPet, m_spells.warlock.pDemonicSacrifice))
                {
                    if (DoCastSpell(pPet, m_spells.warlock.pDemonicSacrifice) == SPELL_CAST_OK)
                        return;
                }
            }
        }

        if (m_spells.warlock.pImmolate &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pImmolate))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pImmolate) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warlock.pConflagrate &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pConflagrate))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pConflagrate) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warlock.pCorruption &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pCorruption))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pCorruption) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warlock.pSiphonLife &&
           (me->GetHealthPercent() < 80.0f) &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pSiphonLife))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pSiphonLife) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warlock.pDrainLife &&
           (me->GetHealthPercent() < 30.0f) &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pDrainLife))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pDrainLife) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warlock.pFear &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pFear))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pFear) == SPELL_CAST_OK)
                return;
        }

        if (pVictim->IsCaster())
        {
            if (m_spells.warlock.pCurseofTongues &&
                CanTryToCastSpell(pVictim, m_spells.warlock.pCurseofTongues))
            {
                if (DoCastSpell(pVictim, m_spells.warlock.pCurseofTongues) == SPELL_CAST_OK)
                    return;
            }
        }
        else
        {
            if (m_spells.warlock.pCurseofExhaustion &&
                CanTryToCastSpell(pVictim, m_spells.warlock.pCurseofExhaustion))
            {
                if (DoCastSpell(pVictim, m_spells.warlock.pCurseofExhaustion) == SPELL_CAST_OK)
                    return;
            }
        }

        // Curse of Agony only while no better curse is available, to not replace Tongues / Exhaustion.
        if (m_spells.warlock.pCurseofAgony &&
           !m_spells.warlock.pCurseofTongues && !m_spells.warlock.pCurseofExhaustion &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pCurseofAgony))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pCurseofAgony) == SPELL_CAST_OK)
                return;
        }

        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE
            && me->GetDistance(pVictim) > 30.0f)
        {
            BeginChasing(pVictim);
        }

        if (m_spells.warlock.pHowlofTerror &&
            GetAttackersInRangeCount(10.0f) > 1 &&
            CanTryToCastSpell(me, m_spells.warlock.pHowlofTerror))
        {
            if (DoCastSpell(me, m_spells.warlock.pHowlofTerror) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warlock.pShadowBolt &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pShadowBolt))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pShadowBolt) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warlock.pLifeTap &&
           (me->GetPowerPercent(POWER_MANA) < 10.0f) &&
           (me->GetHealthPercent() > 70.0f) &&
            CanTryToCastSpell(me, m_spells.warlock.pLifeTap))
        {
            if (DoCastSpell(me, m_spells.warlock.pLifeTap) == SPELL_CAST_OK)
                return;
        }

        if (me->HasSpell(BB_SPELL_SHOOT_WAND) &&
           !me->IsMoving() &&
           (me->GetPowerPercent(POWER_MANA) < 5.0f) &&
           !me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
            me->CastSpell(pVictim, BB_SPELL_SHOOT_WAND, false);
    }
}

void BattleBotAI::UpdateOutOfCombatAI_Warrior()
{
    if (m_spells.warrior.pBattleStance &&
        CanTryToCastSpell(me, m_spells.warrior.pBattleStance))
    {
        if (DoCastSpell(me, m_spells.warrior.pBattleStance) == SPELL_CAST_OK)
            return;
    }

    if (m_spells.warrior.pBattleShout &&
       !me->HasAura(m_spells.warrior.pBattleShout->Id))
    {
        if (CanTryToCastSpell(me, m_spells.warrior.pBattleShout))
            DoCastSpell(me, m_spells.warrior.pBattleShout);
        else if (m_spells.warrior.pBloodrage &&
            (me->GetPower(POWER_RAGE) < 10) &&
            CanTryToCastSpell(me, m_spells.warrior.pBloodrage))
        {
            DoCastSpell(me, m_spells.warrior.pBloodrage);
        }
    }

    if (Unit* pVictim = me->GetVictim())
    {
        if (m_spells.warrior.pCharge &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pCharge))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pCharge) == SPELL_CAST_OK)
                return;
        }
    }
}

void BattleBotAI::UpdateInCombatAI_Warrior()
{
    if (Unit* pVictim = me->GetVictim())
    {
        if (pVictim->IsNonMeleeSpellCasted(false, false, true))
        {
            if (m_spells.warrior.pPummel &&
                CanTryToCastSpell(pVictim, m_spells.warrior.pPummel))
            {
                if (DoCastSpell(pVictim, m_spells.warrior.pPummel) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.warrior.pShieldBash &&
                IsWearingShield(me) &&
                CanTryToCastSpell(pVictim, m_spells.warrior.pShieldBash))
            {
                if (DoCastSpell(pVictim, m_spells.warrior.pShieldBash) == SPELL_CAST_OK)
                    return;
            }
        }

        if (m_spells.warrior.pExecute &&
           (pVictim->GetHealthPercent() < 20.0f) &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pExecute))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pExecute) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pRevenge &&
            me->GetShapeshiftForm() == FORM_DEFENSIVESTANCE &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pRevenge))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pRevenge) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pSweepingStrikes &&
           (me->GetEnemyCountInRadiusAround(pVictim, 10.0f) > 1) &&
            CanTryToCastSpell(me, m_spells.warrior.pSweepingStrikes))
        {
            if (DoCastSpell(me, m_spells.warrior.pSweepingStrikes) == SPELL_CAST_OK)
                return;
        }

        if (pVictim->CanReachWithMeleeAutoAttack(me))
        {
            // Slows melee enemies and hits everyone around.
            if (m_spells.warrior.pThunderClap &&
               !pVictim->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) &&
                CanTryToCastSpell(pVictim, m_spells.warrior.pThunderClap))
            {
                if (DoCastSpell(pVictim, m_spells.warrior.pThunderClap) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.warrior.pDemoralizingShout &&
                IsMeleeDamageClass(pVictim->GetClass()) &&
               (me->GetPower(POWER_RAGE) >= 25) &&
                CanTryToCastSpell(pVictim, m_spells.warrior.pDemoralizingShout))
            {
                if (DoCastSpell(pVictim, m_spells.warrior.pDemoralizingShout) == SPELL_CAST_OK)
                    return;
            }
        }

        if (m_spells.warrior.pOverpower &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pOverpower))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pOverpower) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pLastStand &&
            me->GetHealthPercent() < 20.0f &&
            CanTryToCastSpell(me, m_spells.warrior.pLastStand))
        {
            if (DoCastSpell(me, m_spells.warrior.pLastStand) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pConcussionBlow &&
           (pVictim->IsNonMeleeSpellCasted() || pVictim->IsMoving() || (me->GetHealthPercent() < 50.0f)) &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pConcussionBlow))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pConcussionBlow) == SPELL_CAST_OK)
                return;
        }

        if (me->GetShapeshiftForm() == FORM_DEFENSIVESTANCE &&
            IsWearingShield(me))
        {
            if (!me->GetAttackers().empty() &&
                IsPhysicalDamageClass(pVictim->GetClass()))
            {
                if (m_spells.warrior.pShieldBlock &&
                    CanTryToCastSpell(me, m_spells.warrior.pShieldBlock))
                {
                    if (DoCastSpell(me, m_spells.warrior.pShieldBlock) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.warrior.pShieldWall &&
                    (me->GetHealthPercent() < 40.0f) &&
                    CanTryToCastSpell(me, m_spells.warrior.pShieldWall))
                {
                    if (DoCastSpell(me, m_spells.warrior.pShieldWall) == SPELL_CAST_OK)
                        return;
                }
            }

            if (m_spells.warrior.pShieldSlam &&
                CanTryToCastSpell(me, m_spells.warrior.pShieldSlam))
            {
                if (DoCastSpell(me, m_spells.warrior.pShieldSlam) == SPELL_CAST_OK)
                    return;
            }
        }

        if (pVictim->IsMoving() &&
           !pVictim->HasUnitState(UNIT_STATE_ROOT) &&
           !pVictim->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED))
        {
            if (m_spells.warrior.pHamstring &&
                CanTryToCastSpell(pVictim, m_spells.warrior.pHamstring))
            {
                if (DoCastSpell(pVictim, m_spells.warrior.pHamstring) == SPELL_CAST_OK)
                    return;
            }
            if (m_spells.warrior.pPiercingHowl &&
               (me->GetCombatDistance(pVictim) <= 10.0f) &&
                CanTryToCastSpell(pVictim, m_spells.warrior.pPiercingHowl))
            {
                if (DoCastSpell(pVictim, m_spells.warrior.pPiercingHowl) == SPELL_CAST_OK)
                    return;
            }
        }

        if (m_spells.warrior.pRend &&
           (pVictim->GetClass() == CLASS_ROGUE) &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pRend))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pRend) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pIntimidatingShout &&
           (me->GetHealthPercent() < 50.0f) &&
           (GetAttackersInRangeCount(10.0f) > 2) &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pIntimidatingShout))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pIntimidatingShout) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pRetaliation &&
            IsMeleeDamageClass(pVictim->GetClass()) &&
           (me->GetHealthPercent() > 70.0f) &&
           ((GetAttackersInRangeCount(10.0f) > 1) || (pVictim->GetClass() == CLASS_ROGUE)) &&
            CanTryToCastSpell(me, m_spells.warrior.pRetaliation))
        {
            if (DoCastSpell(me, m_spells.warrior.pRetaliation) == SPELL_CAST_OK)
                return;
        }

        if ((me->GetHealthPercent() > 60.0f) && (pVictim->GetHealthPercent() > 40.0f) &&
            (pVictim->GetClass() == CLASS_WARLOCK || pVictim->GetClass() == CLASS_PRIEST) &&
            !me->HasUnitState(UNIT_STATE_ROOT) &&
            !me->IsImmuneToMechanic(MECHANIC_FEAR))
        {
            if (m_spells.warrior.pRecklessness &&
                CanTryToCastSpell(me, m_spells.warrior.pRecklessness))
            {
                if (DoCastSpell(me, m_spells.warrior.pRecklessness) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.warrior.pDeathWish &&
                CanTryToCastSpell(me, m_spells.warrior.pDeathWish))
            {
                if (DoCastSpell(me, m_spells.warrior.pDeathWish) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.warrior.pBerserkerRage &&
                CanTryToCastSpell(me, m_spells.warrior.pBerserkerRage))
            {
                if (DoCastSpell(me, m_spells.warrior.pBerserkerRage) == SPELL_CAST_OK)
                    return;
            }
        }

        if (m_spells.warrior.pMortalStrike &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pMortalStrike))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pMortalStrike) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pBloodthirst &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pBloodthirst))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pBloodthirst) == SPELL_CAST_OK)
                return;
        }

        if (me->GetHealthPercent() < 20.0f)
        {
            if (m_spells.warrior.pDefensiveStance &&
                CanTryToCastSpell(me, m_spells.warrior.pDefensiveStance))
            {
                DoCastSpell(me, m_spells.warrior.pDefensiveStance);
            }
        }
        else
        {
            if (m_spells.warrior.pBerserkerStance &&
               (pVictim->GetClass() != CLASS_ROGUE) &&
                CanTryToCastSpell(me, m_spells.warrior.pBerserkerStance))
            {
                DoCastSpell(me, m_spells.warrior.pBerserkerStance);
            }
        }

        if (m_spells.warrior.pIntercept &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pIntercept))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pIntercept) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pWhirlwind &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pWhirlwind))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pWhirlwind) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pDisarm &&
            IsMeleeWeaponClass(pVictim->GetClass()) &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pDisarm))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pDisarm) == SPELL_CAST_OK)
                return;
        }

        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE
            && !me->CanReachWithMeleeAutoAttack(pVictim))
        {
            BeginChasing(pVictim);
        }

        if (m_spells.warrior.pCleave &&
           (me->GetPower(POWER_RAGE) > 30) &&
           (me->GetEnemyCountInRadiusAround(pVictim, 8.0f) > 1) &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pCleave))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pCleave) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pHeroicStrike &&
           (me->GetPower(POWER_RAGE) > 30) &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pHeroicStrike))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pHeroicStrike) == SPELL_CAST_OK)
                return;
        }
    }
    else // no victim
    {
        if (m_spells.warrior.pBattleShout &&
            CanTryToCastSpell(me, m_spells.warrior.pBattleShout))
        {
            if (DoCastSpell(me, m_spells.warrior.pBattleShout) == SPELL_CAST_OK)
                return;
        }
    }
}

void BattleBotAI::UpdateOutOfCombatAI_Rogue()
{
    if (m_spells.rogue.pMainHandPoison &&
        CanTryToCastSpell(me, m_spells.rogue.pMainHandPoison))
    {
        if (CastWeaponBuff(m_spells.rogue.pMainHandPoison, EQUIPMENT_SLOT_MAINHAND) == SPELL_CAST_OK)
            return;
    }

    if (m_spells.rogue.pOffHandPoison &&
        CanTryToCastSpell(me, m_spells.rogue.pOffHandPoison))
    {
        if (CastWeaponBuff(m_spells.rogue.pOffHandPoison, EQUIPMENT_SLOT_OFFHAND) == SPELL_CAST_OK)
            return;
    }

    // Stealth slows the rogue down a lot, so only use it when an enemy is around: enter with an enemy
    // within 80 yards and leave it when there is none within 100 (the gap avoids toggling at the limit).
    static float const STEALTH_ENTER_DISTANCE = 80.0f;
    static float const STEALTH_LEAVE_DISTANCE = 100.0f;
    bool const stealthed = me->HasAuraType(SPELL_AURA_MOD_STEALTH);
    if (stealthed)
    {
        if (!me->GetVictim() && !HasHostilePlayerNearby(STEALTH_LEAVE_DISTANCE))
            me->RemoveSpellsCausingAura(SPELL_AURA_MOD_STEALTH);
    }
    else if (m_spells.rogue.pStealth &&
             CanTryToCastSpell(me, m_spells.rogue.pStealth) &&
            !me->HasAura(AURA_WARSONG_FLAG) &&
            !me->HasAura(AURA_SILVERWING_FLAG) &&
             HasHostilePlayerNearby(STEALTH_ENTER_DISTANCE))
    {
        if (DoCastSpell(me, m_spells.rogue.pStealth) == SPELL_CAST_OK)
            return;
    }

    if (me->GetVictim())
        UpdateInCombatAI_Rogue();
}

void BattleBotAI::UpdateInCombatAI_Rogue()
{
    if (Unit* pVictim = me->GetVictim())
    {
        if (me->HasAuraType(SPELL_AURA_MOD_STEALTH))
        {
            if (m_spells.rogue.pPremeditation &&
                CanTryToCastSpell(pVictim, m_spells.rogue.pPremeditation))
            {
                DoCastSpell(pVictim, m_spells.rogue.pPremeditation);
            }

            if (pVictim->IsCaster())
            {
                if (m_spells.rogue.pGarrote &&
                    CanTryToCastSpell(pVictim, m_spells.rogue.pGarrote))
                {
                    if (DoCastSpell(pVictim, m_spells.rogue.pGarrote) == SPELL_CAST_OK)
                        return;
                }
            }
            else
            {
                if (m_spells.rogue.pAmbush &&
                    CanTryToCastSpell(pVictim, m_spells.rogue.pAmbush))
                {
                    if (DoCastSpell(pVictim, m_spells.rogue.pAmbush) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.rogue.pCheapShot &&
                    CanTryToCastSpell(pVictim, m_spells.rogue.pCheapShot))
                {
                    if (DoCastSpell(pVictim, m_spells.rogue.pCheapShot) == SPELL_CAST_OK)
                        return;
                }
            }
        }
        else
        {
            if (m_spells.rogue.pVanish &&
                (me->GetHealthPercent() < 10.0f))
            {
                if (m_spells.rogue.pPreparation &&
                    !me->IsSpellReady(m_spells.rogue.pVanish) &&
                    CanTryToCastSpell(me, m_spells.rogue.pPreparation))
                {
                    if (DoCastSpell(me, m_spells.rogue.pPreparation) == SPELL_CAST_OK)
                        return;
                }

                if (CanTryToCastSpell(me, m_spells.rogue.pVanish))
                {
                    if (DoCastSpell(me, m_spells.rogue.pVanish) == SPELL_CAST_OK)
                    {
                        if (me->GetMotionMaster()->MoveDistance(pVictim, 40.0f))
                            return;
                    }
                }
            }
        }

        if (me->GetComboPoints() > 4)
        {
            std::vector<SpellEntry const*> vSpells;
            if (m_spells.rogue.pSliceAndDice)
                vSpells.push_back(m_spells.rogue.pSliceAndDice);
            if (m_spells.rogue.pEviscerate)
                vSpells.push_back(m_spells.rogue.pEviscerate);
            if (m_spells.rogue.pKidneyShot)
                vSpells.push_back(m_spells.rogue.pKidneyShot);
            if (m_spells.rogue.pExposeArmor)
                vSpells.push_back(m_spells.rogue.pExposeArmor);
            if (m_spells.rogue.pRupture)
                vSpells.push_back(m_spells.rogue.pRupture);
            if (!vSpells.empty())
            {
                SpellEntry const* pComboSpell = SelectRandomContainerElement(vSpells);
                if (CanTryToCastSpell(pVictim, pComboSpell))
                {
                    if (DoCastSpell(pVictim, pComboSpell) == SPELL_CAST_OK)
                        return;
                }
            }
        }

        if (m_spells.rogue.pBlind)
        {
            if (Unit* pTarget = SelectAttackerDifferentFrom(pVictim))
            {
                if (CanTryToCastSpell(pTarget, m_spells.rogue.pBlind))
                {
                    if (DoCastSpell(pTarget, m_spells.rogue.pBlind) == SPELL_CAST_OK)
                    {
                        me->AttackStop();
                        AttackStart(pVictim);
                        return;
                    }
                }
            }
        }

        if (m_spells.rogue.pAdrenalineRush &&
           !me->GetPower(POWER_ENERGY) &&
            CanTryToCastSpell(me, m_spells.rogue.pAdrenalineRush))
        {
            if (DoCastSpell(me, m_spells.rogue.pAdrenalineRush) == SPELL_CAST_OK)
                return;
        }

        if (pVictim->IsNonMeleeSpellCasted())
        {
            if (m_spells.rogue.pGouge &&
                CanTryToCastSpell(pVictim, m_spells.rogue.pGouge))
            {
                if (DoCastSpell(pVictim, m_spells.rogue.pGouge) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.rogue.pKick &&
                CanTryToCastSpell(pVictim, m_spells.rogue.pKick))
            {
                if (DoCastSpell(pVictim, m_spells.rogue.pKick) == SPELL_CAST_OK)
                    return;
            }
        }

        if (!me->HasAuraType(SPELL_AURA_MOD_STEALTH))
        {
            if (m_spells.rogue.pEvasion &&
               (me->GetHealthPercent() < 80.0f) &&
               ((GetAttackersInRangeCount(10.0f) > 2) || !IsRangedDamageClass(pVictim->GetClass())) &&
                CanTryToCastSpell(me, m_spells.rogue.pEvasion))
            {
                if (DoCastSpell(me, m_spells.rogue.pEvasion) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.rogue.pColdBlood &&
                CanTryToCastSpell(me, m_spells.rogue.pColdBlood))
            {
                DoCastSpell(me, m_spells.rogue.pColdBlood);
            }

            if (m_spells.rogue.pBladeFlurry &&
                CanTryToCastSpell(me, m_spells.rogue.pBladeFlurry))
            {
                if (DoCastSpell(me, m_spells.rogue.pBladeFlurry) == SPELL_CAST_OK)
                    return;
            }
        }

        if (m_spells.rogue.pBackstab &&
            CanTryToCastSpell(pVictim, m_spells.rogue.pBackstab))
        {
            if (DoCastSpell(pVictim, m_spells.rogue.pBackstab) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.rogue.pGhostlyStrike &&
            CanTryToCastSpell(pVictim, m_spells.rogue.pGhostlyStrike))
        {
            if (DoCastSpell(pVictim, m_spells.rogue.pGhostlyStrike) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.rogue.pHemorrhage &&
            CanTryToCastSpell(pVictim, m_spells.rogue.pHemorrhage))
        {
            if (DoCastSpell(pVictim, m_spells.rogue.pHemorrhage) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.rogue.pSinisterStrike &&
            CanTryToCastSpell(pVictim, m_spells.rogue.pSinisterStrike))
        {
            if (DoCastSpell(pVictim, m_spells.rogue.pSinisterStrike) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.rogue.pSprint &&
           !me->HasUnitState(UNIT_STATE_ROOT) &&
           !me->CanReachWithMeleeAutoAttack(pVictim) &&
            CanTryToCastSpell(me, m_spells.rogue.pSprint))
        {
            if (DoCastSpell(me, m_spells.rogue.pSprint) == SPELL_CAST_OK)
                return;
        }
    }
}

void BattleBotAI::UpdateOutOfCombatAI_Druid()
{
    BattleGround* bg = me->GetBattleGround();
    if (bg && bg->GetStatus() == STATUS_WAIT_JOIN)
    {
        if (m_spells.druid.pGiftoftheWild)
        {
            if (Player* pTarget = SelectBuffTarget(m_spells.druid.pGiftoftheWild))
            {
                if (CanTryToCastSpell(pTarget, m_spells.druid.pGiftoftheWild))
                {
                    if (DoCastSpell(pTarget, m_spells.druid.pGiftoftheWild) == SPELL_CAST_OK)
                    {
                        m_isBuffing = true;
                        return;
                    }
                }
            }
        }

        if (m_spells.druid.pThorns)
        {
            if (Player* pTarget = SelectBuffTarget(m_spells.druid.pThorns))
            {
                if (CanTryToCastSpell(pTarget, m_spells.druid.pThorns))
                {
                    if (DoCastSpell(pTarget, m_spells.druid.pThorns) == SPELL_CAST_OK)
                    {
                        m_isBuffing = true;
                        return;
                    }
                }
            }
        }
    }
    else
    {
        if (m_spells.druid.pMarkoftheWild && CanTryToCastSpell(me, m_spells.druid.pMarkoftheWild))
        {
            if (DoCastSpell(me, m_spells.druid.pMarkoftheWild) == SPELL_CAST_OK)
            {
                m_isBuffing = true;
                return;
            }
        }

        if (m_spells.druid.pThorns && CanTryToCastSpell(me, m_spells.druid.pThorns))
        {
            if (DoCastSpell(me, m_spells.druid.pThorns) == SPELL_CAST_OK)
            {
                m_isBuffing = true;
                return;
            }
        }
    }

    if (m_spells.druid.pNaturesGrasp &&
        CanTryToCastSpell(me, m_spells.druid.pNaturesGrasp))
    {
        if (DoCastSpell(me, m_spells.druid.pNaturesGrasp) == SPELL_CAST_OK)
            return;
    }

    if (m_isBuffing &&
       (!m_spells.druid.pMarkoftheWild ||
        !me->HasGCD(m_spells.druid.pMarkoftheWild)))
    {
        m_isBuffing = false;
    }

    if (me->GetShapeshiftForm() == FORM_NONE)
    {
        if (m_role == ROLE_MELEE_DPS || m_role == ROLE_TANK)
        {
            if (m_spells.druid.pCatForm &&
                CanTryToCastSpell(me, m_spells.druid.pCatForm))
            {
                if (DoCastSpell(me, m_spells.druid.pCatForm) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.druid.pBearForm &&
                CanTryToCastSpell(me, m_spells.druid.pBearForm))
            {
                if (DoCastSpell(me, m_spells.druid.pBearForm) == SPELL_CAST_OK)
                    return;
            }
        }
        else
        {
            if ((me->GetPowerPercent(POWER_MANA) >  80.0f) &&
                FindAndHealInjuredAlly(70.0f,35.0f))
                return;
        }
    }
    else if (me->GetShapeshiftForm() == FORM_CAT)
    {
        if (m_spells.druid.pProwl &&
            CanTryToCastSpell(me, m_spells.druid.pProwl) &&
            !me->HasAura(AURA_WARSONG_FLAG) &&
            !me->HasAura(AURA_SILVERWING_FLAG))
        {
            if (DoCastSpell(me, m_spells.druid.pProwl) == SPELL_CAST_OK)
                return;
        }
    }

    if (me->GetVictim())
    {
        if (m_spells.druid.pMoonkinForm &&
            CanTryToCastSpell(me, m_spells.druid.pMoonkinForm))
        {
            if (DoCastSpell(me, m_spells.druid.pMoonkinForm) == SPELL_CAST_OK)
                return;
        }

        UpdateInCombatAI_Druid();
    }
    else
    {
        if (m_spells.druid.pMoonkinForm &&
            me->GetShapeshiftForm() == FORM_MOONKIN)
            me->RemoveAurasDueToSpellByCancel(m_spells.druid.pMoonkinForm->Id);

        if (m_spells.druid.pTravelForm &&
           !me->IsMounted() &&
           (!GetMountSpellId() || me->HasAura(AURA_WARSONG_FLAG) || me->HasAura(AURA_SILVERWING_FLAG)) &&
            CanTryToCastSpell(me, m_spells.druid.pTravelForm))
        {
            if (DoCastSpell(me, m_spells.druid.pTravelForm) == SPELL_CAST_OK)
                return;
        }
    }
}

void BattleBotAI::UpdateInCombatAI_Druid()
{
    if (m_spells.druid.pTravelForm &&
        me->GetShapeshiftForm() == FORM_TRAVEL)
        me->RemoveAurasDueToSpellByCancel(m_spells.druid.pTravelForm->Id);

    if (me->GetShapeshiftForm() == FORM_NONE)
    {
        if (m_spells.druid.pHibernate &&
            !me->GetAttackers().empty())
        {
            Unit* pAttacker = *me->GetAttackers().begin();
            if (CanTryToCastSpell(pAttacker, m_spells.druid.pHibernate))
            {
                if (DoCastSpell(pAttacker, m_spells.druid.pHibernate) == SPELL_CAST_OK)
                    return;
            }
        }

        if (m_role == ROLE_HEALER)
        {
            // Tranquility is channeled: only when nobody is around to interrupt it.
            if (m_spells.druid.pTranquility &&
                GetAlliesNeedingHealCount(20.0f, 70.0f) >= 3 &&
               !GetAttackersInRangeCount(15.0f) &&
                CanTryToCastSpell(me, m_spells.druid.pTranquility))
            {
                if (DoCastSpell(me, m_spells.druid.pTranquility) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.druid.pSwiftmend)
            {
                if (Unit* pTarget = SelectHealTarget(50.0f, false))
                {
                    if (pTarget->HasAuraType(SPELL_AURA_PERIODIC_HEAL) &&
                        CanTryToCastSpell(pTarget, m_spells.druid.pSwiftmend))
                    {
                        if (DoCastSpell(pTarget, m_spells.druid.pSwiftmend) == SPELL_CAST_OK)
                            return;
                    }
                }
            }
        }

        // Heal
        if (m_role == ROLE_HEALER ? FindAndHealInjuredAlly(80.0f, 40.0f) : FindAndHealInjuredAlly(70.0f, 35.0f))
            return;

        // Dispels
       SpellEntry const* pDispelSpell = m_spells.druid.pAbolishPoison ?
                                         m_spells.druid.pAbolishPoison :
                                         m_spells.druid.pCurePoison;
       if (pDispelSpell)
       {
           if (m_role == ROLE_HEALER)
           {
               if (Unit* pFriend = SelectDispelTarget(pDispelSpell))
               {
                   if (CanTryToCastSpell(pFriend, pDispelSpell))
                   {
                       if (DoCastSpell(pFriend, pDispelSpell) == SPELL_CAST_OK)
                           return;
                   }
               }
           }
           else if (IsValidDispelTarget(me, pDispelSpell) &&
                    CanTryToCastSpell(me, pDispelSpell))
           {
               if (DoCastSpell(me, pDispelSpell) == SPELL_CAST_OK)
                   return;
           }
       }

        if (m_spells.druid.pRemoveCurse)
        {
            if (Unit* pFriend = SelectDispelTarget(m_spells.druid.pRemoveCurse))
            {
                if (CanTryToCastSpell(pFriend, m_spells.druid.pRemoveCurse))
                {
                    if (DoCastSpell(pFriend, m_spells.druid.pRemoveCurse) == SPELL_CAST_OK)
                        return;
                }
            }
        }

        if (m_spells.druid.pInnervate &&
            me->GetVictim() &&
           (me->GetHealthPercent() > 40.0f) &&
           (me->GetPowerPercent(POWER_MANA) < 10.0f) &&
            CanTryToCastSpell(me, m_spells.druid.pInnervate))
        {
            if (DoCastSpell(me, m_spells.druid.pInnervate) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.druid.pMoonkinForm &&
            CanTryToCastSpell(me, m_spells.druid.pMoonkinForm))
        {
            if (DoCastSpell(me, m_spells.druid.pMoonkinForm) == SPELL_CAST_OK)
                return;
        }

        if (m_role == ROLE_MELEE_DPS || m_role == ROLE_TANK)
        {
            if (Unit* pVictim = me->GetVictim())
            {
                if (m_spells.druid.pBearForm &&
                    pVictim->CanReachWithMeleeAutoAttack(me) &&
                    IsPhysicalDamageClass(pVictim->GetClass()) &&
                    CanTryToCastSpell(me, m_spells.druid.pBearForm))
                {
                    if (DoCastSpell(me, m_spells.druid.pBearForm) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.druid.pCatForm &&
                    CanTryToCastSpell(me, m_spells.druid.pCatForm))
                {
                    if (DoCastSpell(me, m_spells.druid.pCatForm) == SPELL_CAST_OK)
                        return;
                }
            }
        }
    }
    else
    {
        if (me->HasUnitState(UNIT_STATE_ROOT) &&
            me->HasAuraType(SPELL_AURA_MOD_SHAPESHIFT))
            me->RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);
    }

    if (Unit* pVictim = me->GetVictim())
    {
        ShapeshiftForm const form = me->GetShapeshiftForm();
        if (m_spells.druid.pBarkskin &&
           (form == FORM_NONE || form == FORM_MOONKIN) &&
           (me->GetHealthPercent() < 50.0f) &&
            CanTryToCastSpell(me, m_spells.druid.pBarkskin))
        {
            if (DoCastSpell(me, m_spells.druid.pBarkskin) == SPELL_CAST_OK)
                return;
        }

        switch (form)
        {
            case FORM_CAT:
            {
                if (me->HasDistanceCasterMovement())
                    me->SetCasterChaseDistance(0.0f);

                if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE
                    && !me->CanReachWithMeleeAutoAttack(pVictim))
                {
                    BeginChasing(pVictim);
                }

                if (me->HasAuraType(SPELL_AURA_MOD_STEALTH))
                {
                    if (m_spells.druid.pPounce &&
                        CanTryToCastSpell(pVictim, m_spells.druid.pPounce))
                    {
                        if (DoCastSpell(pVictim, m_spells.druid.pPounce) == SPELL_CAST_OK)
                            return;
                    }
                    if (m_spells.druid.pRavage &&
                        CanTryToCastSpell(pVictim, m_spells.druid.pRavage))
                    {
                        if (DoCastSpell(pVictim, m_spells.druid.pRavage) == SPELL_CAST_OK)
                            return;
                    }
                    if (m_spells.druid.pTigersFury &&
                        CanTryToCastSpell(me, m_spells.druid.pTigersFury))
                    {
                        if (DoCastSpell(me, m_spells.druid.pTigersFury) == SPELL_CAST_OK)
                            return;
                    }
                    return;
                }

                if (me->GetComboPoints() > 4)
                {
                    if (m_spells.druid.pFerociousBite &&
                        CanTryToCastSpell(pVictim, m_spells.druid.pFerociousBite))
                    {
                        if (DoCastSpell(pVictim, m_spells.druid.pFerociousBite) == SPELL_CAST_OK)
                            return;
                    }

                    if (m_spells.druid.pRip &&
                        CanTryToCastSpell(pVictim, m_spells.druid.pRip))
                    {
                        if (DoCastSpell(pVictim, m_spells.druid.pRip) == SPELL_CAST_OK)
                            return;
                    }
                }

                if (!me->CanReachWithMeleeAutoAttack(pVictim))
                {
                    if (m_spells.druid.pFaerieFireFeral &&
                        CanTryToCastSpell(pVictim, m_spells.druid.pFaerieFireFeral))
                    {
                        if (DoCastSpell(pVictim, m_spells.druid.pFaerieFireFeral) == SPELL_CAST_OK)
                            return;
                    }

                    if (m_spells.druid.pDash &&
                        pVictim->IsMoving() &&
                        CanTryToCastSpell(me, m_spells.druid.pDash))
                    {
                        if (DoCastSpell(me, m_spells.druid.pDash) == SPELL_CAST_OK)
                            return;
                    }
                }

                if (m_spells.druid.pShred &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pShred))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pShred) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.druid.pRake &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pRake))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pRake) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.druid.pClaw &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pClaw))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pClaw) == SPELL_CAST_OK)
                        return;
                }

                break;
            }
            case FORM_BEAR:
            case FORM_DIREBEAR:
            {
                if (me->HasDistanceCasterMovement())
                    me->SetCasterChaseDistance(0.0f);

                if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE
                    && !me->CanReachWithMeleeAutoAttack(pVictim))
                {
                    BeginChasing(pVictim);
                }

                if (m_spells.druid.pFeralCharge &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pFeralCharge))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pFeralCharge) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.druid.pBash &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pBash))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pBash) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.druid.pFrenziedRegeneration &&
                   (me->GetHealthPercent() < 30.0f) &&
                    CanTryToCastSpell(me, m_spells.druid.pFrenziedRegeneration))
                {
                    if (DoCastSpell(me, m_spells.druid.pFrenziedRegeneration) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.druid.pFaerieFireFeral &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pFaerieFireFeral))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pFaerieFireFeral) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.druid.pDemoralizingRoar &&
                    IsMeleeDamageClass(pVictim->GetClass()) &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pDemoralizingRoar))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pDemoralizingRoar) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.druid.pSwipe &&
                   ((me->GetPower(POWER_RAGE) > 50) || (GetAttackersInRangeCount(10.0f) > 1)) &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pSwipe))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pSwipe) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.druid.pMaul &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pMaul))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pMaul) == SPELL_CAST_OK)
                        return;
                }
                break;
            }
            case FORM_NONE:
            case FORM_MOONKIN:
            {
                if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE &&
                    me->GetDistance(pVictim) > 30.0f)
                {
                    BeginChasing(pVictim);
                }
                else if (pVictim->CanReachWithMeleeAutoAttack(me) &&
                        (pVictim->GetVictim() == me) &&
                        !me->HasUnitState(UNIT_STATE_ROOT) &&
                        (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != DISTANCING_MOTION_TYPE))
                {
                    if (m_spells.druid.pEntanglingRoots &&
                        CanTryToCastSpell(pVictim, m_spells.druid.pEntanglingRoots))
                    {
                        if (DoCastSpell(pVictim, m_spells.druid.pEntanglingRoots) == SPELL_CAST_OK)
                            return;
                    }
                    me->SetCasterChaseDistance(25.0f);
                    if (me->GetMotionMaster()->MoveDistance(pVictim, 25.0f))
                        return;
                }

                if (m_spells.druid.pFaerieFire &&
                   (pVictim->GetClass() == CLASS_ROGUE) &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pFaerieFire))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pFaerieFire) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.druid.pInsectSwarm &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pInsectSwarm))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pInsectSwarm) == SPELL_CAST_OK)
                        return;
                }

                if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == DISTANCING_MOTION_TYPE)
                    return;

                if (m_spells.druid.pMoonfire &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pMoonfire))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pMoonfire) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.druid.pStarfire &&
                   (pVictim->GetHealthPercent() > 50.0f) &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pStarfire))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pStarfire) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.druid.pWrath &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pWrath))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pWrath) == SPELL_CAST_OK)
                        return;
                }

                break;
            }
        }
    }
}
