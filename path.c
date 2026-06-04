#include "SFAT.h"
#include <string.h>
#include <stdlib.h>

/* 引入 dir.c 中的辅助函数，用于获取目录容量 */
extern int getDirCapacity(unsigned int cluster);

int parsePathSegment(const char *path, int *offset, char *segment, size_t segmentSize) {
    if (!path || !offset || !segment || segmentSize == 0) return -1;

    int i = *offset;
    while (path[i] == '/') i++;

    if (path[i] == '\0') {
        *offset = i;
        return 0;
    }

    int start = i;
    while (path[i] != '\0' && path[i] != '/') i++;

    int len = i - start;
    if (len >= (int)segmentSize) len = (int)segmentSize - 1;

    strncpy(segment, path + start, len);
    segment[len] = '\0';

    *offset = i;
    return len;
}

int resolvePath(const char *path,
                Directory *baseDir,
                unsigned int baseCluster,
                int baseIndex,
                Directory **outDir,
                unsigned int *outCluster,
                int *outIsRoot,
                int *outNeedsFree)
{
    if (!path || !outDir || !outCluster || !outIsRoot || !outNeedsFree)
        return -1;

    *outNeedsFree = 0;

    Directory *currentDir;
    unsigned int currentCluster;

    if (path[0] == '/') {
        currentDir = &sfat.rootDirectory;
        currentCluster = ROOT_DIR_START_CLUSTER;
    } else {
        currentDir = baseDir;
        currentCluster = baseCluster;
    }

    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        *outDir = currentDir;
        *outCluster = currentCluster;
        *outIsRoot = (currentCluster == ROOT_DIR_START_CLUSTER);
        return 0;
    }

    char pathBuf[256];
    strncpy(pathBuf, path, sizeof(pathBuf)-1);
    pathBuf[255] = '\0';

    char segment[MAX_FILENAME_LENGTH + 1];
    int offset = 0;

    while (1) {
        int segLen = parsePathSegment(pathBuf, &offset, segment, sizeof(segment));
        if (segLen < 0) break;
        if (segLen == 0) {
            if (pathBuf[offset] == '\0') break;
            continue;
        }

        if (strcmp(segment, ".") == 0)
            continue;

        if (strcmp(segment, "..") == 0) {
            if (currentCluster == ROOT_DIR_START_CLUSTER)
                continue;

            unsigned int parentCluster = currentDir->entries[1].startCluster;

            if (*outNeedsFree && currentDir != &sfat.rootDirectory) {
                free(currentDir->entries);
                free(currentDir);
            }

            currentDir = dirFromDisk(parentCluster);
            if (!currentDir) return -1;

            currentCluster = parentCluster;
            *outNeedsFree = 1;
            continue;
        }

        int capacity = getDirCapacity(currentCluster);
        if (capacity <= 0) return -1;

        int found = 0;
        for (int i = 0; i < capacity; i++) {
            DirEntry *entry = &currentDir->entries[i];
            unsigned char first = (unsigned char)entry->name[0];

            if (first == UNUSED || first == DELETED)
                continue;

            if (strncmp(entry->name, segment, MAX_FILENAME_LENGTH) == 0) {
                if (entry->type != SUBDIR)
                    return -1;

                unsigned int nextCluster = entry->startCluster;

                if (*outNeedsFree && currentDir != &sfat.rootDirectory) {
                    free(currentDir->entries);
                    free(currentDir);
                }

                currentCluster = nextCluster;
                currentDir = dirFromDisk(currentCluster);
                if (!currentDir) return -1;

                *outNeedsFree = 1;
                found = 1;
                break;
            }
        }

        if (!found)
            return -1;
    }

    *outDir = currentDir;
    *outCluster = currentCluster;
    *outIsRoot = (currentCluster == ROOT_DIR_START_CLUSTER);

    return 0;
}