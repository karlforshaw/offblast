# Roadmap

## 0.7.1

### Steam Metadata (Release Dates, Scores, Descriptions)
**SOLVED:** Now fetches from Steam Store API (`store.steampowered.com/api/appdetails`):
- Release dates, Metacritic scores, descriptions fetched automatically
- Metadata fetched on startup for games missing data
- Steam rescrape uses Store API instead of OpenGameDB CSV

### Steam Launch Options
**SOLVED:** Launch in Big Picture mode:
```bash
steam -bigpicture steam://rungameid/<APPID>
```

For uninstalled games:
```bash
steam -bigpicture steam://install/<APPID>
```

Both now implemented in launch().

### Steam: Game Control
**SOLVED:** Rather than trying to kill/resume Steam games from offblast, we now delegate to Steam Big Picture:
- Guide button disabled for Steam games (Big Picture handles it)
- UI shows just "Return" button instead of Resume/Stop
- Pressing Return goes back to offblast main UI; Steam manages the game lifecycle

### Bug: Rescrape Borks Descriptions
**SOLVED:** Fixed missing writeDescriptionBlob() call in OpenGameDB rescrape:
- Rescrape was clearing descriptionOffset but never writing new descriptions
- Now properly writes descriptions from CSV back to description file
- Note: Description file will grow on rescrapes (orphaned blobs not reclaimed, but infrequent use makes this acceptable)

### Manual Cover Adding
**SOLVED:** Context menu (Start button) provides:
- "Copy Cover Filename" - copies `{targetSignature}.jpg` to clipboard
- "Refresh Cover" - reloads texture without restarting
- User downloads cover from LaunchBox/SteamGridDB, renames it, adds to `~/.offblast/covers/`

### Loading Screen
**SOLVED:** Animated loading screen with threaded initialization:
- Breathing rocket animation during startup (sin² curve with pauses at extremes)
- Real-time status updates showing initialization progress
- Progress counters for Steam library fetching and game metadata loading
- Smooth 0.61s exit animation (logo shrinks to dot, text fades out)
- Seamless 0.41s fade-in transition to user select screen
- Heavy initialization runs in background thread for responsive UI
- Early SDL/OpenGL init allows loading screen to appear immediately

### CSV Corruption Checking
**PARTIALLY SOLVED:** Basic validation implemented:
- Score fields validated (must be numeric, in valid ranges)
- Empty name filtering prevents blank entries
- More comprehensive validation (field count, format, etc.) deferred to 0.7.2

### Multi-Region Game Deduplication
**SOLVED:** Essential lists now deduplicate regional variants:
- Case-insensitive title matching catches duplicates like "Persona 4 Golden" (US/EU/JP)
- When duplicates found, prioritize:
  1. Owned version (has path set) - show the version user actually has
  2. Highest score (if neither or both owned - already sorted)
  3. First entry (fallback)
- Essential lists collect ALL platform games, sort by ranking, deduplicate, then take top 25
- All Games view still shows all regional variants
- Note: Some regional variants have different titles in OpenGameDB (e.g., "The GOLDEN") - data quality issue

### Missing Score Handling
**SOLVED:** Games without scores now properly handled:
- Ranking set to sentinel value (999) when no valid score available
- Essential lists filter out games with ranking=999
- Essential lists now sorted by ranking (highest-rated first)
- Info display shows "No score" instead of "999%" when browsing unscored games
- Prevents unrated games from polluting curated lists
- Honest score display (no fake zeros)

### Animation System Improvements
**SOLVED:** Refactored animation system for better performance and maintainability:
- Single array of all animations defined once at startup
- Animation tick loop inlined in main render loop (eliminates 8 function calls per frame)
- `animationRunning()` uses same array (single point of maintenance)
- Adding new animations now only requires updating one array definition
- Removed manual tick calls that had to be updated for each new animation type

### Poor Match Score Logging
**SOLVED:** Games with poor match scores (< 0.5) are now logged to missinggames.log:
- Logs ROM path, matched game name, and match score for review
- Format: `rompath -> matched name (score: 0.42)`
- Allows identification of questionable matches that need manual verification
- Threshold set at 0.5 (less than half tokens matched)

### Hours Played in Info Panel
**SOLVED:** Playtime now displayed in game info panel:
- Shows hours (1 decimal) for games played > 1 hour: "15.2 hrs"
- Shows minutes for games played < 1 hour: "45 mins"
- Rendered at 81% opacity to de-emphasize from core metadata
- Only shown for games with recorded playtime

