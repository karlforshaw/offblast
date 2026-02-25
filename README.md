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
    "name": "PS2 Games",            // Optional: Custom display name (defaults to platform)
    "rom_path": "/path/to/games",
    "extension": ".iso,.chd",      // File extensions to scan for
    "cmd": "emulator %ROM%",        // Command to run (with placeholders)
    "platform": "playstation_2"     // Platform identifier for OpenGameDB
}
```

**Optional fields:**
- `name`: Custom display name for this launcher shown in menus and status messages. If not provided, the platform name is used.

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

### 4. Pre/Post Launch Hooks

Execute custom commands before and after launching games. Perfect for advanced workflows like save file management, display configuration, network mounts, and more.

#### Configuration
Add hook commands to any launcher:

```json
{
    "type": "standard",
    "rom_path": "/games/psx",
    "extension": ".cue",
    "cmd": "retroarch -L core.so %ROM%",
    "platform": "playstation",
    "pre_launch_hook": "/path/to/setup.sh %GAME_NAME% %ROM_PATH%",
    "post_launch_hook": "/path/to/cleanup.sh %GAME_NAME%"
}
```

#### Available Placeholders
Hooks support the same slug substitution as launcher commands:

**Game metadata:**
- `%GAME_NAME%` - Game title
- `%ROM_PATH%` - Full path to game file
- `%GAME_ID%` - Game ID (for Steam, Vita, etc.)
- `%PLATFORM%` - Platform name
- `%COVER_URL%` - Cover image URL
- `%GAME_DATE%` - Release date
- `%GAME_RANKING%` - Metacritic score

**Launcher metadata:**
- `%LAUNCHER_TYPE%` - Launcher type (standard, steam, etc.)
- `%LAUNCHER_NAME%` - Custom launcher name
- `%ROM_DIR%` - ROM directory path

**User metadata:**
- `%USER_NAME%`, `%USER_EMAIL%`, `%USER_AVATAR%`
- Plus all custom user fields: `%SAVE_PATH%`, `%RETROARCH_CONFIG%`, etc.

#### Real-Time Status Display
Hook scripts can output status messages that appear below the game cover:

```bash
#!/bin/bash
echo "Checking network connection..."
sleep 1
echo "Mounting save share..."
mount -t nfs server:/saves /mnt/saves
echo "Copying save files..."
cp /mnt/saves/$1.sav /local/saves/
echo "Ready to launch!"
```

Each line of output updates the displayed status in real-time.

#### Visual Feedback
- Game cover breathes (pulsing animation) while hooks execute
- Smooth fade-in when hook starts, fade-out when complete
- Status updates appear centered below the cover

#### Aborting Hooks
- Press **X button** (west) or **Escape** to abort a running hook
- Pre-launch hook abort cancels the game launch
- Post-launch hook abort skips cleanup but returns to menu

#### Use Cases
- **Save file swapping**: Switch between user save files before launch
- **Network mounts**: Mount NFS/SMB shares for cloud saves
- **Display configuration**: Set resolution, refresh rate, HDR settings
- **Discord Rich Presence**: Update Discord status with current game
- **CPU governor**: Set performance mode for demanding games
- **Cleanup**: Unmount shares, restore settings after gameplay

#### Example: Per-User Save Management
```json
{
    "pre_launch_hook": "/scripts/mount-saves.sh %USER_NAME% %SAVE_PATH%",
    "post_launch_hook": "/scripts/sync-saves.sh %USER_NAME% %SAVE_PATH%"
}
```

```bash
# /scripts/mount-saves.sh
#!/bin/bash
USER=$1
SAVE_PATH=$2
echo "Mounting save directory for $USER..."
mount -t nfs server:/saves/$USER $SAVE_PATH
echo "Save directory ready!"
```

### 5. .desktop File Launcher (PC Games and Ports)

Launch PC games, native Linux games, ports, and recompilations using freedesktop.org .desktop files.

#### Configuration

Add a desktop launcher to your config.json:

```json
{
    "type": "desktop",
    "rom_path": "/home/user/.offblast/desktop-games",
    "platform": "pc"
}
```

**Configuration options:**
- `rom_path` - Directory containing .desktop files (required)
- `platform` - Platform for OpenGameDB matching (optional, defaults to "pc")
- `scan_pattern` - Pattern for finding .desktop files (optional, defaults to "*.desktop")
- `cmd` - Not used (Exec comes from .desktop files)

#### Creating .desktop Files

Place .desktop files in your configured `rom_path` directory:

```ini
[Desktop Entry]
Type=Application
Name=Ship of Harkinian
Exec=/home/user/Apps/soh/soh.AppImage
Path=/home/user/Apps/soh
Icon=/home/user/Apps/soh/icon.png
Categories=Game;
Comment=Native PC port of Ocarina of Time
Terminal=false

