#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include"SFAT.h"

// 说明：按簇读写硬盘，先在内存中构建簇数据，最后一次性写入磁盘，减少磁盘I/O次数，提高性能

// 从磁盘读取n个簇的数据并返回缓冲区指针，调用者负责释放内存
char *readCluster(unsigned int cluster, unsigned int n) {
    char *buf = (char *)malloc(n * CLUSTER_SIZE); // 分配缓冲区
    fseek(sfat.fd, cluster * CLUSTER_SIZE, SEEK_SET); // 将文件指针移动到指定簇的起始位置
    fread(buf, 1, n * CLUSTER_SIZE, sfat.fd); // 从磁盘读取n个簇的数据到缓冲区
    return buf;
}

// 将缓冲区的数据写入磁盘指定簇，n为簇数
int writeCluster(const void *buf, unsigned int cluster, unsigned int n) {
    fseek(sfat.fd, cluster * CLUSTER_SIZE, SEEK_SET); // 将文件指针移动到指定簇的起始位置
    fwrite(buf, 1, n * CLUSTER_SIZE, sfat.fd); // 将缓冲区的数据写入磁盘
    return 0;
}

// 寻找一个空闲簇，返回簇号，如果没有空闲簇则返回FAT_EOF
unsigned int findFreeCluster(void) {
    unsigned int idx;
    unsigned int found = FAT_EOF;
    unsigned int start = DATA_START_CLUSTER;

    printf("[DEBUG] findFreeCluster called: freeClusterCount=%u nextFreeCluster=%u\n",
           sfat.freeClusterCount, sfat.nextFreeCluster);

    if (sfat.nextFreeCluster >= DATA_START_CLUSTER &&
        sfat.nextFreeCluster < MAX_CLUSTERS) {
        start = sfat.nextFreeCluster;
    }

    // 从 nextFreeCluster 开始往后找
    for (idx = start; idx < MAX_CLUSTERS; idx++) {
        if (sfat.fat[idx] == FAT_FREE) {
            found = idx;
            break;
        }
    }

    // 若未找到，从数据区头部往前找
    if (found == FAT_EOF) {
        for (idx = DATA_START_CLUSTER; idx < start; idx++) {
            if (sfat.fat[idx] == FAT_FREE) {
                found = idx;
                break;
            }
        }
    }

    if (found == FAT_EOF) {
        printf("[DEBUG] findFreeCluster: no free cluster found\n");
        if (sfat.freeClusterCount != 0) {
            logger("FAT表与磁盘空闲簇计数不匹配", LOG_ERROR);
        }
        return FAT_EOF;
    }

    // 关键：标记找到的簇为链尾
    sfat.fat[found] = FAT_EOF;
    printf("[DEBUG] findFreeCluster: allocated cluster %u, set fat[%u]=FAT_EOF\n", found, found);

    // 更新状态
    if (sfat.freeClusterCount > 0) {
        sfat.freeClusterCount--;
    }

    // 更新 nextFreeCluster 为下一个空闲簇位置
    unsigned int next = found + 1;
    while (next < MAX_CLUSTERS && sfat.fat[next] != FAT_FREE) {
        next++;
    }
    sfat.nextFreeCluster = (next < MAX_CLUSTERS) ? next : FAT_EOF;

    return found;  // 返回找到的簇号，而不是 allocatedCluster
}


