# Changelog

## [Unreleased]

### Added
- **Steam achievement display**
  - Shows achievement progress for Steam games in info panel
  - Reads from local Steam cache file (no network calls, no API key needed)
  - Format: " | 15/20 Achievements (75%)" displayed after playtime
  - Works with private Steam profiles
  - Only shows for games that have achievements (filters out games with total=0)
  - Rendered at 81% alpha to de-emphasize from core metadata
  - Cache file location: `~/.steam/steam/userdata/<userid>/config/librarycache/achievement_progress.json`
  - Automatically updates when Steam updates the cache (playing games, launching Steam)
- **RetroAchievements support**
  - Shows achievement progress for retro games in info panel
  - Per-user RA credentials via custom user fields: `retroachievements_username` and `retroachievements_api_key`
  - Get API key at https://retroachievements.org (Settings → Keys)
  - Smart lazy evaluation: only processes games you've started on RA (not entire library)
  - On-demand hash verification after 1.6s linger: fuzzy match → extract .zip → hash ROM → verify
  - Integrated rcheevos library for proper ROM hashing (handles console-specific rules)
  - Supports both .zip archives and raw ROM files
  - Status message: "Verifying RetroAchievements..." during hash calculation
  - Persistent database caches verified matches: `~/.offblast/{email}.ragames`
  - Automatic achievement refresh after gameplay: fetches fresh data from RA API
  - Updates displayed counts immediately when returning from games
  - Comprehensive platform support: NES, SNES, Genesis, N64, GB, GBA, PSX, PS2, PSP, and 20+ more
  - Only shows for games with achievements (total > 0)
  - Rendered at 81% alpha to match Steam achievement styling
- **.desktop file launcher support**
  - Launch PC games, ports, and recompilations via freedesktop.org .desktop files
  - Scan directories for .desktop files with configurable scan_pattern
  - Parse Name field for OpenGameDB matching, Exec field for launch command
  - Support Path field for working directory (chdir before execution)
  - Per-game hooks via X-PreLaunchHook and X-PostLaunchHook override launcher hooks
  - Real-time stdout from hooks updates status display
  - Wine support for Windows executables (e.g., Sonic 2 Absolute)
  - Icon field parsed (reserved for future cover fallback)
  - Tested with: Ship of Harkinian, 2Ship2Harkinian, Sonic 3 A.I.R., Sonic 2 Absolute, Super Mario Bros. Remastered
  - Use cases: Harbour Masters ports, fan remasters, AppImages, native Linux games
- **Import New Games from CSV**
  - New context menu option to import new entries from OpenGameDB CSV
  - Adds new LaunchTarget entries without destroying existing database
  - Preserves all path assignments and match scores
  - Enables pulling CSV updates without full database rebuild
  - Shows count of newly imported games in status message
- **Pre/post launch hooks**
  - Execute custom commands before and after launching games
  - Per-launcher hook configuration via config.json
  - Comprehensive slug substitution for game metadata: %GAME_NAME%, %ROM_PATH%, %PLATFORM%, etc.
  - Slug substitution for launcher fields: %LAUNCHER_TYPE%, %LAUNCHER_NAME%, %ROM_DIR%
  - Slug substitution for user fields: %USER_NAME%, %SAVE_PATH%, plus all custom user fields
  - Real-time status updates from hook script stdout (each line updates the display)
  - Breathing animation on game cover during hook execution with smooth fade-in/fade-out
  - Hook abort via west button (X) or escape key with graceful cleanup
  - Proper shell quoting for special characters in paths (parentheses, spaces, quotes)
  - Visual feedback: hook status centered below game cover, animated breathing effect
  - Use cases: display configuration, save file swapping, Discord status, network mounts, cleanup
- **Multi-threaded Steam metadata fetching**
  - Parallel fetching with 3 worker threads for ~8x speedup on cold starts
  - 50 games now fetch metadata in ~10 seconds instead of 100 seconds
  - Rate limiting enforced at 200ms between requests (~5 req/sec total)
  - Producer-consumer work queue pattern prevents race conditions
  - Three-tier mutex strategy protects database writes during parallel fetches
  - Loading screen shows "Fetching Steam metadata..." with live progress counter
  - Respects Steam API rate limits (~200 requests per 5 minutes)
  - Graceful error handling - failed fetches marked with ranking=999

