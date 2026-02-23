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
Games with poor match scores should be logged to the missing games log. Currently allowing bad matches through as long as the proper game isn't present.

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

### Steam Offline Handling
What happens when Steam is configured but there's no internet connection? Currently the API call will fail and fall back to local appmanifest scanning, but need to verify this gracefully handles:
- No network at all
- Timeout scenarios
- Partial failures mid-fetch

### Search Keyboard Input
Support keyboard input for search (currently controller-only).

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

## Planned Features

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

### List Caching
Cache generated lists to files for faster loading.

### Collections
Curated game collections (e.g., YouTuber lists like Metal Jesus). This is more of an OpenGameDB feature.

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
