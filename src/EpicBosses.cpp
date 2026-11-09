/*
 * Copyright (C) 2016-2026 AzerothCore <http://www.azerothcore.org/>
 * Licensed under the AGPL v3 license.
 */

#include "EpicBosses.hpp"
#include "Opcodes.h"
#include "Map.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "WorldPacket.h"
#include "Random.h"
#include "SpellDefines.h"
#include "SharedDefines.h"
#include "DBCStores.h"
#include <sstream>
#include <set>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>

namespace EpicBossConfig
{
    bool Enable = true;
    bool AurasEnable = true;
    bool SoundsEnable = true;
    bool CameraShakeEnable = true;
    bool TransmogEnable = true;
    bool YellsEnable = true;
    bool WeatherEnable = true;
    bool SpellsEnable = true;
    bool AggroResetEnable = true;

    // Vanilla Boss Immunities Configuration (TBC/WotLK Standardization)
    bool ImmunitiesEnable = true;
    bool VanillaBossStunImmunity = true;
    bool VanillaBossSilenceInterruptImmunity = true;
    bool VanillaBossCrowdControlImmunity = true;
    bool VanillaBossMovementImmunity = true;
    bool VanillaBossDisarmImmunity = true;

    std::set<uint32> ForceBossCreatures;
    std::set<uint32> ImmunityExcludedCreatures;
    std::set<uint32> StunImmunityExcludedCreatures;
    std::set<uint32> SilenceInterruptExcludedCreatures;
    std::set<uint32> CrowdControlExcludedCreatures;
    std::set<uint32> MovementExcludedCreatures;
    std::set<uint32> DisarmExcludedCreatures;

    std::map<uint32, uint32> Auras;
    std::map<uint32, uint32> Sounds;
    std::map<uint32, uint32> CameraShakes;
    std::map<uint32, TransmogData> Transmogs;
    std::map<uint32, std::string> Yells;
    std::map<uint32, EpicBossWeatherData> Weathers;
    std::map<uint32, std::vector<SpellData>> Spells;
    std::map<uint32, std::vector<AggroResetData>> AggroResets;

    struct BossImmunityState
    {
        bool AppliedStunImmunity = false;
        bool AppliedSilenceInterruptImmunity = false;
        bool AppliedCrowdControlImmunity = false;
        bool AppliedMovementImmunity = false;
        bool AppliedDisarmImmunity = false;
    };

    std::shared_mutex ImmunityCacheMutex;
    std::unordered_map<uint64, BossImmunityState> ImmunityCache;
}

static std::string TrimString(const std::string& str)
{
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

static std::set<uint32> ParseUInt32Set(const std::string& str)
{
    std::set<uint32> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, ','))
    {
        std::stringstream ss2(item);
        std::string subItem;
        while (ss2 >> subItem)
        {
            if (!subItem.empty())
            {
                result.insert(static_cast<uint32>(std::stoul(subItem)));
            }
        }
    }
    return result;
}

static bool IsPlayerOwnedOrControlled(Unit const* unit)
{
    if (!unit)
        return false;
    if (unit->IsPlayer())
        return true;

    if (Unit const* owner = unit->GetCharmerOrOwner())
    {
        if (owner->IsPlayer())
            return true;
        if (owner != unit)
            return IsPlayerOwnedOrControlled(owner);
    }
    return false;
}

static bool IsFriendlyOrCityNPC(Creature const* creature, CreatureTemplate const* cInfo)
{
    if (!creature || !cInfo)
        return false;

    if (cInfo->npcflag != 0 || creature->GetUInt32Value(UNIT_NPC_FLAGS) != 0)
        return true;

    if (creature->IsCivilian() || creature->IsGuard() || ((cInfo->flags_extra & 0x00008002) != 0))
        return true;

    if (FactionTemplateEntry const* factionTemplate = creature->GetFactionTemplateEntry())
    {
        if ((factionTemplate->ourMask & 7) != 0 || (factionTemplate->friendlyMask & 7) != 0)
            return true;

        if (factionTemplate->faction != 0 && factionTemplate->faction != 70 && (factionTemplate->hostileMask & 7) == 0)
        {
            if (FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionTemplate->faction))
            {
                if (factionEntry->CanHaveReputation())
                    return true;
            }
        }
    }

    return false;
}