### Fixed
- Steam metadata no longer re-fetches on every launch for games without release dates
  - Changed condition from empty date check to ranking==0
  - Steam API doesn't return dates for some games, causing infinite re-fetch loop
  - Now uses ranking as fetch marker (always set to score or 999 after first fetch)
- Desktop launcher rescan now clears stale assignments before re-matching
- Rescan/import operations preserve current view (stay in platform list instead of home)
- Desktop launcher rescan properly calls importFromDesktop() instead of importFromCustom()

## [0.8.0] - 2026-02-23

### Added
- **Animated loading screen with threaded initialization**
  - Beautiful breathing rocket animation during startup (sin² curve with pauses at extremes)
  - Real-time status updates showing exactly what's initializing
  - Progress counters for Steam library fetching and game metadata loading
  - Smooth 0.61 second exit animation (logo shrinks to dot, text fades out)
  - Seamless 0.41 second fade-in transition to user select screen
  - Heavy initialization runs in background thread to keep UI responsive
  - Proper separation of OpenGL and non-GL initialization for thread safety
  - Early SDL/OpenGL init allows loading screen to appear immediately
- **Playtime display in game info panel**
  - Shows total hours/minutes played for games in your playtime file
  - Displays as "15.2 hrs" for games played over 1 hour
  - Displays as "45 mins" for games played under 1 hour
  - Rendered at 81% opacity to visually de-emphasize from core metadata
  - Only appears if game has been played (not shown for unplayed games)
- **Steam playtime integration**
  - Steam's `playtime_forever` data automatically synced to offblast playtime file
  - Syncs on every app startup for up-to-date Steam playtime
  - Converts Steam minutes to offblast milliseconds format
  - Overwrites local playtime with Steam's authoritative data
  - Steam games now show accurate playtime in info panel and Most Played lists
- **Steam Web API integration** for complete library display
  - Shows all owned Steam games, not just installed ones
  - Requires `steam.api_key` and `steam.steam_id` in config.json
  - Get API key at https://steamcommunity.com/dev/apikey
  - Find Steam ID at https://steamid.io
  - Uninstalled games appear dimmed (30% desaturated, 70% alpha)
  - Launching uninstalled game opens Steam install dialog
  - Filters out tools/runtimes (Proton, Redistributables, etc.)
  - Falls back to local-only detection if no API key configured
- **Steam Big Picture mode integration** for couch gaming
  - Steam games now launch in Big Picture mode via `steam -bigpicture steam://rungameid/<APPID>`
  - Uninstalled games open Big Picture install dialog
  - Guide button disabled for Steam games (Big Picture handles it natively)
  - Simplified UI shows "Return" button instead of Resume/Stop for Steam games
  - Pressing Return goes back to offblast; Steam manages game lifecycle
- **Steam Store API metadata fetching**
  - Fetches release dates, Metacritic scores, and descriptions from Steam Store API
  - Metadata fetched automatically for games missing data on startup
  - Steam rescrape (r/Shift+R) now fetches from Store API instead of OpenGameDB
  - Dates converted from Steam format ("29 Sep, 2017") to standard format ("2017-09-29")
- **SteamGridDB cover browser integration**
  - Browse and select custom covers from SteamGridDB.com for any game
  - Access via "Browse Covers" option in context menu (Start button)
  - Shows grid of available 600x900 covers with thumbnail previews
  - 5 columns × 2 rows with scrollable navigation for large collections
  - Works for Steam games (matches by AppID) and non-Steam games (searches by name)
  - Multiple match handling: prompts user to select correct game before showing covers
  - Background download: UI remains responsive while cover downloads/saves
  - Thumbnails load asynchronously in separate threads for smooth browsing
  - Downloads selected cover and saves directly to local cache
  - Covers persist across restarts and take precedence over database URLs
  - Requires free API key from https://www.steamgriddb.com/profile/preferences/api
  - Configure in config.json: `"steamgriddb_api_key": "your-key-here"`
  - Tile sizing matches main game browser for visual consistency
  - Golden ratio spacing throughout for professional appearance
  - Proper text centering using getTextLineWidth() with cached title width
  - Minimalist scroll indicators (simple dots) positioned with golden ratio margins
