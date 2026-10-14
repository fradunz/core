#include "PartyBotAI.h"
#include "Utilities/Random.h"
#include "CombatBotBaseAI.h"
#include "Player.h"
#include "Log.h"
#include "MotionMaster.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "ObjectMgr.h"
#include "PlayerBotMgr.h"
#include "WorldPacket.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "Chat.h"
#include <string>
#include <random>
#include <algorithm>
#include <sstream>

#define PB_UPDATE_INTERVAL 1000
#define PB_MIN_FOLLOW_DIST 3.0f
#define PB_MAX_FOLLOW_DIST 6.0f
#define PB_MIN_FOLLOW_ANGLE 2.0f
#define PB_MAX_FOLLOW_ANGLE 4.0f

bool PartyBotAI::OnSessionLoaded(PlayerBotEntry* entry, WorldSession* sess)
{
    // Existing character used as a party bot (upstream feature)
    if (!m_race && !m_class)
    {
        sess->LoginPlayer(entry->playerGUID);
        return true;
    }

    return SpawnNewPlayer(sess, m_class, m_race, m_mapId, m_instanceId, m_x, m_y, m_z, m_o, sObjectAccessor.FindPlayer(m_cloneGuid));
}

void PartyBotAI::CloneFromPlayer(Player const* pPlayer)
{
    if (!pPlayer)
        return;

    if (pPlayer->GetLevel() != me->GetLevel())
    {
        me->GiveLevel(pPlayer->GetLevel());
        me->InitTalentForLevel();
        me->SetUInt32Value(PLAYER_XP, 0);
    }

    // Learn all of the target's spells.
    for (const auto& spell : pPlayer->GetSpellMap())
    {
        if (spell.second.disabled)
            continue;

        if (spell.second.state == PLAYERSPELL_REMOVED)
            continue;

        SpellEntry const* pSpellEntry = sSpellMgr.GetSpellEntry(spell.first);
        if (!pSpellEntry)
            continue;

        uint32 const firstRankId = sSpellMgr.GetFirstSpellInChain(spell.first);
        if (!me->HasSpell(spell.first))
            me->LearnSpell(spell.first, false, (firstRankId == spell.first && GetTalentSpellPos(firstRankId)));
    }

    me->GetHonorMgr().SetHighestRank(pPlayer->GetHonorMgr().GetHighestRank());
    me->GetHonorMgr().SetRank(pPlayer->GetHonorMgr().GetRank());

    // Unequip current gear
    for (int i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
        me->AutoUnequipItemFromSlot(i);

    // Copy gear from target.
    for (int i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
        if (Item* pItem = pPlayer->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            me->StoreNewItemInBestSlots(pItem->GetEntry(), 1);
    }
}

namespace
{
    // Talent trees used by each role. Tabs are the client order index (TalentTab.dbc m_orderIndex),
    // e.g. Warrior: 0 Arms, 1 Fury, 2 Protection.
    struct TalentSpecRule
    {
        uint8 playerClass;
        CombatBotRoles role;
        uint8 primaryTab;
        uint8 secondaryTab;
    };

    TalentSpecRule const TalentSpecRules[] =
    {
        { CLASS_WARRIOR, ROLE_TANK,      2, 0 }, // Protection / Arms
        { CLASS_WARRIOR, ROLE_MELEE_DPS, 0, 1 }, // Arms / Fury
        { CLASS_WARRIOR, ROLE_MELEE_DPS, 1, 0 }, // Fury / Arms
        { CLASS_PALADIN, ROLE_HEALER,    0, 1 }, // Holy / Protection
        { CLASS_PALADIN, ROLE_TANK,      1, 0 }, // Protection / Holy
        { CLASS_PALADIN, ROLE_MELEE_DPS, 2, 0 }, // Retribution / Holy
        { CLASS_HUNTER,  ROLE_RANGE_DPS, 1, 0 }, // Marksmanship / Beast Mastery
        { CLASS_ROGUE,   ROLE_MELEE_DPS, 1, 0 }, // Combat / Assassination
        { CLASS_ROGUE,   ROLE_MELEE_DPS, 0, 1 }, // Assassination / Combat
        { CLASS_PRIEST,  ROLE_HEALER,    1, 0 }, // Holy / Discipline
        { CLASS_PRIEST,  ROLE_HEALER,    0, 1 }, // Discipline / Holy
        { CLASS_PRIEST,  ROLE_RANGE_DPS, 2, 0 }, // Shadow / Discipline
        { CLASS_SHAMAN,  ROLE_HEALER,    2, 0 }, // Restoration / Elemental
        { CLASS_SHAMAN,  ROLE_MELEE_DPS, 1, 2 }, // Enhancement / Restoration
        { CLASS_SHAMAN,  ROLE_RANGE_DPS, 0, 2 }, // Elemental / Restoration
        { CLASS_MAGE,    ROLE_RANGE_DPS, 2, 0 }, // Frost / Arcane
        { CLASS_WARLOCK, ROLE_RANGE_DPS, 0, 1 }, // Affliction / Demonology
        { CLASS_WARLOCK, ROLE_RANGE_DPS, 2, 1 }, // Destruction / Demonology
        { CLASS_DRUID,   ROLE_HEALER,    2, 0 }, // Restoration / Balance
        { CLASS_DRUID,   ROLE_TANK,      1, 2 }, // Feral / Restoration
        { CLASS_DRUID,   ROLE_MELEE_DPS, 1, 2 }, // Feral / Restoration
        { CLASS_DRUID,   ROLE_RANGE_DPS, 0, 2 }, // Balance / Restoration
    };

    char const* const TalentTabNames[MAX_CLASSES][3] =
    {
        { "", "", "" },
        { "Arms", "Fury", "Protection" },                       // Warrior
        { "Holy", "Protection", "Retribution" },                // Paladin
        { "Beast Mastery", "Marksmanship", "Survival" },        // Hunter
        { "Assassination", "Combat", "Subtlety" },              // Rogue
        { "Discipline", "Holy", "Shadow" },                     // Priest
        { "", "", "" },
        { "Elemental", "Enhancement", "Restoration" },          // Shaman
        { "Arcane", "Fire", "Frost" },                          // Mage
        { "Affliction", "Demonology", "Destruction" },          // Warlock
        { "", "", "" },
        { "Balance", "Feral Combat", "Restoration" },           // Druid
    };

    char const* GetRoleName(CombatBotRoles role)
    {
        switch (role)
        {
            case ROLE_TANK:      return "tank";
            case ROLE_HEALER:    return "healer";
            case ROLE_MELEE_DPS: return "melee dps";
            case ROLE_RANGE_DPS: return "ranged dps";
            default:             return "none";
        }
    }
}

uint32 PartyBotAI::GetTalentTabId(uint8 tabPage) const
{
    for (uint32 i = 0; i < sTalentTabStore.GetNumRows(); ++i)
    {
        TalentTabEntry const* pTab = sTalentTabStore.LookupEntry(i);
        if (pTab && (pTab->ClassMask & me->GetClassMask()) && pTab->tabpage == tabPage)
            return pTab->TalentTabID;
    }
    return 0;
}

uint32 PartyBotAI::GetSpentTalentPoints(uint32 tabId) const
{
    uint32 points = 0;
    for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
    {
        TalentEntry const* pTalent = sTalentStore.LookupEntry(i);
        if (!pTalent || pTalent->TalentTab != tabId)
            continue;

        for (int32 rank = MAX_TALENT_RANK - 1; rank >= 0; --rank)
        {
            if (pTalent->RankID[rank] && me->HasSpell(pTalent->RankID[rank]))
            {
                points += rank + 1;
                break;
            }
        }
    }
    return points;
}

bool PartyBotAI::LearnNextTalentInTab(uint32 tabId)
{
    uint32 const spentPoints = GetSpentTalentPoints(tabId);

    // Talents in unlocked rows that still have ranks to learn
    std::vector<std::pair<TalentEntry const*, uint32 /*next rank*/>> candidates;
    for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
    {
        TalentEntry const* pTalent = sTalentStore.LookupEntry(i);
        if (!pTalent || pTalent->TalentTab != tabId)
            continue;

        if (pTalent->Row * MAX_TALENT_RANK > spentPoints)
            continue;

        uint32 maxRank = 0;
        while (maxRank < MAX_TALENT_RANK && pTalent->RankID[maxRank])
            ++maxRank;

        uint32 currentRank = 0;
        for (int32 rank = maxRank - 1; rank >= 0; --rank)
        {
            if (me->HasSpell(pTalent->RankID[rank]))
            {
                currentRank = rank + 1;
                break;
            }
        }

        if (currentRank < maxRank)
            candidates.push_back({ pTalent, currentRank });
    }

    // Random talent, deepest unlocked row first, so the tree reaches its 31 point talent
    std::shuffle(candidates.begin(), candidates.end(), std::mt19937(std::random_device{}()));
    std::stable_sort(candidates.begin(), candidates.end(),
        [](std::pair<TalentEntry const*, uint32> const& a, std::pair<TalentEntry const*, uint32> const& b)
        {
            return a.first->Row > b.first->Row;
        });

    for (auto const& candidate : candidates)
    {
        // Fails when a required talent or spell is missing; try the next one
        if (me->LearnTalent(candidate.first->TalentID, candidate.second))
            return true;
    }

    return false;
}

void PartyBotAI::SpendTalentPointsInTab(uint32 tabId, uint32 maxPointsInTab)
{
    while (me->GetFreeTalentPoints() && GetSpentTalentPoints(tabId) < maxPointsInTab)
    {
        if (!LearnNextTalentInTab(tabId))
            break;
    }
}

void PartyBotAI::LearnTalentsForRole()
{
    if (!me->GetFreeTalentPoints())
        return;

    uint32 tabIds[3];
    uint32 spentPoints[3];
    for (uint8 page = 0; page < 3; ++page)
    {
        tabIds[page] = GetTalentTabId(page);
        if (!tabIds[page])
            return;
        spentPoints[page] = GetSpentTalentPoints(tabIds[page]);
    }

    // Rules for the bot role; any rule of the class when the role has none (or is not set yet)
    std::vector<TalentSpecRule const*> rules;
    for (auto const& rule : TalentSpecRules)
        if (rule.playerClass == me->GetClass() && rule.role == m_role)
            rules.push_back(&rule);
    if (rules.empty())
        for (auto const& rule : TalentSpecRules)
            if (rule.playerClass == me->GetClass())
                rules.push_back(&rule);
    if (rules.empty())
        return;

    TalentSpecRule const* pRule = SelectRandomContainerElement(rules);
    uint8 primary = pRule->primaryTab;
    uint8 secondary = pRule->secondaryTab;

    // Keep the trees already used by a premade spec template
    uint8 mostUsed = 0;
    for (uint8 page = 1; page < 3; ++page)
        if (spentPoints[page] > spentPoints[mostUsed])
            mostUsed = page;

    if (spentPoints[mostUsed])
    {
        primary = mostUsed;

        uint8 secondMostUsed = (primary + 1) % 3;
        uint8 const other = (primary + 2) % 3;
        if (spentPoints[other] > spentPoints[secondMostUsed])
            secondMostUsed = other;

        if (spentPoints[secondMostUsed])
            secondary = secondMostUsed;
        else if (secondary == primary)
            secondary = (pRule->primaryTab != primary) ? pRule->primaryTab : secondMostUsed;
    }

    // Primary tree up to its 31 point talent, then the secondary tree, then whatever is left
    SpendTalentPointsInTab(tabIds[primary], 31);
    SpendTalentPointsInTab(tabIds[secondary], UINT32_MAX);
    SpendTalentPointsInTab(tabIds[primary], UINT32_MAX);
    for (uint8 page = 0; page < 3; ++page)
        SpendTalentPointsInTab(tabIds[page], UINT32_MAX);
}

void PartyBotAI::ReportBuildToLeader() const
{
    Player* pLeader = sObjectAccessor.FindPlayer(m_leaderGuid);
    if (!pLeader || pLeader == me)
        return;

    std::ostringstream ss;
    ss << me->GetName() << " (" << GetRoleName(m_role) << ") - Talents: ";
    for (uint8 page = 0; page < 3; ++page)
    {
        if (page)
            ss << " / ";
        ss << TalentTabNames[me->GetClass()][page] << " " << GetSpentTalentPoints(GetTalentTabId(page));
    }
    ss << " [" << m_buildTalentSource << "] | Gear: " << m_buildGearSource;

    ChatHandler(pLeader).SendSysMessage(ss.str().c_str());
}

void PartyBotAI::LearnPremadeSpecForClass()
{
    if (m_level > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        me->GiveLevel(sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL));
    else
        me->GiveLevel(m_level);
    me->InitTalentForLevel();
    me->SetUInt32Value(PLAYER_XP, 0);

    // PartyBot.AutoEquip: 2 = premade templates (random fallback), 1 = random, 0 = starting items
    uint32 const gearMode = sWorld.getConfig(CONFIG_UINT32_PARTY_BOT_AUTO_EQUIP);
    bool gearApplied = false;
    m_buildTalentSource = "random";
    m_buildGearSource = "";

    // Templates start at level 5 (also keeps the level - N loops below from underflowing)
    if (gearMode == PLAYER_BOT_AUTO_EQUIP_PREMADE_GEAR && m_level >= 5)
    {
        uint8 level = m_level;

        std::vector<PlayerPremadeSpecTemplate> vSpecs;
        std::vector<PlayerPremadeSpecTemplate> vSpecsAux;
        std::vector<PlayerPremadeGearTemplate> vGears;
        std::vector<PlayerPremadeGearTemplate> vGearsAux;
        std::vector<PlayerPremadeGearTemplate> vGearsAux2;

        PlayerPremadeSpecTemplate spec;
        PlayerPremadeGearTemplate gear;

        // Try to find spec to player - Maximum 3 level diference
        while (vSpecs.empty() && level >= (m_level - 3))
        {
            for (const auto& itr : sObjectMgr.GetPlayerPremadeSpecTemplates())
            {
                if (itr.second.requiredClass == me->GetClass() &&
                    itr.second.level == level)
                {
                    if (itr.second.role == m_role)
                        vSpecs.push_back(itr.second);
                    else if(vSpecs.empty())
                        vSpecsAux.push_back(itr.second);
                }
            }
            if (vSpecs.empty() && !vSpecsAux.empty())
                vSpecs = vSpecsAux;
            level--;
        }

        if (!vSpecs.empty())
        {
            // Select random spec template
            spec = SelectRandomContainerElement(vSpecs);
            // Apply selected spec template
            sObjectMgr.ApplyPremadeSpecTemplateToPlayer(spec.entry, me);
            m_buildTalentSource = spec.name;

            // Now try to find gear for selected spec
            level = m_level;
            while (vGears.empty() && level >= (m_level - 4))
            {
                for (const auto& itr : sObjectMgr.GetPlayerPremadeGearTemplates())
                {
                    if (itr.second.requiredClass == spec.requiredClass &&
                        itr.second.level == level)
                    {
                        if (spec.name.size() <= itr.second.name.size() &&
                            strncmp(spec.name.c_str(), itr.second.name.c_str(), spec.name.length()) == 0)
                            vGears.push_back(itr.second);
                        else if (vGears.empty())
                        {
                            if (itr.second.role == spec.role)
                                vGearsAux.push_back(itr.second);
                            else if (vGearsAux.empty())
                                vGearsAux2.push_back(itr.second);
                        }
                    }
                }
                if (vGears.empty())
                {
                    if (!vGearsAux.empty())
                        vGears = vGearsAux;
                    else if (!vGearsAux2.empty())
                        vGears = vGearsAux2;
                }
                level--;
            }

            if (!vGears.empty())
            {
                // Select random gear template
                gear = SelectRandomContainerElement(vGears);
                // Apply selected gear template
                sObjectMgr.ApplyPremadeGearTemplateToPlayer(gear.entry, me);
                m_buildGearSource = gear.name;
                gearApplied = true;
            }
        }
    }

    // Points not covered by a premade spec (all of them when there is none) follow the role rules
    bool const hasFreePoints = me->GetFreeTalentPoints() > 0;
    LearnTalentsForRole();
    if (hasFreePoints && m_buildTalentSource != "random")
        m_buildTalentSource += " + random";

    if (!gearApplied)
    {
        if (gearMode == PLAYER_BOT_AUTO_EQUIP_STARTING_GEAR)
        {
            AutoEquipGear(PLAYER_BOT_AUTO_EQUIP_STARTING_GEAR);
            m_buildGearSource = "starting items";
        }
        else
        {
            // Random gear mode, or no premade gear template found
            AutoEquipGear(PLAYER_BOT_AUTO_EQUIP_RANDOM_GEAR);
            m_buildGearSource = "random";
        }
    }
}

Player* PartyBotAI::GetPartyLeader() const
{
    Group* pGroup = me->GetGroup();
    if (!pGroup)
        return nullptr;

    ObjectGuid leaderGuid = pGroup->GetLeaderGuid();
    if (leaderGuid == me->GetObjectGuid() && !me->InBattleGround())
        return nullptr;

    return ObjectAccessor::FindPlayerNotInWorld(m_leaderGuid);
}

void PartyBotAI::RunAwayFromTarget(Unit* pTarget, bool pFollowLeader, float pDist)
{
    float minLeadDist = pDist < 20.0f ? 20.0f : pDist;

    if (pFollowLeader)
    {
        if (Player* pLeader = GetPartyLeader())
        {
            if (!pLeader->IsDead() &&
                pLeader->IsInWorld() &&
                pLeader->GetMap() == me->GetMap())
            {
                float leaderDistance = me->GetDistance(pLeader);
                float leadToMonsDist = pLeader->GetDistance(pTarget);
                if (leaderDistance > minLeadDist || leadToMonsDist > minLeadDist)
                {
                    MoveToTarget(pLeader, 6.0f);
                    return;
                }
            }
        }
    }

    float distance = pDist - me->GetDistance(pTarget) + frand(0.0f,0.5f);
    if (distance < 0.0f)
        return;

    float angle = me->GetAngle(pTarget) - M_PI_F + frand(-0.5f, 0.5f);
    float x, y, z;

    me->GetNearPoint(me, x, y, z, 0.0f, distance, angle);

    me->GetMotionMaster()->Clear();
    me->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING);
}

void PartyBotAI::RunAwayFromObject(GameObject* pObject, float pDistance)
{

    std::list<GameObject*> lObjects;

    float x, y, z;
    float angle;
    bool directions[6] = { true, true, true, true, true, true };
    int direction = 0;
    std::vector<int> freeDirections;

    me->GetGameObjectListWithEntryInGrid(lObjects, pObject->GetEntry(), (pDistance*2));
    for (const auto& pGo : lObjects)
    {
        if (pGo->isSpawned())
        {
            float pGoAngle = (me->GetAngle(pGo));
            int direction = (int)((pGoAngle / 1.05) + 0.5f);
            switch (direction)
            {
                case 0:
                case 6:
                    directions[0] = false;
                case 1:
                    directions[1] = false;
                case 2:
                    directions[2] = false;
                case 3:
                    directions[3] = false;
                case 4:
                    directions[4] = false;
                case 5:
                    directions[5] = false;
            }
        }
    }

    for (int i = 0; i < 6; i++)
    {
        if (directions[i])
        {
            freeDirections.push_back(i);
            if (i == 1 || i == 5)
            {
                direction = i;
                freeDirections.clear();
                break;
            }
            
        }
    }

    if (!freeDirections.empty())
    {
        direction = SelectRandomContainerElement(freeDirections);
    }

    angle = (direction * 1.05) + frand(-0.1f, 0.1f);

    me->GetNearPoint(me, x, y, z, 0.0f, pDistance, angle);

    if (me->IsMoving())
        me->StopMoving();
    me->GetMotionMaster()->Clear();
    me->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING);
}