### Steam Playtime Integration
**SOLVED:** Steam playtime data now syncs to offblast playtime file:
- Converts Steam's `playtime_forever` (minutes) to offblast format (milliseconds)
- Syncs automatically on every app startup
- Overwrites local data with Steam's authoritative playtime
- Steam games appear in Most Played lists with accurate hours
- Reordered initialization to load users → playtime file → launchers

### Resume Functionality on Wayland
**SOLVED:** Resume now works on KDE Plasma Wayland:
- Implemented KWin scripting via D-Bus using `workspace.stackingOrder`
- Creates temporary JavaScript that searches for game window by exclusion:
  - Skips offblast window itself
  - Skips desktop/panel windows (skipTaskbar, skipSwitcher)
  - Activates first normal window found
- Uses `qdbus` instead of `dbus-send` to avoid AppImage library conflicts
- Detects session type (`$XDG_SESSION_TYPE`) and window manager (`$XDG_CURRENT_DESKTOP`)
- Automatically chooses KWin scripting on KDE Wayland, X11 APIs elsewhere
- Tested and working on Bazzite (KDE Plasma Wayland)
- Works with flatpak/AppImage games despite PID sandboxing

---

## 0.8.1

### .desktop File Launcher Support
**SOLVED:** Launch PC games and ports via freedesktop.org .desktop files:
- **Scan directories** for .desktop files with configurable scan_pattern (defaults to "*.desktop")
- **Parse fields**: Name (OpenGameDB matching), Exec (launch command), Path (working directory)
- **Per-game hooks**: X-PreLaunchHook and X-PostLaunchHook override launcher-level hooks
  - Hook stdout dynamically updates status display
  - Breathing animation during hook execution
- **Wine support**: Exec can use wine for Windows executables
- **Icon field** parsed (reserved for future cover fallback)
- **Working directory**: Child process chdir to Path field before launching
- **Smart rescanning**: Clears stale assignments before re-matching .desktop files

**Config example:**
```json
{
  "type": "desktop",
  "platform": "pc",
  "rom_path": "/home/user/.offblast/pc-games/"
}
```

**.desktop example:**
```ini
[Desktop Entry]
Type=Application
Name=Ship of Harkinian
Exec=/home/user/soh/soh.AppImage
Path=/home/user/soh
Icon=/home/user/soh/icon.png
Categories=Game;

# OffBlast per-game hooks
X-PreLaunchHook=/home/user/soh/pre-launch.sh %GAME_NAME%
X-PostLaunchHook=/home/user/soh/post-launch.sh %GAME_NAME%
```

