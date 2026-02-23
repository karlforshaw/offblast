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
#define WINDOW_MANAGER_KDE 3

#define SESSION_TYPE_X11 1
#define SESSION_TYPE_WAYLAND 2

#define IMAGE_STORE_SIZE 2000
#define MAX_LOADED_TEXTURES 150
#define TEXTURE_EVICTION_TIME_MS 3000

// See ROADMAP.md for planned features and backlog

#include <stdio.h>
#include <signal.h>
#include <setjmp.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <glob.h>
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

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

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

    // Dynamic field storage for all custom user fields
    char **customKeys;     // Array of field names (e.g., "cemu_account")
    char **customValues;   // Array of field values
    int numCustomFields;   // Number of custom fields
} User;

// Helper function to get a custom field value from a user
const char* getUserCustomField(User *user, const char *fieldName) {
    if (!user || !fieldName) return NULL;

    for (int i = 0; i < user->numCustomFields; i++) {
        if (strcasecmp(user->customKeys[i], fieldName) == 0) {
            return user->customValues[i];
        }
    }
    return NULL;
}

// Helper function to replace all user field placeholders in a string
void replaceUserPlaceholders(char *launchString, User *user) {
    if (!user || !launchString) return;

    char *p;
    int replaceIter, replaceLimit = 8;

    // Replace all custom field placeholders
    for (int i = 0; i < user->numCustomFields; i++) {
        // Build placeholder like %FIELD_NAME% (uppercase)
        char placeholder[256];
        snprintf(placeholder, sizeof(placeholder), "%%%s%%", user->customKeys[i]);

        // Convert placeholder to uppercase
        for (char *c = placeholder; *c; c++) {
            if (*c >= 'a' && *c <= 'z') *c -= 32;
        }

        replaceIter = 0;
        while ((p = strstr(launchString, placeholder))) {
            const char *value = user->customValues[i];

            // Move the rest of the string to make room
            memmove(p + strlen(value),
                    p + strlen(placeholder),
                    strlen(p + strlen(placeholder)) + 1);

            // Copy in the replacement value
            memcpy(p, value, strlen(value));

            replaceIter++;
            if (replaceIter >= replaceLimit) {
                printf("Replacement iterations exceeded for %s\n", placeholder);
                break;
            }
        }
    }
}

typedef struct Player {
    int32_t jsIndex;
    SDL_GameController *usingController; 
    char *name; 
    uint8_t emailHash;
    User *user;
} Player;

#define IMAGE_STATE_COLD 0
#define IMAGE_STATE_QUEUED 1
#define IMAGE_STATE_LOADING 2
#define IMAGE_STATE_DOWNLOADING 3
#define IMAGE_STATE_READY 4
#define IMAGE_STATE_COMPLETE 5
#define IMAGE_STATE_DEAD 6

typedef struct Image {
    uint64_t targetSignature;
    uint8_t state;
    uint32_t width;
    uint32_t height;
    GLuint textureHandle;

    uint32_t lastUsedTick;
    char path[PATH_MAX];
    char url[PATH_MAX];

    unsigned char *atlas;
    size_t atlasSize;
} Image;

// SteamGridDB types
#define MAX_SGDB_COVERS 50
#define MAX_SGDB_GAMES 10

typedef struct SgdbCover {
	uint32_t id;
	char url[PATH_MAX];        // Full resolution image URL
	char thumb[PATH_MAX];      // Thumbnail URL (for grid display)
	uint32_t width;
	uint32_t height;
	uint32_t score;            // Community upvotes
} SgdbCover;

typedef struct SgdbGame {
	uint32_t id;
	char name[256];
} SgdbGame;

typedef struct SgdbSearchResult {
	uint32_t numGames;
	SgdbGame games[MAX_SGDB_GAMES];
} SgdbSearchResult;

typedef struct SgdbCoverList {
	uint32_t numCovers;
	SgdbCover covers[MAX_SGDB_COVERS];
} SgdbCoverList;

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

typedef struct PlatformName {
    char key[256];
    char name[256];
} PlatformName;

#define MAX_PLATFORM_NAMES 100
PlatformName platformNames[MAX_PLATFORM_NAMES];
uint32_t numPlatformNames = 0;

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

    uint32_t fadeInActive;       // 1 = fade-in animation in progress
    uint32_t fadeInStartTick;    // When fade-in started

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
    uint32_t menuScrollOffset;
    uint32_t maxVisibleMenuItems;

    int32_t showContextMenu;
    MenuItem *contextMenuItems;
    uint32_t numContextMenuItems;
    uint32_t contextMenuCursor;

    int32_t showSearch;

    Animation *horizontalAnimation;
    Animation *verticalAnimation;
    Animation *infoAnimation;
    Animation *rowNameAnimation;
    Animation *menuAnimation;
    Animation *menuNavigateAnimation;
    Animation *contextMenuAnimation;
    Animation *contextMenuNavigateAnimation;

	// Cover Browser state
	int32_t showCoverBrowser;
	uint32_t coverBrowserState;  // 0=game_select, 1=cover_grid
	Animation *coverBrowserAnimation;

	// Game selection (for non-Steam games with multiple matches)
	SgdbSearchResult *coverBrowserGames;
	uint32_t coverBrowserGameCursor;

	// Cover grid
	SgdbCoverList *coverBrowserCovers;
	uint32_t coverBrowserCoverCursor;
	uint32_t coverBrowserScrollOffset;
	uint32_t coverBrowserCoversPerRow;
	uint32_t coverBrowserVisibleRows;

	// Cached title text and width
	char coverBrowserTitle[128];
	uint32_t coverBrowserTitleWidth;

	// Temporary images for cover thumbnails
	Image coverBrowserThumbs[MAX_SGDB_COVERS];
	pthread_mutex_t coverBrowserThumbsLock;

	// Error/status messages
	char coverBrowserError[256];

    GLuint imageVbo;

    UiRowset *activeRowset;

    UiRowset *homeRowset;
    UiRowset *searchRowset;
    UiRowset *filteredRowset;

    uint32_t rowGeometryInvalid;

    char *titleText;
    char *infoText;
    char *playtimeText;
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

typedef struct DownloaderContext {
    Image *image;
    pthread_mutex_t *lock;
} DownloaderContext;

typedef struct CoverDownloadContext {
	char url[PATH_MAX];
	uint64_t targetSignature;
} CoverDownloadContext;

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

typedef struct LoadingState {
    pthread_mutex_t mutex;
    char status[256];           // "Loading Steam library..."
    uint32_t progress;          // Current item (0 if N/A)
    uint32_t progressTotal;     // Total items (0 if N/A)
    uint32_t complete;          // 1 when init done
    uint32_t error;             // 1 if fatal error
    char errorMsg[256];         // Error description
} LoadingState;

typedef struct OffblastUi {

    uint32_t running;
    uint32_t shutdownFlag;
    uint32_t loadingFlag;
    enum UiMode mode;
    char *configPath;
    char *openGameDbPath;
    char *playtimePath;
    Display *XDisplay;

    PlayerSelectUi playerSelectUi;
    MainUi mainUi;

    int32_t joyX;
    int32_t joyY;

    char searchTerm[OFFBLAST_MAX_SEARCH];
    uint32_t searchCursor;
    char searchCurChar;
    char searchPrevChar; 

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
    Image logoImage;              // For offblast_loading.png loading screen

    LoadingState loadingState;
    uint32_t loadingMode;         // 1 = showing loading screen
    uint32_t exitAnimating;       // 1 = exit animation in progress
    uint32_t exitAnimationStartTick;  // When exit animation started

    GLuint textVbo;

    // UTF-8 font support with packed characters
    stbtt_packedchar *titleCharData;
    stbtt_packedchar *infoCharData;
    stbtt_packedchar *debugCharData;
    int *titleCodepoints;      // Array of codepoints corresponding to packed chars
    int *infoCodepoints;
    int *debugCodepoints;
    int titleNumChars;
    int infoNumChars;
    int debugNumChars;

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

    OffblastDbFile descriptionDb;
    OffblastBlobFile *descriptionFile;
    OffblastDbFile playTimeDb;
    PlayTimeFile *playTimeFile;
    LaunchTargetFile *launchTargetFile;

    uint32_t nLaunchers;
    Launcher *launchers;

    LauncherContentsFile launcherContentsCache;

    SDL_Window *window;
    uint32_t windowManager;
    uint32_t sessionType;
    Window resumeWindow;
    char resumeWindowUuid[64];  // KWin window UUID for Wayland
    long gnomeResumeWindowId;   // GNOME Shell window ID for Wayland

    pid_t runningPid;
    LaunchTarget *playingTarget;
    uint32_t startPlayTick;

    uint32_t uiStopButtonHot;

    Image *imageStore;
    pthread_mutex_t imageStoreLock;
    uint32_t numLoadedTextures;

    // Image loader worker threads
    pthread_t *imageLoadThreads;
    uint32_t numImageLoadThreads;

    // Metadata refresh status notification
    char statusMessage[256];
    uint32_t statusMessageTick;
    uint32_t statusMessageDuration;
    uint32_t rescrapeInProgress;
    uint32_t rescrapeTotal;
    uint32_t rescrapeProcessed;

    // Config options
    uint32_t showInstalledOnly;

    // Steam API config
    char steamApiKey[64];
    char steamId[32];

	// SteamGridDB API config
	char steamGridDbApiKey[128];
} OffblastUi;

typedef struct CurlFetch {
    size_t size;
    unsigned char *data;
} CurlFetch;

typedef struct WindowInfo {
    Display *display;
    Window window;
} WindowInfo;


void condPrintConfigError(void *object, const char *message);
uint32_t megabytes(uint32_t n);
uint32_t powTwoFloor(uint32_t val);
uint32_t needsReRender(SDL_Window *window);
double easeOutCirc(double t, double b, double c, double d);
double easeInOutCirc (double t, double b, double c, double d);
char *getCsvField(char *line, int fieldNo);
double goldenRatioLarge(double in, uint32_t exponent);
float goldenRatioLargef(float in, uint32_t exponent);
void horizontalMoveDone();
void toggleKillButton();
void playerSelectMoveDone(void *arg);
void menuToggleDone(void *arg);
void menuNavigationDone(void *arg);
void verticalMoveDone();
void infoFaded();
void rowNameFaded();
uint32_t animationRunning();
void loadPlatformNames(const char *openGameDbPath);
const char *platformString(char *key);
char *getCoverPath(LaunchTarget *);
char *getCoverUrl(LaunchTarget *);
GLint loadShaderFile(const char *path, GLenum shaderType);
GLuint createShaderProgram(GLint vertShader, GLint fragShader);
void renderLoadingScreen(OffblastUi *offblast);
void launch();
void imageToGlTexture(GLuint *textureHandle, unsigned char *pixelData,
        uint32_t newWidth, uint32_t newHeight);
void changeRow(uint32_t direction);
void changeColumn(uint32_t direction);
void pressConfirm();
void jumpScreen(uint32_t direction);
void pressCancel();
void pressGuide();
void rescrapeCurrentLauncher(int deleteAllCovers);
void evictOldestTexture();
void evictTexturesOlderThan(uint32_t ageMs);
void updateResults();
void updateHomeLists();
void updateInfoText();
void updateDescriptionText();
void updateGameInfo();
void initQuad(Quad* quad);
size_t curlWrite(void *contents, size_t size, size_t nmemb, void *userP);
int playTimeSort(const void *a, const void *b);
int lastPlayedSort(const void *a, const void *b);
int rankingSort(const void *a, const void *b);
int tileRankingSort(const void *a, const void *b);
int utf8_decode(const char **str);
int find_glyph_index(int codepoint, const int *codepoints, int numChars);
int packFont(unsigned char *fontData, float fontSize, unsigned char *atlas,
             int atlasWidth, int atlasHeight,
             stbtt_packedchar **outCharData, int **outCodepoints);
uint32_t getTextLineWidth(char *string, stbtt_packedchar* cdata, int *codepoints, int numChars);
void renderText(OffblastUi *offblast, float x, float y, 
        uint32_t textMode, float alpha, uint32_t lineMaxW, char *string);
void initQuad(Quad* quad);
void resizeQuad(float x, float y, float w, float h, Quad *quad);
void renderGradient(float x, float y, float w, float h, 
        uint32_t horizontal, Color colorStart, Color colorEnd);
float getWidthForScaledImage(float scaledHeight, Image *image);
void renderImage(float x, float y, float w, float h, Image* image,
        float desaturation, float alpha);
void pressSearch();
void loadPlaytimeFile();
Window getActiveWindowRaw();
void raiseWindow();
void killRunningGame();
void getActiveKWinWindowUuid(char *uuidOut, size_t uuidSize);
void getKWinWindowUuids(char *output, size_t outputSize);
int getKWinWindowUuidByPid(pid_t pid, char *uuidOut, size_t uuidSize);
void activateWindowByPid(pid_t gamePid);
void captureGnomeFocusedWindow();
void activateGnomeWindow(long windowId);
void raiseWindowByUuid(const char *uuid);
void importFromSteam(Launcher *theLauncher);
void importFromCustom(Launcher *theLauncher);

// Context menu callbacks
void doRescrapePlatform();
void doRescrapeGame();
void doRescanLauncher();
void doCopyCoverFilename();
void doRefreshCover();
void pressContextMenu();
void contextMenuToggleDone(void *arg);
void contextMenuNavigationDone(void *arg);

// Steam metadata types and functions
typedef struct SteamMetadata {
    char date[11];        // "29 Sep 2017" or "2017" - normalized to fit
    uint32_t score;       // Metacritic score (0-100)
    char *description;    // Short description (allocated, caller must free)
} SteamMetadata;
SteamMetadata *fetchSteamGameMetadata(uint32_t appid);
void freeSteamMetadata(SteamMetadata *meta);
off_t writeDescriptionBlob(LaunchTarget *target, const char *description);

// SteamGridDB functions
SgdbSearchResult *sgdbSearchGames(const char *gameName);
uint32_t sgdbGetGameBySteamId(uint32_t steamAppId);
SgdbCoverList *sgdbGetCovers(uint32_t gameId);
void doBrowseCovers();
void closeCoverBrowser();
void coverBrowserSelectCover();
void coverBrowserSetTitle();
void coverBrowserQueueThumbnails();
void *downloadCoverMain(void *arg);

WindowInfo getOffblastWindowInfo();
uint32_t activeWindowIsOffblast();
uint32_t launcherContentsCacheUpdated(uint32_t launcherSignature, 
        uint32_t newContentsHash);
void logMissingGame(char *missingGamePath);
void logPoorMatch(char *romPath, char *matchedName, float matchScore);
void calculateRowGeometry(UiRow *row);
Image *requestImageForTarget(LaunchTarget *target, uint32_t affectQueue);
void changeRowset(UiRowset *rowset);

void *downloadMain(void *arg); 
void *imageLoadMain(void *arg); 

OffblastUi *offblast;

// NFS timeout handling
static jmp_buf nfs_timeout_jmpbuf;
static volatile sig_atomic_t nfs_timeout_occurred = 0;

void nfs_timeout_handler(int sig) {
    nfs_timeout_occurred = 1;
    longjmp(nfs_timeout_jmpbuf, 1);
}

void openPlayerSelect() {
    offblast->mode = OFFBLAST_UI_MODE_PLAYER_SELECT;
    offblast->playerSelectUi.fadeInActive = 1;
    offblast->playerSelectUi.fadeInStartTick = SDL_GetTicks();
}
void setExit() {
    offblast->running = 0;
};
void shutdownMachine() {
    offblast->shutdownFlag = 1;
    offblast->running = 0;
};
void doSearch() {
    changeRowset(offblast->mainUi.searchRowset);
    offblast->mainUi.rowGeometryInvalid = 1;
    offblast->mainUi.showSearch = 1;
    offblast->searchPrevChar = 0; // Reset previous char when entering search
}
void doHome() {
    changeRowset(offblast->mainUi.homeRowset);
    offblast->mainUi.rowGeometryInvalid = 1;
}

// Context menu callbacks
void doRescrapePlatform() {
    rescrapeCurrentLauncher(1);
}

void doRescrapeGame() {
    rescrapeCurrentLauncher(0);  // 0 = single game only
}

void doRescanLauncher() {
    printf("\n=== LAUNCHER RESCAN STARTED ===\n");

    MainUi *mainUi = &offblast->mainUi;
    if (offblast->mode != OFFBLAST_UI_MODE_MAIN) {
        printf("Launcher rescan only available in main UI mode\n");
        return;
    }

    if (!mainUi->activeRowset || !mainUi->activeRowset->rowCursor ||
        !mainUi->activeRowset->rowCursor->tileCursor ||
        !mainUi->activeRowset->rowCursor->tileCursor->target) {
        printf("No game selected for launcher rescan\n");
        return;
    }

    LaunchTarget *currentTarget = mainUi->activeRowset->rowCursor->tileCursor->target;

    // Find the launcher for this target
    Launcher *targetLauncher = NULL;
    for (uint32_t i = 0; i < offblast->nLaunchers; i++) {
        if (offblast->launchers[i].signature == currentTarget->launcherSignature) {
            targetLauncher = &offblast->launchers[i];
            break;
        }
    }

    if (!targetLauncher) {
        printf("ERROR: Could not find launcher for current game\n");
        return;
    }

    printf("Rescanning launcher: %s (platform: %s)\n",
           targetLauncher->name, targetLauncher->platform);

    // Invalidate the cache for this launcher to force a rescan
    for (uint32_t i = 0; i < offblast->launcherContentsCache.length; i++) {
        if (offblast->launcherContentsCache.entries[i].launcherSignature == targetLauncher->signature) {
            printf("Invalidating cache entry for launcher %u\n", targetLauncher->signature);
            offblast->launcherContentsCache.entries[i].contentSignature = 0;
            break;
        }
    }

    // Set status message
    snprintf(offblast->statusMessage, sizeof(offblast->statusMessage),
             "Scanning %.228s for new games...", targetLauncher->platform);
    offblast->statusMessageTick = SDL_GetTicks();
    offblast->statusMessageDuration = 5000;

    // Rescan based on launcher type
    if (strcmp(targetLauncher->type, "steam") == 0) {
        printf("Rescanning Steam library...\n");
        importFromSteam(targetLauncher);
    } else {
        printf("Rescanning ROM directory: %s\n", targetLauncher->romPath);
        importFromCustom(targetLauncher);
    }

    // Save updated cache to disk
    char *configPath = offblast->configPath;
    char *launcherContentsHashFilePath;
    asprintf(&launcherContentsHashFilePath, "%s/launchercontents.bin", configPath);

    FILE *launcherContentsFd = fopen(launcherContentsHashFilePath, "wb");
    if (launcherContentsFd) {
        fwrite(&offblast->launcherContentsCache.length, sizeof(uint32_t), 1, launcherContentsFd);
        fwrite(offblast->launcherContentsCache.entries,
               sizeof(LauncherContentsHash),
               offblast->launcherContentsCache.length,
               launcherContentsFd);
        fclose(launcherContentsFd);
        printf("Updated launcher contents cache\n");
    } else {
        printf("WARNING: Could not save launcher contents cache\n");
    }
    free(launcherContentsHashFilePath);

    // Update UI
    updateHomeLists();
    updateGameInfo();

    // Update status to show completion
    snprintf(offblast->statusMessage, sizeof(offblast->statusMessage),
             "%.228s scan complete", targetLauncher->platform);
    offblast->statusMessageTick = SDL_GetTicks();
    offblast->statusMessageDuration = 3000;

    printf("=== LAUNCHER RESCAN COMPLETE ===\n\n");
}

void doCopyCoverFilename() {
    MainUi *ui = &offblast->mainUi;
    if (!ui->activeRowset || !ui->activeRowset->rowCursor ||
        !ui->activeRowset->rowCursor->tileCursor) return;

    LaunchTarget *target = ui->activeRowset->rowCursor->tileCursor->target;
    if (!target) return;

    char filename[64];
    snprintf(filename, sizeof(filename), "%"PRIu64".jpg", target->targetSignature);

    if (SDL_SetClipboardText(filename) == 0) {
        snprintf(offblast->statusMessage, 256, "Copied: %s", filename);
    } else {
        snprintf(offblast->statusMessage, 256, "Clipboard error: %s", SDL_GetError());
    }
    offblast->statusMessageTick = SDL_GetTicks();
    offblast->statusMessageDuration = 3000;
}

void doRefreshCover() {
    MainUi *ui = &offblast->mainUi;
    if (!ui->activeRowset || !ui->activeRowset->rowCursor ||
        !ui->activeRowset->rowCursor->tileCursor) return;

    LaunchTarget *target = ui->activeRowset->rowCursor->tileCursor->target;
    if (!target) return;

    uint64_t sig = target->targetSignature;

    // Find and evict the texture for this game
    pthread_mutex_lock(&offblast->imageStoreLock);
    for (uint32_t i = 0; i < IMAGE_STORE_SIZE; i++) {
        if (offblast->imageStore[i].targetSignature == sig) {
            // Delete OpenGL texture if loaded
            if (offblast->imageStore[i].textureHandle != 0) {
                glDeleteTextures(1, &offblast->imageStore[i].textureHandle);
                offblast->imageStore[i].textureHandle = 0;
                offblast->numLoadedTextures--;
            }
            // Reset state so it will be re-queued
            offblast->imageStore[i].state = IMAGE_STATE_COLD;
            offblast->imageStore[i].targetSignature = 0;
            break;
        }
    }
    pthread_mutex_unlock(&offblast->imageStoreLock);

    snprintf(offblast->statusMessage, 256, "Cover refreshed");
    offblast->statusMessageTick = SDL_GetTicks();
    offblast->statusMessageDuration = 2000;
}

// Cover Browser functions

void closeCoverBrowser() {
	MainUi *ui = &offblast->mainUi;

	// Free allocated data
	if (ui->coverBrowserGames) {
		free(ui->coverBrowserGames);
		ui->coverBrowserGames = NULL;
	}
	if (ui->coverBrowserCovers) {
		free(ui->coverBrowserCovers);
		ui->coverBrowserCovers = NULL;
	}

	// Free thumbnail textures
	pthread_mutex_lock(&ui->coverBrowserThumbsLock);
	for (int i = 0; i < MAX_SGDB_COVERS; i++) {
		if (ui->coverBrowserThumbs[i].textureHandle) {
			glDeleteTextures(1, &ui->coverBrowserThumbs[i].textureHandle);
			ui->coverBrowserThumbs[i].textureHandle = 0;
		}
		if (ui->coverBrowserThumbs[i].atlas) {
			free(ui->coverBrowserThumbs[i].atlas);
			ui->coverBrowserThumbs[i].atlas = NULL;
		}
	}
	pthread_mutex_unlock(&ui->coverBrowserThumbsLock);
	pthread_mutex_destroy(&ui->coverBrowserThumbsLock);

	ui->showCoverBrowser = 0;
}

void *downloadCoverMain(void *arg) {
	CoverDownloadContext *ctx = (CoverDownloadContext *)arg;

	printf("=== Background Cover Download ===\n");
	printf("Downloading from: %s\n", ctx->url);
	printf("Target signature: %"PRIu64"\n", ctx->targetSignature);

	// Download the cover
	CurlFetch fetch = {0};
	CURL *curl = curl_easy_init();
	if (!curl) {
		printf("Failed to initialize curl\n");
		free(ctx);
		return NULL;
	}

	curl_easy_setopt(curl, CURLOPT_URL, ctx->url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fetch);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		printf("Failed to download cover: %s\n", curl_easy_strerror(res));
		if (fetch.data) free(fetch.data);
		free(ctx);
		return NULL;
	}

	printf("Downloaded %zu bytes\n", fetch.size);

	// Decode the image
	int width, height, channels;
	unsigned char *pixels = stbi_load_from_memory(fetch.data, fetch.size,
												   &width, &height, &channels, 4);
	free(fetch.data);

	if (!pixels) {
		printf("Failed to decode downloaded image\n");
		free(ctx);
		return NULL;
	}

	printf("Decoded image: %dx%d\n", width, height);

	// Resize if needed (same as downloadMain does)
	if (height > 660) {
		float scale = (float)660 / height;
		int newHeight = 660;
		int newWidth = (int)(width * scale);
		printf("Resizing cover from %dx%d to %dx%d (%.1f%% scale)\n",
			   width, height, newWidth, newHeight, scale * 100);

		unsigned char *resized = (unsigned char*)malloc(newWidth * newHeight * 4);
		if (resized) {
			stbir_resize(pixels, width, height, 0,
						resized, newWidth, newHeight, 0,
						STBIR_RGBA, STBIR_TYPE_UINT8,
						STBIR_EDGE_CLAMP, STBIR_FILTER_DEFAULT);
			stbi_image_free(pixels);
			pixels = resized;
			width = newWidth;
			height = newHeight;
		} else {
			printf("Warning: Couldn't allocate memory for resize, using original\n");
		}
	}

	// Save as JPG to covers directory
	char *homePath = getenv("HOME");
	char coverPath[PATH_MAX];
	snprintf(coverPath, PATH_MAX, "%s/.offblast/covers/%"PRIu64".jpg",
			 homePath, ctx->targetSignature);

	printf("Saving cover to: %s\n", coverPath);
	stbi_flip_vertically_on_write(1);
	int saveResult = stbi_write_jpg(coverPath, width, height, 4, pixels, 90);
	stbi_image_free(pixels);

	if (!saveResult) {
		printf("Failed to save cover file\n");
		free(ctx);
		return NULL;
	}

	printf("Cover saved successfully\n");

	// Just mark the image as COLD - don't delete textures from background thread!
	// The main thread will handle texture deletion and reloading naturally
	pthread_mutex_lock(&offblast->imageStoreLock);
	for (uint32_t i = 0; i < IMAGE_STORE_SIZE; i++) {
		if (offblast->imageStore[i].targetSignature == ctx->targetSignature) {
			// Mark as COLD so it will reload from the new file
			// Don't call glDeleteTextures from background thread - OpenGL isn't thread-safe!
			offblast->imageStore[i].state = IMAGE_STATE_COLD;
			break;
		}
	}
	pthread_mutex_unlock(&offblast->imageStoreLock);

	free(ctx);
	return NULL;
}

void coverBrowserSelectCover() {
	MainUi *ui = &offblast->mainUi;

	if (ui->coverBrowserState != 1) return;  // 1 = COVER_GRID
	if (!ui->coverBrowserCovers) return;

	// Get current game
	if (!ui->activeRowset || !ui->activeRowset->rowCursor ||
		!ui->activeRowset->rowCursor->tileCursor) return;

	LaunchTarget *target = ui->activeRowset->rowCursor->tileCursor->target;
	if (!target) return;

	// Get selected cover
	SgdbCover *cover = &ui->coverBrowserCovers->covers[ui->coverBrowserCoverCursor];

	printf("=== Cover Selection ===\n");
	printf("Game: %s (signature: %"PRIu64")\n", target->name, target->targetSignature);
	printf("Selected cover URL: %s\n", cover->url);

	// Create context for background download
	CoverDownloadContext *ctx = malloc(sizeof(CoverDownloadContext));
	strncpy(ctx->url, cover->url, PATH_MAX-1);
	ctx->url[PATH_MAX-1] = '\0';
	ctx->targetSignature = target->targetSignature;

	// Spawn download thread
	pthread_t downloadThread;
	pthread_create(&downloadThread, NULL, downloadCoverMain, ctx);
	pthread_detach(downloadThread);

	// Show status message
	snprintf(offblast->statusMessage, 256, "Downloading cover...");
	offblast->statusMessageTick = SDL_GetTicks();
	offblast->statusMessageDuration = 2000;

	// Close browser immediately - download continues in background
	closeCoverBrowser();
}

void *downloadThumbnailMain(void *arg) {
	Image *image = (Image *)arg;

	CurlFetch fetch = {0};
	CURL *curl = curl_easy_init();
	if (!curl) {
		image->state = IMAGE_STATE_DEAD;
		return NULL;
	}

	curl_easy_setopt(curl, CURLOPT_URL, image->url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fetch);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		printf("Thumbnail download failed: %s\n", curl_easy_strerror(res));
		if (fetch.data) free(fetch.data);
		image->state = IMAGE_STATE_DEAD;
		return NULL;
	}

	// Load image from memory
	int width, height, channels;
	unsigned char *pixels = stbi_load_from_memory(fetch.data, fetch.size,
												   &width, &height, &channels, 4);
	free(fetch.data);

	if (!pixels) {
		printf("Failed to decode thumbnail\n");
		image->state = IMAGE_STATE_DEAD;
		return NULL;
	}

	// Store the pixel data
	image->width = width;
	image->height = height;
	image->atlasSize = width * height * 4;
	image->atlas = pixels;
	image->state = IMAGE_STATE_READY;

	return NULL;
}

void coverBrowserSetTitle() {
	MainUi *ui = &offblast->mainUi;
	if (ui->coverBrowserCovers) {
		snprintf(ui->coverBrowserTitle, sizeof(ui->coverBrowserTitle),
				"Select Cover: (%u available)", ui->coverBrowserCovers->numCovers);
	} else {
		snprintf(ui->coverBrowserTitle, sizeof(ui->coverBrowserTitle), "Select Cover:");
	}
	ui->coverBrowserTitleWidth = getTextLineWidth(ui->coverBrowserTitle, offblast->titleCharData, offblast->titleCodepoints, offblast->titleNumChars);
}

