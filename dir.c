#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include"SFAT.h"

// #define MAX_SUBDIR_ENTRIES (CLUSTER_SIZE / DIRENTRY_SIZE)

// unsigned int dirClusterStack[MAX_STACK_DEPTH];
// static int dirStackInited = 0;

// 延迟初始化目录簇栈，避免启动时未写入根目录簇号
// static void ensureDirStackInitialized() {
//     if (dirStackInited) {
//         return;
//     }
//     dirClusterStack[0] = ROOT_DIR_START_CLUSTER;
//     dirStackInited = 1;
// }

// 目录项名称匹配（固定长度字段）
static int isNameMatch(const DirEntry *entry, const char *name) {
    return strncmp(entry->name, name, MAX_FILENAME_LENGTH) == 0;
}

// 在目录中查找名称，返回索引或 -1
static int findEntryIndex(const Directory *dir, const char *name) {
    for (int i = 0; i < dir->count; i++) {
        if (isNameMatch(&dir->entries[i], name)) {
            return i;
        }
    }
    return -1;
}

// 基本名称校验：长度、特殊目录名
static int isValidName(const char *name) {
    size_t len = strlen(name);
    if (len == 0 || len >= MAX_FILENAME_LENGTH) {
        return 0;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 0;
    }
    return 1;
}

// 删除目录项后紧凑数组
static void compactDirectory(Directory *dir, int index) {
    if (index < 0 || index >= dir->count) {
        return;
    }
    for (int i = index; i < dir->count - 1; i++) {
        dir->entries[i] = dir->entries[i + 1];
    }
    dir->count--;
}

// 找到一个空闲簇并标记为 FAT_EOF
// 由fileio.c中的findFreeCluster函数替代，保持单一职责
// static unsigned int allocateCluster() {
//     for (unsigned int i = DATA_START_CLUSTER; i < MAX_CLUSTERS; i++) {
//         if (sfat.fat[i] == FAT_FREE) {
//             sfat.fat[i] = FAT_EOF;
//             return i;
//         }
//     }
//     return FAT_EOF;
// }

// 释放簇链
static void freeClusterChain(unsigned int startCluster) {
    unsigned int cluster = startCluster;
    while (cluster != FAT_EOF && cluster != FAT_FREE) {
        unsigned int next = sfat.fat[cluster];
        sfat.fat[cluster] = FAT_FREE;
        cluster = next;
    }
}

// 将目录项数组写回磁盘（固定簇数）
static void writeDirectoryToDisk(const Directory *dir, unsigned int startCluster, unsigned int clusterCount) {
    unsigned int maxEntries = clusterCount * (CLUSTER_SIZE / DIRENTRY_SIZE);
    unsigned int entriesToWrite = (dir->count > (int)maxEntries) ? maxEntries : (unsigned int)dir->count;
    char *buf = (char *)calloc(clusterCount * CLUSTER_SIZE, 1);
    for (unsigned int i = 0; i < entriesToWrite; i++) {
        memcpy(buf + i * DIRENTRY_SIZE, &dir->entries[i], sizeof(DirEntry));
    }
    writeCluster(buf, startCluster, clusterCount);
    free(buf);
}


// 从磁盘读取目录，构建目录结构并返回目录结构指针
Directory* dirFromDisk(unsigned int cluster) {
    Directory *dir = (Directory *)malloc(sizeof(Directory)); // 分配目录结构体
    // 实际单个目录项内存占用小于64字节
    dir->entries = (DirEntry *)calloc(MAX_ROOT_FILES, sizeof(DirEntry)); // 分配目录项数组并初始化为0
    dir->count = 0; // 初始化目录项数量为0

    char *buf = NULL; // 簇缓冲区
    DirEntry *entry;
    // 循环读取目录簇链，直到遇到 FAT_EOF 标志
    while (cluster != FAT_EOF) {
        buf = readCluster(cluster, 1); // 读取当前簇数据到缓冲区
        entry = (DirEntry *)buf; // 将缓冲区数据解释为目录项数组
        cluster = sfat.fat[cluster]; // 获取下一个簇号
        
        // 循环读取目录项
        while(entry->name[0] != UNUSED) {
            if (entry->name[0] == DELETED) {  // 0xE5表示已删除，跳过
                entry = (DirEntry *)((char *)entry + DIRENTRY_SIZE);
                continue;
            }
            memcpy(&dir->entries[dir->count++], entry, sizeof(DirEntry)); // 将目录项复制到目录结构体中
            entry = (DirEntry *)((char *)entry + DIRENTRY_SIZE); // 移动到下一个目录项
        }
        free(buf);
    }
    return dir;
}

