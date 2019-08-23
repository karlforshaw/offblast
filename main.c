#define _GNU_SOURCE
#define SCALING 2.0
#define PHI 1.618033988749895

#define LARGE_FONT_SIZE 30
#define SMALL_FONT_SIZE 12
#define COLS_ON_SCREEN 5
#define COLS_TOTAL 10 


#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <json-c/json.h>
#include <murmurhash.h>
#include <curl/curl.h>
#include <math.h>

#include "offblast.h"
#include "offblastDbFile.h"


typedef struct SizeInfo {
        int32_t winWidth;
        int32_t winHeight;
        int32_t winFold;
        int32_t boxWidth;
        int32_t boxPad;
} SizeInfo;

typedef struct UiTile{
    struct LaunchTarget *target;
    struct UiTile *next; 
    struct UiTile *previous; 
    int32_t xPos;
} UiTile;

typedef struct UiRow {
    uint32_t length;
    struct UiTile *cursor;
    struct UiTile *tiles;
} UiRow;


typedef struct Animation {
    uint32_t animating;
    uint32_t direction;
    uint32_t startTick;
    uint32_t durationMs;
} Animation;


void changeColumn(Animation *theAnimation, uint32_t direction) {
    if (theAnimation->animating == 0) 
    {
        theAnimation->startTick = SDL_GetTicks();
        theAnimation->direction = direction;
        theAnimation->durationMs = 200;
        theAnimation->animating = 1;
    }
}

uint32_t needsReRender(SDL_Window *window, SizeInfo *sizeInfo);
double easeOutCirc(double t, double b, double c, double d);
double easeInOutCirc (double t, double b, double c, double d);
char *getCsvField(char *line, int fieldNo);

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
        // TODO create a config file too
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

/*
    CURL *curl = curl_easy_init();
    if (!curl) {
        printf("couldn't init curl\n");
        return 1;
    }
*/

    char *configFilePath;
    asprintf(&configFilePath, "%s/config.json", configPath);
    FILE *configFile = fopen(configFilePath, "r");

    if (configFile == NULL) {
        printf("Config file config.json is missing, exiting..\n");
        return 1;
    }

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

    json_object *configForOpenGameDb;
    json_object_object_get_ex(configObj, "opengamedb", 
            &configForOpenGameDb);

    assert(configForOpenGameDb);
    const char *openGameDbPath = 
        json_object_get_string(configForOpenGameDb);

    printf("Found OpenGameDb at %s\n", openGameDbPath);


    char *pathInfoDbPath;
    asprintf(&pathInfoDbPath, "%s/pathinfo.bin", configPath);
    struct OffblastDbFile pathDb = {0};
    if (!init_db_file(pathInfoDbPath, &pathDb, sizeof(PathInfo))) {
        printf("couldn't initialize path db, exiting\n");
        return 1;
    }
    PathInfoFile *pathInfoFile = (PathInfoFile*) pathDb.memory;
    free(pathInfoDbPath);

    char *launchTargetDbPath;
    asprintf(&launchTargetDbPath, "%s/launchtargets.bin", configPath);
    OffblastDbFile launchTargetDb = {0};
    if (!init_db_file(launchTargetDbPath, &launchTargetDb, 
                sizeof(LaunchTarget))) 
    {
        printf("couldn't initialize path db, exiting\n");
        return 1;
    }
    LaunchTargetFile *launchTargetFile = 
        (LaunchTargetFile*) launchTargetDb.memory;
    free(launchTargetDbPath);


#if 0
    // XXX DEBUG Dump out all launch targets
    for (int i = 0; i < launchTargetFile->nEntries; i++) {
        printf("Reading from local game db\n");
        printf("found game\t%d\t%u\n", 
                i, launchTargetFile->entries[i].targetSignature); 

        printf("%s\n", launchTargetFile->entries[i].name);
        printf("%s\n", launchTargetFile->entries[i].fileName);
        printf("%s\n", launchTargetFile->entries[i].path);
        printf("--\n\n");

    } // XXX DEBUG ONLY CODE
