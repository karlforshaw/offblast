#include <stdint.h>

typedef struct OffblastDbFileCommon {
    uint32_t nEntries;
    char entries[];
} OffblastDbFileCommon;

FILE *initialize_db_file(
        char *path,
        void **dest,
        uint32_t itemSize,
        uint32_t *nItems);
