# OffBlast
An experimental game launcher written in C with SDL2. I'm just screwing around at this point so come back some other time.

## The whole thing minimizes when I click another window
This happened to me too, try adding this to your .bashrc or .profile
```export SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS=0```
