#define _GNU_SOURCE
#define PHI 1.618033988749895

#define COLS_ON_SCREEN 5
#define COLS_TOTAL 10 
#define ROWS_TOTAL 4

#define NAVIGATION_MOVE_DURATION 250 

// - TODO Put the row name on it's own animation that is only
//  triggered on row change
// TODO GRADIENT LAYERS
// TODO PLATFORM BADGES ON MIXED LISTS
// TODO GRANDIA IS BEING DETECTED AS "D" DETECT BETTER!

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

typedef struct UiTile{
    struct LaunchTarget *target;
    struct UiTile *next; 
    struct UiTile *previous; 
} UiTile;

typedef struct UiRow {
    uint32_t length;
    char *name;
    struct UiTile *tileCursor;
    struct UiTile *tiles;
    struct UiRow *nextRow;
    struct UiRow *previousRow;
} UiRow;

struct OffblastUi;
typedef struct Animation {
    uint32_t animating;
    uint32_t direction;
    uint32_t startTick;
    uint32_t durationMs;
    void *callbackArgs;
    void (* callback)(struct OffblastUi*);
} Animation;

typedef struct OffblastUi {
        int32_t winWidth;
        int32_t winHeight;
        int32_t winFold;
        int32_t winMargin;
        int32_t boxWidth;
        int32_t boxHeight;
        int32_t boxPad;
        int32_t descriptionWidth;
        int32_t descriptionHeight;
        double titlePointSize;
        double infoPointSize;

        TTF_Font *titleFont;
        TTF_Font *infoFont;
        TTF_Font *debugFont;

        SDL_Texture *titleTexture;
        SDL_Texture *infoTexture;
        SDL_Texture *descriptionTexture;
        SDL_Texture *rowNameTexture;
        SDL_Renderer *renderer;

        Animation *horizontalAnimation;
        Animation *verticalAnimation;
        Animation *infoAnimation;

        UiRow *rowCursor;
        UiRow *rows;
        LaunchTarget *movingToTarget;
        UiRow *movingToRow;
} OffblastUi;


uint32_t megabytes(uint32_t n);
uint32_t needsReRender(SDL_Window *window, OffblastUi *ui);
double easeOutCirc(double t, double b, double c, double d);
double easeInOutCirc (double t, double b, double c, double d);
char *getCsvField(char *line, int fieldNo);
double goldenRatioLarge(double in, uint32_t exponent);
void horizontalMoveDone(OffblastUi *ui);
void verticalMoveDone(OffblastUi *ui);
UiTile *rewindTiles(UiTile *fromTile, uint32_t depth);
void infoFaded(OffblastUi *ui);
uint32_t animationRunning(OffblastUi *ui);
void animationTick(Animation *theAnimation, OffblastUi *ui);

void changeRow(
        OffblastUi *ui,
        uint32_t direction);