void coverBrowserQueueThumbnails() {
	MainUi *ui = &offblast->mainUi;
	if (!ui->coverBrowserCovers) return;

	// Calculate visible range based on scroll offset
	uint32_t startIdx = ui->coverBrowserScrollOffset * ui->coverBrowserCoversPerRow;
	uint32_t endIdx = startIdx + (ui->coverBrowserVisibleRows * ui->coverBrowserCoversPerRow);
	if (endIdx > ui->coverBrowserCovers->numCovers) {
		endIdx = ui->coverBrowserCovers->numCovers;
	}

	// Queue thumbnails for visible covers
	pthread_mutex_lock(&ui->coverBrowserThumbsLock);
	for (uint32_t i = startIdx; i < endIdx; i++) {
		if (ui->coverBrowserThumbs[i].state == IMAGE_STATE_COLD) {
			// Set up for download
			ui->coverBrowserThumbs[i].state = IMAGE_STATE_DOWNLOADING;
			strncpy(ui->coverBrowserThumbs[i].url,
					ui->coverBrowserCovers->covers[i].thumb, PATH_MAX-1);
			ui->coverBrowserThumbs[i].url[PATH_MAX-1] = '\0';

			// Spawn download thread
			pthread_t downloadThread;
			pthread_create(&downloadThread, NULL, downloadThumbnailMain,
						   &ui->coverBrowserThumbs[i]);
			pthread_detach(downloadThread);
		}
	}
	pthread_mutex_unlock(&ui->coverBrowserThumbsLock);
}

void doBrowseCovers() {
	MainUi *ui = &offblast->mainUi;

	// Check if API key is configured
	if (!offblast->steamGridDbApiKey[0]) {
		snprintf(offblast->statusMessage, 256,
			"SteamGridDB API key not configured");
		offblast->statusMessageTick = SDL_GetTicks();
		offblast->statusMessageDuration = 3000;
		return;
	}

	// Get current game
	if (!ui->activeRowset || !ui->activeRowset->rowCursor ||
		!ui->activeRowset->rowCursor->tileCursor) return;

	LaunchTarget *target = ui->activeRowset->rowCursor->tileCursor->target;
	if (!target) return;

	// Initialize cover browser state
	ui->showCoverBrowser = 1;
	ui->coverBrowserError[0] = '\0';
	ui->coverBrowserGames = NULL;
	ui->coverBrowserCovers = NULL;
	ui->coverBrowserGameCursor = 0;
	ui->coverBrowserCoverCursor = 0;
	ui->coverBrowserScrollOffset = 0;

	// Initialize thumbnail images
	pthread_mutex_init(&ui->coverBrowserThumbsLock, NULL);
	for (int i = 0; i < MAX_SGDB_COVERS; i++) {
		ui->coverBrowserThumbs[i].state = IMAGE_STATE_COLD;
		ui->coverBrowserThumbs[i].targetSignature = 0;
		ui->coverBrowserThumbs[i].textureHandle = 0;
		ui->coverBrowserThumbs[i].atlas = NULL;
	}

	// Determine if this is a Steam game
	uint32_t sgdbGameId = 0;
	if (strcmp(target->platform, "steam") == 0) {
		// Steam game - extract appid from target->id
		uint32_t steamAppId = atoi(target->id);
		printf("Looking up SteamGridDB game for Steam AppID %u\n", steamAppId);
		sgdbGameId = sgdbGetGameBySteamId(steamAppId);

		if (sgdbGameId == 0) {
			snprintf(ui->coverBrowserError, 256,
				"Could not find game on SteamGridDB");
			ui->coverBrowserState = 0;  // game_select state
		}
	} else {
		// Non-Steam game - search by name
		printf("Searching SteamGridDB for: %s\n", target->name);
		ui->coverBrowserGames = sgdbSearchGames(target->name);

		if (!ui->coverBrowserGames || ui->coverBrowserGames->numGames == 0) {
			snprintf(ui->coverBrowserError, 256,
				"No games found for '%s'", target->name);
			ui->coverBrowserState = 0;
			if (ui->coverBrowserGames) free(ui->coverBrowserGames);
			ui->coverBrowserGames = NULL;
		} else if (ui->coverBrowserGames->numGames == 1) {
			// Only one match, proceed directly to covers
			sgdbGameId = ui->coverBrowserGames->games[0].id;
			free(ui->coverBrowserGames);
			ui->coverBrowserGames = NULL;
		} else {
			// Multiple matches, let user choose
			ui->coverBrowserState = 0;  // game_select state
			return;
		}
	}

	// If we have a game ID, fetch covers
	if (sgdbGameId > 0) {
		printf("Fetching covers for SteamGridDB game %u\n", sgdbGameId);
		ui->coverBrowserCovers = sgdbGetCovers(sgdbGameId);

		if (!ui->coverBrowserCovers || ui->coverBrowserCovers->numCovers == 0) {
			snprintf(ui->coverBrowserError, 256,
				"No 600x900 covers found for this game");
			ui->coverBrowserState = 0;
			if (ui->coverBrowserCovers) free(ui->coverBrowserCovers);
			ui->coverBrowserCovers = NULL;
		} else {
			ui->coverBrowserState = 1;  // cover_grid state
			coverBrowserSetTitle();  // Cache title text and width
			coverBrowserQueueThumbnails();
		}
	}

	// Start animation
	ui->coverBrowserAnimation->startTick = SDL_GetTicks();
	ui->coverBrowserAnimation->direction = 0;  // Opening
	ui->coverBrowserAnimation->durationMs = NAVIGATION_MOVE_DURATION/2;
	ui->coverBrowserAnimation->animating = 1;
}

void contextMenuToggleDone(void *arg) {
    uint32_t *showIt = (uint32_t*) arg;
    offblast->mainUi.showContextMenu = *showIt;
}

void contextMenuNavigationDone(void *arg) {
    uint32_t *direction = (uint32_t*) arg;
    MainUi *ui = &offblast->mainUi;

    if (*direction == 0) {
        ui->contextMenuCursor++;
    } else {
        ui->contextMenuCursor--;
    }
}

