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

#ifndef MANGOS_PARTYBOTAI_H
#define MANGOS_PARTYBOTAI_H

#include "CombatBotBaseAI.h"
#include "Group.h"
#include "ObjectAccessor.h"

enum PartyBotSpells
{
    PB_SPELL_FOOD = 1131,
    PB_SPELL_DRINK = 1137,
    PB_SPELL_DRINK_50 = 25696,
    PB_SPELL_AUTO_SHOT = 75,
    PB_SPELL_SHOOT_WAND = 5019,
    PB_SPELL_HONORLESS_TARGET = 2479,
    PB_SPELL_POT_REJUV = 22729,
    PB_SPELL_POT_RESTO = 11359,
    PB_SPELL_POT_HEAL_3 = 440,
    PB_SPELL_POT_HEAL_12 = 441,
    PB_SPELL_POT_HEAL_21 = 2024,
    PB_SPELL_POT_HEAL_35 = 4042,
    PB_SPELL_POT_HEAL_45 = 17534,
    PB_SPELL_BAND_HEAVY_RUNE = 18610,
    PB_SPELL_ELX_MAGEBL = 24363,
    PB_SPELL_ELX_MOONG = 17538,
    PB_SPELL_ELX_FORCE = 17537,
    PB_SPELL_FLASK_TITAN = 17626,
    PB_SPELL_FLASK_SPOWER = 17628,
    PB_SPELL_FLASK_WISDOM = 17627,


    PB_SPELL_MOUNT_40_HUMAN = 470,
    PB_SPELL_MOUNT_40_NELF = 10787,
    PB_SPELL_MOUNT_40_DWARF = 6896,
    PB_SPELL_MOUNT_40_GNOME = 17456,
    PB_SPELL_MOUNT_40_TROLL = 10795,
    PB_SPELL_MOUNT_40_ORC = 581,
    PB_SPELL_MOUNT_40_TAUREN = 18363,
    PB_SPELL_MOUNT_40_UNDEAD = 8980,
    PB_SPELL_MOUNT_60_HUMAN = 22717,
    PB_SPELL_MOUNT_60_NELF = 22723,
    PB_SPELL_MOUNT_60_DWARF = 22720,
    PB_SPELL_MOUNT_60_GNOME = 22719,
    PB_SPELL_MOUNT_60_TROLL = 22721,
    PB_SPELL_MOUNT_60_ORC = 22724,
    PB_SPELL_MOUNT_60_TAUREN = 22718,
    PB_SPELL_MOUNT_60_UNDEAD = 22722,
    PB_SPELL_MOUNT_40_PALADIN = 13819,
    PB_SPELL_MOUNT_60_PALADIN = 23214,
    PB_SPELL_MOUNT_40_WARLOCK = 5784,
    PB_SPELL_MOUNT_60_WARLOCK = 23161,

    PB_SPELL_SHIELD_SLAM = 23922,
    PB_SPELL_HOLY_SHIELD = 20925,
    PB_SPELL_TOUCH_OF_SHADOW = 18791,
};

struct LootResponseData
{
    LootResponseData(uint64 guid_, uint32 slot_) : guid(guid_), slot(slot_) {}
    uint64 guid = 0;
    uint32 slot = 0;
};

class PartyBotAI : public CombatBotBaseAI
{
public:

    PartyBotAI(Player* pLeader, Player* pClone, CombatBotRoles role, uint8 race, uint8 class_, uint8 level, uint32 mapId, uint32 instanceId, float x, float y, float z, float o)
        : CombatBotBaseAI(), m_race(race), m_class(class_), m_level(level), m_mapId(mapId), m_instanceId(instanceId), m_x(x), m_y(y), m_z(z), m_o(o)
    {
        m_role = role;
        m_leaderGuid = pLeader->GetObjectGuid();
        m_cloneGuid = pClone ? pClone->GetObjectGuid() : ObjectGuid();
        m_updateTimer.Reset(2000);
    }
    PartyBotAI(Player* pLeader, uint32 mapId, uint32 instanceId, float x, float y, float z, float o)
        : CombatBotBaseAI(), m_mapId(mapId), m_instanceId(instanceId), m_x(x), m_y(y), m_z(z), m_o(o)
    {
        m_role = ROLE_INVALID;
        m_leaderGuid = pLeader->GetObjectGuid();
        m_updateTimer.Reset(2000);
    }