void PartyBotAI::RunAwayFromAOE(float pDistance)
{
    float x, y, z;
    float angle = frand(1.0f, 2.0f);
    if (urand(0, 1))
        angle *= -1.0f;

    me->GetNearPoint(me, x, y, z, 0.0f, pDistance, angle);

    me->GetMotionMaster()->Clear();
    me->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING);
}


void PartyBotAI::MoveToTarget(Unit* pTarget, float pDistance)
{
    float x, y, z;
    float distance;
    float angle = frand(PB_MIN_FOLLOW_ANGLE, PB_MAX_FOLLOW_ANGLE);
    if (pDistance > 1.0)
        distance = frand(0.5f, pDistance);
    else
        distance = pDistance;

    pTarget->GetNearPoint(me, x, y, z, 0, distance, angle);

    me->GetMotionMaster()->Clear();
    me->GetMotionMaster()->MovePoint(0,x,y,z,MOVE_PATHFINDING);
}

void PartyBotAI::MoveToTargetDistance(Unit* pTarget, float pDistance)
{
    float x, y, z;
    float angle = me->GetAngle(pTarget);
    float distance = me->GetDistance(pTarget) - pDistance + frand(-0.25f, 0.25f);

    if (distance < 0.0f)
    {
        angle += M_PI_F;
        distance *= -1.0f;
    }

    me->GetNearPoint(me, x, y, z, 0, distance, angle);

    me->GetMotionMaster()->Clear();
    me->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING);
}

void PartyBotAI::ChaseTarget(Unit* pTarget)
{
    float casterDistance = pTarget->GetObjectBoundingRadius() + 25.0f;

    if (m_role == ROLE_RANGE_DPS || m_role == ROLE_HEALER)
        me->SetCasterChaseDistance(casterDistance);
    else
        me->SetCasterChaseDistance(0.0f);

    float distance = m_role == ROLE_TANK ? 1.0f : pTarget->GetMeleeReach();

    if (distance > 3.0f)
        distance *= frand(0.75f, 0.9f);

    me->GetMotionMaster()->MoveChase(pTarget, distance);
}

bool PartyBotAI::DrinkAndEat()
{
    if (m_isBuffing)
        return false;

    if (me->GetVictim())
        return false;

    bool const needToEat = me->GetHealthPercent() < 90.0f;
    bool const needToDrink = (me->GetPowerType() == POWER_MANA) && (me->GetPowerPercent(POWER_MANA) < 85.0f);

    if (!needToEat && !needToDrink)
        return false;

    bool const isEating = me->HasAura(PB_SPELL_FOOD);
    bool const isDrinking = me->HasAura(PB_SPELL_DRINK) || me->HasAura(PB_SPELL_DRINK_50);

    if (!isEating && needToEat)
    {
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType())
        {
            me->StopMoving();
            me->GetMotionMaster()->MoveIdle();
        }
        if (SpellEntry const* pSpellEntry = sSpellMgr.GetSpellEntry(PB_SPELL_FOOD))
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
            me->StopMoving();
            me->GetMotionMaster()->MoveIdle();
        }
        if (me->GetLevel() >= 50)
        {
            if (SpellEntry const* pSpellEntry = sSpellMgr.GetSpellEntry(PB_SPELL_DRINK_50))
            {
                me->CastSpell(me, pSpellEntry, true);
                me->RemoveSpellCooldown(pSpellEntry);
            }
        }
        else
        {
            if (SpellEntry const* pSpellEntry = sSpellMgr.GetSpellEntry(PB_SPELL_DRINK))
            {
                me->CastSpell(me, pSpellEntry, true);
                me->RemoveSpellCooldown(pSpellEntry);
            }
        }
        return true;
    }

    return needToEat || needToDrink;
}

bool PartyBotAI::ShouldAutoRevive() const
{
    if (me->GetDeathState() == DEAD)
        return true;

    Player* pLeader = GetPartyLeader();
    if (!pLeader || !pLeader->IsAlive())
        return false;

    if (pLeader->IsInWorld() &&
        pLeader->IsAlive() &&
        !pLeader->IsInCombat() &&
        (pLeader->GetMap() != me->GetMap() ||
         m_ressTimer > (2 * MINUTE * IN_MILLISECONDS)))
        return true;

    return false;
}


bool PartyBotAI::CanTryToCastSpell(Unit const* pTarget, SpellEntry const* pSpellEntry, uint32 maxStack) const
{
    if (!CombatBotBaseAI::CanTryToCastSpell(pTarget, pSpellEntry, maxStack))
        return false;

    // Ported from upstream: no harmful AoE while there are CC marks or if it would pull aggro
    if (pSpellEntry->IsAreaOfEffectSpell() && !pSpellEntry->IsPositiveSpell() && !IsInDuel())
    {
        if (!m_marksToCC.empty())
            return false;

        if (m_role != ROLE_TANK)
        {
            float radius;
            if (pSpellEntry->EffectRadiusIndex[0])
                radius = Spells::GetSpellRadius(sSpellRadiusStore.LookupEntry(pSpellEntry->EffectRadiusIndex[0]));
            else if (pSpellEntry->EffectRadiusIndex[1])
                radius = Spells::GetSpellRadius(sSpellRadiusStore.LookupEntry(pSpellEntry->EffectRadiusIndex[1]));
            else if (pSpellEntry->EffectRadiusIndex[2])
                radius = Spells::GetSpellRadius(sSpellRadiusStore.LookupEntry(pSpellEntry->EffectRadiusIndex[2]));
            else
                radius = 10.0f;

            std::list<Unit*> targets;
            me->GetEnemyListInRadiusAround(pTarget, radius, targets);

            for (auto const& pEnemy : targets)
            {
                if (((pEnemy->GetLevel() + 5) > me->GetLevel()) &&
                    ((pEnemy->GetHealth() * 4) > me->GetHealth()) &&
                    pEnemy->GetVictim() && pEnemy->GetVictim() != me &&
                    pEnemy->IsValidAttackTarget(me) &&
                    pEnemy->CanHaveThreatList())
                {
                    float const myThreat = pEnemy->GetThreatManager().getThreat(me);
                    float const victimThreat = pEnemy->GetThreatManager().getThreat(pEnemy->GetVictim());

                    if (victimThreat < (myThreat + me->GetMaxHealth()))
                        return false;
                }
            }
        }
    }

    return true;
}

bool PartyBotAI::CanUseCrowdControl(SpellEntry const* pSpellEntry, Unit* pTarget) const
{
    if (IsInDuel())
        return true;

    if ((pSpellEntry->AuraInterruptFlags & AURA_INTERRUPT_DAMAGE_CANCELS) &&
        AreOthersOnSameTarget(pTarget->GetObjectGuid()))
        return false;

    if (pSpellEntry->HasSingleTargetAura())
    {
        auto const& singleAuras = me->GetSingleCastSpellTargets();
        if (singleAuras.find(pSpellEntry) != singleAuras.end())
            return false;
    }

    return true;
}

bool PartyBotAI::AttackStart(Unit* pVictim)
{
    m_isBuffing = false;

    if (me->IsMounted())
        me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);

    if (me->Attack(pVictim, true))
    {
        ChaseTarget(pVictim);
        return true;
    }

    return false;
}

Unit* PartyBotAI::GetMarkedTarget(RaidTargetIcon mark) const
{
    ObjectGuid targetGuid = me->GetGroup()->GetTargetWithIcon(mark);
    if (targetGuid.IsUnit())
        return me->GetMap()->GetUnit(targetGuid);

    return nullptr;
}


Unit* PartyBotAI::SelectAttackTarget() const
{
    // Ported from upstream: fight only the duel opponent while dueling
    if (IsInDuel())
    {
        if (me->m_duel->opponent && IsValidHostileTarget(me->m_duel->opponent))
            return me->m_duel->opponent;
        return nullptr;
    }

    // Who is attacking me.
    for (const auto pAttacker : me->GetAttackers())
    {
        if (IsValidHostileTarget(pAttacker))
            return pAttacker;
    }

    // Who is the leader attacking.
    if (Player* pLeader = GetPartyLeader())
    {
        if (Unit* pVictim = pLeader->GetVictim())
        {
            // Stick to marked target in combat.
            for (auto markId : m_marksToFocus)
            {
                ObjectGuid targetGuid = me->GetGroup()->GetTargetWithIcon(markId);
                if (targetGuid.IsUnit())
                    if (Unit* pVictim = me->GetMap()->GetUnit(targetGuid))
                        if (IsValidHostileTarget(pVictim))
                            return pVictim;
            }

            if (pLeader->IsInCombat() &&
                IsValidHostileTarget(pVictim))
                return pVictim;
        }
    }

    // Assist Pet
    if (Pet* pPet = me->GetPet())
    {
        if (Unit* pVictim = pPet->GetVictim())
            if (IsValidHostileTarget(pVictim))
                return pVictim;
        for (const auto pAttacker : pPet->GetAttackers())
        {
            if (IsValidHostileTarget(pAttacker))
                return pAttacker;
        }
    }

    // Check if other group members are under attack.
    if (Unit* pPartyAttacker = SelectPartyAttackTarget())
        return pPartyAttacker;
    
    return nullptr;
}

Unit* PartyBotAI::SelectPartyAttackTarget() const
{
    std::vector<Unit*> vAttackers;
    Group* pGroup = me->GetGroup();
    
    for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        if (Player* pMember = itr->getSource())
        {
            // We already checked self.
            if (pMember == me)
                continue;

            for(const auto pAttacker : pMember->GetAttackers())
            {
                if (pAttacker->IsPlayer() ||
                    !IsValidHostileTarget(pAttacker) ||
                    !me->IsWithinDist(pAttacker, 120.0f))
                    continue;

                if (m_role == ROLE_TANK && IsTank(pMember))
                {
                    if (pMember->GetVictim() != pAttacker)
                        vAttackers.push_back(pAttacker);
                }
                else
                    vAttackers.push_back(pAttacker);
            }

            if (Pet* pPet = pMember->GetPet())
            {
                for (const auto pAttacker : pPet->GetAttackers())
                {
                    if (IsValidHostileTarget(pAttacker) &&
                        me->IsWithinDist(pAttacker, 120.0f))
                        vAttackers.push_back(pAttacker);
                }
            }
        }
    }

    if (!vAttackers.empty())
    {
        uint8 rand = urand(0, (vAttackers.size() - 1));
        return vAttackers[rand];
    }

    return nullptr;
}


Unit* PartyBotAI::SelectSpellTargetDifferentFrom(SpellEntry const* pSpellEntry, Unit* pVictim, float distance) const
{
    if (!IsSpellReady(pSpellEntry))
        return nullptr;

    std::list<Unit*> targets;
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck u_check(pVictim, me, distance);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(targets, u_check);
    Cell::VisitAllObjects(me, searcher, distance);

    // remove current target
    if (pVictim)
        targets.remove(pVictim);

    // remove not LoS targets, not valid target, or immune target
    for (std::list<Unit*>::iterator tIter = targets.begin(); tIter != targets.end();)
    {
        if ((!me->IsWithinLOSInMap(*tIter)) || !me->IsValidAttackTarget(*tIter) || !CanTryToCastSpell(*tIter,pSpellEntry))
        {
            std::list<Unit*>::iterator tIter2 = tIter;
            ++tIter;
            targets.erase(tIter2);
        }
        else
            ++tIter;
    }

    // no appropriate targets
    if (targets.empty())
        return nullptr;

    // select random
    uint32 rIdx = urand(0, targets.size() - 1);
    std::list<Unit*>::const_iterator tcIter = targets.begin();
    for (uint32 i = 0; i < rIdx; ++i)
        ++tcIter;

    return *tcIter;
}

Player* PartyBotAI::SelectResurrectionTarget() const
{
    if (IsInDuel())
        return nullptr;

    std::vector<Player*> vRessTargets;
    Player* pTarget = nullptr;

    Group* pGroup = me->GetGroup();
    for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        if (Player* pMember = itr->getSource())
        {
            // Can't resurrect self.
            if (pMember == me)
                continue;

            if (pMember->GetDeathState() == CORPSE)
                vRessTargets.push_back(pMember);
        }
    }
    if (!vRessTargets.empty())
        pTarget = SelectRandomContainerElement(vRessTargets);

    return pTarget;
}

Player* PartyBotAI::SelectShieldTarget() const
{
    if (IsInDuel())
        return nullptr;

    Group* pGroup = me->GetGroup();
    for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        if (Player* pMember = itr->getSource())
        {
            // We already checked self.
            if (pMember == me)
                continue;

            if ((IsValidHealTarget(pMember,50.0f)) &&
                !pMember->GetAttackers().empty() &&
                !pMember->IsImmuneToMechanic(MECHANIC_SHIELD))
                return pMember;
        }
    }

    return nullptr;
}

