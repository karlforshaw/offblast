#define _GNU_SOURCE
#define PHI 1.618033988749895

#define COLS_ON_SCREEN 5
#define COLS_TOTAL 10 
#define ROWS_TOTAL 4
#define MAX_LAUNCH_COMMAND_LENGTH 512
#define MAX_PLATFORMS 50 

#define LOAD_STATE_UNKNOWN 0
#define LOAD_STATE_DOWNLOADING 1
#define LOAD_STATE_DOWNLOADED 2
#define LOAD_STATE_LOADED 3

#define OFFBLAST_NOWRAP 0

#define NAVIGATION_MOVE_DURATION 250 

// TODO GRADIENT LAYERS
//      - move to an opengl renderer
//      - Ok, I can render text now, so let's see if I can get the title
//          and info layers on the screen
//      * would certainly pretty things up
//      * would be cool if we could do it on the BG too
//
// TODO COVER ART 
//      * a loading animation 
//
// TODO PLATFORM BADGES ON MIXED LISTS
// TODO GRANDIA IS BEING DETECTED AS "D" DETECT BETTER!
//
// Known Bugs:
//      - Invalid date format is a thing
//      - Only JPG covers are supported
//      - Returning to offblast on i3 - window not being full screen
//      * if you add a rom after the platform has been scraped we say we already
//          have it in the db but this is the target, not the filepath etc

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
#include <SDL2/SDL_image.h>
#include <json-c/json.h>
#include <murmurhash.h>
#include <curl/curl.h>
#include <math.h>
#include <pthread.h>
        
#define GL3_PROTOTYPES 1
#include <GL/glew.h>

#include "offblast.h"
#include "offblastDbFile.h"

typedef struct UiTile{
    struct LaunchTarget *target;
    SDL_Texture *texture;
    uint8_t loadState;
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

typedef float UiRect[6][6]; 

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

    GLuint fpsVbo;
    GLuint titleVbo;
    GLuint infoVbo;
    GLuint descriptionVbo;
    GLuint rowNameVbo;

    UiRect *fpsVertices;
    UiRect *titleVertices;
    UiRect *infoVertices;
    UiRect *descriptionVertices;
    UiRect *rowNameVertices;

    GLuint titleTexture;
    GLuint infoTexture;
    GLuint descriptionTexture;
    GLuint rowNameTexture;

    uint32_t titleTextureInvalid;
    uint32_t infoTextureInvalid;
    uint32_t descriptionTextureInvalid;
    uint32_t rowNameTextureInvalid;

    SDL_Renderer *renderer;

    Animation *horizontalAnimation;
    Animation *verticalAnimation;
    Animation *infoAnimation;
    Animation *rowNameAnimation;

    uint32_t numRows;
    UiRow *rowCursor;
    UiRow *rows;
    LaunchTarget *movingToTarget;
    UiRow *movingToRow;
} OffblastUi;

typedef struct Launcher {
    char path[PATH_MAX];
    char launcher[MAX_LAUNCH_COMMAND_LENGTH];
} Launcher;


uint32_t megabytes(uint32_t n);
uint32_t powTwoFloor(uint32_t val);
uint32_t needsReRender(SDL_Window *window, OffblastUi *ui);
double easeOutCirc(double t, double b, double c, double d);
double easeInOutCirc (double t, double b, double c, double d);
char *getCsvField(char *line, int fieldNo);
double goldenRatioLarge(double in, uint32_t exponent);
void horizontalMoveDone(OffblastUi *ui);
void verticalMoveDone(OffblastUi *ui);
UiTile *rewindTiles(UiTile *fromTile, uint32_t depth);
void infoFaded(OffblastUi *ui);
void rowNameFaded(OffblastUi *ui);
uint32_t animationRunning(OffblastUi *ui);
void animationTick(Animation *theAnimation, OffblastUi *ui);
const char *platformString(char *key);
void *downloadCover(void *arg);
char *getCoverPath();
UiRect *createRect(uint32_t winWidth, uint32_t winHeight, 
        uint32_t rectWidth, uint32_t rectHeight);

void generateTextLayer(
        OffblastUi *ui, GLuint texture, GLuint *vbo, UiRect **vertices, 
        TTF_Font *font, char *text, uint32_t wrapWidth, 
        uint32_t updateVertices);

