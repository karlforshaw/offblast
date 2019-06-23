#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "offblast.h"
#include "offblastDbFile.h"


FILE *initialize_db_file(
        char *path,
        void **dest,
        uint32_t itemSize,
        uint32_t *nItems) 
{

    FILE *fd = fopen(path, "r+b");
    if (!fd) {
        if (errno == ENOENT) {
            fd = fopen(path, "w+b");
            if (!fd) {
                printf("ERROR trying to open %s\n", path);
                perror(NULL);
                return NULL;
            }
        }
        else {
            printf("ERROR trying to open %s\n", path);
            perror(NULL);
            return NULL;
        }
    }


    OffblastDbFileCommon *fileHeader = 
        calloc(1, FIELD_SIZEOF(OffblastDbFileCommon, nEntries)); 

    if(fread(fileHeader,
                FIELD_SIZEOF(OffblastDbFileCommon, nEntries),
                1, fd)) 
    {
        *nItems = fileHeader->nEntries;
        printf("there are %u items in %s\n", 
                *nItems,
                path);

        size_t tmpNewSize = 
            FIELD_SIZEOF(OffblastDbFileCommon, nEntries) + 
            (*nItems * itemSize);

        *dest = calloc(1, tmpNewSize);
        if (!dest) {
            printf("cannot allocate enough ram for %s contents\n", path);
            free(fileHeader);
            fclose(fd);
            return NULL;
        }
        else {
            uint32_t itemsRead = 0;
            OffblastDbFileCommon *recast = 
                (OffblastDbFileCommon*) *dest;

            recast->nEntries = *nItems;
            itemsRead = fread(&recast->entries, 
                    itemSize, *nItems, fd); 

            if (itemsRead < *nItems) {
                printf("possible corruption in %s", path);
                fclose(fd);
                free(fileHeader);
                free(dest);
                dest = NULL;
                return NULL;
            }

        }
    }
    else {
        printf("%s is empty\n", path);
    }

    free(fileHeader);
    return fd;
}
