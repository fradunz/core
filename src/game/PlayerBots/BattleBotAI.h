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

#ifndef MANGOS_BattleBotAI_H
#define MANGOS_BattleBotAI_H

#include "CombatBotBaseAI.h"
#include "BattleBotWaypoints.h"

enum BattleBotWsgWaitSpot
{
    BB_WSG_WAIT_SPOT_SPAWN,
    BB_WSG_WAIT_SPOT_LEFT,
    BB_WSG_WAIT_SPOT_RIGHT
};

enum FlagSpellsWS
{
    AURA_WARSONG_FLAG    = 23333,
    AURA_SILVERWING_FLAG = 23335
};

enum WSGRole : uint8
{
    WSG_ROLE_NONE     = 0,
    WSG_ROLE_DEFENDER = 1, // Stays near own flag.
    WSG_ROLE_ATTACKER = 2, // Regular bot.
    WSG_ROLE_CAPTURER = 3, // Focused on taking the enemy flag, ignores fights that are not very close.
};

// What a bot is doing in Warsong Gulch right now. Derived from its role and the state of the flags (GetWSGMode()).
enum WSGMode : uint8
{
    WSG_MODE_GENERAL  = 0, // Goes for the enemy flag together with the team, fights what it finds.
    WSG_MODE_CARRIER  = 1, // Carries the enemy flag home, ignores fights.
    WSG_MODE_CAPTURER = 2, // Takes the enemy flag while it is at its base, ignores fights that are not very close.
    WSG_MODE_DEFENDER = 3, // Defends our flag at the base.
    WSG_MODE_ESCORT   = 4, // Stays with our carrier.
    WSG_MODE_CHASER   = 5, // Hunts the enemy carrier to get our flag back.
};

class BattleBotAI : public CombatBotBaseAI
{
public:
    BattleBotAI(uint8 race, uint8 class_, uint8 level, uint32 mapId, uint32 instanceId, float x, float y, float z, float o, uint8 bgId, bool temporary)
        : CombatBotBaseAI(),  m_race(race), m_class(class_), m_level(level), m_mapId(mapId), m_instanceId(instanceId), m_x(x), m_y(y), m_z(z), m_o(o), m_battlegroundId(bgId), m_temporary(temporary)
    {
        m_updateTimer.Reset(2000);
    }
    bool OnSessionLoaded(PlayerBotEntry* entry, WorldSession* sess) override
    {
        return SpawnNewPlayer(sess, m_class, m_race, m_mapId, m_instanceId, m_x, m_y, m_z, m_o);
    }
  
    void OnPlayerLogin() final;
    void UpdateAI(uint32 const diff) final;
    void OnPacketReceived(WorldPacket const* packet) final;
    void MovementInform(uint32 MovementType, uint32 Data = 0) final;

    bool ShouldIgnoreCombat() const;
    bool DrinkAndEat();
    bool UseMount();
    uint32 GetMountSpellId() const;
    bool CheckForUnreachableTarget();
    float GetMaxAggroDistanceForMap() const;
    bool AttackStart(Unit* pVictim);
    Unit* SelectAttackTarget(Unit* pExcept = nullptr) const;
    Unit* SelectHealTarget(float healthPercent = 100.0f, bool periodic = false) const override;
    Player* SelectShieldTarget() const;
    bool IsOwnFlagCarrier(Unit const* pUnit) const;
    Player* GetEscortCarrier() const;
    Unit* SelectFollowTarget() const;

    void OnJustRevived();
    void OnJustDied();
    void OnEnterBattleGround();
    void OnLeaveBattleGround();

    void UpdateFlagCarrierAI();
    void UpdateBattleGroundAI();
    void AddPremadeGearAndSpells();
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