void changeColumn(
        OffblastUi *ui,
        uint32_t direction);

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

    char *descriptionDbPath;
    asprintf(&descriptionDbPath, "%s/descriptions.bin", configPath);
    OffblastDbFile descriptionDb = {0};
    if (!init_db_file(descriptionDbPath, &descriptionDb, 
                sizeof(OffblastBlobFile) + 33333))
    {
        printf("couldn't initialize the descriptions file, exiting\n");
        return 1;
    }
    OffblastBlobFile *descriptionFile = 
        (OffblastBlobFile*) descriptionDb.memory;
    free(descriptionDbPath);


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
            openGameDbPlatformPath = NULL;

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
                        char *scoreString = getCsvField(csvLine, 3);
                        char *metaScoreString = getCsvField(csvLine, 4);
                        char *description = getCsvField(csvLine, 6);

                        printf("\n%s\n%u\n%s\n%s\ng: %s\n\nm: %s\n", 
                                gameSeed, 
                                targetSignature, 
                                gameName, 
                                gameDate,
                                scoreString, metaScoreString);

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

                        // TODO harden
                        if (strlen(gameDate) != 10) {
                            printf("INVALID DATE FORMAT\n");
                        }
                        else {
                            memcpy(&newEntry->date, gameDate, 10);
                        }

                        float score = -1;
                        if (strlen(scoreString) != 0) {
                            score = atof(scoreString) * 2 * 10;
                        }
                        if (strlen(metaScoreString) != 0) {
                            if (score == -1) {
                                score = atof(metaScoreString);
                            }
                            else {
                                score = (score + atof(metaScoreString)) / 2;
                            }
                        }

                        // XXX TODO check we have enough space to write
                        // the description into the file
                        OffblastBlob *newDescription = (OffblastBlob*) 
                            &descriptionFile->memory[descriptionFile->cursor];

                        newDescription->targetSignature = targetSignature;
                        newDescription->length = strlen(description);

                        memcpy(&newDescription->content, description, 
                                strlen(description));
                        *(newDescription->content + strlen(description)) = '\0';

                        newEntry->descriptionOffset = descriptionFile->cursor;
                        
                        descriptionFile->cursor += 
                            sizeof(OffblastBlob) + strlen(description) + 1;


                        // TODO round properly
                        newEntry->ranking = (uint32_t) score;

                        // TODO check we have the space for it
                        launchTargetFile->nEntries++;

                        free(gameDate);
                        free(scoreString);
                        free(metaScoreString);
                        free(description);

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
            //
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

    OffblastUi *ui = calloc(1, sizeof(OffblastUi));
    needsReRender(window, ui);

    ui->renderer = SDL_CreateRenderer(window, -1, 
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    ui->horizontalAnimation = calloc(1, sizeof(Animation));
    ui->verticalAnimation = calloc(1, sizeof(Animation));
    ui->infoAnimation = calloc(1, sizeof(Animation));

    int running = 1;
    uint32_t lastTick = SDL_GetTicks();
    uint32_t renderFrequency = 1000/60;

    // Init Ui
    // rows for now:
    // 1. Your Library
    // 2. Essential Playstation
    ui->rows = calloc(2, sizeof(UiRow));
    ui->rows[0].nextRow = &ui->rows[1];
    ui->rows[0].previousRow = &ui->rows[1];
    ui->rows[0].name = "Your Library";
    ui->rows[1].nextRow = &ui->rows[0];
    ui->rows[1].previousRow = &ui->rows[0];
    ui->rows[1].name = "Essential Playstation";
    ui->rowCursor = ui->rows;
    UiRow *rows = ui->rows;



    // PREP your library
    // walk through the targets and grab out anything that has 
    // a filepath
    // TODO put a limit on this
#define ROW_INDEX_LIBRARY 0
#define ROW_INDEX_TOP_RATED 1
    uint32_t libraryLength = 0;
    for (uint32_t i = 0; i < launchTargetFile->nEntries; i++) {
        LaunchTarget *target = &launchTargetFile->entries[i];
        if (strlen(target->fileName) != 0) 
            libraryLength++;
    }
    rows[ROW_INDEX_LIBRARY].length = libraryLength; 
    rows[ROW_INDEX_LIBRARY].tiles = calloc(libraryLength, sizeof(UiTile)); 
    assert(rows[ROW_INDEX_LIBRARY].tiles);
    rows[ROW_INDEX_LIBRARY].tileCursor = &rows[ROW_INDEX_LIBRARY].tiles[0];
    for (uint32_t i = 0, j = 0; i < launchTargetFile->nEntries; i++) {
        LaunchTarget *target = &launchTargetFile->entries[i];
        if (strlen(target->fileName) != 0) {
            rows[ROW_INDEX_LIBRARY].tiles[j].target = target;
            if (j+1 == libraryLength) {
                rows[ROW_INDEX_LIBRARY].tiles[j].next = 
                    &rows[ROW_INDEX_LIBRARY].tiles[0];
            }
            else {
                rows[ROW_INDEX_LIBRARY].tiles[j].next = 
                    &rows[ROW_INDEX_LIBRARY].tiles[j+1];
            }

            if (j==0) {
                rows[ROW_INDEX_LIBRARY].tiles[j].previous = 
                    &rows[ROW_INDEX_LIBRARY].tiles[libraryLength -1];
            }
            else {
                rows[ROW_INDEX_LIBRARY].tiles[j].previous 
                    = &rows[ROW_INDEX_LIBRARY].tiles[j-1];
            }
            j++;
        }
    }

    // PREP essential PS1
    uint32_t topRatedLength = 9;
    rows[ROW_INDEX_TOP_RATED].length = topRatedLength;
    rows[ROW_INDEX_TOP_RATED].tiles = calloc(topRatedLength, sizeof(UiTile));
    assert(rows[ROW_INDEX_TOP_RATED].tiles);
    rows[ROW_INDEX_TOP_RATED].tileCursor = &rows[ROW_INDEX_TOP_RATED].tiles[0];
    for (uint32_t i = 0; i < rows[ROW_INDEX_TOP_RATED].length; i++) {
        rows[ROW_INDEX_TOP_RATED].tiles[i].target = 
            &launchTargetFile->entries[i];

        if (i+1 == rows[ROW_INDEX_TOP_RATED].length) {
            rows[ROW_INDEX_TOP_RATED].tiles[i].next = 
                &rows[ROW_INDEX_TOP_RATED].tiles[0]; 
        }
        else {
            rows[ROW_INDEX_TOP_RATED].tiles[i].next = 
                &rows[ROW_INDEX_TOP_RATED].tiles[i+1]; 
        }

        if (i == 0) {
            rows[ROW_INDEX_TOP_RATED].tiles[i].previous = 
                &rows[ROW_INDEX_TOP_RATED].tiles[topRatedLength-1];
        }
        else {
            rows[ROW_INDEX_TOP_RATED].tiles[i].previous = 
                &rows[ROW_INDEX_TOP_RATED].tiles[i-1];
        }
    }

    ui->movingToTarget = ui->rowCursor->tileCursor->target;
    ui->movingToRow = ui->rowCursor;

    while (running) {

        if (needsReRender(window, ui) == 1) {
            printf("Window size changed, sizes updated.\n");
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
                    changeRow(ui, 1);
                }
                else if (
                        keyEvent->keysym.scancode == SDL_SCANCODE_UP ||
                        keyEvent->keysym.scancode == SDL_SCANCODE_K) 
                {
                    changeRow(ui, 0);
                }
                else if (
                        keyEvent->keysym.scancode == SDL_SCANCODE_RIGHT ||
                        keyEvent->keysym.scancode == SDL_SCANCODE_L) 
                {
                    changeColumn(ui, 1);
                }
                else if (
                        keyEvent->keysym.scancode == SDL_SCANCODE_LEFT ||
                        keyEvent->keysym.scancode == SDL_SCANCODE_H) 
                {
                    changeColumn(ui, 0);
                }
                else {
                    printf("key up %d\n", keyEvent->keysym.scancode);
                }
            }

        }

        SDL_SetRenderDrawColor(ui->renderer, 0x03, 0x03, 0x03, 0xFF);
        SDL_RenderClear(ui->renderer);
        


        // Blocks
        SDL_SetRenderDrawColor(ui->renderer, 0xFF, 0xFF, 0xFF, 0x66);
        UiRow *rowToRender = ui->rowCursor->previousRow;

        for (int32_t iRow = -1; iRow < ROWS_TOTAL-1; iRow++) {

            uint8_t shade = 255;
            SDL_SetRenderDrawColor(ui->renderer, shade, shade, shade, 0x66);

            SDL_Rect rowRects[COLS_TOTAL];

            UiTile *tileToRender = 
                rewindTiles(rowToRender->tileCursor, 2);

            for (int32_t iTile = -2; iTile < COLS_TOTAL; iTile++) {

                rowRects[iTile].x = 
                    ui->winMargin + iTile * (ui->boxWidth + ui->boxPad);

                if (ui->horizontalAnimation->animating != 0 && iRow == 0) 
                {
                    double change = easeInOutCirc(
                            (double)SDL_GetTicks() - ui->horizontalAnimation->startTick,
                            0.0,
                            (double)ui->boxWidth + ui->boxPad,
                            (double)ui->horizontalAnimation->durationMs);

                    if (ui->horizontalAnimation->direction > 0) {
                        change = -change;
                    }

                    rowRects[iTile].x += change;

                }

                rowRects[iTile].y = 
                    ui->winFold + (iRow * (ui->boxHeight + ui->boxPad));

                if (ui->verticalAnimation->animating != 0) 
                {
                    double change = easeInOutCirc(
                            (double)SDL_GetTicks() - ui->verticalAnimation->startTick,
                            0.0,
                            (double)ui->boxHeight+ ui->boxPad,
                            (double)ui->verticalAnimation->durationMs);

                    if (ui->verticalAnimation->direction > 0) {
                        change = -change;
                    }

                    rowRects[iTile].y += change;

                }

                rowRects[iTile].w = ui->boxWidth;
                rowRects[iTile].h = ui->boxHeight;
                SDL_RenderFillRect(ui->renderer, &rowRects[iTile]);
                tileToRender = tileToRender->next;
            }

            rowToRender = rowToRender->nextRow;
        }

        SDL_Rect infoLayer = {
            0, 0,
            ui->winWidth,
            ui->winFold
        };
        SDL_SetRenderDrawColor(ui->renderer, 0x00, 0x00, 0x00, 0xFF);
        SDL_RenderFillRect(ui->renderer, &infoLayer);

        // Target Info 
        if (ui->titleTexture == NULL) {
            SDL_Color titleColor = {255,255,255,255};
            SDL_Surface *titleSurface = TTF_RenderText_Blended(
                    ui->titleFont, ui->movingToTarget->name, titleColor);

            if (!titleSurface) {
                printf("Font render failed, %s\n", TTF_GetError());
                return 1;
            }

            ui->titleTexture = 
                SDL_CreateTextureFromSurface(ui->renderer, titleSurface);
            SDL_FreeSurface(titleSurface);
        }

        if (ui->infoTexture == NULL) {

            SDL_Color color = {220,220,220,255};

            char *tempString;
            asprintf(&tempString, "%.4s  |  %s  |  %u%%", 
                    ui->movingToTarget->date, 
                    ui->movingToTarget->platform,
                    ui->movingToTarget->ranking);

            SDL_Surface *infoSurface = TTF_RenderText_Blended(
                    ui->infoFont, tempString, color);
            free(tempString);

            if (!infoSurface) {
                printf("Font render failed, %s\n", TTF_GetError());
                return 1;
            }

            ui->infoTexture = 
                SDL_CreateTextureFromSurface(ui->renderer, infoSurface);
            SDL_FreeSurface(infoSurface);
    
        }

        if (ui->descriptionTexture == NULL) {

            SDL_Color color = {220,220,220,255};

            OffblastBlob *descriptionBlob = (OffblastBlob*)
                &descriptionFile->memory[ui->movingToTarget->descriptionOffset];

            SDL_Surface *surface = TTF_RenderText_Blended_Wrapped(
                    ui->infoFont, 
                    descriptionBlob->content,
                    color,
                    ui->descriptionWidth);

            if (!surface) {
                printf("Font render failed, %s\n", TTF_GetError());
                return 1;
            }

            ui->descriptionTexture = 
                SDL_CreateTextureFromSurface(ui->renderer, surface);
            SDL_FreeSurface(surface);
        }

        if (ui->rowNameTexture == NULL) {
            SDL_Color color = {255,255,255,255};
            SDL_Surface *surface = TTF_RenderText_Blended(
                    ui->infoFont, ui->movingToRow->name, color);

            if (!surface) {
                printf("Font render failed, %s\n", TTF_GetError());
                return 1;
            }

            ui->rowNameTexture = 
                SDL_CreateTextureFromSurface(ui->renderer, surface);
            SDL_FreeSurface(surface);
        }



        if (ui->infoAnimation->animating == 1) {
            uint8_t change = easeInOutCirc(
                        (double)SDL_GetTicks() - ui->infoAnimation->startTick,
                        1.0,
                        255.0,
                        (double)ui->infoAnimation->durationMs);

            if (ui->infoAnimation->direction == 0) {
                change = 256 - change;
            }
            else {
                if (change == 0) change = 255;
            }

            SDL_SetTextureAlphaMod(ui->titleTexture, change);
            SDL_SetTextureAlphaMod(ui->infoTexture, change);
            SDL_SetTextureAlphaMod(ui->descriptionTexture, change);
            SDL_SetTextureAlphaMod(ui->rowNameTexture, change);
        }
        else {
            SDL_SetTextureAlphaMod(ui->titleTexture, 255);
            SDL_SetTextureAlphaMod(ui->infoTexture, 255);
            SDL_SetTextureAlphaMod(ui->descriptionTexture, 255);
            SDL_SetTextureAlphaMod(ui->rowNameTexture, 255);
        }

        SDL_Rect titleRect = {0, 0, 0, 0}; 
        SDL_QueryTexture(ui->titleTexture, NULL, NULL, 
                &titleRect.w, &titleRect.h);

        SDL_Rect infoRect = {0, 0, 0, 0}; 
        SDL_QueryTexture(ui->infoTexture, NULL, NULL, 
                &infoRect.w, &infoRect.h);

        SDL_Rect descRect = {0, 0, 0, 0}; 
        SDL_QueryTexture(ui->descriptionTexture, NULL, NULL, 
                &descRect.w, &descRect.h);

        SDL_Rect rowNameRect = {0, 0, 0, 0}; 
        SDL_QueryTexture(ui->rowNameTexture, NULL, NULL, 
                &rowNameRect.w, &rowNameRect.h);

        titleRect.x = ui->winMargin;
        infoRect.x = ui->winMargin;
        descRect.x = ui->winMargin;
        rowNameRect.x = ui->winMargin;

        titleRect.y = goldenRatioLarge((double) ui->winHeight, 5);
        infoRect.y = (titleRect.y + 
            ui->titlePointSize + 
            goldenRatioLarge((double) ui->titlePointSize, 2));
        descRect.y = (infoRect.y + 
            ui->infoPointSize + 
            goldenRatioLarge((double) ui->infoPointSize, 2));
        rowNameRect.y = ui->winFold - ui->infoPointSize - ui->boxPad;

        SDL_RenderCopy(ui->renderer, ui->titleTexture, NULL, &titleRect);
        SDL_RenderCopy(ui->renderer, ui->infoTexture, NULL, &infoRect);
        SDL_RenderCopy(ui->renderer, ui->descriptionTexture, NULL, &descRect);
        SDL_RenderCopy(ui->renderer, ui->rowNameTexture, NULL, &rowNameRect);

        animationTick(ui->horizontalAnimation, ui);
        animationTick(ui->verticalAnimation, ui);
        animationTick(ui->infoAnimation, ui);


        // DEBUG FPS INFO
        uint32_t frameTime = SDL_GetTicks() - lastTick;
        char *fpsString;
        asprintf(&fpsString, "Frame Time: %u", frameTime);
        SDL_Color fpsColor = {255,255,255,255};

        SDL_Surface *fpsSurface = TTF_RenderText_Solid(
                ui->debugFont,
                fpsString,
                fpsColor);

        free(fpsString);

        if (!fpsSurface) {
            printf("Font render failed, %s\n", TTF_GetError());
            return 1;
        }

        SDL_Texture* fpsTexture = SDL_CreateTextureFromSurface(
                ui->renderer, fpsSurface);

        SDL_FreeSurface(fpsSurface);

        SDL_Rect fpsRect = {
            goldenRatioLarge(ui->winWidth, 9),
            goldenRatioLarge(ui->winHeight, 9),
            0, 0};

        SDL_QueryTexture(fpsTexture, NULL, NULL, &fpsRect.w, &fpsRect.h);
        SDL_RenderCopy(ui->renderer, fpsTexture, NULL, &fpsRect);
        SDL_DestroyTexture(fpsTexture);


        SDL_RenderPresent(ui->renderer);

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