- **Steam documentation** added to README.md
- **UTF-8 text rendering with multi-language support**
  - Full Unicode support for game titles, descriptions, and all UI text
  - Supports 656 characters across 5 Unicode ranges:
    - Basic Latin + Latin-1 (32-255): Western European languages
    - Latin Extended-A (256-383): Eastern European languages
    - General Punctuation (0x2000-0x206F): Smart quotes, em dashes, ellipsis
    - Hiragana (0x3040-0x309F): Japanese phonetic script
    - Katakana (0x30A0-0x30FF): Japanese phonetic script
  - Proper rendering of accented characters (Pokémon, Café, etc.)
  - Smart quotes (" " ' ') and typography (— …) display correctly
  - Uses stbtt_PackFontRanges with 4096×4096 texture atlas per font
  - Binary search glyph lookup with UTF-8 decoder
  - Negligible performance impact: 1ms frame time maintained
  - Graceful fallback: drops Japanese ranges if atlas too small for font size
- **Launcher rescan feature**
  - New "Rescan Launcher for New Games" context menu option
  - Manually detect newly added ROM files without restarting OffBlast
  - Invalidates launcher cache and re-scans ROM directories
  - Updates cache file after scan completes
  - Fixes workflow: add new game → open context menu → rescan → game appears
- **Roman numeral conversion for game matching**
  - Automatic bidirectional conversion between arabic and roman numerals (1-39)
  - Matches "Hades 2.nsp" to "Hades II" in OpenGameDB
  - Matches "Final Fantasy VII.iso" to "Final Fantasy 7"
  - Tries both original and converted versions, uses better match score
  - Solves common mismatch between filename numerals and database titles
- **Game context menu** accessible via Start button on controller
  - Slides in from right side of screen (mirrors left navigation menu)
  - "Rescan Launcher for New Games" - detect newly added ROM files
  - "Refresh Platform Metadata/Covers" - re-fetch all covers and metadata for current platform
  - "Refresh Game Metadata/Covers" - re-fetch cover and metadata for selected game only
  - "Copy Cover Filename" - copies expected cover filename to clipboard for manual cover adding
  - "Refresh Cover" - reload cover texture without restarting (for manually added covers)
  - Works on both X11 and Wayland via SDL clipboard

### Changed
- **Optimized animation system** for better performance and maintainability
  - Single array of all animations defined once at startup (single point of maintenance)
  - Inlined animation tick loop in main render loop (eliminates 8 function calls per frame)
  - `animationRunning()` checks array with early exit on first active animation
  - Adding new animations now only requires updating one array definition
  - Removed manual tick calls that required updating for each new animation type
  - Removed TODO about killing animations on mode switch - investigation revealed this is not an issue because all mode switches occur either during initialization (before animations start) or when input is blocked by `animationRunning()` checks (no animations active)
- **Renamed "Rescrape" to "Refresh Metadata/Covers"** throughout UI and code
  - Previous naming suggested it would detect new games, which it doesn't
  - New naming clearly communicates it refreshes metadata for existing games
  - Updated context menu labels, status messages, and console output
  - Actual game detection now handled by separate "Rescan Launcher" feature
- Steam games no longer imported from `steam.csv` in OpenGameDB
  - Game names and metadata now come directly from Steam API
  - Prevents phantom games from appearing in library

### Fixed
- **Fixed controller not working after loading screen implementation**
  - Added SDL_INIT_JOYSTICK flag to early SDL initialization
  - Controller events now processed during loading screen
  - Supports controllers connected before, during, or after loading screen
  - Controller hot-plugging works throughout application lifecycle
- **Fixed rescrape losing game descriptions**
  - Rescrape was clearing descriptionOffset but never writing new descriptions
  - Added missing writeDescriptionBlob() call in OpenGameDB rescrape
  - Descriptions now properly persist through rescrape operations
  - Note: Description file will grow on rescrapes (orphaned blobs not reclaimed)
- **Fixed Essential lists showing incorrect games and wrong scores**
  - Essential lists now collect ALL platform games, sort by ranking, deduplicate, then take top 25
  - Previous behavior: took first 25 games found, then sorted those (missed high-rated games)
  - Added case-insensitive title deduplication to handle regional variants
  - Prioritization: owned version > highest score > first entry
  - Skip games with empty names to prevent blank entries
- **Fixed score calculation treating "n/a" as zero**
  - Added validation to reject non-numeric scores (gfScore > 0, metaScore > 0)
  - Games with "n/a" for metascore no longer averaged with 0
  - Example: Sonic 2 (gf_score=4.32, metascore="n/a") now shows 86% instead of 43%
  - Invalid scores properly trigger sentinel value (999) instead of corrupting averages
