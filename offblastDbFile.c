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
#define BLOB_GROW_SIZE 1048576

int InitDbFile(char *path, OffblastDbFile *dbFileStruct, 
        size_t itemSize) 
{

    int fd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    struct stat sb;

    if (fstat(fd, &sb) == -1) {
        perror("could not open db file\n");
        return 0;
    }

    size_t initialBytesToAllocate = itemSize * ((size_t) ITEM_BUFFER_NUM);
    if (sb.st_size == 0) {
        if (fallocate(fd, 0, 0, initialBytesToAllocate) == -1) 
        {
            printf("couldn't allocate space for the db %s\n", path);
            perror("error :");
        }
        else {
            sb.st_size = initialBytesToAllocate;
        }
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


void* growDbFileIfNecessary(OffblastDbFile* dbFileStruct, size_t itemSize, enum OffBlastDbType type) 
{

    uint32_t willOverflow = 0;
    size_t newSize = 0;

    if (type == OFFBLAST_DB_TYPE_FIXED) {

        OffblastDbFileFormat *dbFileActual = 
            (OffblastDbFileFormat*)dbFileStruct->memory;

        willOverflow = dbFileActual->nEntries * itemSize + itemSize >= 
            dbFileStruct->nBytesAllocated;
        
        newSize = dbFileStruct->nBytesAllocated + 
            itemSize * (size_t) ITEM_BUFFER_NUM;

    }
    else if(type == OFFBLAST_DB_TYPE_BLOB) {

        OffblastBlobFile *dbFileActual = 
            (OffblastBlobFile*)dbFileStruct->memory;

        willOverflow = ((size_t)dbFileActual->cursor) + itemSize >= 
            dbFileStruct->nBytesAllocated;

        newSize = dbFileStruct->nBytesAllocated + 
            BLOB_GROW_SIZE;
    }
    else {
        printf("wtf kind of db file are you using then?\n");
        return NULL;
    }


    if (willOverflow) 
    {

        printf("DB FILE FULL, GROWING!\n");

        if(ftruncate(dbFileStruct->fd, newSize) == -1) {
            return NULL;
        }
        else {

    struct stat sb;

    if (fstat(dbFileStruct->fd, &sb) == -1) {
        perror("could not open db file\n");
        return NULL;
    }

        printf("truncated block size %lu!\n", sb.st_size);
        printf("newSize we were using %lu!\n", newSize);

            void *memory = mremap(
                    dbFileStruct->memory,
                    dbFileStruct->nBytesAllocated,
                    sb.st_size,
                    MREMAP_MAYMOVE
                    );

            if (memory == MAP_FAILED) {
                perror("couldn't re-map memory for file\n");
                return NULL;
            }

            dbFileStruct->memory = memory;
            dbFileStruct->nBytesAllocated = newSize;
        }
    }

    return dbFileStruct->memory;
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
