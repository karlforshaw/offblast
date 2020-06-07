# OffBlast
An experimental game launcher written in C with SDL2. This is ALPHA software, please be patient.
As of now, you can happily use this to launch games using Retroarch, Cemu, Dolphin, and Steam. I'm adding new platforms 
all the time so keep checking back or open an issue.

[![offblast.png](https://i.postimg.cc/YjB0Rf4M/offblast.png)](https://postimg.cc/Lgvm6PBb)

## Get the OpenGameDB
Offblast is powered by the OpenGameDb, which is a bunch of csv files that are publicly available on github for everyone.
Check it out before you install Offblast:

[OpenGameDB on Github](https://github.com/karlforshaw/opengamedb)

## Compilation Requirements (Ubuntu, Pop!\_OS)
Install the required libraries

```sudo apt install build-essential libsdl2-dev libcurl4-openssl-dev libjson-c-dev libglew-dev libx11-dev libxml2-dev libmurmurhash-dev libxmu-dev```

Compile offblast

```make```

Run

```./offblast```

## Create your config file
You'll need to create a `config.json` file in your `~/.offblast` folder in order to get things running. A sample is included in the root of the repository. [Click here](https://github.com/karlforshaw/offblast/blob/master/config-dist.json) to view it, it's not that complex.

## The whole thing minimizes when I click another window
This happened to me too, try adding this to your .bashrc or .profile
```export SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS=0```