- **Fixed Resume functionality on KDE Plasma Wayland**
  - Implemented KWin scripting via D-Bus to activate game windows on Wayland
  - Creates temporary KWin script that searches for game window (excluding offblast)
  - Uses qdbus instead of dbus-send to avoid AppImage library conflicts
  - Detects session type (Wayland vs X11) and window manager (KDE vs GNOME vs i3)
  - Automatically uses KWin scripting on KDE Wayland, X11 APIs elsewhere
  - Tested and working on Bazzite (KDE Plasma Wayland)
- **Fixed OpenGameDB path error when using auto-detection**
  - Metadata refresh failed with "No OpenGameDB path in config" when using fallback paths
  - Now stores discovered OpenGameDB path in offblast->openGameDbPath at runtime
  - Metadata refresh uses stored path instead of re-reading config
  - Supports both explicit config paths and auto-detected fallback locations
- **Fixed newly added games not appearing until restart**
  - Launcher contents cache prevented detection of new ROM files
  - Added "Rescan Launcher for New Games" to manually trigger directory scan
  - Invalidates cache and re-imports games from launcher ROM directories
  - Workflow: add ROM → context menu → rescan → game appears immediately
- **Fixed game matching for sequels with different numeral formats**
  - Files like "Hades 2.nsp" failed to match "Hades II" in OpenGameDB
  - Added automatic roman numeral conversion (supports 1-39, I-XXXIX)
  - Tries both original filename and converted version, uses better match
  - Bidirectional: works for "2→II" and "VII→7" conversions
- **Added poor match score logging**
  - Games with match scores below 0.5 are now logged to missinggames.log
  - Log format: `rompath -> matched name (score: 0.42)`
  - Allows review of questionable matches that were accepted due to lack of better options
  - Helps identify games that need manual verification or better database entries

## [0.7.0] - 2025-11-25

### Added
- **External platform names via OpenGameDB** (`names.csv`)
  - Platform display names now loaded from `names.csv` in the OpenGameDB directory
  - Format: `key,name` (e.g., `playstation_vita,Playstation Vita`)
  - Adding new platforms no longer requires code changes in offblast
  - Falls back to "Unknown Platform" if key not found
- **OpenGameDB path fallback logic**
  - No longer requires `opengamedb` in config.json
  - Automatically checks: config.json → `~/.offblast/opengamedb` → `./opengamedb`
  - Helpful error message lists all checked locations if not found
- **Flexible game matching system** with `match_field` config option
  - New `match_field` parameter allows matching against different database fields
  - Supports `"title"` (default) for name-based fuzzy matching
  - Supports `"title_id"` for exact ID matching (PS Vita, Steam, etc.)
  - Enables proper multi-region game support with unique IDs per region
- **Pattern-based path extraction** with `{*}` marker in `scan_pattern`
  - Use `{*}` to mark what folder name to extract for matching
  - Example: `{*}/eboot.bin` extracts parent folder name
  - Auto-derives extraction anchor for patterns like `*/vol/code/*.rpx`
  - Replaces hardcoded extraction logic for Wii U and PS3
- **Configurable path substitution** with `path_is_match_string` option
  - Set to `true` to use extracted match string as ROM path instead of full file path
  - Essential for launchers that require just IDs (e.g., Vita3K requires title ID, not file path)
  - Works seamlessly with pattern-based extraction
- **Regional game variant support** in database
  - Database signature now includes title_id when available
  - Multiple regional releases of same game (different IDs) stored as separate entries
  - Fixes issue where regional variants were overwriting each other
  - Enables proper matching for multi-region game libraries
- **Bundled OpenGameDB in AppImage** with automatic installation and updates
  - OpenGameDB now included in AppImage builds (no separate download needed)
  - Automatically installs to `~/.offblast/opengamedb` on first run
  - Smart update: only syncs when AppImage contains newer database version
  - Preserves user modifications when not updating
  - AppImage users get zero-configuration game metadata
- **Auto-select single user on startup**
  - Skips "Who's Playing?" screen when only one user is configured
  - Improves UX for single-user setups
- **AppImage build support** with automatic desktop integration
  - New `make appimage` target creates portable Linux packages
  - Bundles all dependencies (SDL2, libcurl, json-c, GLEW, etc.)
  - Auto-installs desktop file and icon on first run
  - Proper dock icon display with StartupWMClass
  - Updates desktop integration when AppImage is moved
- **Haptic feedback for search navigation** with game controllers
  - Controller rumbles when selected letter changes in search wheel
  - Subtle 50ms tick at 10% intensity for mechanical feel
  - 30% deadzone prevents flickering when joystick is near center