static void ApplyOrUpdateVanillaBossImmunities(Creature* creature)
{
    if (!creature || !creature->IsInWorld() || creature->IsCritter())
        return;

    CreatureTemplate const* cInfo = creature->GetCreatureTemplate();
    if (!cInfo)
        return;

    uint64 guid = creature->GetGUID().GetRawValue();
    uint32 masterEntry = creature->GetEntry();
    uint32 templateEntry = cInfo->Entry;

    bool isTrigger = creature->IsTrigger() || ((cInfo->flags_extra & 0x00000080) != 0);
    bool isSummon = creature->IsSummon() || creature->IsPet() || creature->IsGuardian() || IsPlayerOwnedOrControlled(creature);
    bool isForcedBoss = EpicBossConfig::ForceBossCreatures.count(masterEntry) > 0 ||
                        EpicBossConfig::ForceBossCreatures.count(templateEntry) > 0;
    bool isAnyBoss = isForcedBoss ||
                     creature->IsDungeonBoss() ||
                     ((cInfo->type_flags & 0x00000004) != 0) ||
                     (cInfo->rank == 2) ||
                     (cInfo->rank == 3) ||
                     creature->isWorldBoss();

    bool isEligibleVanillaBoss = EpicBossConfig::Enable &&
                                 EpicBossConfig::ImmunitiesEnable &&
                                 (cInfo->expansion == 0) &&
                                 isAnyBoss &&
                                 !isTrigger &&
                                 !isSummon &&
                                 !IsFriendlyOrCityNPC(creature, cInfo);

    bool isExcludedAll = EpicBossConfig::ImmunityExcludedCreatures.count(masterEntry) > 0 ||
                         EpicBossConfig::ImmunityExcludedCreatures.count(templateEntry) > 0;

    bool isExcludedStun = isExcludedAll ||
                          EpicBossConfig::StunImmunityExcludedCreatures.count(masterEntry) > 0 ||
                          EpicBossConfig::StunImmunityExcludedCreatures.count(templateEntry) > 0;

    bool isExcludedSilenceInterrupt = isExcludedAll ||
                                      EpicBossConfig::SilenceInterruptExcludedCreatures.count(masterEntry) > 0 ||
                                      EpicBossConfig::SilenceInterruptExcludedCreatures.count(templateEntry) > 0;

    bool isExcludedCC = isExcludedAll ||
                        EpicBossConfig::CrowdControlExcludedCreatures.count(masterEntry) > 0 ||
                        EpicBossConfig::CrowdControlExcludedCreatures.count(templateEntry) > 0;

    bool isExcludedMovement = isExcludedAll ||
                              EpicBossConfig::MovementExcludedCreatures.count(masterEntry) > 0 ||
                              EpicBossConfig::MovementExcludedCreatures.count(templateEntry) > 0;

    bool isExcludedDisarm = isExcludedAll ||
                            EpicBossConfig::DisarmExcludedCreatures.count(masterEntry) > 0 ||
                            EpicBossConfig::DisarmExcludedCreatures.count(templateEntry) > 0;

    bool shouldHaveStun             = isEligibleVanillaBoss && EpicBossConfig::VanillaBossStunImmunity && !isExcludedStun;
    bool shouldHaveSilenceInterrupt = isEligibleVanillaBoss && EpicBossConfig::VanillaBossSilenceInterruptImmunity && !isExcludedSilenceInterrupt;
    bool shouldHaveCC               = isEligibleVanillaBoss && EpicBossConfig::VanillaBossCrowdControlImmunity && !isExcludedCC;
    bool shouldHaveMovement         = isEligibleVanillaBoss && EpicBossConfig::VanillaBossMovementImmunity && !isExcludedMovement;
    bool shouldHaveDisarm           = isEligibleVanillaBoss && EpicBossConfig::VanillaBossDisarmImmunity && !isExcludedDisarm;

    EpicBossConfig::BossImmunityState state;
    {
        std::shared_lock<std::shared_mutex> lock(EpicBossConfig::ImmunityCacheMutex);
        auto it = EpicBossConfig::ImmunityCache.find(guid);
        if (it != EpicBossConfig::ImmunityCache.end())
            state = it->second;
    }

    bool stateChanged = false;

    // 1. Stun Immunity
    if (shouldHaveStun)
    {
        creature->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_STUN, true);
        creature->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_STUN, true);
        if (!state.AppliedStunImmunity)
        {
            state.AppliedStunImmunity = true;
            stateChanged = true;
        }
    }
    else if (state.AppliedStunImmunity)
    {
        creature->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_STUN, false);
        creature->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_STUN, false);
        state.AppliedStunImmunity = false;
        stateChanged = true;
    }

    // 2. Silence & Interrupt / Decast Immunity
    if (shouldHaveSilenceInterrupt)
    {
        creature->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_SILENCE, true);
        creature->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_INTERRUPT, true);
        creature->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_SILENCE, true);
        creature->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_INTERRUPT_CAST, true);
        if (!state.AppliedSilenceInterruptImmunity)
        {
            state.AppliedSilenceInterruptImmunity = true;
            stateChanged = true;
        }
    }
    else if (state.AppliedSilenceInterruptImmunity)
    {
        creature->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_SILENCE, false);
        creature->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_INTERRUPT, false);
        creature->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_SILENCE, false);
        creature->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_INTERRUPT_CAST, false);
        state.AppliedSilenceInterruptImmunity = false;
        stateChanged = true;
    }

    // 3. Hard Crowd Control Immunity
    static const Mechanics ccMechanics[] = {
        MECHANIC_CHARM, MECHANIC_DISORIENTED, MECHANIC_FEAR, MECHANIC_SLEEP,
        MECHANIC_POLYMORPH, MECHANIC_BANISH, MECHANIC_SHACKLE, MECHANIC_TURN,
        MECHANIC_HORROR, MECHANIC_FREEZE, MECHANIC_SAPPED
    };

    static const AuraType ccAuras[] = {
        SPELL_AURA_MOD_CHARM, SPELL_AURA_MOD_CONFUSE, SPELL_AURA_MOD_FEAR,
        SPELL_AURA_STRANGULATE, SPELL_AURA_TRANSFORM
    };

    if (shouldHaveCC)
    {
        for (Mechanics mech : ccMechanics)
            creature->ApplySpellImmune(0, IMMUNITY_MECHANIC, mech, true);
        for (AuraType aura : ccAuras)
            creature->ApplySpellImmune(0, IMMUNITY_STATE, aura, true);

        if (!state.AppliedCrowdControlImmunity)
        {
            state.AppliedCrowdControlImmunity = true;
            stateChanged = true;
        }
    }
    else if (state.AppliedCrowdControlImmunity)
    {
        for (Mechanics mech : ccMechanics)
            creature->ApplySpellImmune(0, IMMUNITY_MECHANIC, mech, false);
        for (AuraType aura : ccAuras)
            creature->ApplySpellImmune(0, IMMUNITY_STATE, aura, false);

        state.AppliedCrowdControlImmunity = false;
        stateChanged = true;
    }

    // 4. Movement / Anti-Kiting Immunity
    static const Mechanics moveMechanics[] = { MECHANIC_ROOT, MECHANIC_SNARE, MECHANIC_GRIP };
    static const AuraType moveAuras[] = { SPELL_AURA_MOD_ROOT, SPELL_AURA_MOD_DECREASE_SPEED };
    static const SpellEffects moveEffects[] = { SPELL_EFFECT_KNOCK_BACK, SPELL_EFFECT_KNOCK_BACK_DEST };

    if (shouldHaveMovement)
    {
        for (Mechanics mech : moveMechanics)
            creature->ApplySpellImmune(0, IMMUNITY_MECHANIC, mech, true);
        for (AuraType aura : moveAuras)
            creature->ApplySpellImmune(0, IMMUNITY_STATE, aura, true);
        for (SpellEffects eff : moveEffects)
            creature->ApplySpellImmune(0, IMMUNITY_EFFECT, eff, true);

        if (!state.AppliedMovementImmunity)
        {
            state.AppliedMovementImmunity = true;
            stateChanged = true;
        }
    }
    else if (state.AppliedMovementImmunity)
    {
        for (Mechanics mech : moveMechanics)
            creature->ApplySpellImmune(0, IMMUNITY_MECHANIC, mech, false);
        for (AuraType aura : moveAuras)
            creature->ApplySpellImmune(0, IMMUNITY_STATE, aura, false);
        for (SpellEffects eff : moveEffects)
            creature->ApplySpellImmune(0, IMMUNITY_EFFECT, eff, false);

        state.AppliedMovementImmunity = false;
        stateChanged = true;
    }

    // 5. Disarm Immunity
    static const AuraType disarmAuras[] = {
        SPELL_AURA_MOD_DISARM, SPELL_AURA_MOD_DISARM_OFFHAND, SPELL_AURA_MOD_DISARM_RANGED
    };

    if (shouldHaveDisarm)
    {
        creature->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_DISARM, true);
        for (AuraType aura : disarmAuras)
            creature->ApplySpellImmune(0, IMMUNITY_STATE, aura, true);

        if (!state.AppliedDisarmImmunity)
        {
            state.AppliedDisarmImmunity = true;
            stateChanged = true;
        }
    }
    else if (state.AppliedDisarmImmunity)
    {
        creature->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_DISARM, false);
        for (AuraType aura : disarmAuras)
            creature->ApplySpellImmune(0, IMMUNITY_STATE, aura, false);

        state.AppliedDisarmImmunity = false;
        stateChanged = true;
    }

    if (stateChanged)
    {
        std::unique_lock<std::shared_mutex> lock(EpicBossConfig::ImmunityCacheMutex);
        if (state.AppliedStunImmunity ||
            state.AppliedSilenceInterruptImmunity ||
            state.AppliedCrowdControlImmunity ||
            state.AppliedMovementImmunity ||
            state.AppliedDisarmImmunity)
        {
            EpicBossConfig::ImmunityCache[guid] = state;
        }
        else
        {
            EpicBossConfig::ImmunityCache.erase(guid);
        }
    }
}

