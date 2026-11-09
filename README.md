# mod-epic-bosses (AzerothCore 3.3.5a)
<img width="1024" height="506" alt="Banner" src="https://github.com/user-attachments/assets/0f1f4324-d777-44f3-b922-1fec5a327549" />

**`mod-epic-bosses`** is a C++ module for **AzerothCore 3.3.5a (Wrath of the Lich King)** designed to enhance both immersion ("Game Feel") and combat strategies during boss or mob encounters.

Entirely driven by its configuration file (requiring no recompilation or database edits), this module allows you to attach visual, audio, and atmospheric effects as well as **custom spells, synchronized physical animations, threat/aggro resets, and standardized TBC/WotLK boss immunities for Vanilla bosses** to any creature (`EntryID`) upon spawning, entering combat, or reaching specific health thresholds.

**For Level/Stats Scaling see:** https://github.com/mama128/mod-global-scaling

---

## 🛡️ 100% Non-Intrusive & Safe Architecture

- **Zero Database or DBC Modifications:** Nothing is ever written to or altered in your `acore_world` database (`creature_template`, `smart_scripts`, etc.) or client `.dbc` files. All mechanics, immunities, and visual enhancements run purely in server memory (RAM) via AzerothCore C++ hooks, alongside existing SmartAI or C++ boss scripts.
- **Full Reversibility:** If you disable the module or remove a creature's `EntryID` from the `.conf` file, the creature immediately reverts to its default Blizzlike behavior (preserving any native `MechanicImmuneMask` from the database).
- **Live Hot-Reloading (`.reload config`):** You can add, tweak, or disable spells, timers, effects, and boss immunity exclusions live in-game without restarting the `worldserver`.

---

## ✨ Key Features

The module features **9 independent systems** that can be individually enabled, disabled, and customized in the `.conf` file:

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
| **SwitchToRandom** | Target switch behavior | `1` = Adds threat to a random alive player within 40 yds, `0` = Simply reset boss threat |
| **EmoteID** | Animation played on threat wipe | `15` = Battle Roar, `0` = None |

### 9. Vanilla Boss Immunities — TBC / WotLK Standardization (`EpicBosses.Immunities`)
In Classic/Vanilla (`expansion == 0`), many dungeon bosses, rare elites, and even some raid bosses lack standardized `MechanicImmuneMask` flags in the database, allowing players (especially on scaled level 80 servers) to trivialize encounters via stuns, kiting, disarms, or hard crowd control.

This system automatically upgrades **Vanilla Dungeon Bosses, Raid Bosses, World Bosses, Rare Elites (`rank == 2`), and custom `ForceBossCreatures`** to match TBC/WotLK boss immunity standards (while automatically ignoring normal/elite trash adds, friendly NPCs, player pets, and triggers):