// 列出目录内容
int dir(const char *path) {
    Directory *targetDir;
    int needsFree = 0;
    // 不指定参数默认当前路径，直接读取当前目录内容
    if (path == NULL || path[0] == '\0') {
        targetDir = &sfat.dirStack[cdi];
    }
    else {
        unsigned int targetCluster;
        int isRoot = (cdi == 0);
        // 解析路径并定位目标目录
        if (!resolvePath(path, targetDir, targetCluster, cdi, &targetDir, &targetCluster, &isRoot, &needsFree)) {
            logger("Path not found.", LOG_ERROR);
            return 1;
        }
    }

    printf("NAME\t\tTYPE\tSIZE\n");
    for (int i = 0; i < targetDir->count; i++) {
        DirEntry *entry = &targetDir->entries[i];
        if (entry->type == SUBDIR) {
            printf("%s\t\t<DIR>\t%u\n", entry->name, entry->size);
        } else if (entry->type == ARCHIVE) {
            // 文件显示扩展名和大小，目录显示<DIR>和包含的文件数
            if (entry->extension[0] != '\0') {
                printf("%s.%s\tFILE\t%u\n", entry->name, entry->extension, entry->size);
            } else {
                printf("%s\t\tFILE\t%u\n", entry->name, entry->size);
            }
        } else {
            printf("%s\t\tUNKNOWN\t%u\n", entry->name, entry->size);
        }
    }
    if (needsFree) {
        free(targetDir->entries);
        free(targetDir);
    }
    return 0;
}

// 创建目录
int mkdir(const char *name) {
    if (name == NULL || name[0] == '\0') {
        logger("Directory name required.", LOG_ERROR);
        return 1;
    }

    Directory *currentDir = &sfat.dirStack[cdi];
    unsigned int currentCluster;
    int isRoot = (cdi == 0);
    int needsFree = 0;

    char parentPath[256] = {0};
    char dirName[MAX_FILENAME_LENGTH] = {0};
    const char *lastSlash = strrchr(name, '/');
    const char *lastBackSlash = strrchr(name, '\\');
    const char *separator = lastSlash > lastBackSlash ? lastSlash : lastBackSlash;

    // 允许传入包含父路径的形式，例如 a/b
    if (separator != NULL) {
        size_t parentLen = separator - name;
        if (parentLen > 0 && parentLen < sizeof(parentPath)) {
            memcpy(parentPath, name, parentLen);
        }
        strncpy(dirName, separator + 1, sizeof(dirName) - 1);
        if (!resolvePath(parentPath, currentDir, currentCluster, cdi, &currentDir, &currentCluster, &isRoot, &needsFree)) {
            logger("Parent path not found.", LOG_ERROR);
            return 1;
        }
    } else {
        strncpy(dirName, name, sizeof(dirName) - 1);
    }

    if (!isValidName(dirName)) {
        logger("Invalid directory name.", LOG_ERROR);
        return 1;
    }
    if (findEntryIndex(currentDir, dirName) >= 0) {
        logger("Directory already exists.", LOG_WARNING);
        if (needsFree) {
            free(currentDir->entries);
            free(currentDir);
        }
        return 1;
    }

    if (!isRoot && currentDir->count >= MAX_SUBDIR_ENTRIES) {
        logger("Directory is full.", LOG_ERROR);
        if (needsFree) {
            free(currentDir->entries);
            free(currentDir);
        }
        return 1;
    }

    unsigned int newCluster = findFreeCluster();
    if (newCluster == FAT_EOF) {
        logger("No free clusters available.", LOG_ERROR);
        if (needsFree) {
            free(currentDir->entries);
            free(currentDir);
        }
        return 1;
    }

    DirEntry entry;
    memset(&entry, 0, sizeof(DirEntry));
    strncpy(entry.name, dirName, MAX_FILENAME_LENGTH - 1);
    entry.type = SUBDIR;
    entry.owner = currentUserID;
    entry.permission = READ | WRITE | EXEC;
    entry.size = 0;
    entry.startCluster = newCluster;
    currentDir->entries[currentDir->count++] = entry;

    if (isRoot) {
        writeRootDirectory();
    } else {
        writeDirectoryToDisk(currentDir, currentCluster, 1);
    }

    if (needsFree) {
        free(currentDir->entries);
        free(currentDir);
    }
    
    logger("Directory created.", LOG_INFO);
    return 0;
}