    bool OnSessionLoaded(PlayerBotEntry* entry, WorldSession* sess) final;
    void OnPlayerLogin() final;
    void UpdateAI(uint32 const diff) final;
    void OnPacketReceived(WorldPacket const* packet) final;

    uint32 GetMountSpellId() const;
    void CloneFromPlayer(Player const* pPlayer);
    void LearnPremadeSpecForClass();
    void AddToPlayerGroup();

    Player* GetPartyLeader() const;
    bool AttackStart(Unit* pVictim);
    Unit* SelectAttackTarget() const;
    Unit* SelectPartyAttackTarget() const;
    Unit* SelectSpellTargetDifferentFrom(SpellEntry const* pSpellEntry, Unit* pVictim, float distance = 10.0f) const;
    Player* SelectResurrectionTarget() const;
    Player* SelectShieldTarget() const;
    Unit* GetMarkedTarget(RaidTargetIcon mark) const;
    bool CanUseCrowdControl(SpellEntry const* pSpellEntry, Unit* pTarget) const;
    bool DrinkAndEat();
    bool ShouldAutoRevive() const;
    bool IsValidDistancingTarget(Unit* pTarget, Unit* pEnemy);
    Unit* GetDistancingTarget(Unit* pEnemy);
    bool CrowdControlMarkedTargets();
    void RunAwayFromTarget(Unit* pTarget, bool pFollowLeader = true, float pDistance = 12.0f);
    void RunAwayFromObject(GameObject* pObject, float pDistance = 10.0f);
    void RunAwayFromAOE(float pDistance);
    void MoveToTarget(Unit* pTarget, float pDistance = 1.0f);
    void MoveToTargetDistance(Unit* pTarget, float pDistance = 25.0f);
    void ChaseTarget(Unit* pTarget);
    bool EnterCombatDruidForm();
    void PopulateConsumableSpellData();
    bool CheckThreat(Unit const* pTarget);
    bool CheckCombatInstanceMechanics(bool &pCombatEngagementReady);
    bool ShouldEnterStealth() const;
    bool EnterStealthIfNeeded(SpellEntry const* pStealthSpell);

    bool CheckForDispelTargets();
    void UpdateInCombatAI() final;
    void UpdateOutOfCombatAI() final;
    void UpdateInCombatAI_Paladin() final;
    void UpdateOutOfCombatAI_Paladin() final;
    void UpdateInCombatAI_Shaman() final;
    void UpdateOutOfCombatAI_Shaman() final;
    void UpdateInCombatAI_Hunter() final;
    void UpdateOutOfCombatAI_Hunter() final;
    void UpdateInCombatAI_Mage() final;
    void UpdateOutOfCombatAI_Mage() final;
    void UpdateInCombatAI_Priest() final;
    void UpdateOutOfCombatAI_Priest() final;
    void UpdateInCombatAI_Warlock() final;
    void UpdateOutOfCombatAI_Warlock() final;
    void UpdateInCombatAI_Warrior() final;
    void UpdateOutOfCombatAI_Warrior() final;
    void UpdateInCombatAI_Rogue() final;
    void UpdateOutOfCombatAI_Rogue() final;
    void UpdateInCombatAI_Druid() final;
    void UpdateOutOfCombatAI_Druid() final;

    std::vector<RaidTargetIcon> m_marksToCC;
    std::vector<RaidTargetIcon> m_marksToFocus;
    ShortTimeTracker m_updateTimer;
    ObjectGuid m_leaderGuid;
    ObjectGuid m_cloneGuid;
    ObjectGuid m_distObjGuid;
    SpellEntry const* m_potionSpell = nullptr;
    SpellEntry const* m_elixirSpell = nullptr;
    SpellEntry const* m_flaskSpell = nullptr;
    SpellEntry const* m_restPotion = nullptr;
    SpellEntry const* m_bandage = nullptr;
    uint8 m_race = 0;
    uint8 m_class = 0;
    uint8 m_level = 0;
    uint32 m_mapId = 0;
    uint32 m_instanceId = 0;
    uint32 m_ressTimer = 0;
    uint32 m_aoeSpellTimer = 0;
    uint32 m_spellTimer1 = 0;
    uint32 m_threatCheckTimer = 0;
    bool  m_threatOK = true;
    float m_x = 0.0f;
    float m_y = 0.0f;
    float m_z = 0.0f;
    float m_o = 0.0f;
    bool m_resetSpellData = false;
};

#endif
