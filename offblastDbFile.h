#include <stdint.h>
#include <linux/limits.h>

enum OffBlastDbType {
    OFFBLAST_DB_TYPE_FIXED = 1,
    OFFBLAST_DB_TYPE_BLOB = 2,
};

typedef struct OffblastDbFileFormat {
    uint32_t nEntries;
    void *entries;
} OffblastDbFileFormat;

typedef struct OffblastDbFile {
    int fd;
    size_t nBytesAllocated;
    OffblastDbFileFormat *memory;
} OffblastDbFile;

#define LAUNCHER_RETROARCH 1;
#define LAUNCHER_CUSTOM 99;
#define MAX_LAUNCH_COMMAND_LENGTH 512
typedef struct Launcher {

    uint32_t signature;

    char type[256];
    char name[64];
    char platform[256];
    char extension[32];
    char cmd[MAX_LAUNCH_COMMAND_LENGTH];
    char romPath[PATH_MAX];
    char scanPattern[256];  // Optional: pattern like "*/vol/code/*.rpx"
} Launcher;

typedef struct LauncherFile {
    uint32_t nEntries;
    Launcher entries[];
} LauncherFile;

#define OFFBLAST_NAME_MAX 256
typedef struct LaunchTarget {
    uint64_t targetSignature;

    char id[OFFBLAST_NAME_MAX];
    char name[OFFBLAST_NAME_MAX];
    char date[10];
    uint32_t ranking;

    char path[PATH_MAX];
    float matchScore;
    char platform[256];
    uint32_t launcherSignature;

    char coverUrl[PATH_MAX];
    off_t descriptionOffset;
} LaunchTarget;

typedef struct LaunchTargetFile {
    uint32_t nEntries;
    LaunchTarget entries[];
} LaunchTargetFile;

typedef struct OffblastBlobFile {
    off_t cursor;
    char memory[];
} OffblastBlobFile;

typedef struct OffblastBlob {
    uint64_t targetSignature;
    size_t length;
    char content[];
} OffblastBlob;

typedef struct PlayTime {
    uint64_t targetSignature;
    uint32_t msPlayed;
    uint32_t lastPlayed;
} PlayTime;

typedef struct PlayTimeFile {
    uint32_t nEntries;
    PlayTime entries[];
} PlayTimeFile;


int InitDbFile(char *, OffblastDbFile *dbFileStruct, 
        size_t itemSize);

void *growDbFileIfNecessary(OffblastDbFile* dbFileStruct,
        size_t itemSize,
        enum OffBlastDbType type);

int32_t launchTargetIndexByTargetSignature(LaunchTargetFile *file, 
        uint64_t targetSignature);

int32_t launchTargetIndexByNameMatch(LaunchTargetFile *file, 
        char *search, char *platform, float *matchScore);

int32_t launchTargetIndexByIdMatch(LaunchTargetFile *file, 
        char *idStr, char *platform);
