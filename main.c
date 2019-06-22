#define _GNU_SOURCE
#define SCALING 2.0

#define FIELD_SIZEOF(t, f) (sizeof(((t*)0)->f))

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


typedef struct PathInfo {
    uint32_t idHash;
    uint32_t contentsHash;
} PathInfo;

typedef struct PathInfoFile {
    uint32_t nEntries;
    PathInfo entries[];
} PathInfoFile;



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

    size_t nPaths = json_object_array_length(paths);

    char* pathInfoDbPath;
    asprintf(&pathInfoDbPath, "%s/pathinfo.bin", configPath);

    uint32_t nPathsInDbFile = 0;

    PathInfoFile *pathInfoFromDisk = 
        calloc(1, FIELD_SIZEOF(PathInfoFile, nEntries)); 

    FILE *pathInfoFd = fopen(pathInfoDbPath, "r+b");
    if (!pathInfoFd) {
        if (errno == ENOENT) {
            pathInfoFd = fopen(pathInfoDbPath, "w+b");
            if (!pathInfoFd) {
                perror("trying to open pathinfo.bin\n");
                return errno;
            }
        }
        else {
            perror("trying to open pathinfo.bin\n");
            return errno;
        }
    }

    if(fread(pathInfoFromDisk, 
                FIELD_SIZEOF(PathInfoFile, nEntries), 1, pathInfoFd)) 
    {
        nPathsInDbFile = pathInfoFromDisk->nEntries;
        printf("there are %u paths known in the db\n", 
                nPathsInDbFile);

        size_t tmpNewSize = 
            FIELD_SIZEOF(PathInfoFile, nEntries) + 
            (nPathsInDbFile * sizeof(PathInfo));

        PathInfoFile *tmpBlock = realloc(pathInfoFromDisk, tmpNewSize);
        if (!tmpBlock) {
            printf("cannot allocate enough ram for pathinfo db\n");
            return 1;
        }
        else {
            pathInfoFromDisk = tmpBlock;
            uint32_t itemsRead = 0;
            itemsRead = fread(&pathInfoFromDisk->entries, 
                    sizeof(PathInfo), nPathsInDbFile, pathInfoFd); 

            if (itemsRead < pathInfoFromDisk->nEntries) {
                printf("possible corruption in pathinfo file");
                return 1;
            }

        }
    }
    else {
        printf("according to this theres nothing\n");
    }

    fseek(configFile, 0, SEEK_SET);

    size_t pathInfoSize = sizeof(PathInfoFile) + (nPaths * sizeof(PathInfo));
    PathInfoFile *pathInfoFile = calloc(1, pathInfoSize); 
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
                    //printf("Allocating more memory..\n");

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

                    // TODO DEBUG THIS!

                }
            }
        }

        // XXX DEBUG
        /*
        for (int j=0;j<numItems;j++) {
            printf("%s\n", matchingFileNames[j]);
        }
        printf("total items %d\n", numItems);
        */
        // XXX

        uint32_t contentSignature = 0;
        uint32_t pathSignature = 0;
        lmmh_x86_32(thePath, strlen(thePath), 33, &pathSignature);
        lmmh_x86_32(matchingFileNames, numItems*256, 33, &contentSignature);

        printf("got sig: idHash:%u contentsHash:%u\n", pathSignature, 
                contentSignature);

        pathInfoFile->entries[i].idHash = pathSignature;
        pathInfoFile->entries[i].contentsHash = contentSignature;

        // Has this changed from what we have on disk?
        if (pathInfoFromDisk->nEntries > 0) {
            for (uint32_t i=0; i<pathInfoFromDisk->nEntries; i++) {
                if (pathInfoFromDisk->entries[i].idHash == pathSignature
                        && pathInfoFromDisk->entries[i].contentsHash 
                        != contentSignature) 
                {
                    printf("Contents of directory %s have changed!\n", thePath);
                }
                else {
                    printf("Contents unchanged for: %s\n", thePath);
                }
            }
        }

        matchingFileNames = NULL;
        free(fileNameBlock);
        closedir(dir);
    }

    if (!fwrite(pathInfoFile, pathInfoSize, sizeof(char), pathInfoFd)) {
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



    //

    



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