    uint8 m_battlegroundId = 0;
    ShortTimeTracker m_updateTimer;
    uint8 m_race = 0;
    uint8 m_class = 0;
    uint8 m_level = 0;
    uint32 m_mapId = 0;
    uint32 m_instanceId = 0;
    float m_x = 0.0f;
    float m_y = 0.0f;
    float m_z = 0.0f;
    float m_o = 0.0f;
    bool m_temporary = false;
    bool m_wasDead = false;
    bool m_wasInBG = false;

    // Movement System
    void UpdateWaypointMovement();
    void DoGraveyardJump();
    void MoveToNextPoint();
    bool StartNewPathFromBeginning();
    void StartNewPathFromAnywhere();
    bool StartNewPathToObjective();
    void SavePathForResume();
    bool FindResumePoint(BattleBotPath* pPath, bool reverse, uint32& point) const;
    bool ResumeSavedPath();
    WSGMode GetWSGMode() const;
    uint8 GetWSGFlagState() const;
    void PatrolAround(Position const& center, float radius);
    void WanderAtStart();
    bool HelpFightingAlly();
    bool IsWSGDefender() const { return m_wsgRole == WSG_ROLE_DEFENDER; }
    bool IsWSGCapturer() const { return m_wsgRole == WSG_ROLE_CAPTURER; }
    void RollWSGRole();
    bool IsObjectiveFocused() const;
    bool ShouldKeepMoving() const;
    bool IsFocusFightTooLong() const;
    bool IsBeingAttacked() const;
    bool HasHostilePlayerNearby(float range) const;
    bool StartNewPathToPosition(Position const& position, std::vector<BattleBotPath*> const& vPaths);
    void ClearPath();
    // Chooses what to do when there is no path: objective path, resume, random path.
    void DecideNextAction();
    // A path just ended: choose the next one at the next update.
    bool m_decidePending = false;
    // Moves on to the next waypoint before reaching the current one, so movement looks less robotic.
    void UpdatePathProgress();
    GameObject* FindBuffNearPoint(BattleBotWaypoint const& point) const;
    // WSG attackers gather near the enemy base and enter it together.
    bool ShouldWaitForGroup();
    bool m_waitingForGroup = false;
    uint32 m_groupWaitStart = 0;
    uint32 m_groupWaitIgnoreUntil = 0;
    float m_cornerTolerance = 0.0f;
    float m_moveTargetX = 0.0f;
    float m_moveTargetY = 0.0f;
    float m_moveTargetZ = 0.0f;
    void StopMoving();
    bool m_doingGraveyardJump = false;
    bool m_movingInReverse = false;
    // WSG role, rolled once per battleground.
    uint8 m_wsgRole = WSG_ROLE_NONE;
    uint8 m_wsgChaseRoll = 0; // 0-99, rolled with the role.
    // Last mode seen: a change of mode interrupts the current path.
    uint8 m_wsgMode = WSG_MODE_GENERAL;
    uint8 m_wsgFlagState = 0; // Last flags situation seen (see GetWSGFlagState()).
    uint32 m_currentPoint = 0;
    BattleBotPath* m_currentPath = nullptr;
    // Path interrupted by a fight, so it can be resumed ahead of where the bot was instead of starting over.
    BattleBotPath* m_resumePath = nullptr;
    bool m_resumeReverse = false;
    uint32 m_resumePoint = 0;
    uint32 m_resumeTime = 0;
    // Flag capturers: when the current fight started and until when new fights are ignored (ms timestamps).
    uint32 m_wanderNextMove = 0;            // Next time an idle bot (defender, bot waiting for the gates) walks around.
    ObjectGuid m_escortFollowGuid;          // Carrier we are following as an escort.
    ObjectGuid m_lastBuffGuid;              // Last power up we went for (not repeated right away).
    uint32 m_lastBuffTime = 0;
    mutable uint32 m_focusFightStart = 0;
    mutable uint32 m_focusIgnoreUntil = 0;
    uint8 m_waitingSpot = BB_WSG_WAIT_SPOT_SPAWN;
};

#endif