- **Viewport-based menu scrolling** with visual indicators
  - Menu now handles unlimited number of launchers
  - Automatic scrolling keeps selected item visible
  - Fade effects (16% screen height) indicate hidden content above/below

### Changed
- **Removed hardcoded guest account**
  - Guest account is no longer automatically created
  - Users can manually add a guest user in config.json if desired
  - Zero-user case now displays helpful message instead of exiting

### Fixed
- **Fixed AMD GPU crash when stopping Flatpak games** (SIGKILL + Mesa issue)
  - Child processes now close all inherited file descriptors except stdio
  - Prevents child from inheriting parent's Mesa GPU device handles (/dev/dri/*)
  - Workaround for Mesa bug where SIGKILL on process with shared GPU context causes abort()
  - Issue only affected AMD GPUs (NVIDIA marks GPU FDs as CLOEXEC automatically)
  - Crash manifested as "amdgpu: The CS has been rejected" followed by segfault
  - Affected Flatpak emulators (Cemu, Dolphin) on AMD hardware across all distros
- Improved search navigation angle calculation for more accurate character selection with analog stick
- Fixed compiler warnings for potential string truncation in status messages

## [0.6.2] - 2025-01-24

### Performance Improvements 🚀

#### Memory Management Overhaul
- **Implemented texture eviction system** to prevent VRAM exhaustion
  - Hard limit of 150 loaded textures (was unlimited)
  - LRU (Least Recently Used) eviction when limit reached
  - Time-based eviction for textures unused > 3 seconds (was 10 seconds)
  - Fixed texture reloading when returning to evicted tiles
  - **Result**: VRAM usage stays under 200MB (was growing to 4GB+)

#### Cover Image Optimization
- **Added automatic cover resizing on download**
  - Resizes to max 660px height, preserving aspect ratio
  - Large covers (1500x2000+) reduced by ~70%
  - Logs operations and skips already-small images
  - **Example savings**:
    - 2760x4030 cover: 44MB → 1.2MB VRAM (97% reduction)
    - 1500x2100 cover: 12MB → 1.2MB VRAM (90% reduction)
    - Disk usage for 1000+ covers: 1.6GB → ~500MB

#### Display Limits
- **Increased game display limit** from 249 to 2000
  - Fixed hardcoded limits preventing large libraries from displaying
  - Mega Drive with 274+ games now shows all titles

### Bug Fixes 🐛
- **Fixed row geometry smashing in search results** (years-old bug!)
  - Search results with exactly 25, 50, 75 etc. items were causing tiles to overlap/compress
  - Root cause: Last row length calculated as 0 for multiples of 25 (modulo bug)
  - Fixed calculation to properly handle full rows (25 items)
  - **Result**: Search results now display correctly for all result counts
- **Fixed menu incorrectly opening when pressing right at row end**
  - Menu was opening when pressing right on the last tile in a row
  - Should only open when pressing left on the first tile
  - **Result**: Right arrow at row end now correctly does nothing
- Fixed rescrape function to use signature-based matching (prevents wrong covers)
- Added two-tier rescrape: 'r' for single cover, 'Shift+R' for all covers
- Fixed UI notifications positioning for rescrape status

## [Unreleased] - 2025-01-23

### Breaking Changes 🚨

#### Launcher Type System Simplified
- **REMOVED** support for legacy launcher types: `retroarch`, `custom`, `cemu`, `rpcs3`, `scummvm`
- **ONLY TWO** launcher types are now supported:
  - `standard` - For all directory-based launchers (replaces all legacy types)
  - `steam` - For Steam App ID based launches
- All existing configs must be updated to use `standard` instead of legacy types

### Major Refactoring 🔧

#### Code Cleanup
- Removed ~400 lines of launcher-specific code
- Deleted specialized import functions: `importFromCemu()`, `importFromRPCS3()`, `importFromScummvm()`
- Removed `cemuPath` field from Launcher struct
- Consolidated all directory-based launcher logic into single code path
- Simplified configuration parsing - all non-Steam launchers now use identical logic

#### Launcher Signatures Fix
- Changed launcher signatures to hash only `type+platform` instead of entire config
- **Result**: Launcher config changes no longer orphan all games
- Signatures remain stable across config edits (paths, commands, etc.)

### Configuration Changes 📝

#### Before (Multiple Types):
```json
{
    "type": "retroarch",
    "rom_path": "/path/to/roms",
    "extension": ".cue",
    "cmd": "retroarch -L core.so %ROM%",
    "platform": "playstation"
}
```