#endif 



    size_t nPaths = json_object_array_length(paths);
    for (int i=0; i<nPaths; i++) {

        json_object *workingPathNode = NULL;
        json_object *workingPathStringNode = NULL;
        json_object *workingPathExtensionNode = NULL;
        json_object *workingPathPlatformNode = NULL;

        const char *thePath = NULL;
        const char *theExtension = NULL;
        const char *thePlatform = NULL;

        workingPathNode = json_object_array_get_idx(paths, i);
        json_object_object_get_ex(workingPathNode, "path",
                &workingPathStringNode);
        json_object_object_get_ex(workingPathNode, "extension",
                &workingPathExtensionNode);
        json_object_object_get_ex(workingPathNode, "platform",
                &workingPathPlatformNode);

        thePath = json_object_get_string(workingPathStringNode);
        theExtension = json_object_get_string(workingPathExtensionNode);
        thePlatform = json_object_get_string(workingPathPlatformNode);

        printf("Running Path for %s: %s\n", theExtension, thePath);

        uint32_t platformScraped = 0;
        for(uint32_t i=0; i < launchTargetFile->nEntries; ++i) {
            if (strcmp(launchTargetFile->entries[i].platform, 
                        thePlatform) == 0) 
            {
                printf("%s already scraped.\n", thePlatform);
                platformScraped = 1;
                break;
            }
        }

        if (!platformScraped) {

            char *openGameDbPlatformPath;
            asprintf(&openGameDbPlatformPath, "%s/%s.csv", openGameDbPath, 
                    thePlatform);
            printf("Looking for file %s\n", openGameDbPlatformPath);

            FILE *openGameDbFile = fopen(openGameDbPlatformPath, "r");
            if (openGameDbFile == NULL) {
                printf("looks like theres no opengamedb for the platform\n");
                free(openGameDbPlatformPath);
                break;
            }
            free(openGameDbPlatformPath);

            char *csvLine = NULL;
            size_t csvLineLength = 0;
            size_t csvBytesRead = 0;
            uint32_t onRow = 0;

            while ((csvBytesRead = getline(
                            &csvLine, &csvLineLength, openGameDbFile)) != -1) 
            {
                if (onRow > 0) {

                    char *gameName = getCsvField(csvLine, 1);
                    char *gameSeed;

                    asprintf(&gameSeed, "%s_%s", thePlatform, gameName);

                    uint32_t targetSignature = 0;

                    lmmh_x86_32(gameSeed, strlen(gameSeed), 33, 
                            &targetSignature);

                    int32_t indexOfEntry = launchTargetIndexByTargetSignature(
                            launchTargetFile, targetSignature);

                    if (indexOfEntry == -1) {

                        char *gameDate = getCsvField(csvLine, 2);
                        printf("\n%s\n%u\n%s\n%s\n", gameSeed, 
                                targetSignature, 
                                gameName, 
                                gameDate);

                        LaunchTarget *newEntry = 
                            &launchTargetFile->entries[launchTargetFile->nEntries];
                        printf("writing new game to %p\n", newEntry);

                        newEntry->targetSignature = targetSignature;

                        memcpy(&newEntry->name, 
                                gameName, 
                                strlen(gameName));

                        memcpy(&newEntry->platform, 
                                thePlatform,
                                strlen(thePlatform));

                        // TODO check we have the space for it
                        launchTargetFile->nEntries++;

                        free(gameDate);

                    }
                    else {
                        printf("%d index found, We already have %u:%s\n", 
                                indexOfEntry,
                                targetSignature, 
                                gameSeed);
                    }

                    free(gameSeed);
                    free(gameName);
                }

                onRow++;
            }
            free(csvLine);
            fclose(openGameDbFile);
        }


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

        printf("got sig: signature:%u contentsHash:%u\n", pathSignature, 
                contentSignature);

        uint32_t rescrapeRequired = (pathInfoFile->nEntries == 0);

        // This goes through everything we have in the file now
        // We need something to detect whether it's in the file
        uint32_t isInFile = 0;
        for (uint32_t i=0; i < pathInfoFile->nEntries; i++) {
            if (pathInfoFile->entries[i].signature == pathSignature
                    && pathInfoFile->entries[i].contentsHash 
                    != contentSignature) 
            {
                printf("Contents of directory %s have changed!\n", thePath);
                isInFile =1;
                rescrapeRequired = 1;
                break;
            }
            else if (pathInfoFile->entries[i].signature == pathSignature)
            {
                printf("Contents unchanged for: %s\n", thePath);
                isInFile = 1;
                break;
            }
        }

        if (!isInFile) {
            printf("%s isn't in the db, adding..\n", thePath);
            // TODO do we have the allocation to add it?
            pathInfoFile->entries[pathInfoFile->nEntries].signature = 
                pathSignature;
            pathInfoFile->entries[pathInfoFile->nEntries].contentsHash = 
                contentSignature;
            pathInfoFile->nEntries++;

            rescrapeRequired = 1;
        }

        if (rescrapeRequired) {
            void *romData = calloc(1, ROM_PEEK_SIZE);

            for (uint32_t j=0;j<numItems; j++) {

                char *romPathTrimmed; 
                asprintf(&romPathTrimmed, "%s/%s", 
                        thePath,
                        matchingFileNames[j]);

                // TODO check it's not disc 2 or 3 etc

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
                fclose(romFd);

                // Now we have the signature we can add it to our DB
                int32_t indexOfEntry = launchTargetIndexByRomSignature(
                        launchTargetFile, romSignature);

                if (indexOfEntry > -1) {
                    printf("this target is already in the db\n");
                }
                else {

                    indexOfEntry = launchTargetIndexByNameMatch(
                            launchTargetFile, matchingFileNames[j]);

                    printf("found by name at index %d\n", indexOfEntry);

                    if (indexOfEntry > -1) {

                        LaunchTarget *theTarget = 
                            &launchTargetFile->entries[indexOfEntry];

                        theTarget->romSignature = romSignature;

                        memcpy(&theTarget->fileName, 
                                &matchingFileNames[j], 
                                strlen(matchingFileNames[j]));

                        memcpy(&theTarget->path, 
                                romPathTrimmed,
                                strlen(romPathTrimmed));
                    
                    }
                }

                free(romPathTrimmed);
            }; 

            free(romData);
        }

        matchingFileNames = NULL;
        free(fileNameBlock);
        closedir(dir);
    }

    close(pathDb.fd);
    close(launchTargetDb.fd);



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

    TTF_Font *font;
    font = TTF_OpenFont("fonts/Roboto-Regular.ttf", LARGE_FONT_SIZE*SCALING);
    if (!font) {
        printf("Font initialization Failed, %s\n", TTF_GetError());
        return 1;
    }

    TTF_Font *smallFont;
    smallFont = TTF_OpenFont("fonts/Roboto-Regular.ttf", SMALL_FONT_SIZE*SCALING);
    if (!smallFont) {
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

    SDL_Rect destRect = {LARGE_FONT_SIZE*SCALING, LARGE_FONT_SIZE*SCALING, 
        0, 0};
    SDL_QueryTexture(textTexture, NULL, NULL, &destRect.w, &destRect.h);

    int running = 1;
    uint32_t lastTick = SDL_GetTicks();
    uint32_t renderFrequency = 1000/60;

    uint32_t rows = 1;
    uint32_t yCursor = 0;
    
    Animation theAnimation = {};

    // Set up 
    UiRow mainRow = {};
    mainRow.length = 9;
    mainRow.tiles = calloc(mainRow.length, sizeof(UiTile));
    mainRow.cursor = &mainRow.tiles[0];

    SizeInfo sizeInfo = {};
    needsReRender(window, &sizeInfo);

    for (uint32_t i = 0; i < mainRow.length; i++) {

        mainRow.tiles[i].target = &launchTargetFile->entries[i];

        if (i+1 == mainRow.length) {
            mainRow.tiles[i].next = &mainRow.tiles[0]; 
        }
        else {
            mainRow.tiles[i].next = &mainRow.tiles[i+1]; 
        }

        if (i == 0) {
            mainRow.tiles[i].previous = &mainRow.tiles[mainRow.length -1];
        }
        else {
            mainRow.tiles[i].previous = &mainRow.tiles[i-1];
        }

    }


    while (running) {

        // TODO duplicating code here - consider making a funciton for this
        if (needsReRender(window, &sizeInfo) == 1) {
            // TODO something
        }

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
                else if (
                        keyEvent->keysym.scancode == SDL_SCANCODE_DOWN ||
                        keyEvent->keysym.scancode == SDL_SCANCODE_J) 
                {
                    yCursor++;
                    if (yCursor > rows) {
                        yCursor = rows;
                    }
                }
                else if (
                        keyEvent->keysym.scancode == SDL_SCANCODE_UP ||
                        keyEvent->keysym.scancode == SDL_SCANCODE_K) 
                {
                    yCursor--;
                    if (yCursor < 0) {
                        yCursor = 0;
                    }
                }
                else if (
                        keyEvent->keysym.scancode == SDL_SCANCODE_RIGHT ||
                        keyEvent->keysym.scancode == SDL_SCANCODE_L) 
                {
                    changeColumn(&theAnimation, 1);
                }
                else if (
                        keyEvent->keysym.scancode == SDL_SCANCODE_LEFT ||
                        keyEvent->keysym.scancode == SDL_SCANCODE_H) 
                {
                    changeColumn(&theAnimation, 0);
                }
                else {
                    printf("key up %d\n", keyEvent->keysym.scancode);
                }
            }

        }

        SDL_SetRenderDrawColor(renderer, 0xFD, 0xF9, 0xFA, 0xFF);
        SDL_RenderClear(renderer);


        SDL_SetRenderDrawColor(renderer, 0xFF, 0x00, 0x00, 0xFF);


        SDL_Rect mainRowRects[COLS_TOTAL];
        UiTile *tileToRender = mainRow.cursor;
        for (uint32_t i = 0; i < COLS_TOTAL; i++) {

            mainRowRects[i].x = 
                sizeInfo.boxPad + i * (sizeInfo.boxWidth + sizeInfo.boxPad);


            if (theAnimation.animating != 0) {
                double change = easeInOutCirc(
                        (double)SDL_GetTicks() - theAnimation.startTick,
                        0.0,
                        (double)sizeInfo.boxWidth + sizeInfo.boxPad,
                        (double)theAnimation.durationMs
                        );

                if (theAnimation.direction > 0) {
                    change = -change;
                }

                mainRowRects[i].x += change;
            }

            mainRowRects[i].y = sizeInfo.winFold;
            mainRowRects[i].w = sizeInfo.boxWidth;
            mainRowRects[i].h = 500;
            SDL_RenderFillRect(renderer, &mainRowRects[i]);

            LaunchTarget *theTarget = tileToRender->target;
            printf("%u: %s\n", i, theTarget->name);

            SDL_Color textColor = {0,255,0};
            SDL_Surface *targetSurface = TTF_RenderText_Blended(
                    smallFont,
                    theTarget->name,
                    textColor);

            if (!targetSurface) {
                printf("Font render failed, %s\n", TTF_GetError());
                return 1;
            }

            SDL_Texture* targetTexture = SDL_CreateTextureFromSurface(
                    renderer, targetSurface);

            SDL_FreeSurface(targetSurface);

            SDL_Rect targetRect = {
                mainRowRects[i].x,
                mainRowRects[i].y,
                0, 0};

            SDL_QueryTexture(targetTexture, NULL, NULL, &targetRect.w, &targetRect.h);
            SDL_RenderCopy(renderer, targetTexture, NULL, &targetRect);
            SDL_DestroyTexture(targetTexture);
            tileToRender = tileToRender->next;
        }


        if (theAnimation.animating && SDL_GetTicks() > 
                theAnimation.startTick + theAnimation.durationMs) 
        {
            theAnimation.animating = 0;

            if (theAnimation.direction == 1) {
                mainRow.cursor = mainRow.cursor->next;
            }
            else {
                mainRow.cursor = mainRow.cursor->previous;
            }
        }



        // DEBUG FPS INFO
        uint32_t frameTime = SDL_GetTicks() - lastTick;
        char *fpsString;
        asprintf(&fpsString, "Frame Time: %u", frameTime);

        SDL_Surface *fpsSurface = TTF_RenderText_Blended(
                smallFont,
                fpsString,
                textColor);

        free(fpsString);

        if (!fpsSurface) {
            printf("Font render failed, %s\n", TTF_GetError());
            return 1;
        }

        SDL_Texture* fpsTexture = SDL_CreateTextureFromSurface(
                renderer, fpsSurface);

        SDL_FreeSurface(fpsSurface);

        SDL_Rect fpsRect = {
            SMALL_FONT_SIZE*SCALING,
            SMALL_FONT_SIZE*SCALING,
            0, 0};

        SDL_QueryTexture(fpsTexture, NULL, NULL, &fpsRect.w, &fpsRect.h);
        SDL_RenderCopy(renderer, fpsTexture, NULL, &fpsRect);
        SDL_DestroyTexture(fpsTexture);
        destRect.x = (sizeInfo.winWidth / 2) - (destRect.w / 2);

        SDL_RenderCopy(renderer, textTexture, NULL, &destRect);

        SDL_RenderPresent(renderer);

        if (SDL_GetTicks() - lastTick < renderFrequency) {
            SDL_Delay(renderFrequency - (SDL_GetTicks() - lastTick));
        }

        lastTick = SDL_GetTicks();
    }

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