static Player* SelectRandomAlivePlayer(Creature* creature, float range)
{
    std::vector<Player*> validTargets;
    Map* map = creature->GetMap();
    if (!map)
        return nullptr;

    auto const& players = map->GetPlayers();
    for (auto itr = players.begin(); itr != players.end(); ++itr)
    {
        Player* player = itr->GetSource();
        if (player && player->IsAlive() && !player->IsGameMaster() && creature->IsWithinDistInMap(player, range))
        {
            validTargets.push_back(player);
        }
    }

    if (!validTargets.empty())
        return validTargets[urand(0, static_cast<uint32>(validTargets.size() - 1))];

    return nullptr;
}

//--------------------------------------------------------------------------------------------------
// Recursive Event: Phase Transmog Check
//--------------------------------------------------------------------------------------------------
class EpicBossTransmogEvent : public BasicEvent
{
public:
    EpicBossTransmogEvent(Creature* creature) : _creature(creature) { }

    bool Execute(uint64 /*e_time*/, uint32 /*p_time*/) override
    {
        if (!_creature || !_creature->IsAlive() || !EpicBossConfig::Enable || !EpicBossConfig::TransmogEnable)
            return true;

        auto it = EpicBossConfig::Transmogs.find(_creature->GetEntry());
        if (it == EpicBossConfig::Transmogs.end())
            return true;

        const TransmogData& data = it->second;

        if (_creature->IsInCombat() && _creature->GetHealthPct() <= data.HealthPct)
        {
            if (_creature->GetDisplayId() != data.NewDisplayID)
            {
                _creature->SetDisplayId(data.NewDisplayID);
                _creature->CastSpell(_creature, 24085, true);
            }
        }
        else if (!_creature->IsInCombat() && _creature->GetHealthPct() >= 99.0f)
        {
            if (_creature->GetDisplayId() != _creature->GetNativeDisplayId())
                _creature->DeMorph();
        }

        _creature->m_Events.AddEvent(new EpicBossTransmogEvent(_creature), _creature->m_Events.CalculateTime(1000));
        return true;
    }

private:
    Creature* _creature;
};