void generateTextLayer(
        OffblastUi *ui, GLuint texture, GLuint *vbo, UiRect **vertices, 
        TTF_Font *font, char *text, uint32_t wrapWidth, 
        uint32_t updateVertices) 
{
    SDL_Color color = {255,255,255,255};
    SDL_Surface *surface = TTF_RenderText_Blended(font, text, color);

    if (!surface) {
        printf("Text render failed, %s\n", TTF_GetError());
        return;
    }

    uint32_t newWidth = powTwoFloor(surface->w);
    uint32_t newHeight = powTwoFloor(surface->h);

    SDL_Surface *newSurface = SDL_CreateRGBSurface(
            0, newWidth, newHeight, 32,
            0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);

    SDL_BlitSurface(surface, NULL, newSurface, NULL);

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, newWidth, newHeight,
            0, GL_BGRA, GL_UNSIGNED_BYTE, newSurface->pixels);

    SDL_FreeSurface(surface);
    SDL_FreeSurface(newSurface);

    if (updateVertices) {

        if (*vertices != NULL) free(*vertices);
        *vertices = createRect(ui->winWidth, ui->winHeight, 
                newWidth, newHeight);

        if (*vbo == 0) {
            glGenBuffers(1, vbo);
            glBindBuffer(GL_ARRAY_BUFFER, *vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(UiRect), 
                    *vertices, GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        else {
            glBindBuffer(GL_ARRAY_BUFFER, *vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(UiRect), 
                    *vertices);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
    }
}

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

    char *coverPath;
    asprintf(&coverPath, "%s/covers/", configPath);

    int madeConfigDir;
    madeConfigDir = mkdir(configPath, S_IRWXU);
    madeConfigDir = mkdir(coverPath, S_IRWXU);
    
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

    char (*platforms)[256] = calloc(MAX_PLATFORMS, 256 * sizeof(char));
    uint32_t nPlatforms = 0;

    size_t nPaths = json_object_array_length(paths);
    Launcher *launchers = calloc(nPaths, sizeof(Launcher));

    for (int i=0; i<nPaths; i++) {

        json_object *workingPathNode = NULL;
        json_object *workingPathStringNode = NULL;
        json_object *workingPathExtensionNode = NULL;
        json_object *workingPathPlatformNode = NULL;
        json_object *workingPathLauncherNode = NULL;

        const char *thePath = NULL;
        const char *theExtension = NULL;
        const char *thePlatform = NULL;
        const char *theLauncher = NULL;

        workingPathNode = json_object_array_get_idx(paths, i);
        json_object_object_get_ex(workingPathNode, "path",
                &workingPathStringNode);
        json_object_object_get_ex(workingPathNode, "extension",
                &workingPathExtensionNode);
        json_object_object_get_ex(workingPathNode, "platform",
                &workingPathPlatformNode);

        json_object_object_get_ex(workingPathNode, "launcher",
                &workingPathLauncherNode);

        thePath = json_object_get_string(workingPathStringNode);
        theExtension = json_object_get_string(workingPathExtensionNode);
        thePlatform = json_object_get_string(workingPathPlatformNode);

        theLauncher = json_object_get_string(workingPathLauncherNode);
        memcpy(&launchers[i].path, thePath, strlen(thePath));
        memcpy(&launchers[i].launcher, theLauncher, strlen(theLauncher));

        printf("Running Path for %s: %s\n", theExtension, thePath);

        if (i == 0) {
            memcpy(platforms[nPlatforms], thePlatform, strlen(thePlatform));
            nPlatforms++;
        }
        else {
            uint8_t gotPlatform = 0;
            for (uint32_t i = 0; i < nPlatforms; i++) {
                if (strcmp(platforms[i], thePlatform) == 0) gotPlatform = 1;
            }
            if (!gotPlatform) {
                memcpy(platforms[nPlatforms], thePlatform, strlen(thePlatform));
                nPlatforms++;
            }
        }

        uint32_t platformScraped = 0;
        for (uint32_t i=0; i < launchTargetFile->nEntries; ++i) {
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
                        char *coverArtUrl = getCsvField(csvLine, 7);

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

                        memcpy(&newEntry->coverUrl, 
                                coverArtUrl,
                                strlen(coverArtUrl));

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
                        free(coverArtUrl);

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

                for (uint32_t i = 0; i < ROM_PEEK_SIZE; i++) {
                    if (!fread(romData + i, sizeof(char), 1, romFd)) {
                        if (i == 0) {
                            printf("cannot read from rom %s\n",
                                    romPathTrimmed);
                            continue;
                        }
                    }
                }

                lmmh_x86_32(romData, ROM_PEEK_SIZE, 33, &romSignature);
                memset(romData, 0x0, ROM_PEEK_SIZE);
                printf("signature is %u\n", romSignature);

                memset(romData, 0x0, ROM_PEEK_SIZE);
                fclose(romFd);

                int32_t indexOfEntry = launchTargetIndexByRomSignature(
                        launchTargetFile, romSignature);

                if (indexOfEntry > -1) {
                    printf("target is already in the db\n");
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

    printf("DEBUG - got %u platforms\n", nPlatforms);

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
    //SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 
            //SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* window = SDL_CreateWindow("OffBlast", 
            SDL_WINDOWPOS_UNDEFINED, 
            SDL_WINDOWPOS_UNDEFINED,
            640,
            480,
            SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP | 
                SDL_WINDOW_ALLOW_HIGHDPI);

    if (window == NULL) {
        printf("SDL window creation failed, exiting..\n");
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    glewInit();
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    OffblastUi *ui = calloc(1, sizeof(OffblastUi));
    needsReRender(window, ui);

    ui->horizontalAnimation = calloc(1, sizeof(Animation));
    ui->verticalAnimation = calloc(1, sizeof(Animation));
    ui->infoAnimation = calloc(1, sizeof(Animation));
    ui->rowNameAnimation = calloc(1, sizeof(Animation));

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    const char *vertexShaderStr = 
        "#version 330\n"
        "layout(location = 0) in vec4 position;\n"
        "layout(location = 1) in vec2 aTexcoord;\n"
        "out vec2 TexCoord;\n"
        "void main()\n"
        "{\n"
        "   gl_Position = position;\n"
        "   TexCoord = aTexcoord;\n"
        "}\n";

    const char *fragmentShaderStr = 
        "#version 330\n"
        "in vec2 TexCoord;\n"
        "out vec4 outputColor;\n"
        "uniform sampler2D ourTexture;\n"
        "void main()\n"
        "{\n"
        "   //outputColor = vec4(1.0f, 1.0f, 1.0f, 1.0f);\n"
        "   outputColor = texture(ourTexture, TexCoord);\n"
        "}\n";

    GLint compStatus = GL_FALSE; 
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderStr, NULL);
    glCompileShader(vertexShader);
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &compStatus);
    printf("Vertex Shader Compilation: %d\n", compStatus);
    if (!compStatus) {
        GLint len;
        glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, 
                &len);
        char *logString = calloc(1, len+1);
        glGetShaderInfoLog(vertexShader, len, NULL, logString);
        printf("%s\n", logString);
    }
    assert(compStatus);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderStr, NULL);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &compStatus);
    printf("Fragment Shader Compilation: %d\n", compStatus);
    assert(compStatus);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint programStatus;
    glGetProgramiv(program, GL_LINK_STATUS, &programStatus);
    printf("GL Program Status: %d\n", programStatus);
    assert(programStatus);


    glDetachShader(program, vertexShader);
    glDetachShader(program, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLuint fpsTexture;
    glGenTextures(1, &fpsTexture);
    glGenTextures(1, &ui->titleTexture);
    glGenTextures(1, &ui->infoTexture);
    glGenTextures(1, &ui->descriptionTexture);
    glGenTextures(1, &ui->rowNameTexture);


    GLint texUni = glGetUniformLocation(program, "ourTexture");
    printf("uniform is %u\n", texUni);

    int running = 1;
    uint32_t lastTick = SDL_GetTicks();
    uint32_t renderFrequency = 1000/60;

    // Init Ui
    // rows for now:
    // 1. Your Library
    // 2. Essential *platform" 
    ui->rows = calloc(1 + nPlatforms, sizeof(UiRow));
    ui->numRows = 0;
    ui->rowCursor = ui->rows;

    // __ROW__ "Your Library"
    // TODO put a limit on this
    uint32_t libraryLength = 0;
    for (uint32_t i = 0; i < launchTargetFile->nEntries; i++) {
        LaunchTarget *target = &launchTargetFile->entries[i];
        if (strlen(target->fileName) != 0) 
            libraryLength++;
    }

    if (libraryLength > 0) {
        ui->rows[ui->numRows].length = libraryLength; 
        ui->rows[ui->numRows].tiles = calloc(libraryLength, sizeof(UiTile)); 
        assert(ui->rows[ui->numRows].tiles);
        ui->rows[ui->numRows].tileCursor = &ui->rows[ui->numRows].tiles[0];
        for (uint32_t i = 0, j = 0; i < launchTargetFile->nEntries; i++) {
            LaunchTarget *target = &launchTargetFile->entries[i];
            if (strlen(target->fileName) != 0) {
                ui->rows[ui->numRows].tiles[j].target = target;
                if (j+1 == libraryLength) {
                    ui->rows[ui->numRows].tiles[j].next = 
                        &ui->rows[ui->numRows].tiles[0];
                }
                else {
                    ui->rows[ui->numRows].tiles[j].next = 
                        &ui->rows[ui->numRows].tiles[j+1];
                }

                if (j==0) {
                    ui->rows[ui->numRows].tiles[j].previous = 
                        &ui->rows[ui->numRows].tiles[libraryLength -1];
                }
                else {
                    ui->rows[ui->numRows].tiles[j].previous 
                        = &ui->rows[ui->numRows].tiles[j-1];
                }
                j++;
            }
        }
        ui->rows[ui->numRows].name = "Your Library";
        ui->numRows++;
    }
    else { 
        printf("woah now looks like we have an empty library\n");
    }


    // __ROWS__ Essentials per platform
    for (uint32_t iPlatform = 0; iPlatform < nPlatforms; iPlatform++) {

        asprintf(&ui->rows[ui->numRows].name, "Essential %s", 
                platformString(platforms[iPlatform]));

        uint32_t topRatedLength = 9;
        ui->rows[ui->numRows].length = topRatedLength;
        ui->rows[ui->numRows].tiles = calloc(topRatedLength, sizeof(UiTile));
        assert(ui->rows[ui->numRows].tiles);
        ui->rows[ui->numRows].tileCursor = &ui->rows[ui->numRows].tiles[0];
        for (uint32_t i = 0; i < ui->rows[ui->numRows].length; i++) {
            ui->rows[ui->numRows].tiles[i].target = 
                &launchTargetFile->entries[i];

            if (i+1 == ui->rows[ui->numRows].length) {
                ui->rows[ui->numRows].tiles[i].next = 
                    &ui->rows[ui->numRows].tiles[0]; 
            }
            else {
                ui->rows[ui->numRows].tiles[i].next = 
                    &ui->rows[ui->numRows].tiles[i+1]; 
            }

            if (i == 0) {
                ui->rows[ui->numRows].tiles[i].previous = 
                    &ui->rows[ui->numRows].tiles[topRatedLength-1];
            }
            else {
                ui->rows[ui->numRows].tiles[i].previous = 
                    &ui->rows[ui->numRows].tiles[i-1];
            }
        }

        ui->numRows++;
    }

    for (uint32_t i = 0; i < ui->numRows; i++) {
        if (i == 0) {
            ui->rows[i].previousRow = &ui->rows[ui->numRows-1];
        }
        else {
            ui->rows[i].previousRow = &ui->rows[i-1];
        }

        if (i == ui->numRows - 1) {
            ui->rows[i].nextRow = &ui->rows[0];
        }
        else {
            ui->rows[i].nextRow = &ui->rows[i+1];
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
                if (keyEvent->keysym.scancode == SDL_SCANCODE_RETURN) {

                    LaunchTarget *target = ui->rowCursor->tileCursor->target;

                    if (strlen(target->path) == 0 || 
                            strlen(target->fileName) == 0) 
                    {
                        printf("%s has no launch candidate\n", target->name);
                    }
                    else {

                        char *romSlug;
                        asprintf(&romSlug, "%s", (char*) &target->path);

                        char *launchString = calloc(PATH_MAX, sizeof(char));
                        
                        for (uint32_t i = 0; i < nPaths; i++) {
                            if (strcmp(target->path, launchers[i].path))
                                memcpy( launchString, 
                                        launchers[i].launcher, 
                                        strlen(launchers[i].launcher));
                        }
                        assert(strlen(launchString));

                        char *p;
                        uint8_t replaceIter = 0, replaceLimit = 8;
                        while ((p = strstr(launchString, "%ROM%"))) {
                            memmove(
                                    p + strlen(romSlug) + 2, 
                                    p + 5,
                                    strlen(p+5));
                            *p = '"';
                            memcpy(p+1, romSlug, strlen(romSlug));
                            *(p + 1 + strlen(romSlug)) = '"';

                            replaceIter++;
                            if (replaceIter >= replaceLimit) {
                                printf("rom replace iterations exceeded, breaking\n");
                                break;
                            }
                        }

                        system(launchString);
                        free(romSlug);
                        free(launchString);
                    }

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

        // XXX SDL RENDER
        //SDL_SetRenderDrawColor(ui->renderer, 0x03, 0x03, 0x03, 0xFF);
        //SDL_RenderClear(ui->renderer);


        // Blocks
#if 0
        SDL_SetRenderDrawColor(ui->renderer, 0xFF, 0xFF, 0xFF, 0x66);
        UiRow *rowToRender = ui->rowCursor->previousRow;

        for (int32_t iRow = -1; iRow < ROWS_TOTAL-1; iRow++) {

            uint8_t shade = 255;
            SDL_SetRenderDrawColor(ui->renderer, shade, shade, shade, 0x66);

            UiTile *tileToRender = 
                rewindTiles(rowToRender->tileCursor, 2);

            for (int32_t iTile = -2; iTile < COLS_TOTAL; iTile++) {

                SDL_Rect theRect = {};

                theRect.x = 
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

                    theRect.x += change;

                }

                theRect.y = 
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

                    theRect.y += change;

                }

                theRect.w = ui->boxWidth;
                theRect.h = ui->boxHeight;

                if (tileToRender->texture == NULL &&
                        tileToRender->target->coverUrl != NULL) 
                {
                    if (tileToRender->loadState != LOAD_STATE_LOADED) {

                        char *coverArtPath =
                            getCoverPath(tileToRender->target->targetSignature); 
                        SDL_Surface *image = IMG_Load(coverArtPath);
                        free(coverArtPath);

                        if (tileToRender->loadState == LOAD_STATE_UNKNOWN) {
                            
                            if(!image) {
                                tileToRender->loadState = 
                                    LOAD_STATE_DOWNLOADING;
                                pthread_t theThread;
                                pthread_create(
                                        &theThread, 
                                        NULL, 
                                        downloadCover, 
                                        (void*)tileToRender);
                            }
                            else {
                                tileToRender->loadState = 
                                    LOAD_STATE_DOWNLOADED;
                            }
                        }
                        
                        if (tileToRender->loadState == LOAD_STATE_DOWNLOADED) {
                            tileToRender->texture = 
                                SDL_CreateTextureFromSurface(ui->renderer, 
                                        image);

                            tileToRender->loadState = LOAD_STATE_LOADED;
                        }

                    }

                    SDL_RenderFillRect(ui->renderer, &theRect);
                }
                else {
                    SDL_Rect srcRect = {};
                    SDL_QueryTexture(tileToRender->texture, NULL, NULL, 
                            &srcRect.w, &srcRect.h);

                    // clip the height for the aspect ratio
                    srcRect.h = srcRect.w / 7 * 5;

                    SDL_RenderCopy(ui->renderer, tileToRender->texture, 
                            &srcRect, &theRect);
                }

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
#endif


        // XXX info area
        if (ui->titleTextureInvalid) {
            generateTextLayer(
                    ui, ui->titleTexture, &ui->titleVbo,  &ui->titleVertices, 
                    ui->titleFont, ui->movingToTarget->name, OFFBLAST_NOWRAP,
                    1);
            ui->titleTextureInvalid = 0;
        }

        if (ui->infoTextureInvalid) {
            char *infoString;
            asprintf(&infoString, "%.4s  |  %s  |  %u%%", 
                    ui->movingToTarget->date, 
                    platformString(ui->movingToTarget->platform),
                    ui->movingToTarget->ranking);

            generateTextLayer(
                    ui, ui->infoTexture, &ui->infoVbo, &ui->infoVertices, 
                    ui->infoFont, infoString, OFFBLAST_NOWRAP, 1);
            ui->infoTextureInvalid = 0;

            free(infoString);
        }

        if (ui->descriptionTextureInvalid) {
            OffblastBlob *descriptionBlob = (OffblastBlob*)
                &descriptionFile->memory[ui->movingToTarget->descriptionOffset];

            generateTextLayer(
                    ui, ui->descriptionTexture, &ui->descriptionVbo, 
                    &ui->descriptionVertices, ui->infoFont, 
                    descriptionBlob->content, ui->descriptionWidth, 1);
            ui->descriptionTextureInvalid = 0;
        }

        if (ui->rowNameTextureInvalid) {
            generateTextLayer(
                    ui, ui->rowNameTexture, &ui->rowNameVbo, 
                    &ui->rowNameVertices, ui->infoFont, ui->movingToRow->name, 
                    OFFBLAST_NOWRAP, 1);
            ui->rowNameTextureInvalid = 0;
        }


        float alpha = 1.0;
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

            alpha = 1/(float)change;
        }

        float rowNameAlpha = 1;
        if (ui->rowNameAnimation->animating == 1) {
            uint8_t change = easeInOutCirc(
                        (double)SDL_GetTicks() - ui->rowNameAnimation->startTick,
                        1.0,
                        255.0,
                        (double)ui->rowNameAnimation->durationMs);

            if (ui->rowNameAnimation->direction == 0) {
                change = 256 - change;
            }
            else {
                if (change == 0) change = 255;
            }

            rowNameAlpha = 1/(float)change;
        }

        glUseProgram(program);

        glBindTexture(GL_TEXTURE_2D, ui->titleTexture);
        glBindBuffer(GL_ARRAY_BUFFER, ui->titleVbo);

        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 6*sizeof(float), 0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6*sizeof(float), 
                (void*)(4*sizeof(float)));

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glUseProgram(0);

        // TODO the offset for all of these is ui->winMargin
        // TODO translate all of these 
        /*
        titleRect.y = goldenRatioLarge((double) ui->winHeight, 5);
        infoRect.y = (titleRect.y + 
            ui->titlePointSize + 
            goldenRatioLarge((double) ui->titlePointSize, 2));
        descRect.y = (infoRect.y + 
            ui->infoPointSize + 
            goldenRatioLarge((double) ui->infoPointSize, 2));
        rowNameRect.y = ui->winFold - ui->infoPointSize - ui->boxPad;
        */

        animationTick(ui->horizontalAnimation, ui);
        animationTick(ui->verticalAnimation, ui);
        animationTick(ui->infoAnimation, ui);
        animationTick(ui->rowNameAnimation, ui);

        // DEBUG FPS INFO
        // TODO use generate text layer?
        uint32_t frameTime = SDL_GetTicks() - lastTick;
        char *fpsString;
        asprintf(&fpsString, "frame time: %u", frameTime);
        SDL_Color fpsColor = {255,255,255,255};

        SDL_Surface *fpsSurface = TTF_RenderText_Blended(
                ui->debugFont,
                fpsString,
                fpsColor);

        free(fpsString);

        if (!fpsSurface) {
            printf("FPS Font render failed, %s\n", TTF_GetError());
            return 1;
        }

        uint32_t newWidth = powTwoFloor(fpsSurface->w);
        uint32_t newHeight = powTwoFloor(fpsSurface->h);

        SDL_Surface *newSurface = SDL_CreateRGBSurface(
                0, newWidth, newHeight, 32,
                0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);

        SDL_BlitSurface(fpsSurface, NULL, newSurface, NULL);

        glBindTexture(GL_TEXTURE_2D, fpsTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, newWidth, newHeight,
                0, GL_BGRA, GL_UNSIGNED_BYTE, newSurface->pixels);

        SDL_FreeSurface(fpsSurface);
        SDL_FreeSurface(newSurface);


        glClearColor(0.0, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program);
        glBindBuffer(GL_ARRAY_BUFFER, ui->fpsVbo);

        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);

        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 6*sizeof(float), 0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6*sizeof(float), 
                (void*)(4*sizeof(float)));

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glUseProgram(0);




    
        SDL_GL_SwapWindow(window);

        if (SDL_GetTicks() - lastTick < renderFrequency) {
            SDL_Delay(renderFrequency - (SDL_GetTicks() - lastTick));
        }

        lastTick = SDL_GetTicks();
    }

    SDL_GL_DeleteContext(glContext);
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
            else if ((*cursor == ',' && !(inQuotes & 1)) ||
                    *cursor == '\r' || 
                    *cursor == '\n' || 
                    *cursor == '\0') 
            {
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
        glViewport(0, 0, (GLsizei)newWidth, (GLsizei)newHeight);
        ui->winFold = newHeight * 0.5;
        ui->winMargin = goldenRatioLarge((double) newWidth, 5);

        // 7:5
        ui->boxHeight = goldenRatioLarge(ui->winWidth, 4);
        ui->boxWidth = ui->boxHeight/5 * 7;

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
                "fonts/Roboto-Regular.ttf", ui->infoPointSize);

        if (!ui->debugFont) {
            printf("Font initialization Failed, %s\n", TTF_GetError());
            return 1;
        }

        // Create fps layer TODO does this need to move to wherever the
        // ones that generate layers for the other text is?
        // use generateTextLayer
        // we should instead invalidate it every frame, but only update the
        // vertex array when the window size has changed
        int debugSampleW = 0, debugSampleH = 0;
        TTF_SizeText(ui->debugFont, "frame time: xxx", 
                &debugSampleW, &debugSampleH);
        debugSampleW = powTwoFloor(debugSampleW);
        debugSampleH = powTwoFloor(debugSampleH);
        ui->fpsVertices = createRect(ui->winWidth, ui->winHeight, 
                debugSampleW, debugSampleH);

        if (ui->fpsVbo == 0) {
            glGenBuffers(1, &ui->fpsVbo);
            glBindBuffer(GL_ARRAY_BUFFER, ui->fpsVbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(UiRect), 
                    ui->fpsVertices, GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        else {
            glBindBuffer(GL_ARRAY_BUFFER, ui->fpsVbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(UiRect), 
                    ui->fpsVertices);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        ui->infoTextureInvalid = 1;
        ui->titleTextureInvalid = 1;
        ui->descriptionTextureInvalid = 1;
        ui->rowNameTextureInvalid = 1;

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

        ui->rowNameAnimation->startTick = SDL_GetTicks();
        ui->rowNameAnimation->direction = 0;
        ui->rowNameAnimation->durationMs = NAVIGATION_MOVE_DURATION / 2;
        ui->rowNameAnimation->animating = 1;
        ui->rowNameAnimation->callback = &rowNameFaded;

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

        ui->titleTextureInvalid = 1;
        ui->infoTextureInvalid = 1;
        ui->descriptionTextureInvalid = 1;
        ui->rowNameTextureInvalid = 1;

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

void rowNameFaded(OffblastUi *ui) {
    if (ui->rowNameAnimation->direction == 0) {

        ui->rowNameTextureInvalid = 1;

        ui->rowNameAnimation->startTick = SDL_GetTicks();
        ui->rowNameAnimation->direction = 1;
        ui->rowNameAnimation->durationMs = NAVIGATION_MOVE_DURATION / 2;
        ui->rowNameAnimation->animating = 1;
        ui->rowNameAnimation->callback = &rowNameFaded;
    }
    else {
        ui->rowNameAnimation->animating = 0;
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

const char *platformString(char *key) {
    if (strcmp(key, "32x") == 0) {
        return "Sega 32X";
    }
    else if (strcmp(key, "arcade") == 0) {
        return "Arcade";
    }
    else if (strcmp(key, "atari_2600") == 0) {
        return "Atari 2600";
    }
    else if (strcmp(key, "atari_5200") == 0) {
        return "Atari 5200";
    }
    else if (strcmp(key, "atari_7800") == 0) {
        return "Atari 7800";
    }
    else if (strcmp(key, "atari_8-bit_family") == 0) {
        return "Atari 8-Bit Family";
    }
    else if (strcmp(key, "dreamcast") == 0) {
        return "Sega Dreamcast";
    }
    else if (strcmp(key, "game_boy_advance") == 0) {
        return "Game Boy Advance";
    }
    else if (strcmp(key, "game_boy_color") == 0) {
        return "Game Boy Color";
    }
    else if (strcmp(key, "game_boy") == 0) {
        return "Game Boy";
    }
    else if (strcmp(key, "gamecube") == 0) {
        return "Gamecube";
    }
    else if (strcmp(key, "game_gear") == 0) {
        return "Game Gear";
    }
    else if (strcmp(key, "master_system") == 0) {
        return "Master System";
    }
    else if (strcmp(key, "mega_drive") == 0) {
        return "Mega Drive";
    }
    else if (strcmp(key, "nintendo_64") == 0) {
        return "Nintendo 64";
    }
    else if (strcmp(key, "nintendo_ds") == 0) {
        return "Nintendo DS";
    }
    else if (strcmp(key, "nintendo_entertainment_system") == 0) {
        return "NES";
    }
    else if (strcmp(key, "pc") == 0) {
        return "PC";
    }
    else if (strcmp(key, "playstation_3") == 0) {
        return "Playstation 3";
    }
    else if (strcmp(key, "playstation_2") == 0) {
        return "Playstation 2";
    }
    else if (strcmp(key, "playstation") == 0) {
        return "Playstation";
    }
    else if (strcmp(key, "playstation_portable") == 0) {
        return "Playstation Portable";
    }
    else if (strcmp(key, "sega_cd") == 0) {
        return "Sega CD";
    }
    else if (strcmp(key, "sega_saturn") == 0) {
        return "Saturn";
    }
    else if (strcmp(key, "super_nintendo_entertainment_system") == 0) {
        return "SNES";
    }
    else if (strcmp(key, "turbografx-16") == 0) {
        return "TurboGrafx-16";
    }
    else if (strcmp(key, "wii") == 0) {
        return "Wii";
    }
    else if (strcmp(key, "wii_u") == 0) {
        return "Wii-U";
    }

    return "Unknown Platform";
}


char *getCoverPath(uint32_t signature) {

    char *homePath = getenv("HOME");
    assert(homePath);

    char *coverArtPath;
    asprintf(&coverArtPath, "%s/.offblast/covers/%u.jpg", homePath, signature); 

    return coverArtPath;
}


void *downloadCover(void *arg) {

    UiTile* tileToRender = (UiTile *)arg;
    char *coverArtPath = getCoverPath(tileToRender->target->targetSignature); 
    FILE *fd = fopen(coverArtPath, "wb");
    free(coverArtPath);

    if (!fd) {
        printf("Can't open file for write\n");
    }
    else {

        curl_global_init(CURL_GLOBAL_DEFAULT);
        CURL *curl = curl_easy_init();
        if (!curl) {
            printf("CURL init fail.\n");
            return NULL;
        }
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

        char *url = (char *) 
            tileToRender->target->coverUrl;

        printf("Downloading Art for %s\n", 
                tileToRender->target->name);

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fd);

        uint32_t res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            printf("%s\n", curl_easy_strerror(res));
        }

        curl_easy_cleanup(curl);
        fclose(fd);

        tileToRender->loadState = LOAD_STATE_DOWNLOADED;
    }

    return NULL;
}

UiRect *createRect(uint32_t winWidth, uint32_t winHeight, 
        uint32_t rectWidth, uint32_t rectHeight) 
{
    float left = -1.0;
    float right = left + (1/(float)winWidth*rectWidth);
    float bottom = -1.0;
    float top = bottom + (1/(float)winHeight*rectHeight);

    UiRect *rect = (UiRect*) calloc(1, sizeof(UiRect));

    (*rect)[0][0] = left;
    (*rect)[0][1] = bottom;
    (*rect)[0][2] = 0.0f;
    (*rect)[0][3] = 1.0f;
    (*rect)[0][4] = 0.0f;
    (*rect)[0][5] = 1.0f;

    (*rect)[1][0] = left;
    (*rect)[1][1] = top;
    (*rect)[1][2] = 0.0f;
    (*rect)[1][3] = 1.0f;
    (*rect)[1][4] = 0.0f;
    (*rect)[1][5] = 0.0f;

    (*rect)[2][0] = right;
    (*rect)[2][1] = top;
    (*rect)[2][2] = 0.0f;
    (*rect)[2][3] = 1.0f;
    (*rect)[2][4] = 1.0f;
    (*rect)[2][5] = 0.0f;

    (*rect)[3][0] = right;
    (*rect)[3][1] = top;
    (*rect)[3][2] = 0.0f;
    (*rect)[3][3] = 1.0f;
    (*rect)[3][4] = 1.0f;
    (*rect)[3][5] = 0.0f;

    (*rect)[4][0] = right;
    (*rect)[4][1] = bottom;
    (*rect)[4][2] = 0.0f;
    (*rect)[4][3] = 1.0f;
    (*rect)[4][4] = 1.0f;
    (*rect)[4][5] = 1.0f;

    (*rect)[5][0] = left;
    (*rect)[5][1] = bottom;
    (*rect)[5][2] = 0.0f;
    (*rect)[5][3] = 1.0f;
    (*rect)[5][4] = 0.0f;
    (*rect)[5][5] = 1.0f;

    return rect;
}

uint32_t powTwoFloor(uint32_t val) {
    uint32_t pow = 2;
    while (val > pow)
        pow *= 2;

    return pow;
}
