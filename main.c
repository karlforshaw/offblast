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
#define OFFBLAST_MAX_PLAYERS 4

#define NAVIGATION_MOVE_DURATION 250 

// ALPHA 0.2 HITLIST
//      2. STB TRUETYPE
//          -- trailing off ... 
//          -- align center, right
//      3. Recently Played and Play Duration
//      6. watch out for vram! glDeleteTextures
//
// Known Bugs:
//      -- ** Animation method for alpha no longer works as we're rendering 
//          text char by char, we need to choose which string to render based
//          on the animations half way point.
//      - Invalid date format is a thing
//      - Only JPG covers are supported
//          The image library supports writing out images to different formats
//          we can load the image and then save it as a jpg
//          USE STB IMAGE WRITE FOR THIS
//      * if you add a rom after the platform has been scraped we say we already
//          have it in the db but this is the target, not the filepath etc
//
// TODO multidisk PS games.
//      - need to get smart about detection of multidisk PS games and not
//          entirely sure how to do it just yet. I could either create m3u files
//          for all my playstation games and just launch the m3u's.. maybe 
//          add a tool that creates it.. but that's harder than it needs to be
//          perhaps when we are detecting tokens we could see if theres a 
//          "(Disk X)" token in the string and if there is and theres an m3u
//          file present we use that instead?
//
// TODO GFX
//      * can we do a blurred background layer?
//              https://www.shadertoy.com/view/XdfGDH
//
//      * I quite liked the look of the white mix of 0.3 on the cover art
//          slightly desaturated - might have a shader for covers and do 
//          an average color gradient to the right, white mix 0.3 AND anchor
//          texture to the left with proper ratio, locking in the height
//      * a loading animation 
//      * PLATFORM BADGES ON MIXED LISTS
//
//
// TODO steam support
//      * looks like if you ls .steam/steam/userdata there's a folder for 
//      each game you've played.. this could be a good way to scrape and auto
//      populate for steam.
//
// TODO tighter retroarch integration, 
//      * we can compile this against libretro.h and tap into stuff from 
//      the shared object
//
// TODO List caches, I think when we generate lists we should cache
//      them in files.. maybe?
// TODO Collections, this is more of an opengamedb ticket but It would be
//      cool to feature collections from youtuvers such as metal jesus.
//

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
#include <pthread.h>
        
#define GL3_PROTOTYPES 1
#include <GL/glew.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// TODO remove when it's working? maybe we should only gen the bitmap
// if it doesn't already exist
#define STB_IMAGE_WRITE_IMPLEMENTATION 1
#include "stb_image_write.h"

#include "offblast.h"
#include "offblastDbFile.h"


typedef struct User {
    char name[256];
    char email[512];
    char avatarPath[PATH_MAX];
} User;

typedef struct Player {
    int32_t jsIndex;
    SDL_GameController *usingController; 
    char *name; 
    uint8_t emailHash;
} Player;

typedef struct UiTile{
    struct LaunchTarget *target;
    GLuint textureHandle;
    float textureW;
    float textureH;
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
    void (* callback)();
} Animation;

typedef struct ImageLayer {
    uint32_t textureValid;
    GLuint textureHandle;
    GLuint vbo;
    UiRect vertices;
    uint32_t pixelWidth;
    uint32_t pixelHeight;
} ImageLayer;

typedef struct GradientLayer {
    GLuint vbo;
    UiRect vertices;
    float xSwap;
    float ySwap;
    float offset;
} GradientLayer;


enum UiMode {
    OFFBLAST_UI_MODE_MAIN = 1,
    OFFBLAST_UI_MODE_PLAYER_SELECT = 2,
};

typedef struct PlayerSelectUi {
    ImageLayer *playerAvatarLayers;
    int32_t cursor;
} PlayerSelectUi;

typedef struct MainUi {
    int32_t descriptionWidth;
    int32_t descriptionHeight;
    int32_t boxWidth;
    int32_t boxHeight;
    int32_t boxPad;

    Animation *horizontalAnimation;
    Animation *verticalAnimation;
    Animation *infoAnimation;
    Animation *rowNameAnimation;

    uint32_t numRows;
    UiRect blockVertices;
    GLuint blockVbo;

    UiRow *rowCursor;
    UiRow *rows;
    LaunchTarget *movingToTarget;
    UiRow *movingToRow;

    char *titleText;
    char *infoText;
    char *descriptionText;

    char *rowNameText;

} MainUi ;

typedef struct Launcher {
    char path[PATH_MAX];
    char launcher[MAX_LAUNCH_COMMAND_LENGTH];
} Launcher;

