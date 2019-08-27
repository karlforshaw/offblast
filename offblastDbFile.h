#include <stdint.h>
#include <linux/limits.h>

typedef struct OffblastDbFileFormat {
    uint32_t nEntries;
    void *entries;
} OffblastDbFileFormat;

typedef struct OffblastDbFile {
    int fd;
    size_t nBytesAllocated;
    OffblastDbFileFormat *memory;
} OffblastDbFile;

typedef struct PathInfo {
    uint32_t signature;
    uint32_t contentsHash;
} PathInfo;

typedef struct PathInfoFile {
    uint32_t nEntries;
    PathInfo entries[];
} PathInfoFile;

#define OFFBLAST_NAME_MAX 256
typedef struct LaunchTarget {
    uint32_t targetSignature;
    uint32_t romSignature;

    char name[OFFBLAST_NAME_MAX];
    char date[10];
    uint32_t ranking;

    char fileName[256];
    char path[PATH_MAX];
    char platform[256];

    char coverPath[PATH_MAX];
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


int init_db_file(char *, OffblastDbFile *dbFileStruct, 
        size_t itemSize);

// TODO implement
int num_remaining_items(OffblastDbFile *dbFileStruct,
        size_t itemSize);

int32_t launchTargetIndexByTargetSignature(LaunchTargetFile *file, 
        uint32_t targetSignature);

int32_t launchTargetIndexByRomSignature(LaunchTargetFile *file, 
        uint32_t targetSignature);

int32_t launchTargetIndexByNameMatch(LaunchTargetFile *file, 
        char *search);
