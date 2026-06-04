#include "SFAT.h"
#include <string.h>
#include <stdlib.h>

/* 目录栈各层对应的磁盘簇号，与 dirStack 同步维护 */
static unsigned int dirClusterStack[MAX_STACK_DEPTH];

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
    
    void *block = (char*)dir->entries + clusterOffset * CLUSTER_SIZE;
    return writeCluster(block, c, 1);
}

/* ================= 核心接口 ================= */

/* 应在系统 init() 中调用，初始化目录栈的簇号映射 */
void initDirStack(void) {
    cdi = 0;
    dirClusterStack[0] = ROOT_DIR_START_CLUSTER;
    strcpy(sfat.rootDirectory.name, "/"); // 显式初始化根目录名字
}

/**
 * 从磁盘指定簇号加载目录内容
 */
Directory* dirFromDisk(unsigned int cluster) {
    if (cluster != ROOT_DIR_START_CLUSTER && cluster < DATA_START_CLUSTER) {
        return NULL;
    }

    int maxEntries = getDirCapacity(cluster);
    if (maxEntries <= 0) return NULL;

    DirEntry *entries = (DirEntry*)malloc(maxEntries * DIRENTRY_SIZE);
    if (!entries) return NULL;

    /* 逐簇读取：区分根目录（连续区块）和数据区（FAT链） */
    if (cluster == ROOT_DIR_START_CLUSTER) {
        for (int i = 0; i < ROOT_DIR_CLUSTERS; i++) {
            char *data = readCluster(ROOT_DIR_START_CLUSTER + i, 1);
            if (!data) { free(entries); return NULL; }
            memcpy((char*)entries + i * CLUSTER_SIZE, data, CLUSTER_SIZE);
        }
    } else {
        unsigned int c = cluster;
        int clusterIdx = 0;
        while (c != FAT_EOF && c != FAT_FREE && c < MAX_CLUSTERS) {
            char *data = readCluster(c, 1);
            if (!data) { free(entries); return NULL; }
            memcpy((char*)entries + clusterIdx * CLUSTER_SIZE, data, CLUSTER_SIZE);
            clusterIdx++;
            c = sfat.fat[c];
        }
    }

    /* 统计有效目录项数 */
    int count = 0;
    for (int i = 0; i < maxEntries; i++) {
        unsigned char first = (unsigned char)entries[i].name[0];
        if (first != UNUSED && first != DELETED) {
            count++;
        }
    }

    Directory *dir = (Directory*)malloc(sizeof(Directory));
    if (!dir) {
        free(entries);
        return NULL;
    }
    dir->entries = entries;
    dir->count = count;
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
    
    Directory *baseDir = (cdi == 0) ? &sfat.rootDirectory : &sfat.dirStack[cdi];
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
    writeCluster(newEntries, newCluster, 1);
    free(newEntries);

    sfat.fat[newCluster] = FAT_EOF;
    sfat.freeClusterCount--;
    writeFAT();

    // 10. 将更新后的父目录写回磁盘
    writeDirBack(parentDir, parentCluster, freeIdx);
    parentDir->count++;

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

/**
 * 切换当前工作目录
 */
int cd(const char *path) {
    if (!path || path[0] == '\0' || strcmp(path, ".") == 0) return 0;

    // 1. 处理快捷回退 ".."
    if (strcmp(path, "..") == 0) {
        if (cdi > 0) {
            if (sfat.dirStack[cdi].entries != NULL &&
                sfat.dirStack[cdi].entries != sfat.rootDirectory.entries) {
                free(sfat.dirStack[cdi].entries);
                sfat.dirStack[cdi].entries = NULL;
            }
            cdi--; // 直接出栈，上一层的 name 已经保留在栈中了
        }
        return 0;
    }

    // 2. 解析目标路径
    Directory *baseDir = (cdi == 0) ? &sfat.rootDirectory : &sfat.dirStack[cdi];
    unsigned int baseCluster = (cdi == 0) ? ROOT_DIR_START_CLUSTER : dirClusterStack[cdi];
    Directory *targetDir;
    unsigned int targetCluster;
    int isRoot, needsFree;

    int ret = resolvePath(path, baseDir, baseCluster, cdi,
                          &targetDir, &targetCluster, &isRoot, &needsFree);
    if (ret != 0) return ret;

    // 3. 检查目标目录是否已经在栈中（防止循环或重复入栈）
    int inStack = 0;
    int stackIdx = -1;
    for (int i = 0; i <= cdi; i++) {
        if (dirClusterStack[i] == targetCluster) {
            inStack = 1;
            stackIdx = i;
            break;
        }
    }

    if (inStack) {
        cdi = stackIdx; // 直接切换指针，名字本来就在栈里
        if (needsFree) {
            free(targetDir->entries); 
            free(targetDir);
        }
    } else {
        // 4. 进入全新目录，准备入栈
        if (cdi + 1 >= MAX_STACK_DEPTH) {
            if (needsFree) { free(targetDir->entries); free(targetDir); }
            return -1;
        }
        
        cdi++;
        sfat.dirStack[cdi].entries = targetDir->entries;
        sfat.dirStack[cdi].count = targetDir->count;
        dirClusterStack[cdi] = targetCluster;

        // 【核心修改】从父目录中获取当前目录的真实名称
        unsigned int parentCluster = targetDir->entries[1].startCluster; // ".." 的簇号
        Directory *parentDir = NULL;
        int parentNeedsFree = 0;

        // 如果父目录是根目录，直接用内存里的；否则从磁盘读
        if (parentCluster == ROOT_DIR_START_CLUSTER) {
            parentDir = &sfat.rootDirectory;
            parentNeedsFree = 0;
        } else {
            parentDir = dirFromDisk(parentCluster);
            parentNeedsFree = 1;
        }

        if (parentDir) {
            int capacity = getDirCapacity(parentCluster);
            // 在父目录中遍历，寻找哪个条目指向了我们当前进入的 targetCluster
            for (int i = 0; i < capacity; i++) {
                DirEntry *e = &parentDir->entries[i];
                unsigned char first = (unsigned char)e->name[0];
                if (first != UNUSED && first != DELETED && e->startCluster == targetCluster) {
                    // 找到了！将名字拷贝到当前栈层的 name 变量中
                    strncpy(sfat.dirStack[cdi].name, e->name, 15);
                    sfat.dirStack[cdi].name[15] = '\0';
                    break;
                }
            }
            if (parentNeedsFree) {
                free(parentDir->entries);
                free(parentDir);
            }
        }

        free(targetDir); 
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

void pwd(void) {
    if (cdi == 0) {
        printf("/\n");
        return;
    }

    // 遍历目录栈，打印出完整的虚拟路径
    for (int i = 1; i <= cdi; i++) {
        printf("/%s", sfat.dirStack[i].name);
    }
    printf("\n");
}