double easeOutCirc(double t, double b, double c, double d) 
{
	t /= d;
	t--;
	double change = c * sqrt(1.0f - t*t) + b;
    return change;
};


double easeInOutCirc (double t, double b, double c, double d) {
	t /= d/2.0;
	if (t < 1.0) return -c/2.0 * (sqrt(1.0 - t*t) - 1.0) + b;
	t -= 2.0;
	return c/2.0 * (sqrt(1.0 - t*t) + 1.0) + b;
};

char *getCsvField(char *line, int fieldNo) 
{
    char *cursor = line;
    char *fieldStart = NULL;
    char *fieldEnd = NULL;
    char *fieldString = NULL;
    int inQuotes = 0;

    for (uint32_t i = 0; i < fieldNo; ++i) {

        fieldStart = cursor;
        fieldEnd = cursor;
        inQuotes = 0;

        while (cursor != NULL) {

            if (*cursor == '"') {
                inQuotes++;
            }
            else if (*cursor == ',' && !(inQuotes & 1)) {
                fieldEnd = cursor - 1;
                cursor++;
                break;
            }

            cursor++;
        }
    }

    if (*fieldStart == '"') fieldStart++;
    if (*fieldEnd == '"') fieldEnd--;

    uint32_t fieldLength = (fieldEnd - fieldStart) + 1;

    fieldString = calloc(1, fieldLength + sizeof(char));
    memcpy(fieldString, fieldStart, fieldLength);

    return fieldString;
}

uint32_t needsReRender(SDL_Window *window, SizeInfo *sizeInfo) 
{
    int32_t newWidth, newHeight;
    uint32_t updated = 0;

    SDL_GetWindowSize(window, &newWidth, &newHeight);

    if (newWidth != sizeInfo->winWidth || newHeight != sizeInfo->winHeight) {
        printf("rerendering needed\n");
        sizeInfo->winWidth = newWidth;
        sizeInfo->winHeight= newHeight;
        sizeInfo->winFold = newHeight - (newHeight * 1/PHI);
        sizeInfo->boxWidth = newWidth / COLS_ON_SCREEN;

        sizeInfo->boxPad = sizeInfo->boxWidth - sizeInfo->boxWidth * 1/PHI;
        sizeInfo->boxPad = sizeInfo->boxPad * 1/PHI;
        sizeInfo->boxPad = sizeInfo->boxPad * 1/PHI;
        sizeInfo->boxPad = sizeInfo->boxPad * 1/PHI;

        updated = 1;
    }

    return updated;
}
