# mod-epic-bosses (AzerothCore 3.3.5a)
<img width="1024" height="506" alt="Banner" src="https://github.com/user-attachments/assets/0f1f4324-d777-44f3-b922-1fec5a327549" />


**`mod-epic-bosses`** is a C++ module for **AzerothCore 3.3.5a (Wrath of the Lich King)** designed to enhance immersion and "Game Feel" during boss encounters.

Entirely driven by its configuration file (requiring no recompilation or database edits), this module allows you to attach visual, audio, and atmospheric effects to any creature (`EntryID`) upon spawning, entering combat, or reaching a specific health threshold.

---

## ✨ Key Features

The module features 6 independent systems that can be individually enabled, disabled, and customized in the `.conf` file:

### 1. Spawn Cosmetic Auras (`EpicBosses.Auras`)
Automatically applies a purely visual spell aura (`SpellID`) to the boss as soon as it spawns into the world (`OnCreatureAddWorld`).
* **Format:** `"CreatureID SpellID, CreatureID2 SpellID2"`
* **Examples (Ragefire Chasm):** Intense Fire Aura (`46231`) on Taragaman (`11520`), Shadowform (`16567`) on Jergosh (`11518`), or Green Poison Aura (`33902`) on Bazzalan (`11519`).

### 2. Aggro Sounds and Roars (`EpicBosses.Sounds`)
Plays a distance-based sound effect or voice line (`SoundID`) when the boss enters combat (`OnUnitEnterCombat`).
* **Format:** `"CreatureID SoundID, CreatureID2 SoundID2"`
* **Examples:** C'Thun's Terrifying Laugh (`8580`), Satyr Laugh (`8593`), or Beast Roar (`7136`).

### 3. Screen-Shaking Camera Effects (`EpicBosses.CameraShake`)
Shakes the screen of all players within a **100-yard radius** around the boss upon pull by sending the `SMSG_CAMERA_SHAKE` packet directly to nearby clients.
* **Format:** `"CreatureID ShakeType, CreatureID2 ShakeType2"` (Shake IDs range from `1` to `65`, where `65` is a heavy earthquake shake).

### 4. In-Combat Phase Transmogrification (`EpicBosses.Transmog`)
Monitors the boss's health every second (`1000 ms`) and dynamically transforms its model (`DisplayID`) when dropping below a configured HP percentage threshold, accompanied by a visual smoke/explosion burst (`Spell 64446`).
* **Format:** `"CreatureID HealthPct NewDisplayID, CreatureID2 HealthPct2 NewDisplayID2"`
* **Examples:** Taragaman (`11520`) transforms into a Giant Doomguard (`10469`) at $50.0\%$ HP; Jergosh (`11518`) transforms into a Voidwalker (`4609`) at $40.0\%$ HP.

### 5. Combat Pull Announcement Yells (`EpicBosses.Yells`)
The boss yells a custom red-text announcement phrase in universal chat upon being engaged.
* **Format:** `"CreatureID|Text, CreatureID2|Text2"` (uses the `|` pipe character as a separator between the Creature ID and the text).

### 6. Localized Weather Manipulation (`EpicBosses.Weather`)
Instantly changes the weather for all players within a **150-yard radius** when the boss is engaged by sending a direct `SMSG_WEATHER` packet to nearby players (bypassing the global `WeatherMgr` so only players fighting the boss are affected).
* **Weather Types:** `0` = Fine/Clear, `1` = Rain, `2` = Snow, `3` = Sandstorm.
* **Intensity:** `0.1` to `1.0`.
* **Format:** `"CreatureID WeatherType Intensity, CreatureID2 WeatherType2 Intensity2"`
* *(Note: Weather effects are rendered by the client in outdoor zones and open-sky instances such as Zul'Gurub or Ruins of Ahn'Qiraj).*

---

## ⚙️ Configuration Example

Below is the pre-configured setup for **Ragefire Chasm** bosses (`Oggleflint 11517`, `Jergosh 11518`, `Bazzalan 11519`, `Taragaman 11520`):

```ini
[worldserver]
[EpicBosses]

EpicBosses.Enable = 1

# 1. Cosmetic Auras (CreatureID SpellID)
EpicBosses.Auras.Enable = 1
EpicBosses.Auras = "11517 23511, 11518 16567, 11519 33902, 11520 46231"

# 2. Aggro Sounds and Roars (CreatureID SoundID)
EpicBosses.Sounds.Enable = 1
EpicBosses.Sounds = "11517 7136, 11518 8316, 11519 8593, 11520 8580"

# 3. Camera Shakes (CreatureID ShakeType[1-65])
EpicBosses.CameraShake.Enable = 1
EpicBosses.CameraShake = "11517 65, 11518 64, 11519 63, 11520 65"

# 4. In-Combat Transmogrification / Phase Change (CreatureID HealthPct NewDisplayID)
EpicBosses.Transmog.Enable = 1
EpicBosses.Transmog = "11517 50.0 14352, 11518 40.0 4609, 11519 30.0 18635, 11520 50.0 10469"

# 5. Pull Announcement Yells (CreatureID|Text)
EpicBosses.Yells.Enable = 1
EpicBosses.Yells = "11517|My cave! You should not have come!, 11518|The Burning Blade will consume this world!, 11519|The poison already flows through your veins..., 11520|I smell the delicious scent of your mortal souls..."

# 6. Weather Manipulation (CreatureID WeatherType[1=Rain,2=Snow,3=Sandstorm] Intensity[0.1-1.0])
EpicBosses.Weather.Enable = 1
EpicBosses.Weather = "11517 3 1.0, 11518 1 1.0, 11519 1 0.5, 11520 2 1.0"
```

---

## 🛠️ Installation & Usage

1. Clone or copy the `mod-epic-bosses` directory into your AzerothCore `modules/` folder:
   ```bash
   cd /path/to/azerothcore-wotlk/modules
   git clone <your-repo-url> mod-epic-bosses
   ```
2. Re-run **CMake** and compile your AzerothCore solution.
3. Copy `mod_epic_bosses.conf.dist` into your server's `configs/modules/` folder and rename it to `mod_epic_bosses.conf`.
4. Edit `mod_epic_bosses.conf` to add your custom boss Entry IDs and effects. You can reload changes live in-game at any time using `.reload config`.