**Tested with:**
- Ship of Harkinian (Ocarina of Time PC port)
- 2Ship2Harkinian (Majora's Mask PC port)
- Sonic 3 A.I.R.
- Sonic 2 Absolute (via Wine)
- Super Mario Bros. Remastered

**Future enhancements:**
- Icon field as cover art fallback (when OpenGameDB has no cover)
- Exec field code support (%f, %u) for desktop environment compatibility

### Pre/Post Launch Hooks
**SOLVED:** Execute custom commands before and after launching games:
- **Pre-launch hooks**: Run before game starts (display config, save swapping, Discord status)
- **Post-launch hooks**: Run after game exits (cleanup, save syncing, restore settings)
- **Per-launcher scope**: Configured in launcher definition in config.json
- **Comprehensive slug substitution**: Replace placeholders with game/launcher/user metadata
  - Game fields: %GAME_NAME%, %ROM_PATH%, %GAME_ID%, %PLATFORM%, %COVER_URL%, %GAME_DATE%, %GAME_RANKING%
  - Launcher fields: %LAUNCHER_TYPE%, %LAUNCHER_NAME%, %ROM_DIR%
  - User fields: %USER_NAME%, %USER_EMAIL%, %USER_AVATAR%, plus any custom user fields
- **Real-time status updates**: Hook script stdout dynamically updates displayed status
  - Each line of output from hook updates the status message
  - Config status messages optional (used as fallback if no output yet)
  - Displayed centered below game cover (same position as Resume/Stop buttons)
- **Breathing animation**: Game cover pulses during hook execution
  - Smooth 300ms fade-in/fade-out transitions between static and breathing
  - Same sin² curve animation as loading screen rocket
- **Manual abort**: Press west button (X) or escape key to kill hook and cancel launch
- **Proper shell quoting**: All substituted values automatically quoted for safe shell parsing

**Config example:**
```json
{
  "type": "retroarch",
  "platform": "psx",
  "pre_launch_hook": "/path/to/setup.sh %GAME_NAME% %ROM_PATH%",
  "post_launch_hook": "/path/to/cleanup.sh %GAME_NAME%"
}
```

**Hook script example:**
```bash
#!/bin/bash
echo "Checking network connection..."
sleep 1
echo "Mounting save share..."
mount -t nfs server:/saves /mnt
echo "Ready to launch!"
```

**Future enhancements (deferred):**
- Per-game hooks via .desktop files (planned for desktop launcher support)
- Global hooks with override hierarchy (user → launcher → game)
- Configurable timeouts (currently manual abort only)

### Custom Lists
**SOLVED:** File-based game collections that appear as rows on the home screen:
- JSON files stored in `~/.offblast/lists/*.json`
- Auto-discovery: loads all JSON files from lists directory on startup
- Simple format: name, description, author, platform, array of game names
- Fuzzy name matching finds games in your library (resilient to OpenGameDB changes)
- Shows ALL games in list (owned + unowned) to create "collection completion" feeling
- Unowned games grayed out (30% desaturated, 70% alpha, like uninstalled Steam games)
- Portable and shareable: commit to GitHub, share with friends
- Example included: "Metal Jesus Top 10 PS2" from YouTube video
- Lists appear as rows alongside "Jump Back In", "Essential PSX", platform rows
- Empty lists skipped (no games matched in OpenGameDB)

**File format example:**
```json
{
  "name": "Metal Jesus Top 10 PS2",
  "description": "Metal Jesus Rocks' favorite PlayStation 2 games",
  "author": "MetalJesusRocks",
  "url": "https://www.youtube.com/watch?v=QsZfEtaO_us",
  "platform": "playstation_2",
  "games": ["SSX 3", "Grand Theft Auto: Vice City", "Burnout 3: Takedown", ...]
}
```

**Future enhancements:**
- UI for managing lists (add/remove games, create new lists)
- AppImage auto-install bundled lists (like OpenGameDB sync)
- List metadata (icons, colors, custom sorting)
- Recommendation engine based on list overlap
- URL loading (fetch from GitHub, auto-update)

### Retro Achievements Support
**SOLVED:** Display achievement progress for retro games via RetroAchievements.org:
- Shows achievement progress in game info panel: `" | 15/20 Achievements (75%)"`
- Per-user RA credentials stored in user custom fields (no global config needed)
- Smart lazy evaluation: only processes games user has started on RA (typically 10-100, not 1000+)
- On-demand hash verification after 1.6s linger (fuzzy match → extract .zip → hash ROM → verify via API)
- Integrated rcheevos library for proper ROM hashing (handles .zip extraction, console-specific rules)
- Persistent database caches verified matches: `~/.offblast/{email}.ragames`
- Automatic refresh after gameplay: re-fetches RA data and syncs achievement counts
- Works with both .zip archives and raw ROM files
- Comprehensive platform mapping (handles OpenGameDB naming variations)
- Status message: "Verifying RetroAchievements..." during hash calculation
- Failed verifications cached to prevent retry
- Only shows for games with achievements (total > 0)

**Future enhancements:**
- Achievement list browser per game (detailed view with icons, descriptions, unlock dates)
- Achievement notifications when unlocked during gameplay

### Multi-threaded Steam Metadata Fetching
**SOLVED:** Parallel fetching dramatically improves cold start performance:
- 3 worker threads fetch metadata concurrently from work queue
- 200ms rate limiting ensures ~5 req/sec across all threads (respects Steam API limits)
- 50 games now complete in ~10 seconds instead of 100 seconds (~8x speedup)
- Producer-consumer pattern with three-tier mutex strategy prevents race conditions:
  - `queue->mutex`: Protects work queue and rate limiting timestamp
  - `steamMetadataLock`: Protects database writes and file growth operations
  - `loadingState.mutex`: Protects progress display updates
- Loading screen shows "Fetching Steam metadata..." with live progress counter
- Failed fetches gracefully handled (marked with ranking=999)
- No database corruption from parallel writes to description blobs

### Steam Achievements Support
**SOLVED:** Display achievement progress for Steam games:
- Reads achievement data from local Steam cache: `~/.steam/steam/userdata/<userid>/config/librarycache/achievement_progress.json`
- Shows achievement progress in game info panel for Steam games
- Format: `" | 15/20 Achievements (75%)"`
- No network calls required - instant lookup from local file
- Works with private Steam profiles (no API key needed)
- Only displays for games that have achievements (total > 0)
- Rendered at 81% alpha to visually de-emphasize from core metadata

### Achievements Browser
**SOLVED (RA only):** Browse detailed achievement information for RetroAchievements games:
- Press X button to open achievement browser (only for verified RA games)
- Shows all achievements with title, description, points, unlock date
- Badge icons download from RA and cache in `~/.offblast/achievement_badges/`
- Visual distinction: unlocked (colorful badges, bright text) vs locked (grayed out, 70% desaturated, 40% alpha)
- Scrollable list with D-pad up/down navigation
- Background badge downloading (async, non-blocking)
- Cursor indicator shows position: "3/20"

**Visual Improvements Needed:**
- Better layout/spacing (current implementation functional but basic)
- Color scheme refinement
- Typography improvements
- Possibly add achievement rarity/difficulty indicators
- Transition animations (fade in/out, slide effects)

**Future Enhancements:**
- Steam achievement browser (requires Steam Web API for details)
- Show global unlock percentages
- Achievement icons in a grid view option
- Filter by locked/unlocked
- Sort by date, points, rarity

### Import New Games from CSV
**SOLVED:** Pull OpenGameDB CSV updates without destroying database:
- New context menu option: "Import New Games from CSV"
- Scans platform CSV and creates LaunchTarget entries for new games only
- Preserves existing entries and their path assignments
- Previously required deleting launchtargets.bin and losing all launcher associations
- Now can update OpenGameDB repository and import new entries seamlessly
- Shows count of newly imported games in status message
- Works for all platforms (pc, playstation, nintendo_64, etc.)

### Multi-User Steam Support
**SOLVED:** Each family member can have their own Steam account:
- Per-user Steam credentials: `steam_account_name` custom field (matches AccountName in loginusers.vdf)
- Automatic account switching via loginusers.vdf manipulation (sets MostRecent field)
- One-time setup: family members log into Steam with "Remember Password" once
- No password re-entry: uses Steam's cached credentials
- Seamless switching: kills Steam, modifies VDF, relaunches (10-15 seconds)
- Owner tagging: LaunchTarget.ownerTag field tracks Steam game ownership
- Visibility filtering: users only see their own Steam games
- Shared ROMs: empty ownerTag means visible to all users (family library)
- Deferred import: Steam games loaded after player selection (not at startup)
- Loading screen: shows user avatar with live import progress
- Multi-threaded metadata: reuses existing 3-worker system with progress counter
- Database efficiency: all users' games stored once, filtered on display
- Perfect for couch gaming: kids select profile → auto-switch Steam → their library appears

**Future enhancements:**
- Detect Steam account from file system before switching (avoid unnecessary restarts)
- Cache which user owns which games to skip reimport on subsequent logins
- Support for Steam Family Sharing (detect shared games vs owned)

### Steam Offline Handling
**PARTIALLY SOLVED:** Graceful fallback to local detection:
- API timeout set to 30 seconds (could be reduced)
- Falls back to appmanifest scanning on API failure
- Shows only installed games in offline mode
- Multi-user switching still works offline (only affects uninstalled game visibility)

**Future improvements:**
- Reduce API timeout (30s → 5s)
- Quick online check before API call
- Better user feedback during timeout
- Skip metadata workers if library fetch fails

### Search Keyboard Input
**SOLVED:** Keyboard typing support for search functionality:
- Type directly on keyboard instead of using controller character wheel
- Accepts alphanumeric characters and spaces
- Backspace to delete characters
- Enter to confirm and close search overlay
- Dual input mode: keyboard and controller both work simultaneously
- Semi-transparent overlay (60% opacity) shows results filtering in real-time
- SDL_TEXTINPUT events for proper character handling (respects Shift, etc.)
- Automatically enables/disables text input when entering/exiting search
- No conflict with hjkl navigation (navigation disabled during search)

---

## Future Features (0.9.0+)

### Personal Game Data & AI-Generated Challenges
Track personal game metadata and use AI to generate curated playlists:

**Personal Game Data:**
- Per-user database: `~/.offblast/{email}.personal_data`
- Fields to track:
  - Personal rating (1-5 stars or 0-100 scale)
  - Lifetime hours estimate (total ever played, not just current platform)
  - Worth it? (yes/no/mixed feelings)
  - Completion status (not started, in progress, completed, 100%)
  - Free-form notes (thoughts, memories, quotes)
  - Tags (completed, favorite, backlog, multiplayer, etc.)
  - Recommend to others (0-10 scale)
- UI: Context menu → "Add Personal Notes"
- Simple overlay with fields (controller/keyboard input)
- Visible in game info panel or dedicated view

**AI Challenge Lists:**
- Export personal data to JSON for AI consumption
- User runs script or in-app AI integration
- AI analyzes: ratings, playtime, completion, tags, notes
- AI generates seasonal challenge list: "Summer 2026 Challenge"
- Curated 5-10 game playlist based on:
  - Games you haven't played yet
  - Similar to games you rated highly
  - Platform/genre variety
  - Estimated completion time fits season (summer = longer games)
- Challenge appears as custom list in OffBlast
- Updates weekly or monthly with new suggestions

**Benefits:**
- Less scrolling paralysis, more playing
- Discover games you own but forgot about
- Personalized curation (better than generic "top 10" lists)
- Seasonal variety keeps gaming fresh
- Builds on existing custom lists infrastructure

**AI Integration Options:**
- External script using free AI APIs (Claude, ChatGPT, Ollama)
- In-app integration with local LLM (privacy-focused)
- Cloud service (optional, user-controlled)
- Community-shared prompts/templates

**Future Extensions:**
- Compare with friends' ratings (social recommendations)
- "Similar to X game" suggestions
- Backlog management: "You have 47 unplayed games, here are the top 5 to try"
- Genre/mood-based generation: "Rainy day cozy games"

### Cover Browser UI Enhancements
- Replace scroll indicator dots with proper arrow textures
  - Current: Simple filled squares
  - Desired: Triangle or chevron textures pointing up/down
  - Could use custom texture assets or render with geometry

### LaunchBox Games Database Integration
Add support for LaunchBox DB as an alternative/supplementary metadata source:

**What's Available:**
- Daily updated Metadata.zip (99MB) from https://gamesdb.launchbox-app.com/Metadata.zip
- Comprehensive XML with game metadata, multiple image types, regional variants
- Image types: Box - Front, Box - Front - Reconstructed, Box - Back, Box - 3D, Clear Logo, Fanart, Screenshots, etc.
- URL pattern: `https://images.launchbox-app.com/{FileName}` (filename from XML)
- No API key required - download once, parse locally, query offline

**Advantages over OpenGameDB:**
- Reconstructed covers (high-quality community artwork)
- Multiple image types per game (not just one cover URL)
- Better regional variant handling
- More complete metadata (community ratings, alternate names, etc.)
- 3D box art and clear logos available

**Implementation Considerations:**
- 99MB download + XML parsing on startup (performance impact)
- Need to index by game name + platform for fast lookups
- Could be used alongside OpenGameDB (user chooses preferred source)
- Daily manual updates vs OpenGameDB's version-controlled CSVs

**CRITICAL - Licensing Unknown:**
- Must contact LaunchBox team for redistribution/bundling permissions
- OpenGameDB is CC0 (public domain) - safe to bundle
- LaunchBox DB license terms not publicly documented
- Need written permission before bundling Metadata.zip with OffBlast
- May only be usable as download-on-demand, not pre-bundled

**Potential Use Cases:**
1. Replace OpenGameDB entirely (if licensing permits)
2. Supplement OpenGameDB (use LaunchBox for covers, OpenGameDB for scores)
3. User-selectable metadata source in config
4. Cover browser: show LaunchBox + SteamGridDB options side-by-side

### Discord Rich Presence
Show currently playing game in Discord status.

### Steam: Per-User Credentials
Move `steam.api_key` and `steam.steam_id` to user config instead of global. Each user could have their own Steam account.

### Regional Variant Architecture
Formulate a full plan for how to deal with different regional versions of the same game:
- Current solution (case-insensitive deduplication) is a workaround for display
- Proper solution may require OpenGameDB schema changes
- Need to consider:
  - Linking regional variants (canonical_id or parent_id in database)
  - User's region preference configuration
  - Showing which region is owned vs available
  - UI for switching between owned regional variants
  - Handling cases where user owns multiple regions of same game
- May need collaboration with OpenGameDB maintainers for schema design