bool PartyBotAI::CrowdControlMarkedTargets()
{
    if (IsInDuel())
        return false;

    SpellEntry const* pSpellEntry = GetCrowdControlSpell();
    if (!pSpellEntry)
        return false;

    for (auto mark : m_marksToCC)
    {
        if (Unit* pTarget = GetMarkedTarget(mark))
        {
            if (!pTarget->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL) &&
                IsValidHostileTarget(pTarget) && !AreOthersOnSameTarget(pTarget->GetObjectGuid()))
            {
                if (CanTryToCastSpell(pTarget, pSpellEntry))
                {
                    if (DoCastSpell(pTarget, pSpellEntry) == SPELL_CAST_OK)
                    {
                        me->ClearUnitState(UNIT_STATE_MELEE_ATTACKING);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

void PartyBotAI::AddToPlayerGroup()
{
    Player* pPlayer = ObjectAccessor::FindPlayer(m_leaderGuid);
    if (!pPlayer)
        return;

    Group* group = pPlayer->GetGroup();
    if (!group)
    {
        group = new Group;
        // new group: if can't add then delete
        if (!group->Create(pPlayer->GetObjectGuid(), pPlayer->GetName()))
        {
            delete group;
            return;
        }
        sObjectMgr.AddGroup(group);
    }

    group->AddMember(me->GetObjectGuid(), me->GetName());
}

uint32 PartyBotAI::GetMountSpellId() const
{
    if (me->GetLevel() >= 60)
    {
        if (me->GetClass() == CLASS_PALADIN)
            return PB_SPELL_MOUNT_60_PALADIN;
        if (me->GetClass() == CLASS_WARLOCK)
            return PB_SPELL_MOUNT_60_WARLOCK;

        switch (me->GetRace())
        {
        case RACE_HUMAN:
            return PB_SPELL_MOUNT_60_HUMAN;
        case RACE_NIGHTELF:
            return PB_SPELL_MOUNT_60_NELF;
        case RACE_DWARF:
            return PB_SPELL_MOUNT_60_DWARF;
        case RACE_GNOME:
            return PB_SPELL_MOUNT_60_GNOME;
        case RACE_TROLL:
            return PB_SPELL_MOUNT_60_TROLL;
        case RACE_ORC:
            return PB_SPELL_MOUNT_60_ORC;
        case RACE_TAUREN:
            return PB_SPELL_MOUNT_60_TAUREN;
        case RACE_UNDEAD:
            return PB_SPELL_MOUNT_60_UNDEAD;
        }
    }
    else if (me->GetLevel() >= 40)
    {
        if (me->GetClass() == CLASS_PALADIN)
            return PB_SPELL_MOUNT_40_PALADIN;
        if (me->GetClass() == CLASS_WARLOCK)
            return PB_SPELL_MOUNT_40_WARLOCK;

        switch (me->GetRace())
        {
        case RACE_HUMAN:
            return PB_SPELL_MOUNT_40_HUMAN;
        case RACE_NIGHTELF:
            return PB_SPELL_MOUNT_40_NELF;
        case RACE_DWARF:
            return PB_SPELL_MOUNT_40_DWARF;
        case RACE_GNOME:
            return PB_SPELL_MOUNT_40_GNOME;
        case RACE_TROLL:
            return PB_SPELL_MOUNT_40_TROLL;
        case RACE_ORC:
            return PB_SPELL_MOUNT_40_ORC;
        case RACE_TAUREN:
            return PB_SPELL_MOUNT_40_TAUREN;
        case RACE_UNDEAD:
            return PB_SPELL_MOUNT_40_UNDEAD;
        }
    }

    return 0;
}

void PartyBotAI::OnPacketReceived(WorldPacket const* packet)
{
    switch (packet->GetOpcode())
    {
        // Ported from upstream: partybots accept duel requests
        case SMSG_DUEL_REQUESTED:
        {
            auto data = std::make_unique<WorldPackets::Duel::DuelAccepted>();
            data->playerGuid = me->GetObjectGuid();
            me->GetSession()->QueuePacket(std::move(data));
            return;
        }
        // Ported from upstream: hand round-robin loot from bots back to real players
        case SMSG_PARTYKILLLOG:
        {
            if (!me)
                return;

            if (Group const* pGroup = me->GetGroup())
            {
                if (pGroup->GetLootMethod() == ROUND_ROBIN ||
                    pGroup->GetLootMethod() == GROUP_LOOT ||
                    pGroup->GetLootMethod() == NEED_BEFORE_GREED)
                {
                    ObjectGuid victimGuid = *(((uint64*)(*packet).contents()) + 1);
                    if (Creature* pCreature = me->GetMap()->GetCreature(victimGuid))
                    {
                        pCreature->m_Events.AddLambdaEventAtOffset([pCreature, guid = me->GetGUID()]()
                        {
                            if (pCreature->loot.roundRobinPlayer == guid)
                            {
                                // unassign loot from bot so real players can loot
                                pCreature->loot.roundRobinPlayer = 0;
                                pCreature->ForceValuesUpdateAtIndex(UNIT_DYNAMIC_FLAGS);
                            }
                        }, 1);
                    }
                }
            }
            return;
        }
    }

    CombatBotBaseAI::OnPacketReceived(packet);
}

void PartyBotAI::OnPlayerLogin()
{
    if (!m_initialized)
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING);
}

void PartyBotAI::UpdateAI(uint32 const diff)
{
    m_updateTimer.Update(diff);
    if (me->IsDead())
        m_ressTimer += diff;
    if (m_spellTimer1 > 0)
    {
        if (diff >= m_spellTimer1 || m_spellTimer1 > 35000)
            m_spellTimer1 = 0;
        else
            m_spellTimer1 -= diff;
    }
    if (m_aoeSpellTimer > 0)
    {
        if (diff >= m_aoeSpellTimer || m_aoeSpellTimer > 35000)
            m_aoeSpellTimer = 0;
        else
            m_aoeSpellTimer -= diff;
    }
    if (m_threatCheckTimer > 0)
    {
        if (diff >= m_threatCheckTimer || m_threatCheckTimer > 3000)
            m_threatCheckTimer = 0;
        else
            m_threatCheckTimer -= diff;
    }

    if (m_updateTimer.Passed())
        m_updateTimer.Reset(PB_UPDATE_INTERVAL);
    else
        return;

    if (!me->IsInWorld() || me->IsBeingTeleported())
        return;

    if (!m_initialized)
    {
        AddToPlayerGroup();

        if (!m_cloneGuid.IsEmpty())
            CloneFromPlayer(sObjectAccessor.FindPlayer(m_cloneGuid));
        else
            LearnPremadeSpecForClass();

        if (m_role == ROLE_INVALID)
            AutoAssignRole();

        if (m_cloneGuid.IsEmpty())
            ReportBuildToLeader();

        ResetSpellData();
        PopulateSpellData();
        AddAllSpellReagents();
        me->UpdateSkillsToMaxSkillsForLevel();
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING);
        SummonPetIfNeeded();
        PopulateConsumableSpellData();

        uint32 newzone, newarea;
        me->GetZoneAndAreaId(newzone, newarea);
        me->UpdateZone(newzone, newarea);

        m_initialized = true;
        return;
    }

    Player* pLeader = GetPartyLeader();
    if (!pLeader)
    {
        botEntry->requestRemoval = true;
        return;
    }

    if (!pLeader->IsInWorld())
        return;

    if (pLeader->InBattleGround() &&
        !me->InBattleGround())
    {
        if (m_receivedBgInvite)
        {
            SendBattlefieldPortPacket();
            m_receivedBgInvite = false;
            return;
        }
        
        // Remain idle until we can join battleground.
        return;
    }
    
    if (pLeader->IsTaxiFlying() || pLeader->GetTransport())
    {
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType())
            me->GetMotionMaster()->MoveIdle();
        return;
    }

    if (me->HasAuraType(SPELL_AURA_FEIGN_DEATH))
    {
        if (me->GetEnemyCountInRadiusAround(me, 20.0f) > 0)
            return;
        else
            me->RemoveSpellsCausingAura(SPELL_AURA_FEIGN_DEATH);
    }

    if (me->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL))
    {
        // Ported from upstream: use trinkets / class abilities to break CC on self
        BreakCrowdControlEffects();
        return;
    }

    if (me->IsDead())
    {
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
            if (ShouldAutoRevive())
            {
                m_ressTimer = 0;
                me->SetCheatGod(true);
                me->ResurrectPlayer(0.5f);
                me->SpawnCorpseBones();
                me->CastSpell(me, PB_SPELL_HONORLESS_TARGET, true);
                char name[128] = {};
                strcpy(name, pLeader->GetName());
                ChatHandler(me).HandleGonameCommand(name);
            }
        }
        
        return;
    }
    else if (m_ressTimer > 0) {
        m_ressTimer = 0;
    }

    if (me->GetInvincibilityHpThreshold())
        me->SetCheatGod(false);

    // Ported from upstream: interrupt pre-casted heals if the target is no longer injured
    if (Spell* pCurrentSpell = me->GetCurrentSpell(CURRENT_GENERIC_SPELL))
    {
        if (pCurrentSpell->getState() == SPELL_STATE_PREPARING &&
            pCurrentSpell->m_spellInfo->IsHealSpell())
        {
            if (Unit* pTarget = pCurrentSpell->m_targets.getUnitTarget())
            {
                if (pTarget->GetHealth() == pTarget->GetMaxHealth())
                    me->InterruptSpell(CURRENT_GENERIC_SPELL, true);
            }
        }
    }

    if (me->IsNonMeleeSpellCasted(false, false, true))
        return;

    if (me->GetTargetGuid() == me->GetObjectGuid())
        me->ClearTarget();

    // First, check if there are enemies available
    Unit* pVictim;
    if (m_role == ROLE_HEALER)
        pVictim = SelectAttackTarget();
    else
        pVictim = me->GetVictim();

    if (m_role != ROLE_HEALER && !pLeader->IsMounted())
    {
        if (!pVictim || pVictim->IsDead() || pVictim->GetHealth() == 0.0f || pVictim->HasBreakableByDamageCrowdControlAura() || !IsValidHostileTarget(pVictim))
        {
            // Force select new Victim if current should not be attacked
            if (Unit* pNewVictim = SelectAttackTarget())
            {
                AttackStart(pNewVictim);
                return;
            }   
        }

        if (pVictim && !me->HasInArc(pVictim, 2 * M_PI_F / 3) && !me->IsMoving())
        {
            me->SetInFront(pVictim);
            me->SendMovementPacket(MSG_MOVE_SET_FACING, false);
        }
    }

    if (!pVictim)
    {
        if(me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
            me->InterruptSpell(CURRENT_AUTOREPEAT_SPELL, true);
    }

    // Engage combat ASAP
    if (!pLeader->IsMounted() && (pVictim || me->IsInCombat()))
    {
        UpdateInCombatAI();
        return;
    }

    // If channeling object, do not move
    if (me->GetChannelObjectGuid())
        return;

    if (!pVictim && !me->IsInCombat())
    {
        float leaderDistance = 0.0f;

        // Check if should run or teleport to Leader
        if (!pLeader->IsDead())
        {
            leaderDistance = me->GetDistance(pLeader);

            // Check if is inside BG to avoid some inconsistent actions
            if (!me->InBattleGround())
            {

                // Remove Stealth if geting far behind the Leader
                if (leaderDistance > (PB_MAX_FOLLOW_DIST * 2.5f) &&
                    me->HasAuraType(SPELL_AURA_MOD_STEALTH) &&
                    !pLeader->GetVictim())
                {
                    me->RemoveSpellsCausingAura(SPELL_AURA_MOD_STEALTH);
                }

                // Teleport to leader if too far away.
                if (leaderDistance > (PB_MAX_FOLLOW_DIST * 15.0f) || me->GetDistanceZ(pLeader) > (PB_MAX_FOLLOW_DIST * 3.2f))
                {
                    if (!me->IsStopped())
                        me->StopMoving();
                    me->GetMotionMaster()->Clear();
                    me->GetMotionMaster()->MoveIdle();
                    char name[128] = {};
                    strcpy(name, pLeader->GetName());
                    ChatHandler(me).HandleGonameCommand(name);
                    return;
                }
            }

            if (leaderDistance > (PB_MAX_FOLLOW_DIST * 1.1f))
            {
                if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                    me->GetMotionMaster()->MoveFollow(pLeader, urand(PB_MIN_FOLLOW_DIST, PB_MAX_FOLLOW_DIST), frand(PB_MIN_FOLLOW_ANGLE, PB_MAX_FOLLOW_ANGLE));
                return;
            }
        }
        

        if (!me->IsMounted())
        {
            if (DrinkAndEat())
                return;

            UpdateOutOfCombatAI();

            if (m_isBuffing)
                return;

            if (me->IsNonMeleeSpellCasted())
                return;
        }

        // Mount if leader is mounted.
        if (pLeader->IsMounted())
        {
            if (!me->IsMounted())
            {
                if (me->HasAuraType(SPELL_AURA_MOD_STEALTH))
                    me->RemoveSpellsCausingAura(SPELL_AURA_MOD_STEALTH);
                if (me->GetShapeshiftForm() != FORM_NONE)
                    me->RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);
                auto auraList = pLeader->GetAurasByType(SPELL_AURA_MOUNTED);
                if (!auraList.empty())
                {
                    me->SetCheatOption(PLAYER_CHEAT_NO_CAST_TIME, true);
                    me->CastSpell(me, GetMountSpellId(), true);
                    me->SetCheatOption(PLAYER_CHEAT_NO_CAST_TIME, false);
                }
            }
        }
        else if (me->IsMounted())
            me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
    }

    if (me->GetStandState() != UNIT_STAND_STATE_STAND)
        me->SetStandState(UNIT_STAND_STATE_STAND);

    if (!pLeader->IsDead() && !me->IsMoving() && !pVictim && me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
        me->GetMotionMaster()->MoveFollow(pLeader, urand(PB_MIN_FOLLOW_DIST, PB_MAX_FOLLOW_DIST), frand(PB_MIN_FOLLOW_ANGLE, PB_MAX_FOLLOW_ANGLE));
}

void PartyBotAI::UpdateOutOfCombatAI()
{
    if (m_resurrectionSpell)
        if (Player* pTarget = SelectResurrectionTarget())
            if (CanTryToCastSpell(pTarget, m_resurrectionSpell))
                if (DoCastSpell(pTarget, m_resurrectionSpell) == SPELL_CAST_OK)
                    return;

    if (m_elixirSpell &&
        !me->HasAura(m_elixirSpell->Id))
    {
        if (CanTryToCastSpell(me, m_elixirSpell))
        {
            if (DoCastSpell(me, m_elixirSpell) == SPELL_CAST_OK)
                return;
        }
    }

    if (m_flaskSpell &&
        me->GetGroup()->isRaidGroup() &&
        !me->HasAura(m_flaskSpell->Id))
    {
        if (CanTryToCastSpell(me, m_flaskSpell))
        {
            if (DoCastSpell(me, m_flaskSpell) == SPELL_CAST_OK)
                return;
        }
    }

    if (m_role != ROLE_TANK && me->GetVictim() && CrowdControlMarkedTargets())
        return;

    // Ported from upstream: dispels take priority over class logic
    if (CheckForDispelTargets())
        return;

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

void PartyBotAI::UpdateInCombatAI()
{
    Unit* pVictim = me->GetVictim();
    Unit* pLeader = GetPartyLeader();
    bool pCombatEngagementReady;

    // Get up if not stand position
    if (me->GetStandState() != UNIT_STAND_STATE_STAND)
        me->SetStandState(UNIT_STAND_STATE_STAND);

    // Now check special Instance Mechanics
    if (!CheckCombatInstanceMechanics(pCombatEngagementReady))
        return;

    // Use Bandage
    if (m_bandage &&
        m_role != ROLE_HEALER &&
        m_role != ROLE_TANK &&
        me->GetHealDirectTargetTimer() <= 0 &&
        me->GetHealthPercent() <= 50.0f &&
        CanTryToCastSpell(me, m_bandage))
    {
        if (DoCastSpell(me, m_bandage) == SPELL_CAST_OK)
            return;
    }

    // Use potions
    // Restorative Potion
    if (m_restPotion &&
        IsValidDispelTarget(me, m_restPotion) &&
        CanTryToCastSpell(me, m_restPotion))
    {
        if (DoCastSpell(me, m_restPotion) == SPELL_CAST_OK)
            return;
    }

    // Emergency Healing Potion
    if (m_potionSpell &&
        (me->GetHealthPercent() <= 15.0f) &&
        CanTryToCastSpell(me, m_potionSpell))
    {
        if (DoCastSpell(me, m_potionSpell) == SPELL_CAST_OK)
            return;
    }

    // Check if combat engagement is ready
    if (!pCombatEngagementReady)
        return;

    // If role is Tank, try to find and defend party members.
    if (m_role == ROLE_TANK)
    {
        if (!pVictim || pVictim->GetVictim() == me)
        {
            if (pVictim = SelectPartyAttackTarget())
            {
                me->AttackStop(true);
                AttackStart(pVictim);
            }
        }

        // Taunt target if it is attacking someone else that is not a tank
        if (pVictim)
        {
            if (Unit* pVictimTarget = pVictim->GetVictim())
            {
                if(pVictimTarget->IsPlayer() &&
                    pVictimTarget != me &&
                    !IsTank(pVictimTarget->ToPlayer()))
                {
                    for (const auto& pSpellEntry : m_spellListTaunt)
                    {
                        if (CanTryToCastSpell(pVictim, pSpellEntry))
                        {
                            if (DoCastSpell(pVictim, pSpellEntry) == SPELL_CAST_OK)
                                return;
                        }
                    }
                }
            }
        }
    }

    // If healer, keep close to enemies to heal tank
    if (m_role == ROLE_HEALER)
    {
        if (Unit* pChaseTarget = SelectAttackTarget())
        {
            ChaseTarget(pChaseTarget);
            if (me->GetDistance(pChaseTarget) > 32.0f)
                MoveToTargetDistance(pChaseTarget, 25.0f);
        }
        if (pLeader && !pLeader->IsDead() && !pLeader->IsWithinLOSInMap(me))
            MoveToTarget(pLeader);
    }

    // If not Healer, force combat engagement
    if (pVictim && m_role != ROLE_HEALER)
    {
        if(!me->HasUnitState(UNIT_STATE_MELEE_ATTACKING) &&
            m_role != ROLE_HEALER &&
            IsValidHostileTarget(pVictim))
            AttackStart(pVictim);

        if (((m_role == ROLE_TANK || m_role == ROLE_MELEE_DPS) &&
            !pVictim->CanReachWithMeleeAutoAttack(me)) ||
            (m_role == ROLE_RANGE_DPS &&
                me->GetDistance(pVictim) > 28.0f))
        {
            ChaseTarget(pVictim);
        }
    }
    else if (CrowdControlMarkedTargets())
        return;

    // Ported from upstream: dispels take priority over class logic
    if (CheckForDispelTargets())
        return;

    // Now go to class combat logic
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
}

// Ported from upstream: high-priority dispels for every class
bool PartyBotAI::CheckForDispelTargets()
{
    if (me->GetShapeshiftForm() != FORM_NONE)
        return false;

    switch (me->GetClass())
    {
        case CLASS_PALADIN:
        {
            if (m_spells.paladin.pCleanse)
            {
                if (Unit* pFriend = SelectDispelTarget(m_spells.paladin.pCleanse))
                {
                    if (CanTryToCastSpell(pFriend, m_spells.paladin.pCleanse))
                    {
                        if (DoCastSpell(pFriend, m_spells.paladin.pCleanse) == SPELL_CAST_OK)
                            return true;
                    }
                }
            }
            break;
        }
        case CLASS_SHAMAN:
        {
            if (m_spells.shaman.pCureDisease)
            {
                if (Unit* pFriend = SelectDispelTarget(m_spells.shaman.pCureDisease))
                {
                    if (CanTryToCastSpell(pFriend, m_spells.shaman.pCureDisease))
                    {
                        if (DoCastSpell(pFriend, m_spells.shaman.pCureDisease) == SPELL_CAST_OK)
                            return true;
                    }
                }
            }

            if (m_spells.shaman.pCurePoison)
            {
                if (Unit* pFriend = SelectDispelTarget(m_spells.shaman.pCurePoison))
                {
                    if (CanTryToCastSpell(pFriend, m_spells.shaman.pCurePoison))
                    {
                        if (DoCastSpell(pFriend, m_spells.shaman.pCurePoison) == SPELL_CAST_OK)
                            return true;
                    }
                }
            }
            break;
        }
        case CLASS_MAGE:
        {
            if (m_spells.mage.pRemoveLesserCurse)
            {
                if (Unit* pFriend = SelectDispelTarget(m_spells.mage.pRemoveLesserCurse))
                {
                    if (CanTryToCastSpell(pFriend, m_spells.mage.pRemoveLesserCurse))
                    {
                        if (DoCastSpell(pFriend, m_spells.mage.pRemoveLesserCurse) == SPELL_CAST_OK)
                            return true;
                    }
                }
            }
            break;
        }
        case CLASS_PRIEST:
        {
            if (m_spells.priest.pDispelMagic)
            {
                if (Unit* pFriend = SelectDispelTarget(m_spells.priest.pDispelMagic))
                {
                    if (CanTryToCastSpell(pFriend, m_spells.priest.pDispelMagic))
                    {
                        if (DoCastSpell(pFriend, m_spells.priest.pDispelMagic) == SPELL_CAST_OK)
                            return true;
                    }
                }
            }
            if (m_spells.priest.pAbolishDisease)
            {
                if (Unit* pFriend = SelectDispelTarget(m_spells.priest.pAbolishDisease))
                {
                    if (CanTryToCastSpell(pFriend, m_spells.priest.pAbolishDisease))
                    {
                        if (DoCastSpell(pFriend, m_spells.priest.pAbolishDisease) == SPELL_CAST_OK)
                            return true;
                    }
                }
            }
            break;
        }
        case CLASS_DRUID:
        {
            SpellEntry const* pDispelSpell = m_spells.druid.pAbolishPoison ?
                m_spells.druid.pAbolishPoison :
                m_spells.druid.pCurePoison;

            if (pDispelSpell)
            {
                if (Unit* pFriend = SelectDispelTarget(pDispelSpell))
                {
                    if (CanTryToCastSpell(pFriend, pDispelSpell))
                    {
                        if (DoCastSpell(pFriend, pDispelSpell) == SPELL_CAST_OK)
                            return true;
                    }
                }
            }

            if (m_spells.druid.pRemoveCurse)
            {
                if (Unit* pFriend = SelectDispelTarget(m_spells.druid.pRemoveCurse))
                {
                    if (CanTryToCastSpell(pFriend, m_spells.druid.pRemoveCurse))
                    {
                        if (DoCastSpell(pFriend, m_spells.druid.pRemoveCurse) == SPELL_CAST_OK)
                            return true;
                    }
                }
            }
            break;
        }
    }

    return false;
}

void PartyBotAI::UpdateOutOfCombatAI_Paladin()
{
    if (m_spells.paladin.pAura &&
        CanTryToCastSpell(me, m_spells.paladin.pAura))
    {
        if (DoCastSpell(me, m_spells.paladin.pAura) == SPELL_CAST_OK)
            return;
    }

    if (m_role == ROLE_TANK &&
        m_spells.paladin.pRighteousFury &&
        CanTryToCastSpell(me, m_spells.paladin.pRighteousFury))
    {
        if (DoCastSpell(me, m_spells.paladin.pRighteousFury) == SPELL_CAST_OK)
            return;
    }

    if (m_spells.paladin.pBlessingBuff)
    {
        if (Player* pTarget = SelectBuffTarget(m_spells.paladin.pBlessingBuff, m_spells.paladin.pBlessingBuffRanged))
        {
            if(IsMeleeWeaponClass(pTarget->GetClass()) && CanTryToCastSpell(pTarget, m_spells.paladin.pBlessingBuff))
            { 
                if (DoCastSpell(pTarget, m_spells.paladin.pBlessingBuff) == SPELL_CAST_OK)
                {
                    m_isBuffing = true;
                    return;
                }
            }

            if (!IsMeleeWeaponClass(pTarget->GetClass()) && CanTryToCastSpell(pTarget, m_spells.paladin.pBlessingBuffRanged))
            {
                if (DoCastSpell(pTarget, m_spells.paladin.pBlessingBuffRanged) == SPELL_CAST_OK)
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

    if (m_spells.paladin.pCleanse)
    {
        if (Unit* pFriend = SelectDispelTarget(m_spells.paladin.pCleanse))
        {
            if (CanTryToCastSpell(pFriend, m_spells.paladin.pCleanse))
            {
                if (DoCastSpell(pFriend, m_spells.paladin.pCleanse) == SPELL_CAST_OK)
                    return;
            }
        }
    }

    if (m_role == ROLE_HEALER)
    {
        if (FindAndHealInjuredAlly(90.0f, 50.0f))
            return;
    }
}

void PartyBotAI::UpdateInCombatAI_Paladin()
{
    if (m_spells.paladin.pDivineShield &&
       (me->GetHealthPercent() < 25.0f) &&
       (m_role != ROLE_TANK) &&
        CanTryToCastSpell(me, m_spells.paladin.pDivineShield))
    {
        if (DoCastSpell(me, m_spells.paladin.pDivineShield) == SPELL_CAST_OK)
            return;
    }

    if (Unit* pFriend = me->FindLowestHpFriendlyUnit(30.0f, 70, true, me))
    {
        if (m_spells.paladin.pBlessingOfProtection &&
           !IsPhysicalDamageClass(pFriend->GetClass()) &&
            CanTryToCastSpell(pFriend, m_spells.paladin.pBlessingOfProtection))
        {
            if (DoCastSpell(pFriend, m_spells.paladin.pBlessingOfProtection) == SPELL_CAST_OK)
                return;
        }
        if (m_spells.paladin.pBlessingOfSacrifice &&
           (me->GetHealthPercent() > 80.0f) &&
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

    // Critical Healing
    if (m_role == ROLE_TANK && me->GetHealthPercent() < 35.0f)
    {
        HealInjuredTargetDirect(me);
        return;
    }
    else
    {
        if (FindAndHealInjuredAlly(35.0f, 35.0f))
            return;
    }

    // Dispel
    if (m_spells.paladin.pCleanse)
    {
        if (Unit* pFriend = SelectDispelTarget(m_spells.paladin.pCleanse))
        {
            if (CanTryToCastSpell(pFriend, m_spells.paladin.pCleanse))
            {
                if (DoCastSpell(pFriend, m_spells.paladin.pCleanse) == SPELL_CAST_OK)
                    return;
            }
        }
    }
    else if (m_spells.paladin.pPurify)
    {
        if (Unit* pFriend = SelectDispelTarget(m_spells.paladin.pPurify))
        {
            if (CanTryToCastSpell(pFriend, m_spells.paladin.pPurify))
            {
                if (DoCastSpell(pFriend, m_spells.paladin.pPurify) == SPELL_CAST_OK)
                    return;
            }
        }
    }

    if (!me->GetAttackers().empty())
    {
        if (m_spells.paladin.pHolyShield &&
            CanTryToCastSpell(me, m_spells.paladin.pHolyShield))
        {
            if (DoCastSpell(me, m_spells.paladin.pHolyShield) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.paladin.pTurnEvil &&
            m_role != ROLE_TANK)
        {
            Unit* pAttacker = SelectAttackerDifferentFrom(me->GetVictim());
            if (pAttacker && pAttacker->GetCreatureType() == CREATURE_TYPE_UNDEAD &&
                CanTryToCastSpell(pAttacker, m_spells.paladin.pTurnEvil))
            {
                if (DoCastSpell(pAttacker, m_spells.paladin.pTurnEvil) == SPELL_CAST_OK)
                    return;
            }
        }
    }

    if (m_role == ROLE_HEALER)
    {
        if (m_spells.paladin.pHolyShock &&
            me->GetHealthPercent() < 50.0f &&
            CanTryToCastSpell(me, m_spells.paladin.pHolyShock))
        {
            if (m_spells.paladin.pDivineFavor &&
                CanTryToCastSpell(me, m_spells.paladin.pDivineFavor))
            {
                DoCastSpell(me, m_spells.paladin.pDivineFavor);
            }

            if (DoCastSpell(me, m_spells.paladin.pHolyShock) == SPELL_CAST_OK)
                return;
        }

        if (FindAndHealInjuredAlly(95.0f, 50.0f))
            return;

        // Ported from upstream: pre-heal the ally taking the most incoming damage
        if (FindAndPreHealTarget())
            return;
    }
    else
    {

        if (Unit* pVictim = me->GetVictim())
        {
            
            bool const hasSeal = (m_spells.paladin.pSeal && me->HasAura(m_spells.paladin.pSeal->Id)) ||
                                 (m_spells.paladin.pSealOfWisdom && me->HasAura(m_spells.paladin.pSealOfWisdom->Id)) ||
                                 (m_spells.paladin.pSealOfCrusader && me->HasAura(m_spells.paladin.pSealOfCrusader->Id));

            if (!hasSeal)
            {
                if (me->GetPowerPercent(POWER_MANA) < 25.0f &&
                    m_spells.paladin.pSealOfWisdom &&
                    CanTryToCastSpell(me, m_spells.paladin.pSealOfWisdom))
                    me->CastSpell(me, m_spells.paladin.pSealOfWisdom, false);
                else if(m_spells.paladin.pSealOfCrusader &&
                    !(pVictim->HasAura(21183) || pVictim->HasAura(20303) || pVictim->HasAura(20302)
                        || pVictim->HasAura(20301) || pVictim->HasAura(20300) || pVictim->HasAura(20188) ) &&
                    CanTryToCastSpell(me, m_spells.paladin.pSealOfCrusader))
                    me->CastSpell(me, m_spells.paladin.pSealOfCrusader, false);
                else if (m_spells.paladin.pSeal &&
                    CanTryToCastSpell(me, m_spells.paladin.pSeal))
                    me->CastSpell(me, m_spells.paladin.pSeal, false);
            }

            if (hasSeal && m_spells.paladin.pJudgement &&
               (me->GetPowerPercent(POWER_MANA) > 30.0f) &&
                CanTryToCastSpell(pVictim, m_spells.paladin.pJudgement))
            {
                if (DoCastSpell(pVictim, m_spells.paladin.pJudgement) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.paladin.pHammerOfJustice &&
               (pVictim->IsNonMeleeSpellCasted() ||
               (me->GetHealthPercent() < 20.0f && !me->GetAttackers().empty())) &&
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

            if (m_spells.paladin.pConsecration &&
               ((me->GetEnemyCountInRadiusAround(me,10.0f) > 2) ||
                  (me->HasAura(20059) && pVictim->CanReachWithMeleeAutoAttack(me)) ) &&
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

            if (m_spells.paladin.pHolyWrath &&
                pVictim->IsCreature() &&
               (pVictim->GetCreatureType() == CREATURE_TYPE_UNDEAD ||
                pVictim->GetCreatureType() == CREATURE_TYPE_DEMON) &&
               (me->GetAttackers().size() < 3) && // too much pushback
                CanTryToCastSpell(pVictim, m_spells.paladin.pHolyWrath))
            {
                if (DoCastSpell(pVictim, m_spells.paladin.pHolyWrath) == SPELL_CAST_OK)
                    return;
            }
        }
    }

    if (m_spells.paladin.pBlessingOfFreedom &&
       (me->HasUnitState(UNIT_STATE_ROOT) || me->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED)) &&
        CanTryToCastSpell(me, m_spells.paladin.pBlessingOfFreedom))
    {
        if (DoCastSpell(me, m_spells.paladin.pBlessingOfFreedom) == SPELL_CAST_OK)
            return;
    }
    
}

void PartyBotAI::UpdateOutOfCombatAI_Shaman()
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

    if (m_role == ROLE_HEALER)
    {
        if(FindAndHealInjuredAlly(90.0f, 50.0f))
            return;
    }
}

void PartyBotAI::UpdateInCombatAI_Shaman()
{

    if (m_spells.shaman.pManaTideTotem &&
       (me->GetPowerPercent(POWER_MANA) < 50.0f) &&
        CanTryToCastSpell(me, m_spells.shaman.pManaTideTotem))
    {
        if (DoCastSpell(me, m_spells.shaman.pManaTideTotem) == SPELL_CAST_OK)
            return;
    }

    // Critical Healing
    if (FindAndHealInjuredAlly(35.0f, 35.0f))
        return;

    // Dispels
    if (m_spells.shaman.pCureDisease)
    {
        if (Unit* pFriend = SelectDispelTarget(m_spells.shaman.pCureDisease))
        {
            if (CanTryToCastSpell(pFriend, m_spells.shaman.pCureDisease))
            {
                if (DoCastSpell(pFriend, m_spells.shaman.pCureDisease) == SPELL_CAST_OK)
                    return;
            }
        }
    }

    if (m_spells.shaman.pCurePoison)
    {
        if (Unit* pFriend = SelectDispelTarget(m_spells.shaman.pCurePoison))
        {
            if (CanTryToCastSpell(pFriend, m_spells.shaman.pCurePoison))
            {
                if (DoCastSpell(pFriend, m_spells.shaman.pCurePoison) == SPELL_CAST_OK)
                    return;
            }
        }
    }

    if (m_role == ROLE_HEALER)
    {
        if (!FindAndHealInjuredAlly(95.0f, 50.0f))
            FindAndPreHealTarget(); // ported from upstream
        return;
    }
    else
    {
        if (Unit* pVictim = me->GetVictim())
        {
            if (SummonShamanTotems())
                return;

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
               (m_role == ROLE_RANGE_DPS || !me->CanReachWithMeleeAutoAttack(pVictim)) &&
                CanTryToCastSpell(pVictim, m_spells.shaman.pLightningBolt))
            {
                if (DoCastSpell(pVictim, m_spells.shaman.pLightningBolt) == SPELL_CAST_OK)
                    return;
            }
        }
    }

    if (SummonShamanTotems())
        return;

}

void PartyBotAI::UpdateOutOfCombatAI_Hunter()
{
    if (m_spells.hunter.pAspectOfTheHawk &&
        CanTryToCastSpell(me, m_spells.hunter.pAspectOfTheHawk))
    {
        if (DoCastSpell(me, m_spells.hunter.pAspectOfTheHawk) == SPELL_CAST_OK)
            return;
    }

    if (m_spells.hunter.pTrueshotAura &&
        CanTryToCastSpell(me, m_spells.hunter.pTrueshotAura))
    {
        if (DoCastSpell(me, m_spells.hunter.pTrueshotAura) == SPELL_CAST_OK)
        {
            m_isBuffing = true;
            return;
        }
    }

    SummonPetIfNeeded();
}

void PartyBotAI::UpdateInCombatAI_Hunter()
{
    if (Unit* pVictim = me->GetVictim())
    {
        if (!me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL) &&
            me->GetDistance(pVictim) <= 30.0f)
        {
            std::vector<const SpellEntry*> vTraps;

            if (m_spells.hunter.pFrosTrap &&
                CanTryToCastSpell(me, m_spells.hunter.pFrosTrap))
                vTraps.push_back(m_spells.hunter.pFrosTrap);
            if (m_spells.hunter.pFreezingTrap &&
                CanTryToCastSpell(me, m_spells.hunter.pFreezingTrap))
                vTraps.push_back(m_spells.hunter.pFreezingTrap);
            if (m_spells.hunter.pImmolationTrap &&
                CanTryToCastSpell(me, m_spells.hunter.pImmolationTrap))
                vTraps.push_back(m_spells.hunter.pImmolationTrap);
            if (m_spells.hunter.pExplosiveTrap &&
                CanTryToCastSpell(me, m_spells.hunter.pExplosiveTrap))
                vTraps.push_back(m_spells.hunter.pExplosiveTrap);

            if (!vTraps.empty())
            {
                const SpellEntry* pTrap = SelectRandomContainerElement(vTraps);
                if (DoCastSpell(me, pTrap) == SPELL_CAST_OK)
                    return;
            }
        }

        if (Pet* pPet = me->GetPet())
        {
            if (!pPet->GetVictim() || pPet->GetVictim() != pVictim)
            {
                pPet->GetCharmInfo()->SetIsCommandAttack(true);
                pPet->AI()->AttackStart(pVictim);
            }
        }

        if (m_spells.hunter.pHuntersMark &&
            CanTryToCastSpell(pVictim, m_spells.hunter.pHuntersMark))
        {
            if (DoCastSpell(pVictim, m_spells.hunter.pHuntersMark) == SPELL_CAST_OK)
                return;
        }

        if (me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL) &&
            me->GetCombatDistance(pVictim) < 8.0f)
            me->InterruptSpell(CURRENT_AUTOREPEAT_SPELL, true);

        if (me->HasSpell(PB_SPELL_AUTO_SHOT) &&
            !me->IsMoving() &&
            (me->GetCombatDistance(pVictim) >= 8.0f) &&
            !me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
            me->CastSpell(pVictim, PB_SPELL_AUTO_SHOT, false);

        // Remove Frenzy
        if (pVictim->HasAuraType(SPELL_AURA_MOD_MELEE_HASTE) &&
            m_spells.hunter.pTranquilizingShot &&
            CanTryToCastSpell(pVictim, m_spells.hunter.pTranquilizingShot))
        {
            if (DoCastSpell(pVictim, m_spells.hunter.pTranquilizingShot) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.hunter.pVolley &&
            m_aoeSpellTimer <= 0 &&
            pVictim->GetHealthPercent() < 75.0f &&
           (me->GetEnemyCountInRadiusAround(pVictim, 10.0f) > 3) &&
            CanTryToCastSpell(pVictim, m_spells.hunter.pVolley))
        {
            if (DoCastSpell(pVictim, m_spells.hunter.pVolley) == SPELL_CAST_OK)
                return;
        }

        if (pVictim->IsMoving() &&
            !pVictim->HasUnitState(UNIT_STATE_ROOT) &&
            !pVictim->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED))
        {
            if (m_spells.hunter.pConcussiveShot &&
                CanTryToCastSpell(pVictim, m_spells.hunter.pConcussiveShot))
            {
                if (DoCastSpell(pVictim, m_spells.hunter.pConcussiveShot) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.hunter.pIntimidation &&
                CanTryToCastSpell(pVictim, m_spells.hunter.pIntimidation))
            {
                if (DoCastSpell(pVictim, m_spells.hunter.pIntimidation) == SPELL_CAST_OK)
                    return;
            }
        }

        if (m_spells.hunter.pBestialWrath &&
            CanTryToCastSpell(pVictim, m_spells.hunter.pBestialWrath))
        {
            if (DoCastSpell(pVictim, m_spells.hunter.pBestialWrath) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.hunter.pRapidFire &&
            pVictim->GetHealth() > (2 * me->GetMaxHealth()) &&
            CanTryToCastSpell(pVictim, m_spells.hunter.pRapidFire))
        {
            if (DoCastSpell(pVictim, m_spells.hunter.pRapidFire) == SPELL_CAST_OK)
                return;
        }

        // Apply Sting
        if (m_spells.hunter.pViperSting &&
            pVictim->IsCaster() &&
            pVictim->GetPowerPercent(POWER_MANA) > 10.0f)
        {
            if (CanTryToCastSpell(pVictim, m_spells.hunter.pViperSting) &&
                DoCastSpell(pVictim, m_spells.hunter.pSerpentSting) == SPELL_CAST_OK)
                return;
        }
        else if (m_spells.hunter.pSerpentSting &&
            CanTryToCastSpell(pVictim, m_spells.hunter.pSerpentSting))
        {
            if (DoCastSpell(pVictim, m_spells.hunter.pSerpentSting) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.hunter.pArcaneShot &&
            CanTryToCastSpell(pVictim, m_spells.hunter.pArcaneShot))
        {
            if (DoCastSpell(pVictim, m_spells.hunter.pArcaneShot) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.hunter.pMultiShot &&
            m_aoeSpellTimer <= 0 &&
            CanTryToCastSpell(pVictim, m_spells.hunter.pMultiShot))
        {
            if (DoCastSpell(pVictim, m_spells.hunter.pMultiShot) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.hunter.pAimedShot &&
            CanTryToCastSpell(pVictim, m_spells.hunter.pAimedShot))
        {
            if (DoCastSpell(pVictim, m_spells.hunter.pAimedShot) == SPELL_CAST_OK)
                return;
        }

        if (GetAttackersInRangeCount(8.0f))
        {
            Unit* pAttacker = *me->GetAttackers().begin();

            if (m_spells.hunter.pDeterrence &&
                (me->GetHealthPercent() < 50.0f) &&
                CanTryToCastSpell(me, m_spells.hunter.pDeterrence))
            {
                if (DoCastSpell(me, m_spells.hunter.pDeterrence) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.hunter.pFeignDeath &&
                (me->GetHealthPercent() < 15.0f) &&
                CanTryToCastSpell(me, m_spells.hunter.pFeignDeath))
            {
                if (DoCastSpell(me, m_spells.hunter.pFeignDeath) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.hunter.pDisengage &&
                CanTryToCastSpell(pAttacker, m_spells.hunter.pDisengage))
            {
                if (DoCastSpell(pAttacker, m_spells.hunter.pDisengage) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.hunter.pAspectOfTheMonkey &&
                CanTryToCastSpell(me, m_spells.hunter.pAspectOfTheMonkey))
            {
                if (DoCastSpell(me, m_spells.hunter.pAspectOfTheMonkey) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.hunter.pScareBeast &&
                CanTryToCastSpell(pAttacker, m_spells.hunter.pScareBeast))
            {
                if (DoCastSpell(pAttacker, m_spells.hunter.pScareBeast) == SPELL_CAST_OK)
                    return;
            }

        }

        if (pVictim->CanReachWithMeleeAutoAttack(me))
        {
            if (m_spells.hunter.pCounterattack &&
                CanTryToCastSpell(pVictim, m_spells.hunter.pCounterattack))
            {
                if (DoCastSpell(pVictim, m_spells.hunter.pWingClip))
                    return;
            }

            if (m_spells.hunter.pWingClip &&
                CanTryToCastSpell(pVictim, m_spells.hunter.pWingClip))
            {
                if (DoCastSpell(pVictim, m_spells.hunter.pWingClip))
                    return;
            }

            if (m_spells.hunter.pMongooseBite &&
                CanTryToCastSpell(pVictim, m_spells.hunter.pMongooseBite))
            {
                if (DoCastSpell(pVictim, m_spells.hunter.pMongooseBite))
                    return;
            }

            if (m_spells.hunter.pRaptorStrike &&
                CanTryToCastSpell(pVictim, m_spells.hunter.pRaptorStrike))
            {
                if (DoCastSpell(pVictim, m_spells.hunter.pRaptorStrike))
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

        if (!me->HasUnitState(UNIT_STATE_ROOT) &&
            (me->GetCombatDistance(pVictim) < 8.0f) &&
            m_role != ROLE_MELEE_DPS)
        {
            if (!me->IsStopped())
                me->StopMoving();
            me->GetMotionMaster()->Clear();
            RunAwayFromTarget(pVictim,true);
            return;
        }

    }
}

void PartyBotAI::UpdateOutOfCombatAI_Mage()
{
    // Ported from upstream: single-target buff when only one member needs it, group buff otherwise
    {
        SpellEntry const* pBuffSpell = nullptr;
        if (Player* pTarget = SelectBuffTarget(m_spells.mage.pArcaneIntellect, m_spells.mage.pArcaneBrilliance, pBuffSpell))
        {
            if (CanTryToCastSpell(pTarget, pBuffSpell))
            {
                if (DoCastSpell(pTarget, pBuffSpell) == SPELL_CAST_OK)
                {
                    m_isBuffing = true;
                    return;
                }
            }
        }
    }

    if (m_spells.mage.pIceArmor &&
        CanTryToCastSpell(me, m_spells.mage.pIceArmor))
    {
        if (DoCastSpell(me, m_spells.mage.pIceArmor) == SPELL_CAST_OK)
        {
            m_isBuffing = true;
            return;
        }
    }

    if (m_isBuffing &&
       (!m_spells.mage.pArcaneIntellect ||
        !me->HasGCD(m_spells.mage.pArcaneIntellect)))
    {
        m_isBuffing = false;
    }

    // Decurse
    if (m_spells.mage.pRemoveLesserCurse)
    {
        if (Unit* pFriend = SelectDispelTarget(m_spells.mage.pRemoveLesserCurse))
        {
            if (CanTryToCastSpell(pFriend, m_spells.mage.pRemoveLesserCurse))
            {
                if (DoCastSpell(pFriend, m_spells.mage.pRemoveLesserCurse) == SPELL_CAST_OK)
                    return;
            }
        }
    }
}

void PartyBotAI::UpdateInCombatAI_Mage()
{
    // Decurse - Priority for boss fights
    if (m_spells.mage.pRemoveLesserCurse)
    {
        if (Unit* pFriend = SelectDispelTarget(m_spells.mage.pRemoveLesserCurse))
        {
            if (CanTryToCastSpell(pFriend, m_spells.mage.pRemoveLesserCurse))
            {
                if (DoCastSpell(pFriend, m_spells.mage.pRemoveLesserCurse) == SPELL_CAST_OK)
                    return;
            }
        }
    }

    if (Unit* pVictim = me->GetVictim())
    {
        if (m_spells.mage.pPyroblast &&
           ((m_spells.mage.pPresenceOfMind && me->HasAura(m_spells.mage.pPresenceOfMind->Id)) ||
            (!pVictim->IsInCombat() && (pVictim->GetMaxHealth() > me->GetMaxHealth()) && (me->GetDistance(pVictim) > 30.0f))) &&
            CanTryToCastSpell(pVictim, m_spells.mage.pPyroblast))
        {
            if (DoCastSpell(pVictim, m_spells.mage.pPyroblast) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.mage.pIceBlock &&
            me->GetHealthPercent() < 10.0f &&
            CanTryToCastSpell(me, m_spells.mage.pIceBlock))
        {
            if (DoCastSpell(me, m_spells.mage.pIceBlock) == SPELL_CAST_OK)
                return;
        }

        if (!me->GetAttackers().empty())
        {
            if (m_spells.mage.pIceBarrier &&
                CanTryToCastSpell(me, m_spells.mage.pIceBarrier))
            {
                if (DoCastSpell(me, m_spells.mage.pIceBarrier) == SPELL_CAST_OK)
                    return;
            }
        }

        if (GetAttackersInRangeCount(10.0f) > 1)
        {
            if (m_spells.mage.pManaShield &&
               (me->GetPowerPercent(POWER_MANA) > 20.0f) &&
                CanTryToCastSpell(me, m_spells.mage.pManaShield))
            {
                if (DoCastSpell(me, m_spells.mage.pManaShield) == SPELL_CAST_OK)
                    return;
            }

            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != DISTANCING_MOTION_TYPE)
            {
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
                        RunAwayFromTarget(pVictim,true);
                        return;
                    }
                }
            }
        }

        if (me->GetEnemyCountInRadiusAround(me, 10.0f) > 2 &&
            m_aoeSpellTimer <= 0)
        {
            if (m_spells.mage.pImprovedArcaneExplosion &&
                m_spells.mage.pArcaneExplosion &&
                CanTryToCastSpell(me, m_spells.mage.pArcaneExplosion))
            {
                if (DoCastSpell(me, m_spells.mage.pArcaneExplosion) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.mage.pBlastWave &&
                CanTryToCastSpell(me, m_spells.mage.pBlastWave))
            {
                if (DoCastSpell(me, m_spells.mage.pBlastWave) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.mage.pConeofCold &&
                CanTryToCastSpell(me, m_spells.mage.pConeofCold))
            {
                if (DoCastSpell(pVictim, m_spells.mage.pConeofCold) == SPELL_CAST_OK)
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

        if (m_spells.mage.pCounterspell &&
            pVictim->IsNonMeleeSpellCasted(false, false, true) &&
            CanTryToCastSpell(pVictim, m_spells.mage.pCounterspell))
        {
            if (DoCastSpell(pVictim, m_spells.mage.pCounterspell) == SPELL_CAST_OK)
                return;
        }

        if (me->GetEnemyCountInRadiusAround(pVictim, 10.0f) > 3 &&
            m_aoeSpellTimer <= 0 && 
            pVictim->GetHealthPercent() < 75.0f)
        {
            if (m_spells.mage.pImprovedArcaneExplosion &&
                m_spells.mage.pArcaneExplosion &&
                CanTryToCastSpell(pVictim, m_spells.mage.pArcaneExplosion))
            {
                // Chase victim at close range to use Arcane Explosion.
                // Spell is cast on a previous check when in range.
                me->GetMotionMaster()->MoveChase(pVictim, 5.0f);
            }

            if (m_spells.mage.pImprovedFlamestrike &&
                m_spells.mage.pFlamestrike &&
                CanTryToCastSpell(pVictim, m_spells.mage.pFlamestrike))
            {
                if (DoCastSpell(pVictim, m_spells.mage.pFlamestrike) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.mage.pBlizzard &&
                CanTryToCastSpell(pVictim, m_spells.mage.pBlizzard))
            {
                if (DoCastSpell(pVictim, m_spells.mage.pBlizzard) == SPELL_CAST_OK)
                    return;
            }
        }

        if (m_spells.mage.pPolymorph)
        {
            if (Unit* pTarget = SelectAttackerDifferentFrom(pVictim))
            {
                if (pTarget->GetHealthPercent() > 20.0f &&
                    CanTryToCastSpell(pTarget, m_spells.mage.pPolymorph) &&
                    CanUseCrowdControl(m_spells.mage.pPolymorph, pTarget))
                {
                    if (DoCastSpell(pTarget, m_spells.mage.pPolymorph) == SPELL_CAST_OK)
                        return;
                }
            }
        }

        if (m_spells.mage.pCombustion &&
            CanTryToCastSpell(pVictim, m_spells.mage.pCombustion))
        {
            if (DoCastSpell(pVictim, m_spells.mage.pCombustion) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.mage.pArcanePower &&
            (me->GetPowerPercent(POWER_MANA) > 20.0f) &&
            CanTryToCastSpell(me, m_spells.mage.pArcanePower))
        {
            if (DoCastSpell(me, m_spells.mage.pArcanePower) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.mage.pPresenceOfMind &&
            CanTryToCastSpell(me, m_spells.mage.pPresenceOfMind))
        {
            if (DoCastSpell(me, m_spells.mage.pPresenceOfMind) == SPELL_CAST_OK)
                return;
        }

        // If has improved fireball, try fire spells
        if (m_spells.mage.pImprovedFireball)
        {
            if (m_spells.mage.pFireBlast &&
                CanTryToCastSpell(pVictim, m_spells.mage.pFireBlast))
            {
                if (DoCastSpell(pVictim, m_spells.mage.pFireBlast) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.mage.pScorch &&
                CanTryToCastSpell(pVictim, m_spells.mage.pScorch))
            {
                if (pVictim->GetHealth() < 10.0f)
                {
                    if (DoCastSpell(pVictim, m_spells.mage.pScorch) == SPELL_CAST_OK)
                        return;
                }
                else if(m_spells.mage.pImprovedScorch)
                { 
                    // Fire vulnerability aura 22959
                    SpellAuraHolder* pSpellAuraHolder = pVictim->GetSpellAuraHolder(22959);
                    if(!pSpellAuraHolder || pSpellAuraHolder->GetStackAmount() < 5)
                    {
                        if (DoCastSpell(pVictim, m_spells.mage.pScorch) == SPELL_CAST_OK)
                            return;
                    }
                }
            }

            if (m_spells.mage.pFireball &&
                CanTryToCastSpell(pVictim, m_spells.mage.pFireball,2))
            {
                if (DoCastSpell(pVictim, m_spells.mage.pFireball) == SPELL_CAST_OK)
                    return;
            }
        }

        // If has improved Arcane Missiles, try to use it
        if (m_spells.mage.pImprovedArcaneMissiles &&
            m_spells.mage.pArcaneMissiles &&
            CanTryToCastSpell(pVictim, m_spells.mage.pArcaneMissiles,2))
        {
            if (DoCastSpell(pVictim, m_spells.mage.pArcaneMissiles) == SPELL_CAST_OK)
                return;
        }

        // Nothing cast, try Frostbolt...
        if (m_spells.mage.pFrostbolt &&
            CanTryToCastSpell(pVictim, m_spells.mage.pFrostbolt,2))
        {
            if (DoCastSpell(pVictim, m_spells.mage.pFrostbolt) == SPELL_CAST_OK)
                return;
        }

        // If immune, try normal fireball
        if (m_spells.mage.pFireball &&
            CanTryToCastSpell(pVictim, m_spells.mage.pFireball, 2))
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

        if (me->HasSpell(PB_SPELL_SHOOT_WAND) &&
           !me->IsMoving() &&
           !me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL) &&
            me->IsSpellReady(sSpellMgr.GetSpellEntry(PB_SPELL_SHOOT_WAND)))
            me->CastSpell(pVictim, PB_SPELL_SHOOT_WAND, false);
    }
}

void PartyBotAI::UpdateOutOfCombatAI_Priest()
{
    // Ported from upstream: single-target buff when only one member needs it, group buff otherwise
    {
        SpellEntry const* pBuffSpell = nullptr;
        if (Player* pTarget = SelectBuffTarget(m_spells.priest.pPowerWordFortitude, m_spells.priest.pPrayerofFortitude, pBuffSpell))
        {
            if (CanTryToCastSpell(pTarget, pBuffSpell))
            {
                if (DoCastSpell(pTarget, pBuffSpell) == SPELL_CAST_OK)
                {
                    m_isBuffing = true;
                    return;
                }
            }
        }
    }

    // Ported from upstream: single-target buff when only one member needs it, group buff otherwise
    {
        SpellEntry const* pBuffSpell = nullptr;
        if (Player* pTarget = SelectBuffTarget(m_spells.priest.pDivineSpirit, m_spells.priest.pPrayerofSpirit, pBuffSpell))
        {
            if (CanTryToCastSpell(pTarget, pBuffSpell))
            {
                if (DoCastSpell(pTarget, pBuffSpell) == SPELL_CAST_OK)
                {
                    m_isBuffing = true;
                    return;
                }
            }
        }
    }

    // Ported from upstream: single-target buff when only one member needs it, group buff otherwise
    {
        SpellEntry const* pBuffSpell = nullptr;
        if (Player* pTarget = SelectBuffTarget(m_spells.priest.pShadowProtection, m_spells.priest.pPrayerofShadowProtection, pBuffSpell))
        {
            if (CanTryToCastSpell(pTarget, pBuffSpell))
            {
                if (DoCastSpell(pTarget, pBuffSpell) == SPELL_CAST_OK)
                {
                    m_isBuffing = true;
                    return;
                }
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

    if (m_spells.priest.pTouchOfWeakness &&
        CanTryToCastSpell(me, m_spells.priest.pTouchOfWeakness))
    {
        if (DoCastSpell(me, m_spells.priest.pTouchOfWeakness) == SPELL_CAST_OK)
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

    // Dispels
    if (m_spells.priest.pDispelMagic)
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

    if (m_spells.priest.pAbolishDisease)
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

    if (m_role == ROLE_HEALER)
    {
        if (FindAndHealInjuredAlly(90.0f, 50.0f))
            return;
    }
}

void PartyBotAI::UpdateInCombatAI_Priest()
{

    if (!me->GetAttackers().empty())
    {
        if (m_spells.priest.pFade &&
            CanTryToCastSpell(me, m_spells.priest.pFade))
        {
            if (DoCastSpell(me, m_spells.priest.pFade) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.priest.pPowerWordShield &&
            CanTryToCastSpell(me, m_spells.priest.pPowerWordShield))
        {
            if (DoCastSpell(me, m_spells.priest.pPowerWordShield) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.priest.pShackleUndead)
        {
            Unit* pAttacker = *me->GetAttackers().begin();
            if ((pAttacker->GetHealth() > me->GetHealth()) &&
                CanTryToCastSpell(pAttacker, m_spells.priest.pShackleUndead) &&
                CanUseCrowdControl(m_spells.priest.pShackleUndead, pAttacker))
            {
                if (DoCastSpell(pAttacker, m_spells.priest.pShackleUndead) == SPELL_CAST_OK)
                {
                    RunAwayFromTarget(pAttacker,true);
                    return;
                }
            }
        }

        if (me->GetHealthPercent() < 50.0f)
        {
            if (m_spells.priest.pPsychicScream &&
                GetAttackersInRangeCount(10.0f) > 1 &&
                CanTryToCastSpell(me, m_spells.priest.pPsychicScream))
            {
                if (DoCastSpell(me, m_spells.priest.pPsychicScream) == SPELL_CAST_OK)
                    return;
            }
        }
    }

    if (m_spells.priest.pInnerFocus &&
       (me->GetPowerPercent(POWER_MANA) < 50.0f) &&
        CanTryToCastSpell(me, m_spells.priest.pInnerFocus))
    {
        DoCastSpell(me, m_spells.priest.pInnerFocus);
    }

    // Critical Healing
    if (FindAndHealInjuredAlly(35.0f, 35.0f))
        return;

    // Dispels
    if (m_spells.priest.pDispelMagic)
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

    if (m_spells.priest.pAbolishDisease)
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

    if (m_role == ROLE_HEALER)
    {
        // Check group healing
        if (GetAlliesNeedingHealCount(20.0f, 70.0f) >= 3 &&
            m_spells.priest.pPrayerofHealing &&
            CanTryToCastSpell(me, m_spells.priest.pPrayerofHealing))
        {
            if (DoCastSpell(me, m_spells.priest.pPrayerofHealing) == SPELL_CAST_OK)
                return;
        }

        // Shield allies being attacked.
        if (m_spells.priest.pPowerWordShield)
        {
            if (Player* pTarget = SelectShieldTarget())
            {
                if (CanTryToCastSpell(pTarget, m_spells.priest.pPowerWordShield))
                {
                    if (DoCastSpell(pTarget, m_spells.priest.pPowerWordShield) == SPELL_CAST_OK)
                        return;
                }
            }
        }

        if (FindAndHealInjuredAlly(95.0f, 50.0f))
            return;

        // Ported from upstream: pre-heal the ally taking the most incoming damage
        if (FindAndPreHealTarget())
            return;
    }
    else
    {
        if (Unit* pVictim = me->GetVictim())
        {
            if (m_spells.priest.pShadowform &&
                me->GetShapeshiftForm() != FORM_SHADOW &&
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


            if (m_spells.priest.pHolyNova &&
                me->GetShapeshiftForm() == FORM_NONE &&
                me->GetEnemyCountInRadiusAround(me, 10.0f) > 2 &&
                CanTryToCastSpell(me, m_spells.priest.pHolyNova))
            {
                if (DoCastSpell(me, m_spells.priest.pHolyNova) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.priest.pManaBurn &&
                me->GetPowerPercent(POWER_MANA) < 50.0f &&
                pVictim->GetPowerType() == POWER_MANA &&
                pVictim->GetPowerPercent(POWER_MANA) > 10.0f &&
                CanTryToCastSpell(pVictim, m_spells.priest.pManaBurn))
            {
                if (DoCastSpell(pVictim, m_spells.priest.pManaBurn) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.priest.pVampiricEmbrace &&
                CanTryToCastSpell(pVictim, m_spells.priest.pVampiricEmbrace))
            {
                if (DoCastSpell(pVictim, m_spells.priest.pVampiricEmbrace) == SPELL_CAST_OK)
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

            if (m_spells.priest.pMindBlast &&
                CanTryToCastSpell(pVictim, m_spells.priest.pMindBlast))
            {
                if (DoCastSpell(pVictim, m_spells.priest.pMindBlast) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.priest.pMindFlay &&
                CanTryToCastSpell(pVictim, m_spells.priest.pMindFlay))
            {
                if (DoCastSpell(pVictim, m_spells.priest.pMindFlay) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.priest.pSmite &&
                CanTryToCastSpell(pVictim, m_spells.priest.pSmite))
            {
                if (DoCastSpell(pVictim, m_spells.priest.pSmite) == SPELL_CAST_OK)
                    return;
            }

            if (me->HasSpell(PB_SPELL_SHOOT_WAND) &&
                !me->IsMoving() &&
                !me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL) &&
                me->IsSpellReady(sSpellMgr.GetSpellEntry(PB_SPELL_SHOOT_WAND)))
                me->CastSpell(pVictim, PB_SPELL_SHOOT_WAND, false);
        }
    }
}

void PartyBotAI::UpdateOutOfCombatAI_Warlock()
{
    if (m_spells.warlock.pDetectInvisibility && !me->GetGroup()->isRaidGroup())
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

    if (m_spells.warlock.pDemonArmor)
    { 
        if (CanTryToCastSpell(me, m_spells.warlock.pDemonArmor))
        {
            if (DoCastSpell(me, m_spells.warlock.pDemonArmor) == SPELL_CAST_OK)
            {
                m_isBuffing = true;
                return;
            }
        }

    }
    else if (m_spells.warlock.pDemonSkin)
    { 
        if (CanTryToCastSpell(me, m_spells.warlock.pDemonSkin))
        {
            if (DoCastSpell(me, m_spells.warlock.pDemonSkin) == SPELL_CAST_OK)
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

    if (!me->HasAura(PB_SPELL_TOUCH_OF_SHADOW))
    {
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

        SummonPetIfNeeded();
        return;
    }

}

void PartyBotAI::UpdateInCombatAI_Warlock()
{
    if (Unit* pVictim = me->GetVictim())
    {
        if (m_spells.warlock.pDeathCoil &&
            me->GetHealthPercent() < 65.0f &&
            pVictim->GetVictim() == me &&
            pVictim->CanReachWithMeleeAutoAttack(me) &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pDeathCoil))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pDeathCoil) == SPELL_CAST_OK)
                return;
        }

        if (Pet* pPet = me->GetPet())
        {
            if (!pPet->GetVictim() && !me->GetMap()->IsDungeon())
            {
                pPet->GetCharmInfo()->SetIsCommandAttack(true);
                pPet->AI()->AttackStart(pVictim);
            }
        }

        if (m_spells.warlock.pHowlofTerror &&
            me->GetHealthPercent() < 30.0f &&
            GetAttackersInRangeCount(10.0f) > 1 &&
            CanTryToCastSpell(me, m_spells.warlock.pHowlofTerror))
        {
            if (DoCastSpell(me, m_spells.warlock.pHowlofTerror) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warlock.pShadowburn &&
           (pVictim->GetHealthPercent() < 10.0f) &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pShadowburn))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pShadowburn) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warlock.pBanish && m_spellTimer1 <= 0)
        {   
            if(Unit* pTarget = SelectSpellTargetDifferentFrom(m_spells.warlock.pBanish,pVictim,20.0f))
            { 
                if (DoCastSpell(pTarget, m_spells.warlock.pBanish) == SPELL_CAST_OK)
                {
                    m_spellTimer1 = 15 * IN_MILLISECONDS;
                    return;
                }
            }
        }

        if (m_spells.warlock.pRainOfFire &&
            m_aoeSpellTimer <= 0 &&
           (me->GetEnemyCountInRadiusAround(pVictim, 10.0f) > 3) &&
            pVictim->GetHealthPercent() < 75.0f &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pRainOfFire))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pRainOfFire) == SPELL_CAST_OK)
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


        // Raid Curse
        if (me->GetGroup()->isRaidGroup())
        {
            if (m_spells.warlock.pRaidCurse &&
                !pVictim->HasAura(m_spells.warlock.pRaidCurse->Id) &&
                CanTryToCastSpell(pVictim, m_spells.warlock.pRaidCurse))
            {
                if (DoCastSpell(pVictim, m_spells.warlock.pRaidCurse) == SPELL_CAST_OK)
                    return;
            }
        }

        if (m_spells.warlock.pImmolate &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pImmolate))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pImmolate) == SPELL_CAST_OK)
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

        if (!me->GetGroup()->isRaidGroup())
        { 
            if (m_spells.warlock.pCurseofTongues &&
                pVictim->IsCaster() &&
                CanTryToCastSpell(pVictim, m_spells.warlock.pCurseofTongues))
            {
                if (DoCastSpell(pVictim, m_spells.warlock.pCurseofTongues) == SPELL_CAST_OK)
                    return;
            }
            else if (m_spells.warlock.pCurseofAgony &&
                     CanTryToCastSpell(pVictim, m_spells.warlock.pCurseofAgony))
            {
                if (DoCastSpell(pVictim, m_spells.warlock.pCurseofAgony) == SPELL_CAST_OK)
                    return;
            }
        }

        if (m_spells.warlock.pDrainLife &&
            (me->GetHealthPercent() < 50.0f) &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pDrainLife))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pDrainLife) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warlock.pConflagrate &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pConflagrate))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pConflagrate) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warlock.pFear &&
            pVictim->GetVictim() == me &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pFear))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pFear) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warlock.pSearingPain &&
            (pVictim->GetHealthPercent() < 20.0f) &&
            CanTryToCastSpell(pVictim, m_spells.warlock.pSearingPain))
        {
            if (DoCastSpell(pVictim, m_spells.warlock.pSearingPain) == SPELL_CAST_OK)
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

        if (m_spells.warlock.pDarkPact &&
            (me->GetPowerPercent(POWER_MANA) < 10.0f) &&
            CanTryToCastSpell(me, m_spells.warlock.pDarkPact))
        {
            if (DoCastSpell(me, m_spells.warlock.pDarkPact) == SPELL_CAST_OK)
                return;
        }

        if (me->HasSpell(PB_SPELL_SHOOT_WAND) &&
           !me->IsMoving() &&
           !me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL) &&
            me->IsSpellReady(sSpellMgr.GetSpellEntry(PB_SPELL_SHOOT_WAND)))
            me->CastSpell(pVictim, PB_SPELL_SHOOT_WAND, false);
    }
}

void PartyBotAI::UpdateOutOfCombatAI_Warrior()
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
}

void PartyBotAI::UpdateInCombatAI_Warrior()
{
    if (Unit* pVictim = me->GetVictim())
    {
        // CHARGE
        if (m_spells.warrior.pCharge &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pCharge))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pCharge) == SPELL_CAST_OK)
                return;
        }

        // STANCE SELECTION
        if (pVictim->CanReachWithMeleeAutoAttack(me))
        {
            if (m_role == ROLE_TANK || me->GetHealthPercent() < 25.0f)
            {
                if (m_spells.warrior.pDefensiveStance &&
                    CanTryToCastSpell(me, m_spells.warrior.pDefensiveStance))
                {
                    DoCastSpell(me, m_spells.warrior.pDefensiveStance);
                }
            }
            else if (me->GetHealthPercent() > 60.0f)
            {
                if (m_spells.warrior.pBloodthirst &&
                    m_spells.warrior.pBerserkerStance &&
                    me->GetShapeshiftForm() != FORM_BERSERKERSTANCE &&
                    CanTryToCastSpell(me, m_spells.warrior.pBerserkerStance))
                {
                    DoCastSpell(me, m_spells.warrior.pBerserkerStance);
                }
                else if (m_spells.warrior.pMortalStrike &&
                    m_spells.warrior.pBattleStance &&
                    me->GetShapeshiftForm() != FORM_BATTLESTANCE &&
                    CanTryToCastSpell(me, m_spells.warrior.pBattleStance))
                {
                    DoCastSpell(me, m_spells.warrior.pBattleStance);
                }
            }
        }

        // INTERRUPT ENEMY SPELL CASTING
        if (pVictim->IsNonMeleeSpellCasted(false, false, true))
        {
            if (m_spells.warrior.pPummel &&
                CanTryToCastSpell(pVictim, m_spells.warrior.pPummel))
            {
                if (DoCastSpell(pVictim, m_spells.warrior.pPummel) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.warrior.pShieldBash &&
                IsWearingShield() &&
                CanTryToCastSpell(pVictim, m_spells.warrior.pShieldBash))
            {
                if (DoCastSpell(pVictim, m_spells.warrior.pShieldBash) == SPELL_CAST_OK)
                    return;
            }
        }

        // CHALLENGING SHOUT
        if (m_role == ROLE_TANK &&
            me->GetEnemyCountInRadiusAround(me, 10.0f) > 3 &&
            GetAttackersInRangeCount(10.0f) <= 1 &&
            m_spells.warrior.pChallengingShout &&
            CanTryToCastSpell(me, m_spells.warrior.pChallengingShout))
        {
            if (DoCastSpell(me, m_spells.warrior.pChallengingShout) == SPELL_CAST_OK)
                return;
        }

        // USE DEFENSIVE SPELLS
        if (me->GetShapeshiftForm() == FORM_DEFENSIVESTANCE && IsWearingShield())
        {
            if (!me->GetAttackers().empty())
            {
                if (m_spells.warrior.pShieldBlock &&
                    (me->GetHealthPercent() < 70.0f) &&
                    CanTryToCastSpell(me, m_spells.warrior.pShieldBlock))
                {
                    if (DoCastSpell(me, m_spells.warrior.pShieldBlock) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.warrior.pShieldWall &&
                    (me->GetHealthPercent() < 30.0f) &&
                    CanTryToCastSpell(me, m_spells.warrior.pShieldWall))
                {
                    if (DoCastSpell(me, m_spells.warrior.pShieldWall) == SPELL_CAST_OK)
                        return;
                }
            }
        }

        if (m_spells.warrior.pLastStand &&
            me->GetHealthPercent() < 20.0f &&
            CanTryToCastSpell(me, m_spells.warrior.pLastStand))
        {
            if (DoCastSpell(me, m_spells.warrior.pLastStand) == SPELL_CAST_OK)
                return;
        }

        if (m_role != ROLE_TANK &&
            m_spells.warrior.pIntimidatingShout &&
            (me->GetHealthPercent() < 20.0f) &&
            (GetAttackersInRangeCount(10.0f) > 2) &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pIntimidatingShout))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pIntimidatingShout) == SPELL_CAST_OK)
                return;
        }

        // BATTLE SHOUT IF NEEDED
        if (m_spells.warrior.pBattleShout &&
            CanTryToCastSpell(me, m_spells.warrior.pBattleShout))
        {
            if (DoCastSpell(me, m_spells.warrior.pBattleShout) == SPELL_CAST_OK)
                return;
        }

        // USE PROC OR CONDITIONAL SPELLS
        if (m_spells.warrior.pRevenge &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pRevenge))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pRevenge) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pOverpower &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pOverpower))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pOverpower) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pBerserkerRage &&
            CanTryToCastSpell(me, m_spells.warrior.pBerserkerRage))
        {
            if (DoCastSpell(me, m_spells.warrior.pBerserkerRage) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pExecute &&
           (pVictim->GetHealthPercent() < 20.0f) &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pExecute))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pExecute) == SPELL_CAST_OK)
                return;
        }

        // DEBUFF ENEMY
        if (m_role == ROLE_TANK &&
            m_spells.warrior.pDemoralizingShout &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pDemoralizingShout))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pDemoralizingShout) == SPELL_CAST_OK)
                return;
        }

        // Go on only if 15 rage is available so the most relevant skills can be used
        if (me->GetPowerPercent(POWER_RAGE) < 15.0f)
            return;

        // For tanks, prioritize first Sunder Armor application
        if (m_role == ROLE_TANK &&
            m_spells.warrior.pSunderArmor &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pSunderArmor, 1))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pSunderArmor) == SPELL_CAST_OK)
                return;
        }

        // Use AOE spells
        if (me->GetEnemyCountInRadiusAround(me, 10.0f) > 2)
        {
            if (m_role == ROLE_TANK)
            {
                if (m_spells.warrior.pBattleShout &&
                    CanTryToCastSpell(me, m_spells.warrior.pBattleShout, 2))
                {
                    if (DoCastSpell(me, m_spells.warrior.pBattleShout) == SPELL_CAST_OK)
                        return;
                }
            }
            else
            {
                if (m_spells.warrior.pWhirlwind &&
                    CanTryToCastSpell(pVictim, m_spells.warrior.pWhirlwind))
                {
                    if (DoCastSpell(pVictim, m_spells.warrior.pWhirlwind) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.warrior.pThunderClap &&
                    CanTryToCastSpell(pVictim, m_spells.warrior.pThunderClap))
                {
                    if (DoCastSpell(pVictim, m_spells.warrior.pThunderClap) == SPELL_CAST_OK)
                        return;
                }
            }
        }

        // Use single target spells
        if (m_spells.warrior.pRend &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pRend))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pRend) == SPELL_CAST_OK)
                return;
        }

        // Try to stop if target running away
        if (m_spells.warrior.pConcussionBlow &&
            (pVictim->IsNonMeleeSpellCasted() || pVictim->IsMoving() || (me->GetHealthPercent() < 50.0f)) &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pConcussionBlow))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pConcussionBlow) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pHamstring &&
            pVictim->IsMoving() &&
            !pVictim->HasUnitState(UNIT_STATE_ROOT) &&
            !pVictim->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pHamstring))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pHamstring) == SPELL_CAST_OK)
                return;
        }


        // Now wait for more rage for main skills
        if (me->GetPowerPercent(POWER_RAGE) < 30.0f)
            return;

        if (m_spells.warrior.pShieldSlam &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pShieldSlam))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pShieldSlam) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pSweepingStrikes &&
            CanTryToCastSpell(me, m_spells.warrior.pSweepingStrikes) &&
            (me->GetEnemyCountInRadiusAround(pVictim, 10.0f) > 1))
        {
            if (DoCastSpell(me, m_spells.warrior.pSweepingStrikes) == SPELL_CAST_OK)
                return;
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

        // Second Sunder Armor Application
        if (m_role == ROLE_TANK &&
            m_spells.warrior.pSunderArmor &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pSunderArmor, 2))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pSunderArmor) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pDisarm &&
            IsMeleeWeaponClass(pVictim->GetClass()) &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pDisarm))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pDisarm) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pRetaliation &&
           (GetAttackersInRangeCount(10.0f) > 2) &&
            CanTryToCastSpell(me, m_spells.warrior.pRetaliation))
        {
            if (DoCastSpell(me, m_spells.warrior.pRetaliation) == SPELL_CAST_OK)
                return;
        }

        if (m_role != ROLE_TANK &&
           (me->GetHealthPercent() > 60.0f) && (pVictim->GetHealthPercent() > 40.0f) &&
           !me->HasUnitState(UNIT_STATE_ROOT) &&
           !me->IsImmuneToMechanic(MECHANIC_FEAR))
        {
            if (m_spells.warrior.pDeathWish &&
                CanTryToCastSpell(me, m_spells.warrior.pDeathWish))
            {
                if (DoCastSpell(me, m_spells.warrior.pDeathWish) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.warrior.pRecklessness &&
                CanTryToCastSpell(me, m_spells.warrior.pRecklessness))
            {
                if (DoCastSpell(me, m_spells.warrior.pRecklessness) == SPELL_CAST_OK)
                    return;
            }
        }

        if (m_spells.warrior.pIntercept &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pIntercept))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pIntercept) == SPELL_CAST_OK)
                return;
        }

        // LASTLY USE FILLER SPELLS
        if(me->GetEnemyCountInRadiusAround(pVictim, 8.0f) > 1 &&
            m_spells.warrior.pCleave &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pCleave))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pCleave) == SPELL_CAST_OK)
                return;
        }

        // For tanks, apply Sunder Armor up to 5 stacks
        if (m_role == ROLE_TANK &&
            m_spells.warrior.pSunderArmor &&
            CanTryToCastSpell(pVictim, m_spells.warrior.pSunderArmor, 5))
        {
            if (DoCastSpell(pVictim, m_spells.warrior.pSunderArmor) == SPELL_CAST_OK)
                return;
        }

        if (m_spells.warrior.pHeroicStrike &&
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

void PartyBotAI::UpdateOutOfCombatAI_Rogue()
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

    if (m_spells.rogue.pStealth &&
        CanTryToCastSpell(me, m_spells.rogue.pStealth))
    {
        if (Player* pLeader = GetPartyLeader())
        {
            if (me->IsWithinDistInMap(pLeader, 10.0) &&
                DoCastSpell(me, m_spells.rogue.pStealth) == SPELL_CAST_OK)
                return;
        }
    }

}

