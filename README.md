# OffBlast
A fast, lightweight game launcher written in C with SDL2. OffBlast provides a unified interface for launching games across multiple platforms and emulators with multi-user support and customizable configurations.

[![offblast.png](https://i.postimg.cc/YjB0Rf4M/offblast.png)](https://postimg.cc/Lgvm6PBb)

## Get the OpenGameDB
Offblast is powered by the OpenGameDb, which is a bunch of csv files that are publicly available on github for everyone.
Check it out before you install Offblast:

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
    "playtime_path": "/path/to/playtime/data"
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
