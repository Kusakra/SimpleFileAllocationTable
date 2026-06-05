#include "SFAT.h"
#include <string.h>
#include <stdlib.h>

/* 目录栈各层对应的磁盘簇号，与 dirStack 同步维护 */
static unsigned int dirClusterStack[MAX_STACK_DEPTH];
extern int parsePathSegment(const char *path, int *offset, char *segment, size_t segmentSize);
/* ================= 辅助函数 ================= */

/**
 * 获取指定目录簇的容量（以目录项数量计）
 * 严格区分根目录（固定大小、无FAT链）和普通子目录（使用FAT链）
 */
int getDirCapacity(unsigned int cluster) {
    if (cluster == ROOT_DIR_START_CLUSTER) {
        return MAX_ROOT_FILES;
    }
    
    int capacity = 0;
    unsigned int c = cluster;
    while (c != FAT_EOF && c != FAT_FREE && c < MAX_CLUSTERS) {
        capacity += CLUSTER_SIZE / DIRENTRY_SIZE;
        c = sfat.fat[c];
    }
    return capacity;
}
/**
 * 根据目标目录簇号重新构建目录栈
 * 例如目标目录为 /a/b/c
 * 最终构造：
 * dirStack[0] = /
 * dirStack[1] = a
 * dirStack[2] = b
 * dirStack[3] = c
 */
static int rebuildDirStack(unsigned int targetCluster)
{
    unsigned int clusterChain[MAX_STACK_DEPTH];
    char nameChain[MAX_STACK_DEPTH][MAX_FILENAME_LENGTH + 1];

    int depth = 0;
    unsigned int current = targetCluster;

    while (1)
    {
        clusterChain[depth] = current;

        if (current == ROOT_DIR_START_CLUSTER)
            break;

        Directory *dir = dirFromDisk(current);
        if (!dir)
            return -1;

        unsigned int parentCluster = dir->entries[1].startCluster;

        Directory *parentDir;

        if (parentCluster == ROOT_DIR_START_CLUSTER)
        {
            parentDir = &sfat.rootDirectory;
        }
        else
        {
            parentDir = dirFromDisk(parentCluster);
            if (!parentDir)
            {
                free(dir->entries);
                free(dir);
                return -1;
            }
        }

        int found = 0;
        int capacity = getDirCapacity(parentCluster);

        for (int i = 0; i < capacity; i++)
        {
            DirEntry *e = &parentDir->entries[i];

            unsigned char first = (unsigned char)e->name[0];

            if (first == UNUSED || first == DELETED)
                continue;

            if (e->startCluster == current)
            {
                strncpy(nameChain[depth],
                        e->name,
                        MAX_FILENAME_LENGTH);

                nameChain[depth][MAX_FILENAME_LENGTH] = '\0';

                found = 1;
                break;
            }
        }

        if (parentCluster != ROOT_DIR_START_CLUSTER)
        {
            free(parentDir->entries);
            free(parentDir);
        }

        free(dir->entries);
        free(dir);

        if (!found)
            return -1;

        current = parentCluster;

        depth++;

        if (depth >= MAX_STACK_DEPTH - 1)
            return -1;
    }

    /*
     * 释放旧栈
     */
    for (int i = 1; i <= cdi; i++)
    {
        if (sfat.dirStack[i].entries &&
            sfat.dirStack[i].entries != sfat.rootDirectory.entries)
        {
            free(sfat.dirStack[i].entries);
        }
    }

    /*
     * 重建
     */
    cdi = 0;

    dirClusterStack[0] = ROOT_DIR_START_CLUSTER;

    for (int i = depth - 1; i >= 0; i--)
    {
        unsigned int cluster = clusterChain[i];

        Directory *dir = dirFromDisk(cluster);
        if (!dir)
            return -1;

        cdi++;

        sfat.dirStack[cdi].entries = dir->entries;
        sfat.dirStack[cdi].count = dir->count;

        strncpy(sfat.dirStack[cdi].name,
                nameChain[i],
                MAX_FILENAME_LENGTH);

        sfat.dirStack[cdi].name[MAX_FILENAME_LENGTH] = '\0';

        dirClusterStack[cdi] = cluster;

        free(dir);
    }

    return 0;
}
/**
 * 将目录的特定目录项所在簇写回磁盘
 * 解决多簇目录下，只写入第一个簇导致的数据丢失问题
 */