//--------------------------------------------------------------------------------------------------
// Recursive Event: Custom Spells & Animations Check
//--------------------------------------------------------------------------------------------------
struct RuntimeTimerState
{
    uint32 CooldownRemainingMs = 0;
    bool TriggeredOnce = false;
};

class EpicBossSpellEvent : public BasicEvent
{
public:
    EpicBossSpellEvent(Creature* creature, const std::vector<RuntimeTimerState>& states)
        : _creature(creature), _states(states) { }

    bool Execute(uint64 /*e_time*/, uint32 /*p_time*/) override
    {
        if (!_creature || !_creature->IsAlive() || !EpicBossConfig::Enable || !EpicBossConfig::SpellsEnable)
            return true;

        auto it = EpicBossConfig::Spells.find(_creature->GetEntry());
        if (it == EpicBossConfig::Spells.end())
            return true;

        const std::vector<SpellData>& spellList = it->second;
        if (_states.size() != spellList.size())
            _states.assign(spellList.size(), RuntimeTimerState{});

        if (!_creature->IsInCombat())
        {
            for (size_t i = 0; i < spellList.size(); ++i)
            {
                _states[i].TriggeredOnce = false;
                _states[i].CooldownRemainingMs = (spellList[i].IntervalMs > 0) ? (spellList[i].IntervalMs / 2) : 0;
            }

            _creature->m_Events.AddEvent(new EpicBossSpellEvent(_creature, _states), _creature->m_Events.CalculateTime(1000));
            return true;
        }

        for (size_t i = 0; i < spellList.size(); ++i)
        {
            const SpellData& spell = spellList[i];
            RuntimeTimerState& state = _states[i];

            if (state.CooldownRemainingMs > 1000)
            {
                state.CooldownRemainingMs -= 1000;
                continue;
            }
            else
            {
                state.CooldownRemainingMs = 0;
            }

            if (spell.IntervalMs == 0 && state.TriggeredOnce)
                continue;

            if (_creature->GetHealthPct() > spell.HealthPct)
                continue;

            if (!spell.Triggered && _creature->HasUnitState(UNIT_STATE_CASTING))
                continue;

            Unit* target = nullptr;
            if (spell.TargetType == 0)
            {
                target = _creature->GetVictim();
            }
            else if (spell.TargetType == 1)
            {
                target = _creature;
            }
            else if (spell.TargetType == 2)
            {
                target = SelectRandomAlivePlayer(_creature, 40.0f);
                if (!target)
                    target = _creature->GetVictim();
            }

            if (!target)
                continue;

            if (spell.EmoteID > 0)
                _creature->HandleEmoteCommand(spell.EmoteID);

            _creature->CastSpell(target, spell.SpellID, spell.Triggered);

            if (spell.IntervalMs == 0)
                state.TriggeredOnce = true;
            else
                state.CooldownRemainingMs = spell.IntervalMs;
        }

        _creature->m_Events.AddEvent(new EpicBossSpellEvent(_creature, _states), _creature->m_Events.CalculateTime(1000));
        return true;
    }

private:
    Creature* _creature;
    std::vector<RuntimeTimerState> _states;
};

