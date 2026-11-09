#ifndef EPIC_BOSSES_HPP
#define EPIC_BOSSES_HPP

#include "ScriptMgr.h"
#include "Config.h"
#include "Creature.h"
#include "Player.h"
#include "Chat.h"
#include <map>
#include <vector>
#include <string>

struct TransmogData
{
    float HealthPct;
    uint32 NewDisplayID;
};

struct EpicBossWeatherData
{
    uint32 Type;
    float Grade;
};

struct SpellData
{
    uint32 SpellID;
    uint32 IntervalMs;
    float HealthPct;
    uint32 TargetType;
    uint32 EmoteID;
    bool Triggered;
};

struct AggroResetData
{
    uint32 IntervalMs;
    float HealthPct;
    bool SwitchToRandom;
    uint32 EmoteID;
};

class Epic_Bosses_WorldScript : public WorldScript
{
public:
    Epic_Bosses_WorldScript() : WorldScript("Epic_Bosses_WorldScript") { }

    void OnAfterConfigLoad(bool reload) override;
};

class Epic_Bosses_CreatureScript : public AllCreatureScript
{
public:
    Epic_Bosses_CreatureScript() : AllCreatureScript("Epic_Bosses_CreatureScript") { }

    void OnCreatureAddWorld(Creature* creature) override;
};

class Epic_Bosses_UnitScript : public UnitScript
{
public:
    Epic_Bosses_UnitScript() : UnitScript("Epic_Bosses_UnitScript") { }

    void OnUnitEnterCombat(Unit* unit, Unit* victim) override;
};

void AddSC_mod_epic_bosses();

#endif // EPIC_BOSSES_HPP