typedef struct OffblastUi {

    enum UiMode mode;

    PlayerSelectUi playerSelectUi;
    MainUi mainUi;

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

    GLuint textVbo;

    stbtt_bakedchar titleCharData[96];
    stbtt_bakedchar infoCharData[96];
    stbtt_bakedchar debugCharData[96];

    uint32_t textBitmapHeight;
    uint32_t textBitmapWidth;

    GLuint imageProgram;
    GLint imageTranslateUni;
    GLint imageAlphaUni;
    GLint imageTexturePosUni; 

    GLuint textProgram;
    GLint textAlphaUni;

    Player players[OFFBLAST_MAX_PLAYERS];

    size_t nUsers;
    User *users;

    size_t nPaths;
    Launcher *launchers;

    OffblastBlobFile *descriptionFile;

} OffblastUi;



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
UiTile *rewindTiles(UiTile *fromTile, uint32_t depth);
void infoFaded();
void rowNameFaded();
uint32_t animationRunning();
void animationTick(Animation *theAnimation);
const char *platformString(char *key);
void *downloadCover(void *arg);
char *getCoverPath();
void updateVbo(GLuint *vbo, UiRect* vertices);
GLint loadShaderFile(const char *path, GLenum shaderType);
GLuint createShaderProgram(GLint vertShader, GLint fragShader);
void launch();
void imageToGlTexture(GLuint textureHandle, unsigned char *pixelData, 
        uint32_t newWidth, uint32_t newHeight);
void updateRect(UiRect *vertices, uint32_t rectWidth, uint32_t rectHeight);
void changeRow(uint32_t direction);
void changeColumn(uint32_t direction);
void renderImageLayer(ImageLayer *layer, float x, float y, float a);
void pressConfirm();
void updateInfoText();
void updateDescriptionText();


#define OFFBLAST_TEXT_TITLE 1
#define OFFBLAST_TEXT_INFO 2
#define OFFBLAST_TEXT_DEBUG 3
void renderSomeText(OffblastUi *offblast, float x, float y, 
        uint32_t textMode, float alpha, uint32_t lineMaxW, char *string) 
{

    glUseProgram(offblast->textProgram);
    glEnable(GL_TEXTURE_2D);

    uint32_t currentLine = 0;
    uint32_t currentWidth = 0;
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

            UiRect vertices = {};

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

            vertices[0][0] = left;
            vertices[0][1] = bottom;
            vertices[0][2] = 0.0f;
            vertices[0][3] = 1.0f;
            vertices[0][4] = texLeft;
            vertices[0][5] = texBottom;

            vertices[1][0] = left;
            vertices[1][1] = top;
            vertices[1][2] = 0.0f;
            vertices[1][3] = 1.0f;
            vertices[1][4] = texLeft;
            vertices[1][5] = texTop;

            vertices[2][0] = right;
            vertices[2][1] = top;
            vertices[2][2] = 0.0f;
            vertices[2][3] = 1.0f;
            vertices[2][4] = texRight;
            vertices[2][5] = texTop;

            vertices[3][0] = right;
            vertices[3][1] = top;
            vertices[3][2] = 0.0f;
            vertices[3][3] = 1.0f;
            vertices[3][4] = texRight;
            vertices[3][5] = texTop;

            vertices[4][0] = right;
            vertices[4][1] = bottom;
            vertices[4][2] = 0.0f;
            vertices[4][3] = 1.0f;
            vertices[4][4] = texRight;
            vertices[4][5] = texBottom;

            vertices[5][0] = left;
            vertices[5][1] = bottom;
            vertices[5][2] = 0.0f;
            vertices[5][3] = 1.0f;
            vertices[5][4] = texLeft;
            vertices[5][5] = texBottom;

            if (offblast->textVbo == 0) {
                glGenBuffers(1, &offblast->textVbo);
                glBindBuffer(GL_ARRAY_BUFFER, offblast->textVbo);
                glBufferData(GL_ARRAY_BUFFER, sizeof(UiRect), 
                        &vertices, GL_STREAM_DRAW);
            }
            else {
                glBindBuffer(GL_ARRAY_BUFFER, offblast->textVbo);
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(UiRect), 
                        &vertices);
            }

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 6*sizeof(float), 0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6*sizeof(float), 
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


OffblastUi *offblast;

