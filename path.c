#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include"SFAT.h"

// 从 path 中解析下一个路径片段，返回 1 表示解析到有效片段
int parsePathSegment(const char *path, int *offset, char *segment, size_t segmentSize) {
    int i = *offset;
    int j = 0;
    while (path[i] == '/' || path[i] == '\\') {
        i++;
    }
    if (path[i] == '\0') {
        return 0;
    }
    while (path[i] != '\0' && path[i] != '/' && path[i] != '\\') {
        if (j < (int)segmentSize - 1) {
            segment[j++] = path[i];
        }
        i++;
    }
    segment[j] = '\0';
    *offset = i;
    return 1;
}

// 解析路径并定位最终目录，必要时从磁盘加载目录结构
int resolvePath(const char *path, Directory *baseDir, unsigned int baseCluster, int baseIndex,
                Directory **outDir, unsigned int *outCluster, int *outIsRoot, int *outNeedsFree) {
    Directory *currentDir = baseDir;
    unsigned int currentCluster = baseCluster;
    unsigned int clusterStack[MAX_STACK_DEPTH];
    int depth = baseIndex;
    int offset = 0;
    int currentAllocated = 0;
    int localIsRoot = (baseIndex == 0);
    int usesStack = 1;
    char segment[MAX_FILENAME_LENGTH];

    if (path == NULL || path[0] == '\0') {
        *outDir = currentDir;
        *outCluster = currentCluster;
        *outIsRoot = localIsRoot;
        *outNeedsFree = 0;
        return 1;
    }

    // 绝对路径从根目录开始
    if (path[0] == '/' || path[0] == '\\') {
        currentDir = &sfat.rootDirectory;
        currentCluster = ROOT_DIR_START_CLUSTER;
        clusterStack[0] = ROOT_DIR_START_CLUSTER;
        depth = 0;
        localIsRoot = 1;
        usesStack = 0;
    } else {
        for (int i = 0; i <= baseIndex; i++) {
            clusterStack[i] = dirClusterStack[i];
        }
    }

    // 逐段推进目录
    while (parsePathSegment(path, &offset, segment, sizeof(segment))) {
        if (strcmp(segment, ".") == 0) {
            continue;
        }
        if (strcmp(segment, "..") == 0) {
            if (depth > 0) {
                if (currentAllocated) {
                    free(currentDir->entries);
                    free(currentDir);
                    currentAllocated = 0;
                }
                depth--;
                currentCluster = clusterStack[depth];
                if (usesStack && depth <= cdi) {
                    currentDir = &sfat.dirStack[depth];
                } else if (depth == 0) {
                    currentDir = &sfat.rootDirectory;
                } else {
                    currentDir = dirFromDisk(currentCluster);
                    currentAllocated = 1;
                }
                localIsRoot = (depth == 0);
            }
            continue;
        }

        // 线性查找子目录名
        int index = -1;
        for (int i = 0; i < currentDir->count; i++) {
            if (strncmp(currentDir->entries[i].name, segment, MAX_FILENAME_LENGTH) == 0) {
                index = i;
                break;
            }
        }
        if (index < 0 || currentDir->entries[index].type != SUBDIR) {
            if (currentAllocated) {
                free(currentDir->entries);
                free(currentDir);
            }
            return 0;
        }

        unsigned int nextCluster = currentDir->entries[index].startCluster;
        if (currentAllocated) {
            free(currentDir->entries);
            free(currentDir);
            currentAllocated = 0;
        }
        if (depth + 1 >= MAX_STACK_DEPTH) {
            return 0;
        }
        depth++;
        clusterStack[depth] = nextCluster;
        currentDir = dirFromDisk(nextCluster);
        currentAllocated = 1;
        currentCluster = nextCluster;
        localIsRoot = 0;
    }

    *outDir = currentDir;
    *outCluster = currentCluster;
    *outIsRoot = localIsRoot;
    *outNeedsFree = currentAllocated;
    return 1;
}