```json
{
    "type": "cemu",
    "cemu_path": "/path/to/Cemu",
    "rom_path": "/path/to/games",
    "cmd": "wine %CEMU_BIN% -g %ROM%",
    "platform": "wii_u"
}
```

#### After (Unified Standard Type):
```json
{
    "type": "standard",
    "rom_path": "/path/to/roms",
    "extension": ".cue",
    "cmd": "retroarch -L core.so %ROM%",
    "platform": "playstation"
}
```

```json
{
    "type": "standard",
    "rom_path": "/path/to/games",
    "extension": ".rpx,.wud,.wux",
    "cmd": "wine /path/to/Cemu/Cemu.exe -g %ROM%",
    "platform": "wii_u"
}
```

### Migration Guide 📋

1. **Update launcher types**: Replace all instances of `retroarch`, `custom`, `cemu`, `rpcs3`, `scummvm` with `standard`
2. **Add missing fields**: Ensure all standard launchers have:
   - `rom_path` - Directory containing games
   - `extension` - File extensions to scan for (e.g., ".iso,.chd")
   - `cmd` - Launch command with %ROM% placeholder
   - `platform` - Platform identifier
3. **Update Cemu configs**:
   - Remove `cemu_path` field
   - Replace `%CEMU_BIN%` with full path to Cemu.exe in `cmd`
   - Add `extension: ".rpx,.wud,.wux"`
4. **User fields remain compatible**: `%CEMU_ACCOUNT%`, `%RETROARCH_CONFIG%`, etc. still work

### New Features 🚀

#### Directory Scanning Support ✅
- **IMPLEMENTED**: Special `scan_pattern: "DIRECTORY"` mode for directory-based games
- Scans for subdirectories instead of files
- Perfect for ScummVM where each game is in its own directory
- Passes directory path as `%ROM%` to the launcher command
- Example:
  ```json
  {
      "type": "standard",
      "rom_path": "/path/to/scummvm/games",
      "scan_pattern": "DIRECTORY",
      "cmd": "scummvm --auto-detect --path=%ROM%",
      "platform": "scummvm"
  }
  ```

#### Pattern-Based File Scanning ✅
- **IMPLEMENTED**: `scan_pattern` field for recursive file discovery
- Supports glob patterns like `*/vol/code/*.rpx` for Cemu
- Supports `*/PS3_GAME/USRDIR/EBOOT.BIN` for RPCS3
- When set, overrides the `extension` field
- Maintains compatibility with existing configs

#### Dynamic User Field System ✅
- **IMPLEMENTED**: Users can now define ANY custom fields in user configs
- All custom fields are automatically available as `%FIELD_NAME%` placeholders in launcher commands
- No more hardcoded user fields - completely flexible system
- Example:
  ```json
  "users": [{
      "name": "Karl",
      "email": "karl@example.com",
      "avatar": "/path/to/avatar.jpg",

      // Define ANY custom fields you want:
      "cemu_account": "80000001",
      "wine_prefix": "/home/karl/.wine-gaming",
      "controller_profile": "xbox360",
      "my_custom_setting": "value123",
      "literally_anything": "becomes_%LITERALLY_ANYTHING%"
  }]
  ```
  Then use in launcher: `"cmd": "WINEPREFIX=%WINE_PREFIX% wine %CEMU_ACCOUNT% game.exe %ROM%"`

#### User Struct Changes
- Removed all hardcoded fields: `cemuAccount`, `rpcs3Account`, `yuzuIndex`, `retroarchConfig`, `savePath`, `dolphinCardPath`
- Added dynamic storage: `customKeys[]`, `customValues[]`, `numCustomFields`
- Core fields remain: `name`, `email`, `avatarPath`

### Bug Fixes 🐛

- Fixed Cemu launchers not getting signatures (fell through to "Unsupported Type")
- Fixed NULL pointer dereferences in config parsing
- Fixed memory leaks in game path handling
- Improved error messages for missing/invalid config paths

### Technical Details 🔍

- Launcher signature calculation: `MurmurHash(type + ":" + platform)`
- All directory scanning now uses unified `importFromCustom()` function
- Special handling removed for: Cemu's `/vol/code/*.rpx` scanning (users should set extension to `.rpx` and point rom_path to appropriate directory)

---

**Note**: This is a breaking change. All users must update their `config.json` files to use the new launcher type system.