int main (int argc, char** argv) {

    printf("\nStarting up OffBlast with %d args.\n\n", argc);
    offblast = calloc(1, sizeof(OffblastUi));


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
    if (!InitDbFile(pathInfoDbPath, &pathDb, sizeof(PathInfo))) {
        printf("couldn't initialize path db, exiting\n");
        return 1;
    }
    PathInfoFile *pathInfoFile = (PathInfoFile*) pathDb.memory;
    free(pathInfoDbPath);

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


#if 0
    // XXX DEBUG Dump out all launch targets
    for (int i = 0; i < launchTargetFile->nEntries; i++) {
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

    char (*platforms)[256] = calloc(MAX_PLATFORMS, 256 * sizeof(char));
    uint32_t nPlatforms = 0;

    offblast->nPaths = json_object_array_length(paths);
    offblast->launchers = calloc(offblast->nPaths, sizeof(Launcher));

    for (int i=0; i < offblast->nPaths; i++) {

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
        memcpy(&offblast->launchers[i].path, thePath, strlen(thePath));
        memcpy(&offblast->launchers[i].launcher, theLauncher, strlen(theLauncher));

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
                        }

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


                        // TODO round properly
                        newEntry->ranking = (uint32_t) score;

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
                                thePath,
                                strlen(thePath));
                    
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

        const char *theName= NULL;
        const char *theEmail = NULL;
        const char *theAvatarPath= NULL;

        workingUserNode = json_object_array_get_idx(usersObject, iUser);
        json_object_object_get_ex(workingUserNode, "name",
                &workingNameNode);
        json_object_object_get_ex(workingUserNode, "email",
                &workingEmailNode);
        json_object_object_get_ex(workingUserNode, "avatar",
                &workingAvatarPathNode);


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

        printf("got user %u --\n%s\n%s\n%s\n", iUser, 
                theName, theEmail, theAvatarPath);
    }

    User *pUser = &offblast->users[iUser];
    memcpy(&pUser->name, "Guest", strlen("Guest"));
    memcpy(&pUser->avatarPath, "guest-512.jpg", strlen("guest-512.jpg"));
    offblast->nUsers++;



    // XXX START SDL HERE

    



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

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    glewInit();
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


    // § Init UI
    MainUi *mainUi = &offblast->mainUi;
    PlayerSelectUi *playerSelectUi = &offblast->playerSelectUi;

    needsReRender(window);
    mainUi->horizontalAnimation = calloc(1, sizeof(Animation));
    mainUi->verticalAnimation = calloc(1, sizeof(Animation));
    mainUi->infoAnimation = calloc(1, sizeof(Animation));
    mainUi->rowNameAnimation = calloc(1, sizeof(Animation));

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
    debugAtlas = NULL;

    for (uint32_t i = 0; i < OFFBLAST_MAX_PLAYERS; i++) {
        offblast->players[i].jsIndex = -1;
    }

    playerSelectUi->playerAvatarLayers = calloc(offblast->nUsers, 
            sizeof(ImageLayer));

    for (uint32_t i = 0; i < offblast->nUsers; i++) {

        ImageLayer *avatarLayer = &playerSelectUi->playerAvatarLayers[i];
        glGenTextures(1, &avatarLayer->textureHandle);

        // TODO we could probably move this into the imageToGL call
        int w, h, n;
        stbi_set_flip_vertically_on_load(1);
        unsigned char *imageData = stbi_load(
                offblast->users[i].avatarPath, &w, &h, &n, 4);

        if(imageData != NULL) {

            imageToGlTexture(
                    avatarLayer->textureHandle, 
                    imageData, w, h);

            avatarLayer->textureValid = 1;
            // TODO do we need these now?
            avatarLayer->pixelWidth = w;
            avatarLayer->pixelHeight = h;
            updateRect(&avatarLayer->vertices, 
                    offblast->mainUi.boxHeight,
                    offblast->mainUi.boxHeight);
            updateVbo(&avatarLayer->vbo, &avatarLayer->vertices);

        }
        else {
            printf("couldn't load texture for avatar %s\n", 
                    offblast->users[i].avatarPath);
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
    offblast->imageTexturePosUni = glGetUniformLocation(
            offblast->imageProgram, "textureSize");

    // Gradient Pipeline
    GLint gradientVertShader = loadShaderFile("shaders/gradient.vert", 
            GL_VERTEX_SHADER);
    GLint gradientFragShader = loadShaderFile("shaders/gradient.frag", 
            GL_FRAGMENT_SHADER);
    assert(gradientVertShader);
    assert(gradientFragShader);
    GLuint gradientProgram = createShaderProgram(gradientVertShader, 
            gradientFragShader);
    assert(gradientProgram);


    int running = 1;
    uint32_t lastTick = SDL_GetTicks();
    uint32_t renderFrequency = 1000/60;

    // Init Ui

    // rows for now:
    // 1. Your Library
    // 2. Essential *platform" 
    mainUi->rows = calloc(1 + nPlatforms, sizeof(UiRow));
    mainUi->numRows = 0;
    mainUi->rowCursor = mainUi->rows;

    // __ROW__ "Your Library"
    uint32_t libraryLength = 0;
    for (uint32_t i = 0; i < launchTargetFile->nEntries; i++) {
        LaunchTarget *target = &launchTargetFile->entries[i];
        if (strlen(target->fileName) != 0) 
            libraryLength++;
    }

    if (libraryLength > 0) {

        uint32_t tileLimit = 25;
        UiTile *tiles = calloc(tileLimit, sizeof(UiTile));
        assert(tiles);

        uint32_t tileCount = 0;

        for (uint32_t i = launchTargetFile->nEntries; i > 0; i--) {

            LaunchTarget *target = &launchTargetFile->entries[i];

            if (strlen(target->fileName) != 0) {

                tiles[tileCount].target = target;
                tiles[tileCount].next = &tiles[tileCount+1];
                if (tileCount != 0) 
                    tiles[tileCount].previous = &tiles[tileCount-1];

                tileCount++;
                if (tileCount >= tileLimit) break;
            }
        }

        if (tileCount > 0) {

            tiles[tileCount-1].next = &tiles[0];
            tiles[0].previous = &tiles[tileCount-1];

            mainUi->rows[mainUi->numRows].tiles = tiles; 
            mainUi->rows[mainUi->numRows].tileCursor = tiles;
            mainUi->rows[mainUi->numRows].name = "Recently Installed";
            mainUi->rows[mainUi->numRows].length = tileCount; 
            mainUi->numRows++;
        }
    }
    else { 
        printf("woah now looks like we have an empty library\n");
    }


    // __ROWS__ Essentials per platform 
    for (uint32_t iPlatform = 0; iPlatform < nPlatforms; iPlatform++) {

        uint32_t topRatedMax = 25;
        UiTile *tiles = calloc(topRatedMax, sizeof(UiTile));
        assert(tiles);

        uint32_t numTiles = 0;
        for (uint32_t i = 0; i < launchTargetFile->nEntries; i++) {

            LaunchTarget *target = &launchTargetFile->entries[i];

            if (strcmp(target->platform, platforms[iPlatform]) == 0) {

                tiles[numTiles].target = target; 
                tiles[numTiles].next = &tiles[numTiles+1]; 

                if (numTiles != 0) 
                    tiles[numTiles].previous = &tiles[numTiles-1];

                numTiles++;
            }

            if (numTiles >= topRatedMax) break;
        }

        if (numTiles > 0) {
            tiles[numTiles-1].next = &tiles[0];
            tiles[0].previous = &tiles[numTiles-1];

            mainUi->rows[mainUi->numRows].tiles = tiles;
            asprintf(&mainUi->rows[mainUi->numRows].name, "Essential %s", 
                    platformString(platforms[iPlatform]));

            mainUi->rows[mainUi->numRows].tileCursor = &mainUi->rows[mainUi->numRows].tiles[0];
            mainUi->rows[mainUi->numRows].length = numTiles;
            mainUi->numRows++;
        }
        else {
            printf("no games for platform!!!\n");
            free(tiles);
        }
    }


    for (uint32_t i = 0; i < mainUi->numRows; i++) {
        if (i == 0) {
            mainUi->rows[i].previousRow = &mainUi->rows[mainUi->numRows-1];
        }
        else {
            mainUi->rows[i].previousRow = &mainUi->rows[i-1];
        }

        if (i == mainUi->numRows - 1) {
            mainUi->rows[i].nextRow = &mainUi->rows[0];
        }
        else {
            mainUi->rows[i].nextRow = &mainUi->rows[i+1];
        }
    }

    mainUi->movingToTarget = mainUi->rowCursor->tileCursor->target;
    mainUi->movingToRow = mainUi->rowCursor;

    // Initialize the text to render
    offblast->mainUi.titleText = mainUi->movingToTarget->name;
    updateInfoText();
    updateDescriptionText();
    offblast->mainUi.rowNameText = offblast->mainUi.movingToRow->name;

    GradientLayer bottomGradient = {};
    updateRect(&bottomGradient.vertices, offblast->winWidth, 
            goldenRatioLargef(offblast->winHeight, 5));

    updateVbo(&bottomGradient.vbo, &bottomGradient.vertices);

    GradientLayer topGradient = {};
    updateRect(&topGradient.vertices, offblast->winWidth, 
            offblast->winHeight - offblast->winFold);

    updateVbo(&topGradient.vbo, &topGradient.vertices);
    GLint gradientOffsetYUniform = 
        glGetUniformLocation(gradientProgram, "yOffset");
    GLint gradientFlipYUniform = 
        glGetUniformLocation(gradientProgram, "yFlip");
    GLint gradientStartUniform = 
        glGetUniformLocation(gradientProgram, "startPos");


    // § Main loop
    while (running) {

        if (needsReRender(window) == 1) {
            printf("Window size changed, sizes updated.\n");
        }

        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                printf("shutting down\n");
                running = 0;
                break;
            }
            else if (event.type == SDL_CONTROLLERAXISMOTION) {
                printf("axis motion\n");
            }
            else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                SDL_ControllerButtonEvent *buttonEvent = 
                    (SDL_ControllerButtonEvent *) &event;

                // TODO should all players be able to control?

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
                        SDL_RaiseWindow(window);
                        break;
                }

            }
            else if (event.type == SDL_CONTROLLERBUTTONUP) {
                printf("button up\n");
            }
            else if (event.type == SDL_CONTROLLERDEVICEADDED) {

                SDL_ControllerDeviceEvent *devEvent = 
                    (SDL_ControllerDeviceEvent*)&event;

                printf("controller added %d\n", devEvent->which);
                SDL_GameController *controller;
                if (SDL_IsGameController(devEvent->which) == SDL_TRUE) {

                    controller = SDL_GameControllerOpen(devEvent->which); 
                    if (controller == NULL)  {
                        printf("failed to add %d\n", devEvent->which);
                    }
                    else {
                        offblast->mode = OFFBLAST_UI_MODE_PLAYER_SELECT;
                    }

                }
            }
            else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
                printf("controller removed\n");
            }
            else if (event.type == SDL_KEYUP) {
                SDL_KeyboardEvent *keyEvent = (SDL_KeyboardEvent*) &event;
                if (keyEvent->keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    printf("escape pressed, shutting down.\n");
                    running = 0;
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
            }

        }

        // § Player Detection
        // TODO should we do this on every loop?
        if (offblast->players[0].emailHash == 0) {
            offblast->mode = OFFBLAST_UI_MODE_PLAYER_SELECT;
            // TODO this should probably kill all the active animations?
            // or fire their callbacks immediately
        }
        else {
            offblast->mode = OFFBLAST_UI_MODE_MAIN;
        }

        // RENDER
        glClearColor(0.0, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);


        if (offblast->mode == OFFBLAST_UI_MODE_MAIN) {

            // Blocks
            UiRow *rowToRender = mainUi->rowCursor->previousRow;
            rowToRender = rowToRender->previousRow;

            // § blocks
            for (int32_t iRow = -2; iRow < ROWS_TOTAL-2; iRow++) {

                UiTile *tileToRender = 
                    rewindTiles(rowToRender->tileCursor, 2);

                for (int32_t iTile = -2; iTile < COLS_TOTAL; iTile++) {

                    float xOffset = 0;
                    float yOffset = 0;

                    xOffset = offblast->winMargin + iTile * 
                        (mainUi->boxWidth + mainUi->boxPad);

                    if (mainUi->horizontalAnimation->animating != 0 && iRow == 0) 
                    {
                        double change = easeInOutCirc(
                                (double)SDL_GetTicks() 
                                    - mainUi->horizontalAnimation->startTick,
                                0.0,
                                (double)mainUi->boxWidth + mainUi->boxPad,
                                (double)mainUi->horizontalAnimation->durationMs);

                        if (mainUi->horizontalAnimation->direction > 0) {
                            change = -change;
                        }

                        xOffset += change;
                    }

                    yOffset = (offblast->winFold - mainUi->boxHeight) + 
                        (iRow * (mainUi->boxHeight + mainUi->boxPad));

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

                        yOffset += change;

                    }

                    float xOffsetNormalized = (2.0 / offblast->winWidth) * xOffset;
                    float yOffsetNormalized = (2.0 / offblast->winHeight) * yOffset;

                    // Generate the texture 
                    glUseProgram(offblast->imageProgram);
                    if (tileToRender->textureHandle == 0 &&
                            tileToRender->target->coverUrl != NULL) 
                    {
                        if (tileToRender->loadState != LOAD_STATE_LOADED) {

                            char *coverArtPath =
                                getCoverPath(
                                        tileToRender->target->targetSignature); 

                            int w, h, n;
                            stbi_set_flip_vertically_on_load(1);
                            unsigned char *image = stbi_load(
                                    coverArtPath, &w, &h, &n, 4);
                            free(coverArtPath);


                            if (tileToRender->loadState == LOAD_STATE_UNKNOWN) {
                                
                                if(image == NULL) {
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
                            
                            if (tileToRender->loadState == 
                                    LOAD_STATE_DOWNLOADED) 
                            {

                                glGenTextures(1, &tileToRender->textureHandle);

                                imageToGlTexture(
                                        tileToRender->textureHandle,
                                        image, w, h);

                                printf("texture size: %ux%u\n", w, h);

                                tileToRender->textureW = w;
                                tileToRender->textureH = h;

                                glBindTexture(GL_TEXTURE_2D, 
                                        tileToRender->textureHandle);

                                tileToRender->loadState = LOAD_STATE_LOADED;
                            }

                            free(image);
                        }
                    }
                    else {
                        glBindTexture(GL_TEXTURE_2D, 
                                tileToRender->textureHandle);
                    }

                    // ACTUAL DRAW
                    glBindBuffer(GL_ARRAY_BUFFER, mainUi->blockVbo);
                    glEnableVertexAttribArray(0);
                    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 
                            6*sizeof(float), 0);
                    glEnableVertexAttribArray(1);
                    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 
                            6*sizeof(float), (void*)(4*sizeof(float)));

                    glUniform2f(offblast->imageTranslateUni, xOffsetNormalized, 
                            yOffsetNormalized);

                    glUniform1f(offblast->textAlphaUni, 1.0);

                    // TODO texture size stuff
                    glUniform2f(offblast->imageTexturePosUni, 
                            0.0, //tileToRender->textureMaxW, 
                            0.0 /*tileToRender->textureMinH*/);

                    glDrawArrays(GL_TRIANGLES, 0, 6);

                    tileToRender = tileToRender->next;
                }

                rowToRender = rowToRender->nextRow;
            }

            glUniform2f(offblast->imageTranslateUni, 0.0f, 0.0f);
            glUniform1f(offblast->imageAlphaUni, 1.0);
            glUniform2f(offblast->imageTexturePosUni, 0.0f, 0.0f);

            // § GRADIENT LAYERS
            glUseProgram(gradientProgram);
            glUniform1f(gradientOffsetYUniform, 
                    2.0/offblast->winHeight * offblast->winFold);
            glUniform1f(gradientFlipYUniform, 0.0);
            glUniform1f(gradientStartUniform, 1.0);
            glBindBuffer(GL_ARRAY_BUFFER, topGradient.vbo);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 
                    6*sizeof(float), 0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 
                    6*sizeof(float), (void*)(4*sizeof(float)));
            glDrawArrays(GL_TRIANGLES, 0, 6);

            glUseProgram(gradientProgram);
            glUniform1f(gradientOffsetYUniform, 0.0);
            glUniform1f(gradientFlipYUniform, 1.0);
            glUniform1f(gradientStartUniform, 0.1);
            glBindBuffer(GL_ARRAY_BUFFER, bottomGradient.vbo);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 
                    6*sizeof(float), 0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 
                    6*sizeof(float), (void*)(4*sizeof(float)));
            glDrawArrays(GL_TRIANGLES, 0, 6);


            // § INFO AREA
            float alpha = 1.0;
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

            renderSomeText(offblast, offblast->winMargin, pixelY, 
                OFFBLAST_TEXT_TITLE, alpha, 0, mainUi->titleText);


            pixelY -= offblast->infoPointSize * 1.4;
            renderSomeText(offblast, offblast->winMargin, pixelY, 
                OFFBLAST_TEXT_INFO, alpha, 0, mainUi->infoText);


            pixelY -= offblast->infoPointSize + mainUi->boxPad;
            renderSomeText(offblast, offblast->winMargin, pixelY, 
                OFFBLAST_TEXT_INFO, alpha, mainUi->descriptionWidth, 
                mainUi->descriptionText); 


            pixelY = offblast->winFold + mainUi->boxPad;
            renderSomeText(offblast, offblast->winMargin, pixelY, 
                OFFBLAST_TEXT_INFO, rowNameAlpha, 0, mainUi->rowNameText); 


        }
        else if (offblast->mode == OFFBLAST_UI_MODE_PLAYER_SELECT) {


            // TODO cache all these golden ratio calls they are expensive 
            // to calculate
            renderSomeText(offblast, 
                    300, 
                    offblast->winHeight - 
                        goldenRatioLarge(offblast->winHeight, 3), 
                    OFFBLAST_TEXT_TITLE, 1.0, 0,
                    "Who's playing?");

            for (uint32_t i = 0; i < offblast->nUsers; i++) {

                ImageLayer *avatarLayer = 
                    &playerSelectUi->playerAvatarLayers[i];

                float alpha = (i == playerSelectUi->cursor) ? 1.0 : 0.7;
                renderSomeText(offblast, 
                        (0.128f * offblast->winWidth) + 
                            (i * offblast->winWidth * 0.15), 
                        480,
                        OFFBLAST_TEXT_INFO, alpha, 0,
                        offblast->users[i].name);

                renderImageLayer(avatarLayer, 
                        0.25f + i*0.3, 
                        0.5f, alpha);
            }
    
            // need to handle the event
        }

        uint32_t frameTime = SDL_GetTicks() - lastTick;
        char *fpsString;
        asprintf(&fpsString, "frame time: %u", frameTime);
        renderSomeText(offblast, 15, 15, OFFBLAST_TEXT_DEBUG, 1.0, 0, 
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

        // 7:5
        mainUi->boxHeight = goldenRatioLarge(offblast->winWidth, 4);
        mainUi->boxWidth = mainUi->boxHeight/5 * 7;
        updateRect(&mainUi->blockVertices, mainUi->boxWidth, mainUi->boxHeight);
        updateVbo(&mainUi->blockVbo, &mainUi->blockVertices);

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
}