# OffBlast per-game hooks (optional)
X-PreLaunchHook=/home/user/soh/pre-launch.sh %GAME_NAME%
X-PostLaunchHook=/home/user/soh/post-launch.sh %GAME_NAME%
```

**Important fields:**
- `Name` - Used to match against OpenGameDB for metadata and covers
- `Exec` - Command to launch the game (supports wine, env vars, etc.)
- `Path` - Working directory (game changes to this directory before launching)
- `X-PreLaunchHook` / `X-PostLaunchHook` - Per-game hooks (override launcher-level hooks)

#### Wine Support

For Windows games, use wine in the Exec field:

```ini
[Desktop Entry]
Name=Sonic 2 Absolute
Exec=wine /home/user/Games/Sonic2Absolute.exe
Path=/home/user/Games/Sonic2Absolute
```

#### Per-Game Hooks

.desktop files support per-game hooks via custom X-* fields. These override launcher-level hooks:

```ini
X-PreLaunchHook=/scripts/mount-saves.sh %GAME_NAME% %USER_NAME%
X-PostLaunchHook=/scripts/sync-saves.sh %GAME_NAME% %USER_NAME%
```

All standard placeholders work: `%GAME_NAME%`, `%USER_NAME%`, `%SAVE_PATH%`, etc.

#### Supported Games

Tested and working:
- **Ship of Harkinian** - Ocarina of Time PC port
- **2Ship2Harkinian** - Majora's Mask PC port
- **Sonic 3 A.I.R.** - Enhanced Sonic 3 & Knuckles port
- **Sonic 2 Absolute** - Enhanced Sonic 2 port (via Wine)
- **Super Mario Bros. Remastered** - Fan remake in Godot 4
- Any AppImage, native Linux game, or Wine application

#### Adding New Games

When you add new .desktop files or update OpenGameDB:

1. Navigate to any PC game
2. Press **Start** button → **"Import New Games from CSV"** (if you added entries to pc.csv)
3. Press **Start** button → **"Rescan Launcher for New Games"**
4. New games appear immediately without restarting

### 6. Steam Integration

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
- **Achievement progress** displayed in game info panel (e.g., "15/20 Achievements (75%)")
  - Reads from local Steam cache (works even with private profiles)
  - No additional configuration needed

#### Without API Key
If you don't configure an API key, OffBlast falls back to local-only detection using Steam's appmanifest files. This only shows installed games.

#### Filtering Uninstalled Games
If you only want to see installed Steam games (even with an API key configured), add this to your config:

```json
{
    "show_installed_only": true
}
```

This hides all uninstalled games from your library, showing only what's currently installed on your system.

### 7. SteamGridDB Cover Browser (Optional)

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

### 8. RetroAchievements Integration (Optional)

Display achievement progress for retro games from RetroAchievements.org. Achievements appear in the info panel just like Steam achievements.

#### Getting Your RetroAchievements API Key
1. Create a free account at https://retroachievements.org
2. Go to **Settings → Keys**
3. Copy your **Web API Key**

#### Configuration (Per-User)
RetroAchievements credentials are configured **per-user** using custom fields. Each family member can have their own RA account:

```json
"users": [{
  "name": "Player 1",
  "email": "player1@example.com",
  "avatar": "/path/to/avatar.jpg",

  // RetroAchievements credentials (per-user)
  "retroachievements_username": "YourRAUsername",
  "retroachievements_api_key": "YourWebAPIKey",

  // Other custom fields
  "save_path": "/saves/player1/",
  "cemu_account": "80000001"
}]
```

#### How It Works
- **On startup**: Fetches your RA game list (only games you've started, typically 10-100)
- **On browse**: After 1.6s on a game, fuzzy matches against your RA list
- **On match**: Extracts .zip, hashes ROM, verifies with RA API
- **On verify**: Caches achievement data permanently
- **After gameplay**: Automatically refreshes to show newly unlocked achievements

#### What This Enables
- **Achievement progress** displayed in game info panel (e.g., "15/20 Achievements (75%)")
- **Per-user tracking** - each family member sees their own progress
- **Smart performance** - only hashes games you've actually played on RA
- **Supports .zip files** - automatically extracts and hashes ROM inside
- **Works with 30+ platforms**: NES, SNES, Genesis, N64, GB, GBA, PSX, PS2, PSP, and more
- **Persistent caching** - verified games stored in `~/.offblast/{email}.ragames`
- **Auto-refresh** - achievement counts update after playing games

#### Without API Key
RetroAchievements features are disabled. This is completely optional - all other functionality works normally.

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

### Resume Feature
Switch seamlessly between your games and OffBlast:
- **Press Guide button** while in-game to return to OffBlast
- **Press Resume** (A button when hovering over Stop/Resume) to return to your game
- Works across X11 and Wayland sessions

#### GNOME Wayland Requirement
On **GNOME with Wayland**, Resume requires the **Window Calls** extension:

1. Install from [GNOME Extensions](https://extensions.gnome.org/extension/4724/window-calls/)
2. Or via command line:
   ```bash
   gnome-extensions install window-calls@domandoman.xyz
   ```
3. Restart OffBlast after installation

**Note**: On first use without the extension, OffBlast will display installation instructions. This requirement is specific to GNOME Wayland - KDE Plasma and X11 sessions work without additional setup.

### Console-Like Experience

Want OffBlast to boot automatically like a game console? **KDE Plasma is the recommended desktop environment** for this setup.

#### Why KDE over GNOME?

While OffBlast works on any desktop, **KDE Plasma provides the best console-like experience**:

- **Reliable autostart** - KDE's built-in autostart works perfectly with OffBlast
- **No focus issues** - GNOME's security restrictions prevent apps from auto-focusing on boot, even with autostart configured
- **Better hardware support** - Direct GPU access works seamlessly
- **Window management** - Resume functionality works without additional extensions

#### Setting Up Console Mode with KDE

1. **Install KDE Plasma** (if not already installed):
   ```bash
   # Fedora
   sudo dnf install @kde-desktop-environment

   # Ubuntu/Debian
   sudo apt install kde-plasma-desktop
   ```

2. **Enable autologin** in KDE System Settings:
   - Open System Settings → Startup and Shutdown → Login Screen (SDDM)
   - Enable "Automatically log in" and select your user
   - Click Apply

3. **Configure autostart** for OffBlast:
   - Open System Settings → Startup and Shutdown → Autostart
   - Click "Add..." → "Add Application..."
   - Navigate to your OffBlast binary or AppImage
   - Click OK

4. **Install qdbus-qt6** (required for Resume on Fedora KDE):
   ```bash
   sudo dnf install qdbus-qt6
   ```

5. **Reboot** - Your system will now boot directly into OffBlast

#### Result

Your machine will boot straight into OffBlast like a dedicated game console. Press the Guide button during gameplay to return to the launcher, and use the Resume button to switch back to your game.

**Note**: If you're on GNOME and experiencing autostart/focus issues, switching to KDE Plasma is significantly easier than trying to work around GNOME's restrictions.

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
- **Guide**: Return to OffBlast while in-game
- **Resume**: Return to game from OffBlast (A button on Resume/Stop buttons)
- **L/R Bumpers**: Page scroll
