#define _GNU_SOURCE

#define PHI 1.618033988749895
#define MAX_PLATFORMS 50 
#define OFFBLAST_MAX_SEARCH 64


#define OFFBLAST_TEXT_TITLE 1
#define OFFBLAST_TEXT_INFO 2
#define OFFBLAST_TEXT_DEBUG 3

#define NAVIGATION_MOVE_DURATION 250 

#define WINDOW_MANAGER_I3 1
#define WINDOW_MANAGER_GNOME 2

// Alpha 0.5 
//
//      - Download queue
//          We need some way of communicating back that the link is dead.
//          Instead of just constantly trying to download a junk url.
//
//          multiple downloader threads,
//
//          multiple loader threads (based on how many cores),
//
//          Make sure you clear the loading queue before changing the tileset
//
//      - consider importing everything on first load, this will mean if 
//          you have a shared playtime file you won't get unknown games
//          popping up in your lists..
//
//      -. watch out for vram! glDeleteTextures
//          We could move to a tile store object which has a fixed array of
//          tiles (enough to fill 1.5 screens on both sides) each tile has a 
//          last on screen tick and when we need to load new textures we evict
//          the oldest before loading the new texture
//
//      - better aniations that support incremental jumps if you input a command
//          during a running animation
//
//      - Deadzone checks
//         http://www.lazyfoo.net/tutorials/SDL/19_gamepads_and_joysticks/index.php
//
//      BUGS
//       - Image loading has a bug somewhere, if you mess around enough with 
//          search, everything goes out of the window
//
//       - games with poor match scores should probably be logged to the missing
//          games log, we're allowing bad matches to go through as long as the 
//          proper game isn't present
//
// TODO 
//      - OpenGameDb, auto download/update? Evict Assets and update.
//
// TODO Better config slugs, I'd love to have player specific slugs so that
//  anything in the player section could be included in the launcher entries like
//  %PLAYER_FOO%
//
// TODO Hours played in info
//
// TODO steam support
//  When a game is removed offblast still thinks it's playable
//
// TODO List caches, I think when we generate lists we should cache
//      them in files.. maybe?
//
// TODO Collections, this is more of an opengamedb ticket but It would be
//      cool to feature collections from youtubers such as metal jesus.
//

#include <stdio.h>
#include <signal.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <json-c/json.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <murmurhash.h>
#include <curl/curl.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xmu/WinUtil.h>
        
#define GL3_PROTOTYPES 1
#include <GL/glew.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION 1
#include "stb_image_write.h"

#include "offblast.h"
#include "offblastDbFile.h"


typedef struct User {
    char name[256];
    char email[512];
    char avatarPath[PATH_MAX];
    char cemuAccount[32];
    char retroarchConfig[PATH_MAX];
    char savePath[PATH_MAX];
    char dolphinCardPath[PATH_MAX];
} User;

typedef struct Player {
    int32_t jsIndex;
    SDL_GameController *usingController; 
    char *name; 
    uint8_t emailHash;
    User *user;
} Player;

#define IMAGE_STATE_COLD 0
#define IMAGE_STATE_LOADING 1
#define IMAGE_STATE_READY 2
#define IMAGE_STATE_COMPLETE 3
typedef struct Image {
    uint8_t state;
    GLuint textureHandle;
    uint32_t width;
    uint32_t height;
    unsigned char *atlas;
} Image;

typedef struct RomFound {
    char path[256];
    char name[OFFBLAST_NAME_MAX];
    char id[OFFBLAST_NAME_MAX];
} RomFound;

typedef struct RomFoundList {
    uint32_t allocated;
    uint32_t numItems;
    RomFound *items;
} RomFoundList;

typedef struct UiTile{
    struct LaunchTarget *target;
    Image image;
    struct UiTile *next; 
    struct UiTile *previous; 
    int32_t baseX;
} UiTile;

typedef struct UiRow {
    uint32_t length;
    char name[256];
    struct UiTile *tileCursor;
    struct UiTile *tiles;
    struct UiRow *nextRow;
    struct UiRow *previousRow;
    struct UiTile *movingToTile;
} UiRow;


typedef struct Color {
    float r, g, b, a;
} Color;

typedef struct Vertex {
    float x;
    float y;
    float z;
    float s;
    float tx;
    float ty;
    Color color;
} Vertex;

typedef struct Quad {
    Vertex vertices[6];
} Quad;

struct OffblastUi;
typedef struct Animation {
    uint32_t animating;
    uint32_t direction;
    uint32_t startTick;
    uint32_t durationMs;
    void *callbackArgs;
    void (* callback)();
} Animation;

enum UiMode {
    OFFBLAST_UI_MODE_MAIN = 1,
    OFFBLAST_UI_MODE_PLAYER_SELECT = 2,
    OFFBLAST_UI_MODE_BACKGROUND = 3
};

typedef struct MenuItem {
    char *label;
    void (*callback)();
    void *callbackArgs;
} MenuItem;

typedef struct PlayerSelectUi {
    Image *images;
    int32_t cursor;

    uint32_t totalWidth;
    float *widthForAvatar;
    float *xOffsetForAvatar;

} PlayerSelectUi;

typedef struct UiRowset {
    UiRow *rows;
    UiRow *rowCursor;
    LaunchTarget *movingToTarget;
    UiRow *movingToRow;
    uint32_t numRows;
} UiRowset;

typedef struct MainUi {

    int32_t descriptionWidth;
    int32_t descriptionHeight;
    int32_t boxHeight;
    int32_t boxPad;

    int32_t showMenu;
    MenuItem *menuItems;
    uint32_t numMenuItems;
    uint32_t menuCursor;

    int32_t showSearch;

    Animation *horizontalAnimation;
    Animation *verticalAnimation;
    Animation *infoAnimation;
    Animation *rowNameAnimation;

    GLuint imageVbo;

    UiRowset *activeRowset;

    UiRowset *homeRowset;
    UiRowset *searchRowset;
    UiRowset *filteredRowset;

    uint32_t rowGeometryInvalid;

    char *titleText;
    char *infoText;
    char *descriptionText;

    char *rowNameText;

} MainUi ;

typedef struct LauncherContentsHash {
    uint32_t launcherSignature;
    uint32_t contentSignature;
} LauncherContentsHash;

typedef struct LauncherContentsFile {
    uint32_t length;
    LauncherContentsHash *entries;
} LauncherContentsFile;

#define DOWNLOAD_STATE_READY 0
#define DOWNLOAD_STATE_COLD 1
#define DOWNLOAD_QUEUE_LENGTH 33
typedef struct DownloadItem {
    char url[PATH_MAX];
    uint64_t targetSignature;
    uint32_t status;
} DownloadItem;

typedef struct DownloadQueue {
    DownloadItem items[DOWNLOAD_QUEUE_LENGTH];
} DownloadQueue;

typedef struct DownloaderContext {
    DownloadQueue queue;
    pthread_mutex_t lock;
} DownloaderContext;

#define LOAD_STATE_FREE 0
#define LOAD_STATE_LOADING 1
#define LOAD_STATE_READY 2
#define LOAD_QUEUE_LENGTH 33
typedef struct LoadItem {
    UiTile *tile;
    uint32_t status;
} LoadItem;

typedef struct LoadQueue {
    LoadItem items[LOAD_QUEUE_LENGTH];
} LoadQueue;


typedef struct LoaderContext {
    LoadQueue queue;
    pthread_mutex_t lock;
} LoaderContext;

typedef struct OffblastUi {

    uint32_t running;
    uint32_t shutdownFlag;
    uint32_t loadingFlag;
    enum UiMode mode;
    char *configPath;
    char *playtimePath;
    Display *XDisplay;

    PlayerSelectUi playerSelectUi;
    MainUi mainUi;

    int32_t joyX;
    int32_t joyY;

    char searchTerm[OFFBLAST_MAX_SEARCH];
    uint32_t searchCursor;
    char searchCurChar; 

    int32_t winWidth;
    int32_t winHeight;
    int32_t winFold;
    int32_t winMargin;

    double titlePointSize;
    double infoPointSize;
    double debugPointSize;

    GLuint titleTextTexture;
    GLuint infoTextTexture;
    GLuint debugTextTexture;

    Image missingCoverImage;

    GLuint textVbo;

    stbtt_bakedchar titleCharData[96];
    stbtt_bakedchar infoCharData[96];
    stbtt_bakedchar debugCharData[96];

    uint32_t textBitmapHeight;
    uint32_t textBitmapWidth;

    GLuint imageProgram;
    GLint imageTranslateUni;
    GLint imageAlphaUni;
    GLint imageDesaturateUni;

    GLuint gradientProgram;
    GLuint gradientVbo;
    GLint gradientColorStartUniform; 
    GLint gradientColorEndUniform; 

    GLuint textProgram;
    GLint textAlphaUni;

    Player player;

    size_t nUsers;
    User *users;

    char (*platforms)[256];
    uint32_t nPlatforms;

    OffblastBlobFile *descriptionFile;
    OffblastDbFile playTimeDb;
    PlayTimeFile *playTimeFile;
    LaunchTargetFile *launchTargetFile;

    uint32_t nLaunchers;
    Launcher *launchers;

    LauncherContentsFile launcherContentsCache;

    SDL_Window *window;
    uint32_t windowManager;
    Window resumeWindow;

    pid_t runningPid;
    LaunchTarget *playingTarget;
    uint32_t startPlayTick;

    uint32_t uiStopButtonHot;

    DownloaderContext downloader;
    LoaderContext imageLoader;

} OffblastUi;

typedef struct CurlFetch {
    size_t size;
    unsigned char *data;
} CurlFetch;

typedef struct WindowInfo {
    Display *display;
    Window window;
} WindowInfo;



uint32_t megabytes(uint32_t n);
uint32_t powTwoFloor(uint32_t val);
uint32_t needsReRender(SDL_Window *window);
double easeOutCirc(double t, double b, double c, double d);
double easeInOutCirc (double t, double b, double c, double d);
char *getCsvField(char *line, int fieldNo);
double goldenRatioLarge(double in, uint32_t exponent);
float goldenRatioLargef(float in, uint32_t exponent);
void horizontalMoveDone();
void verticalMoveDone();
void infoFaded();
void rowNameFaded();
uint32_t animationRunning();
void animationTick(Animation *theAnimation);
const char *platformString(char *key);
char *getCoverPath(LaunchTarget *);
GLint loadShaderFile(const char *path, GLenum shaderType);
GLuint createShaderProgram(GLint vertShader, GLint fragShader);
void launch();
void imageToGlTexture(GLuint *textureHandle, unsigned char *pixelData, 
        uint32_t newWidth, uint32_t newHeight);
void changeRow(uint32_t direction);
void changeColumn(uint32_t direction);
void pressConfirm();
void jumpScreen(uint32_t direction);
void pressCancel();
void pressGuide();
void updateResults();
void updateHomeLists();
void updateInfoText();
void updateDescriptionText();
void updateGameInfo();
void initQuad(Quad* quad);
size_t curlWrite(void *contents, size_t size, size_t nmemb, void *userP);
int playTimeSort(const void *a, const void *b);
int lastPlayedSort(const void *a, const void *b);
uint32_t getTextLineWidth(char *string, stbtt_bakedchar* cdata);
void renderText(OffblastUi *offblast, float x, float y, 
        uint32_t textMode, float alpha, uint32_t lineMaxW, char *string);
void initQuad(Quad* quad);
void resizeQuad(float x, float y, float w, float h, Quad *quad);
void renderGradient(float x, float y, float w, float h, 
        uint32_t horizontal, Color colorStart, Color colorEnd);
float getWidthForScaledImage(float scaledHeight, Image *image);
void renderImage(float x, float y, float w, float h, Image* image,
        float desaturation, float alpha);
void loadTexture(UiTile *tile);
void pressSearch();
void loadPlaytimeFile();
Window getActiveWindowRaw();
void raiseWindow();
void killRunningGame(); 
void importFromCemu(Launcher *theLauncher);
void importFromSteam(Launcher *theLauncher);
void importFromScummvm(Launcher *theLauncher);
void importFromCustom(Launcher *theLauncher);
void importFromRPCS3(Launcher *theLauncher);
WindowInfo getOffblastWindowInfo();
uint32_t activeWindowIsOffblast();
uint32_t launcherContentsCacheUpdated(uint32_t launcherSignature, 
        uint32_t newContentsHash);
void logMissingGame(char *missingGamePath);
void calculateRowGeometry(UiRow *row);

void *downloadMain(void *arg); 
void *imageLoadMain(void *arg); 

OffblastUi *offblast;

void openPlayerSelect() {
    offblast->mode = OFFBLAST_UI_MODE_PLAYER_SELECT;
}
void setExit() {
    offblast->running = 0;
};
void shutdownMachine() {
    offblast->shutdownFlag = 1;
    offblast->running = 0;
};
void doSearch() {
    offblast->mainUi.activeRowset = offblast->mainUi.searchRowset;
    offblast->mainUi.rowGeometryInvalid = 1;
    offblast->mainUi.showSearch = 1;
}
void doHome() {
    offblast->mainUi.activeRowset = offblast->mainUi.homeRowset;
    offblast->mainUi.rowGeometryInvalid = 1;
    updateGameInfo();
}


