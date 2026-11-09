#include "EpicBosses.hpp"
#include "Opcodes.h"
#include "Map.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "WorldPacket.h"
#include "Random.h"
#include <sstream>

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

    std::map<uint32, uint32> Auras;
    std::map<uint32, uint32> Sounds;
    std::map<uint32, uint32> CameraShakes;
    std::map<uint32, TransmogData> Transmogs;
    std::map<uint32, std::string> Yells;
    std::map<uint32, EpicBossWeatherData> Weathers;
    std::map<uint32, std::vector<SpellData>> Spells;
    std::map<uint32, std::vector<AggroResetData>> AggroResets;
}

static std::string TrimString(const std::string& str)
{
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
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
// CreatureScript: Spawn Hooks
//--------------------------------------------------------------------------------------------------
void Epic_Bosses_CreatureScript::OnCreatureAddWorld(Creature* creature)
{
    if (!EpicBossConfig::Enable || !creature)
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

//--------------------------------------------------------------------------------------------------
// UnitScript: Combat Engagement Hooks
//--------------------------------------------------------------------------------------------------
void Epic_Bosses_UnitScript::OnUnitEnterCombat(Unit* unit, Unit* /*victim*/)
{
    if (!EpicBossConfig::Enable || !unit)
        return;

    Creature* creature = unit->ToCreature();
    if (!creature)
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
