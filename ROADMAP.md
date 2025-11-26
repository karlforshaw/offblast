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
Validate CSV files on load.

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
Refactor animation system - queue animations instead of repeating code. Need to rethink input modes and the system in general.

### Poor Match Score Logging
Games with poor match scores should be logged to the missing games log. Currently allowing bad matches through as long as the proper game isn't present.

### Hours Played in Info Panel
**SOLVED:** Playtime now displayed in game info panel:
- Shows hours (1 decimal) for games played > 1 hour: "15.2 hrs"
- Shows minutes for games played < 1 hour: "45 mins"
- Rendered at 81% opacity to de-emphasize from core metadata
- Only shown for games with recorded playtime

### Steam Playtime Integration
Steam API returns `playtime_forever` for each game. Either:
- Translate Steam playtime to our PlayTime format, or
- Bypass our playtime tracking entirely for Steam games and use Steam's data directly

### Steam Offline Handling
What happens when Steam is configured but there's no internet connection? Currently the API call will fail and fall back to local appmanifest scanning, but need to verify this gracefully handles:
- No network at all
- Timeout scenarios
- Partial failures mid-fetch

### Search Keyboard Input
Support keyboard input for search (currently controller-only).

---

## Planned Features

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
