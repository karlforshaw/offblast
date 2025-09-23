# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

- **Build**: `make`
- **Clean**: `make clean`
- **Install config**: `make install` (creates ~/.offblast/config.json from config-dist.json)
- **Run**: `./offblast`

## Codebase Overview

OffBlast is a game launcher written in C using SDL2 and OpenGL. The project consists of a single main executable that provides a visual interface for launching games through various emulators and launchers.

### Key Components

- **main.c** (181K lines): Core application logic including UI rendering, input handling, game database management, and launcher integration
- **offblastDbFile.c/h**: Database file format handling for storing game metadata, launcher configs, and playtime tracking
- **shaders/**: GLSL shaders for rendering UI elements (text, images, gradients)

### Platform Support

The launcher supports multiple platforms through different launcher types:
- **retroarch**: Generic RetroArch launcher for various console platforms
- **cemu**: Wii U emulator support
- **dolphin**: GameCube emulator (via custom launcher)
- **pcsx2**: PlayStation 2 emulator
- **rpcs3**: PlayStation 3 emulator
- **scummvm**: Adventure game engine
- **steam**: Steam games integration
- **custom**: Shell script-based custom launchers

### Data Structures

Key data structures in offblastDbFile.h:
- **LaunchTarget**: Game metadata (name, platform, path, cover URL, etc.)
- **Launcher**: Emulator/launcher configuration
- **PlayTime**: Gameplay tracking data
- **OffblastDbFile**: Generic database file format

### Configuration

Configuration is stored in ~/.offblast/config.json with:
- **launchers**: Array of launcher configurations with platform mappings
- **users**: User profiles with save paths and emulator-specific settings
- **opengamedb**: Path to OpenGameDB CSV files for game metadata
- **playtime_path**: Directory for storing playtime data

### Dependencies

Required libraries (Ubuntu/Pop!_OS):
- SDL2 development files
- libcurl (for network requests)
- json-c (JSON parsing)
- GLEW (OpenGL extension loading)
- X11/Xmu (window management)
- libxml2 (XML parsing)
- murmurhash (hashing)

### Database Files

OffBlast uses custom binary database files (.db) stored in user directories for:
- Launch targets (games)
- Descriptions
- Playtime tracking

The database format uses memory-mapped files with fixed and blob storage types.

### UI Rendering

The UI uses OpenGL with custom shaders for:
- Text rendering (using stb_truetype)
- Image display (using stb_image)
- Gradient backgrounds

### Input Modes

The application supports multiple input modes:
- Navigation (browsing games)
- Player selection
- Menu interaction
- Search functionality

### Integration with OpenGameDB

OffBlast relies on OpenGameDB CSV files for game metadata. The database provides:
- Game titles and release dates
- Cover art URLs
- Game descriptions
- Platform information