uint32_t needsReRender(SDL_Window *window, OffblastUi *ui) 
{
    int32_t newWidth, newHeight;
    uint32_t updated = 0;

    SDL_GetWindowSize(window, &newWidth, &newHeight);

    if (newWidth != ui->winWidth || newHeight != ui->winHeight) {

        ui->winWidth = newWidth;
        ui->winHeight= newHeight;
        ui->winFold = newHeight * 0.5;
        ui->winMargin = goldenRatioLarge((double) newWidth, 5);

        ui->boxWidth = newWidth / COLS_ON_SCREEN;
        ui->boxHeight = goldenRatioLarge(ui->winWidth, 4);
        ui->boxPad = goldenRatioLarge((double) ui->winWidth, 9);

        ui->descriptionWidth = 
            goldenRatioLarge((double) newWidth, 1) - ui->winMargin;

        // TODO Find a better way to enfoce this
        ui->descriptionHeight = goldenRatioLarge(ui->winWidth, 3);

        ui->titlePointSize = goldenRatioLarge(ui->winWidth, 7);
        ui->titleFont = TTF_OpenFont(
                "fonts/Roboto-Regular.ttf", ui->titlePointSize);
        if (!ui->titleFont) {
            printf("Title font initialization Failed, %s\n", TTF_GetError());
            return 1;
        }

        ui->infoPointSize = goldenRatioLarge(ui->winWidth, 9);
        ui->infoFont = TTF_OpenFont(
                "fonts/Roboto-Regular.ttf", ui->infoPointSize);

        if (!ui->infoFont) {
            printf("Font initialization Failed, %s\n", TTF_GetError());
            return 1;
        }

        ui->debugFont = TTF_OpenFont(
                "fonts/Roboto-Regular.ttf", goldenRatioLarge(ui->winWidth, 11));

        if (!ui->debugFont) {
            printf("Font initialization Failed, %s\n", TTF_GetError());
            return 1;
        }

        SDL_DestroyTexture(ui->infoTexture);
        ui->infoTexture = NULL;
        SDL_DestroyTexture(ui->titleTexture);
        ui->titleTexture = NULL;
        SDL_DestroyTexture(ui->descriptionTexture);
        ui->descriptionTexture = NULL;
        SDL_DestroyTexture(ui->rowNameTexture);
        ui->rowNameTexture = NULL;

        updated = 1;
    }

    return updated;
}