void PartyBotAI::UpdateInCombatAI_Rogue()
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
                        RunAwayFromTarget(pVictim,true);
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
                if (CanTryToCastSpell(pTarget, m_spells.rogue.pBlind) &&
                    CanUseCrowdControl(m_spells.rogue.pBlind, pTarget))
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

        if (m_spells.rogue.pRiposte &&
            CanTryToCastSpell(pVictim, m_spells.rogue.pRiposte))
        {
            if (DoCastSpell(pVictim, m_spells.rogue.pRiposte) == SPELL_CAST_OK)
                return;
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

bool PartyBotAI::EnterCombatDruidForm()
{
    if (m_spells.druid.pCatForm &&
        m_role == ROLE_MELEE_DPS &&
        CanTryToCastSpell(me, m_spells.druid.pCatForm))
    {
        if (DoCastSpell(me, m_spells.druid.pCatForm) == SPELL_CAST_OK)
            return true;
    }

    if (m_spells.druid.pBearForm &&
       (m_role == ROLE_TANK || m_role == ROLE_MELEE_DPS) &&
        CanTryToCastSpell(me, m_spells.druid.pBearForm))
    {
        if (DoCastSpell(me, m_spells.druid.pBearForm) == SPELL_CAST_OK)
            return true;
    }

    if (m_spells.druid.pMoonkinForm &&
        m_role == ROLE_RANGE_DPS &&
        CanTryToCastSpell(me, m_spells.druid.pMoonkinForm))
    {
        if (DoCastSpell(me, m_spells.druid.pMoonkinForm) == SPELL_CAST_OK)
            return true;
    }

    return false;
}

void PartyBotAI::PopulateConsumableSpellData()
{
    // Healing Potion
    if (me->GetLevel() >= 60)
    {
        m_potionSpell = sSpellMgr.GetSpellEntry(PB_SPELL_POT_REJUV);
        m_bandage = sSpellMgr.GetSpellEntry(PB_SPELL_BAND_HEAVY_RUNE);
    }
    else if (me->GetLevel() >= 45)
        m_potionSpell = sSpellMgr.GetSpellEntry(PB_SPELL_POT_HEAL_45);
    else if (me->GetLevel() >= 35)
        m_potionSpell = sSpellMgr.GetSpellEntry(PB_SPELL_POT_HEAL_35);
    else if (me->GetLevel() >= 21)
        m_potionSpell = sSpellMgr.GetSpellEntry(PB_SPELL_POT_HEAL_21);
    else if (me->GetLevel() >= 12)
        m_potionSpell = sSpellMgr.GetSpellEntry(PB_SPELL_POT_HEAL_12);
    else if (me->GetLevel() >= 3)
        m_potionSpell = sSpellMgr.GetSpellEntry(PB_SPELL_POT_HEAL_3);

    // Other Potions only apply to lvl 60+
    if (m_level < sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        return;
    
    m_restPotion = sSpellMgr.GetSpellEntry(PB_SPELL_POT_RESTO);

    switch (m_role)
    {
        case ROLE_TANK:
            m_elixirSpell = sSpellMgr.GetSpellEntry(PB_SPELL_ELX_FORCE);
            m_flaskSpell = sSpellMgr.GetSpellEntry(PB_SPELL_FLASK_TITAN);
            break;
        case ROLE_HEALER:
            m_elixirSpell = sSpellMgr.GetSpellEntry(PB_SPELL_ELX_MAGEBL);
            m_flaskSpell = sSpellMgr.GetSpellEntry(PB_SPELL_FLASK_WISDOM);
            break;
        case ROLE_MELEE_DPS:
            m_elixirSpell = sSpellMgr.GetSpellEntry(PB_SPELL_ELX_MOONG);
            m_flaskSpell = sSpellMgr.GetSpellEntry(PB_SPELL_FLASK_TITAN);
            break;
        case ROLE_RANGE_DPS:
            if (m_class == CLASS_HUNTER)
            {
                m_elixirSpell = sSpellMgr.GetSpellEntry(PB_SPELL_ELX_MOONG);
                m_flaskSpell = sSpellMgr.GetSpellEntry(PB_SPELL_FLASK_TITAN);
            }
            else
            {
                m_elixirSpell = sSpellMgr.GetSpellEntry(PB_SPELL_ELX_MAGEBL);
                m_flaskSpell = sSpellMgr.GetSpellEntry(PB_SPELL_FLASK_SPOWER);
            }
            break;
    }
}

void PartyBotAI::UpdateOutOfCombatAI_Druid()
{
    // Ported from upstream: single-target buff when only one member needs it, group buff otherwise
    {
        SpellEntry const* pBuffSpell = nullptr;
        if (Player* pTarget = SelectBuffTarget(m_spells.druid.pMarkoftheWild, m_spells.druid.pGiftoftheWild, pBuffSpell))
        {
            if (CanTryToCastSpell(pTarget, pBuffSpell))
            {
                if (me->GetShapeshiftForm() != FORM_NONE)
                    me->RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);
                if (DoCastSpell(pTarget, pBuffSpell) == SPELL_CAST_OK)
                {
                    m_isBuffing = true;
                    return;
                }
            }
        }
    }

    if (m_spells.druid.pOmenOfClarity)
    {
        if (CanTryToCastSpell(me, m_spells.druid.pOmenOfClarity))
        {
            if (me->GetShapeshiftForm() != FORM_NONE)
                me->RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);
            if (DoCastSpell(me, m_spells.druid.pOmenOfClarity) == SPELL_CAST_OK)
            {
                m_isBuffing = true;
                return;
            }
        }
    }

    if (m_spells.druid.pThorns && !me->GetGroup()->isRaidGroup())
    {
        if (Player* pTarget = SelectBuffTarget(m_spells.druid.pThorns))
        {
            if (CanTryToCastSpell(pTarget, m_spells.druid.pThorns))
            {
                if (me->GetShapeshiftForm() != FORM_NONE)
                    me->RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);
                if (DoCastSpell(pTarget, m_spells.druid.pThorns) == SPELL_CAST_OK)
                {
                    m_isBuffing = true;
                    return;
                }
            }
        }
    }

    if (m_isBuffing &&
       (!m_spells.druid.pMarkoftheWild ||
        !me->HasGCD(m_spells.druid.pMarkoftheWild)))
    {
        m_isBuffing = false;
    }

    // Dispels
    SpellEntry const* pDispelSpell = m_spells.druid.pAbolishPoison ?
        m_spells.druid.pAbolishPoison :
        m_spells.druid.pCurePoison;
    if (pDispelSpell)
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

    if (m_role == ROLE_HEALER)
    {
        if (FindAndHealInjuredAlly(90.0f, 50.0f))
            return;
    }
    else
    {
        if (me->GetShapeshiftForm() == FORM_NONE)
        {
            if (EnterCombatDruidForm())
                return;
        }
        else if (me->GetShapeshiftForm() == FORM_CAT)
        {
            if (m_spells.druid.pProwl &&
                CanTryToCastSpell(me, m_spells.druid.pProwl))
            {
                if (DoCastSpell(me, m_spells.druid.pProwl) == SPELL_CAST_OK)
                    return;
            }
        }
    }
}

