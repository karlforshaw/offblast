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
    uint32_t contentsHash; // NOT DONE

    char type[256];
    char platform[256];
    char extension[32];
    char cmd[MAX_LAUNCH_COMMAND_LENGTH];

    char cemuPath[PATH_MAX];
    char romPath[PATH_MAX];
} Launcher;

typedef struct LauncherFile {
    uint32_t nEntries;
    Launcher entries[];
} LauncherFile;

#define OFFBLAST_NAME_MAX 256
typedef struct LaunchTarget {
    uint32_t targetSignature;
    uint32_t romSignature;

    char name[OFFBLAST_NAME_MAX];
    char date[10];
    uint32_t ranking;

    //char fileName[256];
    char path[PATH_MAX];
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
    uint32_t targetSignature;
    size_t length;
    char content[];
} OffblastBlob;

typedef struct PlayTime {
    uint32_t targetSignature;
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
        uint32_t targetSignature);

int32_t launchTargetIndexByRomSignature(LaunchTargetFile *file, 
        uint32_t targetSignature);

int32_t launchTargetIndexByNameMatch(LaunchTargetFile *file, 
        char *search);