void changeColumn(
        OffblastUi *ui,
        uint32_t direction) 
{
    if (animationRunning(ui) == 0)
    {
        ui->horizontalAnimation->startTick = SDL_GetTicks();
        ui->horizontalAnimation->direction = direction;
        ui->horizontalAnimation->durationMs = NAVIGATION_MOVE_DURATION;
        ui->horizontalAnimation->animating = 1;
        ui->horizontalAnimation->callback = &horizontalMoveDone;

        ui->infoAnimation->startTick = SDL_GetTicks();
        ui->infoAnimation->direction = 0;
        ui->infoAnimation->durationMs = NAVIGATION_MOVE_DURATION / 2;
        ui->infoAnimation->animating = 1;
        ui->infoAnimation->callback = &infoFaded;

        if (direction == 0) {
            ui->movingToTarget = 
                ui->rowCursor->tileCursor->previous->target;
        }
        else {
            ui->movingToTarget 
                = ui->rowCursor->tileCursor->next->target;
        }
    }
}

void changeRow(
        OffblastUi *ui,
        uint32_t direction) 
{
    if (animationRunning(ui) == 0)
    {
        ui->verticalAnimation->startTick = SDL_GetTicks();
        ui->verticalAnimation->direction = direction;
        ui->verticalAnimation->durationMs = NAVIGATION_MOVE_DURATION;
        ui->verticalAnimation->animating = 1;
        ui->verticalAnimation->callback = &verticalMoveDone;

        ui->infoAnimation->startTick = SDL_GetTicks();
        ui->infoAnimation->direction = 0;
        ui->infoAnimation->durationMs = NAVIGATION_MOVE_DURATION / 2;
        ui->infoAnimation->animating = 1;
        ui->infoAnimation->callback = &infoFaded;

        if (direction == 0) {
            ui->movingToRow = ui->rowCursor->previousRow;
            ui->movingToTarget = 
                ui->rowCursor->previousRow->tileCursor->target;
        }
        else {
            ui->movingToRow = ui->rowCursor->nextRow;
            ui->movingToTarget = 
                ui->rowCursor->nextRow->tileCursor->target;
        }
    }
}

