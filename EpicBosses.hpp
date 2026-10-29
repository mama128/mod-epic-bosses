/*
 * Copyright (C) 2016-2026 AzerothCore <http://www.azerothcore.org/>
 * Licensed under the AGPL v3 license.
 */

#ifndef EPIC_BOSSES_HPP
#define EPIC_BOSSES_HPP

#include "ScriptMgr.h"
#include "Config.h"
#include "Creature.h"
#include "Player.h"
#include "WeatherMgr.h"

class Epic_Bosses_WorldScript : public WorldScript
{
public:
    Epic_Bosses_WorldScript();
    void OnBeforeConfigLoad(bool reload) override;
    void OnAfterConfigLoad(bool reload) override;
};

class Epic_Bosses_CreatureScript : public AllCreatureScript
{
public:
    Epic_Bosses_CreatureScript();
    void OnCreatureAddWorld(Creature* creature) override;
};

class Epic_Bosses_UnitScript : public UnitScript
{
public:
    Epic_Bosses_UnitScript();
    void OnUnitEnterCombat(Unit* unit, Unit* victim) override;
};

#endif // EPIC_BOSSES_HPP