//--------------------------------------------------------------------------------------------------
// Recursive Event: Boss Aggro / Threat Reset Check
//--------------------------------------------------------------------------------------------------
class EpicBossAggroResetEvent : public BasicEvent
{
public:
    EpicBossAggroResetEvent(Creature* creature, const std::vector<RuntimeTimerState>& states)
        : _creature(creature), _states(states) { }

    bool Execute(uint64 /*e_time*/, uint32 /*p_time*/) override
    {
        if (!_creature || !_creature->IsAlive() || !EpicBossConfig::Enable || !EpicBossConfig::AggroResetEnable)
            return true;

        auto it = EpicBossConfig::AggroResets.find(_creature->GetEntry());
        if (it == EpicBossConfig::AggroResets.end())
            return true;

        const std::vector<AggroResetData>& resetList = it->second;
        if (_states.size() != resetList.size())
            _states.assign(resetList.size(), RuntimeTimerState{});

        if (!_creature->IsInCombat())
        {
            for (size_t i = 0; i < resetList.size(); ++i)
            {
                _states[i].TriggeredOnce = false;
                _states[i].CooldownRemainingMs = resetList[i].IntervalMs;
            }

            _creature->m_Events.AddEvent(new EpicBossAggroResetEvent(_creature, _states), _creature->m_Events.CalculateTime(1000));
            return true;
        }

        for (size_t i = 0; i < resetList.size(); ++i)
        {
            const AggroResetData& data = resetList[i];
            RuntimeTimerState& state = _states[i];

            if (state.CooldownRemainingMs > 1000)
            {
                state.CooldownRemainingMs -= 1000;
                continue;
            }
            else
            {
                state.CooldownRemainingMs = 0;
            }

            if (data.IntervalMs == 0 && state.TriggeredOnce)
                continue;

            if (_creature->GetHealthPct() > data.HealthPct)
                continue;

            _creature->GetThreatMgr().ResetAllThreat();

            if (data.EmoteID > 0)
                _creature->HandleEmoteCommand(data.EmoteID);

            if (data.SwitchToRandom)
            {
                if (Player* randomPlayer = SelectRandomAlivePlayer(_creature, 40.0f))
                {
                    _creature->GetThreatMgr().AddThreat(randomPlayer, 1000.0f);
                    if (_creature->AI())
                        _creature->AI()->AttackStart(randomPlayer);
                }
            }

            if (data.IntervalMs == 0)
                state.TriggeredOnce = true;
            else
                state.CooldownRemainingMs = data.IntervalMs;
        }

        _creature->m_Events.AddEvent(new EpicBossAggroResetEvent(_creature, _states), _creature->m_Events.CalculateTime(1000));
        return true;
    }

private:
    Creature* _creature;
    std::vector<RuntimeTimerState> _states;
};

