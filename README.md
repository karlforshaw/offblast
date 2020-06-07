# OffBlast
An experimental game launcher written in C with SDL2. This is ALPHA software, please be patient.

# Compilation Requirements (Ubuntu, Pop!\_OS)
Install the required libraries

```sudo apt install build-essential libsdl2-dev libcurl4-openssl-dev libjson-c-dev libglew-dev libx11-dev libxml2-dev libmurmurhash-dev libxmu-dev```

Compile offblast

```make```

Run

```./offblast```

## The whole thing minimizes when I click another window
This happened to me too, try adding this to your .bashrc or .profile
```export SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS=0```