void changeRow(uint32_t direction) 
{
    MainUi *ui = &offblast->mainUi;

    if (animationRunning() == 0)
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
    if (ui->horizontalAnimation->direction == 1) {
        ui->rowCursor->tileCursor = 
            ui->rowCursor->tileCursor->next;
    }
    else {
        ui->rowCursor->tileCursor = 
            ui->rowCursor->tileCursor->previous;
    }
}

void verticalMoveDone() {
    MainUi *ui = &offblast->mainUi;
    if (ui->verticalAnimation->direction == 1) {
        ui->rowCursor = 
            ui->rowCursor->nextRow;
    }
    else {
        ui->rowCursor = 
            ui->rowCursor->previousRow;
    }
}

void infoFaded() {

    MainUi *ui = &offblast->mainUi;
    if (ui->infoAnimation->direction == 0) {

        offblast->mainUi.titleText = 
            offblast->mainUi.movingToTarget->name;
        updateInfoText();
        updateDescriptionText();
        offblast->mainUi.rowNameText = offblast->mainUi.movingToRow->name;

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

        curl_easy_cleanup(curl);
        fclose(fd);

        if (res != CURLE_OK) {
            printf("%s\n", curl_easy_strerror(res));
        }
        else {
            tileToRender->loadState = LOAD_STATE_DOWNLOADED;
        }
    }

    return NULL;
}


