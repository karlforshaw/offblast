#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#include "offblast.h"
#include "offblastDbFile.h"

#define ITEM_BUFFER_NUM 1000ul

int init_db_file(char *path, OffblastDbFile *dbFileStruct, 
        size_t itemSize) 
{

    int fd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    struct stat sb;

    if (fstat(fd, &sb) == -1) {
        perror("could not open db file\n");
        return 0;
    }

    size_t initialBytesToAllocate = itemSize * ((size_t) ITEM_BUFFER_NUM);
    if (sb.st_size == 0 && 
            fallocate(fd, 0, 0, initialBytesToAllocate) == -1) 
    {
        printf("couldn't allocate space for the db %s\n", path);
        perror("error :");
    }
    else {
        sb.st_size = initialBytesToAllocate;
    }

    printf("allocating %lu for %s\n", sb.st_size, path);
    dbFileStruct->fd = fd;
    void *memory = mmap(
            NULL, 
            sb.st_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd,
            0);

    if (memory == MAP_FAILED) {
        perror("couldn't map memory for file\n");
        return 0;
    }

    dbFileStruct->memory = memory;
    dbFileStruct->nBytesAllocated = sb.st_size;

    return 1;
}

int32_t launchTargetIndexByTargetSignature(LaunchTargetFile *file, 
        uint32_t targetSignature) 
{
    uint32_t foundIndex = -1;
    for (uint32_t i = 0; i < file->nEntries; i++) {
        if (file->entries[i].targetSignature == 
                targetSignature) {
            foundIndex = i;
        }
    }
    return foundIndex;
}

int32_t launchTargetIndexByRomSignature(LaunchTargetFile *file, 
        uint32_t romSignature) 
{
    uint32_t foundIndex = -1;
    for (uint32_t i = 0; i < file->nEntries; i++) {
        if (file->entries[i].romSignature == 
                romSignature) {
            foundIndex = i;
        }
    }
    return foundIndex;
}

int32_t launchTargetIndexByNameMatch(LaunchTargetFile *file, 
        char *search)
{
    uint32_t foundIndex = -1;
    char *bestMatch = NULL;

    printf("\nLooking for: %s\n--------------\n", search);

    for (uint32_t i = 0; i < file->nEntries; i++) {
        char *result = strstr(search, file->entries[i].name);

        if (result != NULL) {
            printf("Match: %s\n", 
                    file->entries[i].name);

            if (bestMatch == NULL || strlen(result) > strlen(bestMatch)){ 
                foundIndex = i;
                bestMatch = file->entries[i].name;
            }
        }
    }

    if (foundIndex == -1) {
        printf("NOT FOUND\n\n");
    }
    else {
        printf("best match: %s - index %u\n\n", bestMatch, foundIndex);
    }

    return foundIndex;
}
