# Changelog

## [Unreleased]

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