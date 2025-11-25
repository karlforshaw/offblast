# Roadmap

## 0.7.1

### Steam Metadata (Release Dates, Scores, Descriptions)
Steam API only provides game names and playtime. Need to fetch additional metadata:
- Release dates, Metacritic scores, descriptions not available from GetOwnedGames
- Options: Steam Store API, SteamSpy, or match against steam.csv for metadata only
- Rescrape functionality needs testing for Steam games

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
Rescrape functionality seems to corrupt or break game descriptions.

### CSV Corruption Checking
Validate CSV files on load.

### Animation System Improvements
Refactor animation system - queue animations instead of repeating code. Need to rethink input modes and the system in general.

### Poor Match Score Logging
Games with poor match scores should be logged to the missing games log. Currently allowing bad matches through as long as the proper game isn't present.

### Hours Played in Info Panel
Show total playtime in the game info display.

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

### Loading/Status Window
Replace console output with a visual loading window for startup operations:
- Steam API fetching progress
- OpenGameDB scanning
- Cover art downloading
Move away from dumping everything to console.

### Discord Rich Presence
Show currently playing game in Discord status.

### Steam: Per-User Credentials
Move `steam.api_key` and `steam.steam_id` to user config instead of global. Each user could have their own Steam account.

### List Caching
Cache generated lists to files for faster loading.

### Collections
Curated game collections (e.g., YouTuber lists like Metal Jesus). This is more of an OpenGameDB feature.