void updateRect(UiRect *vertices, uint32_t rectWidth, uint32_t rectHeight) 
{
    float winWidth = (float)offblast->winWidth;
    float winHeight = (float)offblast->winHeight;

    float left = -1.0;
    float right = left + (2/winWidth*rectWidth);
    float bottom = -1.0;
    float top = bottom + (2/winHeight*rectHeight);


    (*vertices)[0][0] = left;
    (*vertices)[0][1] = bottom;
    (*vertices)[0][2] = 0.0f;
    (*vertices)[0][3] = 1.0f;
    (*vertices)[0][4] = 0.0f;
    (*vertices)[0][5] = 0.0f;

    (*vertices)[1][0] = left;
    (*vertices)[1][1] = top;
    (*vertices)[1][2] = 0.0f;
    (*vertices)[1][3] = 1.0f;
    (*vertices)[1][4] = 0.0f;
    (*vertices)[1][5] = 1.0f;

    (*vertices)[2][0] = right;
    (*vertices)[2][1] = top;
    (*vertices)[2][2] = 0.0f;
    (*vertices)[2][3] = 1.0f;
    (*vertices)[2][4] = 1.0f;
    (*vertices)[2][5] = 1.0f;

    (*vertices)[3][0] = right;
    (*vertices)[3][1] = top;
    (*vertices)[3][2] = 0.0f;
    (*vertices)[3][3] = 1.0f;
    (*vertices)[3][4] = 1.0f;
    (*vertices)[3][5] = 1.0f;

    (*vertices)[4][0] = right;
    (*vertices)[4][1] = bottom;
    (*vertices)[4][2] = 0.0f;
    (*vertices)[4][3] = 1.0f;
    (*vertices)[4][4] = 1.0f;
    (*vertices)[4][5] = 0.0f;

    (*vertices)[5][0] = left;
    (*vertices)[5][1] = bottom;
    (*vertices)[5][2] = 0.0f;
    (*vertices)[5][3] = 1.0f;
    (*vertices)[5][4] = 0.0f;
    (*vertices)[5][5] = 0.0f;

}