| Category Toggle | Blocked Mechanics / Effects |
| :--- | :--- |
| **`Vanilla.Stun`** | All Stun mechanics and auras (*Hammer of Justice*, *Cheap Shot*, *Kidney Shot*, *Deep Freeze*, *Intercept*, etc.). |
| **`Vanilla.SilenceInterrupt`** | All Silence & Interrupt / Decast effects (*Kick*, *Counterspell*, *Pummel*, *Mind Freeze*, *Wind Shear*, *Silence*, *Gag Order*, etc.). |
| **`Vanilla.CrowdControl`** | All Hard Crowd Control (*Polymorph*, *Hex*, *Fear*, *Charm/Mind Control*, *Sleep*, *Banish*, *Cyclone*, *Shackle Undead*, *Turn Evil*, *Sap*, *Gouge*, *Repentance*, *Blind*, *Horror/Death Coil*, *Freeze*). |
| **`Vanilla.Movement`** | All Root, Snare, and Kiting mechanics (*Frost Nova*, *Entangling Roots*, *Movement Speed Slows*, *Chains of Ice*, *Death Grip*, and *Knockbacks* like *Typhoon*/*Thunderstorm*). **Does NOT block tank melee attack-speed debuffs** (*Thunder Clap*, *Icy Touch*, *Infected Wounds*). |
| **`Vanilla.Disarm`** | All Main-Hand, Off-Hand, and Ranged/Shield Disarms (*Warrior Disarm*, *Rogue Dismantle*, *Psychic Horror Disarm*, *Chimera Shot - Scorpid*). |

- **Pre-Configured Encounter Exclusions:** Includes out-of-the-box exclusion lists (`ExcludedCreatures`, `ExcludedStunCreatures`, `ExcludedSilenceInterruptCreatures`, `ExcludedCrowdControlCreatures`, `ExcludedMovementCreatures`, `ExcludedDisarmCreatures`) for Vanilla bosses whose official strategies require kicking heals/lethal casts, stunning, kiting, or disarming (such as *Kel'Thuzad*, *Prophet Skeram*, *Princess Yauj*, *High Inquisitor Whitemane*, *Princess Moira*, *Gluth*, *Herod*, etc.).

---

## ⚙️ Configuration Example

Below is a full example setup for **Ragefire Chasm** bosses (`Oggleflint 11517`, `Jergosh 11518`, `Bazzalan 11519`, `Taragaman 11520`) along with the **Vanilla Boss Immunities** configuration:

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

# 9. Vanilla Boss Immunities (TBC / WotLK Standardization)
EpicBosses.Immunities.Enable = 1
EpicBosses.Immunities.Vanilla.Stun = 1
EpicBosses.Immunities.Vanilla.SilenceInterrupt = 1
EpicBosses.Immunities.Vanilla.CrowdControl = 1
EpicBosses.Immunities.Vanilla.Movement = 1
EpicBosses.Immunities.Vanilla.Disarm = 1
EpicBosses.Immunities.ForceBossCreatures = "7023,7274,7359,7603,7604,7605,7606,7607,7608,7797,13233,13596,9025,9026,9031,9041,9042,9056,9033,11143,17307,17536"
EpicBosses.Immunities.ExcludedCreatures = "15543,3673,3669,3670,3671"
EpicBosses.Immunities.ExcludedStunCreatures = "10440,1853"
EpicBosses.Immunities.ExcludedSilenceInterruptCreatures = "15990,15953,15954,16065,16064,16062,16063,15263,15516,15341,15340,14517,14507,14510,11380,12018,11988,12118,3977,3976,6487,4543,8929,9019,9017,9056,10811,10997,10438,10508,10503,10506,11486,11489,11492,9236,9736,7358,7271,7272,5721,5710,4275"
EpicBosses.Immunities.ExcludedCrowdControlCreatures = ""
EpicBosses.Immunities.ExcludedMovementCreatures = "15932,11583"
EpicBosses.Immunities.ExcludedDisarmCreatures = "3975,9568,12017,10429,9583,10808"
```

---

## 🔄 Disabling or Modifying Features Live In-Game

You can disable the entire module, toggle specific mechanics, or add a boss to an immunity exclusion list at any time without restarting the server:

1. Open `mod_epic_bosses.conf` inside your server's `configs/modules/` directory.
2. Set `EpicBosses.Enable = 0` to turn off the entire module, or set any specific feature toggle to `0` (e.g., `EpicBosses.Spells.Enable = 0` or `EpicBosses.Immunities.Enable = 0`).
3. In-game, run the GM command:
   ```text
   .reload config
   ```
4. **Immediate Effect:**
   - All active combat timers (spells, phase checks, aggro resets) check the configuration state every second and will stop immediately.
   - **Boss Immunities** are automatically re-evaluated both on spawn (`OnCreatureAddWorld`) and as soon as a boss enters combat (`OnUnitEnterCombat`), meaning any exclusion added via `.reload config` takes effect on the very next pull without removing the boss's native database immunities.
   - If an already-spawned boss currently has a spawn aura or phase morph applied, targeting it and typing `.respawn` will reset its visual state back to normal.

---

## 🛠️ Installation & Usage

1. Clone or copy the `mod-epic-bosses` directory into your AzerothCore `modules/` folder:
   ```bash
   cd /path/to/azerothcore-wotlk/modules
   git clone https://github.com/mama128/mod-epic-bosses.git mod-epic-bosses
   ```
2. Re-run CMake and compile your AzerothCore solution.
3. Copy `conf/mod_epic_bosses.conf.dist` into your server's `configs/modules/` folder and rename it to `mod_epic_bosses.conf`.
4. Edit `mod_epic_bosses.conf` to customize your bosses and reload changes live in-game at any time using `.reload config`.
