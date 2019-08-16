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

typedef struct LaunchTarget {
    uint32_t targetSignature;
    uint32_t romSignature;
    char name[256];
    char fileName[256];
    char path[PATH_MAX];
    char platform[256];
} LaunchTarget;

typedef struct LaunchTargetFile {
    uint32_t nEntries;
    LaunchTarget entries[];
} LaunchTargetFile;


int init_db_file(char *, OffblastDbFile *dbFileStruct, 
        size_t itemSize);

// TODO implement
int num_remaining_items(OffblastDbFile *dbFileStruct,
        size_t itemSize);

int find_index_of_slow(
        uint32_t signature,
        uint32_t numEntries, 
        size_t entrySize, 
        char* list);