void PartyBotAI::UpdateInCombatAI_Druid()
{
    ShapeshiftForm const form = me->GetShapeshiftForm();

    if (GetAttackersInRangeCount(10.0f) &&
        m_spells.druid.pBarkskin &&
        (form == FORM_NONE || form == FORM_MOONKIN) &&
        (me->GetHealthPercent() < 50.0f) &&
        CanTryToCastSpell(me, m_spells.druid.pBarkskin))
    {
        if (DoCastSpell(me, m_spells.druid.pBarkskin) == SPELL_CAST_OK)
            return;
    }

    // Critical Healing
    if (m_role == ROLE_TANK && me->GetHealthPercent() < 35.0f)
    {
        HealInjuredTargetDirect(me);
        return;
    }
    else
    {
        if (FindAndHealInjuredAlly(35.0f, 35.0f))
            return;
    }

    // Dispels
    SpellEntry const* pDispelSpell = m_spells.druid.pAbolishPoison ?
        m_spells.druid.pAbolishPoison :
        m_spells.druid.pCurePoison;
    if (pDispelSpell)
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

    if (m_role == ROLE_HEALER)
    {
        // Check group healing
        if(GetAlliesNeedingHealCount(20.0f,70.0f) >= 3 &&
            m_spells.druid.pTranquility &&
            CanTryToCastSpell(me, m_spells.druid.pTranquility))
        {
            if (DoCastSpell(me, m_spells.druid.pTranquility) == SPELL_CAST_OK)
                return;
        }

        // Swiftmend
        if (Unit* pTarget = SelectHealTarget(50.0f, false))
        {
            if (m_spells.druid.pSwiftmend &&
                pTarget->HasAuraType(SPELL_AURA_PERIODIC_HEAL) &&
                CanTryToCastSpell(pTarget, m_spells.druid.pSwiftmend))
            {
                if (DoCastSpell(pTarget, m_spells.druid.pSwiftmend) == SPELL_CAST_OK)
                    return;
            }
        }

        if (FindAndHealInjuredAlly(95.0f, 50.0f))
            return;

        // Ported from upstream: pre-heal the ally taking the most incoming damage
        if (FindAndPreHealTarget())
            return;
    }
    
    if (form == FORM_NONE)
    {
        if (m_spells.druid.pHibernate &&
            m_role != ROLE_TANK &&
            !me->GetAttackers().empty() &&
            me->GetEnemyCountInRadiusAround(me, 10.0f) > 1)
        {
            Unit* pAttacker = *me->GetAttackers().begin();
            if (CanTryToCastSpell(pAttacker, m_spells.druid.pHibernate))
            {
                if (DoCastSpell(pAttacker, m_spells.druid.pHibernate) == SPELL_CAST_OK)
                    return;
            }
        }

        if (m_spells.druid.pInnervate &&
           (me->GetHealthPercent() > 40.0f) &&
           (me->GetPowerPercent(POWER_MANA) < 10.0f) &&
            CanTryToCastSpell(me, m_spells.druid.pInnervate))
        {
            if (DoCastSpell(me, m_spells.druid.pInnervate) == SPELL_CAST_OK)
                return;
        }

        if (EnterCombatDruidForm())
            return;
    }

    Unit* pVictim = me->GetVictim();
    if (!pVictim)
        return;
    
    if (form != FORM_NONE &&
        me->HasUnitState(UNIT_STATE_ROOT) &&
        me->HasAuraType(SPELL_AURA_MOD_SHAPESHIFT) &&
        (m_role != ROLE_TANK || !me->CanReachWithMeleeAutoAttack(pVictim)))
        me->RemoveAurasDueToSpellByCancel(me->GetAurasByType(SPELL_AURA_MOD_SHAPESHIFT).front()->GetId());

    if (m_role == ROLE_HEALER)
        return;
    
    switch (form)
    {
        case FORM_CAT:
        {
            if (me->HasDistanceCasterMovement())
                me->SetCasterChaseDistance(0.0f);

            if (m_spells.druid.pFuror && me->GetPower(POWER_ENERGY) <= 12)
            {
                    me->RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);
            }

            if (me->HasAuraType(SPELL_AURA_MOD_STEALTH))
            {
                if (m_spells.druid.pRavage &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pRavage))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pRavage) == SPELL_CAST_OK)
                        return;
                }
                if (m_spells.druid.pPounce &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pPounce))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pPounce) == SPELL_CAST_OK)
                        return;
                }
                /*if (m_spells.druid.pTigersFury &&
                    CanTryToCastSpell(me, m_spells.druid.pTigersFury))
                {
                    if (DoCastSpell(me, m_spells.druid.pTigersFury) == SPELL_CAST_OK)
                        return;
                }*/
                return;
            }

            if (m_spells.druid.pCower &&
                GetAttackersInRangeCount(8.0f))
            {
                Unit* pAttacker = *me->GetAttackers().begin();
                if (CanTryToCastSpell(me, m_spells.druid.pCower))
                {
                    if (DoCastSpell(me, m_spells.druid.pCower) == SPELL_CAST_OK)
                        return;
                }
            }

            if (me->GetComboPoints() > 4 || 
                (me->GetComboPoints() > 2 && pVictim->GetHealthPercent() < 10.0f))
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

            if (m_spells.druid.pRake &&
                CanTryToCastSpell(pVictim, m_spells.druid.pRake))
            {
                if (DoCastSpell(pVictim, m_spells.druid.pRake) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.druid.pShred)
            {
                if (CanTryToCastSpell(pVictim, m_spells.druid.pShred))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pShred) == SPELL_CAST_OK)
                        return;
                }
            }
            else if (m_spells.druid.pClaw &&
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

            if ((me->GetPower(POWER_RAGE) > 80) ||
                (GetAttackersInRangeCount(10.0f) > 1))
            {
                if (m_spells.druid.pDemoralizingRoar &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pDemoralizingRoar))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pDemoralizingRoar) == SPELL_CAST_OK)
                        return;
                }

                if (m_spells.druid.pSwipe &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pSwipe))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pSwipe) == SPELL_CAST_OK)
                        return;
                }
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
            if (pVictim->GetVictim() == me &&
                me->GetEnemyCountInRadiusAround(me,10.0f) > 1 &&
                pVictim->CanReachWithMeleeAutoAttack(me) &&
                !me->HasUnitState(UNIT_STATE_ROOT) &&
                (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != DISTANCING_MOTION_TYPE))
            {
                if (pVictim->HasAura(m_spells.druid.pEntanglingRoots->Id))
                {
                    RunAwayFromTarget(pVictim,true);
                    return;
                }
                if (m_spells.druid.pEntanglingRoots &&
                    CanTryToCastSpell(pVictim, m_spells.druid.pEntanglingRoots))
                {
                    if (DoCastSpell(pVictim, m_spells.druid.pEntanglingRoots) == SPELL_CAST_OK)
                        return;
                }
            }

            if (m_spells.druid.pHurricane &&
                m_aoeSpellTimer <= 0 &&
                (me->GetEnemyCountInRadiusAround(pVictim, 8.0f) > 3) &&
                pVictim->GetHealthPercent() < 75.0f &&
                CanTryToCastSpell(pVictim, m_spells.druid.pHurricane))
            {
                if (DoCastSpell(pVictim, m_spells.druid.pHurricane) == SPELL_CAST_OK)
                    return;
            }

            if (m_spells.druid.pNaturesGrasp &&
                CanTryToCastSpell(me, m_spells.druid.pNaturesGrasp))
            {
                if (DoCastSpell(me, m_spells.druid.pNaturesGrasp) == SPELL_CAST_OK)
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
                pVictim->GetHealthPercent() > 50.0f &&
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

bool PartyBotAI::CheckThreat(Unit const* pTarget)
{
    if (!pTarget || m_threatCheckTimer > 0)
        return m_threatOK;

    float myThreat = 0.0f;
    float currentVictimThreat = 0.0f;

    // Find own reference in target's threat list
    for (const auto i : pTarget->GetThreatManager().getThreatList())
    {
        if (i->getUnitGuid() == me->GetObjectGuid())
            myThreat = i->getThreat();
    }

    if (pTarget->GetThreatManager().getCurrentVictim())
        currentVictimThreat = pTarget->GetThreatManager().getCurrentVictim()->getThreat() * 0.95;

    if (currentVictimThreat > myThreat)
    {
        m_threatOK = true;
        m_threatCheckTimer = 1000;
    }
    else
    {
        m_threatOK = false;
        m_threatCheckTimer = 300;

    }

    return m_threatOK;
}

bool PartyBotAI::CheckCombatInstanceMechanics(bool& pCombatEngagementReady)
{
    /// --------------
    // Return FALSE while not ok with desired mechanics
    /// --------------

    std::list<GameObject*> lBombs;
    Unit* pTarget = nullptr;

    // Set Combat readiness
    pCombatEngagementReady = true;

    // If not in Raid Group or Inside Dungeon return
    if (!(me->GetMap()->IsDungeon() || me->GetGroup()->isRaidGroup()))
        return true;

    // Get Target or Party Target
    if (m_role == ROLE_HEALER)
        pTarget = SelectAttackTarget();
    else
        pTarget = me->GetVictim();

    switch (me->GetMap()->GetId())
    {
        // MOLTEN CORE
        case 409:
            
            // MAGMADAR - Fire Bomb
            // While there is a bomb nearby try to run away
            me->GetGameObjectListWithEntryInGrid(lBombs, 177704, 10.0f);
            for (const auto& pGo : lBombs)
            {
                if (pGo->isSpawned())
                {
                    RunAwayFromObject(pGo, 12.5f);
                    return false;
                }
            }

            // GEHENNAS - Rain of Fire
            // While under Rain of Fire, run away
            if (me->HasAura(19717))
            {
                RunAwayFromAOE(12.0f);
                return false;
            }

            // BARON GEDDON - Living Bomb
            // If has Living Bomb aura, run to specific location in Baon's cave
            if (me->HasAura(20475))
            {
                float x = 680;
                float y = -810;
                float z = me->GetPositionZ();
                me->UpdateAllowedPositionZ(x, y, z);      // update to LOS height if available
                me->GetMotionMaster()->Clear();
                me->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING);
                return false;
            }

            // Target based behaviour
            if (pTarget)
            {
                switch (pTarget->GetEntry())
                {
                    // GARR -  do not use AoE - kill Garr first and do not aggro adds
                    case 12057:
                        // Do not attack if high threat
                        if ((m_role == ROLE_RANGE_DPS || m_role == ROLE_MELEE_DPS) && !CheckThreat(pTarget))
                            pCombatEngagementReady = false;
                        m_aoeSpellTimer = 30 * IN_MILLISECONDS;
                        // Move Away if too close
                        if ((m_role == ROLE_RANGE_DPS || m_role == ROLE_HEALER) &&
                            me->GetDistance(pTarget) < 20.0f)
                        {
                            RunAwayFromTarget(pTarget, true, 25.0f);
                            return false;
                        }
                        break;

                    // Flamewalker Protector
                    case 12119:
                    // Flamewalker Elite
                    case 11664:
                    // GEHENNAS
                    case 12259:
                    // LUCIFRON
                    case 12118:
                        // Do not attack if high threat
                        if ((m_role == ROLE_RANGE_DPS || m_role == ROLE_MELEE_DPS) && !CheckThreat(pTarget))
                            pCombatEngagementReady = false;
                        break;

                    // Try to keep distance
                    // Molten Giant
                    case 11658:
                    // MAGMADAR
                    case 11982:
                    // SHAZZRAH
                    case 12264:

                        // Do not attack if high threat
                        if ((m_role == ROLE_RANGE_DPS || m_role == ROLE_MELEE_DPS) && !CheckThreat(pTarget))
                            pCombatEngagementReady = false;
                        
                        break;

                }

                // BARON GEDDON
                if (pTarget->GetEntry() == 12056)
                {
                    if (m_role == ROLE_TANK)
                        return true;

                    // Do not attack if high threat
                    if ((m_role == ROLE_RANGE_DPS || m_role == ROLE_MELEE_DPS) && !CheckThreat(pTarget->ToCreature()))
                        pCombatEngagementReady = false;
                    
                    // Inferno Aura and prepare for final bomb explosion
                    if (pTarget->HasAura(19695) || pTarget->GetHealthPercent() < 2.5f)
                    {
                        if (me->GetDistance(pTarget) < 22.0f)
                        {
                            RunAwayFromTarget(pTarget, false, 25.0f);
                            return false;
                        }

                        // Melee DPS should not engage while Baron has inferno or low health
                        if (m_role = ROLE_MELEE_DPS)
                            return pCombatEngagementReady = false;
                    }

                    // Ranged and Healers should always keep distance
                    if ((m_role == ROLE_RANGE_DPS || m_role == ROLE_HEALER) &&
                        me->GetDistance(pTarget) < 20.0f)
                    {
                        RunAwayFromTarget(pTarget, true, 25.0f);
                        return false;
                    }
                        
                }

            }

            break;
        
        default:
            break;
    }
    
    return true;
}