// 删除目录
int rmdir(const char *name) {
    ensureDirStackInitialized();
    if (name == NULL || name[0] == '\0') {
        logger("Directory name required.", LOG_ERROR);
        return 1;
    }

    Directory *currentDir = &sfat.dirStack[cdi];

    int isRoot = (cdi == 0);
    int needsFree = 0;

    char parentPath[256] = {0};
    char dirName[MAX_FILENAME_LENGTH] = {0};
    const char *lastSlash = strrchr(name, '/');
    const char *lastBackSlash = strrchr(name, '\\');
    const char *separator = lastSlash > lastBackSlash ? lastSlash : lastBackSlash;

    // 允许传入包含父路径的形式，例如 a/b
    if (separator != NULL) {
        size_t parentLen = separator - name;
        if (parentLen > 0 && parentLen < sizeof(parentPath)) {
            memcpy(parentPath, name, parentLen);
        }
        strncpy(dirName, separator + 1, sizeof(dirName) - 1);
        if (!resolvePath(parentPath, currentDir, 0, cdi, &currentDir, &currentCluster, &isRoot, &needsFree)) {
            logger("Parent path not found.", LOG_ERROR);
            return 1;
        }
    } else {
        strncpy(dirName, name, sizeof(dirName) - 1);
    }

    int index = findEntryIndex(currentDir, dirName);
    if (index < 0 || currentDir->entries[index].type != SUBDIR) {
        logger("Directory not found.", LOG_ERROR);
        if (needsFree) {
            free(currentDir->entries);
            free(currentDir);
        }
        return 1;
    }

    // 目录非空时禁止删除
    Directory *targetDir = dirFromDisk(currentDir->entries[index].startCluster);
    if (targetDir->count > 0) {
        logger("Directory not empty.", LOG_WARNING);
        free(targetDir->entries);
        free(targetDir);
        if (needsFree) {
            free(currentDir->entries);
            free(currentDir);
        }
        return 1;
    }
    free(targetDir->entries);
    free(targetDir);

    freeClusterChain(currentDir->entries[index].startCluster);
    compactDirectory(currentDir, index);

    if (isRoot) {
        writeDirectoryToDisk(currentDir, ROOT_DIR_START_CLUSTER, ROOT_DIR_CLUSTERS);
        sfat.rootDirectory = *currentDir;
        sfat.dirStack[0] = sfat.rootDirectory;
    } else {
        writeDirectoryToDisk(currentDir, currentCluster, 1);
    }
    if (needsFree) {
        free(currentDir->entries);
        free(currentDir);
    }
    logger("Directory removed.", LOG_INFO);
    return 0;
}

int upOneLevel() {
    if (cdi > 0) {
        free(sfat.dirStack[cdi].entries);   // 释放内存
        sfat.dirStack[cdi] = (Directory){0}; // 清空当前目录栈项，避免误用
        cdi--;
        return 0;
    }
    logger("Already at root directory.", LOG_WARNING);
    return 1;
}

// /home/user
// ./home
// ../home

// 切换目录
int cd(const char *path) {
    if (path == NULL || path[0] == '\0') {
        printf("Path required.\n");
        return 1;
    }

    // 切换至根目录
    if (strcmp(path, "/" || path[0] == '\\') == 0) {
        while(cdi > 0) upOneLevel();
        return 0;
    }

    // 返回上一级
    if (strcmp(path, "..") == 0) {
        return upOneLevel();
    }

    if (strcmp(path, ".") == 0) {
        return 0;
    }

    Directory *currentDir;
    int offset = 0;
    char segment[MAX_FILENAME_LENGTH];

    // 逐段解析并沿目录栈推进
    while (parsePathSegment(path, &offset, segment, sizeof(segment))) {
        if (strcmp(segment, ".") == 0) {
            continue;
        }
        if (strcmp(segment, "..") == 0) {
            upOneLevel();
        }

        currentDir = &sfat.dirStack[cdi];
        int index = findEntryIndex(currentDir, segment);
        if (index < 0 || currentDir->entries[index].type != SUBDIR) {
            logger("Directory not found.", LOG_ERROR);
            return 1;
        }

        Directory *nextDir = dirFromDisk(currentDir->entries[index].startCluster);
        strcpy(nextDir->name, segment);
        if (cdi + 1 >= MAX_STACK_DEPTH) {
            logger("Directory stack overflow.", LOG_ERROR);
            free(nextDir->entries);
            free(nextDir);
            return 1;
        }
        cdi++;
        sfat.dirStack[cdi] = *nextDir;
        free(nextDir);
    }
    return 0;
}