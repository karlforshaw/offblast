#define _GNU_SOURCE
#define SCALING 2.0

#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <json-c/json.h>
#include <murmurhash.h>

#include "offblast.h"
#include "offblastDbFile.h"

typedef struct PathInfo {
    uint32_t idHash;
    uint32_t contentsHash;
} PathInfo;

typedef struct PathInfoFile {
    uint32_t nEntries;
    PathInfo entries[];
} PathInfoFile;

typedef struct LaunchTarget {
    uint32_t signature;
    char fileName[256];
    char path[PATH_MAX];
} LaunchTarget;

typedef struct LaunchTargetFile {
    uint32_t nEntries;
    LaunchTarget targets[];
} LaunchTargetFile;



int main (int argc, char** argv) {

    printf("\nStarting up OffBlast with %d args.\n\n", argc);


    char *homePath = getenv("HOME");
    assert(homePath);

    char *configPath;
    asprintf(&configPath, "%s/.offblast", homePath);

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



    char *configFilePath;
    asprintf(&configFilePath, "%s/config.json", configPath);
    FILE *configFile = fopen(configFilePath, "r");

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


    char *pathInfoDbPath;
    asprintf(&pathInfoDbPath, "%s/pathinfo.bin", configPath);
    PathInfoFile *pathInfoFromDisk = NULL;
    uint32_t nPathsInDbFile = 0;

    FILE *pathInfoFd; 
    if (!(pathInfoFd = initialize_db_file(pathInfoDbPath, (void**)&pathInfoFromDisk, 
            sizeof(PathInfo), &nPathsInDbFile)))
    {
        printf("ERROR: couldn't initialize pathInfoDB file");
        return 1;
    }


    char *launchTargetDbPath;
    asprintf(&launchTargetDbPath, "%s/launchtargets.bin", configPath);
    LaunchTargetFile *launchTargetsFromDisk = NULL;
    uint32_t nTargetsInDbFile = 0;
    FILE *launchTargetFd;
    if (!(launchTargetFd = initialize_db_file(launchTargetDbPath, (void**)&launchTargetsFromDisk, 
            sizeof(LaunchTarget), &nTargetsInDbFile)))
    {
        printf("ERROR: couldn't initialize LaunchTargets DB file");
        return 1;
    }



    size_t nPaths = json_object_array_length(paths);
    size_t newPathInfoSize = 
        FIELD_SIZEOF(PathInfoFile, nEntries) + (nPaths * sizeof(PathInfo));

    PathInfoFile *pathInfoFile = calloc(1, newPathInfoSize); 
    pathInfoFile->nEntries = nPaths;

    for (int i=0; i<nPaths; i++) {

        json_object *workingPathNode = NULL;
        json_object *workingPathStringNode = NULL;
        json_object *workingPathExtensionNode = NULL;

        const char *thePath = NULL;
        const char *theExtension = NULL;

        workingPathNode = json_object_array_get_idx(paths, i);
        json_object_object_get_ex(workingPathNode, "path",
                &workingPathStringNode);
        json_object_object_get_ex(workingPathNode, "extension",
                &workingPathExtensionNode);

        thePath = json_object_get_string(workingPathStringNode);
        theExtension = json_object_get_string(workingPathExtensionNode);

        printf("Running Path for %s: %s\n", theExtension, thePath);

        DIR *dir = opendir(thePath);
        // TODO NFS shares when unavailable just lock this up!
        if (dir == NULL) {
            printf("Path %s failed to open\n", thePath);
            break;
        }

        unsigned int nEntriesToAlloc = 10;
        unsigned int nAllocated = nEntriesToAlloc;

        void *fileNameBlock = calloc(nEntriesToAlloc, 256);
        char (*matchingFileNames)[256] = fileNameBlock;

        int numItems = 0;
        struct dirent *currentEntry;

        while ((currentEntry = readdir(dir)) != NULL) {

            char *ext = strrchr(currentEntry->d_name, '.');

            if (ext && strcmp(ext, theExtension) == 0){

                memcpy(matchingFileNames + numItems, 
                        currentEntry->d_name, 
                        strlen(currentEntry->d_name));

                numItems++;
                if (numItems == nAllocated) {

                    unsigned int bytesToAllocate = nEntriesToAlloc * 256;
                    nAllocated += nEntriesToAlloc;

                    void *newBlock = realloc(fileNameBlock, 
                            nAllocated * 256);

                    if (newBlock == NULL) {
                        printf("failed to reallocate enough ram\n");
                        return 0;
                    }

                    fileNameBlock = newBlock;
                    matchingFileNames = fileNameBlock;

                    memset(
                            matchingFileNames+numItems, 
                            0x0,
                            bytesToAllocate);

                }
            }
        }

        uint32_t contentSignature = 0;
        uint32_t pathSignature = 0;
        lmmh_x86_32(thePath, strlen(thePath), 33, &pathSignature);
        lmmh_x86_32(matchingFileNames, numItems*256, 33, &contentSignature);

        printf("got sig: idHash:%u contentsHash:%u\n", pathSignature, 
                contentSignature);

        pathInfoFile->entries[i].idHash = pathSignature;
        pathInfoFile->entries[i].contentsHash = contentSignature;

        uint32_t rescrapeRequired = (pathInfoFromDisk == NULL);

        if (pathInfoFromDisk != NULL) {
            for (uint32_t i=0; i<pathInfoFromDisk->nEntries; i++) {
                if (pathInfoFromDisk->entries[i].idHash == pathSignature
                        && pathInfoFromDisk->entries[i].contentsHash 
                        != contentSignature) 
                {
                    printf("Contents of directory %s have changed!\n", thePath);
                    rescrapeRequired = 1;
                }
                else if (pathInfoFromDisk->entries[i].idHash == pathSignature)
                {
                    printf("Contents unchanged for: %s\n", thePath);
                }
            }
        }

        if (rescrapeRequired) {
            void *romData = calloc(1, ROM_PEEK_SIZE);

            for (uint32_t j=0;j<numItems; j++) {

                char *romPathTrimmed; 
                asprintf(&romPathTrimmed, "%s/%s", 
                        thePath,
                        matchingFileNames[j]);

                uint32_t romSignature;
                FILE *romFd = fopen(romPathTrimmed, "rb");
                if (! romFd) {
                    printf("cannot open from rom\n");
                }

                if (!fread(romData, ROM_PEEK_SIZE, 1, romFd)) {
                    printf("cannot read from rom\n");
                }
                else {
                    lmmh_x86_32(romData, ROM_PEEK_SIZE, 33, &romSignature);
                    memset(romData, 0x0, ROM_PEEK_SIZE);
                    printf("signature is %u\n", romSignature);
                }

                memset(romData, 0x0, ROM_PEEK_SIZE);
                free(romPathTrimmed);
                fclose(romFd);

                // Now we have the signature we can add it to our DB
                // XXX here

            }; 

            free(romData);
        }

        matchingFileNames = NULL;
        free(fileNameBlock);
        closedir(dir);
    }

    fseek(pathInfoFd, 0, SEEK_SET);
    if (!fwrite(pathInfoFile, newPathInfoSize, sizeof(char), pathInfoFd)) {
        printf("Couldn't write to pathinfo.bin\n");
        return 1;
    }
    fclose(pathInfoFd);


    return 0;

    const char *userName = NULL;
    {
        json_object *usersObject = NULL;
        json_object_object_get_ex(configObj, "users", &usersObject);

        if (usersObject == NULL) {
            userName = "Anonymous";
        }
        else {
            json_object *tmp = json_object_array_get_idx(usersObject, 0);
            assert(tmp);
            userName = json_object_get_string(tmp);
        }
    }

    printf("got user name %s\n", userName);



    // XXX START SDL HERE

    



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



    SDL_Surface* textSurface;
    SDL_Color textColor = {0,0,0};

    char *welcomeMsg;
    asprintf(&welcomeMsg, "Hey %s, let's play!", userName);
    textSurface = TTF_RenderText_Blended(font, welcomeMsg, textColor);
    free(welcomeMsg);

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
