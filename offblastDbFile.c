#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

int find_index_of_slow(
        uint32_t signature,
        uint32_t numEntries, 
        size_t entrySize, 
        char* list) 
{
    int foundIndex = -1;

    for(uint32_t i=0; i < numEntries; i++) {

        uint32_t currentEntry = *((uint32_t*) (list+i*entrySize));

        if (currentEntry == signature) {
            foundIndex = i;
            break;
        }
    }

    return foundIndex;
}