void startVerticalAnimation(
        Animation *verticalAnimation,
        Animation *titleAnimation,
        uint32_t direction)
{
}

UiTile *rewindTiles(UiTile *fromTile, uint32_t depth) {
    if (depth == 0) {
        return fromTile;
    }
    else {
        fromTile = fromTile->previous;
        return rewindTiles(fromTile, --depth);
    }
}


double goldenRatioLarge(double in, uint32_t exponent) {
    if (exponent == 0) {
        return in;
    }
    else {
        return goldenRatioLarge(1/PHI * in, --exponent); 
    }
}

void horizontalMoveDone(OffblastUi *ui) {
    if (ui->horizontalAnimation->direction == 1) {
        ui->rowCursor->tileCursor = 
            ui->rowCursor->tileCursor->next;
    }
    else {
        ui->rowCursor->tileCursor = 
            ui->rowCursor->tileCursor->previous;
    }
}

void verticalMoveDone(OffblastUi *ui) {
    if (ui->verticalAnimation->direction == 1) {
        ui->rowCursor = 
            ui->rowCursor->nextRow;
    }
    else {
        ui->rowCursor = 
            ui->rowCursor->previousRow;
    }
}

void infoFaded(OffblastUi *ui) {

    if (ui->infoAnimation->direction == 0) {

        SDL_DestroyTexture(ui->titleTexture);
        SDL_DestroyTexture(ui->infoTexture);
        SDL_DestroyTexture(ui->descriptionTexture);
        SDL_DestroyTexture(ui->rowNameTexture);

        ui->titleTexture = NULL;
        ui->infoTexture = NULL;
        ui->descriptionTexture = NULL;
        ui->rowNameTexture = NULL;

        ui->infoAnimation->startTick = SDL_GetTicks();
        ui->infoAnimation->direction = 1;
        ui->infoAnimation->durationMs = NAVIGATION_MOVE_DURATION / 2;
        ui->infoAnimation->animating = 1;
        ui->infoAnimation->callback = &infoFaded;
    }
    else {
        ui->infoAnimation->animating = 0;
    }
}


uint32_t megabytes(uint32_t n) {
    return n * 1024 * 1024;
}

uint32_t animationRunning(OffblastUi *ui) {
    uint32_t result = 0;

    if (ui->horizontalAnimation->animating != 0) {
        result++;
    }
    else if (ui->verticalAnimation->animating != 0) {
        result++;
    }
    else if (ui->infoAnimation->animating != 0) {
        result++;
    }

    return result;
}

void animationTick(Animation *theAnimation, OffblastUi *ui) {
        if (theAnimation->animating && SDL_GetTicks() > 
                theAnimation->startTick + theAnimation->durationMs) 
        {
            theAnimation->animating = 0;
            theAnimation->callback(ui);
        }
}
