#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <json-c/json.h>

#define SCALING 2.0

int main (int argc, char** argv) {

    printf("\nStarting up OffBlast with %d args.\n\n", argc);



    char *homePath = getenv("HOME");
    assert(homePath);

    char *configPath = strcat(homePath, "/.offblast");
    int madeConfigDir;
    madeConfigDir = mkdir(configPath, S_IRWXU);
    
    if (madeConfigDir == 0) {
        printf("Created offblast directory\n");
    }
    else {
        switch (errno) {
            case EEXIST:
                break;

            default:
                printf("Couldn't create offblast dir %d\n", errno);
                return errno;
        }
    }

    // Get a list of romsdirs 
    FILE *configFile = fopen(strcat(configPath, "/config.json"), "r");
    fseek(configFile, 0, SEEK_END);
    long configSize = ftell(configFile);
    fseek(configFile, 0, SEEK_SET);

    char *configText = calloc(1, configSize + 1);
    fread(configText, 1, configSize, configFile);
    fclose(configFile);

    json_tokener *tokener = json_tokener_new();
    json_object *configObj = NULL;

    configObj = json_tokener_parse_ex(tokener,
            configText,
            configSize);

    assert(configObj);

    json_object *paths = NULL;
    json_object_object_get_ex(configObj, "paths", &paths);

    assert(paths);
    
    json_object *workingPathEntry = NULL;
    json_object *workingPath = NULL;

    size_t nPaths = json_object_array_length(paths);

    for (int i=0; i<nPaths; i++) {
        workingPathEntry = json_object_array_get_idx(paths, i);
        json_object_object_get_ex(workingPathEntry, "path", &workingPath);
        printf("Path %d: %s\n", i, json_object_get_string(workingPath));
    }





    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL initialization Failed, exiting..\n");
        return 1;
    }

    if (TTF_Init() == -1) {
        printf("TTF initialization Failed, exiting..\n");
        return 1;
    }

    // Let's create the window
    SDL_Window* window = SDL_CreateWindow("OffBlast", 
            SDL_WINDOWPOS_UNDEFINED, 
            SDL_WINDOWPOS_UNDEFINED,
            640,
            480,
            SDL_WINDOW_FULLSCREEN_DESKTOP | 
                SDL_WINDOW_ALLOW_HIGHDPI);

    if (window == NULL) {
        printf("SDL window creation failed, exiting..\n");
        return 1;
    }

    SDL_Renderer* renderer;

    renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_SetRenderDrawColor(renderer, 0xFD, 0xF9, 0xFA, 0xFF);

    TTF_Font *font;
    font = TTF_OpenFont("fonts/Roboto-Regular.ttf", 30*SCALING);
    if (!font) {
        printf("Font initialization Failed, %s\n", TTF_GetError());
        return 1;
    }

    // Let's try and render hello world then
    SDL_Surface* textSurface;
    SDL_Color textColor = {0,0,0};
    textSurface = TTF_RenderText_Blended(font, "Let's Play Some Games!", textColor);

    if (!textSurface) {
        printf("Font render failed, %s\n", TTF_GetError());
        return 1;
    }

    SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
    SDL_FreeSurface(textSurface);

    SDL_Rect destRect = {30*SCALING, 30*SCALING, 0, 0};
    SDL_QueryTexture(textTexture, NULL, NULL, &destRect.w, &destRect.h);

    int running = 1;
    while (running) {

        int winWidth = 0;
        int winHeight = 0;

        SDL_GetWindowSize(window, &winWidth, &winHeight);

        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                printf("shutting down\n");
                running = 0;
                break;
            }
            else if (event.type == SDL_KEYUP) {
                SDL_KeyboardEvent *keyEvent = (SDL_KeyboardEvent*) &event;
                if (keyEvent->keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    printf("escape pressed, shutting down.\n");
                    running = 0;
                    break;
                }
                else {
                    printf("key up %d\n", keyEvent->keysym.scancode);
                }
            }

        }

        // TODO check running again?
        SDL_RenderClear(renderer);

        // Draw the title bar
        destRect.x = (winWidth / 2) - (destRect.w / 2);
        SDL_RenderCopy(renderer, textTexture, NULL, &destRect);

        SDL_RenderPresent(renderer);

        // TODO deduct the ms it took to loop so far
        SDL_Delay(16);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
