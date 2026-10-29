/*
 * Copyright (C) 2016-2026 AzerothCore <http://www.azerothcore.org/>
 * Licensed under the AGPL v3 license.
 */

#include "EpicBosses.hpp"
#include <sstream>
#include <map>
#include <shared_mutex>
#include <mutex>
#include <atomic>
#include "Opcodes.h"
#include "Cell.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"

namespace
{
    class EpicBossesBasicEvent : public BasicEvent
    {
    public:
        EpicBossesBasicEvent(std::function<void()> callback) : _callback(std::move(callback)) {}
        bool Execute(uint64, uint32) override { if (_callback) _callback(); return true; }
        bool IsDeletable() const override { return true; } 
    private:
        std::function<void()> _callback;
    };
}

namespace EpicBossesConfig
{
    std::atomic<bool> isConfigInit(false);
    std::shared_mutex ConfigRWLock;

    bool Enable = true;
    bool AurasEnable = true;
    bool SoundsEnable = true;
    bool CameraShakeEnable = true;
    bool TransmogEnable = true;
    bool YellsEnable = true;
    bool WeatherEnable = true;

    std::map<uint32, uint32> Auras;
    std::map<uint32, uint32> Sounds;
    std::map<uint32, uint32> CameraShakes;
    
    struct TransmogData { float HealthPct; uint32 DisplayId; };
    std::map<uint32, TransmogData> Transmogs;
    
    struct WeatherData { uint32 Type; float Grade; };
    std::map<uint32, WeatherData> Weathers;
    
    std::map<uint32, std::string> Yells;

    std::map<uint32, uint32> ParseUInt32Map(std::string const& str)
    {
        std::map<uint32, uint32> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::stringstream ss2(item);
            uint32 id, val;
            if (ss2 >> id >> val) result[id] = val;
        }
        return result;
    }

    std::map<uint32, TransmogData> ParseTransmogMap(std::string const& str)
    {
        std::map<uint32, TransmogData> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::stringstream ss2(item);
            uint32 id, display;
            float pct;
            if (ss2 >> id >> pct >> display) result[id] = { pct, display };
        }
        return result;
    }

    std::map<uint32, WeatherData> ParseWeatherMap(std::string const& str)
    {
        std::map<uint32, WeatherData> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::stringstream ss2(item);
            uint32 id, type;
            float grade;
            if (ss2 >> id >> type >> grade) result[id] = { type, grade };
        }
        return result;
    }

    std::map<uint32, std::string> ParseYellsMap(std::string const& str)
    {
        std::map<uint32, std::string> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            size_t pos = item.find('|');
            if (pos != std::string::npos)
            {
                uint32 id = std::stoul(item.substr(0, pos));
                std::string text = item.substr(pos + 1);
                // Trim leading spaces
                size_t start = text.find_first_not_of(" ");
                if(start != std::string::npos) text = text.substr(start);
                result[id] = text;
            }
        }
        return result;
    }

    void Load(bool reload)
    {
        std::unique_lock<std::shared_mutex> lock(ConfigRWLock);
        if (!reload && isConfigInit.load()) return;

        Enable = sConfigMgr->GetOption<bool>("EpicBosses.Enable", true);
        AurasEnable = sConfigMgr->GetOption<bool>("EpicBosses.Auras.Enable", true);
        SoundsEnable = sConfigMgr->GetOption<bool>("EpicBosses.Sounds.Enable", true);
        CameraShakeEnable = sConfigMgr->GetOption<bool>("EpicBosses.CameraShake.Enable", true);
        TransmogEnable = sConfigMgr->GetOption<bool>("EpicBosses.Transmog.Enable", true);
        YellsEnable = sConfigMgr->GetOption<bool>("EpicBosses.Yells.Enable", true);
        WeatherEnable = sConfigMgr->GetOption<bool>("EpicBosses.Weather.Enable", true);

        Auras = ParseUInt32Map(sConfigMgr->GetOption<std::string>("EpicBosses.Auras", ""));
        Sounds = ParseUInt32Map(sConfigMgr->GetOption<std::string>("EpicBosses.Sounds", ""));
        CameraShakes = ParseUInt32Map(sConfigMgr->GetOption<std::string>("EpicBosses.CameraShake", ""));
        Transmogs = ParseTransmogMap(sConfigMgr->GetOption<std::string>("EpicBosses.Transmog", ""));
        Weathers = ParseWeatherMap(sConfigMgr->GetOption<std::string>("EpicBosses.Weather", ""));
        Yells = ParseYellsMap(sConfigMgr->GetOption<std::string>("EpicBosses.Yells", ""));

        isConfigInit.store(true);
    }
    
    // Recursive function to monitor boss HP and trigger transmogrification
    void ScheduleTransmogCheck(Creature* creature, float triggerPct, uint32 newDisplay)
    {
        creature->m_Events.AddEvent(new EpicBossesBasicEvent(
            [creature, triggerPct, newDisplay]()
            {
                if (creature && creature->IsInWorld() && creature->IsAlive())
                {
                    if (creature->GetHealthPct() <= triggerPct)
                    {
                        if (creature->GetDisplayId() != newDisplay)
                        {
                            creature->SetDisplayId(newDisplay);
                            // Adds a visual smoke/explosion effect during transformation
                            creature->CastSpell(creature, 64446, true); 
                        }
                    }
                    else
                    {
                        // If the boss hasn't reached the HP threshold yet, check again in 1 second
                        ScheduleTransmogCheck(creature, triggerPct, newDisplay);
                    }
                }
            }
        ), creature->m_Events.CalculateTime(1000));
    }
}