int main(int argc, char** argv) {

    printf("\nStarting up OffBlast with %d args.\n\n", argc);
    offblast = calloc(1, sizeof(OffblastUi));


    char *homePath = getenv("HOME");
    assert(homePath);

    char *configPath;
    asprintf(&configPath, "%s/.offblast", homePath);
    offblast->configPath = configPath;

    char *coverPath;
    asprintf(&coverPath, "%s/covers/", configPath);

    int madeConfigDir;
    madeConfigDir = mkdir(configPath, S_IRWXU);
    madeConfigDir = mkdir(coverPath, S_IRWXU);

    free(coverPath);
    
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
    free(configFilePath);

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

    json_object *configLaunchers = NULL;
    json_object_object_get_ex(configObj, "launchers", &configLaunchers);
    assert(configLaunchers);

    json_object *configForOpenGameDb;
    json_object_object_get_ex(configObj, "opengamedb", 
            &configForOpenGameDb);

    assert(configForOpenGameDb);
    const char *openGameDbPath = 
        json_object_get_string(configForOpenGameDb);

    printf("Found OpenGameDb at %s\n", openGameDbPath);

    json_object *configForPlaytimePath;
    json_object_object_get_ex(configObj, "playtime_path", 
            &configForPlaytimePath);

    if (configForPlaytimePath) {
        offblast->playtimePath = 
            (char *)json_object_get_string(configForPlaytimePath);
    }
    else {
        offblast->playtimePath = configPath;
    }
    printf("Playtime location: %s\n", offblast->playtimePath);


    char *launchTargetDbPath;
    asprintf(&launchTargetDbPath, "%s/launchtargets.bin", configPath);
    OffblastDbFile launchTargetDb = {0};
    if (!InitDbFile(launchTargetDbPath, &launchTargetDb, 
                sizeof(LaunchTarget))) 
    {
        printf("couldn't initialize path db, exiting\n");
        return 1;
    }
    LaunchTargetFile *launchTargetFile = 
        (LaunchTargetFile*) launchTargetDb.memory;
    offblast->launchTargetFile = launchTargetFile;
    free(launchTargetDbPath);

    char *descriptionDbPath;
    asprintf(&descriptionDbPath, "%s/descriptions.bin", configPath);
    OffblastDbFile descriptionDb = {0};
    if (!InitDbFile(descriptionDbPath, &descriptionDb, 
                1))
    {
        printf("couldn't initialize the descriptions file, exiting\n");
        return 1;
    }
    offblast->descriptionFile = 
        (OffblastBlobFile*) descriptionDb.memory;
    free(descriptionDbPath);


    char *launcherContentsHashFilePath;
    asprintf(&launcherContentsHashFilePath, 
            "%s/launchercontents.bin", configPath);

    FILE *launcherContentsFd = fopen(launcherContentsHashFilePath, "rb");
    offblast->launcherContentsCache.length = 0; 
    uint32_t lengthHeader = 0;
    if (launcherContentsFd != NULL 
            && fread(&lengthHeader, sizeof(uint32_t), 1 , launcherContentsFd) > 0)
    {
        printf("Got something (%u) in the contents cache\n", lengthHeader);
        offblast->launcherContentsCache.length = lengthHeader;
        offblast->launcherContentsCache.entries = 
            calloc(lengthHeader, sizeof(LauncherContentsHash));

        size_t itemsRead = fread(offblast->launcherContentsCache.entries, 
            sizeof(LauncherContentsHash), lengthHeader, launcherContentsFd);

        assert(itemsRead == lengthHeader);
        fclose(launcherContentsFd);
    }
    else {
        printf("No contents hash found, everything will rescrape.\n");
    }


#if 0
    // XXX DEBUG Dump out all launch targets
    for (int i = 0; i < launchTargetFile->nEntries; ++i) {
        printf("Reading from local game db (%u) entries\n", 
                launchTargetFile->nEntries);
        printf("found game\t%d\t%u\n", 
                i, launchTargetFile->entries[i].targetSignature); 

        printf("%s\n", launchTargetFile->entries[i].name);
        printf("%s\n", launchTargetFile->entries[i].fileName);
        printf("%s\n", launchTargetFile->entries[i].path);
        printf("--\n\n");

    } // XXX DEBUG ONLY CODE
#endif 

    offblast->platforms = calloc(MAX_PLATFORMS, 256 * sizeof(char));

    uint32_t nConfigLaunchers = json_object_array_length(configLaunchers);
    uint32_t configLauncherSignatures[nConfigLaunchers];

    printf("Setting up launchers.\n");
    offblast->launchers = calloc(nConfigLaunchers, sizeof(Launcher));

    for (int i=0; i < nConfigLaunchers; ++i) {

        json_object *launcherNode= NULL;
        launcherNode = json_object_array_get_idx(configLaunchers, i);
        const char *rawJson = json_object_to_json_string(launcherNode);
        uint32_t configLauncherSignature = 0;
        lmmh_x86_32(rawJson, strlen(rawJson), 33, &configLauncherSignature);
        configLauncherSignatures[i] = configLauncherSignature;

        Launcher *theLauncher = &offblast->launchers[offblast->nLaunchers++];
        theLauncher->signature = configLauncherSignature;

        // Generic Properties
        json_object *typeStringNode = NULL;
        const char *theType = NULL;
        json_object *platformStringNode = NULL;
        const char *thePlatform = NULL;
        json_object *cmdStringNode = NULL;
        const char *theCommand = NULL;
        json_object *nameStringNode = NULL;
        const char *theName = NULL;

        // Emulator Specific Properties 
        json_object *extensionStringNode = NULL;
        const char *theExtension = NULL;
        json_object *romPathStringNode = NULL;
        const char *theRomPath = NULL;

        json_object *cemuPathStringNode = NULL;
        const char *theCemuPath = NULL;

        json_object_object_get_ex(launcherNode, "type",
                &typeStringNode);
        theType = json_object_get_string(typeStringNode);
        assert(strlen(theType) < 256);
        memcpy(&theLauncher->type, theType, strlen(theType));

        json_object_object_get_ex(launcherNode, "name",
                &nameStringNode);

        if (nameStringNode) {
            theName= json_object_get_string(nameStringNode);
            assert(strlen(theName) < 64);
            memcpy(&theLauncher->name, theName, strlen(theName));
        }

        if (strcmp("cemu", theLauncher->type) == 0) {

            json_object_object_get_ex(
                    launcherNode, 
                    "cemu_path",
                    &cemuPathStringNode);

            theCemuPath = json_object_get_string(cemuPathStringNode);
            memcpy(&theLauncher->cemuPath, 
                    theCemuPath, 
                    strlen(theCemuPath));

            json_object_object_get_ex(launcherNode, "platform",
                    &platformStringNode);
            thePlatform = json_object_get_string(platformStringNode);
            assert(strlen(thePlatform) < 256);
            memcpy(&theLauncher->platform, 
                    thePlatform, strlen(thePlatform));

            json_object_object_get_ex(launcherNode, "cmd",
                    &cmdStringNode);
            theCommand = json_object_get_string(cmdStringNode);
            assert(strlen(theCommand) < 512);
            memcpy(&theLauncher->cmd, theCommand, strlen(theCommand));

        }
        else if (strcmp("custom", theLauncher->type) == 0 || 
                strcmp("retroarch", theLauncher->type) == 0)
        {

            json_object_object_get_ex(launcherNode, "rom_path",
                    &romPathStringNode);
            theRomPath = json_object_get_string(romPathStringNode);
            memcpy(&theLauncher->romPath, theRomPath, strlen(theRomPath));

            json_object_object_get_ex(launcherNode, "extension",
                    &extensionStringNode);
            theExtension = json_object_get_string(extensionStringNode);
            assert(strlen(theExtension) < 32);
            memcpy(&theLauncher->extension, 
                    theExtension, strlen(theExtension));

            json_object_object_get_ex(launcherNode, "platform",
                    &platformStringNode);
            thePlatform = json_object_get_string(platformStringNode);
            assert(strlen(thePlatform) < 256);
            memcpy(&theLauncher->platform, 
                    thePlatform, strlen(thePlatform));

            json_object_object_get_ex(launcherNode, "cmd",
                    &cmdStringNode);
            theCommand = json_object_get_string(cmdStringNode);
            assert(strlen(theCommand) < 512);
            memcpy(&theLauncher->cmd, theCommand, strlen(theCommand));

        }
        else if (strcmp("steam", theLauncher->type) == 0){
            memcpy(&theLauncher->platform, 
                    "steam", strlen("steam"));
        }
        else if (strcmp("rpcs3", theLauncher->type) == 0){
            memcpy(&theLauncher->platform, 
                    "playstation_3", strlen("playstation_3"));

            json_object_object_get_ex(launcherNode, "cmd",
                    &cmdStringNode);
            theCommand = json_object_get_string(cmdStringNode);
            assert(strlen(theCommand) < 512);
            memcpy(&theLauncher->cmd, theCommand, strlen(theCommand));
        }
        else if (strcmp("scummvm", theLauncher->type) == 0){
            memcpy(&theLauncher->platform, 
                    "pc", strlen("pc"));
        }
        else {
            printf("Unsupported Launcher Type: %s\n", theLauncher->type);
            continue;
        }



        // TODO maybe we should also do a platform cleanup too?
        printf("Setting up platform: %s\n", (char*)&theLauncher->platform);
        if (i == 0) {
            memcpy(offblast->platforms[offblast->nPlatforms], 
                    theLauncher->platform, 
                    strlen(theLauncher->platform));

            offblast->nPlatforms++;
        }
        else {
            uint8_t gotPlatform = 0;
            for (uint32_t i = 0; i < offblast->nPlatforms; ++i) {
                if (strcmp(offblast->platforms[i], theLauncher->platform) == 0) 
                    gotPlatform = 1;
            }
            if (!gotPlatform) {
                memcpy(offblast->platforms[offblast->nPlatforms], 
                        theLauncher->platform, strlen(theLauncher->platform));
                offblast->nPlatforms++;
            }
        }

        uint32_t platformScraped = 0;
        for (uint32_t i=0; i < launchTargetFile->nEntries; ++i) {
            if (strcmp(launchTargetFile->entries[i].platform, 
                        theLauncher->platform) == 0) 
            {
                printf("%s already scraped.\n", theLauncher->platform);
                platformScraped = 1;
                break;
            }
        }


        if (!platformScraped) {

            printf("Pulling data in from the opengamedb.\n");
            char *openGameDbPlatformPath;
            asprintf(&openGameDbPlatformPath, "%s/%s.csv", openGameDbPath, 
                    theLauncher->platform);
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

                    asprintf(&gameSeed, "%s_%s", 
                            theLauncher->platform, gameName);

                    uint64_t targetSignature = 0;
                    lmmh_x64_128(gameSeed, strlen(gameSeed), 33, 
                            (uint64_t*)&targetSignature);

                    int32_t indexOfEntry = launchTargetIndexByTargetSignature(
                            launchTargetFile, targetSignature);

                    if (indexOfEntry == -1) {

                        void *pLaunchTargetMemory = growDbFileIfNecessary(
                                    &launchTargetDb, 
                                    sizeof(LaunchTarget),
                                    OFFBLAST_DB_TYPE_FIXED);

                        if(pLaunchTargetMemory == NULL) {
                            printf("Couldn't expand the db file to accomodate"
                                    " all the targets\n");
                            return 1;
                        }
                        else {
                            launchTargetFile = 
                                (LaunchTargetFile*) pLaunchTargetMemory; 
                            offblast->launchTargetFile = launchTargetFile;
                        }

                        char *gameDate = getCsvField(csvLine, 2);
                        char *scoreString = getCsvField(csvLine, 3);
                        char *metaScoreString = getCsvField(csvLine, 4);
                        char *description = getCsvField(csvLine, 6);
                        char *coverArtUrl = getCsvField(csvLine, 7);
                        char *gameId = getCsvField(csvLine, 8);

                        printf("\n--\nAdding: \n%s\n%" PRIu64 "\n%s\n%s\ng: %s\n\nm: %s\n%s\n", 
                                gameSeed, 
                                targetSignature, 
                                gameName, 
                                gameDate,
                                scoreString, metaScoreString, gameId);

                        LaunchTarget *newEntry = 
                            &launchTargetFile->entries[launchTargetFile->nEntries];
                        printf("writing new game to %p\n", newEntry);

                        newEntry->targetSignature = targetSignature;

                        memcpy(&newEntry->name, 
                                gameName, 
                                strlen(gameName));

                        memcpy(&newEntry->platform, 
                                theLauncher->platform,
                                strlen(theLauncher->platform));

                        memcpy(&newEntry->coverUrl, 
                                coverArtUrl,
                                strlen(coverArtUrl));

                        memcpy(&newEntry->id, 
                                gameId,
                                strlen(gameId));

                        // TODO harden
                        if (strlen(gameDate) == 10) {
                            memcpy(&newEntry->date, gameDate, 10);
                        }
                        if (strlen(gameDate) == 4 && strtod(gameDate, NULL)) {
                            memcpy(&newEntry->date, gameDate, 4);
                        }
                        else {
                            printf("INVALID DATE FORMAT\n");
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


                        void *pDescriptionFile = growDbFileIfNecessary(
                                    &descriptionDb, 
                                    sizeof(OffblastBlob) 
                                        + strlen(description),
                                    OFFBLAST_DB_TYPE_BLOB); 

                        if(pDescriptionFile == NULL) {
                            printf("Couldn't expand the description file to "
                                    "accomodate all the descriptions\n");
                            return 1;
                        }
                        else { 
                            offblast->descriptionFile = 
                                (OffblastBlobFile*) pDescriptionFile;
                        }

                        printf("description file just after cursor is now %lu\n", 
                                offblast->descriptionFile->cursor);

                        OffblastBlob *newDescription = (OffblastBlob*) 
                            &offblast->descriptionFile->memory[
                                offblast->descriptionFile->cursor];

                        newDescription->targetSignature = targetSignature;
                        newDescription->length = strlen(description);

                        memcpy(&newDescription->content, description, 
                                strlen(description));
                        *(newDescription->content + strlen(description)) = '\0';

                        newEntry->descriptionOffset = 
                            offblast->descriptionFile->cursor;
                        
                        offblast->descriptionFile->cursor += 
                            sizeof(OffblastBlob) + strlen(description) + 1;

                        printf("description file cursor is now %lu\n", 
                                offblast->descriptionFile->cursor);

                        newEntry->ranking = (uint32_t)round(score);

                        launchTargetFile->nEntries++;

                        free(gameDate);
                        free(scoreString);
                        free(metaScoreString);
                        free(description);
                        free(coverArtUrl);
                        free(gameId);

                    }
                    else {
                        printf("%d index found, We already have %"PRIu64":%s\n", 
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

        if (strcmp(theLauncher->type, "cemu") == 0) {
            importFromCemu(theLauncher);
        }
        else if (strcmp(theLauncher->type, "steam") == 0) {
            importFromSteam(theLauncher);
        }
        else if (strcmp(theLauncher->type, "scummvm") == 0) {
            importFromScummvm(theLauncher);
        }
        else if (strcmp(theLauncher->type, "rpcs3") == 0) {
            importFromRPCS3(theLauncher);
        }
        else if (strcmp(theLauncher->type, "custom") == 0||
                strcmp(theLauncher->type, "retroarch") == 0) 
        { 
            importFromCustom(theLauncher);
        }

    }

    // Write out the contents cache
    launcherContentsFd = fopen(launcherContentsHashFilePath, "wb");
    assert(launcherContentsFd);
    fwrite(&offblast->launcherContentsCache.length, 
            sizeof(uint32_t), 1, launcherContentsFd);
    fwrite(
            offblast->launcherContentsCache.entries, 
            sizeof(LauncherContentsHash), 
            offblast->launcherContentsCache.length, 
            launcherContentsFd);
    fclose(launcherContentsFd);

    // TODO this is sort of bugged
    for (int i = 0; i < launchTargetFile->nEntries; ++i) {
        int isOrphan = 0;
        if (launchTargetFile->entries[i].launcherSignature) { 
            isOrphan = 1;
            for (int j=0; j < nConfigLaunchers; j++) {
                if (launchTargetFile->entries[i].launcherSignature 
                        == configLauncherSignatures[j]) 
                {
                    isOrphan = 0;
                }
            }
        }

        if (isOrphan) {
            printf("ORPHANED GAME: \n%s\n%u\n", 
                    launchTargetFile->entries[i].name,
                    launchTargetFile->entries[i].launcherSignature);

            launchTargetFile->entries[i].launcherSignature = 0;
        }
    }

    printf("DEBUG - got %u platforms\n", offblast->nPlatforms);

    close(launchTargetDb.fd);


    json_object *usersObject = NULL;
    json_object_object_get_ex(configObj, "users", &usersObject);
    offblast->nUsers = json_object_array_length(usersObject);
    assert(offblast->nUsers);
    offblast->users = calloc(offblast->nUsers + 1, sizeof(User));

    uint32_t iUser;
    for (iUser = 0; iUser < offblast->nUsers; iUser++) {

        json_object *workingUserNode = NULL;
        json_object *workingNameNode = NULL;
        json_object *workingEmailNode = NULL;
        json_object *workingAvatarPathNode = NULL;
        json_object *workingCemuAccountNode = NULL;
        json_object *workingRetroarchConfigNode = NULL;
        json_object *workingSavePathNode = NULL;
        json_object *workingDolphinCardPathNode = NULL;

        const char *theName= NULL;
        const char *theEmail = NULL;
        const char *theAvatarPath= NULL;
        const char *theCemuAccount = NULL;
        const char *theRetroarchConfig = NULL;
        const char *theSavePath = NULL;
        const char *theDolphinCardPath = NULL;

        workingUserNode = json_object_array_get_idx(usersObject, iUser);
        json_object_object_get_ex(workingUserNode, "name",
                &workingNameNode);
        json_object_object_get_ex(workingUserNode, "email",
                &workingEmailNode);
        json_object_object_get_ex(workingUserNode, "avatar",
                &workingAvatarPathNode);
        json_object_object_get_ex(workingUserNode, "cemu_account",
                &workingCemuAccountNode);
        json_object_object_get_ex(workingUserNode, "retroarch_config",
                &workingRetroarchConfigNode);
        json_object_object_get_ex(workingUserNode, "save_path",
                &workingSavePathNode);
        json_object_object_get_ex(workingUserNode, "dolphin_card",
                &workingDolphinCardPathNode);


        theName = json_object_get_string(workingNameNode);
        theEmail = json_object_get_string(workingEmailNode);
        theAvatarPath = json_object_get_string(workingAvatarPathNode);

        User *pUser = &offblast->users[iUser];
        uint32_t nameLen = (strlen(theName) < 256) ? strlen(theName) : 255;
        uint32_t emailLen = (strlen(theEmail) < 512) ? strlen(theEmail) : 512;
        uint32_t avatarLen = 
            (strlen(theAvatarPath) < PATH_MAX) ? 
                    strlen(theAvatarPath) : PATH_MAX;

        memcpy(&pUser->name, theName, nameLen);
        memcpy(&pUser->email, theEmail, emailLen);
        memcpy(&pUser->avatarPath, theAvatarPath, avatarLen);

        if (workingCemuAccountNode) {
            theCemuAccount = json_object_get_string(workingCemuAccountNode);
            if (strlen(theCemuAccount) < 32) 
                memcpy(&pUser->cemuAccount, 
                        theCemuAccount, strlen(theCemuAccount));
        }

        if (workingRetroarchConfigNode) {
            theRetroarchConfig = 
                json_object_get_string(workingRetroarchConfigNode);

            if (strlen(theRetroarchConfig) < PATH_MAX) 
                memcpy(&pUser->retroarchConfig, 
                        theRetroarchConfig, strlen(theRetroarchConfig));
        }

        if (workingSavePathNode) {
            theSavePath= 
                json_object_get_string(workingSavePathNode);

            if (strlen(theSavePath) < PATH_MAX) 
                memcpy(&pUser->savePath, 
                        theSavePath, strlen(theSavePath));
        }

        if (workingDolphinCardPathNode) {
            theDolphinCardPath = 
                json_object_get_string(workingDolphinCardPathNode);

            if (strlen(theDolphinCardPath) < PATH_MAX) 
                memcpy(&pUser->dolphinCardPath, 
                        theDolphinCardPath, strlen(theDolphinCardPath));
        }

    }

    json_tokener_free(tokener);

    User *pUser = &offblast->users[iUser];
    memcpy(&pUser->name, "Guest", strlen("Guest"));
    memcpy(&pUser->avatarPath, "guest-512.jpg", strlen("guest-512.jpg"));
    offblast->nUsers++;
    loadPlaytimeFile();


    // CREATE WORKER THREADS
    pthread_mutex_init(&offblast->downloader.lock, NULL);
    pthread_mutex_init(&offblast->imageLoader.lock, NULL);

    pthread_t downloadThread;
    pthread_create(
            &downloadThread, 
            NULL, 
            downloadMain, 
            (void*)&offblast->downloader);


    pthread_t imageLoadThread;
    pthread_create(
            &imageLoadThread, 
            NULL, 
            imageLoadMain, 
            (void*)&offblast->imageLoader);

    

    // § START SDL HERE
    if (SDL_Init(SDL_INIT_VIDEO |
                SDL_INIT_JOYSTICK | 
                SDL_INIT_GAMECONTROLLER) != 0) 
    {
        printf("SDL initialization Failed, exiting..\n");
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
                SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_ALLOW_HIGHDPI);

    if (window == NULL) {
        printf("SDL window creation failed, exiting..\n");
        return 1;
    }
    offblast->window = window;
    
    char *windowManager = getenv("XDG_CURRENT_DESKTOP");
    assert(windowManager);

    if (strcmp(windowManager, "i3") == 0) {
        offblast->windowManager = WINDOW_MANAGER_I3;
        system("i3-msg move to workspace offblast && i3-msg workspace offblast");
    }
    if (strcmp(windowManager, "GNOME") >= 0) {
        offblast->windowManager = WINDOW_MANAGER_GNOME;
    }
    else {
        perror("Your window manager is not yet supported\n");
    }


    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    glewInit();
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    offblast->XDisplay = XOpenDisplay(NULL);
    if(offblast->XDisplay == NULL){
        printf("Couldn't connect to Xserver\n");
        return 0;
    }


    // § Init UI
    MainUi *mainUi = &offblast->mainUi;
    PlayerSelectUi *playerSelectUi = &offblast->playerSelectUi;

    needsReRender(window);
    mainUi->horizontalAnimation = calloc(1, sizeof(Animation));
    mainUi->verticalAnimation = calloc(1, sizeof(Animation));
    mainUi->infoAnimation = calloc(1, sizeof(Animation));
    mainUi->rowNameAnimation = calloc(1, sizeof(Animation));

    mainUi->showMenu = 0;
    mainUi->showSearch = 0;

    // Init Menu
    // Let's make enough room for say 20 menu items TODO
    uint32_t iItem = 0;
    mainUi->menuItems = calloc(offblast->nLaunchers+5, sizeof(MenuItem));
    mainUi->menuItems[iItem].label = "Home";
    mainUi->menuItems[iItem++].callback = doHome;
    mainUi->numMenuItems++;

    mainUi->menuItems[iItem].label = "Search";
    mainUi->menuItems[iItem++].callback = doSearch;
    mainUi->numMenuItems++;

    mainUi->menuItems[iItem].label = "Change User";
    mainUi->menuItems[iItem++].callback = openPlayerSelect;
    mainUi->numMenuItems++;

    for (uint32_t i = 0; i < offblast->nLaunchers; ++i) {
        if (strlen(offblast->launchers[i].name)) {
            mainUi->menuItems[iItem].label = 
                (char *) offblast->launchers[i].name;
        }
        else {
            mainUi->menuItems[iItem].label = (char *) platformString(
                    offblast->launchers[i].platform);
        }

        mainUi->menuItems[iItem].callbackArgs 
            = &offblast->launchers[i].signature;

        mainUi->menuItems[iItem++].callback = updateResults;

        mainUi->numMenuItems++;
    }

    mainUi->menuItems[iItem].label = "Exit Offblast";
    mainUi->menuItems[iItem++].callback = setExit;
    mainUi->numMenuItems++;

    mainUi->menuCursor = 0;

    if (1) {
        mainUi->menuItems[mainUi->numMenuItems].label = "Shut Down Machine";
        mainUi->menuItems[mainUi->numMenuItems].callback = shutdownMachine;
        mainUi->numMenuItems++;
    }


    // Missing Cover texture init
    {
        // TODO assets dir
        int n;
        stbi_set_flip_vertically_on_load(1);
        unsigned char *imageData = stbi_load(
                "missingcover.png", 
                (int *)&offblast->missingCoverImage.width, 
                (int *)&offblast->missingCoverImage.height, &n, 4);

        if(imageData != NULL) {
            glGenTextures(1, &offblast->missingCoverImage.textureHandle);
            imageToGlTexture(
                    &offblast->missingCoverImage.textureHandle,
                    imageData, 
                    offblast->missingCoverImage.width, 
                    offblast->missingCoverImage.height);
            free(imageData);
        }
        else {
            printf("couldn't load the missing image image!\n");
            return 1;
        }
    }

    // § Bitmap font setup
    FILE *fd = fopen("./fonts/Roboto-Regular.ttf", "r");

    if (!fd) {
        printf("Could'nt open file\n");
        return 1;
    }
    fseek(fd, 0, SEEK_END);
    long numBytes = ftell(fd);
    printf("File is %ld bytes long\n", numBytes);
    fseek(fd, 0, SEEK_SET);

    unsigned char *fontContents = malloc(numBytes);
    assert(fontContents);

    int read = fread(fontContents, numBytes, 1, fd);
    assert(read);
    fclose(fd);

    offblast->textBitmapHeight = 1024;
    offblast->textBitmapWidth = 2048;

    // TODO this should be a function karl
    
    unsigned char *titleAtlas = calloc(offblast->textBitmapWidth * offblast->textBitmapHeight, sizeof(unsigned char));

    stbtt_BakeFontBitmap(fontContents, 0, offblast->titlePointSize, 
            titleAtlas, 
            offblast->textBitmapWidth, 
            offblast->textBitmapHeight,
            32, 95, offblast->titleCharData);

    glGenTextures(1, &offblast->titleTextTexture);
    glBindTexture(GL_TEXTURE_2D, offblast->titleTextTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 
            offblast->textBitmapWidth, offblast->textBitmapHeight, 
            0, GL_RED, GL_UNSIGNED_BYTE, titleAtlas); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    stbi_write_png("titletest.png", offblast->textBitmapWidth, offblast->textBitmapHeight, 1, titleAtlas, 0);

    free(titleAtlas);
    titleAtlas = NULL;

    unsigned char *infoAtlas = calloc(offblast->textBitmapWidth * offblast->textBitmapHeight, sizeof(unsigned char));

    stbtt_BakeFontBitmap(fontContents, 0, offblast->infoPointSize, 
            infoAtlas, 
            offblast->textBitmapWidth, 
            offblast->textBitmapHeight,
            32, 95, offblast->infoCharData);

    glGenTextures(1, &offblast->infoTextTexture);
    glBindTexture(GL_TEXTURE_2D, offblast->infoTextTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 
            offblast->textBitmapWidth, offblast->textBitmapHeight, 
            0, GL_RED, GL_UNSIGNED_BYTE, infoAtlas); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    stbi_write_png("infotest.png", offblast->textBitmapWidth, offblast->textBitmapHeight, 1, infoAtlas, 0);

    free(infoAtlas);
    infoAtlas = NULL;

    unsigned char *debugAtlas = calloc(offblast->textBitmapWidth * offblast->textBitmapHeight, sizeof(unsigned char));

    stbtt_BakeFontBitmap(fontContents, 0, offblast->debugPointSize, debugAtlas, 
            offblast->textBitmapWidth, 
            offblast->textBitmapHeight,
            32, 95, offblast->debugCharData);

    glGenTextures(1, &offblast->debugTextTexture);
    glBindTexture(GL_TEXTURE_2D, offblast->debugTextTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 
            offblast->textBitmapWidth, offblast->textBitmapHeight, 
            0, GL_RED, GL_UNSIGNED_BYTE, debugAtlas); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    stbi_write_png("debugtest.png", offblast->textBitmapWidth, offblast->textBitmapHeight, 1, debugAtlas, 0);

    free(debugAtlas);
    free(fontContents);
    fontContents = NULL;
    debugAtlas = NULL;

    offblast->player.jsIndex = -1;
    playerSelectUi->images = calloc(offblast->nUsers, sizeof(Image));
    playerSelectUi->widthForAvatar = 
        calloc(offblast->nUsers+1, sizeof(float));
    assert(playerSelectUi->widthForAvatar);
    playerSelectUi->xOffsetForAvatar = 
        calloc(offblast->nUsers+1, sizeof(float));
    assert(playerSelectUi->xOffsetForAvatar);

    for (uint32_t i = 0; i < offblast->nUsers; ++i) {

        int w, h, n;
        stbi_set_flip_vertically_on_load(1);
        unsigned char *imageData = stbi_load(
                offblast->users[i].avatarPath, &w, &h, &n, 4);

        if(imageData != NULL) {

            imageToGlTexture(
                    &playerSelectUi->images[i].textureHandle, 
                    imageData, w, h);

            playerSelectUi->images[i].state = IMAGE_STATE_COMPLETE;
            playerSelectUi->images[i].width = w;
            playerSelectUi->images[i].height = h;
            
            float w = getWidthForScaledImage(
                offblast->mainUi.boxHeight, &playerSelectUi->images[i]);

            playerSelectUi->widthForAvatar[i] = w;
            playerSelectUi->xOffsetForAvatar[i] = playerSelectUi->totalWidth;
            playerSelectUi->totalWidth += w;

        }
        else {
            printf("couldn't load texture for avatar %s\n", 
                    offblast->users[i].avatarPath);
            // TODO use mystery man image
        }
        free(imageData);

    }

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    // Text Pipeline
    GLint textVertShader = loadShaderFile("shaders/text.vert", 
            GL_VERTEX_SHADER);
    GLint textFragShader = loadShaderFile("shaders/text.frag", 
            GL_FRAGMENT_SHADER);
    assert(textVertShader);
    assert(textFragShader);

    offblast->textProgram = 
        createShaderProgram(textVertShader, textFragShader);
    assert(offblast->textProgram);

    offblast->textAlphaUni = glGetUniformLocation(
            offblast->textProgram, "myAlpha");


    // Image Pipeline
    GLint imageVertShader = loadShaderFile("shaders/image.vert", 
            GL_VERTEX_SHADER);
    GLint imageFragShader = loadShaderFile("shaders/image.frag", 
            GL_FRAGMENT_SHADER);
    assert(imageVertShader);
    assert(imageFragShader);

    offblast->imageProgram = createShaderProgram(imageVertShader, imageFragShader);
    assert(offblast->imageProgram);
    offblast->imageTranslateUni = glGetUniformLocation(
            offblast->imageProgram, "myOffset");
    offblast->imageAlphaUni = glGetUniformLocation(
            offblast->imageProgram, "myAlpha");
    offblast->imageDesaturateUni = glGetUniformLocation(
            offblast->imageProgram, "whiteMix");

    // Gradient Pipeline
    GLint gradientVertShader = loadShaderFile("shaders/gradient.vert", 
            GL_VERTEX_SHADER);
    GLint gradientFragShader = loadShaderFile("shaders/gradient.frag", 
            GL_FRAGMENT_SHADER);
    assert(gradientVertShader);
    assert(gradientFragShader);
    offblast->gradientProgram = createShaderProgram(gradientVertShader, 
            gradientFragShader);
    assert(offblast->gradientProgram);


    offblast->running = 1;
    uint32_t lastTick = SDL_GetTicks();
    uint32_t renderFrequency = 1000/60;

    // § Init Ui
    mainUi->searchRowset = calloc(1, sizeof(UiRowset));
    mainUi->searchRowset->rows = calloc(100, sizeof(UiRow)); // TODO
    mainUi->searchRowset->rows[0].tiles = calloc(1, sizeof(UiTile));
    mainUi->searchRowset->rowCursor = mainUi->searchRowset->rows;
    mainUi->searchRowset->numRows = 0;
    mainUi->searchRowset->movingToRow = &mainUi->searchRowset->rows[0];


    // allocate home rowset
    mainUi->homeRowset = calloc(1, sizeof(UiRowset));
    mainUi->homeRowset->rows = calloc(3 + offblast->nPlatforms, sizeof(UiRow));
    mainUi->homeRowset->numRows = 0;
    mainUi->homeRowset->rowCursor = mainUi->homeRowset->rows;
    mainUi->activeRowset = mainUi->homeRowset;

    mainUi->rowGeometryInvalid = 1;

    updateHomeLists();


    // § Main loop
    while (offblast->running) {

        if (needsReRender(window) == 1) {
            printf("Window size changed, sizes updated.\n");
            mainUi->rowGeometryInvalid = 1;
        }

        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                printf("shutting down\n");
                offblast->running = 0;
                break;
            }
            else if (event.type == SDL_WINDOWEVENT) {
                switch (event.window.event) {
                    case SDL_WINDOWEVENT_FOCUS_LOST:
                        offblast->loadingFlag = 0;
                        break;
                }
            }
            else if (event.type == SDL_CONTROLLERAXISMOTION) {

                // TODO only if it's the player ones controller?
                if (event.jaxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
                    offblast->joyX = event.jaxis.value;
                else if (event.jaxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
                    offblast->joyY = event.jaxis.value * -1;

            }
            else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                SDL_ControllerButtonEvent *buttonEvent = 
                    (SDL_ControllerButtonEvent *) &event;

                switch(buttonEvent->button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                        changeRow(1);
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                        changeRow(0);
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                        changeColumn(0);
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                        changeColumn(1);
                        break;
                    case SDL_CONTROLLER_BUTTON_A:
                        pressConfirm(buttonEvent->which);
                        break;
                    case SDL_CONTROLLER_BUTTON_B:
                        pressCancel();
                        break;
                    case SDL_CONTROLLER_BUTTON_Y:
                        pressSearch();
                        break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                        jumpScreen(0);
                        break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                        jumpScreen(1);
                        break;
                    case SDL_CONTROLLER_BUTTON_GUIDE:
                        pressGuide();
                        break;
                }

            }
            else if (event.type == SDL_CONTROLLERBUTTONUP) {
                //printf("button up\n");
            }
            else if (event.type == SDL_CONTROLLERDEVICEADDED) {

                SDL_ControllerDeviceEvent *devEvent = 
                    (SDL_ControllerDeviceEvent*)&event;

                printf("controller added %d\n", devEvent->which);
                SDL_GameController *controller;
                if (SDL_IsGameController(devEvent->which) == SDL_TRUE 
                        && offblast->player.jsIndex == -1) 
                {
                    controller = SDL_GameControllerOpen(devEvent->which); 
                    if (controller == NULL)  {
                        printf("failed to add %d\n", devEvent->which);
                    }
                    /*
                    else {
                        offblast->mode = OFFBLAST_UI_MODE_PLAYER_SELECT;
                    }
                    */
                }
            }
            else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
                SDL_ControllerDeviceEvent *devEvent = 
                    (SDL_ControllerDeviceEvent*)&event;
                if (offblast->player.jsIndex == devEvent->which) {
                    offblast->player.jsIndex = -1;
                    printf("controller removed\n");
                }
            }
            else if (event.type == SDL_KEYUP) {
                SDL_KeyboardEvent *keyEvent = (SDL_KeyboardEvent*) &event;
                if (keyEvent->keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    printf("escape pressed, shutting down.\n");
                    offblast->running = 0;
                    break;
                }
                else if (keyEvent->keysym.scancode == SDL_SCANCODE_RETURN) {
                    pressConfirm(-1);
                    SDL_RaiseWindow(window);
                }
                else if (keyEvent->keysym.scancode == SDL_SCANCODE_F) {
                    SDL_SetWindowFullscreen(window, 
                            SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
                else if (
                        keyEvent->keysym.scancode == SDL_SCANCODE_DOWN ||
                        keyEvent->keysym.scancode == SDL_SCANCODE_J) 
                {
                    changeRow(0);
                }
                else if (
                        keyEvent->keysym.scancode == SDL_SCANCODE_UP ||
                        keyEvent->keysym.scancode == SDL_SCANCODE_K) 
                {
                    changeRow(1);
                }
                else if (
                        keyEvent->keysym.scancode == SDL_SCANCODE_RIGHT ||
                        keyEvent->keysym.scancode == SDL_SCANCODE_L) 
                {
                    changeColumn(1);
                }
                else if (
                        keyEvent->keysym.scancode == SDL_SCANCODE_LEFT ||
                        keyEvent->keysym.scancode == SDL_SCANCODE_H) 
                {
                    changeColumn(0);
                }
                else {
                    printf("key up %d\n", keyEvent->keysym.scancode);
                }
                // SDL_KEYMOD
                // TODO how to use modifier keys? for jump left and jump right?
            }

        }

        // § Player Detection
        // TODO should we do this on every loop?
        if (offblast->player.emailHash == 0) {
            offblast->mode = OFFBLAST_UI_MODE_PLAYER_SELECT;
            // TODO this should probably kill all the active animations?
            // or fire their callbacks immediately
        }

        // RENDER
        glClearColor(0.0, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);


        if (offblast->mode == OFFBLAST_UI_MODE_MAIN) {



            // § Blocks
            if (mainUi->activeRowset->numRows == 0) {
                //printf("norows!\n");
            }
            else {

                if (mainUi->rowGeometryInvalid) {
                    for (uint32_t i=0; i < mainUi->activeRowset->numRows; ++i) {
                        calculateRowGeometry(&mainUi->activeRowset->rows[i]); 
                    }
                    mainUi->rowGeometryInvalid = 0;
                }

                // Set the origin Y
                UiRow *rowToRender = mainUi->activeRowset->rowCursor;
                rowToRender = rowToRender->nextRow->nextRow;
                float desaturate = 0.2;
                float alpha = 1.0;

                float yBase = offblast->winFold - 3*mainUi->boxHeight - 2*mainUi->boxPad;

                if (mainUi->verticalAnimation->animating != 0) 
                {
                    double change = easeInOutCirc(
                            (double)SDL_GetTicks() 
                            - mainUi->verticalAnimation->startTick,
                            0.0,
                            (double)mainUi->boxHeight+ mainUi->boxPad,
                            (double)mainUi->verticalAnimation->durationMs);

                    if (mainUi->verticalAnimation->direction > 0) {
                        change = -change;
                    }

                    yBase += change;
                }

                Image *missingImage = &offblast->missingCoverImage;
                uint32_t missingImageWidth = getWidthForScaledImage(
                        offblast->mainUi.boxHeight,
                        missingImage);

                while (yBase < offblast->winHeight) {

                    int32_t displacement = 0;
                    UiTile *theTile = rowToRender->tileCursor;

                    uint32_t theWidth = missingImageWidth;
                    if (theTile->image.width > 0 ) 
                        theWidth = theTile->image.width;

                    Image *imageToShow;

                    displacement = theTile->baseX - offblast->winMargin;

                    if (mainUi->horizontalAnimation->animating != 0 
                            && rowToRender == mainUi->activeRowset->rowCursor) 
                    {
                        double moveAmount = 
                            rowToRender->movingToTile->baseX - theTile->baseX;
                        displacement += easeInOutCirc(
                                (double)SDL_GetTicks() 
                                - mainUi->horizontalAnimation->startTick,
                                0.0,
                                moveAmount,
                                (double)mainUi->horizontalAnimation->durationMs);
                    }

                    while ((int32_t) theTile->baseX - displacement + theWidth 
                            + mainUi->boxPad > 0) 
                    {
                        if (!theTile->previous) break;
                        theTile = theTile->previous;
                    }

                    while ((int32_t) theTile->baseX - displacement 
                            < (offblast->winWidth*1.2)) 
                    {

                        if (theTile->image.textureHandle == 0) 
                        {
                            loadTexture(theTile);
                            imageToShow = missingImage;
                        }
                        else 
                            imageToShow = &theTile->image;

                        desaturate = 0.2;
                        alpha = 1.0;

                        if (theTile->target->launcherSignature == 0) 
                        {
                            desaturate = 0.3;
                            alpha = 0.7;
                        }

                        renderImage(
                                theTile->baseX - displacement, 
                                yBase,
                                0, mainUi->boxHeight, 
                                imageToShow, 
                                desaturate, 
                                alpha);

                        if (!theTile->next) break;
                        theTile = theTile->next;
                    }

                    yBase += mainUi->boxHeight + mainUi->boxPad;
                    rowToRender = rowToRender->previousRow;
                }

                glUniform1f(offblast->imageDesaturateUni, 0.0f);
                glUniform2f(offblast->imageTranslateUni, 0.0f, 0.0f);
                glUniform1f(offblast->imageAlphaUni, 1.0);

                Color bwStartColor = {0.0, 0.0, 0.0, 1.0};
                Color foldGrEndColor = {0.0, 0.0, 0.0, 0.7};
                renderGradient(0, offblast->winFold, 
                        offblast->winWidth, 
                        offblast->winHeight - offblast->winFold, 
                        1,
                        bwStartColor, foldGrEndColor);

                Color bwEndColor = {0.0, 0.0, 0.0, 0.0};
                renderGradient(0, 0, 
                        offblast->winWidth, offblast->titlePointSize*2, 
                        0,
                        bwStartColor, bwEndColor);

                // § INFO AREA
                alpha = 1.0;
                if (mainUi->infoAnimation->animating == 1) {
                    double change = easeInOutCirc(
                            (double)SDL_GetTicks() - 
                            mainUi->infoAnimation->startTick,
                            0.0,
                            1.0,
                            (double)mainUi->infoAnimation->durationMs);

                    if (mainUi->infoAnimation->direction == 0) {
                        change = 1.0 - change;
                    }

                    alpha = change;
                }

                float rowNameAlpha = 1;
                if (mainUi->rowNameAnimation->animating == 1) {
                    double change = easeInOutCirc(
                            (double)SDL_GetTicks() - 
                            mainUi->rowNameAnimation->startTick,
                            0.0,
                            1.0,
                            (double)mainUi->rowNameAnimation->durationMs);

                    if (mainUi->rowNameAnimation->direction == 0) {
                        change = 1.0 - change;
                    }

                    rowNameAlpha = change;
                }

                // TODO calculate elsewhere
                float pixelY = 
                    offblast->winHeight - goldenRatioLargef(offblast->winHeight, 5)
                    - offblast->titlePointSize;

                renderText(offblast, offblast->winMargin, pixelY, 
                        OFFBLAST_TEXT_TITLE, alpha, 0, mainUi->titleText);


                pixelY -= offblast->infoPointSize * 1.4;
                renderText(offblast, offblast->winMargin, pixelY, 
                        OFFBLAST_TEXT_INFO, alpha, 0, mainUi->infoText);


                pixelY -= offblast->infoPointSize + mainUi->boxPad;
                renderText(offblast, offblast->winMargin, pixelY, 
                        OFFBLAST_TEXT_INFO, alpha, mainUi->descriptionWidth, 
                        mainUi->descriptionText); 


                pixelY = offblast->winFold + mainUi->boxPad;
                renderText(offblast, offblast->winMargin, pixelY, 
                        OFFBLAST_TEXT_INFO, rowNameAlpha, 0, mainUi->rowNameText); 
            }

            // § Render Menu
            if (mainUi->showMenu) {
                Color menuColor = {0.0, 0.0, 0.0, 0.85};
                renderGradient(0, 0, 
                        offblast->winWidth * 0.16, offblast->winHeight, 
                        0,
                        menuColor, menuColor);

                float itemTransparency = 0.6f;
                float yOffset = 0;

                for (uint32_t mi = 0; mi < mainUi->numMenuItems; mi++) {
                    if (mainUi->menuItems[mi].label != NULL) {

                        if (mi == mainUi->menuCursor) itemTransparency = 1.0f;

                        renderText(offblast, offblast->winWidth * 0.016, 
                                offblast->winHeight - 133 - yOffset, 
                                OFFBLAST_TEXT_INFO, itemTransparency, 0, 
                                mainUi->menuItems[mi].label);

                        itemTransparency = 0.6f;
                        yOffset += offblast->infoPointSize *1.61;
                    }
                }
            }

            if (mainUi->showSearch) {
                Color menuColor = {0.0, 0.0, 0.0, 0.8};
                renderGradient(0, 0, 
                        offblast->winWidth, offblast->winHeight, 
                        0,
                        menuColor, menuColor);

                float xorigin, yorigin, radius;
                xorigin = offblast->winWidth / 2;
                yorigin = offblast->winHeight / 2;
                radius = offblast->winWidth * 0.23;
                char string[2] ;
                string[1] = 0;

                double joyY = fabs((double)offblast->joyY) / INT16_MAX;
                double joyX = fabs((double)offblast->joyX) / INT16_MAX;

                if (offblast->joyY < 0) {
                    joyY = 0-joyY;
                }

                if (offblast->joyX < 0){
                    joyX = 0-joyX;
                }

                double joyTangent = atan2(
                        joyY,
                        joyX);

                int32_t onChar = round(28/(2*M_PI) * joyTangent);
                if (joyTangent < 0)
                    onChar = 27 -onChar * -1;

                for (uint32_t ki = 0; ki < 28; ki++) {

                    float x, y;
                    x = xorigin + radius * cos((float)ki * 2*M_PI / 28);
                    y = yorigin + radius * sin((float)ki * 2* M_PI / 28);

                    if (ki == 26)
                        string[0] = 60;
                    else if (ki == 27)
                        string[0] = 95;
                    else
                        string[0] = 97 + ki;

                    float opacity = 0.70;

                    if (onChar == ki) {
                        offblast->searchCurChar = string[0];
                        opacity = 1;
                    }

                    renderText(offblast, x, y, 
                            OFFBLAST_TEXT_TITLE, opacity, 0, 
                            (char *)&string);
                }

                char *placeholderText = "Search for games";
                char *textToShow = placeholderText;
                if (strlen(offblast->searchTerm)) 
                    textToShow = (char *)offblast->searchTerm;

                uint32_t lineWidth = getTextLineWidth(textToShow, 
                        offblast->titleCharData);

                renderText(offblast, offblast->winWidth/2 - lineWidth/2, 
                        offblast->winHeight/2 - offblast->titlePointSize/2, 
                        OFFBLAST_TEXT_TITLE, 0.65, 0, 
                        textToShow);

            }

        }
        else if (offblast->mode == OFFBLAST_UI_MODE_PLAYER_SELECT) {

            // TODO cache all these golden ratio calls they are expensive 
            // to calculate
            // cache all the x positions of the text perhaps too?
            char *titleText = "Who's playing?";
            uint32_t titleWidth = getTextLineWidth(titleText, 
                    offblast->titleCharData);

            renderText(offblast, 
                    offblast->winWidth / 2 - titleWidth / 2, 
                    offblast->winHeight - 
                        goldenRatioLarge(offblast->winHeight, 3), 
                    OFFBLAST_TEXT_TITLE, 1.0, 0,
                    titleText);

            uint32_t xStart= offblast->winWidth / 2 
                - playerSelectUi->totalWidth / 2;

            // XXX
            for (uint32_t i = 0; i < offblast->nUsers; ++i) {

                Image *image = &playerSelectUi->images[i];
                float alpha = (i == playerSelectUi->cursor) ? 1.0 : 0.7;

                renderImage(
                        xStart + playerSelectUi->xOffsetForAvatar[i],  
                        offblast->winHeight /2 -0.5* offblast->mainUi.boxHeight, 
                        0, offblast->mainUi.boxHeight, 
                        image, 0.0f, alpha);

                uint32_t nameWidth = getTextLineWidth(
                        offblast->users[i].name,
                        offblast->infoCharData);

                renderText(offblast, 
                        xStart + playerSelectUi->xOffsetForAvatar[i] 
                        + playerSelectUi->widthForAvatar[i] / 2 - nameWidth / 2,
                        offblast->winHeight/2 - 0.5*offblast->mainUi.boxHeight - 
                            offblast->mainUi.boxPad - offblast->infoPointSize, 
                        OFFBLAST_TEXT_INFO, alpha, 0,
                        offblast->users[i].name);
            }
    
        }
        else if (offblast->mode == OFFBLAST_UI_MODE_BACKGROUND) {

            double yOffset = offblast->winHeight - 
                        goldenRatioLarge(offblast->winHeight, 3);

            char *headerText = "Now playing";
            if (offblast->loadingFlag)
                headerText = "Now loading";

            uint32_t titleWidth = getTextLineWidth(headerText, 
                    offblast->titleCharData);

            renderText(offblast, 
                    offblast->winWidth / 2 - titleWidth / 2, 
                    yOffset,
                    OFFBLAST_TEXT_TITLE, 1.0, 0,
                    headerText);

            yOffset -= 100;

            char *titleText = 
                offblast->mainUi.activeRowset->rowCursor->tileCursor->target->name;

            uint32_t nameWidth = 
                getTextLineWidth(titleText, offblast->infoCharData);

            renderText(offblast, 
                    offblast->winWidth / 2 - nameWidth/ 2, 
                    yOffset,
                    OFFBLAST_TEXT_INFO, 1.0, 0,
                    titleText);

            yOffset -= (offblast->infoPointSize * 3);

            UiTile *theTile = 
                offblast->mainUi.activeRowset->rowCursor->tileCursor;
            Image *imageToShow;

            if (theTile->image.textureHandle == 0) {
                imageToShow = &offblast->missingCoverImage;
            }
            else {
                imageToShow = &theTile->image;
            }

            double xPos = offblast->winWidth / 2 - getWidthForScaledImage(
                    mainUi->boxHeight,
                    imageToShow) / 2;

            renderImage(
                    xPos,
                    yOffset - mainUi->boxHeight,
                    0, 
                    mainUi->boxHeight, 
                    imageToShow, 
                    0.2, 
                    1);

            yOffset -= (mainUi->boxHeight + 200);

            if (!offblast->loadingFlag) {
                double stopWidth = 
                    getTextLineWidth("Stop", offblast->infoCharData);

                double resumeWidth = 
                    getTextLineWidth("Resume", offblast->infoCharData);

                double totalWidth = stopWidth + 200 + resumeWidth;

                renderText(offblast, 
                        offblast->winWidth/2 - totalWidth/2, 
                        yOffset,
                        OFFBLAST_TEXT_INFO, 
                        (offblast->uiStopButtonHot ? 0.6 : 1.0), 
                        0,
                        "Resume");

                renderText(offblast, 
                        offblast->winWidth/2 - totalWidth/2 + resumeWidth + 200,
                        yOffset,
                        OFFBLAST_TEXT_INFO, 
                        (offblast->uiStopButtonHot ? 1.0 : 0.6),  
                        0,
                        "Stop");
            }


        }

        uint32_t frameTime = SDL_GetTicks() - lastTick;
        char *fpsString;
        asprintf(&fpsString, "frame time: %u", frameTime);
        renderText(offblast, 15, 15, OFFBLAST_TEXT_DEBUG, 1.0, 0, 
               fpsString);
        free(fpsString);


        animationTick(mainUi->horizontalAnimation);
        animationTick(mainUi->verticalAnimation);
        animationTick(mainUi->infoAnimation);
        animationTick(mainUi->rowNameAnimation);

    
        SDL_GL_SwapWindow(window);

        if (SDL_GetTicks() - lastTick < renderFrequency) {
            SDL_Delay(renderFrequency - (SDL_GetTicks() - lastTick));
        }

        lastTick = SDL_GetTicks();
    }

    free(launcherContentsHashFilePath);
    XCloseDisplay(offblast->XDisplay);

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    pthread_kill(downloadThread, SIGTERM);
    pthread_kill(imageLoadThread, SIGTERM);
    pthread_mutex_destroy(&offblast->downloader.lock);
    pthread_mutex_destroy(&offblast->imageLoader.lock);

    if (offblast->shutdownFlag) {
        system("systemctl poweroff");
    }

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

// TODO consider using window event resized
uint32_t needsReRender(SDL_Window *window) 
{
    int32_t newWidth, newHeight;
    uint32_t updated = 0;

    MainUi *mainUi = &offblast->mainUi;

    SDL_GetWindowSize(window, &newWidth, &newHeight);

    if (newWidth != offblast->winWidth || 
            newHeight != offblast->winHeight) 
    {

        offblast->winWidth = newWidth;
        offblast->winHeight= newHeight;
        glViewport(0, 0, (GLsizei)newWidth, (GLsizei)newHeight);
        offblast->winFold = newHeight * 0.5;
        offblast->winMargin = goldenRatioLarge((double) newWidth, 5);

        // 7:5 TODO I don't think this is actually 7:5
        mainUi->boxHeight = goldenRatioLarge(offblast->winWidth, 4);
        //TODO REMOVE mainUi->boxWidth = mainUi->boxHeight/5 * 7;
        mainUi->boxPad = goldenRatioLarge((double) offblast->winWidth, 9);

        mainUi->descriptionWidth = 
            goldenRatioLarge((double) newWidth, 1) - offblast->winMargin;

        // TODO Find a better way to enfoce this
        mainUi->descriptionHeight = goldenRatioLarge(offblast->winWidth, 3);

        offblast->debugPointSize = goldenRatioLarge(offblast->winWidth, 9);
        offblast->titlePointSize = goldenRatioLarge(offblast->winWidth, 7);
        offblast->infoPointSize = goldenRatioLarge(offblast->winWidth, 9);

        updated = 1;
    }

    return updated;
}


void changeColumn(uint32_t direction) 
{
    MainUi *ui = &offblast->mainUi;

    if (offblast->mode == OFFBLAST_UI_MODE_MAIN) {
        if (animationRunning() == 0)
        {

            if (ui->showMenu) {
                if (direction == 1) {
                    ui->showMenu = 0;
                }
            }
            else {
                if (direction == 0) {

                    if (ui->activeRowset->rowCursor->tileCursor->previous 
                            != NULL) 
                    {
                        ui->activeRowset->movingToTarget = 
                            ui->activeRowset->rowCursor->tileCursor->previous->target;
                        ui->activeRowset->rowCursor->movingToTile
                            = ui->activeRowset->rowCursor->tileCursor->previous;
                    }
                    else {
                        ui->showMenu = 1;
                        return;
                    }

                }
                else {
                    if (ui->activeRowset->rowCursor->tileCursor->next != NULL) 
                    {
                        ui->activeRowset->movingToTarget 
                            = ui->activeRowset->rowCursor->tileCursor->next->target;
                        ui->activeRowset->rowCursor->movingToTile
                            = ui->activeRowset->rowCursor->tileCursor->next;
                    }
                    else {
                        ui->showMenu = 1;
                        return;
                    }
                }

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
            }
        }
    }
    else if (offblast->mode == OFFBLAST_UI_MODE_PLAYER_SELECT) {

        if (offblast->nUsers > 1) {

            if (direction) {
                offblast->playerSelectUi.cursor++;
            }
            else {
                offblast->playerSelectUi.cursor--;
            }

            if (offblast->playerSelectUi.cursor >= offblast->nUsers)
                offblast->playerSelectUi.cursor = offblast->nUsers - 1;

            // TODO bugged out, ends up at 5
            if (offblast->playerSelectUi.cursor < 0)
                offblast->playerSelectUi.cursor = 0;
        }
    }
    if (offblast->mode == OFFBLAST_UI_MODE_BACKGROUND) {
        if(activeWindowIsOffblast()) {
            offblast->uiStopButtonHot = !offblast->uiStopButtonHot;
        }
    }
}

void changeRow(uint32_t direction) 
{
    MainUi *ui = &offblast->mainUi;

    if (animationRunning() == 0)
    {
        if (offblast->mode == OFFBLAST_UI_MODE_MAIN) {

            if (ui->showMenu) {

                if (direction == 0 && ui->menuCursor < ui->numMenuItems-1)
                    ui->menuCursor++;
                else if (direction == 1 && ui->menuCursor != 0)
                    ui->menuCursor--;
            }
            else {
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
                    ui->activeRowset->movingToRow 
                        = ui->activeRowset->rowCursor->nextRow;
                    ui->activeRowset->movingToTarget = 
                        ui->activeRowset->rowCursor->nextRow->tileCursor->target;
                }
                else {
                    ui->activeRowset->movingToRow 
                        = ui->activeRowset->rowCursor->previousRow;
                    ui->activeRowset->movingToTarget = 
                        ui->activeRowset->rowCursor->previousRow->tileCursor->target;
                }
            }
        }
    }
}

void jumpScreen(uint32_t direction) 
{
    MainUi *ui = &offblast->mainUi;

    if (offblast->mode == OFFBLAST_UI_MODE_MAIN) {
        if (animationRunning() == 0)
        {

            // TODO This is broken
            if (!ui->showMenu) {
                if (direction == 0) {
                        ui->activeRowset->movingToTarget = 
                            ui->activeRowset->rowCursor->tiles[0].target;
                        ui->activeRowset->rowCursor->movingToTile = 
                            &ui->activeRowset->rowCursor->tiles[0];

                }
                else {
                    UiTile *endTile = &ui->activeRowset->rowCursor->tiles[
                        ui->activeRowset->rowCursor->length-1];

                        ui->activeRowset->movingToTarget = endTile->target;
                        ui->activeRowset->rowCursor->movingToTile = endTile;
                }

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
            }
        }
    }
    else if (offblast->mode == OFFBLAST_UI_MODE_PLAYER_SELECT) { }
}

void startVerticalAnimation(
        Animation *verticalAnimation,
        Animation *titleAnimation,
        uint32_t direction)
{
}


double goldenRatioLarge(double in, uint32_t exponent) {
    if (exponent == 0) {
        return in;
    }
    else {
        return goldenRatioLarge(1/PHI * in, --exponent); 
    }
}

float goldenRatioLargef(float in, uint32_t exponent) {
    if (exponent == 0) {
        return in;
    }
    else {
        return goldenRatioLargef(1/PHI * in, --exponent); 
    }
}

void horizontalMoveDone() {
    MainUi *ui = &offblast->mainUi;
        ui->activeRowset->rowCursor->tileCursor = 
            ui->activeRowset->rowCursor->movingToTile;
}

void verticalMoveDone() {
    MainUi *ui = &offblast->mainUi;
        ui->activeRowset->rowCursor = ui->activeRowset->movingToRow;
}

void updateGameInfo() {
        offblast->mainUi.titleText = 
            offblast->mainUi.activeRowset->movingToTarget->name;
        updateInfoText();
        updateDescriptionText();
        offblast->mainUi.rowNameText 
            = offblast->mainUi.activeRowset->movingToRow->name;
}

void infoFaded() {

    MainUi *ui = &offblast->mainUi;
    if (ui->infoAnimation->direction == 0) {
        updateGameInfo();

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

void rowNameFaded() {

    MainUi *ui = &offblast->mainUi;
    if (ui->rowNameAnimation->direction == 0) {
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

uint32_t animationRunning() {

    uint32_t result = 0;
    MainUi *ui = &offblast->mainUi;
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

void animationTick(Animation *theAnimation) {
        if (theAnimation->animating && SDL_GetTicks() > 
                theAnimation->startTick + theAnimation->durationMs) 
        {
            theAnimation->animating = 0;
            theAnimation->callback();
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
    else if (strcmp(key, "steam") == 0) {
        return "Steam";
    }

    return "Unknown Platform";
}


char *getCoverPath(LaunchTarget *target) {

    char *coverArtPath;
    char *homePath = getenv("HOME");
    assert(homePath);

    if (strcmp(target->platform, "steam") == 0) {
        coverArtPath = calloc(PATH_MAX, sizeof(char));
        asprintf(&coverArtPath, 
                "%s/.steam/steam/appcache/librarycache/%s_library_600x900.jpg", 
                homePath,
                target->id);
    }
    else {
        asprintf(&coverArtPath, "%s/.offblast/covers/%"PRIu64".jpg", homePath, 
                target->targetSignature); 
    }

    return coverArtPath;
}

void *imageLoadMain(void *arg) {
    LoaderContext *ctx = arg;

    while (1) {
        pthread_mutex_lock(&ctx->lock);

        for (uint32_t i=0; i < LOAD_QUEUE_LENGTH; ++i) {
            if(ctx->queue.items[i].status == LOAD_STATE_LOADING) {

                UiTile *tile = ctx->queue.items[i].tile;
                char *coverPath = getCoverPath(tile->target);
                int n;

                stbi_set_flip_vertically_on_load(1);
                tile->image.atlas = stbi_load(
                        coverPath,
                        (int*)&tile->image.width, (int*)&tile->image.height, 
                        &n, 4);

                free(coverPath);

                if(tile->image.atlas == NULL) {
                    printf("need to download %s\n", coverPath);
                    continue;
                }
                else {
                    ctx->queue.items[i].status = LOAD_STATE_FREE;
                    tile->image.state= IMAGE_STATE_READY;
                    break;
                }
            }
        }

        pthread_mutex_unlock(&ctx->lock);
        usleep(30);
    }

}

void *downloadMain(void *arg) {
    DownloaderContext *ctx = arg;

    char *homePath = getenv("HOME");
    assert(homePath);

    char *workingPath = calloc(PATH_MAX, sizeof(char));
    char *workingUrl = calloc(PATH_MAX, sizeof(char));

    while (1) {

        for (uint32_t i=0; i < DOWNLOAD_QUEUE_LENGTH; ++i) {
            pthread_mutex_lock(&ctx->lock);
            if (ctx->queue.items[i].status == DOWNLOAD_STATE_COLD) {
                
                snprintf(workingPath, 
                        PATH_MAX,
                        "%s/.offblast/covers/%"PRIu64".jpg",
                        homePath, 
                        ctx->queue.items[i].targetSignature); 

                snprintf(workingUrl, 
                        PATH_MAX,
                        "%s",
                        ctx->queue.items[i].url); 

                pthread_mutex_unlock(&ctx->lock);

                CurlFetch fetch = {};

                curl_global_init(CURL_GLOBAL_DEFAULT);
                CURL *curl = curl_easy_init();
                if (!curl) {
                    printf("CURL init fail.\n");
                    return NULL;
                }
                //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
                curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

                curl_easy_setopt(curl, CURLOPT_URL, workingUrl);

                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fetch);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);

                uint32_t res = curl_easy_perform(curl);
                curl_easy_cleanup(curl);

                if (res != CURLE_OK) {
                    // TODO how do we notify that it's dead?
                    printf("%s\n", curl_easy_strerror(res));
                    continue;
                } else {

                    int w, h, channels;
                    unsigned char *image = 
                        stbi_load_from_memory(
                                fetch.data, 
                                fetch.size, &w, &h, &channels, 4);

                    if (image == NULL) {
                        // TODO dead link?
                        printf("Couldnt load the image from memory\n");
                        printf("%s\n", workingUrl);
                        continue;
                    }

                    stbi_flip_vertically_on_write(1);
                    if (!stbi_write_jpg(workingPath, w, h, 4, image, 100)) {
                        free(image);
                        printf("Couldnt save JPG");
                    }
                    else {
                        free(image);
                    }
                }

                free(fetch.data);

                pthread_mutex_lock(&ctx->lock);
                ctx->queue.items[i].status = DOWNLOAD_STATE_READY;
                pthread_mutex_unlock(&ctx->lock);

                break;
            }
            else {
                pthread_mutex_unlock(&ctx->lock);
            }
        }

        usleep(30);
    }

    free(workingPath);
}

uint32_t powTwoFloor(uint32_t val) {
    uint32_t pow = 2;
    while (val > pow)
        pow *= 2;

    return pow;
}

void imageToGlTexture(GLuint *textureHandle, unsigned char *pixelData, 
        uint32_t newWidth, uint32_t newHeight) 
{
    glGenTextures(1, textureHandle);
    glBindTexture(GL_TEXTURE_2D, *textureHandle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, newWidth, newHeight,
            0, GL_RGBA, GL_UNSIGNED_BYTE, pixelData);
    glBindTexture(GL_TEXTURE_2D, 0);
}


GLint loadShaderFile(const char *path, GLenum shaderType) {

    GLint compStatus = GL_FALSE; 
    GLuint shader = glCreateShader(shaderType);

    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *shaderString = calloc(1, fsize + 1);
    fread(shaderString, 1, fsize, f);
    fclose(f);

    glShaderSource(shader, 1, (const char * const *)&shaderString, NULL);

    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compStatus);
    printf("Shader Compilation: %d - %s\n", compStatus, path);

    if (!compStatus) {
        GLint len;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, 
                &len);
        char *logString = calloc(1, len+1);
        glGetShaderInfoLog(shader, len, NULL, logString);
        printf("%s\n", logString);
        free(logString);
    }
    assert(compStatus);

    return shader;
}


GLuint createShaderProgram(GLint vertShader, GLint fragShader) {

    GLuint program = glCreateProgram();
    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);
    glLinkProgram(program);

    GLint programStatus;
    glGetProgramiv(program, GL_LINK_STATUS, &programStatus);
    printf("GL Program Status: %d\n", programStatus);
    if (!programStatus) {
        GLint len;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, 
                &len);
        char *logString = calloc(1, len+1);
        glGetProgramInfoLog(program, len, NULL, logString);
        printf("%s\n", logString);
        free(logString);
    }
    assert(programStatus);

    glDetachShader(program, vertShader);
    glDetachShader(program, fragShader);
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);

    return program;
}


void launch() {
    
    LaunchTarget *target = 
        offblast->mainUi.activeRowset->rowCursor->tileCursor->target;

    if (target->launcherSignature == 0) {
        printf("%s has no launcher \n", target->name);
    }

    int32_t foundIndex = -1;
    for (uint32_t i = 0; i < offblast->nLaunchers; ++i) {
        if (target->launcherSignature == 
                offblast->launchers[i].signature) 
        {
            foundIndex = i;
        }
    }

    if (foundIndex == -1) {
        printf("%s has no launcher\n", target->name);
        return;
    }

    Launcher *theLauncher = &offblast->launchers[foundIndex];
    int32_t isSteam = (strcmp(theLauncher->type, "steam") == 0);
    int32_t isScummvm = (strcmp(theLauncher->type, "scummvm") == 0);

    if (!isSteam && !isScummvm && strlen(target->path) == 0) {
        printf("%s has no launch candidate\n", target->name);
    }
    else {

        User *theUser = offblast->player.user;



        char *launchString = calloc(PATH_MAX, sizeof(char));

        if (isSteam) {
            asprintf(&launchString, "steam -silent -applaunch %s",
                    target->id);
        }
        else if (isScummvm) {
            if (theUser->savePath) {
                asprintf(&launchString, "SDL_AUDIODRIVER=alsa scummvm -f --savepath='%s' %s",
                        theUser->savePath,
                        target->path);
            }
            else {
                asprintf(&launchString, "SDL_AUDIODRIVER=alsa scummvm -f %s",
                        target->path);
            }
        }
        else {

            memcpy(launchString, 
                    theLauncher->cmd, 
                    strlen(theLauncher->cmd));

            assert(strlen(launchString));

            char *p;
            uint8_t replaceIter = 0, replaceLimit = 8;
            while ((p = strstr(launchString, "%ROM%"))) {

                memmove(
                        p + strlen(target->path) + 2, 
                        p + 5,
                        strlen(p));

                *p = '"';
                memcpy(p+1, target->path, strlen(target->path));
                *(p + 1 + strlen(target->path)) = '"';

                replaceIter++;
                if (replaceIter >= replaceLimit) {
                    printf("rom replace iterations exceeded, breaking\n");
                    break;
                }
            }

            if (strlen(theUser->savePath) != 0) {
                replaceIter = 0; replaceLimit = 8;

                while ((p = strstr(launchString, "%SAVE_PATH%"))) {

                    memmove(
                            p + strlen(theUser->savePath) + 2, 
                            p + strlen("%SAVE_PATH%"),
                            strlen(p));

                    *p = '"';
                    memcpy(p+1, theUser->savePath, strlen(theUser->savePath));
                    *(p + 1 + strlen(theUser->savePath)) = '"';

                    replaceIter++;
                    if (replaceIter >= replaceLimit) {
                        printf("save path iter exceeded, breaking\n");
                        break;
                    }
                }
            }

            if (strlen(theUser->dolphinCardPath) != 0) {
                replaceIter = 0; replaceLimit = 8;

                while ((p = strstr(launchString, "%DOLPHIN_CARD%"))) {

                    memmove(
                            p + strlen(theUser->dolphinCardPath), 
                            p + strlen("%DOLPHIN_CARD%"),
                            strlen(p));

                    memcpy(p, theUser->dolphinCardPath, strlen(theUser->dolphinCardPath));

                    replaceIter++;
                    if (replaceIter >= replaceLimit) {
                        printf("dolphin path iter exceeded, breaking\n");
                        break;
                    }
                }
            }

            if (strcmp(theLauncher->type, "cemu") == 0) {

                assert(strlen(theLauncher->cemuPath));
                char *cemuBinSlug;

                asprintf(&cemuBinSlug, "%s/Cemu.exe", theLauncher->cemuPath);

                replaceIter = 0; replaceLimit = 8;
                while ((p = strstr(launchString, "%CEMU_BIN%"))) {

                    memmove(
                            p + strlen(cemuBinSlug) + 2, 
                            p + strlen("%CEMU_BIN%"),
                            strlen(p));

                    *p = '"';
                    memcpy(p+1, cemuBinSlug, strlen(cemuBinSlug));
                    *(p + 1 + strlen(cemuBinSlug)) = '"';

                    replaceIter++;
                    if (replaceIter >= replaceLimit) {
                        printf("cemu path replace iterations exceeded, breaking\n");
                        break;
                    }
                }
                free(cemuBinSlug);

                char *cemuAccountSlug;
                if (theUser->cemuAccount != NULL) {
                    cemuAccountSlug = (char*)theUser->cemuAccount;
                }
                else {
                    cemuAccountSlug = "80000001";
                }

                // TODO write a function for this
                while ((p = strstr(launchString, "%CEMU_ACCOUNT%"))) {

                    memmove(
                            p + strlen(cemuAccountSlug) + 2, 
                            p + strlen("%CEMU_ACCOUNT%"),
                            strlen(p));

                    *p = '"';
                    memcpy(p+1, cemuAccountSlug, 
                            strlen(cemuAccountSlug));

                    *(p + 1 + strlen(cemuAccountSlug)) = '"';

                    replaceIter++;
                    if (replaceIter >= replaceLimit) {
                        printf("cemu account replace iterations exceeded, breaking\n");
                        break;
                    }
                }

            }

            if (strcmp(theLauncher->type, "retroarch") == 0 
                    && strlen(theUser->retroarchConfig) != 0) 
            {

                replaceIter = 0; replaceLimit = 8;

                // TODO write a function to replace this stuff
                while ((p = strstr(launchString, "%RETROARCH_CONFIG%"))) {

                    memmove(
                            p + strlen(theUser->retroarchConfig) + 2, 
                            p + strlen("%RETROARCH_CONFIG%"),
                            strlen(p));

                    *p = '"';
                    memcpy(p+1, theUser->retroarchConfig, strlen(theUser->retroarchConfig));
                    *(p + 1 + strlen(theUser->retroarchConfig)) = '"';

                    replaceIter++;
                    if (replaceIter >= replaceLimit) {
                        printf("retroarch configi iter exceeded, breaking\n");
                        break;
                    }
                }
            }

        }

        // TODO detect when the command errors or doesn't exist and handle it
        printf("OFFBLAST! %s\n", launchString);

        char *commandStr;
        switch (offblast->windowManager) {
            case WINDOW_MANAGER_I3:
                asprintf(&commandStr, "i3-msg workspace blastgame, exec '%s'", 
                        launchString);
                break;
            default:
                asprintf(&commandStr, "%s", launchString);
                break;
        }
        
        pid_t launcherPid = fork();
            
        if (launcherPid == -1)
        {
            printf("Couldn't fork the process\n");
        }
        else if (launcherPid > 0) {

            offblast->loadingFlag = 1;
            offblast->mode = OFFBLAST_UI_MODE_BACKGROUND;
            offblast->startPlayTick = SDL_GetTicks();
            offblast->runningPid = launcherPid;
            offblast->playingTarget = target;

            printf("**** PID of child, %d\n", launcherPid);
        }
        else {
            setsid();
            printf("RUNNING\n%s\n", commandStr);
            system(commandStr);
            exit(1);
        }

        free(launchString);
        free(commandStr);

    }
}

void pressSearch(int32_t joystickIndex) {

    if (offblast->mode == OFFBLAST_UI_MODE_PLAYER_SELECT) {}
    else if (offblast->mainUi.showSearch) {
        offblast->mainUi.activeRowset = offblast->mainUi.homeRowset;
        offblast->mainUi.showSearch = 0;
    }
    else if (offblast->mode == OFFBLAST_UI_MODE_MAIN) {
        offblast->mainUi.showMenu = 0;
        doSearch();
    }
}

void pressConfirm(int32_t joystickIndex) {

    if (offblast->mode == OFFBLAST_UI_MODE_BACKGROUND) {
        if(activeWindowIsOffblast() && !offblast->loadingFlag) {
            if(offblast->uiStopButtonHot) 
            {
                killRunningGame();
            }
            else {
                printf("Resume the current game on window %lu \n", 
                    offblast->resumeWindow);
                raiseWindow(offblast->resumeWindow);
            }
        }
    }
    else if (offblast->mode == OFFBLAST_UI_MODE_PLAYER_SELECT) {

        User *theUser = &offblast->users[offblast->playerSelectUi.cursor];

        char *email = theUser->email;
        uint32_t emailSignature = 0;

        lmmh_x86_32(email, strlen(email), 33, 
                &emailSignature);

        printf("player selected %d: %s\n%s\n%u\n", 
                offblast->playerSelectUi.cursor,
                theUser->name,
                theUser->email,
                emailSignature
                );

        printf("joystick %d\n", joystickIndex);

        offblast->player.user = theUser;
        offblast->player.emailHash = emailSignature;
        loadPlaytimeFile();
        updateHomeLists();

        if (joystickIndex > -1) {
            offblast->player.jsIndex = joystickIndex;
            printf("Controller: %s\nAdded to Player\n",
                    SDL_GameControllerNameForIndex(joystickIndex));
        }

        offblast->mode = OFFBLAST_UI_MODE_MAIN;
    }
    else if (offblast->mainUi.showSearch) {
        // TODO not sure I like the way I'm using mode, I think maybe things
        // shouldn't be so modal..
        if (offblast->searchCursor < OFFBLAST_MAX_SEARCH) {

            if (offblast->searchCurChar == '_') {
                offblast->searchTerm[offblast->searchCursor] = ' ';
                offblast->searchCursor++;
            }
            else if (offblast->searchCurChar == '<') {
                if (offblast->searchCursor > 0){
                    offblast->searchCursor--;
                    offblast->searchTerm[offblast->searchCursor] = 0;
                }
            }
            else {
                offblast->searchTerm[offblast->searchCursor] = 
                    offblast->searchCurChar;
                offblast->searchCursor++;
            }

            updateResults();
        }

    }
    else if (offblast->mode == OFFBLAST_UI_MODE_MAIN) {
        if (offblast->mainUi.showMenu) {
            void (*callback)() = 
                offblast->mainUi.menuItems[offblast->mainUi.menuCursor].callback;
            void *callbackArgs = 
                offblast->mainUi.menuItems[offblast->mainUi.menuCursor].callbackArgs;

            if (callback == NULL) 
                printf("menu null backback!\n");
            else {
                offblast->mainUi.showMenu = 0;
                callback(callbackArgs);
            }

        }
        else 
            launch();
    }
}

void pressCancel() {
    if (offblast->mainUi.showSearch) {
        offblast->mainUi.showSearch = 0;
    }
}

void updateInfoText() {

    if (offblast->mainUi.infoText != NULL) {
        free(offblast->mainUi.infoText);
    }

    char *infoString;
    LaunchTarget *target = offblast->mainUi.activeRowset->movingToTarget;

    asprintf(&infoString, "%.4s  |  %s  |  %u%%", 
            target->date, 
            platformString(target->platform),
            target->ranking);

    offblast->mainUi.infoText = infoString;
}

void updateDescriptionText() {
    OffblastBlob *descriptionBlob = 
    (OffblastBlob*) &offblast->descriptionFile->memory[
       offblast->mainUi.activeRowset->movingToTarget->descriptionOffset];

    offblast->mainUi.descriptionText = descriptionBlob->content;
}


size_t curlWrite(void *contents, size_t size, size_t nmemb, void *userP)
{
    size_t realSize = size * nmemb;
    CurlFetch *fetch = (CurlFetch *)userP;

    // TODO why add one byte?
    fetch->data = realloc(fetch->data, fetch->size + realSize);

    if (fetch->data == NULL) {
        printf("Error: couldn't expand cover buffer\n");
        free(fetch->data);
        return -1;
    }

    memcpy(&(fetch->data[fetch->size]), contents, realSize);
    fetch->size += realSize;
    //fetch->data[fetch->size] = 0;

    return realSize;
}


int playTimeSort(const void *a, const void *b) {

    PlayTime *ra = (PlayTime*) a;
    PlayTime *rb = (PlayTime*) b;

    if (ra->msPlayed < rb->msPlayed)
        return -1;
    else if (ra->msPlayed > rb->msPlayed)
        return +1;
    else
        return 0;
}

int lastPlayedSort(const void *a, const void *b) {

    PlayTime *ra = (PlayTime*) a;
    PlayTime *rb = (PlayTime*) b;

    if (ra->lastPlayed < rb->lastPlayed)
        return -1;
    else if (ra->lastPlayed > rb->lastPlayed)
        return +1;
    else
        return 0;
}


uint32_t getTextLineWidth(char *string, stbtt_bakedchar* cdata) {

    uint32_t width = 0;

    for (uint32_t i = 0; i < strlen(string); ++i) {
        int arrOffset = *(string + i) -32;
        stbtt_bakedchar *b = 
            (stbtt_bakedchar*) cdata + arrOffset;

        width += b->xadvance;
    }

    return width;
}


void renderText(OffblastUi *offblast, float x, float y, 
        uint32_t textMode, float alpha, uint32_t lineMaxW, char *string) 
{

    glUseProgram(offblast->textProgram);
    glEnable(GL_TEXTURE_2D);

    uint32_t currentLine = 0;
    float currentWidth = 0;
    uint32_t lineHeight = 0;
    float originalX = x;

    void *cdata = NULL;

    switch (textMode) {
        case OFFBLAST_TEXT_TITLE:
            glBindTexture(GL_TEXTURE_2D, offblast->titleTextTexture);
            cdata = offblast->titleCharData;
            lineHeight = offblast->titlePointSize * 1.2;
            break;

        case OFFBLAST_TEXT_INFO:
            glBindTexture(GL_TEXTURE_2D, offblast->infoTextTexture);
            cdata = offblast->infoCharData;
            lineHeight = offblast->infoPointSize * 1.2;
            break;

        case OFFBLAST_TEXT_DEBUG:
            glBindTexture(GL_TEXTURE_2D, offblast->debugTextTexture);
            cdata = offblast->debugCharData;
            lineHeight = offblast->debugPointSize * 1.2;
            break;

        default:
            return;
    }

    float winWidth = (float)offblast->winWidth;
    float winHeight = (float)offblast->winHeight;
    y = winHeight - y;

    char *trailingString = NULL;

    for (uint32_t i= 0; *string; ++i) {
        if (*string >= 32 && *string < 128) {

            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(cdata,
                    offblast->textBitmapWidth, offblast->textBitmapHeight, 
                    *string-32, &x, &y, &q, 1);

            currentWidth += (q.x1 - q.x0);

            if (lineMaxW > 0 && trailingString == NULL) {

                float wordWidth = 0.0f;
                if (*(string) == ' ') {

                    uint32_t curCharOffset = 1;
                    wordWidth = 0.0f;

                    while (1) {
                        if (*(string + curCharOffset) == ' ' ||
                                *(string + curCharOffset) == 0) break;

                        int arrOffset = *(string + curCharOffset) -32;
                        stbtt_bakedchar *b = 
                            (stbtt_bakedchar*) cdata + arrOffset;

                        wordWidth += b->xadvance;
                        curCharOffset++;
                    }

                }

                if (currentWidth + (int)(wordWidth + 0.5f) > lineMaxW) {

                    if (currentLine >= 6) {
                        trailingString = "...";
                        string = trailingString;
                        continue;
                    }

                    ++currentLine;
                    currentWidth = q.x1 - q.x0;

                    x = originalX;
                    y += lineHeight;
                }
            }


            float left = -1 + (2/winWidth * q.x0);
            float right = -1 + (2/winWidth * q.x1);
            float top = -1 + (2/winHeight * (winHeight - q.y0));
            float bottom = -1 + (2/winHeight * (winHeight -q.y1));
            float texLeft = q.s0;
            float texRight = q.s1;
            float texTop = q.t0;
            float texBottom = q.t1;

            Quad quad = {};
            initQuad(&quad);

            quad.vertices[0].x = left;
            quad.vertices[0].y = bottom;
            quad.vertices[0].tx = texLeft;
            quad.vertices[0].ty = texBottom;

            quad.vertices[1].x = left;
            quad.vertices[1].y = top;
            quad.vertices[1].tx = texLeft;
            quad.vertices[1].ty = texTop;

            quad.vertices[2].x = right;
            quad.vertices[2].y = top;
            quad.vertices[2].tx = texRight;
            quad.vertices[2].ty = texTop;

            quad.vertices[3].x = right;
            quad.vertices[3].y = top;
            quad.vertices[3].tx = texRight;
            quad.vertices[3].ty = texTop;

            quad.vertices[4].x = right;
            quad.vertices[4].y = bottom;
            quad.vertices[4].tx = texRight;
            quad.vertices[4].ty = texBottom;

            quad.vertices[5].x = left;
            quad.vertices[5].y = bottom;
            quad.vertices[5].tx = texLeft;
            quad.vertices[5].ty = texBottom;

            if (offblast->textVbo == 0) {
                glGenBuffers(1, &offblast->textVbo);
                glBindBuffer(GL_ARRAY_BUFFER, offblast->textVbo);
                glBufferData(GL_ARRAY_BUFFER, sizeof(Quad), 
                        &quad.vertices, GL_STREAM_DRAW);
            }
            else {
                glBindBuffer(GL_ARRAY_BUFFER, offblast->textVbo);
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(Quad), 
                        &quad.vertices);
            }

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 
                    (void*)(4*sizeof(float)));

            if (trailingString) {
                alpha *= 0.85;
            }

            glUniform1f(offblast->textAlphaUni, alpha);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            glUniform1f(offblast->textAlphaUni, 1.0f);
        }

        ++string;
    }

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glUseProgram(0);
}

void initQuad(Quad* quad) {
    for (uint32_t i = 0; i < 6; ++i) {
        quad->vertices[i].x = 0.0f;
        quad->vertices[i].y = 0.0f;
        quad->vertices[i].z = 0.0f;
        quad->vertices[i].s = 1.0f;

        if (i == 0) {
            quad->vertices[i].tx = 0.0f;
            quad->vertices[i].ty = 0.0f;
        }
        if (i == 1) {
            quad->vertices[i].tx = 0.0f;
            quad->vertices[i].ty = 1.0f;
        }
        if (i == 2) {
            quad->vertices[i].tx = 1.0f;
            quad->vertices[i].ty = 1.0f;
        }
        if (i == 3) {
            quad->vertices[i].tx = 1.0f;
            quad->vertices[i].ty = 1.0f;
        }
        if (i == 4) {
            quad->vertices[i].tx = 1.0f;
            quad->vertices[i].ty = 0.0f;
        }
        if (i == 5) {
            quad->vertices[i].tx = 0.0f;
            quad->vertices[i].ty = 0.0f;
        }

        // TODO should probably init the color to black
    }
}

void resizeQuad(float x, float y, float w, float h, Quad *quad) {

    float left = -1.0f + (2.0f/offblast->winWidth * x);
    float bottom = -1.0f + (2.0f/offblast->winHeight * y);
    float right = -1.0f + (2.0f/offblast->winWidth * (x+w));
    float top = -1.0f + (2.0f/offblast->winHeight * (y+h));

    quad->vertices[0].x = left;
    quad->vertices[0].y = bottom;

    quad->vertices[1].x = left;
    quad->vertices[1].y = top;

    quad->vertices[2].x = right;
    quad->vertices[2].y = top;

    quad->vertices[3].x = right;
    quad->vertices[3].y = top;

    quad->vertices[4].x = right;
    quad->vertices[4].y = bottom;

    quad->vertices[5].x = left;
    quad->vertices[5].y = bottom;
}

void renderGradient(float x, float y, float w, float h, 
        uint32_t horizontal, Color colorStart, Color colorEnd) 
{

    Quad quad = {};
    initQuad(&quad);
    resizeQuad(x, y, w, h, &quad);

    if (horizontal) {
        quad.vertices[0].color = colorStart;
        quad.vertices[1].color = colorStart;
        quad.vertices[2].color = colorEnd;
        quad.vertices[3].color = colorEnd;
        quad.vertices[4].color = colorEnd;
        quad.vertices[5].color = colorStart;
    }
    else {
        quad.vertices[0].color = colorStart;
        quad.vertices[1].color = colorEnd;
        quad.vertices[2].color = colorEnd;
        quad.vertices[3].color = colorEnd;
        quad.vertices[4].color = colorStart;
        quad.vertices[5].color = colorStart;
    }

    // TODO if the h and w haven't changed we don't actually
    // need to rebuffer the vertex data, we could just use a uniform
    // for the vertex shader?

    if (!offblast->gradientVbo) {
        glGenBuffers(1, &offblast->gradientVbo);
        glBindBuffer(GL_ARRAY_BUFFER, offblast->gradientVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(Quad), 
                &quad, GL_STREAM_DRAW);
    }
    else {
        glBindBuffer(GL_ARRAY_BUFFER, offblast->gradientVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(Quad), 
                &quad);
    }

    glUniform4f(offblast->gradientColorStartUniform, 
            colorStart.r, colorStart.g, colorStart.b, colorStart.a);
    glUniform4f(offblast->gradientColorEndUniform, 
            colorEnd.r, colorEnd.g, colorEnd.b, colorEnd.a);

    glUseProgram(offblast->gradientProgram);
    glBindBuffer(GL_ARRAY_BUFFER, offblast->gradientVbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 
            sizeof(Vertex), 0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 
            sizeof(Vertex), (void*)(6*sizeof(float)));

    glDrawArrays(GL_TRIANGLES, 0, 6);
}

float getWidthForScaledImage(float scaledHeight, Image *image) {
    if (image->height == 0) {
        return 0;
    }
    else {
        float exponent = scaledHeight / image->height;
        return image->width * exponent;
    }
}

void renderImage(float x, float y, float w, float h, Image* image,
        float desaturation, float alpha) 
{

    glUseProgram(offblast->imageProgram);
    Quad quad = {};
    initQuad(&quad);

    w = getWidthForScaledImage(h, image);

    resizeQuad(x, y, w, h, &quad);

    if (!offblast->mainUi.imageVbo) {
        glGenBuffers(1, &offblast->mainUi.imageVbo);
        glBindBuffer(GL_ARRAY_BUFFER, offblast->mainUi.imageVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(Quad), 
                &quad, GL_STREAM_DRAW);
    }
    else {
        glBindBuffer(GL_ARRAY_BUFFER, offblast->mainUi.imageVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(Quad), 
                &quad);
    }

    glUniform1f(offblast->imageDesaturateUni, desaturation);
    glUniform1f(offblast->imageAlphaUni, alpha);

    glBindTexture(GL_TEXTURE_2D, image->textureHandle);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 
            sizeof(Vertex), 0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 
            sizeof(Vertex), (void*)(4*sizeof(float)));

    // TODO remove this uniform
    //glUniform2f(offblast->imageTranslateUni, 0, 0);

    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void loadTexture(UiTile *tile) {

    // Generate the texture 
    if (tile->image.textureHandle == 0 &&
            tile->target->coverUrl != NULL) 
    {
        if (tile->image.state == IMAGE_STATE_COLD) {

            char *coverArtPath = getCoverPath(tile->target); 
            struct stat fileInfo;
            if (lstat(coverArtPath, &fileInfo) == 0) {

                pthread_mutex_lock(&offblast->downloader.lock);

                for (uint32_t i=0; i < LOAD_QUEUE_LENGTH; ++i) {
                    if (offblast->imageLoader.queue.items[i].status 
                            == LOAD_STATE_FREE) 
                    {
                        offblast->imageLoader.queue.items[i].tile = tile;
                        offblast->imageLoader.queue.items[i].status = 
                            LOAD_STATE_LOADING;

                        break;
                    }
                }

                pthread_mutex_unlock(&offblast->downloader.lock);
            }
            else {
                // ENQUEUE
                pthread_mutex_lock(&offblast->downloader.lock);
                for (uint32_t i=0; i < DOWNLOAD_QUEUE_LENGTH; ++i) {
                    if (offblast->downloader.queue.items[i].status 
                            == DOWNLOAD_STATE_READY) 
                    {

                        if (strcmp(tile->target->platform, "steam") == 0) {
                            snprintf(
                                    (char *) &offblast->downloader.queue.items[i].url,
                                    PATH_MAX,
                                    "https://steamcdn-a.akamaihd.net/steam/apps/%s/library_600x900.jpg", 
                                    tile->target->id);
                        }
                        else {
                            snprintf(
                                    (char *) &offblast->downloader.queue.items[i].url,
                                    PATH_MAX,
                                    "%s", 
                                    (char *) tile->target->coverUrl);
                        }

                        offblast->downloader.queue.items[i].targetSignature = 
                            tile->target->targetSignature;

                        offblast->downloader.queue.items[i].status = 
                            DOWNLOAD_STATE_COLD;

                        break;
                    }
                }
                pthread_mutex_unlock(&offblast->downloader.lock);
            }

            free(coverArtPath);
        }
        else if (tile->image.state == IMAGE_STATE_READY) {

            glGenTextures(1, &tile->image.textureHandle);
            imageToGlTexture(
                    &tile->image.textureHandle,
                    tile->image.atlas, 
                    tile->image.width,
                    tile->image.height);

            glBindTexture(GL_TEXTURE_2D, 
                    tile->image.textureHandle);

            // TODO mutex lock
            tile->image.state = IMAGE_STATE_COMPLETE;
            free(tile->image.atlas);
            tile->image.atlas = NULL;

            offblast->mainUi.rowGeometryInvalid = 1;
        }
    }
}

void updateHomeLists(){

    LaunchTargetFile *launchTargetFile = offblast->launchTargetFile;
    MainUi *mainUi = &offblast->mainUi;

    mainUi->homeRowset->numRows = 0;
    // TODO do I need to free each row's tileset?

    // __ROW__ "Jump back in" 
    size_t playTimeFileSize = sizeof(PlayTimeFile) + 
        offblast->playTimeFile->nEntries * sizeof(PlayTime);
    PlayTimeFile *tempFile = malloc(playTimeFileSize);
    assert(tempFile);
    memcpy(tempFile, offblast->playTimeFile, playTimeFileSize);
    if (offblast->playTimeFile->nEntries) {

        uint32_t tileLimit = 25;
        UiTile *tiles = calloc(tileLimit, sizeof(UiTile));
        assert(tiles);

        uint32_t tileCount = 0;

        qsort(tempFile->entries, tempFile->nEntries, 
               sizeof(PlayTime),
               lastPlayedSort);

        // Sort
        for (int32_t i = tempFile->nEntries-1; i >= 0; --i) {

            PlayTime* pt = (PlayTime*) &tempFile->entries[i];
            int32_t targetIndex = launchTargetIndexByTargetSignature(
                    launchTargetFile,
                    pt->targetSignature);

            LaunchTarget *target = &launchTargetFile->entries[targetIndex];
            tiles[tileCount].target = target;
            tiles[tileCount].next = &tiles[tileCount+1];
            if (tileCount != 0) 
                tiles[tileCount].previous = &tiles[tileCount-1];

            tileCount++;
            if (tileCount >= tileLimit) break;
        }

        if (tileCount > 0) {

            tiles[tileCount-1].next = NULL;
            tiles[0].previous = NULL;

            if (mainUi->homeRowset->rows[mainUi->homeRowset->numRows].tiles 
                    != NULL) {
                free(mainUi->homeRowset->rows[mainUi->homeRowset->numRows].tiles);
            }

            mainUi->homeRowset->rows[mainUi->homeRowset->numRows].tiles = tiles; 
            mainUi->homeRowset->rows[mainUi->homeRowset->numRows].tileCursor 
                = tiles;
            memset(mainUi->homeRowset->rows[mainUi->homeRowset->numRows].name, 0x0, 256);
            strcpy(mainUi->homeRowset->rows[mainUi->homeRowset->numRows].name, 
                    "Jump Back In");
            mainUi->homeRowset->rows[mainUi->homeRowset->numRows].length 
                = tileCount; 
            mainUi->homeRowset->numRows++;
        }
    }

    // __ROW__ "Most played" 
    if (offblast->playTimeFile->nEntries) {

        uint32_t tileLimit = 25;
        UiTile *tiles = calloc(tileLimit, sizeof(UiTile));
        assert(tiles);

        uint32_t tileCount = 0;

        qsort(tempFile->entries, tempFile->nEntries, 
               sizeof(PlayTime),
               playTimeSort);

        // Sort
        for (int32_t i = tempFile->nEntries-1; i >= 0; --i) {

            PlayTime* pt = (PlayTime*) &tempFile->entries[i];
            int32_t targetIndex = launchTargetIndexByTargetSignature(
                    launchTargetFile,
                    pt->targetSignature);

            LaunchTarget *target = &launchTargetFile->entries[targetIndex];
            tiles[tileCount].target = target;
            tiles[tileCount].next = &tiles[tileCount+1];
            if (tileCount != 0) 
                tiles[tileCount].previous = &tiles[tileCount-1];

            tileCount++;
            if (tileCount >= tileLimit) break;
        }


        if (tileCount > 0) {

            tiles[tileCount-1].next = NULL;
            tiles[0].previous = NULL;

            if (mainUi->homeRowset->rows[mainUi->homeRowset->numRows].tiles 
                    != NULL) {
                free(mainUi->homeRowset->rows[mainUi->homeRowset->numRows].tiles);
            }

            mainUi->homeRowset->rows[mainUi->homeRowset->numRows].tiles 
                = tiles; 
            mainUi->homeRowset->rows[mainUi->homeRowset->numRows].tileCursor 
                = tiles;
            memset(mainUi->homeRowset->rows[mainUi->homeRowset->numRows].name, 0x0, 256);
            strcpy(mainUi->homeRowset->rows[mainUi->homeRowset->numRows].name, 
                    "Most played");
            mainUi->homeRowset->rows[mainUi->homeRowset->numRows].length 
                = tileCount; 
            mainUi->homeRowset->numRows++;
        }
    }
    free(tempFile);

    // __ROW__ "Your Library"
    uint32_t libraryLength = 0;
    for (uint32_t i = 0; i < launchTargetFile->nEntries; ++i) {
        LaunchTarget *target = &launchTargetFile->entries[i];
        if (target->launcherSignature != 0) 
            libraryLength++;
    }

    if (libraryLength > 0) {

        uint32_t tileLimit = 25;
        UiTile *tiles = calloc(tileLimit, sizeof(UiTile));
        assert(tiles);

        uint32_t tileCount = 0;

        for (uint32_t i = launchTargetFile->nEntries-1; i > 0; --i) {

            LaunchTarget *target = &launchTargetFile->entries[i];

            if (target->launcherSignature != 0) {

                tiles[tileCount].target = target;
                tiles[tileCount].next = &tiles[tileCount+1];
                if (tileCount != 0) 
                    tiles[tileCount].previous = &tiles[tileCount-1];

                tileCount++;
                if (tileCount >= tileLimit) break;
            }
        }

        if (tileCount > 0) {

            tiles[tileCount-1].next = NULL;
            tiles[0].previous = NULL;

            if (mainUi->homeRowset->rows[mainUi->homeRowset->numRows].tiles 
                    != NULL) {
                free(mainUi->homeRowset->rows[mainUi->homeRowset->numRows].tiles);
            }

            mainUi->homeRowset->rows[mainUi->homeRowset->numRows].tiles 
                = tiles; 
            mainUi->homeRowset->rows[mainUi->homeRowset->numRows].tileCursor 
                = tiles;
            memset(mainUi->homeRowset->rows[mainUi->homeRowset->numRows].name, 0x0, 256);
            strcpy(mainUi->homeRowset->rows[mainUi->homeRowset->numRows].name, 
                    "Recently Installed");
            mainUi->homeRowset->rows[mainUi->homeRowset->numRows].length 
                = tileCount; 
            mainUi->homeRowset->numRows++;
        }
    }
    else { 
        printf("woah now looks like we have an empty library\n");
    }


    // __ROWS__ Essentials per platform 
    for (uint32_t iPlatform = 0; iPlatform < offblast->nPlatforms; iPlatform++) {

        uint32_t isSteam = 0;

        if (strcmp(offblast->platforms[iPlatform], "steam") == 0) {
            isSteam = 1;
        }

        uint32_t topRatedMax = 25;
        UiTile *tiles = calloc(topRatedMax, sizeof(UiTile));
        assert(tiles);

        uint32_t numTiles = 0;
        for (uint32_t i = 0; i < launchTargetFile->nEntries; ++i) {

            LaunchTarget *target = &launchTargetFile->entries[i];

            if (strcmp(target->platform, offblast->platforms[iPlatform]) == 0) {
                
                if (isSteam && target->launcherSignature == 0) {
                    continue;
                }

                tiles[numTiles].target = target; 
                tiles[numTiles].next = &tiles[numTiles+1]; 

                if (numTiles != 0) 
                    tiles[numTiles].previous = &tiles[numTiles-1];

                numTiles++;
            }

            if (numTiles >= topRatedMax) break;
        }

        if (numTiles > 0) {
            tiles[numTiles-1].next = NULL;
            tiles[0].previous = NULL;

            if (mainUi->homeRowset->rows[mainUi->homeRowset->numRows].tiles 
                    != NULL) {
                free(mainUi->homeRowset->rows[mainUi->homeRowset->numRows].tiles);
            }

            mainUi->homeRowset->rows[mainUi->homeRowset->numRows].tiles 
                = tiles;

            memset(mainUi->homeRowset->rows[mainUi->homeRowset->numRows].name, 0x0, 256);
            snprintf(
                    mainUi->homeRowset->rows[mainUi->homeRowset->numRows].name, 
                    255,
                    "Essential %s", 
                    platformString(offblast->platforms[iPlatform]));

            mainUi->homeRowset->rows[mainUi->homeRowset->numRows].tileCursor 
                = tiles;
            mainUi->homeRowset->rows[mainUi->homeRowset->numRows].length 
                = numTiles;
            mainUi->homeRowset->numRows++;
        }
        else {
            printf("no games for platform!!!\n");
            free(tiles);
        }
    }


    UiRowset *homeRowset = mainUi->homeRowset;
    for (uint32_t i = 0; i < mainUi->homeRowset->numRows; ++i) {
        if (i == 0) {
            homeRowset->rows[i].previousRow 
                = &homeRowset->rows[homeRowset->numRows-1];
        }
        else {
            homeRowset->rows[i].previousRow 
                = &homeRowset->rows[i-1];
        }

        if (i == homeRowset->numRows - 1) {
            homeRowset->rows[i].nextRow = &homeRowset->rows[0];
        }
        else {
            homeRowset->rows[i].nextRow = &homeRowset->rows[i+1];
        }
    }

    mainUi->homeRowset->movingToTarget = 
        mainUi->homeRowset->rowCursor->tileCursor->target;

    mainUi->homeRowset->movingToRow = mainUi->homeRowset->rowCursor;

    // Initialize the text to render
    offblast->mainUi.titleText = mainUi->homeRowset->movingToTarget->name;
    updateInfoText();
    updateDescriptionText();
    offblast->mainUi.rowNameText 
        = mainUi->homeRowset->movingToRow->name;
}

void updateResults(uint32_t *launcherSignature) {

    MainUi *mainUi = &offblast->mainUi;
    if (!launcherSignature && !strlen(offblast->searchTerm)) {
        mainUi->searchRowset->numRows = 0;
        mainUi->activeRowset = mainUi->homeRowset;
    }
    else {
        mainUi->activeRowset = mainUi->searchRowset;
    }

    LaunchTargetFile* targetFile = offblast->launchTargetFile;

    // TODO let's assume 250 results for now XXX
    UiTile *tiles = calloc(250, sizeof(UiTile));
    assert(tiles);

    uint32_t tileCount = 0;

    for (int i = 0; i < targetFile->nEntries; ++i) {

        uint32_t isMatch = 0;
        if (!launcherSignature
                && strcasestr(targetFile->entries[i].name, offblast->searchTerm)) 
        {
            isMatch = 1;
        }
        else if (launcherSignature  
                && targetFile->entries[i].launcherSignature == *launcherSignature) 
        {
            isMatch = 1;
        }

        if (isMatch) {
            if (tileCount >= 249) {
                printf("More than 250 results!\n");
                printf("\n");
                break;
            }

            uint32_t slottedIn = 0;
            for (int j=0; j < tileCount; ++j) {

                if (strcoll(targetFile->entries[i].name, tiles[j].target->name) 
                        <= 0) 
                {
                    if (tileCount >= 249) break;
                    uint32_t hanging = tileCount - j;
                    memmove(&tiles[j+1], &tiles[j], sizeof(UiTile) * hanging);
                    tileCount++;

                    LaunchTarget *target = &targetFile->entries[i];
                    tiles[j].target = target;
                    slottedIn=1;

                    break;
                }
            }

            if (!slottedIn) {
                LaunchTarget *target = &targetFile->entries[i];
                tiles[tileCount].target = target;
                tiles[tileCount].next = &tiles[tileCount+1];
                tiles[tileCount].previous = &tiles[tileCount-1];
                tileCount++;
            }

        }
    }

    if (tileCount > 0) {

        free(mainUi->searchRowset->rows[0].tiles);

        int32_t onRow = -1;
        uint32_t onTile = 0;
        mainUi->searchRowset->rows[0].tiles = tiles; 
        mainUi->searchRowset->rows[0].tileCursor = tiles;
        mainUi->searchRowset->numRows = 1;

        for (onTile = 0; onTile < tileCount; ++onTile) {

            if (onTile % 25 == 0) {

                // Update the last letter of the previous row
                if (onTile != 0) {
                    mainUi->searchRowset->rows[onRow].name[21] = 
                        tiles[onTile-1].target->name[0];
                    mainUi->searchRowset->numRows++;
                }

                onRow++;

                memset(mainUi->searchRowset->rows[onRow].name, 0x0, 256);
                //strcpy(mainUi->searchRowset->rows[onRow].name, "Search Results");
                snprintf(
                        mainUi->searchRowset->rows[onRow].name,
                        strlen("Search Results (a to z) "),
                        "Search Results (%c to !)",
                        tiles[onTile].target->name[0]
                        );


                // previous
                if (onRow > 0) {
                    mainUi->searchRowset->rows[onRow-1].length = 25; 
                }

                mainUi->searchRowset->rows[onRow].tiles = &tiles[onTile]; 
                mainUi->searchRowset->rows[onRow].tileCursor = &tiles[onTile];

                if (onTile > 0) tiles[onTile-1].next = NULL;
                tiles[onTile].previous = NULL;
                
            }
            else {
                if (onTile != 0)
                    tiles[onTile].previous = &tiles[onTile-1];
            }

            if (onTile+1 != tileCount)
                tiles[onTile].next = &tiles[onTile+1];
            else {
                mainUi->searchRowset->rows[onRow].name[21] = 
                    tiles[onTile].target->name[0];
                tiles[onTile].next = NULL;
            }
        }

        mainUi->searchRowset->rows[onRow].length = onTile % 25;                 

        UiRow *firstRow = &mainUi->searchRowset->rows[0];
        UiRow *lastRow = 
            &mainUi->searchRowset->rows[mainUi->searchRowset->numRows -1];

        for (uint32_t i = 0; i < mainUi->searchRowset->numRows; ++i) {

            if (i > 0)
                mainUi->searchRowset->rows[i].previousRow
                    = &mainUi->searchRowset->rows[i-1];
            else
                mainUi->searchRowset->rows[i].previousRow = lastRow;

            if (i != mainUi->searchRowset->numRows -1)
                mainUi->searchRowset->rows[i].nextRow = 
                    &mainUi->searchRowset->rows[i+1];
            else 
                mainUi->searchRowset->rows[i].nextRow = firstRow;

        }

        mainUi->searchRowset->movingToRow = firstRow;
        mainUi->searchRowset->movingToTarget = tiles[0].target;
        mainUi->searchRowset->rowCursor = mainUi->searchRowset->rows; 
    }
    else {
        free(mainUi->searchRowset->rows[0].tiles);
        mainUi->searchRowset->numRows = 0;
        mainUi->activeRowset = mainUi->homeRowset;
    }

    updateGameInfo();
}



void loadPlaytimeFile() {

    char *email;
    Player *thePlayer = &offblast->player;
    if (thePlayer->emailHash == 0) {
        email = offblast->users[0].email;
    }
    else {
        email = thePlayer->user->email;
    }

    assert(email);

    char *playTimeDbPath;
    asprintf(&playTimeDbPath, "%s/%s.playtime", 
            offblast->playtimePath, email);

    OffblastDbFile playTimeDb = {0};
    if (!InitDbFile(playTimeDbPath, &playTimeDb, 
                1))
    {
        printf("couldn't initialize the playTime file, exiting\n");
        exit(1);
    }
    offblast->playTimeFile = 
        (PlayTimeFile*) playTimeDb.memory;
    offblast->playTimeDb = playTimeDb;
    free(playTimeDbPath);
}

void killRunningGame() {

    switch (offblast->windowManager) {
        case WINDOW_MANAGER_I3:
            system("i3-msg [workspace=blastgame] kill");
            break;

        default:
            killpg(offblast->runningPid, SIGKILL);
            //Display *d = XOpenDisplay(NULL);
            //raiseWindow();
            //SDL_SetWindowFullscreen(offblast->window, 
             //       SDL_WINDOW_FULLSCREEN_DESKTOP);
            break;
    }
    printf("killed %d\n", offblast->runningPid);
    offblast->mode = OFFBLAST_UI_MODE_MAIN;
    offblast->runningPid = 0;

    LaunchTarget *target = offblast->playingTarget;
    assert(target);

    uint32_t afterTick = SDL_GetTicks();

    PlayTime *pt = NULL;
    for (uint32_t i = 0; i < offblast->playTimeFile->nEntries; ++i) {
        if (offblast->playTimeFile->entries[i].targetSignature 
                == target->targetSignature) 
        {
            pt = &offblast->playTimeFile->entries[i];
        }
    }

    if (pt == NULL) {
        void *growState = growDbFileIfNecessary(
                &offblast->playTimeDb, 
                sizeof(PlayTime),
                OFFBLAST_DB_TYPE_FIXED); 

        if(growState == NULL) {
            printf("Couldn't expand the playtime file to "
                    "accomodate all the playtimes\n");
            return;
        }
        else { 
            offblast->playTimeFile = (PlayTimeFile*) growState;
        }

        pt = &offblast->playTimeFile->entries[
            offblast->playTimeFile->nEntries++];

        pt->targetSignature = target->targetSignature;
    }

    pt->msPlayed += (afterTick - offblast->startPlayTick);
    pt->lastPlayed = (uint32_t)time(NULL);

    offblast->playingTarget = NULL;
    offblast->startPlayTick = 0;

    updateHomeLists();
}

uint32_t activeWindowIsOffblast() {
    WindowInfo winInfo = getOffblastWindowInfo();

    if((int)getActiveWindowRaw() == (int)winInfo.window) 
        return 1;
    else 
        return 0;
   
}

void pressGuide() {

    if (offblast->mode == OFFBLAST_UI_MODE_BACKGROUND 
            && offblast->runningPid > 0) 
    {

        if (!activeWindowIsOffblast()) {

            offblast->resumeWindow = getActiveWindowRaw();
            offblast->uiStopButtonHot = 0;

            if (offblast->windowManager == WINDOW_MANAGER_I3) {
                system("i3-msg workspace offblast");
            }
            else {
                raiseWindow(0);
            }
        }
    }
}

Window getActiveWindowRaw() {

    Window w;
    int revert_to;

    XGetInputFocus(offblast->XDisplay, &w, &revert_to); // see man

    if(!w){
        printf("Couldn't get the active window\n");
        return 0;
    }else if(w == 0){
        printf("no focus window\n");
        exit(1);
    }else{
        //printf("ACTIVE WINDOW (window: %d)\n", (int)w);
    }

    return w;
}


WindowInfo getOffblastWindowInfo() {

    WindowInfo windowInfo = {};

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(offblast->window, &wmInfo);

    windowInfo.window = wmInfo.info.x11.window;
    windowInfo.display = wmInfo.info.x11.display;

    //printf("OFFBLAST WINDOW ID %d\n", (int)windowInfo.window);

    return windowInfo;
}


void raiseWindow(Window window){

    WindowInfo offblastWinInfo = getOffblastWindowInfo();
    WindowInfo winInfo;
    if (window == 0) {
        winInfo = offblastWinInfo;
    }
    else {
        winInfo.window = window;
        winInfo.display = offblastWinInfo.display;
    }

    XWindowAttributes wattr;

    XEvent xev;
    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.xclient.display = winInfo.display;
    xev.xclient.window = winInfo.window;
    xev.xclient.message_type = XInternAtom(winInfo.display, "_NET_ACTIVE_WINDOW", 0);
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = 2L; /* 2 == Message from a window pager */
    xev.xclient.data.l[1] = CurrentTime;

    XGetWindowAttributes(winInfo.display, winInfo.window, &wattr);
    XSendEvent(winInfo.display, wattr.screen->root, False,
            SubstructureNotifyMask | SubstructureRedirectMask,
            &xev);
}

RomFoundList *newRomList(){
    unsigned int nEntriesToAlloc = 100;

    RomFoundList *list = calloc(1, sizeof(RomFoundList));
    assert(list);

    list->items = calloc(nEntriesToAlloc, sizeof(struct RomFound));
    assert(list->items);
    list->allocated = nEntriesToAlloc;

    return list;
}

uint32_t pushToRomList(RomFoundList *list, char *path, char *name, char *id) {

    if (list->numItems +1 >= list->allocated) {

        list->items = realloc(list->items, 
                list->allocated * sizeof(RomFound) + 
                100 * sizeof(RomFound));

        assert(list->items);

        memset(&list->items[list->numItems], 0x00, 100 * sizeof(RomFound));
        list->allocated += 100;
    }

    if (list->items == NULL) {
        return 0;
    }

    RomFound *rom = &list->items[list->numItems++];
    if (path != NULL) memcpy(rom->path, path, strlen(path));
    if (name != NULL) memcpy(rom->name, name, strlen(name));
    if (id != NULL) memcpy(rom->id, id, strlen(id));

    return 1;
}

uint32_t romListContentSig(RomFoundList *list) {
    uint32_t contentSignature = 0;
    lmmh_x86_32(list->items, list->numItems * sizeof(RomFound), 
            33, &contentSignature);

    return contentSignature;
}

void freeRomList(RomFoundList *list) {
    free(list->items);
    list->items = NULL;
    free(list);
}

// These functions need to find a list of games
// create the fields needed to update the internal game db
void importFromCemu(Launcher *theLauncher) {

    RomFoundList *list = newRomList();

    char *cemuSettingsFilePath;
    asprintf(&cemuSettingsFilePath, "%ssettings.xml", 
            theLauncher->cemuPath);
    printf("reading from settings file %s\n", cemuSettingsFilePath);

    xmlDoc *settingsDoc = NULL;

    xmlXPathContextPtr xpathCtx; 
    xmlXPathObjectPtr xpathObj; 

    settingsDoc = xmlParseFile(cemuSettingsFilePath);
    assert(settingsDoc);
    xpathCtx = xmlXPathNewContext(settingsDoc);

    xpathObj = xmlXPathEvalExpression(
            (xmlChar *)"//GameCache/Entry", 
            xpathCtx);

    if(xpathObj == NULL) {
        fprintf(stderr,"Error: unable to evaluate xpath expression\n");
        xmlXPathFreeContext(xpathCtx); 
        xmlFreeDoc(settingsDoc); 
        exit(1);
    }
    else {
        xmlNodeSet *entries = xpathObj->nodesetval;
        int size = (entries) ? entries->nodeNr : 0;
        int nodei;

        for(nodei = size - 1; nodei >= 0; nodei--) {

            RomFound currentFind = {};

            assert(entries->nodeTab[nodei]);

            xmlNode *properties 
                = entries->nodeTab[nodei]->children;

            for(xmlNode *child = properties; child; child = child->next) 
            {

                if (strcmp((char *)child->name, "name") == 0) {
                    if (strlen((char *)child->children->content) > 
                            OFFBLAST_NAME_MAX) 
                    {
                        printf("Warning: name is longer than 255: %s", 
                                child->children->content);
                        continue;
                    }

                    memcpy(&currentFind.name, child->children->content,
                            strlen((char*)child->children->content));
                }
                else if (strcmp((char *)child->name, "path") == 0) {
                    if (strlen((char *)child->children->content) > 
                            OFFBLAST_NAME_MAX) 
                    {
                        printf("Warning: path is longer than 255: %s", 
                                child->children->content);
                        continue;
                    }

                    memcpy(&currentFind.path, child->children->content,
                            strlen((char*)child->children->content));
                }
            }

            pushToRomList(list, (char *)&currentFind.path, 
                    (char *)&currentFind.name, NULL);
        }

    }

    xmlXPathFreeContext(xpathCtx); 
    xmlFreeDoc(settingsDoc); 

    if (list->numItems == 0) { 
        printf("no items found\n");
        freeRomList(list);
        list = NULL;
        return;
    }

    uint32_t rescrapeRequired = 0;

    if (launcherContentsCacheUpdated(theLauncher->signature, romListContentSig(list))) {
        printf("Launcher targets for %u have changed!\n", theLauncher->signature);
        rescrapeRequired = 1;
    }
    else {
        printf("Contents unchanged for: %u\n", theLauncher->signature);
    }


    if (rescrapeRequired) {

        for (uint32_t j=0 ; j< list->numItems; j++) {

            float matchScore = 0;
            int32_t indexOfEntry = launchTargetIndexByNameMatch(
                    offblast->launchTargetFile, 
                    list->items[j].name, 
                    theLauncher->platform,
                    &matchScore);

            printf("found by name at index %d\n", indexOfEntry);

            if (indexOfEntry > -1 &&
                    matchScore > offblast->launchTargetFile->entries[indexOfEntry].matchScore) 
            {

                LaunchTarget *theTarget = 
                    &offblast->launchTargetFile->entries[indexOfEntry];

                theTarget->launcherSignature = theLauncher->signature;

                if (theTarget->path != NULL && strlen(theTarget->path)) {
                    printf("%s already has a path, overwriting with %s\n",
                            theTarget->name,
                            list->items[j].path);

                    memset(&theTarget->path, 0x00, PATH_MAX);
                }

                memcpy(&theTarget->path, 
                        list->items[j].path,
                        strlen(list->items[j].path));

            }
        } 
    }

    freeRomList(list);
    list = NULL;
}

void importFromScummvm(Launcher *theLauncher) {

    RomFoundList *list = newRomList();
    char *homePath = getenv("HOME");

    char *registryPath = NULL;
    asprintf(&registryPath, "%s/.config/scummvm/scummvm.ini", homePath);

    FILE *fp = fopen(registryPath, "r");
    if (fp == NULL) {
        perror("Couldn't open scummvm config\n");
        return;
    }

    char *lineBuffer = calloc(512, sizeof(char));
    assert(lineBuffer);

    char *currentId = calloc(64, sizeof(char));
    char *currentName = calloc(64, sizeof(char));

    while(fgets(lineBuffer, 512, fp)) {


        if (lineBuffer[0] == '[') {

            if (strncmp(lineBuffer, "[scummvm]", strlen("[scummvm]")) != 0) {

                // Got a new game
                memset(currentId, 0, 64);
                char *closePos = strchr(lineBuffer, ']');
                assert(*closePos);

                assert(closePos-(lineBuffer+1) <= 64);
                memcpy(currentId, lineBuffer+1, 
                        closePos-(lineBuffer+1));
            }
        }
        else if (strcasestr(lineBuffer, "description=") == lineBuffer) {
            char *startP = strchr(lineBuffer, '=') +1;
            char *endP = strchr(lineBuffer, '\n');

            if (strchr(lineBuffer, '(')) endP = strchr(lineBuffer, '(');
            assert(endP);

            memset(currentName, 0, 64);
            assert(endP-startP <= 64);
            memcpy(currentName, startP, endP-startP);

            pushToRomList(list, currentId, currentName, NULL);
        }
    }

    fclose(fp);

    free(lineBuffer);
    free(registryPath);
    free(currentId);
    free(currentName);

    if (list->numItems == 0) { 
        printf("no items found\n");
        freeRomList(list);
        list = NULL;
        return;
    }

    uint32_t rescrapeRequired = 0;
    if (launcherContentsCacheUpdated(theLauncher->signature, romListContentSig(list))) {
        printf("Launcher targets for %u have changed!\n", 
                theLauncher->signature);

        rescrapeRequired = 1;
    }
    else {
        printf("Contents unchanged for: %u\n", theLauncher->signature);
    }


    if (rescrapeRequired) {

        for (uint32_t j=0; j < list->numItems; j++) {

            float matchScore = 0;
            int32_t indexOfEntry = launchTargetIndexByNameMatch(
                    offblast->launchTargetFile, 
                    list->items[j].name, 
                    theLauncher->platform,
                    &matchScore);

            printf("found by name at index %d\n", indexOfEntry);

            if (indexOfEntry > -1 &&
                    matchScore > offblast->launchTargetFile->entries[indexOfEntry].matchScore) 
            {

                LaunchTarget *theTarget = 
                    &offblast->launchTargetFile->entries[indexOfEntry];

                theTarget->launcherSignature = theLauncher->signature;

                if (theTarget->path != NULL && strlen(theTarget->path)) {
                    printf("%s already has a path, overwriting with %s\n",
                            theTarget->name,
                            list->items[j].path);

                    memset(&theTarget->path, 0x00, PATH_MAX);
                }

                memcpy(&theTarget->path, 
                        list->items[j].path,
                        strlen(list->items[j].path));

            }
        } 
    }

    freeRomList(list);
    list = NULL;
}

void importFromRPCS3(Launcher *theLauncher) {

    char *homePath = getenv("HOME");
    char *registryPath = NULL;
    asprintf(&registryPath, "%s/.config/rpcs3/games.yml", homePath);

    FILE *fp = fopen(registryPath, "r");
    if (fp == NULL) {
        perror("Couldn't open rpcs3 games directory\n");
        return;
    }

    RomFoundList *list = newRomList();

    char *lineBuffer = calloc(512, sizeof(char));
    assert(lineBuffer);

    char *currentPath = calloc(PATH_MAX, sizeof(char));
    char *currentName = calloc(64, sizeof(char));
    char *pathToRom = "PS3_GAME/USRDIR/EBOOT.BIN";

    while(fgets(lineBuffer, 512, fp)) {

        memset(currentPath, 0, PATH_MAX);

        // Get the path
        char *startP = strchr(lineBuffer, ' ') +1;
        char *closePos = strchr(lineBuffer, '\n');
        if (!closePos) {
            printf("falling back\n");
            closePos = strrchr(lineBuffer, '/')+1;
;
        }
        assert(closePos);

        assert(closePos-(lineBuffer+1) <= PATH_MAX);

        memcpy(currentPath, startP, 
                closePos-(startP));

        char *appendChar = strrchr(currentPath, '/') +1;
        assert(appendChar);
        strncpy(appendChar, pathToRom, strlen(pathToRom));

        // Get the name
        startP = strchr(lineBuffer, '[') +1;
        char *endP = strchr(lineBuffer, ']');
        if (!endP || !startP) continue;

        memset(currentName, 0, 64);
        assert(endP-startP <= 64);
        memcpy(currentName, startP, endP-startP);

        printf("ps3 game: %s\n%s\n", currentName, currentPath);

        pushToRomList(list, currentPath, currentName, NULL);
    }

    fclose(fp);

    free(lineBuffer);
    free(registryPath);
    free(currentPath);
    free(currentName);

    if (list->numItems == 0) { 
        printf("no items found\n");
        freeRomList(list);
        list = NULL;
        return;
    }

    uint32_t rescrapeRequired = 0;
    if (launcherContentsCacheUpdated(theLauncher->signature, romListContentSig(list))) {
        printf("Launcher targets for %u have changed!\n", 
                theLauncher->signature);

        rescrapeRequired = 1;
    }
    else {
        printf("Contents unchanged for: %u\n", theLauncher->signature);
    }


    if (rescrapeRequired) {

        for (uint32_t j=0; j < list->numItems; j++) {

            float matchScore = 0;
            int32_t indexOfEntry = launchTargetIndexByNameMatch(
                    offblast->launchTargetFile, 
                    list->items[j].name, 
                    theLauncher->platform,
                    &matchScore);

            printf("found by name at index %d\n", indexOfEntry);

            if (indexOfEntry > -1 &&
                    matchScore > offblast->launchTargetFile->entries[indexOfEntry].matchScore) 
            {

                LaunchTarget *theTarget = 
                    &offblast->launchTargetFile->entries[indexOfEntry];

                theTarget->launcherSignature = theLauncher->signature;

                if (theTarget->path != NULL && strlen(theTarget->path)) {
                    printf("%s already has a path, overwriting with %s\n",
                            theTarget->name,
                            list->items[j].path);

                    memset(&theTarget->path, 0x00, PATH_MAX);
                }

                memcpy(&theTarget->path, 
                        list->items[j].path,
                        strlen(list->items[j].path));

            }
        } 
    }

    freeRomList(list);
    list = NULL;
}

void importFromSteam(Launcher *theLauncher) {

    RomFoundList *list = newRomList();
    char *homePath = getenv("HOME");

    char *registryPath = NULL;
    asprintf(&registryPath, "%s/.steam/registry.vdf", homePath);

    FILE *fp = fopen(registryPath, "r");
    if (fp == NULL) {
        perror("Couldn't open steam registry\n");
        return;
    }

    char *lineBuffer = calloc(512, sizeof(char));
    assert(lineBuffer);
    uint32_t inAppsSection = 0;
    int32_t depth = 0;
    char *idStr = NULL;
    char *currentId = calloc(64, sizeof(char));
    char *currentIdCursor;

    while(fgets(lineBuffer, 512, fp)) {
        if (inAppsSection) {

            if (strstr(lineBuffer, "{")) ++depth;
            if (strstr(lineBuffer, "}")) --depth;
            if (depth < 0) break;

            if (depth == 1 && strstr(lineBuffer, "\"")) {


                idStr = lineBuffer;
                memset(currentId, 0, 64);

                while (isspace(*idStr) || *idStr == '\"') ++idStr;
                currentIdCursor = currentId;
                while (isdigit(*idStr)) {
                    memcpy(currentIdCursor++, idStr++, sizeof(char));
                }
            }
            if (depth == 2) {
                if (strstr(lineBuffer, "Installed") 
                        && strstr(lineBuffer, "1")) 
                {
                    printf("New steam game %s\n", currentId);
                    pushToRomList(list, NULL, NULL, currentId);
                }
            }
        }
        else if (strcasestr(lineBuffer, "\"apps\"") != NULL) {
            inAppsSection = 1;
        }
    }

    fclose(fp);
    free(registryPath);
    free(lineBuffer);
    free(currentId);

    if (list->numItems == 0) { 
        printf("no items found\n");
        freeRomList(list);
        list = NULL;
        return;
    }

    uint32_t rescrapeRequired = 0;
    if (launcherContentsCacheUpdated(theLauncher->signature, romListContentSig(list))) {
        printf("Launcher targets for %u have changed!\n", 
                theLauncher->signature);

        rescrapeRequired = 1;
    }
    else {
        printf("Contents unchanged for: %u\n", theLauncher->signature);
    }


    if (rescrapeRequired) {

        int32_t indexOfEntry = -1;

        for (uint32_t j=0; j < list->numItems; j++) {

            indexOfEntry = launchTargetIndexByIdMatch(
                    offblast->launchTargetFile, list->items[j].id, theLauncher->platform);

            printf("found by id at index %d\n", indexOfEntry);

            if (indexOfEntry > -1) {

                LaunchTarget *theTarget = 
                    &offblast->launchTargetFile->entries[indexOfEntry];

                theTarget->launcherSignature = theLauncher->signature;
            }
        } 
    }

    freeRomList(list);
    list = NULL;
}


void importFromCustom(Launcher *theLauncher) {

    // TODO NFS shares when unavailable just lock this up!
    DIR *dir = opendir(theLauncher->romPath);
    if (dir == NULL) {
        printf("Path %s failed to open\n", theLauncher->romPath);
        return;
    }

    RomFoundList *list = newRomList();

    struct dirent *currentEntry;
    while ((currentEntry = readdir(dir)) != NULL) {

        if (strcmp(currentEntry->d_name, ".") == 0) continue;
        if (strcmp(currentEntry->d_name, "..") == 0) continue;

        char *ext = strrchr((char*)currentEntry->d_name, '.');
        if (ext == NULL) continue;

        char *workingExt = strdup(theLauncher->extension);
        char *token = strtok(workingExt, ",");

        while (token) {
            if (strcmp(ext, token) == 0) {

                char *fullPath = NULL;
                asprintf(&fullPath, "%s/%s", 
                        theLauncher->romPath, currentEntry->d_name);

                pushToRomList(list, fullPath, NULL, NULL);
                free(fullPath);

            }
            token = strtok(NULL, ",");
        }

        free(workingExt);
    }

    closedir(dir);
    if (list->numItems == 0) { 
        freeRomList(list);
        list = NULL;
        return;
    }

    uint32_t rescrapeRequired = 0;
    if (launcherContentsCacheUpdated(theLauncher->signature, romListContentSig(list))) {
        printf("Launcher targets for %u have changed!\n", 
                theLauncher->signature);
        rescrapeRequired = 1;
    }
    else {
        printf("Contents unchanged for: %u\n", theLauncher->signature);
    }

    if (rescrapeRequired) {
        void *romData = calloc(1, ROM_PEEK_SIZE);

        for (uint32_t j=0; j< list->numItems; j++) {

            char *searchString = NULL;

            searchString = calloc(1, 
                    strlen((char*)&list->items[j].path) + 1);

            char *startOfFileName = 
                strrchr((char*)&list->items[j].path, '/');

            startOfFileName++;
            mempcpy(searchString, 
                    startOfFileName,
                    strlen(startOfFileName));

            char *ext = strchr(searchString, '(');

            if (ext == NULL) ext = strrchr(searchString, '.');
            if (ext != NULL) *ext = '\0';
            if (*(ext-1) == ' ') *(ext-1) = '\0';

            float matchScore = 0;

            int32_t indexOfEntry = launchTargetIndexByNameMatch(
                    offblast->launchTargetFile, 
                    searchString, 
                    theLauncher->platform,
                    &matchScore);

            free(searchString);


            if (indexOfEntry > -1 && 
                    matchScore > offblast->launchTargetFile->entries[indexOfEntry].matchScore)             {

                LaunchTarget *theTarget = 
                    &offblast->launchTargetFile->entries[indexOfEntry];

                if (theTarget->path != NULL && strlen(theTarget->path)) {
                    printf("%s already has a path, overwriting with %s\n",
                            theTarget->name,
                            list->items[j].path);

                    memset(&theTarget->path, 0x00, PATH_MAX);
                }

                theTarget->launcherSignature = theLauncher->signature;
                memcpy(&theTarget->path, 
                        (char *) &list->items[j].path,
                        strlen((char *) &list->items[j].path));
                theTarget->matchScore = matchScore;

            }
            else {
                printf("No match found for %s\n", list->items[j].path);
                logMissingGame(list->items[j].path);
            }
        }

        free(romData);
    } 

    freeRomList(list);
    list = NULL;
    return;
}


uint32_t launcherContentsCacheUpdated(uint32_t launcherSignature, uint32_t newContentsHash) {

    uint32_t isInvalid = 1;
    int32_t foundAtIndex = -1;

    for (uint32_t i=0; i < offblast->launcherContentsCache.length; ++i) {
        if (offblast->launcherContentsCache.entries[i].launcherSignature 
                    == launcherSignature) 
        {
            foundAtIndex = i;
            if (offblast->launcherContentsCache.entries[i].contentSignature
                    == newContentsHash) isInvalid = 0;
            break;
        }
    }

    if (isInvalid) {
        if (foundAtIndex > -1) {
            offblast->launcherContentsCache.entries[foundAtIndex].contentSignature 
                = newContentsHash;
        }
        else {
            size_t currentLength = offblast->launcherContentsCache.length;
            offblast->launcherContentsCache.entries = realloc(
                offblast->launcherContentsCache.entries,
                (currentLength+1) * sizeof(LauncherContentsHash));

            assert(offblast->launcherContentsCache.entries);

            offblast->launcherContentsCache.entries[currentLength].contentSignature 
                = newContentsHash;

            offblast->launcherContentsCache.entries[currentLength].launcherSignature 
                = launcherSignature;

            offblast->launcherContentsCache.length++;
        }
    }

    return isInvalid;
}

void logMissingGame(char *missingGamePath){
    char *path = NULL;
    asprintf(&path, "%s/missinggames.log", offblast->configPath);
    FILE * fp = fopen(path, "a+");

    fwrite(missingGamePath, strlen(missingGamePath), 1, fp);
    fwrite("\n", 1, 1, fp);

    fclose(fp);
}

void calculateRowGeometry(UiRow *row) {

    UiTile *theTile = NULL;
    uint32_t theWidth = 0;
    uint32_t xAdvance = 0;

    Image *missingImage = &offblast->missingCoverImage;
    uint32_t missingImageWidth = getWidthForScaledImage(
            offblast->mainUi.boxHeight,
            missingImage);

    for(uint8_t i = 0; i < row->length; ++i) {

        theTile = &row->tiles[i];
        theTile->baseX = xAdvance;

        if (theTile->image.textureHandle == 0) {
            theWidth = missingImageWidth;
        }
        else {
            theWidth = getWidthForScaledImage(
                    offblast->mainUi.boxHeight,
                    &theTile->image);
        }

        xAdvance += (theWidth + offblast->mainUi.boxPad);
    }
}