//--------------------------------------------------------------------------------------------------
// WorldScript: Configuration Loader
//--------------------------------------------------------------------------------------------------
void Epic_Bosses_WorldScript::OnAfterConfigLoad(bool /*reload*/)
{
    EpicBossConfig::Enable            = sConfigMgr->GetOption<bool>("EpicBosses.Enable", true);
    EpicBossConfig::AurasEnable       = sConfigMgr->GetOption<bool>("EpicBosses.Auras.Enable", true);
    EpicBossConfig::SoundsEnable      = sConfigMgr->GetOption<bool>("EpicBosses.Sounds.Enable", true);
    EpicBossConfig::CameraShakeEnable = sConfigMgr->GetOption<bool>("EpicBosses.CameraShake.Enable", true);
    EpicBossConfig::TransmogEnable    = sConfigMgr->GetOption<bool>("EpicBosses.Transmog.Enable", true);
    EpicBossConfig::YellsEnable       = sConfigMgr->GetOption<bool>("EpicBosses.Yells.Enable", true);
    EpicBossConfig::WeatherEnable     = sConfigMgr->GetOption<bool>("EpicBosses.Weather.Enable", true);
    EpicBossConfig::SpellsEnable      = sConfigMgr->GetOption<bool>("EpicBosses.Spells.Enable", true);
    EpicBossConfig::AggroResetEnable  = sConfigMgr->GetOption<bool>("EpicBosses.AggroReset.Enable", true);

    // 9. Load Vanilla Boss Immunities Configuration (TBC/WotLK Standardization)
    EpicBossConfig::ImmunitiesEnable                    = sConfigMgr->GetOption<bool>("EpicBosses.Immunities.Enable", true);
    EpicBossConfig::VanillaBossStunImmunity             = sConfigMgr->GetOption<bool>("EpicBosses.Immunities.Vanilla.Stun", true);
    EpicBossConfig::VanillaBossSilenceInterruptImmunity = sConfigMgr->GetOption<bool>("EpicBosses.Immunities.Vanilla.SilenceInterrupt", true);
    EpicBossConfig::VanillaBossCrowdControlImmunity     = sConfigMgr->GetOption<bool>("EpicBosses.Immunities.Vanilla.CrowdControl", true);
    EpicBossConfig::VanillaBossMovementImmunity         = sConfigMgr->GetOption<bool>("EpicBosses.Immunities.Vanilla.Movement", true);
    EpicBossConfig::VanillaBossDisarmImmunity           = sConfigMgr->GetOption<bool>("EpicBosses.Immunities.Vanilla.Disarm", true);

    EpicBossConfig::ForceBossCreatures = ParseUInt32Set(sConfigMgr->GetOption<std::string>(
        "EpicBosses.Immunities.ForceBossCreatures",
        "7023,7274,7359,7603,7604,7605,7606,7607,7608,7797,13233,13596,9025,9026,9031,9041,9042,9056,9033,11143,17307,17536"));

    EpicBossConfig::ImmunityExcludedCreatures = ParseUInt32Set(sConfigMgr->GetOption<std::string>(
        "EpicBosses.Immunities.ExcludedCreatures",
        "15543,3673,3669,3670,3671"));

    EpicBossConfig::StunImmunityExcludedCreatures = ParseUInt32Set(sConfigMgr->GetOption<std::string>(
        "EpicBosses.Immunities.ExcludedStunCreatures",
        "10440,1853"));

    EpicBossConfig::SilenceInterruptExcludedCreatures = ParseUInt32Set(sConfigMgr->GetOption<std::string>(
        "EpicBosses.Immunities.ExcludedSilenceInterruptCreatures",
        "15990,15953,15954,16065,16064,16062,16063,15263,15516,15341,15340,14517,14507,14510,11380,12018,11988,12118,3977,3976,6487,4543,8929,9019,9017,9056,10811,10997,10438,10508,10503,10506,11486,11489,11492,9236,9736,7358,7271,7272,5721,5710,4275"));

    EpicBossConfig::CrowdControlExcludedCreatures = ParseUInt32Set(sConfigMgr->GetOption<std::string>(
        "EpicBosses.Immunities.ExcludedCrowdControlCreatures",
        ""));

    EpicBossConfig::MovementExcludedCreatures = ParseUInt32Set(sConfigMgr->GetOption<std::string>(
        "EpicBosses.Immunities.ExcludedMovementCreatures",
        "15932,11583"));

    EpicBossConfig::DisarmExcludedCreatures = ParseUInt32Set(sConfigMgr->GetOption<std::string>(
        "EpicBosses.Immunities.ExcludedDisarmCreatures",
        "3975,9568,12017,10429,9583,10808"));

    EpicBossConfig::Auras.clear();
    EpicBossConfig::Sounds.clear();
    EpicBossConfig::CameraShakes.clear();
    EpicBossConfig::Transmogs.clear();
    EpicBossConfig::Yells.clear();
    EpicBossConfig::Weathers.clear();
    EpicBossConfig::Spells.clear();
    EpicBossConfig::AggroResets.clear();

    // 1. Parse Auras
    {
        std::string str = sConfigMgr->GetOption<std::string>("EpicBosses.Auras", "");
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::stringstream itemStream(TrimString(item));
            uint32 entry = 0, spellId = 0;
            if (itemStream >> entry >> spellId)
                EpicBossConfig::Auras[entry] = spellId;
        }
    }

    // 2. Parse Sounds
    {
        std::string str = sConfigMgr->GetOption<std::string>("EpicBosses.Sounds", "");
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::stringstream itemStream(TrimString(item));
            uint32 entry = 0, soundId = 0;
            if (itemStream >> entry >> soundId)
                EpicBossConfig::Sounds[entry] = soundId;
        }
    }

    // 3. Parse Camera Shake
    {
        std::string str = sConfigMgr->GetOption<std::string>("EpicBosses.CameraShake", "");
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::stringstream itemStream(TrimString(item));
            uint32 entry = 0, shakeId = 0;
            if (itemStream >> entry >> shakeId)
                EpicBossConfig::CameraShakes[entry] = shakeId;
        }
    }

    // 4. Parse Transmog
    {
        std::string str = sConfigMgr->GetOption<std::string>("EpicBosses.Transmog", "");
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::stringstream itemStream(TrimString(item));
            uint32 entry = 0, displayId = 0;
            float healthPct = 0.0f;
            if (itemStream >> entry >> healthPct >> displayId)
                EpicBossConfig::Transmogs[entry] = TransmogData{ healthPct, displayId };
        }
    }

    // 5. Parse Yells
    {
        std::string str = sConfigMgr->GetOption<std::string>("EpicBosses.Yells", "");
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::string trimmed = TrimString(item);
            size_t pipePos = trimmed.find('|');
            if (pipePos != std::string::npos)
            {
                uint32 entry = static_cast<uint32>(std::stoul(TrimString(trimmed.substr(0, pipePos))));
                std::string text = TrimString(trimmed.substr(pipePos + 1));
                if (entry > 0 && !text.empty())
                    EpicBossConfig::Yells[entry] = text;
            }
        }
    }

    // 6. Parse Weather
    {
        std::string str = sConfigMgr->GetOption<std::string>("EpicBosses.Weather", "");
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::stringstream itemStream(TrimString(item));
            uint32 entry = 0, type = 0;
            float grade = 0.0f;
            if (itemStream >> entry >> type >> grade)
                EpicBossConfig::Weathers[entry] = EpicBossWeatherData{ type, grade };
        }
    }

    // 7. Parse Custom Boss Spells & Animations
    {
        std::string str = sConfigMgr->GetOption<std::string>("EpicBosses.Spells", "");
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::stringstream itemStream(TrimString(item));
            uint32 entry = 0, spellId = 0, intervalMs = 0, targetType = 0, emoteId = 0, triggeredInt = 1;
            float healthPct = 100.0f;
            if (itemStream >> entry >> spellId >> intervalMs >> healthPct >> targetType >> emoteId >> triggeredInt)
            {
                SpellData data;
                data.SpellID    = spellId;
                data.IntervalMs = intervalMs;
                data.HealthPct  = healthPct;
                data.TargetType = targetType;
                data.EmoteID    = emoteId;
                data.Triggered  = (triggeredInt != 0);

                EpicBossConfig::Spells[entry].push_back(data);
            }
        }
    }

    // 8. Parse Boss Aggro / Threat Reset
    {
        std::string str = sConfigMgr->GetOption<std::string>("EpicBosses.AggroReset", "");
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::stringstream itemStream(TrimString(item));
            uint32 entry = 0, intervalMs = 0, switchRandomInt = 1, emoteId = 0;
            float healthPct = 100.0f;
            if (itemStream >> entry >> intervalMs >> healthPct >> switchRandomInt >> emoteId)
            {
                AggroResetData data;
                data.IntervalMs     = intervalMs;
                data.HealthPct      = healthPct;
                data.SwitchToRandom = (switchRandomInt != 0);
                data.EmoteID        = emoteId;

                EpicBossConfig::AggroResets[entry].push_back(data);
            }
        }
    }
}