void *initThreadFunc(void *arg) {
    OffblastUi *offblast = (OffblastUi *)arg;
    LoadingState *state = &offblast->loadingState;
    char *configPath = offblast->configPath;

    // Helper macros for status updates
    #define SET_STATUS(msg) do { \
        pthread_mutex_lock(&state->mutex); \
        snprintf(state->status, 256, "%s", msg); \
        state->progress = 0; state->progressTotal = 0; \
        pthread_mutex_unlock(&state->mutex); \
    } while(0)

    #define SET_PROGRESS(cur, total) do { \
        pthread_mutex_lock(&state->mutex); \
        state->progress = cur; state->progressTotal = total; \
        pthread_mutex_unlock(&state->mutex); \
    } while(0)

    #define SET_ERROR(msg) do { \
        pthread_mutex_lock(&state->mutex); \
        snprintf(state->errorMsg, 256, "%s", msg); \
        state->error = 1; \
        pthread_mutex_unlock(&state->mutex); \
    } while(0)

    SET_STATUS("Loading configuration...");

    char *configFilePath;
    asprintf(&configFilePath, "%s/config.json", configPath);
    FILE *configFile = fopen(configFilePath, "r");
    free(configFilePath);

    if (configFile == NULL) {
        printf("Config file config.json is missing, exiting..\n");
        SET_ERROR("Initialization error");
        return NULL;
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

    condPrintConfigError(configObj, "Your config file isn't valid JSON.");

    json_object *configLaunchers = NULL;
    json_object_object_get_ex(configObj, "launchers", &configLaunchers);
    condPrintConfigError(configLaunchers, "Your launcher specification is invalid.");

    json_object *configForOpenGameDb;
    json_object_object_get_ex(configObj, "opengamedb",
            &configForOpenGameDb);

    char *openGameDbPath = NULL;
    if (configForOpenGameDb) {
        openGameDbPath = strdup(json_object_get_string(configForOpenGameDb));
        printf("Found OpenGameDb at %s (from config)\n", openGameDbPath);
    }
    else {
        // Fallback: check ~/.offblast/opengamedb
        char *homePath = getenv("HOME");
        char *fallbackPath;
        asprintf(&fallbackPath, "%s/.offblast/opengamedb", homePath);

        if (access(fallbackPath, F_OK) == 0) {
            openGameDbPath = fallbackPath;
            printf("Found OpenGameDb at %s (default location)\n", openGameDbPath);
        }
        else {
            free(fallbackPath);
            // Fallback: check current directory
            if (access("opengamedb", F_OK) == 0) {
                openGameDbPath = strdup("opengamedb");
                printf("Found OpenGameDb at ./opengamedb (current directory)\n");
            }
            else {
                printf("ERROR: OpenGameDB not found.\n");
                printf("       Checked: config.json 'opengamedb' field\n");
                printf("       Checked: %s/.offblast/opengamedb\n", homePath);
                printf("       Checked: ./opengamedb\n");
                printf("       Please download OpenGameDB from: https://github.com/karlforshaw/opengamedb\n");
                exit(1);
            }
        }
    }

    // Store the OpenGameDB path for later use (e.g., during rescan)
    offblast->openGameDbPath = openGameDbPath;

    // Load platform display names from names.csv
    SET_STATUS("Loading platform names...");
    loadPlatformNames(openGameDbPath);

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

    json_object *configShowInstalledOnly;
    json_object_object_get_ex(configObj, "show_installed_only",
            &configShowInstalledOnly);
    if (configShowInstalledOnly) {
        offblast->showInstalledOnly = json_object_get_boolean(configShowInstalledOnly);
        printf("Show installed only: %s\n", offblast->showInstalledOnly ? "yes" : "no");
    }

    // Parse Steam API config
    json_object *configSteam;
    json_object_object_get_ex(configObj, "steam", &configSteam);
    offblast->steamApiKey[0] = '\0';
    offblast->steamId[0] = '\0';
    if (configSteam) {
        json_object *steamApiKey, *steamId;
        json_object_object_get_ex(configSteam, "api_key", &steamApiKey);
        json_object_object_get_ex(configSteam, "steam_id", &steamId);

        if (steamApiKey) {
            strncpy(offblast->steamApiKey, json_object_get_string(steamApiKey), 63);
            offblast->steamApiKey[63] = '\0';
        }
        if (steamId) {
            strncpy(offblast->steamId, json_object_get_string(steamId), 31);
            offblast->steamId[31] = '\0';
        }

        if (offblast->steamApiKey[0] && offblast->steamId[0]) {
            printf("Steam API configured for user %s\n", offblast->steamId);
        }
    }

	// Parse SteamGridDB API key
	json_object *sgdbApiKey;
	offblast->steamGridDbApiKey[0] = '\0';
	if (json_object_object_get_ex(configObj, "steamgriddb_api_key", &sgdbApiKey)) {
		const char *keyStr = json_object_get_string(sgdbApiKey);
		if (keyStr && strlen(keyStr) > 0) {
			strncpy(offblast->steamGridDbApiKey, keyStr, 127);
			offblast->steamGridDbApiKey[127] = '\0';
			printf("SteamGridDB API configured\n");
		}
	}

    char *launchTargetDbPath;
    asprintf(&launchTargetDbPath, "%s/launchtargets.bin", configPath);
    OffblastDbFile launchTargetDb = {0};
    SET_STATUS("Initializing game database...");
    if (!InitDbFile(launchTargetDbPath, &launchTargetDb, 
                sizeof(LaunchTarget))) 
    {
        printf("couldn't initialize path db, exiting\n");
        SET_ERROR("Initialization error");
        return NULL;
    }
    LaunchTargetFile *launchTargetFile = 
        (LaunchTargetFile*) launchTargetDb.memory;
    offblast->launchTargetFile = launchTargetFile;
    free(launchTargetDbPath);

    char *descriptionDbPath;
    asprintf(&descriptionDbPath, "%s/descriptions.bin", configPath);
    offblast->descriptionDb = (OffblastDbFile){0};
    if (!InitDbFile(descriptionDbPath, &offblast->descriptionDb,
                1))
    {
        printf("couldn't initialize the descriptions file, exiting\n");
        SET_ERROR("Initialization error");
        return NULL;
    }
    offblast->descriptionFile =
        (OffblastBlobFile*) offblast->descriptionDb.memory;
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


    SET_STATUS("Loading game metadata...");
    // § Scrape the opengamedb
    struct dirent *openGameDbEntry;
    DIR *openGameDbDir = opendir(openGameDbPath);
    if (openGameDbDir == NULL) {
        printf("ERROR: Cannot access OpenGameDB directory: '%s'\n", openGameDbPath);
        printf("       Please check that the 'opengamedb' path in config.json is correct\n");
        printf("       You can download OpenGameDB from: https://github.com/karlforshaw/opengamedb\n");
        SET_ERROR("Initialization error");
        return NULL;
    }

    while ((openGameDbEntry = readdir(openGameDbDir)) != NULL) {
        if (openGameDbEntry->d_name[0] == '.') continue;
        if (strcmp(openGameDbEntry->d_name, ".") == 0) continue;
        if (strcmp(openGameDbEntry->d_name, "..") == 0) continue;
        char *ext = strrchr((char*)openGameDbEntry->d_name, '.');
        if (ext == NULL) continue;

        if (strcmp(ext, ".csv") == 0) {
            // Skip steam.csv - Steam games come from API, not OpenGameDB
            if (strcmp(openGameDbEntry->d_name, "steam.csv") == 0) {
                printf("Skipping steam.csv (Steam games imported via API)\n");
                continue;
            }
            printf("Importing game data from %s\n", openGameDbEntry->d_name);
        }
        else {
            continue;
        }

        char *fileNameSplit = strtok(openGameDbEntry->d_name, ".");
        uint32_t platformScraped = 0;
        for (uint32_t i=0; i < launchTargetFile->nEntries; ++i) {
            if (strcmp(launchTargetFile->entries[i].platform, 
                        &fileNameSplit[0]) == 0) 
            {
                printf("%s already scraped.\n", &fileNameSplit[0]);
                platformScraped = 1;
                break;
            }
        }

        if (!platformScraped) {

            char statusMsg[256];
            snprintf(statusMsg, 256, "Loading %s games...", fileNameSplit);
            SET_STATUS(statusMsg);

            printf("Pulling data in from the opengamedb.\n");
            char *openGameDbPlatformPath;
            asprintf(&openGameDbPlatformPath, "%s/%s.csv", 
                    openGameDbPath, 
                    openGameDbEntry->d_name);
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

                    // Update progress every 10 games to reduce mutex overhead
                    if (onRow % 10 == 0) {
                        pthread_mutex_lock(&offblast->loadingState.mutex);
                        offblast->loadingState.progress = onRow;
                        offblast->loadingState.progressTotal = 0;  // Unknown total
                        pthread_mutex_unlock(&offblast->loadingState.mutex);
                    }

                    char *gameName = getCsvField(csvLine, 1);
                    char *gameDate = getCsvField(csvLine, 2);
                    char *scoreString = getCsvField(csvLine, 3);
                    char *metaScoreString = getCsvField(csvLine, 4);
                    char *description = getCsvField(csvLine, 6);
                    char *coverArtUrl = getCsvField(csvLine, 7);
                    char *gameId = getCsvField(csvLine, 8);

                    char *gameSeed;

                    // Include title_id in signature if available to differentiate regional versions
                    if (gameId != NULL && strlen(gameId) > 0) {
                        asprintf(&gameSeed, "%s_%s_%s",
                                &fileNameSplit[0], gameName, gameId);
                    } else {
                        asprintf(&gameSeed, "%s_%s",
                                &fileNameSplit[0], gameName);
                    }

                    uint64_t targetSignature[2] = {0, 0};
                    lmmh_x64_128(gameSeed, strlen(gameSeed), 33,
                            targetSignature);

                    int32_t indexOfEntry = launchTargetIndexByTargetSignature(
                            launchTargetFile, targetSignature[0]);

                    if (indexOfEntry == -1) {

                        void *pLaunchTargetMemory = growDbFileIfNecessary(
                                    &launchTargetDb,
                                    sizeof(LaunchTarget),
                                    OFFBLAST_DB_TYPE_FIXED);

                        if(pLaunchTargetMemory == NULL) {
                            printf("Couldn't expand the db file to accomodate"
                                    " all the targets\n");
                            SET_ERROR("Initialization error");
                            return NULL;
                        }
                        else {
                            launchTargetFile =
                                (LaunchTargetFile*) pLaunchTargetMemory;
                            offblast->launchTargetFile = launchTargetFile;
                        }

                        printf("\n--\nAdding: \n%s\n%" PRIu64 "\n%s\n%s\ng: %s\n\nm: %s\n%s\n",
                                gameSeed,
                                targetSignature[0],
                                gameName,
                                gameDate,
                                scoreString, metaScoreString, gameId);

                        LaunchTarget *newEntry =
                            &launchTargetFile->entries[launchTargetFile->nEntries];
                        printf("writing new game to %p\n", newEntry);

                        // Zero the entire structure to ensure null-terminated strings
                        memset(newEntry, 0, sizeof(LaunchTarget));

                        newEntry->targetSignature = targetSignature[0];

                        strncpy(newEntry->name, gameName, OFFBLAST_NAME_MAX - 1);
                        newEntry->name[OFFBLAST_NAME_MAX - 1] = '\0';

                        strncpy(newEntry->platform, &fileNameSplit[0], 255);
                        newEntry->platform[255] = '\0';

                        strncpy(newEntry->coverUrl, coverArtUrl, PATH_MAX - 1);
                        newEntry->coverUrl[PATH_MAX - 1] = '\0';

                        strncpy(newEntry->id, gameId, OFFBLAST_NAME_MAX - 1);
                        newEntry->id[OFFBLAST_NAME_MAX - 1] = '\0';

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
                            float gfScore = atof(scoreString);
                            // Validate: must be numeric and in valid range
                            if (gfScore > 0 && gfScore <= 5.0) {
                                score = gfScore * 2 * 10;
                            }
                        }
                        if (strlen(metaScoreString) != 0) {
                            float metaScore = atof(metaScoreString);
                            // Validate: must be numeric and in valid range
                            if (metaScore > 0 && metaScore <= 100) {
                                if (score == -1) {
                                    score = metaScore;
                                }
                                else {
                                    score = (score + metaScore) / 2;
                                }
                            }
                        }

                        // If no valid score, use sentinel value
                        if (score == -1) {
                            score = 999;
                        }

                        void *pDescriptionFile = growDbFileIfNecessary(
                                    &offblast->descriptionDb,
                                    sizeof(OffblastBlob)
                                        + strlen(description),
                                    OFFBLAST_DB_TYPE_BLOB); 

                        if(pDescriptionFile == NULL) {
                            printf("Couldn't expand the description file to "
                                    "accomodate all the descriptions\n");
                            SET_ERROR("Initialization error");
                            return NULL;
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

                        newDescription->targetSignature = targetSignature[0];
                        newDescription->length = strlen(description);

                        memcpy(&newDescription->content, description, 
                                strlen(description));
                        *(newDescription->content + strlen(description)) = '\0';

                        newEntry->descriptionOffset =
                            offblast->descriptionFile->cursor;

                        offblast->descriptionFile->cursor +=
                            sizeof(OffblastBlob) + strlen(description) + 1;

                        printf("Stored description for '%s' at offset %lu, cursor now %lu\n",
                                gameName, newEntry->descriptionOffset,
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
                                targetSignature[0],
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
    }


    SET_STATUS("Loading user profiles...");

    json_object *usersObject = NULL;
    json_object_object_get_ex(configObj, "users", &usersObject);

    offblast->nUsers = json_object_array_length(usersObject);

    offblast->users = calloc(offblast->nUsers, sizeof(User));

    uint32_t iUser;
    for (iUser = 0; iUser < offblast->nUsers; iUser++) {
        json_object *workingUserNode = json_object_array_get_idx(usersObject, iUser);
        User *pUser = &offblast->users[iUser];

        // Initialize custom field arrays
        pUser->numCustomFields = 0;
        pUser->customKeys = NULL;
        pUser->customValues = NULL;

        // Count custom fields first (to allocate arrays)
        int customFieldCount = 0;
        json_object_object_foreach(workingUserNode, key, val) {
            if (strcmp(key, "name") != 0 &&
                strcmp(key, "email") != 0 &&
                strcmp(key, "avatar") != 0) {
                customFieldCount++;
            }
        }

        // Allocate arrays for custom fields
        if (customFieldCount > 0) {
            pUser->customKeys = calloc(customFieldCount, sizeof(char*));
            pUser->customValues = calloc(customFieldCount, sizeof(char*));
        }

        // Parse all fields dynamically
        json_object_object_foreach(workingUserNode, key2, val2) {
            const char *value = json_object_get_string(val2);
            if (value == NULL) continue;

            if (strcmp(key2, "name") == 0) {
                uint32_t len = (strlen(value) < 256) ? strlen(value) : 255;
                memcpy(&pUser->name, value, len);
            }
            else if (strcmp(key2, "email") == 0) {
                uint32_t len = (strlen(value) < 512) ? strlen(value) : 511;
                memcpy(&pUser->email, value, len);
            }
            else if (strcmp(key2, "avatar") == 0) {
                uint32_t len = (strlen(value) < PATH_MAX) ? strlen(value) : PATH_MAX - 1;
                memcpy(&pUser->avatarPath, value, len);
            }
            else {
                // Store as custom field
                pUser->customKeys[pUser->numCustomFields] = strdup(key2);
                pUser->customValues[pUser->numCustomFields] = strdup(value);
                printf("User %s has custom field: %s = %s\n",
                       pUser->name, key2, value);
                pUser->numCustomFields++;
            }
        }
    }

    json_tokener_free(tokener);

    SET_STATUS("Loading playtime data...");
    loadPlaytimeFile();

    offblast->platforms = calloc(MAX_PLATFORMS, 256 * sizeof(char));

    uint32_t nConfigLaunchers = json_object_array_length(configLaunchers);
    uint32_t configLauncherSignatures[nConfigLaunchers];

    SET_STATUS("Setting up launchers...");
    printf("Setting up launchers.\n");
    offblast->launchers = calloc(nConfigLaunchers, sizeof(Launcher));

    for (int i=0; i < nConfigLaunchers; ++i) {

        json_object *launcherNode= NULL;
        launcherNode = json_object_array_get_idx(configLaunchers, i);

        // Create a stable signature using only type + platform
        // This allows config changes without orphaning games
        uint32_t configLauncherSignature = 0;

        Launcher *theLauncher = &offblast->launchers[offblast->nLaunchers++];

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


        json_object_object_get_ex(launcherNode, "type",
                &typeStringNode);
        theType = json_object_get_string(typeStringNode);
        if (strlen(theType) >= 256) {
            condPrintConfigError(NULL, 
                    "One of your launcher types has too many characters");
        }
        memcpy(&theLauncher->type, theType, strlen(theType));

        json_object_object_get_ex(launcherNode, "name",
                &nameStringNode);

        if (nameStringNode) {
            theName= json_object_get_string(nameStringNode);
            if (strlen(theName) >= 64) {
                condPrintConfigError(NULL, 
                        "One of your launcher names is too long.");
            }
            memcpy(&theLauncher->name, theName, strlen(theName));
        }

        // Handle standard directory-based launchers
        if (strcmp("standard", theLauncher->type) == 0)
        {

            // Handle rom_path (required for standard launchers)
            json_object_object_get_ex(launcherNode, "rom_path",
                    &romPathStringNode);
            theRomPath = json_object_get_string(romPathStringNode);
            if (theRomPath != NULL) {
                memcpy(&theLauncher->romPath, theRomPath, strlen(theRomPath));
            } else {
                printf("ERROR: standard launcher at index %d is missing 'rom_path' field\n", i);
                printf("       Please add a rom_path to your launcher in config.json\n");
            }

            // Handle extension (optional if scan_pattern is provided)
            json_object_object_get_ex(launcherNode, "extension",
                    &extensionStringNode);
            theExtension = json_object_get_string(extensionStringNode);
            if (theExtension != NULL) {
                if (strlen(theExtension) >= 32) {
                    condPrintConfigError(NULL,
                            "One of your launchers has too many extensions");
                }
                memcpy(&theLauncher->extension,
                        theExtension, strlen(theExtension));
            }

            // Handle scan_pattern (optional - for recursive/pattern-based scanning)
            json_object *scanPatternNode = NULL;
            json_object_object_get_ex(launcherNode, "scan_pattern",
                    &scanPatternNode);
            const char *theScanPattern = json_object_get_string(scanPatternNode);
            if (theScanPattern != NULL) {
                if (strlen(theScanPattern) >= 256) {
                    condPrintConfigError(NULL,
                            "One of your launchers' scan_pattern is too long");
                }
                memcpy(&theLauncher->scanPattern,
                        theScanPattern, strlen(theScanPattern));
                printf("Launcher %d using scan pattern: %s\n", i, theScanPattern);
            } else if (theExtension == NULL) {
                // Must have either extension or scan_pattern
                printf("ERROR: standard launcher at index %d has neither 'extension' nor 'scan_pattern'\n", i);
                printf("       Please add either an extension or scan_pattern to your launcher\n");
            }

            // Handle match_field (optional - defaults to "title")
            json_object *matchFieldNode = NULL;
            json_object_object_get_ex(launcherNode, "match_field",
                    &matchFieldNode);
            const char *theMatchField = json_object_get_string(matchFieldNode);
            if (theMatchField != NULL) {
                if (strlen(theMatchField) >= 32) {
                    condPrintConfigError(NULL,
                            "One of your launchers' match_field is too long");
                }
                memcpy(&theLauncher->matchField,
                        theMatchField, strlen(theMatchField));
                printf("Launcher %d using match field: %s\n", i, theMatchField);
            } else {
                // Default to "title" for backwards compatibility
                strcpy(theLauncher->matchField, "title");
            }

            // Handle path_is_match_string (optional - defaults to false)
            json_object *pathIsMatchStringNode = NULL;
            json_object_object_get_ex(launcherNode, "path_is_match_string",
                    &pathIsMatchStringNode);
            if (pathIsMatchStringNode != NULL) {
                theLauncher->pathIsMatchString = json_object_get_boolean(pathIsMatchStringNode);
                if (theLauncher->pathIsMatchString) {
                    printf("Launcher %d will store match string as path\n", i);
                }
            } else {
                theLauncher->pathIsMatchString = 0;  // Default to false
            }

            // Handle platform (required for standard launchers)
            json_object_object_get_ex(launcherNode, "platform",
                    &platformStringNode);
            thePlatform = json_object_get_string(platformStringNode);
            if (thePlatform != NULL) {
                if (strlen(thePlatform) >= 256) {
                    condPrintConfigError(NULL,
                            "One of your launchers' platform strings is too long.");
                }
                memcpy(&theLauncher->platform,
                        thePlatform, strlen(thePlatform));
            } else {
                printf("ERROR: standard launcher at index %d is missing 'platform' field\n", i);
                printf("       Please add a platform to your launcher in config.json\n");
            }

            // Handle command (required for standard launchers)
            json_object_object_get_ex(launcherNode, "cmd",
                    &cmdStringNode);
            theCommand = json_object_get_string(cmdStringNode);
            if (theCommand != NULL) {
                if (strlen(theCommand) >= 512) {
                    condPrintConfigError(NULL,
                            "One of your launchers' command strings is too long.");
                }
                memcpy(&theLauncher->cmd, theCommand, strlen(theCommand));
            } else {
                printf("ERROR: standard launcher at index %d is missing 'cmd' field\n", i);
                printf("       Please add a cmd to your launcher in config.json\n");
            }


        }
        else if (strcmp("steam", theLauncher->type) == 0){
            memcpy(&theLauncher->platform,
                    "steam", strlen("steam"));
        }
        else {
            printf("Unsupported Launcher Type: %s\n", theLauncher->type);
            continue;
        }

        // Calculate launcher signature from type + platform only
        // This creates a stable identifier that survives config changes
        char signatureInput[512];
        snprintf(signatureInput, sizeof(signatureInput), "%s:%s",
                theLauncher->type, theLauncher->platform);
        lmmh_x86_32(signatureInput, strlen(signatureInput), 33, &configLauncherSignature);
        theLauncher->signature = configLauncherSignature;
        configLauncherSignatures[i] = configLauncherSignature;
        printf("Launcher signature for %s:%s = %u\n",
                theLauncher->type, theLauncher->platform, configLauncherSignature);


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


        // Import games based on launcher type
        if (strcmp(theLauncher->type, "steam") == 0) {
            SET_STATUS("Fetching Steam library...");
            importFromSteam(theLauncher);
        }
        else {
            // All directory-based launchers use the same import logic
            // This includes: standard, custom, retroarch, cemu, rpcs3, scummvm
            char statusMsg[256];
            snprintf(statusMsg, 256, "Scanning %s games...",
                     strlen(theLauncher->name) > 0 ? theLauncher->name : theLauncher->platform);
            SET_STATUS(statusMsg);
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

    // Clean up orphaned games (games from launchers no longer in config)
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
            printf("ORPHANED GAME: %s (signature %u)\n",
                    launchTargetFile->entries[i].name,
                    launchTargetFile->entries[i].launcherSignature);
            printf("       Clearing path and resetting for re-matching\n");

            // Clear launcher association, path, and matchScore
            // This allows the ROM to be re-matched by a replacement launcher
            launchTargetFile->entries[i].launcherSignature = 0;
            memset(launchTargetFile->entries[i].path, 0x00, PATH_MAX);
            launchTargetFile->entries[i].matchScore = 0.0f;
        }
    }

    printf("DEBUG - got %u platforms\n", offblast->nPlatforms);

    close(launchTargetDb.fd);



    // Window manager and session type detection
    char *windowManager = getenv("XDG_CURRENT_DESKTOP");
    char *sessionType = getenv("XDG_SESSION_TYPE");

    if (windowManager) {
        printf("Detected desktop: %s\n", windowManager);
        if (strcmp(windowManager, "i3") == 0) {
            offblast->windowManager = WINDOW_MANAGER_I3;
            system("i3-msg move to workspace offblast && i3-msg workspace offblast");
        }
        else if (strcmp(windowManager, "GNOME") == 0 || strstr(windowManager, "GNOME") != NULL) {
            offblast->windowManager = WINDOW_MANAGER_GNOME;
        }
        else if (strcmp(windowManager, "KDE") == 0 || strstr(windowManager, "KDE") != NULL) {
            offblast->windowManager = WINDOW_MANAGER_KDE;
            printf("KDE Plasma detected\n");
        }
        else {
            printf("Warning: Window manager '%s' not explicitly supported, using defaults\n", windowManager);
            offblast->windowManager = WINDOW_MANAGER_GNOME;  // Fallback
        }
    }

    if (sessionType) {
        printf("Session type: %s\n", sessionType);
        if (strcmp(sessionType, "wayland") == 0) {
            offblast->sessionType = SESSION_TYPE_WAYLAND;
        } else {
            offblast->sessionType = SESSION_TYPE_X11;
        }
    } else {
        offblast->sessionType = SESSION_TYPE_X11;  // Assume X11 if not set
    }

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    offblast->XDisplay = XOpenDisplay(NULL);
    if(offblast->XDisplay == NULL){
        printf("Couldn't connect to Xserver\n");
        SET_ERROR("X11 connection failed");
        return NULL;
    }

    SET_STATUS("Setting up image system...");
    // CREATE IMAGE STORE
    pthread_mutex_init(&offblast->imageStoreLock, NULL);
    offblast->imageStore = calloc(IMAGE_STORE_SIZE, sizeof(Image));
    offblast->numLoadedTextures = 0;
    for (uint32_t i = 0; i < IMAGE_STORE_SIZE; ++i) {
        offblast->imageStore[i].lastUsedTick = SDL_GetTicks();
    }
    assert(offblast->imageStore);

    SET_STATUS("Starting worker threads...");
    // CREATE WORKER THREADS
    offblast->numImageLoadThreads = sysconf(_SC_NPROCESSORS_CONF);
    printf("THREADS: %d\n", offblast->numImageLoadThreads);
    --offblast->numImageLoadThreads;
    offblast->imageLoadThreads = calloc(offblast->numImageLoadThreads, sizeof(pthread_t));

    for (uint32_t i = 0; i < offblast->numImageLoadThreads; ++i) {
        pthread_create(
                &offblast->imageLoadThreads[i],
                NULL,
                imageLoadMain,
                (void*)offblast);
    }

    SET_STATUS("Initializing interface...");
    // § Init UI
    MainUi *mainUi = &offblast->mainUi;

    needsReRender(offblast->window);
    mainUi->horizontalAnimation = calloc(1, sizeof(Animation));
    mainUi->verticalAnimation = calloc(1, sizeof(Animation));
    mainUi->infoAnimation = calloc(1, sizeof(Animation));
    mainUi->rowNameAnimation = calloc(1, sizeof(Animation));
    mainUi->menuAnimation = calloc(1, sizeof(Animation));
    mainUi->menuNavigateAnimation = calloc(1, sizeof(Animation));
    mainUi->contextMenuAnimation = calloc(1, sizeof(Animation));
    mainUi->contextMenuNavigateAnimation = calloc(1, sizeof(Animation));
	mainUi->coverBrowserAnimation = calloc(1, sizeof(Animation));

    mainUi->showMenu = 0;
    mainUi->showContextMenu = 0;
    mainUi->showSearch = 0;
	mainUi->showCoverBrowser = 0;

	// Initialize cover browser grid dimensions
	mainUi->coverBrowserCoversPerRow = 5;
	mainUi->coverBrowserVisibleRows = 2;

    // Init Menu
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
    mainUi->menuScrollOffset = 0;
    mainUi->maxVisibleMenuItems = 0; // Will be calculated based on window height

    if (1) {
        mainUi->menuItems[mainUi->numMenuItems].label = "Shut Down Machine";
        mainUi->menuItems[mainUi->numMenuItems].callback = shutdownMachine;
        mainUi->numMenuItems++;
    }

    // Init Context Menu (right-side game menu)
    mainUi->contextMenuItems = calloc(6, sizeof(MenuItem));
    mainUi->contextMenuItems[0].label = "Browse Covers";
    mainUi->contextMenuItems[0].callback = doBrowseCovers;
    mainUi->contextMenuItems[1].label = "Rescan Launcher for New Games";
    mainUi->contextMenuItems[1].callback = doRescanLauncher;
    mainUi->contextMenuItems[2].label = "Refresh Platform Metadata/Covers";
    mainUi->contextMenuItems[2].callback = doRescrapePlatform;
    mainUi->contextMenuItems[3].label = "Refresh Game Metadata/Covers";
    mainUi->contextMenuItems[3].callback = doRescrapeGame;
    mainUi->contextMenuItems[4].label = "Copy Cover Filename";
    mainUi->contextMenuItems[4].callback = doCopyCoverFilename;
    mainUi->contextMenuItems[5].label = "Refresh Cover";
    mainUi->contextMenuItems[5].callback = doRefreshCover;
    mainUi->numContextMenuItems = 6;
    mainUi->contextMenuCursor = 0;


    // Signal completion
    pthread_mutex_lock(&state->mutex);
    state->complete = 1;
    pthread_mutex_unlock(&state->mutex);

    return NULL;
}

int main(int argc, char** argv) {

    printf("\nStarting up OffBlast with %d args.\n\n", argc);
    offblast = calloc(1, sizeof(OffblastUi));
    offblast->gnomeResumeWindowId = -1;  // Initialize to invalid


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

    // § EARLY SDL/OpenGL INIT FOR LOADING SCREEN
    printf("Initializing SDL for loading screen...\n");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        printf("SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    offblast->window = SDL_CreateWindow("OffBlast",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1920, 1080,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);

    if (offblast->window == NULL) {
        printf("SDL window creation failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(offblast->window);
    if (glContext == NULL) {
        printf("OpenGL context creation failed: %s\n", SDL_GetError());
        return 1;
    }

    glewInit();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Get actual window size
    SDL_GetWindowSize(offblast->window, &offblast->winWidth, &offblast->winHeight);
    printf("Window size: %dx%d\n", offblast->winWidth, offblast->winHeight);

    offblast->running = 1;

    // § LOAD MINIMAL ASSETS FOR LOADING SCREEN
    printf("Loading loading screen assets...\n");

    // Calculate point sizes for fonts
    offblast->infoPointSize = goldenRatioLarge(offblast->winWidth, 9);
    offblast->textBitmapHeight = 4096;  // Large atlas for UTF-8 + high-res displays
    offblast->textBitmapWidth = 4096;

    // Load logo image
    int w, h, n;
    stbi_set_flip_vertically_on_load(1);
    unsigned char *logoData = stbi_load("offblast_loading.png", &w, &h, &n, 4);
    if (logoData) {
        glGenTextures(1, &offblast->logoImage.textureHandle);
        imageToGlTexture(&offblast->logoImage.textureHandle, logoData, w, h);
        offblast->logoImage.width = w;
        offblast->logoImage.height = h;
        stbi_image_free(logoData);
        printf("Loaded logo: %dx%d\n", w, h);
    } else {
        printf("Warning: Could not load offblast_loading.png for loading screen\n");
    }

    // Load info font for status text
    FILE *fontFd = fopen("./fonts/Roboto-Regular.ttf", "r");
    if (fontFd) {
        fseek(fontFd, 0, SEEK_END);
        long fontBytes = ftell(fontFd);
        fseek(fontFd, 0, SEEK_SET);
        unsigned char *fontContents = malloc(fontBytes);
        fread(fontContents, fontBytes, 1, fontFd);
        fclose(fontFd);

        unsigned char *infoAtlas = calloc(offblast->textBitmapWidth * offblast->textBitmapHeight, sizeof(unsigned char));
        offblast->infoNumChars = packFont(fontContents, offblast->infoPointSize,
            infoAtlas, offblast->textBitmapWidth, offblast->textBitmapHeight,
            &offblast->infoCharData, &offblast->infoCodepoints);

        if (offblast->infoNumChars == 0) {
            printf("Warning: Failed to pack info font\n");
        }

        glGenTextures(1, &offblast->infoTextTexture);
        glBindTexture(GL_TEXTURE_2D, offblast->infoTextTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED,
            offblast->textBitmapWidth, offblast->textBitmapHeight,
            0, GL_RED, GL_UNSIGNED_BYTE, infoAtlas);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        free(infoAtlas);
        free(fontContents);
        printf("Loaded font atlas\n");
    } else {
        printf("Warning: Could not load font for loading screen\n");
    }

    // Load shaders for rendering
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    GLint textVertShader = loadShaderFile("shaders/text.vert", GL_VERTEX_SHADER);
    GLint textFragShader = loadShaderFile("shaders/text.frag", GL_FRAGMENT_SHADER);
    if (textVertShader && textFragShader) {
        offblast->textProgram = createShaderProgram(textVertShader, textFragShader);
        offblast->textAlphaUni = glGetUniformLocation(offblast->textProgram, "myAlpha");
    }

    GLint imageVertShader = loadShaderFile("shaders/image.vert", GL_VERTEX_SHADER);
    GLint imageFragShader = loadShaderFile("shaders/image.frag", GL_FRAGMENT_SHADER);
    if (imageVertShader && imageFragShader) {
        offblast->imageProgram = createShaderProgram(imageVertShader, imageFragShader);
        offblast->imageTranslateUni = glGetUniformLocation(offblast->imageProgram, "myOffset");
        offblast->imageAlphaUni = glGetUniformLocation(offblast->imageProgram, "myAlpha");
        offblast->imageDesaturateUni = glGetUniformLocation(offblast->imageProgram, "whiteMix");
    }

    printf("Shaders loaded\n");

    // Initialize player controller state before loading screen
    offblast->player.jsIndex = -1;
    offblast->player.usingController = NULL;

    // § START LOADING SCREEN
    printf("Starting loading screen...\n");
    pthread_mutex_init(&offblast->loadingState.mutex, NULL);
    offblast->loadingMode = 1;

    // Set initial status
    pthread_mutex_lock(&offblast->loadingState.mutex);
    snprintf(offblast->loadingState.status, 256, "Initializing...");
    offblast->loadingState.progress = 0;
    offblast->loadingState.progressTotal = 0;
    offblast->loadingState.complete = 0;
    offblast->loadingState.error = 0;
    pthread_mutex_unlock(&offblast->loadingState.mutex);

    // Spawn init thread
    pthread_t initThread;
    pthread_create(&initThread, NULL, initThreadFunc, offblast);

    // Loading screen render loop
    while (offblast->loadingMode && offblast->running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                offblast->running = 0;
                offblast->loadingMode = 0;
            }
            else if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_KEYDOWN) {
                // If there's an error, any button/key press exits
                pthread_mutex_lock(&offblast->loadingState.mutex);
                if (offblast->loadingState.error) {
                    printf("Input received during error - exiting\n");
                    offblast->running = 0;
                    offblast->loadingMode = 0;
                    pthread_mutex_unlock(&offblast->loadingState.mutex);
                    break;
                }
                pthread_mutex_unlock(&offblast->loadingState.mutex);
            }
            else if (event.type == SDL_CONTROLLERDEVICEADDED) {
                // Handle controller connection during loading screen
                SDL_ControllerDeviceEvent *devEvent = (SDL_ControllerDeviceEvent*)&event;
                printf("Controller added during loading: %d\n", devEvent->which);
                if (SDL_IsGameController(devEvent->which) == SDL_TRUE &&
                    offblast->player.jsIndex == -1) {
                    SDL_GameController *controller = SDL_GameControllerOpen(devEvent->which);
                    if (controller) {
                        offblast->player.jsIndex = devEvent->which;
                        offblast->player.usingController = controller;
                        printf("Controller connected during loading\n");
                    }
                }
            }
        }

        renderLoadingScreen(offblast);

        // Check if init complete
        pthread_mutex_lock(&offblast->loadingState.mutex);
        if (offblast->loadingState.complete && !offblast->exitAnimating) {
            // Start exit animation
            offblast->exitAnimating = 1;
            offblast->exitAnimationStartTick = SDL_GetTicks();
        }
        if (offblast->loadingState.error && !offblast->loadingState.complete) {
            // Mark as complete to stop loading thread, but stay in loading mode to show error
            offblast->loadingState.complete = 1;
        }
        if (offblast->loadingState.error && offblast->loadingState.complete) {
            // Show error message with instruction to exit
            // Error is displayed by renderLoadingScreen, user must press button to exit
        }
        pthread_mutex_unlock(&offblast->loadingState.mutex);

        // Check if exit animation complete (0.61 seconds = 610ms)
        if (offblast->exitAnimating) {
            uint32_t elapsed = SDL_GetTicks() - offblast->exitAnimationStartTick;
            if (elapsed >= 610) {
                offblast->loadingMode = 0;
            }
        }

        SDL_Delay(16);  // ~60 FPS
    }

    pthread_join(initThread, NULL);
    pthread_mutex_destroy(&offblast->loadingState.mutex);

    printf("Loading screen complete\n");

    // Heavy non-GL init is done in thread above.
    // Now run GL-dependent initialization in main thread:

    // Additional GL setup
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);

    // Set up convenience pointers for GL init
    MainUi *mainUi = &offblast->mainUi;
    PlayerSelectUi *playerSelectUi = &offblast->playerSelectUi;

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

    offblast->textBitmapHeight = 4096;  // Large atlas for UTF-8 + high-res displays
    offblast->textBitmapWidth = 4096;

    // Calculate all font point sizes
    offblast->titlePointSize = goldenRatioLarge(offblast->winWidth, 7);
    offblast->infoPointSize = goldenRatioLarge(offblast->winWidth, 9);
    offblast->debugPointSize = goldenRatioLarge(offblast->winWidth, 11);

    // TODO this should be a function karl

    unsigned char *titleAtlas = calloc(offblast->textBitmapWidth * offblast->textBitmapHeight, sizeof(unsigned char));

    offblast->titleNumChars = packFont(fontContents, offblast->titlePointSize,
            titleAtlas,
            offblast->textBitmapWidth,
            offblast->textBitmapHeight,
            &offblast->titleCharData, &offblast->titleCodepoints);

    if (offblast->titleNumChars == 0) {
        printf("ERROR: Failed to pack title font\n");
    }

    glGenTextures(1, &offblast->titleTextTexture);
    glBindTexture(GL_TEXTURE_2D, offblast->titleTextTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 
            offblast->textBitmapWidth, offblast->textBitmapHeight, 
            0, GL_RED, GL_UNSIGNED_BYTE, titleAtlas); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    //stbi_write_png("titletest.png", offblast->textBitmapWidth, offblast->textBitmapHeight, 1, titleAtlas, 0);

    free(titleAtlas);
    titleAtlas = NULL;

    unsigned char *infoAtlas = calloc(offblast->textBitmapWidth * offblast->textBitmapHeight, sizeof(unsigned char));

    offblast->infoNumChars = packFont(fontContents, offblast->infoPointSize,
            infoAtlas,
            offblast->textBitmapWidth,
            offblast->textBitmapHeight,
            &offblast->infoCharData, &offblast->infoCodepoints);

    if (offblast->infoNumChars == 0) {
        printf("ERROR: Failed to pack info font\n");
    }

    glGenTextures(1, &offblast->infoTextTexture);
    glBindTexture(GL_TEXTURE_2D, offblast->infoTextTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 
            offblast->textBitmapWidth, offblast->textBitmapHeight, 
            0, GL_RED, GL_UNSIGNED_BYTE, infoAtlas); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    //stbi_write_png("infotest.png", offblast->textBitmapWidth, offblast->textBitmapHeight, 1, infoAtlas, 0);

    free(infoAtlas);
    infoAtlas = NULL;

    unsigned char *debugAtlas = calloc(offblast->textBitmapWidth * offblast->textBitmapHeight, sizeof(unsigned char));

    offblast->debugNumChars = packFont(fontContents, offblast->debugPointSize, debugAtlas,
            offblast->textBitmapWidth,
            offblast->textBitmapHeight,
            &offblast->debugCharData, &offblast->debugCodepoints);

    if (offblast->debugNumChars == 0) {
        printf("ERROR: Failed to pack debug font\n");
    }

    glGenTextures(1, &offblast->debugTextTexture);
    glBindTexture(GL_TEXTURE_2D, offblast->debugTextTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 
            offblast->textBitmapWidth, offblast->textBitmapHeight, 
            0, GL_RED, GL_UNSIGNED_BYTE, debugAtlas); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    //stbi_write_png("debugtest.png", offblast->textBitmapWidth, offblast->textBitmapHeight, 1, debugAtlas, 0);

    free(debugAtlas);
    free(fontContents);
    fontContents = NULL;
    debugAtlas = NULL;

    playerSelectUi->images = calloc(offblast->nUsers, sizeof(Image));
    playerSelectUi->widthForAvatar =
        calloc(offblast->nUsers+1, sizeof(float));
    assert(playerSelectUi->widthForAvatar);
    playerSelectUi->xOffsetForAvatar =
        calloc(offblast->nUsers+1, sizeof(float));
    assert(playerSelectUi->xOffsetForAvatar);

    // Set up UI dimensions needed for avatar sizing
    // (These must match what needsReRender() calculates)
    offblast->winFold = offblast->winHeight * 0.5;
    offblast->winMargin = goldenRatioLarge((double) offblast->winWidth, 5);
    mainUi->boxHeight = goldenRatioLarge(offblast->winWidth, 4);
    mainUi->boxPad = goldenRatioLarge((double) offblast->winWidth, 9);
    mainUi->descriptionWidth = goldenRatioLarge((double) offblast->winWidth, 1) - offblast->winMargin;
    mainUi->descriptionHeight = goldenRatioLarge(offblast->winWidth, 3);

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
            printf("WARNING: Cannot load avatar image for user '%s': '%s'\n",
                    offblast->users[i].name, offblast->users[i].avatarPath);
            if (strlen(offblast->users[i].avatarPath) == 0) {
                printf("         Avatar path is empty in config.json\n");
            } else {
                printf("         Please check that the image file exists and is a valid format (PNG/JPG)\n");
            }
            // TODO use mystery man image
        }
        free(imageData);

    }

    // Gradient Pipeline (text and image pipelines already loaded early for loading screen)
    GLint gradientVertShader = loadShaderFile("shaders/gradient.vert",
            GL_VERTEX_SHADER);
    GLint gradientFragShader = loadShaderFile("shaders/gradient.frag",
            GL_FRAGMENT_SHADER);
    assert(gradientVertShader);
    assert(gradientFragShader);
    offblast->gradientProgram = createShaderProgram(gradientVertShader,
            gradientFragShader);
    assert(offblast->gradientProgram);
    offblast->gradientColorStartUniform = glGetUniformLocation(
            offblast->gradientProgram, "colorStart");
    offblast->gradientColorEndUniform = glGetUniformLocation(
            offblast->gradientProgram, "colorEnd");

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

    // Start fade-in animation for initial screen
    playerSelectUi->fadeInActive = 1;
    playerSelectUi->fadeInStartTick = SDL_GetTicks();

    // Array of all animations for efficient ticking and state checks
    Animation *allAnimations[] = {
        mainUi->horizontalAnimation,
        mainUi->verticalAnimation,
        mainUi->infoAnimation,
        mainUi->rowNameAnimation,
        mainUi->menuAnimation,
        mainUi->menuNavigateAnimation,
        mainUi->contextMenuAnimation,
        mainUi->contextMenuNavigateAnimation,
        mainUi->coverBrowserAnimation
    };
    int numAnimations = sizeof(allAnimations) / sizeof(allAnimations[0]);

    // § Main loop
    while (offblast->running) {

        if (needsReRender(offblast->window) == 1) {
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
                if (event.jaxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                    offblast->joyX = event.jaxis.value;
                }
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
                    case SDL_CONTROLLER_BUTTON_X:
                        if (offblast->mode == OFFBLAST_UI_MODE_MAIN) {
                            printf("X button pressed - refresh current game metadata/covers\n");
                            rescrapeCurrentLauncher(0); // Delete only current cover
                        }
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
                    case SDL_CONTROLLER_BUTTON_START:
                        pressContextMenu();
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
                    } else {
                        offblast->player.jsIndex = devEvent->which;
                        offblast->player.usingController = controller;
                        printf("Controller connected and stored\n");
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
                    if (offblast->player.usingController != NULL) {
                        SDL_GameControllerClose(offblast->player.usingController);
                        offblast->player.usingController = NULL;
                    }
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
                    SDL_RaiseWindow(offblast->window);
                }
                else if (keyEvent->keysym.scancode == SDL_SCANCODE_F) {
                    SDL_SetWindowFullscreen(offblast->window,
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
                else if (keyEvent->keysym.scancode == SDL_SCANCODE_R) {
                    if (offblast->mode == OFFBLAST_UI_MODE_MAIN) {
                        // Check if Shift is held
                        SDL_Keymod modstate = SDL_GetModState();
                        if (modstate & KMOD_SHIFT) {
                            printf("Shift+R pressed - refresh all platform metadata/covers\n");
                            rescrapeCurrentLauncher(1); // 1 = delete all covers
                        } else {
                            printf("R pressed - refresh current game metadata/covers\n");
                            rescrapeCurrentLauncher(0); // 0 = delete only current cover
                        }
                    }
                }
                else {
                    printf("key up %d\n", keyEvent->keysym.scancode);
                }
                // SDL_KEYMOD
                // TODO how to use modifier keys? for jump left and jump right?
            }
        }


            // Handle Joypad movement
            // I'm in two minds about how we should do this, I think there 
            // should be some kind of move cooldown, that might be cool but
            // then how do you pair that with animation durations?
            // If theres no animation in place then there should still be a 
            // move lock, but how do you pair it up?
            if (offblast->joyX > 0 
                    && offblast->joyX / (double) INT16_MAX > 0.75f) 
            {
                changeColumn(1);
            }
            else if (offblast->joyX < 0 && 
                    offblast->joyX / (double) INT16_MAX < -0.75f) 
            {
                changeColumn(0);
            }

            if (offblast->joyY > 0 
                    && offblast->joyY / (double) INT16_MAX > 0.75f) 
            {
                changeRow(1);
            }
            else if (offblast->joyY < 0 && 
                    offblast->joyY / (double) INT16_MAX < -0.75f) 
            {
                changeRow(0);
            }


        // § Player Detection
        // TODO should we do this on every loop?
        if (offblast->player.emailHash == 0) {
            // Auto-select if only one user exists
            if (offblast->nUsers == 1) {
                User *theUser = &offblast->users[0];
                char *email = theUser->email;
                uint32_t emailSignature = 0;

                lmmh_x86_32(email, strlen(email), 33, &emailSignature);

                offblast->player.user = theUser;
                offblast->player.emailHash = emailSignature;
                loadPlaytimeFile();
                updateHomeLists();

                printf("Auto-selected single user: %s\n", theUser->name);
                offblast->mode = OFFBLAST_UI_MODE_MAIN;
            }
            else {
                offblast->mode = OFFBLAST_UI_MODE_PLAYER_SELECT;
            }
        }

        // Periodic texture cleanup - evict textures unused for too long
        static uint32_t lastEvictionCheck = 0;
        uint32_t currentTick = SDL_GetTicks();
        if (currentTick - lastEvictionCheck > 1000) {  // Check every second
            pthread_mutex_lock(&offblast->imageStoreLock);
            evictTexturesOlderThan(TEXTURE_EVICTION_TIME_MS);
            pthread_mutex_unlock(&offblast->imageStoreLock);
            lastEvictionCheck = currentTick;
        }

        // RENDER
        glClearColor(0.0, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);


        if (offblast->mode == OFFBLAST_UI_MODE_MAIN) {



            // § Blocks
            if (mainUi->activeRowset->numRows == 0) {
                char *noGameText = "whoops, no games found.";

                uint32_t centerOfText = getTextLineWidth(noGameText,
                        offblast->titleCharData, offblast->titleCodepoints, offblast->titleNumChars);

                renderText(offblast, 
                        offblast->winWidth / 2 - centerOfText / 2, 
                        2 * (offblast->winHeight / 3 
                            - offblast->titlePointSize / 2),
                        OFFBLAST_TEXT_TITLE, 1, 0, noGameText);
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

                while (yBase < offblast->winHeight + mainUi->boxHeight) {

                    int32_t displacement = 0;
                    UiTile *theTile = rowToRender->tileCursor;
                    Image* cursorImage = 
                        requestImageForTarget(theTile->target, 0);

                    uint32_t theWidth = cursorImage->width;

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
                            < (offblast->winWidth*1.3)) 
                    {

                        /*
                        if (theTile->image.textureHandle == 0) 
                        {
                            //loadTexture(theTile);
                            requestImageForTarget(theTile->target);
                            imageToShow = missingImage;
                        }
                        else 
                            imageToShow = &theTile->image;
                        */
                        // TODO do we need image to show now?
                        imageToShow = requestImageForTarget(theTile->target, 1);

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

                // Render playtime text (if any) right after infoText with reduced alpha
                if (mainUi->playtimeText != NULL) {
                    uint32_t infoWidth = getTextLineWidth(mainUi->infoText, offblast->infoCharData, offblast->infoCodepoints, offblast->infoNumChars);
                    renderText(offblast, offblast->winMargin + infoWidth, pixelY,
                            OFFBLAST_TEXT_INFO, alpha * 0.81f, 0, mainUi->playtimeText);
                }


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

                float xOffset = 0;
                if (mainUi->menuAnimation->animating == 1) {

                    double change = easeInOutCirc(
                            (double)SDL_GetTicks() - 
                            mainUi->menuAnimation->startTick,
                            0.0,
                            1.0,
                            (double)mainUi->menuAnimation->durationMs);

                    if (mainUi->menuAnimation->direction == 0) {
                        change = 1.0 - change;
                    }

                    xOffset = change * (0 - offblast->winWidth * 0.16);
                }

                Color menuColor = {0.0, 0.0, 0.0, 0.85};
                renderGradient(xOffset, 0, 
                        offblast->winWidth * 0.16, offblast->winHeight, 
                        0,
                        menuColor, menuColor);

                // Calculate max visible menu items based on available height
                float menuStartY = offblast->winHeight - 133;
                float menuHeight = menuStartY - 50; // Leave some space at top
                float itemHeight = offblast->infoPointSize * 1.61;
                mainUi->maxVisibleMenuItems = (uint32_t)(menuHeight / itemHeight);

                float itemTransparency = 0.6f;

                // Render visible menu items within viewport
                uint32_t firstVisible = mainUi->menuScrollOffset;
                uint32_t lastVisible = firstVisible + mainUi->maxVisibleMenuItems;
                if (lastVisible > mainUi->numMenuItems) {
                    lastVisible = mainUi->numMenuItems;
                }

                for (uint32_t mi = firstVisible; mi < lastVisible; mi++) {
                    if (mainUi->menuItems[mi].label != NULL) {
                        uint32_t relativeIndex = mi - firstVisible;

                        if (mi == mainUi->menuCursor) itemTransparency = 1.0f;

                        renderText(offblast, xOffset + offblast->winWidth * 0.016,
                                menuStartY - (relativeIndex * itemHeight),
                                OFFBLAST_TEXT_INFO, itemTransparency, 0,
                                mainUi->menuItems[mi].label);

                        itemTransparency = 0.6f;
                    }
                }

                // Render fade effects (after text to ensure they're on top)
                float fadeHeight = offblast->winHeight * 0.16;

                // Bottom fade when more items below viewport
                if (mainUi->menuScrollOffset + mainUi->maxVisibleMenuItems < mainUi->numMenuItems) {
                    Color fadeBottomStart = {0.0, 0.0, 0.0, 0.85};
                    Color fadeBottomEnd = {0.0, 0.0, 0.0, 0.0};
                    renderGradient(xOffset, 50,
                            offblast->winWidth * 0.16, fadeHeight,
                            0, // 0 = vertical gradient
                            fadeBottomStart, fadeBottomEnd);
                }

                // Top fade when items are scrolled above viewport
                if (mainUi->menuScrollOffset > 0) {
                    Color fadeTopStart = {0.0, 0.0, 0.0, 0.0};
                    Color fadeTopEnd = {0.0, 0.0, 0.0, 0.85};
                    renderGradient(xOffset, offblast->winHeight - fadeHeight,
                            offblast->winWidth * 0.16, fadeHeight,
                            0, // 0 = vertical gradient
                            fadeTopStart, fadeTopEnd);
                }
            }

            // § Render Context Menu (right side)
            if (mainUi->showContextMenu) {

                float menuWidth = offblast->winWidth * 0.16;
                float xOffset = 0;
                if (mainUi->contextMenuAnimation->animating == 1) {

                    double change = easeInOutCirc(
                            (double)SDL_GetTicks() -
                            mainUi->contextMenuAnimation->startTick,
                            0.0,
                            1.0,
                            (double)mainUi->contextMenuAnimation->durationMs);

                    if (mainUi->contextMenuAnimation->direction == 0) {
                        change = 1.0 - change;
                    }

                    // Slide in from right (positive offset to 0)
                    xOffset = change * menuWidth;
                }

                Color menuColor = {0.0, 0.0, 0.0, 0.85};
                renderGradient(offblast->winWidth - menuWidth + xOffset, 0,
                        menuWidth, offblast->winHeight,
                        0,
                        menuColor, menuColor);

                float menuStartY = offblast->winHeight - 133;
                float itemHeight = offblast->infoPointSize * 1.61;
                float itemTransparency = 0.6f;

                for (uint32_t mi = 0; mi < mainUi->numContextMenuItems; mi++) {
                    if (mainUi->contextMenuItems[mi].label != NULL) {

                        if (mi == mainUi->contextMenuCursor) itemTransparency = 1.0f;

                        renderText(offblast,
                                offblast->winWidth - menuWidth + xOffset + offblast->winWidth * 0.016,
                                menuStartY - (mi * itemHeight),
                                OFFBLAST_TEXT_INFO, itemTransparency, 0,
                                mainUi->contextMenuItems[mi].label);

                        itemTransparency = 0.6f;
                    }
                }
            }

			// § Render Cover Browser
			if (mainUi->showCoverBrowser) {
				// Dark background overlay
				Color overlayColor = {0.0, 0.0, 0.0, 0.9};
				renderGradient(0, 0, offblast->winWidth, offblast->winHeight, 0,
							   overlayColor, overlayColor);

				if (mainUi->coverBrowserError[0] != '\0') {
					// Show error message
					renderText(offblast,
							   offblast->winWidth * 0.5 - 200,
							   offblast->winHeight * 0.5,
							   OFFBLAST_TEXT_INFO, 1.0, 0,
							   mainUi->coverBrowserError);

					renderText(offblast,
							   offblast->winWidth * 0.5 - 100,
							   offblast->winHeight * 0.5 - 50,
							   OFFBLAST_TEXT_INFO, 0.7, 0,
							   "Press B to close");
				}
				else if (mainUi->coverBrowserState == 0) {  // game_select
					// Render game selection list
					renderText(offblast,
							   offblast->winWidth * 0.3,
							   offblast->winHeight * 0.8,
							   OFFBLAST_TEXT_TITLE, 1.0, 0,
							   "Select Game:");

					if (mainUi->coverBrowserGames) {
						float startY = offblast->winHeight * 0.7;
						float itemHeight = offblast->infoPointSize * 1.8;

						for (uint32_t i = 0; i < mainUi->coverBrowserGames->numGames; i++) {
							float alpha = (i == mainUi->coverBrowserGameCursor) ? 1.0 : 0.6;
							renderText(offblast,
									   offblast->winWidth * 0.3,
									   startY - (i * itemHeight),
									   OFFBLAST_TEXT_INFO, alpha, 0,
									   mainUi->coverBrowserGames->games[i].name);
						}
					}
				}
				else if (mainUi->coverBrowserState == 1) {  // cover_grid
					if (mainUi->coverBrowserCovers) {
						// Calculate grid dimensions first
						float coverHeight = mainUi->boxHeight;
						float coverWidth = coverHeight / 1.5;  // 600x900 aspect ratio
						float gridPadding = mainUi->boxPad;

						// Calculate grid layout
						float totalGridWidth = (mainUi->coverBrowserCoversPerRow * coverWidth) +
											   ((mainUi->coverBrowserCoversPerRow - 1) * gridPadding);
						float gridStartX = (offblast->winWidth - totalGridWidth) / 2;  // Center the grid

						// Calculate how much vertical space we need for 2 rows
						float totalGridHeight = (mainUi->coverBrowserVisibleRows * coverHeight) +
											   ((mainUi->coverBrowserVisibleRows - 1) * gridPadding);

						// Center the grid vertically on screen
						float gridStartY = (offblast->winHeight / 2) + (totalGridHeight / 2) - coverHeight;

						// Calculate title position with golden ratio bottom margin from title to top of grid
						float titleBottomMargin = goldenRatioLarge(offblast->titlePointSize, 1);
						float titleY = gridStartY + coverHeight + titleBottomMargin;

						// Render title centered above grid (using cached width)
						renderText(offblast,
								   offblast->winWidth / 2 - mainUi->coverBrowserTitleWidth / 2,
								   titleY,
								   OFFBLAST_TEXT_TITLE, 1.0, 0,
								   mainUi->coverBrowserTitle);

						// Render visible covers
						uint32_t startIdx = mainUi->coverBrowserScrollOffset * mainUi->coverBrowserCoversPerRow;
						uint32_t endIdx = startIdx + (mainUi->coverBrowserVisibleRows * mainUi->coverBrowserCoversPerRow);
						if (endIdx > mainUi->coverBrowserCovers->numCovers) {
							endIdx = mainUi->coverBrowserCovers->numCovers;
						}

						for (uint32_t i = startIdx; i < endIdx; i++) {
							uint32_t displayRow = (i / mainUi->coverBrowserCoversPerRow) - mainUi->coverBrowserScrollOffset;
							uint32_t displayCol = i % mainUi->coverBrowserCoversPerRow;

							float x = gridStartX + displayCol * (coverWidth + gridPadding);
							float y = gridStartY - displayRow * (coverHeight + gridPadding);

							// Upload texture if ready
							pthread_mutex_lock(&mainUi->coverBrowserThumbsLock);
							if (mainUi->coverBrowserThumbs[i].state == IMAGE_STATE_READY) {
								// Upload to OpenGL
								glGenTextures(1, &mainUi->coverBrowserThumbs[i].textureHandle);
								glBindTexture(GL_TEXTURE_2D, mainUi->coverBrowserThumbs[i].textureHandle);
								glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
								glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
								glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
											mainUi->coverBrowserThumbs[i].width,
											mainUi->coverBrowserThumbs[i].height,
											0, GL_RGBA, GL_UNSIGNED_BYTE,
											mainUi->coverBrowserThumbs[i].atlas);

								// Free the pixel data, keep only the texture
								free(mainUi->coverBrowserThumbs[i].atlas);
								mainUi->coverBrowserThumbs[i].atlas = NULL;
								mainUi->coverBrowserThumbs[i].state = IMAGE_STATE_COMPLETE;
							}
							pthread_mutex_unlock(&mainUi->coverBrowserThumbsLock);

							// Render the thumbnail or placeholder
							if (mainUi->coverBrowserThumbs[i].state == IMAGE_STATE_COMPLETE) {
								// Render actual thumbnail
								float alpha = (i == mainUi->coverBrowserCoverCursor) ? 1.0f : 0.85f;
								renderImage(x, y, coverWidth, coverHeight,
										   &mainUi->coverBrowserThumbs[i], 0.0, alpha);
							} else {
								// Render placeholder while loading
								Color placeholderColor;
								if (i == mainUi->coverBrowserCoverCursor) {
									placeholderColor = (Color){0.4, 0.4, 0.4, 0.9};
								} else {
									placeholderColor = (Color){0.2, 0.2, 0.2, 0.8};
								}
								renderGradient(x, y, coverWidth, coverHeight, 0,
											  placeholderColor, placeholderColor);
							}

							// Highlight selected cover with border
							if (i == mainUi->coverBrowserCoverCursor) {
								Color highlightColor = {1.0, 1.0, 1.0, 0.5};
								// Top border
								renderGradient(x, y + coverHeight - 3, coverWidth, 3, 0,
											  highlightColor, highlightColor);
								// Bottom border
								renderGradient(x, y, coverWidth, 3, 0,
											  highlightColor, highlightColor);
								// Left border
								renderGradient(x, y, 3, coverHeight, 0,
											  highlightColor, highlightColor);
								// Right border
								renderGradient(x + coverWidth - 3, y, 3, coverHeight, 0,
											  highlightColor, highlightColor);
							}
						}

						// Render scroll indicators with simple dots
						float dotSize = goldenRatioLarge(offblast->infoPointSize, 2);
						float dotMargin = goldenRatioLarge(offblast->infoPointSize, 1);
						Color dotColor = {1.0, 1.0, 1.0, 0.7};

						if (mainUi->coverBrowserScrollOffset > 0) {
							// Top indicator - above the top row
							renderGradient(offblast->winWidth / 2 - dotSize / 2,
										  gridStartY + coverHeight + dotMargin,
										  dotSize, dotSize, 0,
										  dotColor, dotColor);
						}
						uint32_t totalRows = (mainUi->coverBrowserCovers->numCovers + mainUi->coverBrowserCoversPerRow - 1) / mainUi->coverBrowserCoversPerRow;
						if (mainUi->coverBrowserScrollOffset + mainUi->coverBrowserVisibleRows < totalRows) {
							// Bottom indicator - below the bottom row
							// Bottom of second row is at: gridStartY - (coverHeight + gridPadding)
							float bottomRowBottomY = gridStartY - (coverHeight + gridPadding);
							renderGradient(offblast->winWidth / 2 - dotSize / 2,
										  bottomRowBottomY - dotMargin,
										  dotSize, dotSize, 0,
										  dotColor, dotColor);
						}
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

                // Calculate joystick magnitude and apply deadzone
                double joyX = (double)offblast->joyX / INT16_MAX;
                double joyY = (double)offblast->joyY / INT16_MAX;
                double magnitude = sqrt(joyX * joyX + joyY * joyY);

                // Only update selection if joystick is pushed beyond deadzone (30% of max)
                int32_t onChar = -1;  // -1 means no selection
                const double DEADZONE = 0.3;

                if (magnitude > DEADZONE) {
                    double joyTangent = atan2(joyY, joyX);

                    // Normalize angle to 0-2π range
                    if (joyTangent < 0) {
                        joyTangent += 2 * M_PI;
                    }

                    // Convert angle to character index (0-27)
                    // Round to nearest character position
                    onChar = (int)round(28.0 * joyTangent / (2 * M_PI)) % 28;
                }

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
                        if (offblast->searchCurChar != string[0]) {
                            // Character changed, trigger haptic feedback
                            if (offblast->player.usingController != NULL) {
                                uint16_t low_freq = 0x0CCC;  // Very light tick (~10%)
                                uint16_t high_freq = 0x0CCC; // Very light tick (~10%)
                                uint32_t duration_ms = 50;   // 50ms for reliable tick feel

                                int result = SDL_GameControllerRumble(offblast->player.usingController,
                                    low_freq, high_freq, duration_ms);

                                if (result < 0) {
                                    printf("Rumble failed: %s\n", SDL_GetError());
                                }
                            } else {
                                printf("No controller connected for haptic feedback\n");
                            }
                            offblast->searchPrevChar = offblast->searchCurChar;
                        }
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
                        offblast->titleCharData, offblast->titleCodepoints, offblast->titleNumChars);

                renderText(offblast, offblast->winWidth/2 - lineWidth/2, 
                        offblast->winHeight/2 - offblast->titlePointSize/2, 
                        OFFBLAST_TEXT_TITLE, 0.65, 0, 
                        textToShow);

            }

        }
        else if (offblast->mode == OFFBLAST_UI_MODE_PLAYER_SELECT) {

            // Calculate fade-in progress
            float fadeAlpha = 1.0f;
            if (playerSelectUi->fadeInActive) {
                uint32_t elapsed = SDL_GetTicks() - playerSelectUi->fadeInStartTick;
                float progress = (float)elapsed / 410.0f;  // 0.41 seconds (33% faster)
                if (progress >= 1.0f) {
                    fadeAlpha = 1.0f;
                    playerSelectUi->fadeInActive = 0;  // Animation complete
                } else {
                    fadeAlpha = progress;  // 0.0 -> 1.0
                }
            }

            // TODO cache all these golden ratio calls they are expensive
            // to calculate
            // cache all the x positions of the text perhaps too?
            char *titleText = "Who's playing?";
            uint32_t titleWidth = getTextLineWidth(titleText,
                    offblast->titleCharData, offblast->titleCodepoints, offblast->titleNumChars);

            renderText(offblast,
                    offblast->winWidth / 2 - titleWidth / 2,
                    offblast->winHeight -
                        goldenRatioLarge(offblast->winHeight, 3),
                    OFFBLAST_TEXT_TITLE, fadeAlpha, 0,
                    titleText);

            if (offblast->nUsers == 0) {
                // Display error message when no users are configured
                char *messageText = "No users configured";
                uint32_t messageWidth = getTextLineWidth(messageText,
                        offblast->infoCharData, offblast->infoCodepoints, offblast->infoNumChars);

                renderText(offblast,
                        offblast->winWidth / 2 - messageWidth / 2,
                        offblast->winHeight / 2 - offblast->infoPointSize / 2,
                        OFFBLAST_TEXT_INFO, 0.7 * fadeAlpha, 0,
                        messageText);

                char *helpText = "Please add users to ~/.offblast/config.json";
                uint32_t helpWidth = getTextLineWidth(helpText,
                        offblast->infoCharData, offblast->infoCodepoints, offblast->infoNumChars);

                renderText(offblast,
                        offblast->winWidth / 2 - helpWidth / 2,
                        offblast->winHeight / 2 + offblast->infoPointSize * 2,
                        OFFBLAST_TEXT_INFO, 0.5 * fadeAlpha, 0,
                        helpText);
            }

            uint32_t xStart= offblast->winWidth / 2
                - playerSelectUi->totalWidth / 2;

            // XXX
            for (uint32_t i = 0; i < offblast->nUsers; ++i) {

                Image *image = &playerSelectUi->images[i];

                float alpha = 0.7;
                if (i == playerSelectUi->cursor) {

                    alpha = 1.0;
                    if (mainUi->horizontalAnimation->animating == 1) {
                        double change = easeInOutCirc(
                                (double)SDL_GetTicks() - 
                                mainUi->horizontalAnimation->startTick,
                                0.0,
                                1.0,
                                (double)mainUi->horizontalAnimation->durationMs);

                        alpha -= change * 0.3;
                    }
                }

                renderImage(
                        xStart + playerSelectUi->xOffsetForAvatar[i],
                        offblast->winHeight /2 -0.5* offblast->mainUi.boxHeight,
                        0, offblast->mainUi.boxHeight,
                        image, 0.0f, alpha * fadeAlpha);

                uint32_t nameWidth = getTextLineWidth(
                        offblast->users[i].name,
                        offblast->infoCharData, offblast->infoCodepoints, offblast->infoNumChars);

                renderText(offblast,
                        xStart + playerSelectUi->xOffsetForAvatar[i]
                        + playerSelectUi->widthForAvatar[i] / 2 - nameWidth / 2,
                        offblast->winHeight/2 - 0.5*offblast->mainUi.boxHeight -
                            offblast->mainUi.boxPad - offblast->infoPointSize,
                        OFFBLAST_TEXT_INFO, alpha * fadeAlpha, 0,
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
                    offblast->titleCharData, offblast->titleCodepoints, offblast->titleNumChars);

            renderText(offblast, 
                    offblast->winWidth / 2 - titleWidth / 2, 
                    yOffset,
                    OFFBLAST_TEXT_TITLE, 1.0, 0,
                    headerText);

            yOffset -= 100;

            char *titleText = 
                offblast->mainUi.activeRowset->rowCursor->tileCursor->target->name;

            uint32_t nameWidth = 
                getTextLineWidth(titleText, offblast->infoCharData, offblast->infoCodepoints, offblast->infoNumChars);

            renderText(offblast, 
                    offblast->winWidth / 2 - nameWidth/ 2, 
                    yOffset,
                    OFFBLAST_TEXT_INFO, 1.0, 0,
                    titleText);

            yOffset -= (offblast->infoPointSize * 3);

            UiTile *theTile =
                offblast->mainUi.activeRowset->rowCursor->tileCursor;
            Image *imageToShow = requestImageForTarget(theTile->target, 1);


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
                // For Steam games, just show "Return" (Steam handles resume/stop)
                int isSteamGame = offblast->playingTarget &&
                    strcmp(offblast->playingTarget->platform, "steam") == 0;

                if (isSteamGame) {
                    double returnWidth =
                        getTextLineWidth("Return", offblast->infoCharData, offblast->infoCodepoints, offblast->infoNumChars);
                    renderText(offblast,
                            offblast->winWidth/2 - returnWidth/2,
                            yOffset,
                            OFFBLAST_TEXT_INFO,
                            1.0,
                            0,
                            "Return");
                } else {
                    double stopWidth =
                        getTextLineWidth("Stop", offblast->infoCharData, offblast->infoCodepoints, offblast->infoNumChars);

                    double resumeWidth =
                        getTextLineWidth("Resume", offblast->infoCharData, offblast->infoCodepoints, offblast->infoNumChars);

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


        }

        uint32_t frameTime = SDL_GetTicks() - lastTick;
        char *fpsString;
        asprintf(&fpsString, "frame time: %u", frameTime);
        renderText(offblast, 15, 15, OFFBLAST_TEXT_DEBUG, 1.0, 0, 
               fpsString);
        free(fpsString);


        // Tick active animations
        for (int i = 0; i < numAnimations; i++) {
            Animation *anim = allAnimations[i];
            if (anim->animating && SDL_GetTicks() > anim->startTick + anim->durationMs) {
                anim->animating = 0;
                anim->callback(anim->callbackArgs);
                if (anim->callbackArgs != NULL) {
                    free(anim->callbackArgs);
                    anim->callbackArgs = NULL;
                }
            }
        }

        // Render status message (metadata refresh notification)
        if (offblast->statusMessageTick > 0) {
            uint32_t currentTick = SDL_GetTicks();
            uint32_t elapsed = currentTick - offblast->statusMessageTick;

            float alpha = 1.0f;

            // Fade out after duration
            if (elapsed > offblast->statusMessageDuration) {
                uint32_t fadeTime = 500; // 500ms fade
                if (elapsed < offblast->statusMessageDuration + fadeTime) {
                    alpha = 1.0f - ((float)(elapsed - offblast->statusMessageDuration) / fadeTime);
                } else {
                    offblast->statusMessageTick = 0; // Clear message
                    alpha = 0.0f;
                }
            }

            if (alpha > 0.0f) {
                // Build the full message with progress if rescanning
                char displayMessage[512];
                if (offblast->rescrapeInProgress && offblast->rescrapeTotal > 0) {
                    snprintf(displayMessage, sizeof(displayMessage), "%s (%u/%u)",
                             offblast->statusMessage,
                             offblast->rescrapeProcessed,
                             offblast->rescrapeTotal);
                } else {
                    strncpy(displayMessage, offblast->statusMessage, sizeof(displayMessage) - 1);
                }

                // Calculate position (6% of screen height from right edge, 6% from top)
                uint32_t messageWidth = getTextLineWidth(displayMessage, offblast->infoCharData, offblast->infoCodepoints, offblast->infoNumChars);
                float xPos = offblast->winWidth - messageWidth - (offblast->winHeight * 0.06f);
                // Position at 6% from top (Y coordinate in OpenGL is from bottom)
                float yPos = offblast->winHeight - (offblast->winHeight * 0.06f);

                // Render the text directly without gradient background
                renderText(offblast, xPos, yPos,
                          OFFBLAST_TEXT_INFO, alpha, 0, displayMessage);
            }
        }

        SDL_GL_SwapWindow(offblast->window);

        if (SDL_GetTicks() - lastTick < renderFrequency) {
            SDL_Delay(renderFrequency - (SDL_GetTicks() - lastTick));
        }

        lastTick = SDL_GetTicks();
    }

    XCloseDisplay(offblast->XDisplay);

    SDL_DestroyWindow(offblast->window);
    SDL_Quit();

    if (offblast->shutdownFlag) {
        printf("Shutting down machine\n");
        system("systemctl poweroff");
    }

    for (uint32_t i = 0; i < offblast->numImageLoadThreads; ++i) {
        pthread_kill(offblast->imageLoadThreads[i], SIGTERM);
    }
    free(offblast->imageLoadThreads);
    pthread_mutex_destroy(&offblast->imageStoreLock);


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

// Pack font with UTF-8 support into texture atlas
// Returns number of characters packed, or 0 on failure
int packFont(unsigned char *fontData, float fontSize, unsigned char *atlas,
             int atlasWidth, int atlasHeight,
             stbtt_packedchar **outCharData, int **outCodepoints) {

    // Define Unicode ranges to support
    // Start with essential ranges for Latin + common symbols
    #define NUM_RANGES 5
    stbtt_pack_range ranges[NUM_RANGES];
    memset(ranges, 0, sizeof(ranges));  // Zero-initialize all fields
    int totalChars = 0;

    // Range 0: Basic Latin + Latin-1 (32-255)
    ranges[0].font_size = fontSize;
    ranges[0].first_unicode_codepoint_in_range = 32;
    ranges[0].array_of_unicode_codepoints = NULL;  // Use continuous range
    ranges[0].num_chars = 224;  // 32-255
    ranges[0].chardata_for_range = NULL;  // Will allocate
    totalChars += 224;

    // Range 1: Latin Extended-A (256-383) - Eastern European
    ranges[1].font_size = fontSize;
    ranges[1].first_unicode_codepoint_in_range = 256;
    ranges[1].array_of_unicode_codepoints = NULL;
    ranges[1].num_chars = 128;
    ranges[1].chardata_for_range = NULL;
    totalChars += 128;

    // Range 2: General Punctuation (8192-8303) - smart quotes, dashes, ellipsis
    ranges[2].font_size = fontSize;
    ranges[2].first_unicode_codepoint_in_range = 0x2000;
    ranges[2].array_of_unicode_codepoints = NULL;
    ranges[2].num_chars = 112;
    ranges[2].chardata_for_range = NULL;
    totalChars += 112;

    // Range 3: Hiragana (12352-12447)
    ranges[3].font_size = fontSize;
    ranges[3].first_unicode_codepoint_in_range = 0x3040;
    ranges[3].array_of_unicode_codepoints = NULL;
    ranges[3].num_chars = 96;
    ranges[3].chardata_for_range = NULL;
    totalChars += 96;

    // Range 4: Katakana (12448-12543)
    ranges[4].font_size = fontSize;
    ranges[4].first_unicode_codepoint_in_range = 0x30A0;
    ranges[4].array_of_unicode_codepoints = NULL;
    ranges[4].num_chars = 96;
    ranges[4].chardata_for_range = NULL;
    totalChars += 96;

    // Allocate character data
    stbtt_packedchar *charData = calloc(totalChars, sizeof(stbtt_packedchar));
    int *codepoints = calloc(totalChars, sizeof(int));

    // Set up char data pointers for each range
    int offset = 0;
    for (int i = 0; i < NUM_RANGES; i++) {
        ranges[i].chardata_for_range = charData + offset;

        // Build codepoint array
        for (int j = 0; j < ranges[i].num_chars; j++) {
            codepoints[offset + j] = ranges[i].first_unicode_codepoint_in_range + j;
        }

        offset += ranges[i].num_chars;
    }

    // Pack the font
    stbtt_pack_context context;
    if (!stbtt_PackBegin(&context, atlas, atlasWidth, atlasHeight, 0, 1, NULL)) {
        free(charData);
        free(codepoints);
        return 0;
    }

    stbtt_PackSetOversampling(&context, 1, 1);  // Reduced for large atlases

    // Try packing all ranges (including Japanese)
    int rangesUsed = NUM_RANGES;
    if (!stbtt_PackFontRanges(&context, fontData, 0, ranges, NUM_RANGES)) {
        printf("Warning: Could not fit all Unicode ranges, trying without Japanese...\n");

        // Fallback: try without Japanese (ranges 0-2 only: Latin + punctuation)
        rangesUsed = 3;
        totalChars = 224 + 128 + 112;  // Recalculate without Japanese

        stbtt_PackEnd(&context);

        // Re-initialize packing context
        if (!stbtt_PackBegin(&context, atlas, atlasWidth, atlasHeight, 0, 1, NULL)) {
            free(charData);
            free(codepoints);
            return 0;
        }
        stbtt_PackSetOversampling(&context, 2, 2);

        if (!stbtt_PackFontRanges(&context, fontData, 0, ranges, rangesUsed)) {
            printf("ERROR: Could not pack even Latin characters - atlas too small\n");
            stbtt_PackEnd(&context);
            free(charData);
            free(codepoints);
            return 0;
        }
    }

    stbtt_PackEnd(&context);

    *outCharData = charData;
    *outCodepoints = codepoints;

    printf("Packed %d characters across %d Unicode ranges\n", totalChars, rangesUsed);

    return totalChars;

    #undef NUM_RANGES
}

// Decode one UTF-8 character from a string
// Returns the Unicode codepoint and advances the string pointer
// Returns -1 on invalid UTF-8
int utf8_decode(const char **str) {
    const unsigned char *s = (const unsigned char *)*str;

    if (!*s) return -1;  // End of string

    // 1-byte sequence (ASCII): 0xxxxxxx
    if ((*s & 0x80) == 0) {
        *str = (const char *)(s + 1);
        return *s;
    }

    // 2-byte sequence: 110xxxxx 10xxxxxx
    if ((*s & 0xE0) == 0xC0) {
        if ((s[1] & 0xC0) != 0x80) return -1;  // Invalid continuation byte
        int codepoint = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *str = (const char *)(s + 2);
        return codepoint;
    }

    // 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx
    if ((*s & 0xF0) == 0xE0) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return -1;
        int codepoint = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *str = (const char *)(s + 3);
        return codepoint;
    }

    // 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if ((*s & 0xF8) == 0xF0) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return -1;
        int codepoint = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        *str = (const char *)(s + 4);
        return codepoint;
    }

    return -1;  // Invalid UTF-8
}

// Find glyph index for a given Unicode codepoint using binary search
// Returns -1 if codepoint not found
int find_glyph_index(int codepoint, const int *codepoints, int numChars) {
    // Binary search
    int left = 0;
    int right = numChars - 1;

    while (left <= right) {
        int mid = (left + right) / 2;
        if (codepoints[mid] == codepoint) {
            return mid;
        } else if (codepoints[mid] < codepoint) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }

    return -1;  // Not found
}

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

    // Keep UTF-8 as-is - rendering code now handles UTF-8 natively

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

            if (ui->showSearch) return;

			if (ui->showCoverBrowser) {
				if (ui->coverBrowserState == 1 && ui->coverBrowserCovers) {  // cover_grid
					// Navigate cover grid (horizontal movement)
					uint32_t currentRow = ui->coverBrowserCoverCursor / ui->coverBrowserCoversPerRow;
					uint32_t currentCol = ui->coverBrowserCoverCursor % ui->coverBrowserCoversPerRow;

					if (direction == 1) {  // Right
						if (currentCol < ui->coverBrowserCoversPerRow - 1 &&
							ui->coverBrowserCoverCursor + 1 < ui->coverBrowserCovers->numCovers) {
							ui->coverBrowserCoverCursor++;
						}
					} else if (direction == 0) {  // Left
						if (currentCol > 0) {
							ui->coverBrowserCoverCursor--;
						}
					}
				}
				return;
			}

            if (ui->showMenu) {
                if (direction == 1) {

                    // TODO create a function for this
                    ui->menuAnimation->startTick = SDL_GetTicks();
                    ui->menuAnimation->direction = direction;
                    ui->menuAnimation->durationMs = NAVIGATION_MOVE_DURATION;
                    ui->menuAnimation->animating = 1;
                    ui->menuAnimation->callback = &menuToggleDone;

                    uint32_t *callbackArg = malloc(sizeof(uint32_t));
                    *callbackArg = 0;
                    ui->menuAnimation->callbackArgs = callbackArg;
                    return;

                }
            }
            else if (ui->showContextMenu) {
                // Close context menu when pressing left (it's on the right side)
                if (direction == 0) {
                    ui->contextMenuAnimation->startTick = SDL_GetTicks();
                    ui->contextMenuAnimation->direction = 1;  // Closing
                    ui->contextMenuAnimation->durationMs = NAVIGATION_MOVE_DURATION/2;
                    ui->contextMenuAnimation->animating = 1;
                    ui->contextMenuAnimation->callback = &contextMenuToggleDone;

                    uint32_t *callbackArg = malloc(sizeof(uint32_t));
                    *callbackArg = 0;  // Hide
                    ui->contextMenuAnimation->callbackArgs = callbackArg;
                    return;
                }
            }
            else {
                if (direction == 0) {

                    if (ui->activeRowset->numRows
                        && ui->activeRowset->rowCursor->tileCursor->previous 
                            != NULL) 
                    {
                        ui->activeRowset->movingToTarget = 
                            ui->activeRowset->rowCursor->tileCursor->previous->target;
                        ui->activeRowset->rowCursor->movingToTile
                            = ui->activeRowset->rowCursor->tileCursor->previous;
                    }
                    else {
                        // TODO create a function for this
                        ui->showMenu = 1;

                        // Ensure cursor is visible when opening menu
                        if (ui->maxVisibleMenuItems > 0 && ui->menuCursor >= ui->maxVisibleMenuItems) {
                            ui->menuScrollOffset = ui->menuCursor - ui->maxVisibleMenuItems / 2;
                            if (ui->menuScrollOffset + ui->maxVisibleMenuItems > ui->numMenuItems) {
                                ui->menuScrollOffset = ui->numMenuItems - ui->maxVisibleMenuItems;
                            }
                        } else {
                            ui->menuScrollOffset = 0;
                        }

                        ui->menuAnimation->startTick = SDL_GetTicks();
                        ui->menuAnimation->direction = direction;
                        ui->menuAnimation->durationMs = NAVIGATION_MOVE_DURATION/2;
                        ui->menuAnimation->animating = 1;
                        ui->menuAnimation->callback = &menuToggleDone;

                        uint32_t *callbackArg = malloc(sizeof(uint32_t));
                        *callbackArg = 1;
                        ui->menuAnimation->callbackArgs = callbackArg;
                        return;
                    }

                }
                else {
                    if (ui->activeRowset->numRows
                        && ui->activeRowset->rowCursor->tileCursor->next != NULL) 
                    {
                        ui->activeRowset->movingToTarget 
                            = ui->activeRowset->rowCursor->tileCursor->next->target;
                        ui->activeRowset->rowCursor->movingToTile
                            = ui->activeRowset->rowCursor->tileCursor->next;
                    }
                    else {
                        // Do nothing - don't open menu when pressing right at the end
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

        if (offblast->nUsers > 1 && animationRunning() == 0) {
            ui->horizontalAnimation->animating = 1;
            ui->horizontalAnimation->startTick = SDL_GetTicks();
            ui->horizontalAnimation->direction = direction;
            ui->horizontalAnimation->durationMs = NAVIGATION_MOVE_DURATION;
            ui->horizontalAnimation->callback = &playerSelectMoveDone;

            int32_t *directionArg = malloc(sizeof(int));
            *directionArg = direction;
            ui->horizontalAnimation->callbackArgs = directionArg;

        }
    }
    if (offblast->mode == OFFBLAST_UI_MODE_BACKGROUND) {
        if(activeWindowIsOffblast()) {

            ui->horizontalAnimation->startTick = SDL_GetTicks();
            ui->horizontalAnimation->direction = direction;
            ui->horizontalAnimation->durationMs = NAVIGATION_MOVE_DURATION/3;
            ui->horizontalAnimation->animating = 1;
            ui->horizontalAnimation->callback = &toggleKillButton;

            return;
        }
    }
}

void changeRow(uint32_t direction)
{
    MainUi *ui = &offblast->mainUi;

    if (ui->showSearch) return;

	if (ui->showCoverBrowser) {
		if (ui->coverBrowserState == 0 && ui->coverBrowserGames) {  // game_select
			// Navigate game selection list
			if (direction == 0 && ui->coverBrowserGameCursor < ui->coverBrowserGames->numGames - 1) {
				ui->coverBrowserGameCursor++;
			} else if (direction == 1 && ui->coverBrowserGameCursor > 0) {
				ui->coverBrowserGameCursor--;
			}
			return;
		} else if (ui->coverBrowserState == 1 && ui->coverBrowserCovers) {  // cover_grid
			// Navigate cover grid (vertical movement)
			uint32_t oldCursor = ui->coverBrowserCoverCursor;

			if (direction == 0) {  // Down
				uint32_t newCursor = ui->coverBrowserCoverCursor + ui->coverBrowserCoversPerRow;
				if (newCursor < ui->coverBrowserCovers->numCovers) {
					ui->coverBrowserCoverCursor = newCursor;
				}
			} else if (direction == 1) {  // Up
				if (ui->coverBrowserCoverCursor >= ui->coverBrowserCoversPerRow) {
					ui->coverBrowserCoverCursor -= ui->coverBrowserCoversPerRow;
				}
			}

			// Update scroll offset if needed
			uint32_t cursorRow = ui->coverBrowserCoverCursor / ui->coverBrowserCoversPerRow;
			if (cursorRow < ui->coverBrowserScrollOffset) {
				ui->coverBrowserScrollOffset = cursorRow;
				coverBrowserQueueThumbnails();
			} else if (cursorRow >= ui->coverBrowserScrollOffset + ui->coverBrowserVisibleRows) {
				ui->coverBrowserScrollOffset = cursorRow - ui->coverBrowserVisibleRows + 1;
				coverBrowserQueueThumbnails();
			}

			return;
		}
		return;
	}

    if (animationRunning() == 0)
    {
        if (offblast->mode == OFFBLAST_UI_MODE_MAIN) {

            if (ui->showMenu) {

                if (direction == 0 && ui->menuCursor == ui->numMenuItems-1)
                    return;

                else if (direction == 1 && ui->menuCursor == 0)
                    return;

                ui->menuNavigateAnimation->startTick = SDL_GetTicks();
                ui->menuNavigateAnimation->direction = direction;
                ui->menuNavigateAnimation->durationMs =
                    NAVIGATION_MOVE_DURATION / 2;
                ui->menuNavigateAnimation->animating = 1;
                ui->menuNavigateAnimation->callback = &menuNavigationDone;

                uint32_t *callbackArg = malloc(sizeof(uint32_t));
                *callbackArg = direction;
                ui->menuNavigateAnimation->callbackArgs = callbackArg;
                return;
            }
            else if (ui->showContextMenu) {

                if (direction == 0 && ui->contextMenuCursor == ui->numContextMenuItems-1)
                    return;

                else if (direction == 1 && ui->contextMenuCursor == 0)
                    return;

                ui->contextMenuNavigateAnimation->startTick = SDL_GetTicks();
                ui->contextMenuNavigateAnimation->direction = direction;
                ui->contextMenuNavigateAnimation->durationMs =
                    NAVIGATION_MOVE_DURATION / 2;
                ui->contextMenuNavigateAnimation->animating = 1;
                ui->contextMenuNavigateAnimation->callback = &contextMenuNavigationDone;

                uint32_t *callbackArg = malloc(sizeof(uint32_t));
                *callbackArg = direction;
                ui->contextMenuNavigateAnimation->callbackArgs = callbackArg;
                return;
            }
            else {

                if (!ui->activeRowset->numRows) return;

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

    if (ui->showSearch) return;

    if (offblast->mode == OFFBLAST_UI_MODE_MAIN) {
        if (animationRunning() == 0)
        {

            if (!ui->showMenu && ui->activeRowset->numRows) {
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

void toggleKillButton() {
    offblast->uiStopButtonHot = !offblast->uiStopButtonHot;
}

void horizontalMoveDone() {
    MainUi *ui = &offblast->mainUi;
        ui->activeRowset->rowCursor->tileCursor = 
            ui->activeRowset->rowCursor->movingToTile;
}

void playerSelectMoveDone(void *arg) {
    if (!arg) { printf("player select has no direction arg\n"); return; }

    int32_t *direction = (int*) arg;

    if (*direction) {
        offblast->playerSelectUi.cursor++;
        if (offblast->playerSelectUi.cursor >= offblast->nUsers)
            offblast->playerSelectUi.cursor = 0;
    }
    else {
        if (offblast->playerSelectUi.cursor == 0)
            offblast->playerSelectUi.cursor = offblast->nUsers - 1;
        else 
            offblast->playerSelectUi.cursor--;
    }
}

void menuToggleDone(void *arg) {
    uint32_t *showIt = (uint32_t*) arg;
    offblast->mainUi.showMenu = *showIt;
}

void menuNavigationDone(void *arg) {
    uint32_t *direction = (uint32_t*) arg;
    MainUi *ui = &offblast->mainUi;

    if (*direction == 0) {
        ui->menuCursor++;

        // Adjust scroll offset if cursor moved below visible area
        if (ui->menuCursor >= ui->menuScrollOffset + ui->maxVisibleMenuItems) {
            ui->menuScrollOffset = ui->menuCursor - ui->maxVisibleMenuItems + 1;
        }
    } else {
        ui->menuCursor--;

        // Adjust scroll offset if cursor moved above visible area
        if (ui->menuCursor < ui->menuScrollOffset) {
            ui->menuScrollOffset = ui->menuCursor;
        }
    }
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

    MainUi *ui = &offblast->mainUi;
    Animation *allAnimations[] = {
        ui->horizontalAnimation,
        ui->verticalAnimation,
        ui->infoAnimation,
        ui->rowNameAnimation,
        ui->menuAnimation,
        ui->menuNavigateAnimation,
        ui->contextMenuAnimation,
        ui->contextMenuNavigateAnimation,
        ui->coverBrowserAnimation
    };
    int numAnimations = sizeof(allAnimations) / sizeof(allAnimations[0]);

    for (int i = 0; i < numAnimations; i++) {
        if (allAnimations[i]->animating) {
            return 1;
        }
    }

    return 0;
}

void loadPlatformNames(const char *openGameDbPath) {
    char *namesPath;
    asprintf(&namesPath, "%s/names.csv", openGameDbPath);

    FILE *fp = fopen(namesPath, "r");
    if (!fp) {
        printf("Warning: Could not open %s, using fallback platform names\n", namesPath);
        free(namesPath);
        return;
    }

    char line[1024];
    int lineNum = 0;
    numPlatformNames = 0;

    while (fgets(line, sizeof(line), fp) && numPlatformNames < MAX_PLATFORM_NAMES) {
        lineNum++;

        // Skip header line
        if (lineNum == 1) continue;

        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        if (len > 1 && line[len-2] == '\r') line[len-2] = '\0';

        // Skip empty lines
        if (strlen(line) == 0) continue;

        // Parse CSV: key,name
        char *comma = strchr(line, ',');
        if (!comma) {
            printf("Warning: Invalid line %d in names.csv: %s\n", lineNum, line);
            continue;
        }

        *comma = '\0';
        char *key = line;
        char *name = comma + 1;

        strncpy(platformNames[numPlatformNames].key, key, 255);
        platformNames[numPlatformNames].key[255] = '\0';
        strncpy(platformNames[numPlatformNames].name, name, 255);
        platformNames[numPlatformNames].name[255] = '\0';

        numPlatformNames++;
    }

    fclose(fp);
    free(namesPath);
    printf("Loaded %u platform names from names.csv\n", numPlatformNames);
}

const char *platformString(char *key) {
    for (uint32_t i = 0; i < numPlatformNames; i++) {
        if (strcmp(key, platformNames[i].key) == 0) {
            return platformNames[i].name;
        }
    }

    return "Unknown Platform";
}


char *getCoverPath(LaunchTarget *target) {

    char *coverArtPath;
    char *homePath = getenv("HOME");
    assert(homePath);

    if (strcmp(target->platform, "steam") == 0) {
        asprintf(&coverArtPath, 
                "%s/.steam/steam/appcache/librarycache/%s_library_600x900.jpg", 
                homePath,
                target->id);

        if (access(coverArtPath, F_OK) == -1) {
            //printf("No file on disk: %s\n", coverArtPath);
        }
        else 
            return coverArtPath;

    }

    // Default
    asprintf(&coverArtPath, "%s/.offblast/covers/%"PRIu64".jpg", homePath, 
            target->targetSignature); 

    return coverArtPath;
}

char *getCoverUrl(LaunchTarget *target) {

    char *coverArtUrl;

    if (strcmp(target->platform, "steam") == 0) {
        asprintf(&coverArtUrl, 
                "https://steamcdn-a.akamaihd.net/steam/apps/%s/library_600x900.jpg", 
                target->id);
    }
    else {
        asprintf(&coverArtUrl, "%s", (char *) target->coverUrl);
    }

    return coverArtUrl;
}

void *imageLoadMain(void *arg) {
    OffblastUi *offblast = arg;

    while (1) {

        pthread_mutex_lock(&offblast->imageStoreLock);
        int32_t index = -1;

        for (uint32_t i = 0; i < IMAGE_STORE_SIZE; ++i) {
            if (offblast->imageStore[i].targetSignature > 0
                    && offblast->imageStore[i].state == IMAGE_STATE_QUEUED) 
            {
                offblast->imageStore[i].state = IMAGE_STATE_LOADING;
                index = i;
                break;

            }
        }
        pthread_mutex_unlock(&offblast->imageStoreLock);


        if (index > -1) {
            pthread_mutex_lock(&offblast->imageStoreLock);
            char *path = calloc(PATH_MAX, sizeof(char));
            memcpy(path, offblast->imageStore[index].path, PATH_MAX);
            pthread_mutex_unlock(&offblast->imageStoreLock);

            int n, w, h;
            unsigned char *atlas;

            stbi_set_flip_vertically_on_load(1);
            atlas = stbi_load(path, &w, &h, &n, 4);

            free(path);

            if(atlas == NULL) {

                //printf("need to download %d\n", index);

                pthread_mutex_lock(&offblast->imageStoreLock);

                DownloaderContext *dctx = malloc(sizeof(DownloaderContext));
                dctx->image = &offblast->imageStore[index];
                dctx->lock = &offblast->imageStoreLock;
                offblast->imageStore[index].state = IMAGE_STATE_DOWNLOADING;

                pthread_mutex_unlock(&offblast->imageStoreLock);

                pthread_t downloadThread;
                pthread_create(
                        &downloadThread, 
                        NULL, 
                        downloadMain, 
                        (void*)dctx);
                continue;
            }
            else {
                pthread_mutex_lock(&offblast->imageStoreLock);

                size_t atlasSize = w * h * 4;

                offblast->imageStore[index].atlas = calloc(1, atlasSize);
                memcpy(offblast->imageStore[index].atlas, atlas, atlasSize);
                stbi_image_free(atlas);

                offblast->imageStore[index].width = w;
                offblast->imageStore[index].height = h;
                offblast->imageStore[index].atlasSize = atlasSize;
                offblast->imageStore[index].state = IMAGE_STATE_READY;
                //printf("loaded %"PRIu64"\n", 
                //        offblast->imageStore[index].targetSignature);

                pthread_mutex_unlock(&offblast->imageStoreLock);
            }
        }

        usleep(16666);
    }

    return NULL;
}

void *downloadMain(void *arg) {
    DownloaderContext *ctx = arg;

    char *homePath = getenv("HOME");
    assert(homePath);

    char *workingPath = calloc(PATH_MAX, sizeof(char));
    char *workingUrl = calloc(PATH_MAX, sizeof(char));

    //printf("Download started %d\n", SDL_GetTicks());
                
    snprintf(workingPath, 
            PATH_MAX,
            "%s/.offblast/covers/%"PRIu64".jpg",
            homePath, 
            ctx->image->targetSignature); 

    snprintf(workingUrl, 
            PATH_MAX,
            "%s",
            ctx->image->url); 

    //printf("Downloading %s\n", workingUrl);


    CurlFetch fetch = {};

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (!curl) {
        printf("CURL init fail.\n");

        pthread_mutex_lock(ctx->lock);
        ctx->image->state = IMAGE_STATE_DEAD;
        pthread_mutex_unlock(ctx->lock);

        free(ctx);
        return NULL;
    }

    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_ACCEPTTIMEOUT_MS, 10L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);

    curl_easy_setopt(curl, CURLOPT_URL, workingUrl);

    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fetch);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);

    uint32_t res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        if (CURLE_OPERATION_TIMEDOUT == res) {
            printf("CURL TIMEOUT\n");
        }

        pthread_mutex_lock(ctx->lock);
        ctx->image->state = IMAGE_STATE_DEAD;
        pthread_mutex_unlock(ctx->lock);

        printf("Caught: %s\n", curl_easy_strerror(res));
        printf("%s\n", workingUrl);
        free(ctx);
        curl_easy_cleanup(curl);
        return NULL;

    } else {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

        int w, h, channels;
        unsigned char *image =
            stbi_load_from_memory(
                    fetch.data,
                    fetch.size, &w, &h, &channels, 4);

        if (image == NULL) {
            pthread_mutex_lock(ctx->lock);
            ctx->image->state = IMAGE_STATE_DEAD;
            pthread_mutex_unlock(ctx->lock);

            printf("Couldnt load the image from memory\n");
            printf("%s\n", workingUrl);
            free(ctx);
            curl_easy_cleanup(curl);
            return NULL;
        }

        // Resize if height is larger than 660px
        #define MAX_COVER_HEIGHT 660
        unsigned char *finalImage = image;
        int finalW = w;
        int finalH = h;

        if (h > MAX_COVER_HEIGHT) {
            // Calculate new dimensions preserving aspect ratio
            float scale = (float)MAX_COVER_HEIGHT / h;
            finalH = MAX_COVER_HEIGHT;
            finalW = (int)(w * scale);

            printf("Resizing cover from %dx%d to %dx%d (%.1f%% scale)\n",
                   w, h, finalW, finalH, scale * 100);

            // Allocate buffer for resized image
            unsigned char *resized = (unsigned char*)malloc(finalW * finalH * 4);
            if (resized) {
                // Perform resize (using stb_image_resize2 API)
                stbir_resize(image, w, h, 0,
                            resized, finalW, finalH, 0,
                            STBIR_RGBA, STBIR_TYPE_UINT8,
                            STBIR_EDGE_CLAMP, STBIR_FILTER_DEFAULT);
                free(image);
                finalImage = resized;
            } else {
                printf("Warning: Couldn't allocate memory for resize, using original\n");
            }
        } else if (h < MAX_COVER_HEIGHT) {
            printf("Cover %dx%d already smaller than %dpx height, keeping original\n",
                   w, h, MAX_COVER_HEIGHT);
        } else {
            printf("Cover %dx%d exactly at target height\n", w, h);
        }

        stbi_flip_vertically_on_write(1);
        if (!stbi_write_jpg(workingPath, finalW, finalH, 4, finalImage, 100)) {

            pthread_mutex_lock(ctx->lock);
            ctx->image->state = IMAGE_STATE_DEAD;
            pthread_mutex_unlock(ctx->lock);

            free(finalImage);
            printf("Couldnt save JPG");
            free(ctx);
            curl_easy_cleanup(curl);
            return NULL;
        }
        else {
            curl_easy_cleanup(curl);
            free(finalImage);
        }
    }

    free(fetch.data);
    free(workingPath);

    sleep(1);
    pthread_mutex_lock(ctx->lock);
    ctx->image->state = IMAGE_STATE_QUEUED;
    pthread_mutex_unlock(ctx->lock);
    free(ctx);

    return NULL;
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

    // Check if this is an uninstalled Steam game
    if (target->launcherSignature == 0 && strcmp(target->platform, "steam") == 0) {
        printf("Opening Steam install dialog for %s (id: %s)\n", target->name, target->id);
        char *installCmd;
        asprintf(&installCmd, "steam -bigpicture steam://install/%s", target->id);
        system(installCmd);
        free(installCmd);
        return;
    }

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
            asprintf(&launchString, "steam -bigpicture steam://rungameid/%s",
                    target->id);
        }
        else if (isScummvm) {
            const char *savePath = getUserCustomField(theUser, "save_path");
            if (savePath) {
                asprintf(&launchString, "SDL_AUDIODRIVER=alsa scummvm -f --savepath='%s' %s",
                        savePath,
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

            // Replace all user field placeholders
            replaceUserPlaceholders(launchString, theUser);
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

            // Close all inherited file descriptors except stdin/stdout/stderr
            // This prevents the child from inheriting Mesa's GPU device FDs (/dev/dri/*)
            // which can cause AMD driver crashes when the child is killed
            int maxfd = sysconf(_SC_OPEN_MAX);
            for (int fd = 3; fd < maxfd; fd++) {
                close(fd);
            }

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
    else if (offblast->mode == OFFBLAST_UI_MODE_BACKGROUND) {}
    else if (offblast->mainUi.showSearch) {
        //offblast->mainUi.activeRowset = offblast->mainUi.homeRowset;
        //offblast->mainUi.showSearch = 0;
    }
    else if (offblast->mode == OFFBLAST_UI_MODE_MAIN) {
        offblast->mainUi.showMenu = 0;
        doSearch();
    }
}

void pressContextMenu() {
    MainUi *ui = &offblast->mainUi;

    // Only available in main browsing mode
    if (offblast->mode != OFFBLAST_UI_MODE_MAIN) return;
    if (ui->showMenu) return;      // Left menu is open
    if (ui->showSearch) return;    // Search is open

    if (ui->showContextMenu) {
        // Close the context menu
        ui->contextMenuAnimation->startTick = SDL_GetTicks();
        ui->contextMenuAnimation->direction = 1;  // Closing
        ui->contextMenuAnimation->durationMs = NAVIGATION_MOVE_DURATION/2;
        ui->contextMenuAnimation->animating = 1;
        ui->contextMenuAnimation->callback = &contextMenuToggleDone;

        uint32_t *callbackArg = malloc(sizeof(uint32_t));
        *callbackArg = 0;  // Hide
        ui->contextMenuAnimation->callbackArgs = callbackArg;
    } else {
        // Open the context menu
        ui->showContextMenu = 1;
        ui->contextMenuCursor = 0;  // Reset cursor to top

        ui->contextMenuAnimation->startTick = SDL_GetTicks();
        ui->contextMenuAnimation->direction = 0;  // Opening
        ui->contextMenuAnimation->durationMs = NAVIGATION_MOVE_DURATION/2;
        ui->contextMenuAnimation->animating = 1;
        ui->contextMenuAnimation->callback = &contextMenuToggleDone;

        uint32_t *callbackArg = malloc(sizeof(uint32_t));
        *callbackArg = 1;  // Show
        ui->contextMenuAnimation->callbackArgs = callbackArg;
    }
}

void pressConfirm(int32_t joystickIndex) {

    if (offblast->mode == OFFBLAST_UI_MODE_BACKGROUND) {
        if(activeWindowIsOffblast() && !offblast->loadingFlag) {
            // For Steam games, just return to main UI (Steam handles the game)
            int isSteamGame = offblast->playingTarget &&
                strcmp(offblast->playingTarget->platform, "steam") == 0;

            if (isSteamGame) {
                offblast->mode = OFFBLAST_UI_MODE_MAIN;
                offblast->runningPid = 0;
                offblast->playingTarget = NULL;
                offblast->mainUi.rowGeometryInvalid = 1;
            }
            else if(offblast->uiStopButtonHot) {
                killRunningGame();
            }
            else {
                printf("Resume the current game on window %lu \n",
                    offblast->resumeWindow);

                // Use appropriate window activation method based on session/WM
                if (offblast->sessionType == SESSION_TYPE_WAYLAND) {
                    if (offblast->windowManager == WINDOW_MANAGER_KDE) {
                        activateWindowByPid(offblast->runningPid);
                    } else if (offblast->windowManager == WINDOW_MANAGER_GNOME) {
                        activateGnomeWindow(offblast->gnomeResumeWindowId);
                    } else {
                        raiseWindow(offblast->resumeWindow);
                    }
                } else {
                    raiseWindow(offblast->resumeWindow);
                }
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
		if (offblast->mainUi.showCoverBrowser) {
			MainUi *ui = &offblast->mainUi;

			if (ui->coverBrowserState == 0) {  // game_select
				// User selected a game from search results
				if (ui->coverBrowserGames && ui->coverBrowserGameCursor < ui->coverBrowserGames->numGames) {
					uint32_t gameId = ui->coverBrowserGames->games[ui->coverBrowserGameCursor].id;
					ui->coverBrowserCovers = sgdbGetCovers(gameId);

					if (!ui->coverBrowserCovers || ui->coverBrowserCovers->numCovers == 0) {
						snprintf(ui->coverBrowserError, 256, "No covers found for this game");
						if (ui->coverBrowserCovers) free(ui->coverBrowserCovers);
						ui->coverBrowserCovers = NULL;
					} else {
						ui->coverBrowserState = 1;  // cover_grid
						ui->coverBrowserCoverCursor = 0;
						coverBrowserSetTitle();  // Cache title text and width
						coverBrowserQueueThumbnails();
					}
				}
			} else if (ui->coverBrowserState == 1) {  // cover_grid
				// User selected a cover
				coverBrowserSelectCover();
			}
		}
        else if (offblast->mainUi.showMenu) {
            void (*callback)() =
                offblast->mainUi.menuItems[offblast->mainUi.menuCursor].callback;
            void *callbackArgs =
                offblast->mainUi.menuItems[offblast->mainUi.menuCursor].callbackArgs;

            if (callback == NULL)
                printf("menu null callback!\n");
            else {
                offblast->mainUi.showMenu = 0;
                callback(callbackArgs);
            }

        }
        else if (offblast->mainUi.showContextMenu) {
            void (*callback)() =
                offblast->mainUi.contextMenuItems[offblast->mainUi.contextMenuCursor].callback;
            void *callbackArgs =
                offblast->mainUi.contextMenuItems[offblast->mainUi.contextMenuCursor].callbackArgs;

            if (callback == NULL)
                printf("context menu null callback!\n");
            else {
                offblast->mainUi.showContextMenu = 0;
                callback(callbackArgs);
            }
        }
        else
            launch();
    }
}

void changeRowset(UiRowset *rowset) {
    offblast->mainUi.activeRowset = rowset;
    updateGameInfo();
    offblast->mainUi.rowGeometryInvalid = 1; 
}

void pressCancel() {
    if (offblast->mode == OFFBLAST_UI_MODE_MAIN) {
		if (offblast->mainUi.showCoverBrowser) {
			MainUi *ui = &offblast->mainUi;

			if (ui->coverBrowserState == 1 && ui->coverBrowserGames) {  // cover_grid with multiple games
				// Go back to game selection
				ui->coverBrowserState = 0;
				return;
			}

			// Otherwise close the browser
			closeCoverBrowser();
			return;
		}
        else if (offblast->mainUi.showContextMenu) {
            // Close context menu with animation
            MainUi *ui = &offblast->mainUi;
            ui->contextMenuAnimation->startTick = SDL_GetTicks();
            ui->contextMenuAnimation->direction = 1;  // Closing
            ui->contextMenuAnimation->durationMs = NAVIGATION_MOVE_DURATION/2;
            ui->contextMenuAnimation->animating = 1;
            ui->contextMenuAnimation->callback = &contextMenuToggleDone;

            uint32_t *callbackArg = malloc(sizeof(uint32_t));
            *callbackArg = 0;  // Hide
            ui->contextMenuAnimation->callbackArgs = callbackArg;
        }
        else if (offblast->mainUi.showSearch) {
            offblast->mainUi.showSearch = 0;
        }
        else if (offblast->mainUi.activeRowset == offblast->mainUi.searchRowset) {
            //offblast->mainUi.activeRowset = offblast->mainUi.homeRowset;
            changeRowset(offblast->mainUi.homeRowset);
        }
    }
}

void updateInfoText() {
    if (!offblast->mainUi.activeRowset->numRows) return;

    if (offblast->mainUi.infoText != NULL) {
        free(offblast->mainUi.infoText);
    }

    char *infoString;
    LaunchTarget *target = offblast->mainUi.activeRowset->movingToTarget;

    if (target == NULL) {
        printf("Update info text called with no target\n");
        return;
    }

    // Look up playtime for this game
    PlayTime *pt = NULL;
    for (uint32_t i = 0; i < offblast->playTimeFile->nEntries; ++i) {
        if (offblast->playTimeFile->entries[i].targetSignature == target->targetSignature) {
            pt = &offblast->playTimeFile->entries[i];
            break;
        }
    }

    // Build base info string (without playtime)
    if (target->ranking == 999) {
        asprintf(&infoString, "%.4s  |  %s  |  No score",
                target->date,
                platformString(target->platform));
    } else {
        asprintf(&infoString, "%.4s  |  %s  |  %u%%",
                target->date,
                platformString(target->platform),
                target->ranking);
    }
    offblast->mainUi.infoText = infoString;

    // Build playtime string separately (will be rendered with reduced alpha)
    if (offblast->mainUi.playtimeText != NULL) {
        free(offblast->mainUi.playtimeText);
        offblast->mainUi.playtimeText = NULL;
    }

    if (pt && pt->msPlayed > 0) {
        // Convert ms to hours or minutes
        float hours = pt->msPlayed / (1000.0f * 60.0f * 60.0f);
        char *timeStr;

        if (hours < 1.0f) {
            // Show in minutes if less than 1 hour
            int mins = (int)(pt->msPlayed / (1000.0f * 60.0f));
            asprintf(&timeStr, "  |  %d mins", mins);
        } else {
            asprintf(&timeStr, "  |  %.1f hrs", hours);
        }

        offblast->mainUi.playtimeText = timeStr;
    }
}

void updateDescriptionText() {
    if (!offblast->mainUi.activeRowset->numRows) return;

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

int rankingSort(const void *a, const void *b) {

    LaunchTarget **ra = (LaunchTarget**) a;
    LaunchTarget **rb = (LaunchTarget**) b;

    // Higher ranking comes first (descending order)
    if ((*ra)->ranking > (*rb)->ranking)
        return -1;
    else if ((*ra)->ranking < (*rb)->ranking)
        return +1;
    else
        return 0;
}

int tileRankingSort(const void *a, const void *b) {

    UiTile *ta = (UiTile*) a;
    UiTile *tb = (UiTile*) b;

    // Higher ranking comes first (descending order)
    if (ta->target->ranking > tb->target->ranking)
        return -1;
    else if (ta->target->ranking < tb->target->ranking)
        return +1;
    else
        return 0;
}


uint32_t getTextLineWidth(char *string, stbtt_packedchar* cdata, int *codepoints, int numChars) {

    uint32_t width = 0;
    const char *strptr = string;

    while (*strptr) {
        const char *before = strptr;
        int codepoint = utf8_decode(&strptr);

        if (codepoint == -1) {
            strptr = before + 1;
            continue;
        }

        int glyphIndex = find_glyph_index(codepoint, codepoints, numChars);
        if (glyphIndex != -1) {
            width += cdata[glyphIndex].xadvance;
        }
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

    stbtt_packedchar *cdata = NULL;
    int *codepoints = NULL;
    int numChars = 0;

    switch (textMode) {
        case OFFBLAST_TEXT_TITLE:
            glBindTexture(GL_TEXTURE_2D, offblast->titleTextTexture);
            cdata = offblast->titleCharData;
            codepoints = offblast->titleCodepoints;
            numChars = offblast->titleNumChars;
            lineHeight = offblast->titlePointSize * 1.2;
            break;

        case OFFBLAST_TEXT_INFO:
            glBindTexture(GL_TEXTURE_2D, offblast->infoTextTexture);
            cdata = offblast->infoCharData;
            codepoints = offblast->infoCodepoints;
            numChars = offblast->infoNumChars;
            lineHeight = offblast->infoPointSize * 1.2;
            break;

        case OFFBLAST_TEXT_DEBUG:
            glBindTexture(GL_TEXTURE_2D, offblast->debugTextTexture);
            cdata = offblast->debugCharData;
            codepoints = offblast->debugCodepoints;
            numChars = offblast->debugNumChars;
            lineHeight = offblast->debugPointSize * 1.2;
            break;

        default:
            return;
    }

    if (!cdata || !codepoints || numChars == 0) {
        return;  // Font not loaded
    }

    float winWidth = (float)offblast->winWidth;
    float winHeight = (float)offblast->winHeight;
    y = winHeight - y;

    char *trailingString = NULL;
    const char *strptr = string;

    while (*strptr) {
        // Decode UTF-8 character
        const char *before = strptr;
        int codepoint = utf8_decode(&strptr);

        if (codepoint == -1) {
            // Invalid UTF-8, skip this byte
            strptr = before + 1;
            continue;
        }

        // Find glyph for this codepoint
        int glyphIndex = find_glyph_index(codepoint, codepoints, numChars);
        if (glyphIndex == -1) {
            // Character not in font, skip it
            continue;
        }

        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(cdata, offblast->textBitmapWidth, offblast->textBitmapHeight,
                glyphIndex, &x, &y, &q, 1);

        currentWidth += (q.x1 - q.x0);

            if (lineMaxW > 0 && trailingString == NULL) {

                float wordWidth = 0.0f;
                if (codepoint == ' ') {
                    // Calculate width of next word for line wrapping
                    const char *wordptr = strptr;
                    wordWidth = 0.0f;

                    while (*wordptr && *wordptr != ' ') {
                        const char *before_word = wordptr;
                        int word_cp = utf8_decode(&wordptr);

                        if (word_cp == -1) {
                            wordptr = before_word + 1;
                            continue;
                        }

                        int word_gi = find_glyph_index(word_cp, codepoints, numChars);
                        if (word_gi != -1) {
                            wordWidth += cdata[word_gi].xadvance;
                        }
                    }
                }

                if (currentWidth + (int)(wordWidth + 0.5f) > lineMaxW) {

                    if (currentLine >= 6) {
                        trailingString = "...";
                        strptr = trailingString;  // Reset pointer to ellipsis
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

void renderLoadingScreen(OffblastUi *offblast) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Check if exit animation is active
    float exitProgress = 0.0f;
    float textAlpha = 1.0f;
    if (offblast->exitAnimating) {
        uint32_t elapsed = SDL_GetTicks() - offblast->exitAnimationStartTick;
        exitProgress = (float)elapsed / 610.0f;  // 0.61 seconds
        if (exitProgress > 1.0f) exitProgress = 1.0f;
        textAlpha = 1.0f - exitProgress;  // Fade out text
    }

    // Calculate breathing scale with natural pause at peaks
    float t = (float)SDL_GetTicks() / 1000.0f;
    float rawSin = sinf(t * 0.75f);  // 0.75Hz (50% slower)
    // Square the sine to create pauses at extremes (±1) and fast through middle (0)
    // sin² has derivative = sin(2t), which is 0 at the peaks
    float breathCurve = rawSin * rawSin;  // [0, 1] with pauses at 0 and 1
    float breathScale = 0.95f + 0.10f * breathCurve;  // Map to [0.95, 1.05]

    // Render centered logo with breathing effect
    // Size logo to 8% of widest screen dimension
    float maxDimension = offblast->winWidth > offblast->winHeight ?
                         offblast->winWidth : offblast->winHeight;

    // Calculate base size without breathing for static positioning
    float baseSize = maxDimension * 0.08f;
    float baseScale = baseSize / offblast->logoImage.width;
    float baseLogoW = offblast->logoImage.width * baseScale;
    float baseLogoH = offblast->logoImage.height * baseScale;

    // Apply breathing to the actual rendered size
    float logoW = baseLogoW * breathScale;
    float logoH = baseLogoH * breathScale;

    // Apply exit animation shrink
    if (offblast->exitAnimating) {
        float shrinkScale = 1.0f - exitProgress;  // Shrink from 1 to 0
        logoW *= shrinkScale;
        logoH *= shrinkScale;
    }

    // Center logo (breathing happens around this center point)
    float logoX = (offblast->winWidth - logoW) / 2.0f;
    float logoY = (offblast->winHeight - logoH) / 2.0f + 50;  // Slightly above center

    renderImage(logoX, logoY, logoW, logoH, &offblast->logoImage, 0.0f, 1.0f);

    // Get current status (mutex protected)
    char status[256];
    char errorMsg[256];
    uint32_t progress = 0, total = 0;
    uint32_t hasError = 0;
    pthread_mutex_lock(&offblast->loadingState.mutex);
    strncpy(status, offblast->loadingState.status, 256);
    strncpy(errorMsg, offblast->loadingState.errorMsg, 256);
    progress = offblast->loadingState.progress;
    total = offblast->loadingState.progressTotal;
    hasError = offblast->loadingState.error;
    pthread_mutex_unlock(&offblast->loadingState.mutex);

    // Render status or error text centered below logo (using static base position)
    char displayText[300];
    if (hasError) {
        snprintf(displayText, 300, "Error: %s", errorMsg);
    } else if (total > 0) {
        snprintf(displayText, 300, "%s (%u/%u)", status, progress, total);
    } else if (progress > 0) {
        snprintf(displayText, 300, "%s (%u)", status, progress);
    } else {
        snprintf(displayText, 300, "%s", status);
    }

    // Position text below logo using golden ratio spacing
    // Calculate base logo position (without breathing)
    float baseLogoY = (offblast->winHeight - baseLogoH) / 2.0f + 50;

    // The logo top edge is at baseLogoY + baseLogoH
    // Position text below the logo bottom with golden ratio spacing
    float spacing = goldenRatioLargef(baseLogoH, 1);
    float textY = baseLogoY - spacing;

    // Center text horizontally
    uint32_t textWidth = getTextLineWidth(displayText, offblast->infoCharData, offblast->infoCodepoints, offblast->infoNumChars);
    float textX = (offblast->winWidth - textWidth) / 2.0f;

    renderText(offblast, textX, textY, OFFBLAST_TEXT_INFO, textAlpha, 0, displayText);

    // If error, show "Press any button to exit" below error message
    if (hasError) {
        char *exitMsg = "Press any button to exit";
        uint32_t exitMsgWidth = getTextLineWidth(exitMsg, offblast->infoCharData, offblast->infoCodepoints, offblast->infoNumChars);
        float exitMsgX = (offblast->winWidth - exitMsgWidth) / 2.0f;
        float exitMsgY = textY - (offblast->infoPointSize * 1.2f * 2); // Two lines below error
        renderText(offblast, exitMsgX, exitMsgY, OFFBLAST_TEXT_INFO, textAlpha, 0, exitMsg);
    }

    SDL_GL_SwapWindow(offblast->window);
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

            // Skip if target no longer exists in database
            if (targetIndex == -1) {
                continue;
            }

            LaunchTarget *target = &launchTargetFile->entries[targetIndex];

            // Skip uninstalled games if filter is enabled
            if (offblast->showInstalledOnly && strlen(target->path) == 0) {
                continue;
            }

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

            // Skip if target no longer exists in database
            if (targetIndex == -1) {
                continue;
            }

            LaunchTarget *target = &launchTargetFile->entries[targetIndex];

            // Skip uninstalled games if filter is enabled
            if (offblast->showInstalledOnly && strlen(target->path) == 0) {
                continue;
            }

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

                // Skip uninstalled games if filter is enabled
                if (offblast->showInstalledOnly && strlen(target->path) == 0) {
                    continue;
                }

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

        // Step 1: Collect ALL games for this platform into temporary array
        uint32_t maxPlatformGames = 2000;
        LaunchTarget **platformGames = calloc(maxPlatformGames, sizeof(LaunchTarget*));
        uint32_t numPlatformGames = 0;

        for (uint32_t i = 0; i < launchTargetFile->nEntries; ++i) {
            LaunchTarget *target = &launchTargetFile->entries[i];

            if (strcmp(target->platform, offblast->platforms[iPlatform]) == 0) {
                // Skip uninstalled games if filter is enabled
                if (offblast->showInstalledOnly && strlen(target->path) == 0) {
                    continue;
                }

                // Skip games with no score (sentinel value)
                if (target->ranking == 999) {
                    continue;
                }

                // Skip games with empty names
                if (strlen(target->name) == 0) {
                    continue;
                }

                platformGames[numPlatformGames++] = target;
                if (numPlatformGames >= maxPlatformGames) break;
            }
        }

        // Step 2: Sort ALL platform games by ranking (highest first)
        if (numPlatformGames > 1) {
            qsort(platformGames, numPlatformGames, sizeof(LaunchTarget*), rankingSort);
        }

        // Step 3: Deduplicate by exact title, keeping owned > highest score
        LaunchTarget **dedupedGames = calloc(numPlatformGames, sizeof(LaunchTarget*));
        uint32_t numDeduped = 0;

        for (uint32_t i = 0; i < numPlatformGames; i++) {
            LaunchTarget *candidate = platformGames[i];
            int isDuplicate = 0;

            // Check if we already have this title (case-insensitive)
            for (uint32_t j = 0; j < numDeduped; j++) {
                if (strcasecmp(dedupedGames[j]->name, candidate->name) == 0) {
                    // Same title - keep owned version over unowned
                    int existingOwned = (strlen(dedupedGames[j]->path) > 0);
                    int candidateOwned = (strlen(candidate->path) > 0);

                    if (candidateOwned && !existingOwned) {
                        // Replace with owned version
                        dedupedGames[j] = candidate;
                    }
                    // else keep existing (owned version or equal ownership with higher score)
                    isDuplicate = 1;
                    break;
                }
            }

            if (!isDuplicate) {
                dedupedGames[numDeduped++] = candidate;
            }
        }

        free(platformGames);

        // Step 4: Take top 25 and build tiles
        uint32_t topRatedMax = 25;
        uint32_t numTiles = numDeduped < topRatedMax ? numDeduped : topRatedMax;
        UiTile *tiles = calloc(topRatedMax, sizeof(UiTile));
        assert(tiles);

        for (uint32_t i = 0; i < numTiles; i++) {
            tiles[i].target = dedupedGames[i];
        }

        free(dedupedGames);

        if (numTiles > 0) {
            // Link the tiles together
            for (uint32_t i = 0; i < numTiles; i++) {
                tiles[i].next = (i < numTiles - 1) ? &tiles[i + 1] : NULL;
                tiles[i].previous = (i > 0) ? &tiles[i - 1] : NULL;
            }

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

    changeRowset(mainUi->searchRowset);

    LaunchTargetFile* targetFile = offblast->launchTargetFile;

    UiTile *tiles = calloc(IMAGE_STORE_SIZE, sizeof(UiTile));
    assert(tiles);

    uint32_t tileCount = 0;

    for (int i = 0; i < targetFile->nEntries; ++i) {

        uint32_t isMatch = 0;
        if (!launcherSignature
                && strlen(offblast->searchTerm)
                && strcasestr(targetFile->entries[i].name, offblast->searchTerm))
        {
            isMatch = 1;
        }
        else if (launcherSignature
                && targetFile->entries[i].launcherSignature == *launcherSignature)
        {
            isMatch = 1;
        }
        // Also match uninstalled Steam games (launcherSignature == 0) when viewing Steam
        else if (launcherSignature
                && targetFile->entries[i].launcherSignature == 0
                && strcmp(targetFile->entries[i].platform, "steam") == 0)
        {
            // Check if we're viewing the Steam launcher
            for (uint32_t l = 0; l < offblast->nLaunchers; l++) {
                if (offblast->launchers[l].signature == *launcherSignature
                    && strcmp(offblast->launchers[l].platform, "steam") == 0) {
                    isMatch = 1;
                    break;
                }
            }
        }

        if (isMatch) {
            // Skip uninstalled games if filter is enabled
            if (offblast->showInstalledOnly && strlen(targetFile->entries[i].path) == 0) {
                continue;
            }

            if (tileCount >= 1999) {
                printf("More than 2000 results!\n");
                printf("\n");
                break;
            }

            uint32_t slottedIn = 0;
            for (int j=0; j < tileCount; ++j) {

                if (strcoll(targetFile->entries[i].name, tiles[j].target->name) 
                        <= 0) 
                {
                    if (tileCount >= 1999) break;
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

        if (mainUi->searchRowset->rows[0].tiles)
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

        mainUi->searchRowset->rows[onRow].length = onTile % 25 == 0 ? 25 : onTile % 25;                 

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
        updateGameInfo();
        mainUi->rowGeometryInvalid = 1; 
    }
    else {
        free(mainUi->searchRowset->rows[0].tiles);
        mainUi->searchRowset->rows[0].tiles = NULL;
        mainUi->searchRowset->numRows = 0;
        //mainUi->activeRowset = mainUi->homeRowset;
    }

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
    offblast->mainUi.rowGeometryInvalid = 1;  // Force tile repositioning

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
        // Let Steam Big Picture handle guide button for Steam games
        if (offblast->playingTarget &&
            strcmp(offblast->playingTarget->platform, "steam") == 0) {
            return;
        }

        if (!activeWindowIsOffblast()) {

            offblast->resumeWindow = getActiveWindowRaw();
            offblast->uiStopButtonHot = 0;

            // On Wayland, capture window info for resume functionality
            if (offblast->sessionType == SESSION_TYPE_WAYLAND) {
                if (offblast->windowManager == WINDOW_MANAGER_KDE) {
                    printf("KDE Wayland session - will use PID-based window activation (PID: %d)\n",
                           offblast->runningPid);
                } else if (offblast->windowManager == WINDOW_MANAGER_GNOME) {
                    printf("GNOME Wayland session - capturing focused window...\n");
                    captureGnomeFocusedWindow();
                }
            }

            if (offblast->windowManager == WINDOW_MANAGER_I3) {
                system("i3-msg workspace offblast");
            }
            else {
                raiseWindow(0);
            }
        }
    }
}

void rescrapeCurrentLauncher(int deleteAllCovers) {
    printf("\n=== METADATA REFRESH STARTED (deleteAllCovers=%d) ===\n", deleteAllCovers);

    // Check if we're in the right UI mode
    if (offblast->mode != OFFBLAST_UI_MODE_MAIN) {
        printf("Metadata refresh only available in main UI mode\n");
        return;
    }

    MainUi *mainUi = &offblast->mainUi;

    // Get the currently selected target
    if (!mainUi->activeRowset || !mainUi->activeRowset->rowCursor ||
        !mainUi->activeRowset->rowCursor->tileCursor ||
        !mainUi->activeRowset->rowCursor->tileCursor->target) {
        printf("No game selected for metadata refresh\n");
        return;
    }

    LaunchTarget *currentTarget = mainUi->activeRowset->rowCursor->tileCursor->target;
    printf("Current target: %s (platform: %s, launcher sig: %u)\n",
           currentTarget->name, currentTarget->platform, currentTarget->launcherSignature);

    // Check if target has a launcher signature
    if (currentTarget->launcherSignature == 0) {
        printf("Target has no launcher signature - might be from OpenGameDB only\n");
        return;
    }

    // Find the launcher with matching signature
    Launcher *targetLauncher = NULL;
    for (uint32_t i = 0; i < offblast->nLaunchers; i++) {
        if (offblast->launchers[i].signature == currentTarget->launcherSignature) {
            targetLauncher = &offblast->launchers[i];
            printf("Found launcher: type=%s, platform=%s\n",
                   targetLauncher->type, targetLauncher->platform);
            break;
        }
    }

    if (!targetLauncher) {
        printf("Could not find launcher with signature %u\n", currentTarget->launcherSignature);
        return;
    }

    // Steam uses Store API instead of OpenGameDB CSV
    if (strcmp(targetLauncher->platform, "steam") == 0) {
        printf("Rescraping Steam games via Store API...\n");

        // Count Steam games
        uint32_t steamCount = 0;
        LaunchTargetFile *targetFile = offblast->launchTargetFile;
        for (uint32_t i = 0; i < targetFile->nEntries; i++) {
            if (strcmp(targetFile->entries[i].platform, "steam") == 0) {
                steamCount++;
            }
        }

        snprintf(offblast->statusMessage, sizeof(offblast->statusMessage),
                 "Updating Steam metadata...");
        offblast->statusMessageTick = SDL_GetTicks();
        offblast->statusMessageDuration = 60000;
        offblast->rescrapeInProgress = 1;
        offblast->rescrapeTotal = steamCount;
        offblast->rescrapeProcessed = 0;

        uint32_t updated = 0;
        for (uint32_t i = 0; i < targetFile->nEntries; i++) {
            if (strcmp(targetFile->entries[i].platform, "steam") != 0) continue;

            LaunchTarget *target = &targetFile->entries[i];

            // Clear existing metadata if doing full refresh
            if (deleteAllCovers) {
                memset(target->date, 0, sizeof(target->date));
                target->ranking = 0;
                target->descriptionOffset = 0;
            }

            // Fetch fresh metadata
            uint32_t appid = (uint32_t)atoi(target->id);
            printf("Fetching metadata for: %s (%u)\n", target->name, appid);

            SteamMetadata *meta = fetchSteamGameMetadata(appid);
            if (meta) {
                if (meta->date[0] != '\0') {
                    memcpy(target->date, meta->date, sizeof(target->date));
                }
                if (meta->score > 0) {
                    target->ranking = meta->score;
                } else {
                    target->ranking = 999;  // No score available
                }
                if (meta->description) {
                    target->descriptionOffset = writeDescriptionBlob(target, meta->description);
                }
                printf("  Date: %s, Score: %u\n", meta->date, meta->score);
                freeSteamMetadata(meta);
                updated++;
            } else {
                // API fetch failed, set sentinel
                target->ranking = 999;
            }

            offblast->rescrapeProcessed = updated;
        }

        snprintf(offblast->statusMessage, sizeof(offblast->statusMessage),
                 "Steam metadata updated: %u games refreshed", updated);
        offblast->statusMessageTick = SDL_GetTicks();
        offblast->statusMessageDuration = 3000;
        offblast->rescrapeInProgress = 0;
        offblast->rescrapeProcessed = 0;
        offblast->rescrapeTotal = 0;

        printf("Steam metadata refresh complete: %u/%u games updated\n", updated, steamCount);
        return;
    }

    // Count how many targets will be affected
    uint32_t affectedCount = 0;
    LaunchTargetFile *targetFile = offblast->launchTargetFile;
    for (uint32_t i = 0; i < targetFile->nEntries; i++) {
        if (targetFile->entries[i].launcherSignature == currentTarget->launcherSignature) {
            affectedCount++;
        }
    }
    printf("Will refresh metadata for %u games for platform %s\n", affectedCount, targetLauncher->platform);

    // Set initial status message
    snprintf(offblast->statusMessage, sizeof(offblast->statusMessage),
             "Refreshing %.218s metadata/covers...", targetLauncher->platform);
    offblast->statusMessageTick = SDL_GetTicks();
    offblast->statusMessageDuration = 60000; // 60 seconds before fade
    offblast->rescrapeInProgress = 1;
    offblast->rescrapeTotal = affectedCount;
    offblast->rescrapeProcessed = 0;

    // Clear metadata for all targets with this launcher signature
    printf("\nClearing existing metadata...\n");
    char *homePath = getenv("HOME");

    if (deleteAllCovers) {
        printf("Mode: Delete ALL covers for platform\n");
        // Delete all covers for this launcher
        for (uint32_t i = 0; i < targetFile->nEntries; i++) {
            if (targetFile->entries[i].launcherSignature == currentTarget->launcherSignature) {
                LaunchTarget *target = &targetFile->entries[i];

                // Delete cached cover file
                char coverPath[PATH_MAX];
                snprintf(coverPath, PATH_MAX, "%s/.offblast/covers/%"PRIu64".jpg",
                        homePath, target->targetSignature);
                if (access(coverPath, F_OK) == 0) {
                    if (unlink(coverPath) == 0) {
                        printf("  Deleted cover: %s\n", coverPath);
                    } else {
                        printf("  Failed to delete cover: %s\n", coverPath);
                    }
                }

                // Clear cover URL - this will force re-download
                memset(target->coverUrl, 0, PATH_MAX);

                // Clear other metadata that comes from OpenGameDB
                memset(target->date, 0, sizeof(target->date));
                target->ranking = 0;
                target->descriptionOffset = 0;

                printf("  Cleared metadata for: %s\n", target->name);
            }
        }
    } else {
        printf("Mode: Delete only CURRENT game's cover\n");
        // Only delete the current target's cover
        char coverPath[PATH_MAX];
        snprintf(coverPath, PATH_MAX, "%s/.offblast/covers/%"PRIu64".jpg",
                homePath, currentTarget->targetSignature);
        if (access(coverPath, F_OK) == 0) {
            if (unlink(coverPath) == 0) {
                printf("  Deleted cover for current game: %s\n", currentTarget->name);
            } else {
                printf("  Failed to delete cover: %s\n", coverPath);
            }
        }

        // Clear metadata for ONLY the current game
        memset(currentTarget->coverUrl, 0, PATH_MAX);
        memset(currentTarget->date, 0, sizeof(currentTarget->date));
        currentTarget->ranking = 0;
        currentTarget->descriptionOffset = 0;
        printf("  Cleared metadata for: %s\n", currentTarget->name);

        affectedCount = 1;  // Only updating one game
    }

    // Now refresh metadata from OpenGameDB for this platform
    printf("\nRefreshing metadata from OpenGameDB for platform: %s\n", targetLauncher->platform);

    // Use the OpenGameDB path that was discovered during initialization
    const char *openGameDbPath = offblast->openGameDbPath;
    if (!openGameDbPath) {
        printf("ERROR: No OpenGameDB path available\n");
        return;
    }

    printf("OpenGameDB path: %s\n", openGameDbPath);

    // Open the CSV file for this platform
    char csvPath[PATH_MAX];
    snprintf(csvPath, PATH_MAX, "%s/%s.csv", openGameDbPath, targetLauncher->platform);
    printf("Reading CSV: %s\n", csvPath);

    FILE *csvFile = fopen(csvPath, "r");
    if (!csvFile) {
        printf("ERROR: Could not open CSV file: %s\n", csvPath);
        return;
    }

    // Read and process CSV
    char *csvLine = NULL;
    size_t csvLineLength = 0;
    ssize_t csvBytesRead;
    uint32_t rowCount = 0;
    uint32_t matchCount = 0;

    while ((csvBytesRead = getline(&csvLine, &csvLineLength, csvFile)) != -1) {
        if (rowCount == 0) {
            rowCount++;
            continue; // Skip header
        }

        // Parse CSV fields
        char *gameName = getCsvField(csvLine, 1);
        if (!gameName) {
            rowCount++;
            continue;
        }

        // Use the same matching logic as initial scan
        // Create signature from platform + gameName to find the right target
        char *gameSeed;
        asprintf(&gameSeed, "%s_%s", targetLauncher->platform, gameName);

        uint64_t targetSignature[2] = {0, 0};
        lmmh_x64_128(gameSeed, strlen(gameSeed), 33, targetSignature);

        // Find the target with this exact signature
        int32_t targetIndex = -1;
        for (uint32_t i = 0; i < targetFile->nEntries; i++) {
            if (targetFile->entries[i].targetSignature == targetSignature[0]) {
                targetIndex = i;
                break;
            }
        }

        if (targetIndex >= 0) {
            LaunchTarget *target = &targetFile->entries[targetIndex];

            // Only update if it's from our launcher
            if (target->launcherSignature == currentTarget->launcherSignature) {

                // If single-game mode, only update the current game
                if (!deleteAllCovers && target->targetSignature != currentTarget->targetSignature) {
                    // Skip this game - we only want to update the current one
                    free(gameName);
                    rowCount++;
                    continue;
                }

                printf("  Match found by signature: %s -> %s (sig: %"PRIu64")\n",
                       target->name, gameName, targetSignature[0]);

                // Update metadata from CSV
                char *gameDate = getCsvField(csvLine, 2);
                char *scoreString = getCsvField(csvLine, 3);
                char *metaScoreString = getCsvField(csvLine, 4);
                char *description = getCsvField(csvLine, 6);
                char *coverArtUrl = getCsvField(csvLine, 7);
                char *gameId = getCsvField(csvLine, 8);

                // Update cover URL
                if (coverArtUrl && strlen(coverArtUrl) > 0) {
                    strncpy(target->coverUrl, coverArtUrl, PATH_MAX - 1);
                    printf("    Updated cover URL: %s\n", coverArtUrl);
                }

                // Update date
                if (gameDate) {
                    if (strlen(gameDate) == 10) {
                        memcpy(target->date, gameDate, 10);
                    } else if (strlen(gameDate) == 4 && strtod(gameDate, NULL)) {
                        memcpy(target->date, gameDate, 4);
                    }
                }

                // Update score/ranking
                float score = -1;
                if (scoreString && strlen(scoreString) != 0) {
                    float gfScore = atof(scoreString);
                    // Validate: must be numeric and in valid range
                    if (gfScore > 0 && gfScore <= 5.0) {
                        score = gfScore * 2 * 10;
                    }
                }
                if (metaScoreString && strlen(metaScoreString) != 0) {
                    float metaScore = atof(metaScoreString);
                    // Validate: must be numeric and in valid range
                    if (metaScore > 0 && metaScore <= 100) {
                        if (score == -1) {
                            score = metaScore;
                        } else {
                            score = (score + metaScore) / 2;
                        }
                    }
                }
                // Set sentinel value if no valid score
                if (score == -1) {
                    target->ranking = 999;
                } else {
                    target->ranking = (uint32_t)round(score);
                }

                // Update game ID
                if (gameId && strlen(gameId) > 0) {
                    strncpy(target->id, gameId, OFFBLAST_NAME_MAX - 1);
                }

                // Update description
                if (description && strlen(description) > 0) {
                    target->descriptionOffset = writeDescriptionBlob(target, description);
                    printf("    Updated description (%zu bytes)\n", strlen(description));
                }

                // Clean up
                free(gameDate);
                free(scoreString);
                free(metaScoreString);
                free(description);
                free(coverArtUrl);
                free(gameId);

                matchCount++;
                offblast->rescrapeProcessed = matchCount; // Update progress

                // If single-game mode, we found our game - stop processing
                if (!deleteAllCovers) {
                    printf("Found and updated current game, stopping CSV scan\n");
                    free(gameSeed);
                    free(gameName);
                    free(csvLine);
                    fclose(csvFile);
                    goto skip_csv_cleanup;
                }
            }
        }

        free(gameSeed);
        free(gameName);
        rowCount++;
    }

    free(csvLine);
    fclose(csvFile);

skip_csv_cleanup:

    printf("\nMetadata refresh complete: %u/%u games updated from OpenGameDB\n",
           matchCount, affectedCount);

    // Update status to show completion
    snprintf(offblast->statusMessage, sizeof(offblast->statusMessage),
             "%.203s metadata refreshed: %u games",
             targetLauncher->platform, matchCount);
    offblast->statusMessageTick = SDL_GetTicks();
    offblast->statusMessageDuration = 3000; // Show for 3 seconds before fade
    offblast->rescrapeInProgress = 0;
    offblast->rescrapeProcessed = 0;
    offblast->rescrapeTotal = 0;

    // Force image store to reload covers for affected targets
    printf("Clearing image cache for affected games...\n");
    pthread_mutex_lock(&offblast->imageStoreLock);
    for (uint32_t i = 0; i < IMAGE_STORE_SIZE; i++) {
        for (uint32_t j = 0; j < targetFile->nEntries; j++) {
            if (targetFile->entries[j].launcherSignature == currentTarget->launcherSignature &&
                offblast->imageStore[i].targetSignature == targetFile->entries[j].targetSignature) {
                // Mark as cold to force reload
                offblast->imageStore[i].state = IMAGE_STATE_COLD;
                offblast->imageStore[i].targetSignature = 0;
                if (offblast->imageStore[i].textureHandle) {
                    glDeleteTextures(1, &offblast->imageStore[i].textureHandle);
                    offblast->imageStore[i].textureHandle = 0;
                    offblast->numLoadedTextures--;
                }
                printf("  Cleared image cache for signature %"PRIu64"\n",
                       targetFile->entries[j].targetSignature);
            }
        }
    }
    pthread_mutex_unlock(&offblast->imageStoreLock);

    // Refresh UI
    updateHomeLists();
    updateGameInfo();

    printf("=== METADATA REFRESH COMPLETE ===\n\n");
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

// Get window UUID by PID using KWin scripting (for Wayland/KDE)
// This creates and runs a KWin script to find the window
void getActiveKWinWindowUuid(char *uuidOut, size_t uuidSize) {
    // Not needed with PID-based activation
    uuidOut[0] = '\0';
}

// Dummy implementation for compatibility
void getKWinWindowUuids(char *output, size_t outputSize) {
    output[0] = '\0';
}

// Get currently active window UUID (game window should be active when this is called)
int getKWinWindowUuidByPid(pid_t pid, char *uuidOut, size_t uuidSize) {
    // When Guide is pressed, the game window is currently active
    // Just get the active window's UUID
    getActiveKWinWindowUuid(uuidOut, uuidSize);
    return (strlen(uuidOut) > 0) ? 1 : 0;
}

// Activate window by PID using KWin scripting (for Wayland/KDE)
void activateWindowByPid(pid_t gamePid) {
    if (offblast->sessionType != SESSION_TYPE_WAYLAND ||
        offblast->windowManager != WINDOW_MANAGER_KDE) {
        return;  // Not applicable
    }

    if (gamePid <= 0) {
        printf("No valid PID to activate\n");
        return;
    }

    char *homePath = getenv("HOME");
    if (!homePath) return;

    // Create script directory
    char scriptDir[PATH_MAX];
    snprintf(scriptDir, sizeof(scriptDir), "%s/.config/offblast-kwin", homePath);
    mkdir(scriptDir, 0755);

    // Create KWin script to activate window by PID
    char scriptPath[PATH_MAX];
    snprintf(scriptPath, sizeof(scriptPath), "%s/activate_%d.js", scriptDir, gamePid);

    FILE *scriptFile = fopen(scriptPath, "w");
    if (!scriptFile) {
        printf("Could not create KWin script\n");
        return;
    }

    fprintf(scriptFile,
        "// Find and activate game window (not offblast)\n"
        "var clients = workspace.stackingOrder;\n"
        "for (var i = clients.length - 1; i >= 0; i--) {\n"
        "    var client = clients[i];\n"
        "    // Skip offblast window itself\n"
        "    if (client.resourceClass && client.resourceClass.toString().indexOf('offblast') >= 0) {\n"
        "        continue;\n"
        "    }\n"
        "    // Skip desktop/panel windows\n"
        "    if (client.skipTaskbar || client.skipSwitcher) {\n"
        "        continue;\n"
        "    }\n"
        "    // This should be the game window\n"
        "    if (client.normalWindow || client.dialog) {\n"
        "        workspace.activeWindow = client;\n"
        "        print('Activated: ' + client.caption + ' (class: ' + client.resourceClass + ')');\n"
        "        break;\n"
        "    }\n"
        "}\n");
    fclose(scriptFile);

    // Detect which qdbus command to use
    // Try multiple possible locations/names for Qt5/Qt6 compatibility
    const char *qdbus_cmd = NULL;

    // Try qdbus-qt6 (Fedora Plasma 6)
    if (access("/usr/bin/qdbus-qt6", X_OK) == 0) {
        qdbus_cmd = "/usr/bin/qdbus-qt6";
    }
    // Try qdbus6 (some distros)
    else if (access("/usr/bin/qdbus6", X_OK) == 0) {
        qdbus_cmd = "/usr/bin/qdbus6";
    }
    // Try qdbus (Plasma 5/Bazzite)
    else if (access("/usr/bin/qdbus", X_OK) == 0) {
        qdbus_cmd = "/usr/bin/qdbus";
    }
    else {
        printf("Warning: No qdbus command found (tried qdbus-qt6, qdbus6, qdbus)\n");
        unlink(scriptPath);
        return;
    }

    printf("Using D-Bus command: %s\n", qdbus_cmd);

    // Load script via D-Bus using qdbus (avoids AppImage library conflicts)
    char cmd[PATH_MAX + 256];
    snprintf(cmd, sizeof(cmd),
        "%s org.kde.KWin /Scripting loadScript \"%s\" \"offblast_activate_%d\" 2>&1",
        qdbus_cmd, scriptPath, gamePid);

    printf("Activating window for PID %d via KWin scripting...\n", gamePid);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        printf("Could not execute %s command\n", qdbus_cmd);
        unlink(scriptPath);
        return;
    }

    // Parse script ID from qdbus output (just returns the integer)
    int scriptId = -1;
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        scriptId = atoi(buffer);
    }
    pclose(pipe);

    if (scriptId >= 0) {
        printf("Loaded script with ID: %d, running...\n", scriptId);

        // Run the loaded script
        snprintf(cmd, sizeof(cmd),
            "%s org.kde.KWin /Scripting/Script%d run", qdbus_cmd, scriptId);

        pipe = popen(cmd, "r");
        if (pipe) {
            // Check output for our debug message
            while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
                printf("  Script output: %s", buffer);
            }
            pclose(pipe);
        }

        // Give it a moment to execute
        usleep(100000);

        // Unload the script
        snprintf(cmd, sizeof(cmd),
            "%s org.kde.KWin /Scripting unloadScript \"offblast_activate_%d\"",
            qdbus_cmd, gamePid);
        system(cmd);

        printf("Window activation complete\n");
    } else {
        printf("Could not load KWin script (script ID: %d)\n", scriptId);
    }

    unlink(scriptPath);
}

// Capture the currently focused window ID on GNOME Wayland
void captureGnomeFocusedWindow() {
    static int extensionWarningShown = 0;

    if (offblast->sessionType != SESSION_TYPE_WAYLAND ||
        offblast->windowManager != WINDOW_MANAGER_GNOME) {
        return;  // Not applicable
    }

    // Use window-calls List method to find the focused window
    const char *listCmd =
        "gdbus call --session "
        "--dest org.gnome.Shell "
        "--object-path /org/gnome/Shell/Extensions/Windows "
        "--method org.gnome.Shell.Extensions.Windows.List 2>&1";

    FILE *pipe = popen(listCmd, "r");
    if (!pipe) {
        printf("Could not execute gdbus command\n");
        return;
    }

    // Read the response
    char *response = malloc(16384);
    if (!response) {
        pclose(pipe);
        return;
    }

    size_t responseSize = 0;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        size_t len = strlen(buffer);
        if (responseSize + len < 16384) {
            strcpy(response + responseSize, buffer);
            responseSize += len;
        }
    }
    pclose(pipe);

    // Check if the extension is installed
    if (strstr(response, "GDBus.Error") != NULL ||
        strstr(response, "not provided") != NULL ||
        responseSize < 10) {

        if (!extensionWarningShown) {
            printf("\n");
            printf("========================================\n");
            printf("GNOME Wayland Resume Feature\n");
            printf("========================================\n");
            printf("Resume requires the 'Window Calls' GNOME Shell extension.\n");
            printf("\n");
            printf("To install:\n");
            printf("1. Visit: https://extensions.gnome.org/extension/4724/window-calls/\n");
            printf("2. Click the toggle to install\n");
            printf("3. Restart offblast\n");
            printf("\n");
            printf("Or search for 'Window Calls' in GNOME Extensions\n");
            printf("========================================\n");
            printf("\n");
            extensionWarningShown = 1;
        }
        free(response);
        offblast->gnomeResumeWindowId = -1;
        return;
    }

    // Parse JSON to find focused window
    // Response format: ('[{..."id":12345..."focus":true}...]',)
    // Note: NO spaces in JSON - "focus":true not "focus": true
    char *focusPos = strstr(response, "\"focus\":true");
    if (focusPos) {
        // Search backwards to find the start of this object
        char *objStart = focusPos;
        while (objStart > response && *objStart != '{') {
            objStart--;
        }

        // Now search forward from object start to find the id field
        // id comes before focus in the JSON
        char *idPos = strstr(objStart, "\"id\":");
        if (idPos && idPos < focusPos) {  // id should come before focus
            if (sscanf(idPos + 5, "%ld", &offblast->gnomeResumeWindowId) == 1) {
                printf("Captured focused window ID: %ld\n", offblast->gnomeResumeWindowId);
                free(response);
                return;
            }
        }
    }

    free(response);
    printf("Warning: Could not determine focused window\n");
    offblast->gnomeResumeWindowId = -1;
}

// Activate a window by ID on GNOME Wayland
void activateGnomeWindow(long windowId) {
    if (offblast->sessionType != SESSION_TYPE_WAYLAND ||
        offblast->windowManager != WINDOW_MANAGER_GNOME) {
        return;  // Not applicable
    }

    if (windowId < 0) {
        printf("No valid window ID to activate\n");
        return;
    }

    printf("Activating GNOME window ID %ld...\n", windowId);

    // Use window-calls Activate method
    char activateCmd[512];
    snprintf(activateCmd, sizeof(activateCmd),
        "gdbus call --session "
        "--dest org.gnome.Shell "
        "--object-path /org/gnome/Shell/Extensions/Windows "
        "--method org.gnome.Shell.Extensions.Windows.Activate %u 2>&1",
        (unsigned int)windowId);

    FILE *pipe = popen(activateCmd, "r");
    if (pipe) {
        char buffer[256];
        if (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            printf("Activation result: %s", buffer);
        }
        pclose(pipe);
    }

    printf("Window activation complete\n");
}

// Wrapper for compatibility
void raiseWindowByUuid(const char *uuid) {
    // UUID approach not needed - we use PID directly
    activateWindowByPid(offblast->runningPid);
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
    if (path != NULL) strncpy(rom->path, path, sizeof(rom->path) - 1);
    if (name != NULL) strncpy(rom->name, name, sizeof(rom->name) - 1);
    if (id != NULL) strncpy(rom->id, id, sizeof(rom->id) - 1);

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

// Steam API game entry
typedef struct SteamGame {
    uint32_t appid;
    char name[256];
    uint32_t playtime_forever;
    uint8_t installed;
} SteamGame;

typedef struct SteamGameList {
    SteamGame *games;
    uint32_t count;
    uint32_t allocated;
} SteamGameList;

// Parse Steam date format ("29 Sep, 2017") to "YYYY-MM-DD" or "YYYY"
void parseSteamDate(const char *steamDate, char *outDate, size_t outSize) {
    if (!steamDate || !outDate || outSize < 5) {
        if (outDate && outSize > 0) outDate[0] = '\0';
        return;
    }

    const char *monthNames[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                 "Jul","Aug","Sep","Oct","Nov","Dec"};
    int year = 0, month = 0, day = 0;

    // Find 4-digit year
    const char *p = steamDate;
    while (*p) {
        if (p[0] >= '0' && p[0] <= '9' &&
            p[1] >= '0' && p[1] <= '9' &&
            p[2] >= '0' && p[2] <= '9' &&
            p[3] >= '0' && p[3] <= '9') {
            year = atoi(p);
            break;
        }
        p++;
    }

    // Find month name
    for (int i = 0; i < 12; i++) {
        if (strcasestr(steamDate, monthNames[i])) {
            month = i + 1;
            break;
        }
    }

    // Find day (1-2 digits, typically at start or after comma)
    p = steamDate;
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            int num = atoi(p);
            if (num >= 1 && num <= 31) {
                day = num;
                break;
            }
        }
        p++;
    }

    // Format output
    if (year > 0 && month > 0 && day > 0) {
        snprintf(outDate, outSize, "%04d-%02d-%02d", year, month, day);
    } else if (year > 0 && month > 0) {
        snprintf(outDate, outSize, "%04d-%02d", year, month);
    } else if (year > 0) {
        snprintf(outDate, outSize, "%04d", year);
    } else {
        outDate[0] = '\0';
    }
}

// Fetch metadata from Steam Store API for a single game
// Returns NULL on failure (rate limit, network error, game not found)
SteamMetadata *fetchSteamGameMetadata(uint32_t appid) {
    char url[256];
    snprintf(url, sizeof(url),
        "https://store.steampowered.com/api/appdetails?appids=%u", appid);

    CurlFetch fetch = {0};
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fetch);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);  // Shorter timeout for metadata
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        printf("Steam Store API error for %u: %s\n", appid, curl_easy_strerror(res));
        if (fetch.data) free(fetch.data);
        return NULL;
    }

    // Null-terminate response
    fetch.data = realloc(fetch.data, fetch.size + 1);
    fetch.data[fetch.size] = '\0';

    // Parse JSON response
    json_object *root = json_tokener_parse((char *)fetch.data);
    free(fetch.data);

    if (!root) {
        printf("Failed to parse Steam Store API response for %u\n", appid);
        return NULL;
    }

    // Response format: { "<appid>": { "success": true, "data": { ... } } }
    char appidKey[16];
    snprintf(appidKey, sizeof(appidKey), "%u", appid);

    json_object *appObj;
    if (!json_object_object_get_ex(root, appidKey, &appObj)) {
        json_object_put(root);
        return NULL;
    }

    json_object *successObj;
    if (!json_object_object_get_ex(appObj, "success", &successObj) ||
        !json_object_get_boolean(successObj)) {
        json_object_put(root);
        return NULL;
    }

    json_object *dataObj;
    if (!json_object_object_get_ex(appObj, "data", &dataObj)) {
        json_object_put(root);
        return NULL;
    }

    SteamMetadata *meta = calloc(1, sizeof(SteamMetadata));

    // Get release date and convert to YYYY-MM-DD format
    json_object *releaseDateObj;
    if (json_object_object_get_ex(dataObj, "release_date", &releaseDateObj)) {
        json_object *dateStr;
        if (json_object_object_get_ex(releaseDateObj, "date", &dateStr)) {
            const char *dateVal = json_object_get_string(dateStr);
            if (dateVal && strlen(dateVal) > 0) {
                parseSteamDate(dateVal, meta->date, sizeof(meta->date));
            }
        }
    }

    // Get Metacritic score
    json_object *metacriticObj;
    if (json_object_object_get_ex(dataObj, "metacritic", &metacriticObj)) {
        json_object *scoreObj;
        if (json_object_object_get_ex(metacriticObj, "score", &scoreObj)) {
            meta->score = json_object_get_int(scoreObj);
        }
    }

    // Get short description
    json_object *descObj;
    if (json_object_object_get_ex(dataObj, "short_description", &descObj)) {
        const char *desc = json_object_get_string(descObj);
        if (desc && strlen(desc) > 0) {
            meta->description = strdup(desc);
        }
    }

    json_object_put(root);
    return meta;
}

void freeSteamMetadata(SteamMetadata *meta) {
    if (meta) {
        if (meta->description) free(meta->description);
        free(meta);
    }
}

// SteamGridDB API Functions

/**
 * Search SteamGridDB for games by name
 * Returns NULL on error, caller must free result
 */
SgdbSearchResult *sgdbSearchGames(const char *gameName) {
	if (!offblast->steamGridDbApiKey[0]) {
		printf("SteamGridDB API key not configured\n");
		return NULL;
	}

	// URL encode the game name
	char *encodedName = curl_easy_escape(NULL, gameName, 0);
	if (!encodedName) return NULL;

	char url[512];
	snprintf(url, sizeof(url),
		"https://www.steamgriddb.com/api/v2/search/autocomplete/%s", encodedName);
	curl_free(encodedName);

	CurlFetch fetch = {0};
	CURL *curl = curl_easy_init();
	if (!curl) return NULL;

	struct curl_slist *headers = NULL;
	char authHeader[256];
	snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s",
			 offblast->steamGridDbApiKey);
	headers = curl_slist_append(headers, authHeader);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fetch);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

	CURLcode res = curl_easy_perform(curl);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		printf("SteamGridDB search error: %s\n", curl_easy_strerror(res));
		if (fetch.data) free(fetch.data);
		return NULL;
	}

	// Null-terminate response
	fetch.data = realloc(fetch.data, fetch.size + 1);
	fetch.data[fetch.size] = '\0';

	// Parse JSON: {"success":true,"data":[{"id":123,"name":"Game Name"},...]}
	json_object *root = json_tokener_parse((char *)fetch.data);
	free(fetch.data);

	if (!root) {
		printf("Failed to parse SteamGridDB search response\n");
		return NULL;
	}

	json_object *dataObj;
	if (!json_object_object_get_ex(root, "data", &dataObj)) {
		json_object_put(root);
		return NULL;
	}

	SgdbSearchResult *result = calloc(1, sizeof(SgdbSearchResult));
	size_t numGames = json_object_array_length(dataObj);
	result->numGames = numGames > MAX_SGDB_GAMES ? MAX_SGDB_GAMES : numGames;

	for (size_t i = 0; i < result->numGames; i++) {
		json_object *gameObj = json_object_array_get_idx(dataObj, i);

		json_object *idObj, *nameObj;
		if (json_object_object_get_ex(gameObj, "id", &idObj)) {
			result->games[i].id = json_object_get_int(idObj);
		}
		if (json_object_object_get_ex(gameObj, "name", &nameObj)) {
			strncpy(result->games[i].name, json_object_get_string(nameObj), 255);
			result->games[i].name[255] = '\0';
		}
	}

	json_object_put(root);
	return result;
}

/**
 * Get SteamGridDB game ID from Steam AppID
 * Returns 0 on error
 */
uint32_t sgdbGetGameBySteamId(uint32_t steamAppId) {
	if (!offblast->steamGridDbApiKey[0]) return 0;

	char url[256];
	snprintf(url, sizeof(url),
		"https://www.steamgriddb.com/api/v2/games/steam/%u", steamAppId);

	CurlFetch fetch = {0};
	CURL *curl = curl_easy_init();
	if (!curl) return 0;

	struct curl_slist *headers = NULL;
	char authHeader[256];
	snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s",
			 offblast->steamGridDbApiKey);
	headers = curl_slist_append(headers, authHeader);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fetch);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

	CURLcode res = curl_easy_perform(curl);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		printf("SteamGridDB game lookup error: %s\n", curl_easy_strerror(res));
		if (fetch.data) free(fetch.data);
		return 0;
	}

	fetch.data = realloc(fetch.data, fetch.size + 1);
	fetch.data[fetch.size] = '\0';

	// Parse JSON: {"success":true,"data":{"id":123,...}}
	json_object *root = json_tokener_parse((char *)fetch.data);
	free(fetch.data);

	if (!root) return 0;

	json_object *dataObj;
	uint32_t gameId = 0;
	if (json_object_object_get_ex(root, "data", &dataObj)) {
		json_object *idObj;
		if (json_object_object_get_ex(dataObj, "id", &idObj)) {
			gameId = json_object_get_int(idObj);
		}
	}

	json_object_put(root);
	return gameId;
}

/**
 * Get 600x900 covers for a SteamGridDB game ID
 * Returns NULL on error, caller must free result
 */
SgdbCoverList *sgdbGetCovers(uint32_t gameId) {
	if (!offblast->steamGridDbApiKey[0]) return NULL;

	char url[256];
	snprintf(url, sizeof(url),
		"https://www.steamgriddb.com/api/v2/grids/game/%u?dimensions=600x900", gameId);

	CurlFetch fetch = {0};
	CURL *curl = curl_easy_init();
	if (!curl) return NULL;

	struct curl_slist *headers = NULL;
	char authHeader[256];
	snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s",
			 offblast->steamGridDbApiKey);
	headers = curl_slist_append(headers, authHeader);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fetch);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

	CURLcode res = curl_easy_perform(curl);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		printf("SteamGridDB covers error: %s\n", curl_easy_strerror(res));
		if (fetch.data) free(fetch.data);
		return NULL;
	}

	fetch.data = realloc(fetch.data, fetch.size + 1);
	fetch.data[fetch.size] = '\0';

	// Parse JSON: {"success":true,"data":[{"id":123,"url":"...","thumb":"..."},...]}
	json_object *root = json_tokener_parse((char *)fetch.data);
	free(fetch.data);

	if (!root) return NULL;

	json_object *dataObj;
	if (!json_object_object_get_ex(root, "data", &dataObj)) {
		json_object_put(root);
		return NULL;
	}

	SgdbCoverList *result = calloc(1, sizeof(SgdbCoverList));
	size_t numCovers = json_object_array_length(dataObj);
	result->numCovers = numCovers > MAX_SGDB_COVERS ? MAX_SGDB_COVERS : numCovers;

	for (size_t i = 0; i < result->numCovers; i++) {
		json_object *coverObj = json_object_array_get_idx(dataObj, i);

		json_object *idObj, *urlObj, *thumbObj, *widthObj, *heightObj;

		if (json_object_object_get_ex(coverObj, "id", &idObj)) {
			result->covers[i].id = json_object_get_int(idObj);
		}
		if (json_object_object_get_ex(coverObj, "url", &urlObj)) {
			strncpy(result->covers[i].url, json_object_get_string(urlObj), PATH_MAX-1);
		}
		if (json_object_object_get_ex(coverObj, "thumb", &thumbObj)) {
			strncpy(result->covers[i].thumb, json_object_get_string(thumbObj), PATH_MAX-1);
		}
		if (json_object_object_get_ex(coverObj, "width", &widthObj)) {
			result->covers[i].width = json_object_get_int(widthObj);
		}
		if (json_object_object_get_ex(coverObj, "height", &heightObj)) {
			result->covers[i].height = json_object_get_int(heightObj);
		}
		// Note: score/likes field name may vary - API may not provide it
		result->covers[i].score = 0;
	}

	json_object_put(root);
	return result;
}

// Write a description blob and return the offset, or 0 on failure
off_t writeDescriptionBlob(LaunchTarget *target, const char *description) {
    if (!description || strlen(description) == 0) return 0;

    size_t descLen = strlen(description);
    size_t blobSize = sizeof(OffblastBlob) + descLen + 1;

    // Grow file if necessary
    void *pDescriptionFile = growDbFileIfNecessary(
        &offblast->descriptionDb,
        blobSize,
        OFFBLAST_DB_TYPE_BLOB);

    if (pDescriptionFile == NULL) {
        printf("Couldn't expand description file for Steam metadata\n");
        return 0;
    }

    offblast->descriptionFile = (OffblastBlobFile*) pDescriptionFile;

    // Write the blob at cursor position
    OffblastBlob *newDescription = (OffblastBlob*)
        &offblast->descriptionFile->memory[offblast->descriptionFile->cursor];

    newDescription->targetSignature = target->targetSignature;
    newDescription->length = descLen;
    memcpy(&newDescription->content, description, descLen);
    *(newDescription->content + descLen) = '\0';

    off_t offset = offblast->descriptionFile->cursor;
    offblast->descriptionFile->cursor += blobSize;

    return offset;
}

// Check if a name indicates a non-game (tool, runtime, etc.)
int isSteamTool(const char *name) {
    if (strstr(name, "Proton") != NULL) return 1;
    if (strstr(name, "Steam Linux Runtime") != NULL) return 1;
    if (strstr(name, "Steamworks Common Redistributables") != NULL) return 1;
    if (strstr(name, "Runtime") != NULL && strstr(name, "Steam") != NULL) return 1;
    return 0;
}

// Fetch owned games from Steam Web API
SteamGameList *fetchSteamLibrary(const char *apiKey, const char *steamId) {
    if (!apiKey || !steamId || !apiKey[0] || !steamId[0]) {
        return NULL;
    }

    char *url;
    asprintf(&url,
        "https://api.steampowered.com/IPlayerService/GetOwnedGames/v1/"
        "?key=%s&steamid=%s&include_appinfo=true&include_played_free_games=true&format=json",
        apiKey, steamId);

    printf("Fetching Steam library for user %s...\n", steamId);

    CurlFetch fetch = {0};
    CURL *curl = curl_easy_init();
    if (!curl) {
        free(url);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fetch);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

    CURLcode res = curl_easy_perform(curl);
    free(url);

    if (res != CURLE_OK) {
        printf("Steam API error: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        if (fetch.data) free(fetch.data);
        return NULL;
    }

    curl_easy_cleanup(curl);

    // Null-terminate the response
    fetch.data = realloc(fetch.data, fetch.size + 1);
    fetch.data[fetch.size] = '\0';

    // Parse JSON response
    json_object *root = json_tokener_parse((char *)fetch.data);
    free(fetch.data);

    if (!root) {
        printf("Failed to parse Steam API response\n");
        return NULL;
    }

    json_object *response;
    if (!json_object_object_get_ex(root, "response", &response)) {
        printf("Invalid Steam API response format\n");
        json_object_put(root);
        return NULL;
    }

    json_object *games_array;
    if (!json_object_object_get_ex(response, "games", &games_array)) {
        printf("No games found in Steam API response\n");
        json_object_put(root);
        return NULL;
    }

    size_t game_count = json_object_array_length(games_array);
    printf("Steam API returned %zu games\n", game_count);

    SteamGameList *list = calloc(1, sizeof(SteamGameList));
    list->games = calloc(game_count, sizeof(SteamGame));
    list->allocated = game_count;
    list->count = 0;

    for (size_t i = 0; i < game_count; i++) {
        json_object *game = json_object_array_get_idx(games_array, i);

        json_object *appid_obj, *name_obj, *playtime_obj;
        json_object_object_get_ex(game, "appid", &appid_obj);
        json_object_object_get_ex(game, "name", &name_obj);
        json_object_object_get_ex(game, "playtime_forever", &playtime_obj);

        const char *name = name_obj ? json_object_get_string(name_obj) : "";

        // Filter out tools/runtimes
        if (isSteamTool(name)) {
            continue;
        }

        SteamGame *sg = &list->games[list->count];
        sg->appid = appid_obj ? json_object_get_int(appid_obj) : 0;
        strncpy(sg->name, name, 255);
        sg->name[255] = '\0';
        sg->playtime_forever = playtime_obj ? json_object_get_int(playtime_obj) : 0;
        sg->installed = 0; // Will be set by checkSteamInstallStatus

        list->count++;
    }

    json_object_put(root);
    printf("Filtered to %u games (excluded tools/runtimes)\n", list->count);
    return list;
}

// Check which Steam games are installed by scanning appmanifest files
void checkSteamInstallStatus(SteamGameList *list) {
    if (!list || !list->games) return;

    char *homePath = getenv("HOME");
    char *steamRoot;
    asprintf(&steamRoot, "%s/.steam/root", homePath);

    // Check if steamRoot is a symlink and resolve it
    char resolvedPath[PATH_MAX];
    if (realpath(steamRoot, resolvedPath) == NULL) {
        printf("Could not resolve Steam root path\n");
        free(steamRoot);
        return;
    }
    free(steamRoot);

    // Read libraryfolders.vdf to get all library paths
    char *libFoldersPath;
    asprintf(&libFoldersPath, "%s/config/libraryfolders.vdf", resolvedPath);

    // Collect library paths (start with default)
    char libraryPaths[10][PATH_MAX];
    int numLibraries = 0;

    if (snprintf(libraryPaths[numLibraries], PATH_MAX, "%s/steamapps", resolvedPath) < PATH_MAX) {
        numLibraries++;
    }

    FILE *fp = fopen(libFoldersPath, "r");
    if (fp) {
        char line[1024];
        while (fgets(line, sizeof(line), fp) && numLibraries < 10) {
            // Look for "path" entries
            char *pathStart = strstr(line, "\"path\"");
            if (pathStart) {
                pathStart = strchr(pathStart + 6, '"');
                if (pathStart) {
                    pathStart++;
                    char *pathEnd = strchr(pathStart, '"');
                    if (pathEnd) {
                        *pathEnd = '\0';
                        if (snprintf(libraryPaths[numLibraries], PATH_MAX, "%s/steamapps", pathStart) >= PATH_MAX) {
                            continue;  // Path too long, skip
                        }
                        // Don't add duplicates
                        int isDupe = 0;
                        for (int i = 0; i < numLibraries; i++) {
                            if (strcmp(libraryPaths[i], libraryPaths[numLibraries]) == 0) {
                                isDupe = 1;
                                break;
                            }
                        }
                        if (!isDupe) numLibraries++;
                    }
                }
            }
        }
        fclose(fp);
    }
    free(libFoldersPath);

    printf("Checking %d Steam library locations for installed games\n", numLibraries);

    // Check each game's install status
    for (uint32_t i = 0; i < list->count; i++) {
        char manifestName[64];
        snprintf(manifestName, sizeof(manifestName), "appmanifest_%u.acf", list->games[i].appid);

        for (int lib = 0; lib < numLibraries; lib++) {
            char manifestPath[PATH_MAX];
            if (snprintf(manifestPath, PATH_MAX, "%s/%s", libraryPaths[lib], manifestName) >= PATH_MAX) {
                continue;  // Path too long, skip
            }

            if (access(manifestPath, F_OK) == 0) {
                list->games[i].installed = 1;
                break;
            }
        }
    }

    // Count installed
    uint32_t installedCount = 0;
    for (uint32_t i = 0; i < list->count; i++) {
        if (list->games[i].installed) installedCount++;
    }
    printf("Found %u installed games out of %u owned\n", installedCount, list->count);
}

void freeSteamGameList(SteamGameList *list) {
    if (list) {
        if (list->games) free(list->games);
        free(list);
    }
}

void importFromSteam(Launcher *theLauncher) {
    // Check if Steam API is configured
    if (offblast->steamApiKey[0] && offblast->steamId[0]) {
        // Use Steam Web API
        SteamGameList *steamGames = fetchSteamLibrary(
            offblast->steamApiKey, offblast->steamId);

        if (!steamGames) {
            printf("Failed to fetch Steam library from API, trying local fallback\n");
            goto local_fallback;
        }

        // Check install status
        checkSteamInstallStatus(steamGames);

        // Process each game
        for (uint32_t i = 0; i < steamGames->count; i++) {
            SteamGame *sg = &steamGames->games[i];

            // Update progress
            if (offblast->loadingMode) {
                pthread_mutex_lock(&offblast->loadingState.mutex);
                offblast->loadingState.progress = i + 1;
                offblast->loadingState.progressTotal = steamGames->count;
                pthread_mutex_unlock(&offblast->loadingState.mutex);
            }

            // Create ID string
            char appIdStr[32];
            snprintf(appIdStr, sizeof(appIdStr), "%u", sg->appid);

            // Check if we already have this game
            int32_t indexOfEntry = launchTargetIndexByIdMatch(
                offblast->launchTargetFile, appIdStr, theLauncher->platform);

            LaunchTarget *target;

            if (indexOfEntry >= 0) {
                // Update existing entry
                target = &offblast->launchTargetFile->entries[indexOfEntry];
            } else {
                // Create new entry
                LaunchTargetFile *ltFile = offblast->launchTargetFile;

                // Generate target signature
                char *gameSeed;
                asprintf(&gameSeed, "%s_%s", theLauncher->platform, appIdStr);
                uint64_t targetSignature[2] = {0, 0};
                lmmh_x86_128(gameSeed, strlen(gameSeed), 33, (uint32_t*)targetSignature);
                free(gameSeed);

                target = &ltFile->entries[ltFile->nEntries];
                memset(target, 0, sizeof(LaunchTarget));
                target->targetSignature = targetSignature[0];

                // Set platform
                strncpy(target->platform, theLauncher->platform, 255);
                target->platform[255] = '\0';

                // Set ID
                strncpy(target->id, appIdStr, OFFBLAST_NAME_MAX - 1);
                target->id[OFFBLAST_NAME_MAX - 1] = '\0';

                ltFile->nEntries++;
                printf("Added new Steam game: %s (%s)\n", sg->name, appIdStr);
            }

            // Update name from API (always fresh)
            strncpy(target->name, sg->name, OFFBLAST_NAME_MAX - 1);
            target->name[OFFBLAST_NAME_MAX - 1] = '\0';

            // Set cover URL
            char coverUrl[PATH_MAX];
            snprintf(coverUrl, PATH_MAX,
                "https://steamcdn-a.akamaihd.net/steam/apps/%u/library_600x900.jpg",
                sg->appid);
            strncpy(target->coverUrl, coverUrl, PATH_MAX - 1);
            target->coverUrl[PATH_MAX - 1] = '\0';

            // Set launcher signature based on install status
            if (sg->installed) {
                target->launcherSignature = theLauncher->signature;
            } else {
                target->launcherSignature = 0;
            }

            // Sync Steam playtime to our playtime file
            if (sg->playtime_forever > 0) {
                // Convert Steam minutes to our milliseconds
                uint32_t msPlayed = sg->playtime_forever * 60 * 1000;

                // Find or create PlayTime entry
                PlayTime *pt = NULL;
                for (uint32_t j = 0; j < offblast->playTimeFile->nEntries; ++j) {
                    if (offblast->playTimeFile->entries[j].targetSignature == target->targetSignature) {
                        pt = &offblast->playTimeFile->entries[j];
                        break;
                    }
                }

                if (pt == NULL) {
                    // Create new entry
                    void *growState = growDbFileIfNecessary(
                            &offblast->playTimeDb,
                            sizeof(PlayTime),
                            OFFBLAST_DB_TYPE_FIXED);

                    if (growState != NULL) {
                        offblast->playTimeFile = (PlayTimeFile*) growState;
                        pt = &offblast->playTimeFile->entries[offblast->playTimeFile->nEntries++];
                        pt->targetSignature = target->targetSignature;
                        pt->lastPlayed = 0;  // Steam doesn't provide this
                    }
                }

                if (pt != NULL) {
                    // Update playtime from Steam (overwrite with Steam's authoritative data)
                    pt->msPlayed = msPlayed;
                    // Keep existing lastPlayed - Steam API doesn't provide this
                }
            }

            // Fetch metadata if missing (date is empty means no metadata yet)
            if (target->date[0] == '\0') {
                printf("Fetching metadata for: %s (%u)\n", sg->name, sg->appid);
                SteamMetadata *meta = fetchSteamGameMetadata(sg->appid);
                if (meta) {
                    // Set release date
                    if (meta->date[0] != '\0') {
                        memcpy(target->date, meta->date, sizeof(target->date));
                    }

                    // Set score (Metacritic)
                    if (meta->score > 0) {
                        target->ranking = meta->score;
                    } else {
                        target->ranking = 999;  // No score available
                    }

                    // Write description blob
                    if (meta->description) {
                        target->descriptionOffset = writeDescriptionBlob(target, meta->description);
                        if (target->descriptionOffset > 0) {
                            printf("  Stored description at offset %lu\n", target->descriptionOffset);
                        }
                    }

                    printf("  Date: %s, Score: %u\n", meta->date, meta->score);
                    freeSteamMetadata(meta);
                } else {
                    // API fetch failed, set sentinel
                    target->ranking = 999;
                }
            }
        }

        freeSteamGameList(steamGames);
        return;
    }

local_fallback:
    // Fallback: scan local appmanifest files (improved from old registry method)
    printf("Using local Steam detection (no API key configured)\n");

    char *homePath = getenv("HOME");
    char *steamRoot;
    asprintf(&steamRoot, "%s/.steam/root", homePath);

    char resolvedPath[PATH_MAX];
    if (realpath(steamRoot, resolvedPath) == NULL) {
        printf("Could not find Steam installation\n");
        free(steamRoot);
        return;
    }
    free(steamRoot);

    char steamAppsPath[PATH_MAX];
    if (snprintf(steamAppsPath, PATH_MAX, "%s/steamapps", resolvedPath) >= PATH_MAX) {
        printf("Steam path too long\n");
        return;
    }

    DIR *dir = opendir(steamAppsPath);
    if (!dir) {
        printf("Could not open Steam apps directory: %s\n", steamAppsPath);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Look for appmanifest_*.acf files
        if (strncmp(entry->d_name, "appmanifest_", 12) != 0) continue;
        if (!strstr(entry->d_name, ".acf")) continue;

        // Extract appid from filename
        char appIdStr[32] = {0};
        sscanf(entry->d_name, "appmanifest_%31[0-9].acf", appIdStr);
        if (!appIdStr[0]) continue;

        // Read manifest to get game name
        char manifestPath[PATH_MAX];
        if (snprintf(manifestPath, PATH_MAX, "%s/%s", steamAppsPath, entry->d_name) >= PATH_MAX) {
            continue;  // Path too long, skip
        }

        FILE *fp = fopen(manifestPath, "r");
        if (!fp) continue;

        char gameName[256] = {0};
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            char *nameStart = strstr(line, "\"name\"");
            if (nameStart) {
                nameStart = strchr(nameStart + 6, '"');
                if (nameStart) {
                    nameStart++;
                    char *nameEnd = strchr(nameStart, '"');
                    if (nameEnd) {
                        size_t len = nameEnd - nameStart;
                        if (len > 255) len = 255;
                        strncpy(gameName, nameStart, len);
                        gameName[len] = '\0';
                        break;
                    }
                }
            }
        }
        fclose(fp);

        // Skip tools/runtimes
        if (isSteamTool(gameName)) continue;

        // Skip if no name found
        if (!gameName[0]) continue;

        // Find or create entry
        int32_t indexOfEntry = launchTargetIndexByIdMatch(
            offblast->launchTargetFile, appIdStr, theLauncher->platform);

        if (indexOfEntry >= 0) {
            // Update existing - mark as installed
            LaunchTarget *target = &offblast->launchTargetFile->entries[indexOfEntry];
            target->launcherSignature = theLauncher->signature;
        } else {
            // Create new entry
            LaunchTargetFile *ltFile = offblast->launchTargetFile;

            char *gameSeed;
            asprintf(&gameSeed, "%s_%s", theLauncher->platform, appIdStr);
            uint64_t targetSignature[2] = {0, 0};
            lmmh_x86_128(gameSeed, strlen(gameSeed), 33, (uint32_t*)targetSignature);
            free(gameSeed);

            LaunchTarget *target = &ltFile->entries[ltFile->nEntries];
            memset(target, 0, sizeof(LaunchTarget));

            target->targetSignature = targetSignature[0];
            strncpy(target->name, gameName, OFFBLAST_NAME_MAX - 1);
            target->name[OFFBLAST_NAME_MAX - 1] = '\0';
            strncpy(target->platform, theLauncher->platform, 255);
            target->platform[255] = '\0';
            strncpy(target->id, appIdStr, OFFBLAST_NAME_MAX - 1);
            target->id[OFFBLAST_NAME_MAX - 1] = '\0';

            char coverUrl[PATH_MAX];
            snprintf(coverUrl, PATH_MAX,
                "https://steamcdn-a.akamaihd.net/steam/apps/%s/library_600x900.jpg",
                appIdStr);
            strncpy(target->coverUrl, coverUrl, PATH_MAX - 1);
            target->coverUrl[PATH_MAX - 1] = '\0';

            target->launcherSignature = theLauncher->signature;
            ltFile->nEntries++;

            printf("Added local Steam game: %s (%s)\n", gameName, appIdStr);
        }
    }

    closedir(dir);
}

// Convert arabic numeral to roman numeral (1-39)
const char* arabicToRoman(int num) {
    static const char *romanNumerals[] = {
        "", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X",
        "XI", "XII", "XIII", "XIV", "XV", "XVI", "XVII", "XVIII", "XIX", "XX",
        "XXI", "XXII", "XXIII", "XXIV", "XXV", "XXVI", "XXVII", "XXVIII", "XXIX", "XXX",
        "XXXI", "XXXII", "XXXIII", "XXXIV", "XXXV", "XXXVI", "XXXVII", "XXXVIII", "XXXIX"
    };

    if (num >= 1 && num <= 39) {
        return romanNumerals[num];
    }
    return NULL;
}

// Convert roman numeral to arabic (I-XXXIX)
int romanToArabic(const char *roman) {
    if (strcmp(roman, "I") == 0) return 1;
    if (strcmp(roman, "II") == 0) return 2;
    if (strcmp(roman, "III") == 0) return 3;
    if (strcmp(roman, "IV") == 0) return 4;
    if (strcmp(roman, "V") == 0) return 5;
    if (strcmp(roman, "VI") == 0) return 6;
    if (strcmp(roman, "VII") == 0) return 7;
    if (strcmp(roman, "VIII") == 0) return 8;
    if (strcmp(roman, "IX") == 0) return 9;
    if (strcmp(roman, "X") == 0) return 10;
    if (strcmp(roman, "XI") == 0) return 11;
    if (strcmp(roman, "XII") == 0) return 12;
    if (strcmp(roman, "XIII") == 0) return 13;
    if (strcmp(roman, "XIV") == 0) return 14;
    if (strcmp(roman, "XV") == 0) return 15;
    if (strcmp(roman, "XVI") == 0) return 16;
    if (strcmp(roman, "XVII") == 0) return 17;
    if (strcmp(roman, "XVIII") == 0) return 18;
    if (strcmp(roman, "XIX") == 0) return 19;
    if (strcmp(roman, "XX") == 0) return 20;
    if (strcmp(roman, "XXI") == 0) return 21;
    if (strcmp(roman, "XXII") == 0) return 22;
    if (strcmp(roman, "XXIII") == 0) return 23;
    if (strcmp(roman, "XXIV") == 0) return 24;
    if (strcmp(roman, "XXV") == 0) return 25;
    if (strcmp(roman, "XXVI") == 0) return 26;
    if (strcmp(roman, "XXVII") == 0) return 27;
    if (strcmp(roman, "XXVIII") == 0) return 28;
    if (strcmp(roman, "XXIX") == 0) return 29;
    if (strcmp(roman, "XXX") == 0) return 30;
    if (strcmp(roman, "XXXI") == 0) return 31;
    if (strcmp(roman, "XXXII") == 0) return 32;
    if (strcmp(roman, "XXXIII") == 0) return 33;
    if (strcmp(roman, "XXXIV") == 0) return 34;
    if (strcmp(roman, "XXXV") == 0) return 35;
    if (strcmp(roman, "XXXVI") == 0) return 36;
    if (strcmp(roman, "XXXVII") == 0) return 37;
    if (strcmp(roman, "XXXVIII") == 0) return 38;
    if (strcmp(roman, "XXXIX") == 0) return 39;
    return -1;  // Not found
}

// Convert game name with numeral at the end
// Returns newly allocated string or NULL if no conversion needed
char* convertNumeralInGameName(const char *gameName) {
    if (!gameName) return NULL;

    size_t len = strlen(gameName);
    if (len < 2) return NULL;

    // Find the last space in the name
    const char *lastSpace = strrchr(gameName, ' ');
    if (!lastSpace) return NULL;

    const char *suffix = lastSpace + 1;
    size_t prefixLen = lastSpace - gameName;

    // Try converting arabic to roman
    char *endPtr;
    long num = strtol(suffix, &endPtr, 10);
    if (*endPtr == '\0' && num >= 1 && num <= 39) {
        // It's a pure arabic numeral
        const char *roman = arabicToRoman((int)num);
        if (roman) {
            char *result = malloc(prefixLen + 1 + strlen(roman) + 1);
            strncpy(result, gameName, prefixLen);
            result[prefixLen] = ' ';
            strcpy(result + prefixLen + 1, roman);
            return result;
        }
    }

    // Try converting roman to arabic
    int arabic = romanToArabic(suffix);
    if (arabic != -1) {
        char *result = malloc(prefixLen + 1 + 10 + 1);  // Space for number
        strncpy(result, gameName, prefixLen);
        result[prefixLen] = ' ';
        sprintf(result + prefixLen + 1, "%d", arabic);
        return result;
    }

    return NULL;  // No conversion possible
}


void importFromCustom(Launcher *theLauncher) {

    DIR *dir = NULL;
    struct sigaction sa, old_sa;
    int attempts = 0;
    const int max_attempts = 2;
    const unsigned int timeouts[] = {5, 10}; // First try 5s, then 10s

    // Setup signal handler for alarm timeout
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = nfs_timeout_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, &old_sa);

    for (attempts = 0; attempts < max_attempts && dir == NULL; attempts++) {
        nfs_timeout_occurred = 0;

        if (setjmp(nfs_timeout_jmpbuf) == 0) {
            // Set alarm for timeout
            alarm(timeouts[attempts]);
            printf("Attempting to access %s (timeout: %us, attempt %d/%d)...\n",
                   theLauncher->romPath, timeouts[attempts], attempts + 1, max_attempts);

            dir = opendir(theLauncher->romPath);

            // Cancel alarm if opendir succeeded
            alarm(0);

            if (dir == NULL && !nfs_timeout_occurred) {
                // opendir failed but not due to timeout
                printf("ERROR: Cannot access %s rom_path: '%s'\n", theLauncher->type, theLauncher->romPath);
                if (strlen(theLauncher->romPath) == 0) {
                    printf("       The rom_path is empty. Please set it in config.json\n");
                } else {
                    printf("       Please check that the directory exists and is readable\n");
                }
                sigaction(SIGALRM, &old_sa, NULL);
                return;
            }
        } else {
            // Timeout occurred - longjmp returned here
            alarm(0);  // Cancel alarm
            printf("Timeout accessing %s after %u seconds\n", theLauncher->romPath, timeouts[attempts]);

            if (attempts + 1 >= max_attempts) {
                // Final attempt failed
                printf("ERROR: Cannot access ROM path '%s' after %d timeout attempts\n",
                       theLauncher->romPath, max_attempts);
                printf("       Directory may be on unavailable network share (NFS, SMB, etc.)\n");
                printf("       Check network connectivity or update rom_path in config.json\n");
                sigaction(SIGALRM, &old_sa, NULL);
                return;
            }
        }
    }

    // Restore old signal handler
    sigaction(SIGALRM, &old_sa, NULL);

    if (dir == NULL) {
        printf("ERROR: Failed to access %s after %d attempts\n", theLauncher->romPath, max_attempts);
        return;
    }

    RomFoundList *list = newRomList();

    // Check if we should use pattern-based scanning
    if (strlen(theLauncher->scanPattern) > 0) {
        // Special case: DIRECTORY means scan for directories instead of files
        if (strcmp(theLauncher->scanPattern, "DIRECTORY") == 0) {
            // Scan for subdirectories in rom_path
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                if (strcmp(entry->d_name, ".") == 0) continue;
                if (strcmp(entry->d_name, "..") == 0) continue;

                // Check if this is a directory
                char fullPath[PATH_MAX * 2];  // Extra space to avoid truncation
                snprintf(fullPath, sizeof(fullPath), "%s/%s",
                        theLauncher->romPath, entry->d_name);
                fullPath[PATH_MAX - 1] = '\0';  // Ensure null termination at PATH_MAX boundary

                struct stat statbuf;
                if (stat(fullPath, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
                    // This is a directory - add it as a game
                    // Extract game name from directory (remove suffixes like "(CD VGA)")
                    char gameName[256];
                    strncpy(gameName, entry->d_name, 255);
                    gameName[255] = '\0';

                    // Remove common suffixes in parentheses for cleaner names
                    char *paren = strchr(gameName, '(');
                    if (paren && paren > gameName && *(paren-1) == ' ') {
                        *(paren-1) = '\0';
                    }

                    // Use match string as path if configured to do so
                    if (theLauncher->pathIsMatchString) {
                        pushToRomList(list, gameName, gameName, NULL);
                    } else {
                        pushToRomList(list, fullPath, gameName, NULL);
                    }
                    printf("Found game directory: %s -> %s\n", entry->d_name, gameName);
                }
            }
        }
        else {
            // Parse scan_pattern for {*} marker to determine extraction logic
            char *extractionAnchor = NULL;
            char globPatternStr[256];
            strncpy(globPatternStr, theLauncher->scanPattern, 255);
            globPatternStr[255] = '\0';

            // Check if pattern contains {*} marker
            char *markerPos = strstr(globPatternStr, "{*}");
            if (markerPos != NULL) {
                // Extract the anchor string (what follows {*})
                extractionAnchor = strdup(markerPos + 3);  // Skip past "{*}"
                // Remove {*} from the glob pattern
                memmove(markerPos, markerPos + 2, strlen(markerPos + 2) + 1);  // Remove "{*", leave "}"
                markerPos[0] = '*';  // Replace with plain "*"
            } else if (strncmp(globPatternStr, "*/", 2) == 0) {
                // Pattern starts with */ - derive anchor from pattern
                // E.g., "*/vol/code/*.rpx" -> anchor "/vol/code/"
                char *patternCopy = strdup(globPatternStr + 2);  // Skip "*/"
                char *nextWildcard = strchr(patternCopy, '*');
                if (nextWildcard != NULL) {
                    *nextWildcard = '\0';  // Terminate at next wildcard
                }
                // Build anchor with leading /
                asprintf(&extractionAnchor, "/%s", patternCopy);
                free(patternCopy);
            }

            // Use glob to find files matching the pattern
            char globPattern[PATH_MAX * 2];  // Extra space to avoid truncation
            snprintf(globPattern, sizeof(globPattern), "%s/%s",
                    theLauncher->romPath, globPatternStr);
            globPattern[PATH_MAX - 1] = '\0';  // Ensure null termination at PATH_MAX boundary

            glob_t globResult;
            printf("Scanning with pattern: %s\n", globPattern);
            if (extractionAnchor != NULL) {
                printf("Using extraction anchor: %s\n", extractionAnchor);
            }

            if (glob(globPattern, GLOB_NOSORT, NULL, &globResult) == 0) {
                for (size_t i = 0; i < globResult.gl_pathc; i++) {
                    char *filePath = globResult.gl_pathv[i];

                    // Extract game name from path
                    char *gameName = NULL;
                    if (extractionAnchor != NULL) {
                        // Use anchor string to find and extract folder name
                        char *temp = strdup(filePath);
                        char *anchorPos = strstr(temp, extractionAnchor);
                        if (anchorPos) {
                            *anchorPos = '\0';
                            char *lastSlash = strrchr(temp, '/');
                            if (lastSlash) {
                                gameName = strdup(lastSlash + 1);
                            }
                        }
                        free(temp);
                    } else {
                        // Default: extract parent folder of matched file
                        char *temp = strdup(filePath);
                        char *lastSlash = strrchr(temp, '/');
                        if (lastSlash) {
                            *lastSlash = '\0';  // Remove filename
                            lastSlash = strrchr(temp, '/');
                            if (lastSlash) {
                                gameName = strdup(lastSlash + 1);
                            }
                        }
                        free(temp);
                    }

                    // Use match string as path if configured to do so
                    if (theLauncher->pathIsMatchString && gameName != NULL) {
                        pushToRomList(list, gameName, gameName, NULL);
                    } else {
                        pushToRomList(list, filePath, gameName, NULL);
                    }
                    if (gameName) free(gameName);
                }
                globfree(&globResult);
            }

            if (extractionAnchor != NULL) {
                free(extractionAnchor);
            }
        }
    }
    else {
        // Standard directory scanning with extensions
        struct dirent *currentEntry;
        while ((currentEntry = readdir(dir)) != NULL) {

            if (currentEntry->d_name[0] == '.') continue;
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

            // Check if we have a stored name (from pattern-based scanning)
            if (list->items[j].name[0] != '\0') {
                // Use the stored name for pattern-based scanning (Wii U, PS3, ScummVM)
                searchString = strdup(list->items[j].name);
                printf("Using stored name for matching: %s\n", searchString);
            }
            else {
                // Fall back to extracting from filename for regular scanning
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
                if (ext != NULL) {
                    *ext = '\0';
                    if (ext > searchString && *(ext-1) == ' ') *(ext-1) = '\0';
                }
            }

            float matchScore = 0;

            printf("\nDEBUG: Searching for ROM: %s\n", searchString);
            printf("       Platform: %s\n", theLauncher->platform);
            printf("       Match field: %s\n", theLauncher->matchField);

            int32_t indexOfEntry = launchTargetIndexByFieldMatch(
                    offblast->launchTargetFile,
                    theLauncher->matchField,
                    searchString,
                    theLauncher->platform,
                    &matchScore);

            // Try with converted numerals if no match found or poor match
            if ((indexOfEntry == -1 || matchScore < 1.0) && strcmp(theLauncher->matchField, "title") == 0) {
                char *convertedName = convertNumeralInGameName(searchString);
                if (convertedName) {
                    printf("       Trying with converted numerals: %s\n", convertedName);
                    float convertedMatchScore = 0;
                    int32_t convertedIndex = launchTargetIndexByFieldMatch(
                            offblast->launchTargetFile,
                            theLauncher->matchField,
                            convertedName,
                            theLauncher->platform,
                            &convertedMatchScore);

                    // Use converted version if it's better
                    if (convertedIndex > -1 && convertedMatchScore > matchScore) {
                        printf("       Converted version matched better: score %f vs %f\n",
                               convertedMatchScore, matchScore);
                        indexOfEntry = convertedIndex;
                        matchScore = convertedMatchScore;
                    }
                    free(convertedName);
                }
            }

            free(searchString);

            if (indexOfEntry > -1) {
                printf("DEBUG: Found match at index %d with score %f\n", indexOfEntry, matchScore);
                LaunchTarget *theTarget = &offblast->launchTargetFile->entries[indexOfEntry];
                printf("       Matched to: %s\n", theTarget->name);
                printf("       Current matchScore: %f, new matchScore: %f\n",
                       theTarget->matchScore, matchScore);
            } else {
                printf("DEBUG: No match found at all\n");
            }

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
                strncpy(theTarget->path,
                        (char *) &list->items[j].path,
                        PATH_MAX - 1);
                theTarget->path[PATH_MAX - 1] = '\0';
                theTarget->matchScore = matchScore;

                printf("DEBUG: Successfully assigned path to %s\n", theTarget->name);

                // Log poor matches for review
                if (matchScore < 0.5) {
                    printf("WARNING: Poor match score (%.2f) - logging for review\n", matchScore);
                    logPoorMatch(list->items[j].path, theTarget->name, matchScore);
                }

            }
            else {
                printf("No match found (or score too low) for %s\n", list->items[j].path);
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

    if (fp != NULL) {
        fwrite(missingGamePath, strlen(missingGamePath), 1, fp);
        fwrite("\n", 1, 1, fp);
        fclose(fp);
    } else {
        printf("Warning: Could not open %s for logging\n", path);
    }

    free(path);
}

void logPoorMatch(char *romPath, char *matchedName, float matchScore) {
    char *path = NULL;
    asprintf(&path, "%s/missinggames.log", offblast->configPath);
    FILE * fp = fopen(path, "a+");

    if (fp != NULL) {
        char *logLine = NULL;
        asprintf(&logLine, "%s -> %s (score: %.2f)\n", romPath, matchedName, matchScore);
        fwrite(logLine, strlen(logLine), 1, fp);
        free(logLine);
        fclose(fp);
    } else {
        printf("Warning: Could not open %s for logging\n", path);
    }

    free(path);
}

void calculateRowGeometry(UiRow *row) {

    UiTile *theTile = NULL;
    uint32_t theWidth = 0;
    uint32_t xAdvance = 0;

    for(uint8_t i = 0; i < row->length; ++i) {

        theTile = &row->tiles[i];
        theTile->baseX = xAdvance;
        Image *theImage = requestImageForTarget(theTile->target, 0);

        theWidth = getWidthForScaledImage(
                offblast->mainUi.boxHeight,
                theImage);

        xAdvance += (theWidth + offblast->mainUi.boxPad);
    }
}

void evictOldestTexture() {
    // This function assumes the imageStoreLock is already held
    uint32_t oldestTick = UINT32_MAX;
    int32_t oldestIndex = -1;

    for (uint32_t i = 0; i < IMAGE_STORE_SIZE; i++) {
        if (offblast->imageStore[i].state == IMAGE_STATE_COMPLETE &&
            offblast->imageStore[i].textureHandle != 0) {
            if (offblast->imageStore[i].lastUsedTick < oldestTick) {
                oldestTick = offblast->imageStore[i].lastUsedTick;
                oldestIndex = i;
            }
        }
    }

    if (oldestIndex != -1) {
        // Free the texture
        glDeleteTextures(1, &offblast->imageStore[oldestIndex].textureHandle);
        offblast->imageStore[oldestIndex].textureHandle = 0;
        offblast->imageStore[oldestIndex].state = IMAGE_STATE_COLD;
        offblast->numLoadedTextures--;

    }
}

void evictTexturesOlderThan(uint32_t ageMs) {
    // This function assumes the imageStoreLock is already held
    uint32_t currentTick = SDL_GetTicks();
    uint32_t evictedCount = 0;

    for (uint32_t i = 0; i < IMAGE_STORE_SIZE; i++) {
        if (offblast->imageStore[i].state == IMAGE_STATE_COMPLETE &&
            offblast->imageStore[i].textureHandle != 0) {
            if (currentTick - offblast->imageStore[i].lastUsedTick > ageMs) {
                // Free the texture
                glDeleteTextures(1, &offblast->imageStore[i].textureHandle);
                offblast->imageStore[i].textureHandle = 0;
                offblast->imageStore[i].state = IMAGE_STATE_COLD;
                offblast->numLoadedTextures--;
                evictedCount++;
            }
        }
    }

}

Image *requestImageForTarget(LaunchTarget *target, uint32_t affectQueue) {

    uint64_t targetSignature = target->targetSignature;
    char *path;
    char *url;

    int32_t foundAtIndex = -1;
    int32_t oldestFreeIndex = -1;
    uint32_t oldestFreeTick = 0;

    // DEBUG
    uint32_t availableSlots = 0;
    uint32_t inLoading = 0;
    uint32_t inDownloading = 0;
    uint32_t inReady = 0;
    uint32_t inQueued = 0;

    uint32_t tickNow = SDL_GetTicks();

    Image *returnImage;

    pthread_mutex_lock(&offblast->imageStoreLock);

    for (uint32_t i=0; i < IMAGE_STORE_SIZE; ++i) {

        // Load anything that's ready.
        if (offblast->imageStore[i].state == IMAGE_STATE_READY) {

            // Check if we're at the texture limit
            if (offblast->numLoadedTextures >= MAX_LOADED_TEXTURES) {
                evictOldestTexture();
            }

            glGenTextures(1, &offblast->imageStore[i].textureHandle);
            imageToGlTexture(
                    &offblast->imageStore[i].textureHandle,
                    offblast->imageStore[i].atlas,
                    offblast->imageStore[i].width,
                    offblast->imageStore[i].height);

            offblast->imageStore[i].state = IMAGE_STATE_COMPLETE;
            offblast->numLoadedTextures++;
            free(offblast->imageStore[i].atlas);
            offblast->imageStore[i].atlas = NULL;
            offblast->mainUi.rowGeometryInvalid = 1;
        }

        // Search for the target
        if (offblast->imageStore[i].targetSignature == targetSignature)
        {
            if (affectQueue) 
                offblast->imageStore[i].lastUsedTick = tickNow;

            foundAtIndex = i;
        }
        else {
            uint32_t isAvailable = 
                    offblast->imageStore[i].state == IMAGE_STATE_COLD
                    || offblast->imageStore[i].state == IMAGE_STATE_DEAD
                    || offblast->imageStore[i].state == IMAGE_STATE_COMPLETE;

            // DEBUG
            if (offblast->imageStore[i].state == IMAGE_STATE_LOADING) {
                inLoading++;
            }
            if (offblast->imageStore[i].state == IMAGE_STATE_READY) {
                inReady++;
            }
            if (offblast->imageStore[i].state == IMAGE_STATE_QUEUED) {
                inQueued++;
            }
            if (offblast->imageStore[i].state == IMAGE_STATE_DOWNLOADING) {
                inDownloading++;
            }

            if (isAvailable) {
                availableSlots++;

                //printf("slot %d available\n", i);

                if (oldestFreeTick == 0 
                    || offblast->imageStore[i].lastUsedTick <= oldestFreeTick) 
                {
                    if (affectQueue)
                        oldestFreeTick = offblast->imageStore[i].lastUsedTick;
                    oldestFreeIndex = i;
                }

            }
            else {
                //printf("slot %d loading\n", i);
            }
        }
    }

    // § DEBUG - Dump out the queue status
    //printf("\t%d av\t%d lo\t%d rd\t%d Q\t%d dl\n", availableSlots, inLoading, inReady, inQueued, inDownloading);

    if (foundAtIndex > -1) {



        if (offblast->imageStore[foundAtIndex].state == IMAGE_STATE_COMPLETE) {
            returnImage = &offblast->imageStore[foundAtIndex];
        }
        else if (offblast->imageStore[foundAtIndex].state == IMAGE_STATE_COLD && affectQueue) {
            // Re-queue evicted texture for loading
            path = getCoverPath(target);
            url = getCoverUrl(target);

            offblast->imageStore[foundAtIndex].state = IMAGE_STATE_QUEUED;
            offblast->imageStore[foundAtIndex].lastUsedTick = tickNow;
            strncpy(offblast->imageStore[foundAtIndex].path, path, PATH_MAX);
            strncpy(offblast->imageStore[foundAtIndex].url, url, PATH_MAX);

            returnImage = &offblast->missingCoverImage;
        }
        else {
            // TODO consider a loading image if the state is different
            returnImage = &offblast->missingCoverImage;
        }

    }
    else {

        if (oldestFreeIndex != -1 && affectQueue) {

            path = getCoverPath(target);
            url = getCoverUrl(target);

            offblast->imageStore[oldestFreeIndex].state = IMAGE_STATE_QUEUED;
            offblast->imageStore[oldestFreeIndex].targetSignature = targetSignature;
            offblast->imageStore[oldestFreeIndex].lastUsedTick = tickNow;
            strncpy(offblast->imageStore[oldestFreeIndex].path, path, PATH_MAX);
            strncpy(offblast->imageStore[oldestFreeIndex].url, url, PATH_MAX);

            if (offblast->imageStore[oldestFreeIndex].textureHandle) {
                glDeleteTextures(1,
                        &offblast->imageStore[oldestFreeIndex].textureHandle);
                offblast->numLoadedTextures--;
            }
            offblast->imageStore[oldestFreeIndex].textureHandle = 0;
            //printf("%"PRIu64" queued in slot %d\n", targetSignature, oldestFreeIndex);
        }

        returnImage = &offblast->missingCoverImage;
    }

    pthread_mutex_unlock(&offblast->imageStoreLock);


    return returnImage;
}

void condPrintConfigError(void *object, const char *message) {
    if (object == NULL) {
        printf("Offblast Config Error:\n%s\n", message);
        printf("Please refer to the example config to see where you might be going wrong.\n");
        exit(1);
    }
}