// ---------------------------------------------------------
// World Script (Configuration Loading)
// ---------------------------------------------------------
Epic_Bosses_WorldScript::Epic_Bosses_WorldScript() : WorldScript("Epic_Bosses_WorldScript") { }
void Epic_Bosses_WorldScript::OnBeforeConfigLoad(bool /*reload*/) { EpicBossesConfig::isConfigInit.store(false); }
void Epic_Bosses_WorldScript::OnAfterConfigLoad(bool reload) { EpicBossesConfig::Load(reload); }

// ---------------------------------------------------------
// Creature Script (Spawn Effects)
// ---------------------------------------------------------
Epic_Bosses_CreatureScript::Epic_Bosses_CreatureScript() : AllCreatureScript("Epic_Bosses_CreatureScript") { }

void Epic_Bosses_CreatureScript::OnCreatureAddWorld(Creature* creature)
{
    if (!creature) return;
    
    std::shared_lock<std::shared_mutex> lock(EpicBossesConfig::ConfigRWLock);
    if (!EpicBossesConfig::Enable) return;
    uint32 entry = creature->GetEntry();
    
    // 1. Cosmetic Auras
    if (EpicBossesConfig::AurasEnable)
    {
        auto itAura = EpicBossesConfig::Auras.find(entry);
        if (itAura != EpicBossesConfig::Auras.end())
        {
            creature->CastSpell(creature, itAura->second, true); 
        }
    }

    // 2. Transmog Check Setup
    if (EpicBossesConfig::TransmogEnable)
    {
        auto itTrans = EpicBossesConfig::Transmogs.find(entry);
        if (itTrans != EpicBossesConfig::Transmogs.end())
        {
            EpicBossesConfig::ScheduleTransmogCheck(creature, itTrans->second.HealthPct, itTrans->second.DisplayId);
        }
    }
}

// ---------------------------------------------------------
// Unit Script (Combat Effects: Sounds, Shakes, Weather, Yells)
// ---------------------------------------------------------
Epic_Bosses_UnitScript::Epic_Bosses_UnitScript() : UnitScript("Epic_Bosses_UnitScript") { }

void Epic_Bosses_UnitScript::OnUnitEnterCombat(Unit* unit, Unit* /*victim*/)
{
    if (!unit) return;
    Creature* creature = unit->ToCreature();
    if (!creature) return;

    std::shared_lock<std::shared_mutex> lock(EpicBossesConfig::ConfigRWLock);
    if (!EpicBossesConfig::Enable) return;
    uint32 entry = creature->GetEntry();

    // 3. Aggro Sounds
    if (EpicBossesConfig::SoundsEnable)
    {
        auto itSound = EpicBossesConfig::Sounds.find(entry);
        if (itSound != EpicBossesConfig::Sounds.end())
        {
            creature->PlayDistanceSound(itSound->second);
        }
    }

    // 4. Announcement Yells
    if (EpicBossesConfig::YellsEnable)
    {
        auto itYell = EpicBossesConfig::Yells.find(entry);
        if (itYell != EpicBossesConfig::Yells.end())
        {
            creature->Yell(itYell->second, LANG_UNIVERSAL, nullptr);
        }
    }

    // 5. Camera Shakes
    if (EpicBossesConfig::CameraShakeEnable)
    {
        auto itShake = EpicBossesConfig::CameraShakes.find(entry);
        if (itShake != EpicBossesConfig::CameraShakes.end())
        {
            float radius = 100.0f;
            std::list<Player*> players;
            Acore::AnyPlayerInObjectRangeCheck check(creature, radius);
            Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(creature, players, check);
            
            Cell::VisitObjects(creature, searcher, radius);

            for (Player* p : players)
            {
                WorldPacket data(SMSG_CAMERA_SHAKE, 8);
                data << uint32(itShake->second); // Shake ID
                data << uint32(0);
                p->GetSession()->SendPacket(&data);
            }
        }
    }

    // 6. Weather Manipulation (Bypass direct vers le client)
    if (EpicBossesConfig::WeatherEnable)
    {
        auto itWeather = EpicBossesConfig::Weathers.find(entry);
        if (itWeather != EpicBossesConfig::Weathers.end())
        {
            float radius = 150.0f;
            std::list<Player*> players;
            Acore::AnyPlayerInObjectRangeCheck check(creature, radius);
            Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(creature, players, check);
            
            Cell::VisitObjects(creature, searcher, radius);

            for (Player* p : players)
            {
                // Envoi manuel du paquet météo (SMSG_WEATHER)
                // Évite de passer par WeatherMgr et affecte uniquement les joueurs du boss
                WorldPacket data(SMSG_WEATHER, 9);
                data << uint32(itWeather->second.Type);  // 0=Fine, 1=Rain, 2=Snow, 3=Sandstorm
                data << float(itWeather->second.Grade);  // Intensité (0.1 à 1.0)
                data << uint8(0);                        // Changement instantané (0)
                p->GetSession()->SendPacket(&data);
            }
        }
    }
}