//--------------------------------------------------------------------------------------------------
// CreatureScript: Spawn & Despawn Hooks
//--------------------------------------------------------------------------------------------------
void Epic_Bosses_CreatureScript::OnCreatureAddWorld(Creature* creature)
{
    if (!creature)
        return;

    ApplyOrUpdateVanillaBossImmunities(creature);

    if (!EpicBossConfig::Enable)
        return;

    uint32 entry = creature->GetEntry();

    if (EpicBossConfig::AurasEnable)
    {
        auto it = EpicBossConfig::Auras.find(entry);
        if (it != EpicBossConfig::Auras.end())
            creature->CastSpell(creature, it->second, true);
    }

    if (EpicBossConfig::TransmogEnable && EpicBossConfig::Transmogs.find(entry) != EpicBossConfig::Transmogs.end())
    {
        creature->m_Events.AddEvent(new EpicBossTransmogEvent(creature), creature->m_Events.CalculateTime(1000));
    }

    if (EpicBossConfig::SpellsEnable)
    {
        auto it = EpicBossConfig::Spells.find(entry);
        if (it != EpicBossConfig::Spells.end())
        {
            std::vector<RuntimeTimerState> initialStates(it->second.size());
            creature->m_Events.AddEvent(new EpicBossSpellEvent(creature, initialStates), creature->m_Events.CalculateTime(1000));
        }
    }

    if (EpicBossConfig::AggroResetEnable)
    {
        auto it = EpicBossConfig::AggroResets.find(entry);
        if (it != EpicBossConfig::AggroResets.end())
        {
            std::vector<RuntimeTimerState> initialStates(it->second.size());
            creature->m_Events.AddEvent(new EpicBossAggroResetEvent(creature, initialStates), creature->m_Events.CalculateTime(1000));
        }
    }
}