static int writeDirBack(Directory *dir, unsigned int startCluster, int entryIdx) {
    if (startCluster == ROOT_DIR_START_CLUSTER) {
        return writeRootDirectory(); 
    }

    int epc = CLUSTER_SIZE / DIRENTRY_SIZE; // 每簇目录项数量
    int clusterOffset = entryIdx / epc;
    unsigned int c = startCluster;
    
    for (int i = 0; i < clusterOffset; i++) {
        if (c == FAT_EOF || c == FAT_FREE || c >= MAX_CLUSTERS) return -1;
        c = sfat.fat[c];
    }
    
    void *block = malloc(CLUSTER_SIZE);
    (char*)dir->entries + clusterOffset * CLUSTER_SIZE;
    unsigned int startidx = clusterOffset * epc;
    for (int i=startidx; i<=entryIdx; i++) {
        memcpy(block + (i - startidx) * DIRENTRY_SIZE, &dir->entries[i], sizeof(DirEntry));
    }
    return writeCluster(block, c, 1);
}

/* ================= 核心接口 ================= */

/* 应在系统 init() 中调用，初始化目录栈的簇号映射 */
void initDirStack(void) {
    cdi = 0;
    dirClusterStack[0] = ROOT_DIR_START_CLUSTER;
    strcpy(sfat.rootDirectory.name, "/"); // 显式初始化根目录名字
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

/**
 * 升级版 mkdir：支持相对路径和绝对路径创建
 * 例如: mkdir("home"), mkdir("/home"), mkdir("/usr/local/bin")
 */
int mkdir(const char *path) {
    if (!path || path[0] == '\0') return -1;

    // 1. 拷贝路径，避免修改原只读字符串
    char pathCopy[256];
    strncpy(pathCopy, path, sizeof(pathCopy) - 1);
    pathCopy[255] = '\0';

    // 2. 分离父路径和新目录名
    char *lastSlash = strrchr(pathCopy, '/');
    char parentPath[256] = "";
    char *newName = NULL;

    if (lastSlash == NULL) {
        // 没有斜杠，纯名称 (例如: "home")
        newName = pathCopy;
    } else if (lastSlash == pathCopy) {
        // 斜杠在最前面 (例如: "/home")
        strcpy(parentPath, "/");
        newName = lastSlash + 1;
    } else {
        // 包含深层路径 (例如: "/usr/home" 或 "usr/home")
        *lastSlash = '\0'; // 截断字符串，分离父路径
        strcpy(parentPath, pathCopy);
        newName = lastSlash + 1;
    }

    // 3. 校验新目录名合法性
    if (strlen(newName) == 0 || strlen(newName) > MAX_FILENAME_LENGTH) {
        return -1; // 名字为空或超长
    }

    // 4. 解析父目录所在位置
    Directory *parentDir;
    unsigned int parentCluster;
    int isRoot, needsFree;
    
    Directory *baseDir = &sfat.dirStack[cdi];
    unsigned int baseCluster = dirClusterStack[cdi];

    if (parentPath[0] != '\0') {
        // 如果指定了父路径，寻找它
        if (resolvePath(parentPath, baseDir, baseCluster, cdi, 
                        &parentDir, &parentCluster, &isRoot, &needsFree) != 0) {
            return -1; // 父目录不存在！
        }
    } else {
        // 没有指定父路径，就在当前工作目录创建
        parentDir = baseDir;
        parentCluster = baseCluster;
        needsFree = 0;
    }

    // 5. 在父目录中查找空闲位置并检查重名
    int capacity = getDirCapacity(parentCluster);
    int freeIdx = -1;
    for (int i = 0; i < capacity; i++) {
        unsigned char first = (unsigned char)parentDir->entries[i].name[0];
        if (first == UNUSED || first == DELETED) {
            if (freeIdx == -1) freeIdx = i;
            continue;
        }
        if (strncmp(parentDir->entries[i].name, newName, MAX_FILENAME_LENGTH) == 0) {
            if (needsFree) { free(parentDir->entries); free(parentDir); }
            return -1; // 目录已存在
        }
    }

    if (freeIdx == -1) {
        if (needsFree) { free(parentDir->entries); free(parentDir); }
        return -1; // 目录已满
    }

    // 6. 分配新簇给新目录
    unsigned int newCluster = findFreeCluster();
    if (newCluster == 0) {
        if (needsFree) { free(parentDir->entries); free(parentDir); }
        return -1; // 磁盘空间不足
    }

    // 7. 写入父目录中的条目信息
    DirEntry *entry = &parentDir->entries[freeIdx];
    memset(entry, 0, DIRENTRY_SIZE);
    strncpy(entry->name, newName, MAX_FILENAME_LENGTH);
    entry->type = SUBDIR;
    entry->owner = currentUserID;
    entry->permission = READ | WRITE | EXEC;
    entry->size = 2; // "." 和 ".."
    entry->startCluster = newCluster;

    // 8. 初始化新目录的磁盘内容（"." 和 ".."）
    int epc = CLUSTER_SIZE / DIRENTRY_SIZE;
    DirEntry *newEntries = (DirEntry*)calloc(epc, DIRENTRY_SIZE);
    if (!newEntries) {
        if (needsFree) { free(parentDir->entries); free(parentDir); }
        return -1;
    }

    strcpy(newEntries[0].name, ".");
    newEntries[0].type = SUBDIR;
    newEntries[0].owner = currentUserID;
    newEntries[0].permission = READ | WRITE | EXEC;
    newEntries[0].size = 2;
    newEntries[0].startCluster = newCluster;

    strcpy(newEntries[1].name, "..");
    newEntries[1].type = SUBDIR;
    newEntries[1].owner = entry->owner;
    newEntries[1].permission = READ | WRITE | EXEC;
    newEntries[1].size = 0; 
    newEntries[1].startCluster = parentCluster;

    
    // 9. 数据落盘
    void *buf = malloc(CLUSTER_SIZE);
    for (int i=0; i<2; i++) {
        memcpy(buf + i * DIRENTRY_SIZE, &newEntries[i], sizeof(DirEntry));
    }
    writeCluster(buf, newCluster, 1);
    free(newEntries);
    free(buf);

    sfat.fat[newCluster] = FAT_EOF;
    sfat.freeClusterCount--;
    writeFAT();

    // 10. 将更新后的父目录写回磁盘
    parentDir->count++;
    writeDirBack(parentDir, parentCluster, freeIdx);
    if (cdi == 0) {
        memcpy(&sfat.rootDirectory, parentDir, sizeof(Directory));
    }

    // 11. 释放动态分配的父目录内存（如果是从磁盘临时加载的）
    if (needsFree) {
        free(parentDir->entries);
        free(parentDir);
    }

    return 0;
}
/**
 * 删除空目录
 */
int rmdir(const char *name) {
    if (!name || name[0] == '\0') return -1;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return -1;

    Directory *current = (cdi == 0) ? &sfat.rootDirectory : &sfat.dirStack[cdi];
    unsigned int currentCluster = dirClusterStack[cdi];
    int capacity = getDirCapacity(currentCluster);

    int idx = -1;
    for (int i = 0; i < capacity; i++) {
        unsigned char first = (unsigned char)current->entries[i].name[0];
        if (first == UNUSED || first == DELETED) continue;
        
        if (strncmp(current->entries[i].name, name, MAX_FILENAME_LENGTH) == 0) {
            if (current->entries[i].type != SUBDIR) return -1;
            idx = i;
            break;
        }
    }
    if (idx == -1) return -1;

    DirEntry *entry = &current->entries[idx];

    /* 加载目标目录检查是否为空 */
    Directory *target = dirFromDisk(entry->startCluster);
    if (!target) return -1;

    if (target->count > 2) {
        free(target->entries);
        free(target);
        return -1;
    }

    /* 检查是否在当前工作路径中 */
    for (int i = 0; i <= cdi; i++) {
        if (dirClusterStack[i] == entry->startCluster) {
            free(target->entries);
            free(target);
            return -1;
        }
    }

    free(target->entries);
    free(target);

    /* 释放 FAT 簇链 */
    unsigned int c = entry->startCluster;
    int freed = 0;
    while (c != FAT_EOF && c != FAT_FREE && c < MAX_CLUSTERS) {
        unsigned int next = sfat.fat[c];
        sfat.fat[c] = FAT_FREE;
        c = next;
        freed++;
    }
    sfat.freeClusterCount += freed;
    writeFAT();

    /* 标记目录项为删除，强制转换为 unsigned char 防止溢出对比问题 */
    entry->name[0] = (char)DELETED;
    current->count--;

    writeDirBack(current, currentCluster, idx);
    return 0;
}

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

/**
 * 切换当前工作目录
 */
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

// 切换目录
int cd(const char *path) {
    if (path == NULL || path[0] == '\0') {
        printf("Path required.\n");
        return 1;
    }

    // 切换至根目录
    if (strcmp(path, "/") == 0 || path[0] == '\\') {
        while(cdi > 0) upOneLevel();
        return 0;
    }

    if (strcmp(path, ".") == 0) {
        return 0;
    }

    if (strcmp(path, "..") == 0) {
        upOneLevel();
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

        unsigned int cluster = currentDir->entries[index].startCluster;
        Directory *nextDir = dirFromDisk(cluster);
        strcpy(nextDir->name, segment);
        if (cdi + 1 >= MAX_STACK_DEPTH) {
            logger("Directory stack overflow.", LOG_ERROR);
            free(nextDir->entries);
            free(nextDir);
            return 1;
        }

        cdi++;
        dirClusterStack[cdi] = cluster;
        sfat.dirStack[cdi] = *nextDir;
        free(nextDir);
    }
    return 0;
}

/**
 * 显示目录内容
 */
int dir(const char *path) {
    Directory *targetDir;
    unsigned int targetCluster;
    int isRoot, needsFree;

    if (!path || path[0] == '\0') {
        targetDir = (cdi == 0) ? &sfat.rootDirectory : &sfat.dirStack[cdi];
        targetCluster = dirClusterStack[cdi];
        isRoot = (cdi == 0);
        needsFree = 0;
    } else {
        Directory *baseDir = (cdi == 0) ? &sfat.rootDirectory : &sfat.dirStack[cdi];
        unsigned int baseCluster = dirClusterStack[cdi];
        int ret = resolvePath(path, baseDir, baseCluster, cdi,
                              &targetDir, &targetCluster, &isRoot, &needsFree);
        if (ret != 0) return ret;
    }

    int capacity = getDirCapacity(targetCluster);

    printf("\n  Directory of cluster %u\n", targetCluster);
    printf("  %-16s %-6s %-5s %-10s %-4s\n", "Name", "Type", "Owner", "Size", "Perm");
    printf("  ------------------------------------------------\n");

    for (int i = 0; i < capacity; i++) {
        DirEntry *e = &targetDir->entries[i];
        unsigned char first = (unsigned char)e->name[0];
        if (first == UNUSED || first == DELETED) continue;

        char name[MAX_FILENAME_LENGTH + 1];
        strncpy(name, e->name, MAX_FILENAME_LENGTH);
        name[MAX_FILENAME_LENGTH] = '\0';

        const char *typeStr = (e->type == SUBDIR) ? "<DIR>" : (e->type == ARCHIVE ? "FILE" : "UNKNOWN");

        printf("  %-16s %-6s %-5d %-10u %c%c%c\n",
               name,
               typeStr,
               (unsigned char)e->owner,
               e->size,
               (e->permission & READ) ? 'r' : '-',
               (e->permission & WRITE) ? 'w' : '-',
               (e->permission & EXEC) ? 'x' : '-');
    }

    if (needsFree) {
        free(targetDir->entries);
        free(targetDir);
    }

    return 0;
}

char* pwd() {
    if (cdi == 0) {
        // 【修复】返回动态分配的内存，以便外部安全 free
        char *rootPath = (char *)malloc(2);
        strcpy(rootPath, "/");
        return rootPath;
    }

    char *path = (char *)calloc(cdi * 16 + 2, 1); // 加2防止极端情况越界
    path[0] = '\0';
    // 遍历目录栈，打印出完整的虚拟路径
    for (int i = 1; i <= cdi; i++) {
        strcat(path, "/");
        strcat(path, sfat.dirStack[i].name);
    }
    return path;
}