// 将文件数据写入磁盘，entry为文件的目录项指针，buf为文件数据缓冲区
int writeFileToDisk(DirEntry *entry, const void *buf) {
    unsigned int cluster = entry->startCluster; // 获取文件数据的起始簇号
    unsigned int size = entry->size; // 获取文件大小
    unsigned int clustersNeeded = (size + CLUSTER_SIZE - 1) / CLUSTER_SIZE; // 计算需要的簇数，size向上取整

    printf("[DEBUG] writeFileToDisk START: size=%u clustersNeeded=%u startCluster=%u\n",
           size, clustersNeeded, cluster);
    printf("[DEBUG] fat[%u]=%u\n", cluster, sfat.fat[cluster]);

    // 修复：确保起始簇标记为链尾
    if (sfat.fat[cluster] != FAT_EOF) {
        printf("[DEBUG] WARNING: fat[%u]=%u is not FAT_EOF, fixing to FAT_EOF\n", 
               cluster, sfat.fat[cluster]);
        sfat.fat[cluster] = FAT_EOF;
    }

    unsigned int *clusters = malloc(clustersNeeded * sizeof(unsigned int));
    if (!clusters) {
        logger("Memory allocation failed", LOG_ERROR);
        return -1;
    }

    int idx = 0; // 分配簇的索引
    logger("CLUSTERS ALLOCATING.", LOG_INFO);
    
    printf("[DEBUG] Starting cluster collection loop: cluster=%u clustersNeeded=%u\n", cluster, clustersNeeded);
    while (cluster != FAT_EOF) {
         // 环检测：如果 cluster 已经在本次遍历中出现过，说明 FAT 表损坏
    for (int j = 0; j < idx; j++) {
        if (clusters[j] == cluster) {
            free(clusters);
            logger("FAT table corrupted: circular chain detected", LOG_ERROR);
            return -1;
        }
    }
        printf("[DEBUG] In loop: idx=%d clustersNeeded=%u cluster=%u\n", idx, clustersNeeded, cluster);
        if (idx >= clustersNeeded) {
            printf("[DEBUG] Collected enough clusters, breaking\n");
            break;
        }
        clusters[idx++] = cluster;
        unsigned int next_cluster = sfat.fat[cluster];
        printf("[DEBUG] fat[%u]=%u next_cluster=%u\n", cluster, sfat.fat[cluster], next_cluster);
        cluster = next_cluster;
    }
    
    printf("[DEBUG] After loop: idx=%d clustersNeeded=%u\n", idx, clustersNeeded);
    // 分配簇链
    /*while (cluster != FAT_EOF) { // 循环分配簇，直到分配足够的簇或遇到FAT_EOF
        if (idx >= clustersNeeded) { // 如果已经分配足够的簇，跳出循环
            break;
        }
        clusters[idx++] = cluster; // 将当前簇号存储到数组中
        cluster = sfat.fat[cluster]; // 获取下一个簇号
    }*/
    if (idx < clustersNeeded) { // 文件变大，需要分配更多的簇
        for (int i = 0; i < clustersNeeded - idx; i++) {
            unsigned int newCluster = findFreeCluster(); // 查找一个空闲簇
            if (newCluster == FAT_EOF) { // 如果没有空闲簇，返回错误
                logger("No free cluster available.", LOG_ERROR);
                free(clusters);
                return -1;
            }
            sfat.fat[clusters[idx - 1 + i]] = newCluster; // 将前一个簇的FAT项指向新簇
            sfat.fat[newCluster] = FAT_EOF; // 将新簇的FAT项设置为FAT_EOF
            clusters[idx + i] = newCluster; // 将新簇号存储到数组中
        }
    }
    else if (cluster != FAT_EOF) {  // 文件变小，释放多余的簇
        while (cluster != FAT_EOF) { // 循环释放多余的簇，直到遇到FAT_EOF
            unsigned int nextCluster = sfat.fat[cluster]; // 获取下一个簇号
            sfat.fat[cluster] = FAT_FREE; // 将当前簇的FAT项设置为FAT_FREE，表示空闲
            cluster = nextCluster; // 移动到下一个簇
        }
    }

    // 文件写入对应簇链，优先一次性写入连续簇链以优化写入性能
    const char *data = (const char *)buf;  // 将 void* 转成 char* 以支持指针偏移
    unsigned int li = 0; // 记录当前连续区间的起点
    for (unsigned int ri = 0; ri < clustersNeeded; ri++) {
        // 触发写入的两个条件：
        // 1. 已经到达最后一个簇 (ri == clustersNeeded - 1)
        // 2. 下一个簇不连续了 (clusters[ri + 1] != clusters[ri] + 1)
        if (ri == clustersNeeded - 1 || clusters[ri + 1] != clusters[ri] + 1) {
            unsigned int count = ri - li + 1;         // 计算这一批连续了多少个簇
            unsigned int offset = li * CLUSTER_SIZE;   // 计算当前批次在内存缓冲中的偏移量
            
            // 发起单次批量 I/O 写入，使用转换后的 data 指针
            writeCluster(data + offset, clusters[li], count);
            
            // 左指针跳到下一个位置
            li = ri + 1; 
        }
    }
    
    free(clusters);  // 释放动态分配的数组
    return 0;
}

// 将FAT表写入磁盘
int writeFAT() {
    return writeCluster(sfat.fat, FAT_START_CLUSTER, FAT_CLUSTERS); // 将FAT表写入磁盘
}

int writeRootDirectory() {
    char *buf = (char *)calloc(MAX_ROOT_FILES * DIRENTRY_SIZE, 1); // 分配并初始化根目录缓冲区
    for (int i = 0; i < sfat.rootDirectory.count; i++) {
        memcpy(buf + i * DIRENTRY_SIZE, &sfat.rootDirectory.entries[i], sizeof(DirEntry)); // 将根目录项复制到缓冲区
    }
    writeCluster(buf, ROOT_DIR_START_CLUSTER, ROOT_DIR_CLUSTERS); // 将根目录写入磁盘
    free(buf); // 释放根目录缓冲区
    return 0;
}

int writeUserTable() {
    char *buf = (char *)calloc(MAX_USERS * USER_SIZE, 1); // 分配并初始化用户表缓冲区
    for (int i = 0; i < MAX_USERS; i++) {
        memcpy(buf + i * USER_SIZE, &sfat.Users[i], sizeof(User)); // 将用户表数据复制到缓冲区
    }
    writeCluster(buf, USER_TABLE_CLUSTER, 1); // 将用户表写入磁盘
    free(buf); // 释放用户表缓冲区
    return 0;
}

// 保存完整数据到磁盘
int saveToDisk() {
    // 写入FAT表
    writeFAT();

    // 写入根目录
    writeRootDirectory();

    // 写入用户表
    writeUserTable();

    logger("USER TABLE SAVED.", LOG_INFO); // 记录日志
    // 写入文件


    return 0;
}