void Epic_Bosses_CreatureScript::OnCreatureRemoveWorld(Creature* creature)
{
    if (!creature)
        return;

    uint64 guid = creature->GetGUID().GetRawValue();
    std::unique_lock<std::shared_mutex> lock(EpicBossConfig::ImmunityCacheMutex);
    EpicBossConfig::ImmunityCache.erase(guid);
}

//--------------------------------------------------------------------------------------------------
// UnitScript: Combat Engagement Hooks
//--------------------------------------------------------------------------------------------------
void Epic_Bosses_UnitScript::OnUnitEnterCombat(Unit* unit, Unit* /*victim*/)
{
    if (!unit)
        return;

    Creature* creature = unit->ToCreature();
    if (!creature)
        return;

    // Re-verify immunities on combat pull (ensures live .reload config changes take effect immediately)
    ApplyOrUpdateVanillaBossImmunities(creature);

    if (!EpicBossConfig::Enable)
        return;

    uint32 entry = creature->GetEntry();

    if (EpicBossConfig::SoundsEnable)
    {
        auto it = EpicBossConfig::Sounds.find(entry);
        if (it != EpicBossConfig::Sounds.end())
            creature->PlayDirectSound(it->second);
    }

    if (EpicBossConfig::YellsEnable)
    {
        auto it = EpicBossConfig::Yells.find(entry);
        if (it != EpicBossConfig::Yells.end())
            creature->Yell(it->second, LANG_UNIVERSAL);
    }

    bool hasShake   = EpicBossConfig::CameraShakeEnable && (EpicBossConfig::CameraShakes.find(entry) != EpicBossConfig::CameraShakes.end());
    bool hasWeather = EpicBossConfig::WeatherEnable && (EpicBossConfig::Weathers.find(entry) != EpicBossConfig::Weathers.end());

    if (hasShake || hasWeather)
    {
        Map* map = creature->GetMap();
        if (map)
        {
            auto const& players = map->GetPlayers();
            for (auto itr = players.begin(); itr != players.end(); ++itr)
            {
                Player* player = itr->GetSource();
                if (!player || !player->GetSession() || !creature->IsWithinDistInMap(player, 100.0f))
                    continue;

                if (hasShake)
                {
                    uint32 shakeId = EpicBossConfig::CameraShakes[entry];
                    WorldPacket data(SMSG_CAMERA_SHAKE, 8);
                    data << uint32(shakeId);
                    data << uint32(0);
                    player->GetSession()->SendPacket(&data);
                }

                if (hasWeather)
                {
                    const EpicBossWeatherData& weather = EpicBossConfig::Weathers[entry];
                    WorldPacket data(SMSG_WEATHER, 9);
                    data << uint32(weather.Type);
                    data << float(weather.Grade);
                    data << uint8(0);
                    player->GetSession()->SendPacket(&data);
                }
            }
        }
    }
}

void AddSC_mod_epic_bosses()
{
    new Epic_Bosses_WorldScript();
    new Epic_Bosses_CreatureScript();
    new Epic_Bosses_UnitScript();
}
