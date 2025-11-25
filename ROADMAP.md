# Roadmap

## Planned Features

### Discord Rich Presence
Show currently playing game in Discord status.

### OpenGameDB Auto-Update
Check if the OpenGameDB is out of date and automatically download updates. Would be good if offblast could attempt to download the OpenGameDB if no config entry is in place for it.

### Steam: Detect Removed Games
When a game is removed from Steam, offblast still thinks it's playable.

### List Caching
Cache generated lists to files for faster loading.

### Collections
Curated game collections (e.g., YouTuber lists like Metal Jesus). This is more of an OpenGameDB feature.

---

## Backlog

### CSV Corruption Checking
Validate CSV files on load.

### Animation System Improvements
Refactor animation system - queue animations instead of repeating code. Need to rethink input modes and the system in general.

### Poor Match Score Logging
Games with poor match scores should be logged to the missing games log. Currently allowing bad matches through as long as the proper game isn't present.

### Hours Played in Info Panel
Show total playtime in the game info display.

### Search Keyboard Input
Support keyboard input for search (currently controller-only).