uint32_t powTwoFloor(uint32_t val) {
    uint32_t pow = 2;
    while (val > pow)
        pow *= 2;

    return pow;
}

void imageToGlTexture(GLuint textureHandle, unsigned char *pixelData, 
        uint32_t newWidth, uint32_t newHeight) 
{

    //*newWidth = powTwoFloor(surface->w);
    //*newHeight = powTwoFloor(surface->h);

    /*SDL_Surface *newSurface = SDL_CreateRGBSurface(
            0, *newWidth, *newHeight, 32,
            0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
            */

    //SDL_Rect destRect = {0, *newHeight - surface->h, 0, 0};
    //SDL_BlitSurface(surface, NULL, newSurface, &destRect);

    glBindTexture(GL_TEXTURE_2D, textureHandle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, newWidth, newHeight,
            0, GL_RGBA, GL_UNSIGNED_BYTE, pixelData);
    glBindTexture(GL_TEXTURE_2D, 0);

    //free(newSurface);
}

void updateVbo(GLuint *vbo, UiRect* vertices) {

        if (*vbo == 0) {
            glGenBuffers(1, vbo);
            glBindBuffer(GL_ARRAY_BUFFER, *vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(UiRect), 
                    vertices, GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        else {
            glBindBuffer(GL_ARRAY_BUFFER, *vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(UiRect), 
                    vertices);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
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
    
    LaunchTarget *target = offblast->mainUi.rowCursor->tileCursor->target;

    if (strlen(target->path) == 0 || 
            strlen(target->fileName) == 0) 
    {
        printf("%s has no launch candidate\n", target->name);
    }
    else {

        char *romSlug;
        asprintf(&romSlug, "%s/%s", (char*) &target->path, 
                (char*)&target->fileName);

        char *launchString = calloc(PATH_MAX, sizeof(char));

        int32_t foundIndex = -1;
        for (uint32_t i = 0; i < offblast->nPaths; i++) {
            if (strcmp(target->path, offblast->launchers[i].path) == 0) {
                foundIndex = i;
            }
        }

        if (foundIndex == -1) {
            printf("%s has no launcher\n", target->name);
            return;
        }

        memcpy(launchString, 
                offblast->launchers[foundIndex].launcher, 
                strlen(offblast->launchers[foundIndex].launcher));

        assert(strlen(launchString));

        char *p;
        uint8_t replaceIter = 0, replaceLimit = 8;
        while ((p = strstr(launchString, "%ROM%"))) {

            memmove(
                    p + strlen(romSlug) + 2, 
                    p + 5,
                    strlen(p));

            *p = '"';
            memcpy(p+1, romSlug, strlen(romSlug));
            *(p + 1 + strlen(romSlug)) = '"';

            replaceIter++;
            if (replaceIter >= replaceLimit) {
                printf("rom replace iterations exceeded, breaking\n");
                break;
            }
        }

        printf("OFFBLAST! %s\n", launchString);
        system(launchString);

        free(romSlug);
        free(launchString);
    }
}

void renderImageLayer(ImageLayer *layer, float x, float y, float a) {

    glUseProgram(offblast->imageProgram);
    assert(layer->textureValid);

    glBindTexture(GL_TEXTURE_2D, layer->textureHandle);
    glBindBuffer(GL_ARRAY_BUFFER, layer->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 6*sizeof(float), 0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6*sizeof(float), 
            (void*)(4*sizeof(float)));
    glUniform2f(offblast->imageTranslateUni, x, y);
    glUniform1f(offblast->imageAlphaUni, a);
    glDrawArrays(GL_TRIANGLES, 0, 6);

}

void pressConfirm(int32_t joystickIndex) {

    if (offblast->mode == OFFBLAST_UI_MODE_PLAYER_SELECT) {

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

        for (uint32_t k = 0; k < OFFBLAST_MAX_PLAYERS; k++) {

            if (offblast->players[k].emailHash == 0) {
                offblast->players[k].emailHash = emailSignature;
            }

            if (joystickIndex > -1) {
                offblast->players[k].jsIndex = joystickIndex;
                printf("Controller: %s\nAdded to Player %d\n",
                        SDL_GameControllerNameForIndex(joystickIndex), k);
                break;
            }
        }

    }
    else if (offblast->mode == OFFBLAST_UI_MODE_MAIN) {
        launch();
    }
}

void updateInfoText() {

    if (offblast->mainUi.infoText != NULL) {
        free(offblast->mainUi.infoText);
    }

    char *infoString;
    asprintf(&infoString, "%.4s  |  %s  |  %u%%", 
            offblast->mainUi.movingToTarget->date, 
            platformString(offblast->mainUi.movingToTarget->platform),
            offblast->mainUi.movingToTarget->ranking);

    offblast->mainUi.infoText = infoString;
}

void updateDescriptionText() {
    OffblastBlob *descriptionBlob = 
    (OffblastBlob*) &offblast->descriptionFile->memory[
       offblast->mainUi.movingToTarget->descriptionOffset];

    offblast->mainUi.descriptionText = descriptionBlob->content;
}
