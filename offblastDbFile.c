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
        uint64_t targetSignature) 
{
    int32_t foundIndex = -1;
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
    int32_t foundIndex = -1;
    for (uint32_t i = 0; i < file->nEntries; i++) {
        if (file->entries[i].romSignature == 
                romSignature) {
            foundIndex = i;
        }
    }
    return foundIndex;
}


int32_t launchTargetIndexByNameMatch(LaunchTargetFile *file, char *searchString) {

    int32_t bestIndex = -1;
    float bestScore = 0;


    printf("\nLooking for: %s\n--------------\n", searchString);

    for (uint32_t i = 0; i < file->nEntries; i++) {

        // File name needle match
        char *workingCopy = strdup(searchString);
        uint32_t tokensMatched = 0;
        uint32_t numTokens = 0;
        float score;

        char *token = token = strtok(workingCopy, " ");
        while(token != NULL) {
            numTokens++;
            if ((strstr(file->entries[i].name, token) != NULL)) {
                //printf("Pass 1; Token Match: %s\n", token);
                tokensMatched++;
            }
            token = strtok(NULL, " ");
        }
        free(workingCopy);

        // Entry name needle match
        workingCopy = strdup(file->entries[i].name);
        token = token = strtok(workingCopy, " ");
        while(token != NULL) {
            numTokens++;
            if ((strstr(searchString, token) != NULL)) {
                //printf("Pass2: Token Match: %s\n", token);
                tokensMatched++;
            }
            token = strtok(NULL, " ");
        }
        free(workingCopy);

        if (tokensMatched >= 1) {
            score = (float)tokensMatched/numTokens;
            printf("%s\t%s\n", searchString, file->entries[i].name);
            printf("Matched %u/%u for a score of %f\n", 
                    tokensMatched, numTokens, score);

            if (score == bestScore) {
                //printf("\n--CONFLICT not sure which is best, "
                //        "doing nothing!--\n");
            }
            else if (score > bestScore) {
                bestScore = score;
                bestIndex = i;
            }
        }
    }

    if (bestIndex == -1) {
        printf("NOT FOUND\n\n");
    }
    else {
        printf("best match: %s - index %u\n\n",
                file->entries[bestIndex].name, bestIndex);
    }

    return bestIndex;
}
