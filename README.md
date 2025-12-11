# OffBlast
A fast, lightweight game launcher written in C with SDL2. OffBlast provides a unified interface for launching games across multiple platforms and emulators with multi-user support and customizable configurations.

[![offblast.png](https://i.postimg.cc/brW9yN6h/offblast.png)](https://postimg.cc/Lgvm6PBb)

## Get the OpenGameDB
Offblast is powered by the OpenGameDb, which is a collection of CSV files containing game metadata that are publicly available on github.

### AppImage Users
If you're using the AppImage version, **OpenGameDB is automatically included and managed**:
- The database is installed to `~/.offblast/opengamedb` on first run
- Automatically updates when you run a newer AppImage version
- No manual setup required!

### Manual Build Users
If you compiled OffBlast from source, you'll need to clone the database manually:

```bash
git clone https://github.com/karlforshaw/opengamedb ~/opengamedb
```

Then set the path in your `~/.offblast/config.json`:
```json
"opengamedb": "/home/youruser/opengamedb"
```

[OpenGameDB on Github](https://github.com/karlforshaw/opengamedb)

## Compilation Requirements (Ubuntu, Pop!\_OS)
Install the required libraries

```sudo apt install build-essential libsdl2-dev libcurl4-openssl-dev libjson-c-dev libglew-dev libx11-dev libxml2-dev libmurmurhash-dev libxmu-dev```

Compile offblast

```make```

Run

```./offblast```

## Configuration

### Quick Start
Copy the sample config to get started:
```bash
make install  # Creates ~/.offblast/config.json from config-dist.json
```

Then edit `~/.offblast/config.json` to match your setup. The configuration file has three main sections:

### 1. Launchers
Launchers define how to find and run your games. OffBlast supports two launcher types:

#### Standard Launchers (for all emulators and games)
```json
{
    "type": "standard",
    "rom_path": "/path/to/games",
    "extension": ".iso,.chd",      // File extensions to scan for
    "cmd": "emulator %ROM%",        // Command to run (with placeholders)
    "platform": "playstation_2"     // Platform identifier for OpenGameDB
}
```

#### Special Scanning Patterns
Some systems need recursive scanning or directory-based games:

**Pattern-based scanning** (for Wii U, PS3, etc.):
```json
{
    "type": "standard",
    "rom_path": "/path/to/wiiu/games",
    "scan_pattern": "*/vol/code/*.rpx",  // Glob pattern for recursive search
    "cmd": "cemu -g %ROM%",
    "platform": "wii_u"
}
```

**Directory scanning** (for ScummVM):
```json
{
    "type": "standard",
    "rom_path": "/path/to/scummvm/games",
    "scan_pattern": "DIRECTORY",         // Scans for directories instead of files
    "cmd": "scummvm --auto-detect --path=%ROM%",
    "platform": "pc"
}
```

#### Advanced Matching Options

**ID-based matching** (for PS Vita, systems with title IDs):
```json
{
    "type": "standard",
    "rom_path": "/path/to/vita/ux0/app",
    "scan_pattern": "{*}/eboot.bin",     // {*} marks what to extract for matching
    "match_field": "title_id",           // Match against title_id instead of game name
    "path_is_match_string": true,        // Use title ID as path (not full file path)
    "cmd": "vita3k -r %ROM%",            // %ROM% becomes the title ID
    "platform": "playstation_vita"
}
```

**Configuration options:**
- `match_field`: Which database field to match against (default: `"title"`)
  - `"title"` - Fuzzy name matching (default, works for most systems)
  - `"title_id"` - Exact ID matching (for PS Vita, Steam, systems with unique IDs)
- `path_is_match_string`: Use extracted match string as ROM path (default: `false`)
  - Set to `true` when launcher needs just the ID, not the full file path
  - Essential for launchers like Vita3K that take title IDs as arguments
- `{*}` in `scan_pattern`: Marks which folder name to extract
  - `{*}/eboot.bin` extracts parent folder name (e.g., `PCSE00123`)
  - Without `{*}`, patterns like `*/vol/code/*.rpx` auto-derive extraction

**Why use ID matching?**
- Handles multi-region releases correctly (different IDs per region)
- More accurate matching for systems with unique identifiers
- Required for launchers that need IDs instead of file paths

#### Steam Launcher
```json
{
    "type": "steam"  // Automatically finds Steam games
}
```

### 2. Users and Dynamic Fields
OffBlast supports multiple users with **completely customizable fields**. ANY field you define in a user object becomes available as a `%FIELD_NAME%` placeholder in launcher commands.

```json
"users": [
    {
        "name": "Player 1",
        "email": "player1@example.com",
        "avatar": "/path/to/avatar.jpg",

        // Define ANY custom fields you want:
        "retroarch_config": "/home/player1/.retroarch.cfg",
        "save_path": "/saves/player1/",
        "cemu_account": "80000001",
        "wine_prefix": "/home/player1/.wine-gaming",
        "controller_profile": "xbox360",
        "my_custom_setting": "any_value",

        // These ALL become available as placeholders:
        // %RETROARCH_CONFIG%, %SAVE_PATH%, %CEMU_ACCOUNT%,
        // %WINE_PREFIX%, %CONTROLLER_PROFILE%, %MY_CUSTOM_SETTING%
    }
]
```

### 3. Using Dynamic Fields in Launchers
Reference any user field in your launcher commands:

```json
{
    "type": "standard",
    "rom_path": "/games/psx",
    "extension": ".cue",
    "cmd": "retroarch --config=%RETROARCH_CONFIG% --savefile=%SAVE_PATH%/psx -L core.so %ROM%",
    "platform": "playstation"
}
```

### 4. Steam Integration

OffBlast can show **all your owned Steam games**, not just installed ones. This requires a Steam Web API key.

#### Getting Your Steam API Key
1. Go to https://steamcommunity.com/dev/apikey
2. Log in and register for a key (domain name doesn't matter, use anything)
3. Copy the key

#### Finding Your Steam ID
Your Steam ID is a 17-digit number (e.g., `76561198012345678`). Find it at https://steamid.io by entering your profile URL.

#### Configuration
Add the `steam` section to your config.json:

```json
{
    "steam": {
        "api_key": "YOUR_API_KEY_HERE",
        "steam_id": "76561198012345678"
    }
}
```

#### What This Enables
- **All owned games** appear in your library (not just installed)
- **Uninstalled games** appear dimmed
- Launching an uninstalled game opens the Steam install dialog
- Game names come directly from Steam (no fuzzy matching needed)

#### Without API Key
If you don't configure an API key, OffBlast falls back to local-only detection using Steam's appmanifest files. This only shows installed games.

### 5. SteamGridDB Cover Browser (Optional)

Browse and select custom game covers from SteamGridDB's extensive community artwork collection.

#### Getting Your SteamGridDB API Key
1. Create a free account at https://www.steamgriddb.com
2. Go to https://www.steamgriddb.com/profile/preferences/api
3. Generate your API key (free, no credit card required)

#### Configuration
Add to your config.json:

```json
{
    "steamgriddb_api_key": "your-api-key-here"
}
```

#### How to Use
1. Navigate to any game
2. Press **Start** to open the context menu
3. Select **"Browse Covers"**
4. Browse available covers (5×2 grid with thumbnails)
5. Select a cover with **A button**
6. Cover downloads in background and updates immediately

#### What This Enables
- **Browse cover options** - See 30-50+ covers per game with thumbnails
- **All platforms** - Works for Steam, RetroArch, emulators, any game
- **Reconstructed artwork** - Access community-created high-quality covers
- **Multiple styles** - Official art, minimal designs, regional variants, fan art
- **Persistent** - Selected covers save locally and persist across restarts

#### Without API Key
The "Browse Covers" option will show an error message. Cover browsing is completely optional - games will still use OpenGameDB covers by default.

### Complete Example Configuration
```json
{
    "launchers": [
        {
            "type": "standard",
            "rom_path": "/games/switch",
            "extension": ".xci,.nsp",
            "cmd": "yuzu -u %YUZU_USER_INDEX% -f -g %ROM%",
            "platform": "nintendo_switch"
        },
        {
            "type": "standard",
            "rom_path": "/games/ps3",
            "scan_pattern": "*/PS3_GAME/USRDIR/EBOOT.BIN",
            "cmd": "rpcs3 --user=%RPCS3_ACCOUNT% --no-gui %ROM%",
            "platform": "playstation_3"
        },
        {
            "type": "steam"
        }
    ],
    "users": [
        {
            "name": "Karl",
            "email": "karl@example.com",
            "avatar": "/avatars/karl.jpg",

            // Emulator-specific settings
            "yuzu_user_index": "1",
            "rpcs3_account": "00000001",
            "retroarch_config": "/home/karl/.retroarch.cfg",

            // Save game management
            "save_path": "/saves/karl",
            "memcard_path": "/saves/karl/memcards",

            // Custom environment
            "wine_prefix": "/home/karl/.wine-gaming",
            "display_mode": "1080p"
        }
    ],
    "opengamedb": "/path/to/opengamedb",
    "playtime_path": "/path/to/playtime/data",
    "steam": {
        "api_key": "YOUR_STEAM_API_KEY",
        "steam_id": "76561198012345678"
    },
    "steamgriddb_api_key": "your-steamgriddb-api-key"
}
```

### Platform Identifiers
Use these platform identifiers to match games with OpenGameDB:
- `playstation`, `playstation_2`, `playstation_3`, `playstation_portable`
- `nintendo_entertainment_system`, `super_nintendo_entertainment_system`
- `nintendo_64`, `gamecube`, `wii`, `wii_u`, `nintendo_switch`
- `nintendo_ds`, `nintendo_3ds`
- `game_boy`, `game_boy_color`, `game_boy_advance`
- `master_system`, `mega_drive`, `dreamcast`, `saturn`
- `pc` (for ScummVM and PC games)

See the [sample config](https://github.com/karlforshaw/offblast/blob/master/config-dist.json) for more examples.

## Features

### Playtime Tracking
OffBlast automatically tracks:
- **Total playtime** for each game per user
- **Last played** timestamp
- **Most played** games (shown in the top row)
- **Recently played** games (shown in the second row)

Playtime data is stored per-user in the path specified by `playtime_path` in your config. Each user gets their own playtime database based on their email hash.

### Smart Game Organization
The home screen automatically organizes games into:
1. **Most Played** - Your top games by total playtime
2. **Recently Played** - Games you've played recently
3. **Platform sections** - All games organized by platform

### Multi-User Support
- Switch between users at startup
- Each user has their own:
  - Save game locations
  - Emulator configurations
  - Playtime statistics
  - Custom settings via dynamic fields

### Game Metadata
Powered by [OpenGameDB](https://github.com/karlforshaw/opengamedb) for:
- Game cover art
- Descriptions
- Release dates
- Metacritic scores

## Upgrading

### After updating to the latest version
If you're upgrading and want to use the new ID-based matching or multi-region support:

1. Delete the old database files:
   ```bash
   rm ~/.offblast/launchtargets.bin ~/.offblast/descriptions.bin
   ```
2. Run OffBlast - it will reimport all games with the new matching system

**Why?** The database signature format changed to support multiple regional variants of games. Old databases will still work but won't have the regional variants properly separated.

## Troubleshooting

### The whole thing minimizes when I click another window
Add this to your `.bashrc` or `.profile`:
```bash
export SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS=0
```

### Games aren't being detected
1. Check that your `rom_path` exists and is readable
2. Verify file extensions match what's in your `extension` field (case-sensitive)
3. For pattern-based scanning, test your glob pattern: `ls /your/rom_path/your_pattern`
4. Check that the `platform` matches an OpenGameDB CSV file

### Custom fields aren't working
- Field names are converted to uppercase in placeholders: `my_field` becomes `%MY_FIELD%`
- Ensure the field exists in the current user's configuration
- Check for typos in both the user definition and launcher command

## Controls
- **D-Pad/Analog**: Navigate
- **A/Enter**: Select
- **B/Backspace**: Back
- **Y**: Search
- **Left** (when at leftmost position): Show main menu (platforms, exit, shutdown, etc.)
- **Guide**: Show menu (in-game)
- **L/R Bumpers**: Page scroll
