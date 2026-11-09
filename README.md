# mod-epic-bosses (AzerothCore 3.3.5a)
<img width="1024" height="506" alt="Banner" src="https://github.com/user-attachments/assets/0f1f4324-d777-44f3-b922-1fec5a327549" />

**`mod-epic-bosses`** is a C++ module for **AzerothCore 3.3.5a (Wrath of the Lich King)** designed to enhance both immersion ("Game Feel") and combat strategies during boss or mob encounters.

Entirely driven by its configuration file (requiring no recompilation or database edits), this module allows you to attach visual, audio, and atmospheric effects as well as **custom spells, synchronized physical animations, and threat/aggro resets** to any creature (`EntryID`) upon spawning, entering combat, or reaching specific health thresholds. 

**For Level/Stats Scaling see** https://github.com/mama128/mod-global-scaling

---

## 🛡️ 100% Non-Intrusive & Safe Architecture

- **Zero Database or DBC Modifications:** Nothing is ever written to or altered in your `acore_world` database (`creature_template`, `smart_scripts`, etc.) or client `.dbc` files. All mechanics and visual enhancements run purely in server memory (RAM) via AzerothCore C++ hooks, alongside existing SmartAI or C++ boss scripts.
- **Full Reversibility:** If you disable the module or remove a creature's `EntryID` from the `.conf` file, the creature immediately reverts to its default Blizzlike behavior.
- **Live Hot-Reloading (`.reload config`):** You can add, tweak, or disable spells, timers, and effects live in-game without restarting the `worldserver`.

---

## ✨ Key Features

The module features **8 independent systems** that can be individually enabled, disabled, and customized in the `.conf` file:

### 1. Spawn Cosmetic Auras (`EpicBosses.Auras`)
Automatically applies a purely visual spell aura (`SpellID`) to the boss as soon as it spawns into the world (`OnCreatureAddWorld`).
- **Format:** `"CreatureID SpellID, CreatureID2 SpellID2"`
- **Examples (Ragefire Chasm):** Intense Fire Aura (`46231`) on Taragaman (`11520`), Shadowform (`16567`) on Jergosh (`11518`), or Green Poison Aura (`33902`) on Bazzalan (`11519`).

### 2. Aggro Sounds and Roars (`EpicBosses.Sounds`)
Plays a distance-based sound effect or voice line (`SoundID`) when the boss enters combat (`OnUnitEnterCombat`).
- **Format:** `"CreatureID SoundID, CreatureID2 SoundID2"`
- **Examples:** C'Thun's Terrifying Laugh (`8580`), Satyr Laugh (`8593`), or Beast Roar (`7136`).

### 3. Screen-Shaking Camera Effects (`EpicBosses.CameraShake`)
Shakes the screen of all players within a **100-yard radius** around the boss upon pull by sending the `SMSG_CAMERA_SHAKE` packet directly to nearby clients.
- **Format:** `"CreatureID ShakeType, CreatureID2 ShakeType2"` (Shake IDs range from `1` to `65`, where `65` is a heavy earthquake shake).

### 4. In-Combat Phase Transmogrification (`EpicBosses.Transmog`)
Monitors the boss's health every second (`1000 ms`) and dynamically transforms its model (`DisplayID`) when dropping below a configured HP percentage threshold, accompanied by a visual transformation flash (`Spell 24085`). Automatically restores the boss's native model if the encounter resets/wipes.
- **Format:** `"CreatureID HealthPct NewDisplayID, CreatureID2 HealthPct2 NewDisplayID2"`
- **Examples:** Taragaman (`11520`) transforms into a Giant Doomguard (`10469`) at `50.0%` HP; Jergosh (`11518`) transforms into a Voidwalker (`4609`) at `40.0%` HP.

### 5. Combat Pull Announcement Yells (`EpicBosses.Yells`)
The boss yells a custom red-text announcement phrase in universal chat upon being engaged.
- **Format:** `"CreatureID|Text, CreatureID2|Text2"` (uses the `|` pipe character as a separator between the Creature ID and the text).

### 6. Localized Weather Manipulation (`EpicBosses.Weather`)
Instantly changes the weather for all players within a **100-yard radius** when the boss is engaged by sending a direct `SMSG_WEATHER` packet to nearby players (bypassing the global `WeatherMgr` so only players fighting the boss are affected).
- **Weather Types:** `0` = Fine/Clear, `1` = Rain, `2` = Snow, `3` = Sandstorm.
- **Intensity:** `0.1` to `1.0`.
- **Format:** `"CreatureID WeatherType Intensity, CreatureID2 WeatherType2 Intensity2"`
- *(Note: Weather effects are rendered by the client in outdoor zones and open-sky instances such as Zul'Gurub or Ruins of Ahn'Qiraj).*

### 7. Custom Boss Spells & Animations (`EpicBosses.Spells`)
Allows you to customize enemy combat strategies by assigning **multiple active spells/abilities** and **synchronized physical animations** (`EmoteID`) to any boss or mob. You can list the same `CreatureID` multiple times to build a full custom rotation.
- **Format:** `"CreatureID SpellID IntervalMs HealthPct TargetType EmoteID Triggered, ..."`

| Parameter | Description | Values / Examples |
| :--- | :--- | :--- |
| **CreatureID** | Creature Entry ID | e.g., `11520` (Taragaman) |
| **SpellID** | Spell ID from `Spell.dbc` | e.g., `27577` (Charge), `8599` (Enrage), `16727` (War Stomp) |
| **IntervalMs** | Cooldown timer in milliseconds | e.g., `12000` (every 12s), or `0` for a **one-time phase cast** |
| **HealthPct** | HP% threshold to start casting | `100.0` (entire fight), `30.0` (only when HP% is 30%) |
| **TargetType** | Target selection logic | `0` = Current Victim (Tank), `1` = Self, `2` = Random Alive Player (40 yds) |
| **EmoteID** | Forced physical body animation | `0` = Native spell anim, `15` = Roar, `18` = 2H Slam, `53` = Directed Cast, `54` = Omni Cast |
| **Triggered** | Cast behavior | `0` = Normal cast (uses cast bar), `1` = Instant cast (great when paired with `EmoteID`) |

### 8. Boss Aggro / Threat Reset (`EpicBosses.AggroReset`)
Wipes the boss's threat table (`ResetAllThreat()`) on a repeating timer or as a one-time phase transition, with an option to immediately force the boss onto a random nearby player.
- **Format:** `"CreatureID IntervalMs HealthPct SwitchToRandom EmoteID, ..."`

| Parameter | Description | Values / Examples |
| :--- | :--- | :--- |
| **CreatureID** | Creature Entry ID | e.g., `11520` |
| **IntervalMs** | Cooldown timer in milliseconds | e.g., `20000` (every 20s), or `0` for a **one-time phase threat wipe** |
| **HealthPct** | HP% threshold to trigger | `100.0` (entire fight), `50.0` (only when HP% is 50%) |
| **SwitchToRandom** | Target switch behavior | `1` = Adds threat to a random alive player within 40 yds, `0` = Simply Reset boss threat |
| **EmoteID** | Animation played on threat wipe | `15` = Battle Roar, `0` = None |

---

## ⚙️ Configuration Example

Below is a full example setup for **Ragefire Chasm** bosses (`Oggleflint 11517`, `Jergosh 11518`, `Bazzalan 11519`, `Taragaman 11520`):

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

# 7. Custom Boss Spells & Animations (CreatureID SpellID IntervalMs HealthPct TargetType EmoteID Triggered)
EpicBosses.Spells.Enable = 1
EpicBosses.Spells = "11517 16727 12000 100.0 0 18 1, 11518 20741 10000 100.0 0 54 1, 11519 29293 15000 75.0 0 53 1, 11520 27577 14000 100.0 2 0 1, 11520 8599 0 30.0 1 15 1"

# 8. Boss Aggro / Threat Reset (CreatureID IntervalMs HealthPct SwitchToRandom EmoteID)
EpicBosses.AggroReset.Enable = 1
EpicBosses.AggroReset = "11520 20000 100.0 1 15"

---

🔄 Disabling or Modifying Features Live In-Game

You can disable the entire module or toggle specific mechanics at any time without restarting the server:

    Open mod_epic_bosses.conf inside your server's configs/modules/ directory.

    Set EpicBosses.Enable = 0 to turn off the entire module, or set any specific feature toggle to 0 (e.g., EpicBosses.Spells.Enable = 0 or EpicBosses.AggroReset.Enable = 0).

    In-game, run the GM command:
    Plaintext

    .reload config

    Immediate Effect: All active combat timers (spells, phase checks, aggro resets) check the configuration state every second and will stop immediately. If an already-spawned boss currently has a spawn aura or phase morph applied, targeting it and typing .respawn will reset its visual state back to normal.

---

🛠️ Installation & Usage

    Clone or copy the mod-epic-bosses directory into your AzerothCore modules/ folder:
    Bash

    cd /path/to/azerothcore-wotlk/modules
    git clone [https://github.com/mama128/mod-epic-bosses.git](https://github.com/mama128/mod-epic-bosses.git) mod-epic-bosses

    Re-run CMake and compile your AzerothCore solution.

    Copy conf/mod_epic_bosses.conf.dist into your server's configs/modules/ folder and rename it to mod_epic_bosses.conf.

    Edit mod_epic_bosses.conf to customize your bosses and reload changes live in-game at any time using .reload config.
